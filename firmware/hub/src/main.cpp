// Hub firmware — Seeed XIAO ESP32S3 + Wio-SX1262 kit.
//
// The hub's whole job:
//   1. sit in LoRa receive mode and collect DeskPacket broadcasts from nodes
//   2. mirror the latest state of every desk into Firebase over WiFi/HTTPS
//   3. write its own heartbeat so the dashboard can tell "hub is down" apart
//      from "all desks are quiet"
//
//   node --LoRa 915 MHz--> [hub] --HTTPS PATCH--> Firebase RTDB --> dashboard
//
// Wiring background: docs/01-hardware.md. Radio settings: shared/protocol.h.

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <RadioLib.h>
#include <ArduinoJson.h>

#include "config.h"
#include "protocol.h"

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  // Builds and runs the LoRa side without credentials; it just can't upload.
  #warning "include/secrets.h missing - copy secrets.h.example. WiFi/Firebase disabled."
  #define WIFI_SSID     ""
  #define WIFI_PASSWORD ""
  #define FIREBASE_HOST ""
  #define FIREBASE_AUTH ""
#endif

// RadioLib drives the SX1262's registers over SPI; we hand it the four
// control lines and it owns them from here on.
SX1262 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RESET, PIN_LORA_BUSY);

// Set by the radio's DIO1 interrupt. volatile: it changes outside normal
// program flow. IRAM_ATTR: the handler must live in RAM because flash can be
// mid-operation when the interrupt fires.
static volatile bool packetArrived = false;
static void IRAM_ATTR onPacketArrived() { packetArrived = true; }

// Last known state of every desk, indexed by nodeId. `dirty` marks entries
// Firebase hasn't seen yet; the loop uploads at most one per pass so a slow
// HTTPS request never starves radio servicing for long.
struct DeskState {
  bool       seen  = false;
  bool       dirty = false;
  DeskPacket pkt{};
  float      rssi = 0, snr = 0;
  uint32_t   lastRxMs = 0;
  uint32_t   rxCount  = 0;
};
static DeskState desks[MAX_NODE_ID + 1];

static WiFiClientSecure tls;
static HTTPClient http;

static uint32_t lastHeartbeatMs   = 0;
static uint32_t lastWifiAttemptMs = 0;
static uint32_t lastPushAttemptMs = 0;
static uint32_t ledOffAtMs        = 0;
static bool     wasConnected      = false;

static bool cloudConfigured() {
  return strlen(WIFI_SSID) > 0 && strlen(FIREBASE_HOST) > 0;
}

static void blinkLed() {
  digitalWrite(PIN_LED, LOW);            // active-low: LOW = on
  ledOffAtMs = millis() + 40;
}

static void serviceLed() {
  if (ledOffAtMs != 0 && (int32_t)(millis() - ledOffAtMs) >= 0) {
    digitalWrite(PIN_LED, HIGH);
    ledOffAtMs = 0;
  }
}

// Radio init failed = nothing else can work. Print the RadioLib error code
// forever (look it up in docs/03-flashing.md troubleshooting) and blink fast.
static void haltWithRadioError(int16_t code) {
  while (true) {
    Serial.printf("[FATAL] SX1262 init failed, RadioLib code %d "
                  "(-707 = TCXO/no clock, -2 = wrong wiring/chip absent)\n", code);
    digitalWrite(PIN_LED, LOW);  delay(80);
    digitalWrite(PIN_LED, HIGH); delay(80);
    digitalWrite(PIN_LED, LOW);  delay(80);
    digitalWrite(PIN_LED, HIGH); delay(760);
  }
}

// One PATCH to https://<host><path>.json — RTDB's REST API. PATCH merges
// fields instead of replacing the whole object like PUT would.
static int firebasePatch(const String& path, const String& body) {
  String url = String("https://") + FIREBASE_HOST + path + ".json";
  if (strlen(FIREBASE_AUTH) > 0) url += String("?auth=") + FIREBASE_AUTH;
  http.begin(tls, url);
  http.addHeader("Content-Type", "application/json");
  int code = http.sendRequest("PATCH", body);
  http.end();  // setReuse(true) keeps the TLS session alive between calls
  return code;
}

static void pushDesk(uint8_t nodeId) {
  DeskState& d = desks[nodeId];
  JsonDocument doc;
  doc["nodeId"]     = d.pkt.nodeId;
  doc["occupied"]   = d.pkt.occupied != 0;
  doc["distanceMm"] = d.pkt.distanceMm;
  doc["batteryMv"]  = d.pkt.batteryMv;
  doc["seq"]        = d.pkt.seq;
  doc["rssi"]       = d.rssi;
  doc["snr"]        = d.snr;
  // ".sv" is an RTDB "server value": the server stamps epoch-ms on arrival.
  // Immune to this MCU having no real-time clock.
  doc["lastSeenAt"][".sv"] = "timestamp";

  String body;
  serializeJson(doc, body);
  int code = firebasePatch(String("/desks/node-") + nodeId, body);
  if (code == 200) {
    d.dirty = false;
  } else {
    Serial.printf("[fb] push node-%u failed: HTTP %d (will retry)\n", nodeId, code);
  }
}

static void drainOneDirtyDesk() {
  // After a failure, back off briefly instead of hammering the network.
  if ((int32_t)(millis() - lastPushAttemptMs) < 500) return;
  for (uint8_t id = 0; id <= MAX_NODE_ID; id++) {
    if (desks[id].dirty) {
      lastPushAttemptMs = millis();
      pushDesk(id);
      return;
    }
  }
}

static void sendHubHeartbeat() {
  JsonDocument doc;
  doc["lastSeenAt"][".sv"] = "timestamp";
  doc["ip"]       = WiFi.localIP().toString();
  doc["wifiRssi"] = WiFi.RSSI();
  doc["uptimeS"]  = millis() / 1000;
  String body;
  serializeJson(doc, body);
  int code = firebasePatch("/hub", body);
  Serial.printf("[fb] heartbeat: HTTP %d\n", code);
}

static void handlePacket() {
  DeskPacket pkt;
  size_t len = radio.getPacketLength();
  int16_t state = radio.readData(reinterpret_cast<uint8_t*>(&pkt), sizeof(pkt));
  float rssi = radio.getRSSI();
  float snr  = radio.getSNR();
  // Back to listening ASAP — the SX1262 can only buffer one packet.
  radio.startReceive();

  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[lora] readData error %d\n", state);
    return;
  }
  // Anything can key a 915 MHz receiver; only accept exactly our packets.
  if (len != sizeof(DeskPacket) || pkt.magic != PACKET_MAGIC) return;
  if (pkt.version != PROTOCOL_VERSION) {
    Serial.printf("[lora] node %u speaks protocol v%u, we are v%u - reflash it\n",
                  pkt.nodeId, pkt.version, PROTOCOL_VERSION);
    return;
  }
  if (pkt.nodeId > MAX_NODE_ID) return;

  DeskState& d = desks[pkt.nodeId];
  // seq is uint16 and so is this subtraction — wraparound-safe.
  uint16_t lost = d.seen ? (uint16_t)(pkt.seq - d.pkt.seq - 1) : 0;
  d.pkt = pkt;
  d.rssi = rssi;
  d.snr  = snr;
  d.lastRxMs = millis();
  d.seen  = true;
  d.dirty = true;
  d.rxCount++;

  blinkLed();
  Serial.printf("[lora] node %u seq=%u %s dist=%umm rssi=%.1fdBm snr=%.1fdB%s\n",
                pkt.nodeId, pkt.seq, pkt.occupied ? "OCCUPIED" : "free",
                pkt.distanceMm, rssi, snr,
                lost ? (String(" (") + lost + " lost)").c_str() : "");
}

static void serviceWifi() {
  if (!cloudConfigured()) return;
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    Serial.printf("[wifi] connected, ip=%s rssi=%d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }
  if (!connected && (int32_t)(millis() - lastWifiAttemptMs) > (int32_t)WIFI_RETRY_MS) {
    lastWifiAttemptMs = millis();
    Serial.println("[wifi] connecting...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  wasConnected = connected;
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) {}  // wait for USB, but boot headless too

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);  // off

  Serial.println("\n=== deskfinder hub ===");
  Serial.printf("LoRa %.1f MHz, BW %.0f kHz, SF%u, CR4/%u, sync 0x%02X\n",
                LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC_WORD);

  // Without this the RF switch between antenna and radio is unpowered and
  // no packet ever makes it in or out, even though SPI looks healthy.
  pinMode(PIN_LORA_ANT_SW, OUTPUT);
  digitalWrite(PIN_LORA_ANT_SW, HIGH);

  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
  int16_t state = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                              LORA_SYNC_WORD, LORA_TX_DBM,
                              LORA_PREAMBLE_SYMBOLS, LORA_TCXO_VOLTS);
  if (state != RADIOLIB_ERR_NONE) haltWithRadioError(state);
  // On the Wio-SX1262 the radio's own DIO2 pin steers the TX/RX antenna
  // switch — the radio flips it automatically once told.
  radio.setDio2AsRfSwitch(true);

  radio.setPacketReceivedAction(onPacketArrived);
  state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) haltWithRadioError(state);
  Serial.println("[lora] listening");

  if (cloudConfigured()) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastWifiAttemptMs = millis();
    // Prototype tradeoff: skip TLS certificate verification (no CA bundle /
    // trusted clock on this MCU yet). Traffic is still encrypted, but the
    // server isn't authenticated. docs/05-firebase.md covers doing it right.
    tls.setInsecure();
    http.setReuse(true);
    Serial.printf("[wifi] connecting to '%s'...\n", WIFI_SSID);
  } else {
    Serial.println("[wifi] no secrets.h - running radio-only (packets print below)");
  }
}

void loop() {
  serviceLed();
  if (packetArrived) {
    packetArrived = false;
    handlePacket();
  }
  serviceWifi();
  if (cloudConfigured() && WiFi.status() == WL_CONNECTED) {
    drainOneDirtyDesk();
    if ((int32_t)(millis() - lastHeartbeatMs) > (int32_t)HUB_HEARTBEAT_MS) {
      lastHeartbeatMs = millis();
      sendHubHeartbeat();
    }
  }
}
