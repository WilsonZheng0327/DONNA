#pragma once

// ---------------------------------------------------------------------------
// XIAO ESP32S3 <-> Wio-SX1262 wiring (fixed by the board-to-board connector).
// These are ESP32-S3 GPIO numbers, verified against Seeed's schematic and the
// Meshtastic port for this exact kit. See docs/01-hardware.md for the roles.
// ---------------------------------------------------------------------------
constexpr int PIN_LORA_SCK   = 7;   // SPI clock
constexpr int PIN_LORA_MISO  = 8;   // SPI radio -> MCU
constexpr int PIN_LORA_MOSI  = 9;   // SPI MCU -> radio
constexpr int PIN_LORA_NSS   = 41;  // SPI chip select (active low)
constexpr int PIN_LORA_DIO1  = 39;  // radio interrupt line: "packet arrived"
constexpr int PIN_LORA_RESET = 42;  // hard reset into the radio
constexpr int PIN_LORA_BUSY  = 40;  // radio "still working, don't talk to me"
// Must be driven HIGH to power the antenna's TX/RX switch. Forget this and
// both radios "work" (SPI answers fine) but the antenna is disconnected.
constexpr int PIN_LORA_ANT_SW = 38;

// XIAO ESP32S3 user LED (yellow, next to USB). Wired active-LOW:
// LOW = on. We blink it on every received packet.
constexpr int PIN_LED = 21;

// How often the hub writes its own liveness record to /hub in Firebase.
// The dashboard uses this to show "hub offline" instead of stale desk data.
constexpr uint32_t HUB_HEARTBEAT_MS = 20000;

constexpr uint32_t WIFI_RETRY_MS = 8000;

// Hub self-test: until the first REAL node packet arrives over LoRa, the hub
// invents a fake desk and flips it occupied/free on this interval, pushed
// through the exact same code path as radio traffic. Lets you verify
// hub -> WiFi -> Firebase -> dashboard with zero node hardware. The fake
// desk lives at /US/SVL/CRBN100/4/_SELFTEST — obviously named, and deleted
// automatically once real node traffic arrives.
constexpr bool     DEMO_DESK_ENABLED     = true;
constexpr uint8_t  DEMO_DESK_NODE_ID     = 0;
constexpr uint32_t DEMO_DESK_INTERVAL_MS = 10000;
#define DEMO_COUNTRY "US"
#define DEMO_SITE    "SVL"
#define DEMO_OFFICE  "CRBN100"
#define DEMO_FLOOR   "4"
#define DEMO_DESK_ID "_SELFTEST"

// Proof-of-life line on the serial monitor — works before WiFi is even
// configured, so it's the very first thing to check after flashing.
constexpr uint32_t STATUS_TICKER_MS = 10000;

// How many distinct desks the hub tracks at once. Desks are keyed by their
// location path (not by nodeId), so this just bounds the state table; raise it
// if a single hub ever serves more desks than this.
constexpr uint8_t MAX_DESKS = 64;
