// Hub firmware — Seeed XIAO ESP32S3 + Wio-SX1262 kit.
//
// The hub's whole job:
//   1. sit in LoRa receive mode and collect DeskPacket broadcasts from nodes
//   2. mirror each desk into Firebase under the team schema
//        /{country}/{site}/{office}/{floor}/{deskId}
//      e.g. /US/SVL/CRBN100/4/4T434G  ->  { occupied, last_updated, ... }
//   3. write its own /hub heartbeat so the dashboard can tell "hub is down"
//      apart from "all desks are quiet"
//
//   node --LoRa 915 MHz--> [hub] --HTTPS PATCH--> Firebase RTDB --> dashboard
//
// The hub is a stateless translator: every packet carries its own location
// (see shared/protocol.h), so adding/moving desks never touches hub firmware.
// last_updated is epoch SECONDS to match the team's existing records, which
// is why this firmware syncs NTP before it is allowed to upload anything.

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <RadioLib.h>
#include <ArduinoJson.h>
#include <time.h>

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
  String     fbPath;      // "/US/SVL/CRBN100/4/4T434G", built from the packet
  String     deskId;      // trimmed, for logs
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
static bool     timeSyncAnnounced = false;

static bool          realPacketSeen = false;  // first real packet retires the demo
static unsigned long totalRxCount   = 0;
static uint32_t      lastDemoMs     = 0;
static uint16_t      demoSeq        = 0;
static bool          demoOccupied   = false;
static bool          demoCleanupPending = false;
static uint32_t      lastCleanupAttemptMs = 0;
static uint32_t      lastTickerMs   = 0;

static bool demoActive() { return DEMO_DESK_ENABLED && !realPacketSeen; }

static bool cloudConfigured() {
  return strlen(WIFI_SSID) > 0 && strlen(FIREBASE_HOST) > 0;
}

// SNTP runs in the background once configTime() is called; the clock jumping
// past 2023 is how we know it has synced. Until then we must not upload —
// last_updated would be seconds-since-1970 ≈ 0, i.e. garbage.
static bool timeSynced() { return time(nullptr) > 1700000000UL; }

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

static String firebaseUrl(const String& path) {
  String url = String("https://") + FIREBASE_HOST + path + ".json";
  if (strlen(FIREBASE_AUTH) > 0) url += String("?auth=") + FIREBASE_AUTH;
  return url;
}

// One PATCH to https://<host><path>.json — RTDB's REST API. PATCH merges
// fields instead of replacing the whole object like PUT would.
static int firebasePatch(const String& path, const String& body) {
  http.begin(tls, firebaseUrl(path));
  http.addHeader("Content-Type", "application/json");
  int code = http.sendRequest("PATCH", body);
  http.end();  // setReuse(true) keeps the TLS session alive between calls
  return code;
}

static int firebaseDelete(const String& path) {
  http.begin(tls, firebaseUrl(path));
  int code = http.sendRequest("DELETE");
  http.end();
  return code;
}

// Location fields from the packet become part of a database path written
// with admin rights, so only characters that cannot alter the path structure
// are allowed through. A corrupt (or hostile) packet with "/" or "." in a
// field gets dropped here, not written somewhere surprising.
static bool sanitizeField(const char* src, size_t rawLen, String& out) {
  out = "";
  for (size_t i = 0; i < rawLen && src[i] != '\0'; i++) {
    char c = src[i];
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) return false;
    out += c;
  }
  return out.length() > 0;
}

// Shared by real radio traffic and the self-test desk, so the demo exercises
// the exact same table + Firebase path a real node would.
static void recordPacket(const DeskPacket& pkt, float rssi, float snr,
                         const String& fbPath, const String& deskId) {
  DeskState& d = desks[pkt.nodeId];
  d.pkt = pkt;
  d.fbPath = fbPath;
  d.deskId = deskId;
  d.rssi = rssi;
  d.snr  = snr;
  d.lastRxMs = millis();
  d.seen  = true;
  d.dirty = true;
  d.rxCount++;
}

static void pushDesk(uint8_t nodeId) {
  DeskState& d = desks[nodeId];
  JsonDocument doc;
  // Team schema fields first — same names and units (epoch seconds) as the
  // records already in this database.
  doc["occupied"]     = d.pkt.occupied != 0;
  doc["last_updated"] = (uint32_t)time(nullptr);
  // Our extra telemetry, for tuning and diagnostics.
  doc["distance_mm"]  = d.pkt.distanceMm;
  doc["battery_mv"]   = d.pkt.batteryMv;
  doc["seq"]          = d.pkt.seq;
  doc["node_id"]      = d.pkt.nodeId;
  doc["rssi"]         = d.rssi;
  doc["snr"]          = d.snr;

  String body;
  serializeJson(doc, body);
  int code = firebasePatch(d.fbPath, body);
  if (code == 200) {
    d.dirty = false;
  } else {
    Serial.printf("[fb] push %s failed: HTTP %d (will retry)\n",
                  d.fbPath.c_str(), code);
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
  doc["last_updated"] = (uint32_t)time(nullptr);
  doc["ip"]        = WiFi.localIP().toString();
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["uptime_s"]  = millis() / 1000;
  String body;
  serializeJson(doc, body);
  int code = firebasePatch("/hub", body);
  Serial.printf("[fb] heartbeat: HTTP %d\n", code);
}

static String demoPath() {
  return String("/") + DEMO_COUNTRY + "/" + DEMO_SITE + "/" + DEMO_OFFICE +
         "/" + DEMO_FLOOR + "/" + DEMO_DESK_ID;
}

static void serviceDemoDesk() {
  if (!demoActive()) return;
  if ((int32_t)(millis() - lastDemoMs) < (int32_t)DEMO_DESK_INTERVAL_MS) return;
  lastDemoMs = millis();
  demoOccupied = !demoOccupied;

  DeskPacket pkt{};
  pkt.magic      = PACKET_MAGIC;
  pkt.version    = PROTOCOL_VERSION;
  pkt.nodeId     = DEMO_DESK_NODE_ID;
  pkt.seq        = demoSeq++;
  pkt.occupied   = demoOccupied ? 1 : 0;
  pkt.distanceMm = demoOccupied ? 615 : 1830;  // plausible sat-down / empty readings
  packStr(pkt.country,   sizeof pkt.country,   DEMO_COUNTRY);
  packStr(pkt.site,      sizeof pkt.site,      DEMO_SITE);
  packStr(pkt.office,    sizeof pkt.office,    DEMO_OFFICE);
  packStr(pkt.floorCode, sizeof pkt.floorCode, DEMO_FLOOR);
  packStr(pkt.deskId,    sizeof pkt.deskId,    DEMO_DESK_ID);

  recordPacket(pkt, 0, 0, demoPath(), DEMO_DESK_ID);
  Serial.printf("[demo] self-test desk %s -> %s (retires when a real node is heard)\n",
                demoPath().c_str(), demoOccupied ? "OCCUPIED" : "free");
}

static void serviceDemoCleanup() {
  if (!demoCleanupPending) return;
  if ((int32_t)(millis() - lastCleanupAttemptMs) < 3000) return;
  lastCleanupAttemptMs = millis();
  if (firebaseDelete(demoPath()) == 200) {
    demoCleanupPending = false;
    Serial.println("[demo] self-test desk removed from Firebase");
  }
}

static void serviceStatusTicker() {
  if ((int32_t)(millis() - lastTickerMs) < (int32_t)STATUS_TICKER_MS) return;
  lastTickerMs = millis();
  String wifiState = !cloudConfigured()   ? String("no secrets.h")
                     : WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString()
                                                     : String("connecting");
  Serial.printf("[status] up=%lus wifi=%s time=%s lora_rx=%lu demo=%s heap=%lu\n",
                millis() / 1000, wifiState.c_str(),
                timeSynced() ? "synced" : "no-ntp",
                totalRxCount, demoActive() ? "on" : "off",
                (unsigned long)ESP.getFreeHeap());
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

  String country, site, office, floorCode, deskId;
  if (!sanitizeField(pkt.country,   sizeof pkt.country,   country)   ||
      !sanitizeField(pkt.site,      sizeof pkt.site,      site)      ||
      !sanitizeField(pkt.office,    sizeof pkt.office,    office)    ||
      !sanitizeField(pkt.floorCode, sizeof pkt.floorCode, floorCode) ||
      !sanitizeField(pkt.deskId,    sizeof pkt.deskId,    deskId)) {
    Serial.printf("[lora] node %u: bad location field, dropped\n", pkt.nodeId);
    return;
  }
  String path = "/" + country + "/" + site + "/" + office + "/" + floorCode + "/" + deskId;

  if (!realPacketSeen && demoActive() && demoSeq > 0) {
    Serial.println("[demo] real node heard - self-test desk retired");
    demoCleanupPending = true;  // remove the fake desk from Firebase too
  }
  realPacketSeen = true;
  totalRxCount++;

  DeskState& d = desks[pkt.nodeId];
  // seq is uint16 and so is this subtraction — wraparound-safe.
  uint16_t lost = d.seen ? (uint16_t)(pkt.seq - d.pkt.seq - 1) : 0;
  recordPacket(pkt, rssi, snr, path, deskId);

  blinkLed();
  Serial.printf("[lora] %s (node %u) seq=%u %s dist=%umm rssi=%.1fdBm snr=%.1fdB%s\n",
                path.c_str(), pkt.nodeId, pkt.seq,
                pkt.occupied ? "OCCUPIED" : "free",
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
  Serial.printf("LoRa %.1f MHz, BW %.0f kHz, SF%u, CR4/%u, sync 0x%02X, proto v%u\n",
                LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC_WORD,
                PROTOCOL_VERSION);

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
    // Background SNTP: the clock is needed because last_updated is epoch
    // seconds (team schema) and this MCU boots thinking it's 1970.
    configTime(0, 0, "pool.ntp.org", "time.google.com");
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
  serviceDemoDesk();
  serviceStatusTicker();

  if (cloudConfigured() && WiFi.status() == WL_CONNECTED) {
    if (!timeSynced()) return;  // no uploads until last_updated can be real
    if (!timeSyncAnnounced) {
      timeSyncAnnounced = true;
      Serial.printf("[time] NTP synced: %lu\n", (unsigned long)time(nullptr));
    }
    serviceDemoCleanup();
    drainOneDirtyDesk();
    if ((int32_t)(millis() - lastHeartbeatMs) > (int32_t)HUB_HEARTBEAT_MS) {
      lastHeartbeatMs = millis();
      sendHubHeartbeat();
    }
  }
}
