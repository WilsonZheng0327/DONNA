# 01 — The hardware, wire by wire

## The two chips that matter

The hub (and, until proven otherwise, each node) is a **Seeed XIAO ESP32S3 &
Wio-SX1262 kit** — two separate boards snapped together by a board-to-board
(B2B) connector:

- **XIAO ESP32S3** — the computer. Dual-core MCU @ 240 MHz, WiFi, BLE, USB.
  Runs our firmware. This is the thing you flash.
- **Wio-SX1262** — the radio. A Semtech SX1262 LoRa transceiver plus an
  antenna switch and a TCXO. It has *no firmware of its own* — it's a
  peripheral the ESP32 commands over SPI, like a very fancy sensor.

Your board enumerates on USB as `2886:0059 seeed-xiao-s3` (vendor 2886 =
Seeed) — that USB identity comes from the **Meshtastic firmware it shipped
with**, which we overwrite the first time we flash.

## The B2B connector pinout

These assignments are fixed in copper — you can't change them in software,
you can only tell the software where things are. ESP32-S3 GPIO numbers:

| GPIO | Signal | What it's for |
|------|--------|---------------|
| 7    | SCK    | SPI clock — MCU pulses this; one bit moves per pulse |
| 8    | MISO   | SPI data, radio → MCU ("Master In, Slave Out") |
| 9    | MOSI   | SPI data, MCU → radio |
| 41   | NSS/CS | Chip select, active low — "radio, I'm talking to YOU" |
| 39   | DIO1   | Radio's interrupt line — pulses high on "packet received" / "TX done" |
| 40   | BUSY   | Radio holds this high while its internal CPU is mid-command; we must wait |
| 42   | RESET  | Hard reset into the radio |
| 38   | ANT_SW | Powers the antenna's TX/RX switch. **Must be driven HIGH.** |

The `BUSY` line is the SX126x family's quirk: unlike dumber SPI chips, the
SX1262 has its own little processor, and after most commands it needs a few
µs before it can accept the next one. RadioLib polls BUSY for us.

### The two traps on this specific module

1. **GPIO 38 (ANT_SW)** — the RF switch that routes the antenna between the
   TX and RX paths needs power. If you never drive GPIO 38 high, everything
   *looks* alive (SPI responds, `begin()` succeeds, `transmit()` returns OK)
   but the antenna is physically disconnected. Both firmwares set it HIGH
   first thing in `setup()`.
2. **The TCXO** — the radio's reference clock is a 1.8 V
   temperature-compensated oscillator that the SX1262 itself powers via its
   DIO3 pin. Until you pass `tcxoVoltage = 1.8` to `radio.begin()`, the radio
   has no clock and initialization fails with RadioLib error `-707`.

There's a third semi-trap: **DIO2**. On this module the radio's DIO2 output
is wired as the control signal of the TX/RX switch, so firmware must call
`radio.setDio2AsRfSwitch(true)` — after that the radio flips its own antenna
switch when it transmits. (ANT_SW = switch *power*, DIO2 = switch *position*.)

## What's still free for sensors

The B2B connector eats the high-numbered GPIOs; the XIAO's castellated edge
pins stay available. We use the standard XIAO I2C pair for the ToF sensor:

- **GPIO 5 = SDA, GPIO 6 = SCL** — these are the D4/D5 pads, which every
  Grove-for-XIAO expansion board routes to its I2C connector.

The user LED is **GPIO 21, active-low** (write LOW to light it). Hub blinks
it per received packet; nodes blink it per transmit — a free debugging tool
you can read from across the room.

## Sources

- [Seeed wiki: XIAO ESP32S3 & Wio-SX1262 kit](https://wiki.seeedstudio.com/wio_sx1262_with_xiao_esp32s3_kit/)
- [RadioLib discussion #1361 — working B2B pin map](https://github.com/jgromes/RadioLib/discussions/1361)
- Meshtastic's board variant for `seeed-xiao-s3` (same pin facts, independently)
