#pragma once

// ---------------------------------------------------------------------------
// Where this node lives. These strings ride inside every LoRa packet and
// become the desk's path in the database:
//   /{COUNTRY}/{SITE}/{OFFICE}/{floor}/{deskId}  e.g. /US/SVL/CRBN100/4/4T434G
// Building-wide values live here; the per-desk floor + desk ID come from the
// node1/node2/... envs in platformio.ini, since they differ per device.
// Allowed characters: letters, digits, _ and - (the hub drops anything else).
// ---------------------------------------------------------------------------
#define DESK_COUNTRY "US"
#define DESK_SITE    "SVL"
#define DESK_OFFICE  "CRBN100"

// ---------------------------------------------------------------------------
// Radio wiring — XIAO ESP32S3 <-> Wio-SX1262 B2B connector, same as the hub.
// If the real node board turns out to be different hardware, these pin
// numbers are the main thing that changes. See docs/01-hardware.md.
// ---------------------------------------------------------------------------
constexpr int PIN_LORA_SCK    = 7;
constexpr int PIN_LORA_MISO   = 8;
constexpr int PIN_LORA_MOSI   = 9;
constexpr int PIN_LORA_NSS    = 41;
constexpr int PIN_LORA_DIO1   = 39;
constexpr int PIN_LORA_RESET  = 42;
constexpr int PIN_LORA_BUSY   = 40;
constexpr int PIN_LORA_ANT_SW = 38;  // must be HIGH or the antenna is disconnected

constexpr int PIN_LED = 21;          // XIAO user LED, active-low

// VL53L0X ToF sensor on the I2C bus. GPIO5/6 are the XIAO's D4/D5 pads —
// the pins every Grove-for-XIAO base board routes to its I2C connector.
constexpr int PIN_I2C_SDA = 5;
constexpr int PIN_I2C_SCL = 6;

// ---------------------------------------------------------------------------
// Occupancy tuning — adjust these to your mounting position.
// ---------------------------------------------------------------------------

// Sensor sees "someone there" when the reading falls in this window.
// Below the minimum it's usually the sensor's own cover glass reflecting.
// The maximum depends on where you mount it: under the desk aimed at the
// chair, ~1000 mm covers a seated person; facing the seat back, use less.
constexpr uint16_t TOF_MIN_MM      = 40;
constexpr uint16_t TOF_OCCUPIED_MM = 1000;

// Debounce, in two directions with very different time constants:
//  - occupy fast (2 s) so the dashboard reacts when someone sits down
//  - vacate slow (30 s) so leaning away or standing up for a moment
//    doesn't flap the desk back to "free"
constexpr uint32_t OCCUPY_AFTER_MS = 2000;
constexpr uint32_t VACANT_AFTER_MS = 30000;

constexpr uint32_t SENSOR_POLL_MS = 100;
