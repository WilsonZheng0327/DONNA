// Desk node firmware — VL53L5CX multizone time-of-flight sensor + SX1262 LoRa TX.
// Board: Seeed XIAO nRF52840 + Wio-SX1262 kit.
//
// A node is deliberately dumb: measure distance, debounce it into a boolean
// "occupied", and shout the result over LoRa. It never listens, never knows
// about WiFi or Firebase, and keeps working if the hub reboots — the next
// heartbeat simply gets picked up again.
//
// Packet timing: immediately on every occupied<->free transition, plus a
// heartbeat at least every NODE_HEARTBEAT_MS so the hub can tell a free desk
// from a dead node. See shared/protocol.h and docs/04-protocol.md.

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <RadioLib.h>
#include <SparkFun_VL53L5CX_Library.h>

#include "config.h"
#include "protocol.h"

#ifndef NODE_ID
#error "Build with -DNODE_ID=n — flash via ./flash.sh <COUNTRY-SITE-OFFICE-FLOOR-DESKID>"
#endif
#if !defined(DESK_FLOOR) || !defined(DESK_ID_STR)
#error "Build with -DDESK_FLOOR and -DDESK_ID_STR — flash via ./flash.sh <desk-id>"
#endif

SX1262 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RESET, PIN_LORA_BUSY);

// The radio owns the default SPI (which is SPIM3 on this core). The VL53L5CX
// needs I2C on D6/D7 (D4/D5 are the radio's CS/RXEN on this kit), so we reuse
// the core's second I2C bus `Wire1` — it lives on TWIM1, which shares nothing
// with SPIM3 — and remap it to D6/D7 in setup(). (Wire1's default D17/D16 go to
// the unused on-board IMU.) We reuse Wire1 rather than construct our own TwoWire
// so we get the core's IRQ handler for that peripheral.
#define tofWire Wire1
SparkFun_VL53L5CX tof;
VL53L5CX_ResultsData tofFrame;  // one grid of zone readings, filled per frame

// Debounced output state + the raw evidence timers behind it.
static bool     occupied        = false;
static uint32_t presenceSinceMs = 0;   // 0 = not currently seeing presence
static uint32_t absenceSinceMs  = 0;   // 0 = not currently seeing absence
static uint16_t lastDistanceMm  = 0;

static uint32_t lastPollMs = 0;
static uint32_t lastTxMs   = 0;
static uint16_t txSeq      = 0;

// Nodes powered on together would otherwise heartbeat in lockstep and
// collide on-air forever; the per-node offset drifts them apart.
constexpr uint32_t HEARTBEAT_MS = NODE_HEARTBEAT_MS + (NODE_ID * 700UL);

static void haltWithError(const char* what, int code) {
  while (true) {
    Serial.printf("[FATAL] %s (code %d)\n", what, code);
    digitalWrite(PIN_LED, LOW);  delay(80);
    digitalWrite(PIN_LED, HIGH); delay(920);
  }
}

static void sendState() {
  DeskPacket pkt{};
  pkt.magic      = PACKET_MAGIC;
  pkt.version    = PROTOCOL_VERSION;
  pkt.nodeId     = NODE_ID;
  pkt.seq        = txSeq++;
  pkt.occupied   = occupied ? 1 : 0;
  pkt.distanceMm = lastDistanceMm;
  pkt.batteryMv  = 0;  // no battery sensing wired up yet
  // This node's address in the world — the hub files the report under
  // /{country}/{site}/{office}/{floor}/{deskId} verbatim.
  packStr(pkt.country,   sizeof pkt.country,   DESK_COUNTRY);
  packStr(pkt.site,      sizeof pkt.site,      DESK_SITE);
  packStr(pkt.office,    sizeof pkt.office,    DESK_OFFICE);
  packStr(pkt.floorCode, sizeof pkt.floorCode, DESK_FLOOR);
  packStr(pkt.deskId,    sizeof pkt.deskId,    DESK_ID_STR);

  digitalWrite(PIN_LED, LOW);
  // Blocking call, ~280 ms at SF9/125kHz for our 43-byte packet — that IS the
  // airtime, the radio really is busy modulating the whole time.
  int16_t state = radio.transmit(reinterpret_cast<uint8_t*>(&pkt), sizeof(pkt));
  digitalWrite(PIN_LED, HIGH);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.printf("[tx] seq=%u %s dist=%umm\n",
                  pkt.seq, occupied ? "OCCUPIED" : "free", pkt.distanceMm);
  } else {
    Serial.printf("[tx] failed, RadioLib code %d\n", state);
  }
  lastTxMs = millis();
}

// Dump the raw zone grid to serial — one row per line, distance in mm, or '.'
// for a zone with no valid return. Orientation may be mirrored/rotated vs the
// physical scene; for tuning you only need the magnitudes, not the exact cell.
static void dumpTofGrid(int zones) {
  const int side = (zones == 64) ? 8 : 4;
  for (int r = 0; r < side; r++) {
    Serial.print("  ");
    for (int c = 0; c < side; c++) {
      const int i = r * side + c;
      const uint8_t st = tofFrame.target_status[i];
      if (st == 5 || st == 9) Serial.printf("%5d", tofFrame.distance_mm[i]);
      else                    Serial.print("    .");
    }
    Serial.println();
  }
}

// TEMPORARY calibration dump (gated by CALIBRATION_MODE). Prints one full
// frame in as much detail as the sensor gives: the distance_mm grid, the
// matching target_status grid, and a summary line. Used to read off the
// numbers a seated person produces so thresholds can be picked. See config.h.
static void dumpCalibrationFrame(int zones) {
  const int side = (zones == 64) ? 8 : 4;

  uint16_t nearest = 0xFFFF, farthest = 0;
  int valid = 0, inWindow = 0;
  for (int i = 0; i < zones; i++) {
    const uint8_t st = tofFrame.target_status[i];
    if (st != 5 && st != 9) continue;
    valid++;
    const uint16_t d = (uint16_t)tofFrame.distance_mm[i];
    if (d < nearest)  nearest  = d;
    if (d > farthest) farthest = d;
    if (d >= TOF_MIN_MM && d <= TOF_OCCUPIED_MM) inWindow++;
  }

  Serial.println("[calib] distance_mm grid (. = no valid target):");
  for (int r = 0; r < side; r++) {
    Serial.print("  ");
    for (int c = 0; c < side; c++) {
      const int i = r * side + c;
      const uint8_t st = tofFrame.target_status[i];
      if (st == 5 || st == 9) Serial.printf("%6d", tofFrame.distance_mm[i]);
      else                    Serial.print("     .");
    }
    Serial.println();
  }
  Serial.println("[calib] target_status grid (5/9 = valid):");
  for (int r = 0; r < side; r++) {
    Serial.print("  ");
    for (int c = 0; c < side; c++) {
      Serial.printf("%6d", tofFrame.target_status[r * side + c]);
    }
    Serial.println();
  }
  Serial.printf("[calib] valid=%2d/%d  nearest=%4u mm  farthest=%4u mm  in[%u-%u]=%2d\n\n",
                valid, zones,
                (nearest == 0xFFFF ? 0 : nearest), farthest,
                TOF_MIN_MM, TOF_OCCUPIED_MM, inWindow);
}

// One sensor frame -> presence/absence evidence -> maybe flip `occupied`.
// The VL53L5CX reports a whole grid of zones; we reduce it to a boolean:
// presence = at least TOF_MIN_ZONES zones see something inside the window.
// lastDistanceMm reports the nearest valid zone, for tuning/telemetry.
static bool updateOccupancy() {
  if (!tof.isDataReady()) return false;   // no fresh frame yet; nothing changes
  tof.getRangingData(&tofFrame);

  const int zones = (TOF_RESOLUTION == 64) ? 64 : 16;
  uint16_t nearest = 0xFFFF;
  int inRange = 0, valid = 0;
  for (int i = 0; i < zones; i++) {
    // target_status 5 (100% valid) and 9 (50% valid) are the usable returns;
    // everything else is no-target/too-noisy and is ignored.
    uint8_t st = tofFrame.target_status[i];
    if (st != 5 && st != 9) continue;
    valid++;
    uint16_t d = (uint16_t)tofFrame.distance_mm[i];
    if (d < nearest) nearest = d;
    if (d >= TOF_MIN_MM && d <= TOF_OCCUPIED_MM) inRange++;
  }
  if (nearest != 0xFFFF) lastDistanceMm = nearest;

  bool presence = inRange >= TOF_MIN_ZONES;

  // Live tuning telemetry: prints every TOF_DEBUG_MS even when the debounced
  // state is steady, so you can watch the numbers as you move around. The
  // window shown is exactly the [TOF_MIN_MM, TOF_OCCUPIED_MM] band that counts.
  static uint32_t lastDebugMs = 0;
  if (TOF_DEBUG && millis() - lastDebugMs >= TOF_DEBUG_MS) {
    lastDebugMs = millis();
    Serial.printf("[tof] nearest=%4u mm  inrange[%u-%u]=%2d  valid=%2d/%d  presence=%d occ=%d\n",
                  (nearest == 0xFFFF ? 0 : nearest), TOF_MIN_MM, TOF_OCCUPIED_MM,
                  inRange, valid, zones, presence ? 1 : 0, occupied ? 1 : 0);
    if (TOF_DEBUG_GRID) dumpTofGrid(zones);
  }

  uint32_t now = millis();

  if (presence) {
    absenceSinceMs = 0;
    if (presenceSinceMs == 0) presenceSinceMs = now;
    if (!occupied && now - presenceSinceMs >= OCCUPY_AFTER_MS) {
      occupied = true;
      Serial.printf("[occ] OCCUPIED (nearest %u mm, %d zones)\n", nearest, inRange);
      return true;
    }
  } else {
    presenceSinceMs = 0;
    if (absenceSinceMs == 0) absenceSinceMs = now;
    if (occupied && now - absenceSinceMs >= VACANT_AFTER_MS) {
      occupied = false;
      Serial.println("[occ] free");
      return true;
    }
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) {}

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);

  Serial.printf("\n=== DONNA node %u — desk %s/%s/%s/%s/%s ===\n", NODE_ID,
                DESK_COUNTRY, DESK_SITE, DESK_OFFICE, DESK_FLOOR, DESK_ID_STR);

  // ToF sensor first — fail loud if the I2C wiring is loose. The VL53L5CX
  // wants a 400 kHz bus; at 100 kHz an 8x8 frame barely keeps up. begin()
  // uploads the sensor's firmware and can take a couple of seconds.
  tofWire.setPins(PIN_I2C_SDA, PIN_I2C_SCL);  // move Wire1 off the IMU onto D7/D6
  tofWire.begin();
  tofWire.setClock(400000);
  if (!tof.begin(0x29, tofWire)) {
    haltWithError("VL53L5CX not found on I2C - check SDA/SCL (D6/D7) + 3V3", 0);
  }
  tof.setResolution(TOF_RESOLUTION);        // 16 (4x4) or 64 (8x8) zones
  tof.setRangingFrequency(TOF_RANGING_HZ);
  tof.startRanging();                        // free-running; poll isDataReady()

  // RF switch: DIO2 flips the TX side automatically during transmit (enabled
  // just after radio.begin below). RXEN gates the RX side and is held LOW —
  // a node only ever transmits, so the receive path stays off.
  pinMode(PIN_LORA_RXEN, OUTPUT);
  digitalWrite(PIN_LORA_RXEN, LOW);

  SPI.begin();  // nRF52840 uses the variant's fixed SPI pins (D8/D9/D10)
  int16_t state = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                              LORA_SYNC_WORD, LORA_TX_DBM,
                              LORA_PREAMBLE_SYMBOLS, LORA_TCXO_VOLTS);
  if (state != RADIOLIB_ERR_NONE) {
    haltWithError("SX1262 init failed (-707 = TCXO/no clock, -2 = wiring)", state);
  }
  radio.setDio2AsRfSwitch(true);

  Serial.printf("[lora] ready, heartbeat every %lu ms\n", (unsigned long)HEARTBEAT_MS);
  sendState();  // announce ourselves immediately on boot
}

void loop() {
  uint32_t now = millis();
  bool changed = false;

  // TEMPORARY: for the first CALIBRATION_MS after boot, dump every fresh frame
  // in full and skip the normal occupancy/TX path so the log stays readable.
  // The window starts on the first loop() so it runs after sensor init, giving
  // a clean CALIBRATION_MS of readings. See config.h CALIBRATION_MODE.
  if (CALIBRATION_MODE) {
    static uint32_t calibStartMs = 0;
    static bool     calibDone    = false;
    if (!calibDone) {
      if (calibStartMs == 0) {
        calibStartMs = now;
        Serial.printf("\n[calib] === calibration burst: dumping frames for %lu ms ===\n"
                      "[calib] sit in the chair now; read off the seated distances/zones.\n\n",
                      (unsigned long)CALIBRATION_MS);
      }
      if (now - calibStartMs < CALIBRATION_MS) {
        const int zones = (TOF_RESOLUTION == 64) ? 64 : 16;
        if (tof.isDataReady()) {
          tof.getRangingData(&tofFrame);
          Serial.printf("[calib] t=%5lu ms\n", (unsigned long)(now - calibStartMs));
          dumpCalibrationFrame(zones);
        }
        return;  // no occupancy logic, no TX during the burst
      }
      calibDone = true;
      Serial.println("[calib] === calibration burst done; resuming normal operation ===");
      lastTxMs = now;  // heartbeat from here, not from boot
    }
  }

  if (now - lastPollMs >= SENSOR_POLL_MS) {
    lastPollMs = now;
    changed = updateOccupancy();
  }

  if (changed || now - lastTxMs >= HEARTBEAT_MS) {
    sendState();
  }
}
