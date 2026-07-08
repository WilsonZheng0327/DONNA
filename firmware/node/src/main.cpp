// Desk node firmware — VL53L0X time-of-flight sensor + SX1262 LoRa TX.
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
#include <VL53L0X.h>

#include "config.h"
#include "protocol.h"

#ifndef NODE_ID
#error "Build with -DNODE_ID=n — use the node1/node2/node3 envs in platformio.ini"
#endif
#if !defined(DESK_FLOOR) || !defined(DESK_ID_STR)
#error "Build with -DDESK_FLOOR and -DDESK_ID_STR — see the envs in platformio.ini"
#endif

SX1262 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RESET, PIN_LORA_BUSY);
VL53L0X tof;

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
  // Blocking call, ~180 ms at SF9/125kHz for our 13 bytes — that IS the
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

// One sensor reading -> presence/absence evidence -> maybe flip `occupied`.
static bool updateOccupancy() {
  uint16_t d = tof.readRangeContinuousMillimeters();
  bool valid = !tof.timeoutOccurred() && d < 8000;  // >=8000 = out of range
  if (valid) lastDistanceMm = d;

  bool presence = valid && d >= TOF_MIN_MM && d <= TOF_OCCUPIED_MM;
  uint32_t now = millis();

  if (presence) {
    absenceSinceMs = 0;
    if (presenceSinceMs == 0) presenceSinceMs = now;
    if (!occupied && now - presenceSinceMs >= OCCUPY_AFTER_MS) {
      occupied = true;
      Serial.printf("[occ] OCCUPIED (%u mm)\n", d);
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

  // ToF sensor first — fail loud if the Grove cable is loose.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  tof.setTimeout(500);
  if (!tof.init()) {
    haltWithError("VL53L0X not found on I2C - check the Grove/I2C cable", 0);
  }
  // Free-running mode: the sensor re-measures itself every SENSOR_POLL_MS
  // and we just collect the latest result.
  tof.startContinuous(SENSOR_POLL_MS);

  pinMode(PIN_LORA_ANT_SW, OUTPUT);
  digitalWrite(PIN_LORA_ANT_SW, HIGH);  // power the antenna's TX/RX switch

  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
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

  if (now - lastPollMs >= SENSOR_POLL_MS) {
    lastPollMs = now;
    changed = updateOccupancy();
  }

  if (changed || now - lastTxMs >= HEARTBEAT_MS) {
    sendState();
  }
}
