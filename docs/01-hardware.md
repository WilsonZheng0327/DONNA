# 01 — The hardware, wire by wire

## Two different boards, one radio

DONNA has two kinds of device, and they are **different MCU families** that
happen to share the same Semtech radio:

-   **Hub** — Seeed **XIAO ESP32S3** + Wio-SX1262, joined by a board-to-board
    (B2B) connector. The ESP32S3 has WiFi, which the hub needs to reach
    Firebase.
-   **Node** — Seeed **XIAO nRF52840** + Wio-SX1262 kit (SKU 102010710). The
    nRF52840 has no WiFi (fine — nodes only transmit LoRa) and sips power, which
    suits a battery desk sensor.

In both, the **Wio-SX1262** is the radio: a Semtech SX1262 LoRa transceiver plus
an antenna switch and a TCXO. It has *no firmware of its own* — it's a
peripheral the MCU commands over SPI, like a very fancy sensor. The catch is
that the two XIAO carriers number their GPIO completely differently and use
different PlatformIO platforms (`espressif32` vs `nordicnrf52`), so the two
`include/config.h` pin maps are **not** interchangeable.

Both boards ship running **Meshtastic firmware**, which we overwrite the first
time we flash. The ESP32S3 hub enumerates on USB as `2886:0059 seeed-xiao-s3`.

## Radio quirks shared by both boards

Three things about the SX1262 are true regardless of which XIAO drives it:

1.  **BUSY line.** Unlike dumber SPI chips, the SX1262 has its own little
    processor, and after most commands it needs a few µs before it can accept
    the next one — it holds BUSY high meanwhile. RadioLib polls it for us.
2.  **The TCXO.** The radio's reference clock is a 1.8 V temperature-compensated
    oscillator that the SX1262 powers via its DIO3 pin. Until you pass
    `tcxoVoltage = 1.8` to `radio.begin()`, the radio has no clock and init
    fails with RadioLib error `-707`. (`-2` = wrong wiring / chip absent.)
3.  **DIO2 as RF switch.** The radio's DIO2 output is wired as the TX/RX switch
    control, so firmware must call `radio.setDio2AsRfSwitch(true)` — then the
    radio flips its own antenna switch when it transmits.

## Hub pinout — XIAO ESP32S3 (B2B connector)

These assignments are fixed in copper. ESP32-S3 GPIO numbers:

GPIO | Signal | What it's for
---- | ------ | -----------------------------------------------------------
7    | SCK    | SPI clock
8    | MISO   | SPI data, radio → MCU
9    | MOSI   | SPI data, MCU → radio
41   | NSS/CS | Chip select, active low
39   | DIO1   | Radio interrupt — pulses on "packet received" / "TX done"
40   | BUSY   | Radio busy (see above)
42   | RESET  | Hard reset into the radio
38   | ANT_SW | Powers the antenna's TX/RX switch. **Must be driven HIGH.**

**The ANT_SW trap:** if you never drive GPIO 38 high, everything *looks* alive
(SPI responds, `begin()` succeeds, `transmit()` returns OK) but the antenna is
physically disconnected and no packet moves. The hub sets it HIGH first thing.
(ANT_SW = switch *power*; DIO2 = switch *position*.)

Sensors on the hub would use the free XIAO I2C pair (GPIO 5 = SDA, 6 = SCL, the
D4/D5 pads) — but the hub carries no sensor; it only listens. User LED = GPIO
21, active-low; the hub blinks it per received packet.

## Node pinout — XIAO nRF52840 + Wio-SX1262 kit

The nRF52840 kit uses the standard "Wio-SX1262 for XIAO" pinout (the header /
kit version, not the ESP32S3's B2B version). Pins below are XIAO Arduino pin
numbers (`D0..D10`, which equal 0..10 in the nRF52840 core), from Meshtastic's
board variant for this exact kit:

XIAO pin | Signal | Notes
-------- | ------ | -------------------------------------------------------
D8       | SCK    | `SPI.begin()` takes no pin args here — fixed by variant
D9       | MISO   |
D10      | MOSI   |
D4       | NSS/CS | radio chip select
D1       | DIO1   | "packet done" interrupt
D2       | RESET  |
D3       | BUSY   |
D5       | RXEN   | RX half of the RF switch — held LOW (node is TX-only)

**The RF switch differs from the hub.** There is no single ANT_SW pin. The TX
side is handled by `setDio2AsRfSwitch(true)`; the RX side is a separate RXEN
line (D5). Because a node only ever transmits, the firmware drives RXEN LOW and
lets DIO2 flip the TX path during `transmit()`.

**The sensor cannot use the default I2C pads.** On this kit D4/D5 are the
radio's CS/RXEN, so the VL53L5CX goes on the free D6/D7 pads and the firmware
gives it a dedicated `TwoWire` on the nRF52840's **TWIM1** peripheral:

-   **D7 = SDA, D6 = SCL** (as wired on the bench).
-   On the nRF52840 a TWIM shares silicon with the SPIM of the same index. TWIM1
    is chosen so it does not collide with the SPIM RadioLib drives — if the
    radio bus ever goes silent, this pairing is the first thing to move.

User LED = `LED_BUILTIN`, active-low; nodes blink it per transmit.

## Sources

-   [Seeed wiki: XIAO ESP32S3 & Wio-SX1262 kit](https://wiki.seeedstudio.com/wio_sx1262_with_xiao_esp32s3_kit/)
-   [Seeed wiki: XIAO nRF52840 & Wio-SX1262 kit](https://wiki.seeedstudio.com/xiao_nrf52840&_wio_SX1262_kit_for_meshtastic/)
-   [RadioLib discussion #1361 — working ESP32S3 B2B pin map](https://github.com/jgromes/RadioLib/discussions/1361)
-   Meshtastic board variants for `seeed-xiao-s3` and `seeed_xiao_nrf52840_kit`
    (same pin facts, independently)
