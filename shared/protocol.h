// Shared between hub and node firmware. Both sides #include this file, so the
// radio settings and the packet layout can never drift apart — if they did,
// the two radios would be transmitting past each other and nothing would work.
#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// LoRa radio parameters — must be IDENTICAL on every device in the network.
// See docs/02-lora.md for what each knob physically does.
// ---------------------------------------------------------------------------

// US/Canada ISM band is 902–928 MHz. 915.0 is the conventional center choice.
// (In Europe this would have to be 868 MHz — the band is set by law.)
constexpr float    LORA_FREQ_MHZ  = 915.0f;

// Bandwidth 125 kHz + spreading factor 9 + coding rate 4/5 puts our 13-byte
// packet at roughly 180 ms of airtime — slow, but with huge range margin for
// an office. Drop SF to 7 (~35 ms) if you ever need faster updates.
constexpr float    LORA_BW_KHZ    = 125.0f;
constexpr uint8_t  LORA_SF        = 9;
constexpr uint8_t  LORA_CR        = 5;      // denominator: 5 means rate 4/5

// "Private network" sync word — radios ignore packets whose sync word
// differs, so this keeps us from waking up on LoRaWAN/Meshtastic traffic.
constexpr uint8_t  LORA_SYNC_WORD = 0x12;

// The SX1262 can do +22 dBm, but 17 is already overkill indoors and runs
// cooler. Legal in US915 either way.
constexpr int8_t   LORA_TX_DBM    = 17;

constexpr uint16_t LORA_PREAMBLE_SYMBOLS = 8;

// The Wio-SX1262 module has a 1.8 V TCXO (temperature-compensated crystal)
// feeding the radio's clock; the radio must be told to power it via DIO3 or
// it never gets a clock and begin() fails with error -707.
constexpr float    LORA_TCXO_VOLTS = 1.8f;

// ---------------------------------------------------------------------------
// Packet format
// ---------------------------------------------------------------------------

// First 4 bytes of every packet. Anything on 915 MHz can key our receiver —
// garage doors, other LoRa networks — so the hub drops any payload that
// doesn't open with these exact bytes ("DSK1").
constexpr uint32_t PACKET_MAGIC     = 0x44534B31;
constexpr uint8_t  PROTOCOL_VERSION = 1;

// 13 bytes on the wire. `packed` forbids the compiler from inserting padding
// between fields — the struct bytes ARE the radio payload, so layout must be
// deterministic and identical on both MCUs.
struct __attribute__((packed)) DeskPacket {
  uint32_t magic;       // PACKET_MAGIC, or the hub ignores the packet
  uint8_t  version;     // PROTOCOL_VERSION, bump when this struct changes
  uint8_t  nodeId;      // which desk (set per-node in platformio.ini)
  uint16_t seq;         // +1 every transmit; gaps at the hub = lost packets
  uint8_t  occupied;    // 1 = someone is at the desk
  uint16_t distanceMm;  // raw ToF reading behind the decision (for tuning)
  uint16_t batteryMv;   // 0 = not measured (future use)
};

static_assert(sizeof(DeskPacket) == 13, "packet layout changed - bump PROTOCOL_VERSION");

// Nodes re-send their state at least this often even when nothing changes.
// That heartbeat is what lets the system distinguish "desk is free" from
// "node is dead/unplugged" — silence beyond ~3 heartbeats means offline.
constexpr uint32_t NODE_HEARTBEAT_MS = 30000;
