#pragma once
#include <Arduino.h>  // fixed-width int types + LED_BUILTIN / pin macros

// ---------------------------------------------------------------------------
// Where this node lives. These strings ride inside every LoRa packet and
// become the desk's path in the database:
//   /{COUNTRY}/{SITE}/{OFFICE}/{floor}/{deskId}  e.g. /US/SVL/CRBN100/4/4T434G
// The values below are defaults; flash.sh passes the full desk identity
// (country/site/office/floor/deskId) as -D build flags, which override these
// via the #ifndef guards. Allowed characters: letters, digits, _ and - (the
// hub drops anything else).
// ---------------------------------------------------------------------------
#ifndef DESK_COUNTRY
#define DESK_COUNTRY "US"
#endif
#ifndef DESK_SITE
#define DESK_SITE    "SVL"
#endif
#ifndef DESK_OFFICE
#define DESK_OFFICE  "CRBN100"
#endif

// ---------------------------------------------------------------------------
// Radio wiring — Seeed XIAO nRF52840 + Wio-SX1262 kit (SKU 102010710).
// These are the XIAO Arduino pin numbers (D0..D10 == 0..10 in the nRF52840
// core), taken from the Wio-SX1262-for-XIAO pinout used by Meshtastic's board
// variant for this kit. Unlike the ESP32S3 hub there is no single
// "antenna switch" GPIO: DIO2 drives the TX side of the RF switch (handled in
// firmware via setDio2AsRfSwitch), and a separate RXEN line gates the RX side.
// See docs/01-hardware.md.
// ---------------------------------------------------------------------------
constexpr int PIN_LORA_SCK   = 8;   // D8
constexpr int PIN_LORA_MISO  = 9;   // D9
constexpr int PIN_LORA_MOSI  = 10;  // D10
constexpr int PIN_LORA_NSS   = 4;   // D4, radio chip select
constexpr int PIN_LORA_DIO1  = 1;   // D1, "packet done" interrupt
constexpr int PIN_LORA_RESET = 2;   // D2
constexpr int PIN_LORA_BUSY  = 3;   // D3
// RX enable half of the RF switch. A node only ever transmits, so the firmware
// holds this LOW and lets DIO2 flip the TX side automatically during transmit.
constexpr int PIN_LORA_RXEN  = 5;   // D5

// PIN_LED is already provided by the XIAO variant (the red LED of the RGB,
// active-low: LOW = on). We deliberately do NOT redefine it here — the variant
// defines it as a macro, so a `constexpr int PIN_LED` would be rewritten into
// nonsense by the preprocessor. main.cpp blinks it via that macro.

// VL53L5CX ToF sensor (I2C, address 0x29).
// NOTE: the XIAO's *default* I2C pads are D4/D5, but on this kit those are the
// radio's CS/RXEN — so the sensor must go elsewhere and Wire is remapped in
// firmware. D6/D7 are free here because we do not fit the kit's optional GNSS.
// Wiring as built: D7 = SDA, D6 = SCL. See docs/06-tof-sensor.md.
constexpr int PIN_I2C_SDA = 7;   // D7
constexpr int PIN_I2C_SCL = 6;   // D6

// ---------------------------------------------------------------------------
// Occupancy tuning — adjust these to your mounting position.
// The VL53L5CX returns a grid of zones per frame (see TOF_RESOLUTION); a desk
// counts as occupied when at least TOF_MIN_ZONES of them see something inside
// the [TOF_MIN_MM, TOF_OCCUPIED_MM] window.
// ---------------------------------------------------------------------------

// 16 = 4x4 grid (faster, up to 60 Hz), 64 = 8x8 grid (wider coverage, <=15 Hz).
// 8x8 is more forgiving of exactly where the person sits in the cone.
constexpr uint8_t  TOF_RESOLUTION  = 64;
constexpr uint8_t  TOF_RANGING_HZ  = 15;
// How many in-range zones count as "someone is there". This is the primary
// occupied decision at the monitor-top mount, where distance alone can't tell
// an empty chair from a person in it. Measured here: an empty chair in place
// lights ~6-8 zones (just its back row) at ~810 mm, while a seated person fills
// ~12-16 zones (whole torso, several rows). 10 sits in that gap, so an empty
// chair reads free and a seated person reads occupied. Re-tune if remounted.
constexpr uint8_t  TOF_MIN_ZONES   = 10;

// Below the minimum it's usually the sensor's own cover glass reflecting.
// Kept deliberately WIDE at this mount: a seated person's zones span ~530-900 mm
// (nearest ~650) and the chair back sits ~810 mm, while the wall behind reads
// ~3600 mm. 1200 includes the whole seated person (and the chair) but excludes
// the wall, so distance no longer separates chair from person - TOF_MIN_ZONES
// does that. Re-tune if the sensor is remounted.
constexpr uint16_t TOF_MIN_MM      = 40;
constexpr uint16_t TOF_OCCUPIED_MM = 1200;

// Debounce, in two directions with different time constants:
//  - occupy fast (2 s) so the dashboard reacts when someone sits down
//  - vacate currently fast (500 ms) so the free transition is near-instant
//    while testing. For deployment raise it (e.g. 30000) so leaning away or
//    standing up for a moment doesn't flap the desk back to "free".
constexpr uint32_t OCCUPY_AFTER_MS = 2000;
constexpr uint32_t VACANT_AFTER_MS = 500;

constexpr uint32_t SENSOR_POLL_MS = 100;

// ---------------------------------------------------------------------------
// Tuning telemetry. With TOF_DEBUG on, the node prints a live sensor summary
// every TOF_DEBUG_MS even when the occupied/free state is steady, so you can
// watch nearest-distance and in-range zone counts while you move around and
// pick thresholds. TOF_DEBUG_GRID additionally dumps the full 8x8 distance
// grid (verbose — good for aiming/mounting). Set TOF_DEBUG = false before
// deploying to keep the serial quiet.
// ---------------------------------------------------------------------------
constexpr bool     TOF_DEBUG      = true;
constexpr bool     TOF_DEBUG_GRID = false;
constexpr uint32_t TOF_DEBUG_MS   = 500;

// ---------------------------------------------------------------------------
// TEMPORARY calibration burst. When on, the node spends its first
// CALIBRATION_MS after boot dumping every fresh sensor frame in full (the 8x8
// distance grid + per-zone status + a summary line) and does NOT transmit, so
// you can sit in the chair and read off the distances/zones a seated person
// produces, then pick TOF_MIN_MM / TOF_OCCUPIED_MM / TOF_MIN_ZONES from them.
// Set CALIBRATION_MODE = false (or revert this block) to return to normal ops.
// ---------------------------------------------------------------------------
constexpr bool     CALIBRATION_MODE = false;
constexpr uint32_t CALIBRATION_MS   = 10000;
