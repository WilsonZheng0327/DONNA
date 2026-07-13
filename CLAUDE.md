# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

DONNA is a desk-occupancy sensor network. Per-desk nodes (VL53L5CX multizone
time-of-flight sensor + LoRa) decide occupied/free and broadcast over LoRa 915 MHz.
A single hub receives every broadcast and mirrors it into Firebase Realtime
Database. A React wall dashboard reads Firebase directly over a websocket.

```
node (ToF -> LoRa TX) --915 MHz--> hub (LoRa RX -> HTTPS PATCH) --> Firebase RTDB --> dashboard (React)
```

Three deployable pieces, three toolchains:

- `hub/` — PlatformIO / Arduino / C++ for Seeed **XIAO ESP32S3** + Wio-SX1262.
  The receiver; stays on the ESP32S3 because it needs WiFi to reach Firebase.
  (Firebase path `/hub`, `HUB_HEARTBEAT_MS`, dashboard `HubRecord` — all "hub".)
- `node/` — PlatformIO / Arduino / C++ for Seeed **XIAO nRF52840** + Wio-SX1262 kit
  (SKU 102010710), one shared build env flashed per desk by its desk id via
  `node/flash.sh`. No WiFi (nodes only transmit LoRa).
- `dashboard/` — Vite + React 19 + Firebase JS SDK
- `docs/` — numbered hardware/protocol walkthroughs; read these before touching firmware

The two boards are different MCU families with different GPIO numbering and
different PlatformIO platforms (`espressif32` vs `nordicnrf52`), so their
`include/config.h` pin maps are not interchangeable.

## The central invariant: `shared/protocol.h`

Both firmwares `#include "shared/protocol.h"` (wired via `build_flags = -I../shared`).
It defines the LoRa radio parameters and the `DeskPacket` struct that is the literal
on-wire payload. These MUST stay identical on both sides or the radios talk past each
other. Rules when editing it:

- `DeskPacket` is `__attribute__((packed))` and guarded by `static_assert(sizeof == 43)`.
  Any layout change must keep both firmwares consistent and **bump `PROTOCOL_VERSION`** —
  the hub drops packets whose version differs.
- Every packet opens with `PACKET_MAGIC` ("DSK1"); the hub ignores anything else on the band.
- Location lives in the packet (country/site/office/floorCode/deskId), not in the hub.
  The hub is a **stateless translator**: it writes each packet to
  `/{country}/{site}/{office}/{floor}/{deskId}` (e.g. `/US/SVL/CRBN100/4/4T434G`).
  Moving or adding a desk never touches hub firmware.

## Desk identity is passed at flash time, not baked into envs

A desk's full identity (country/site/office/floor/deskId) is passed to
`node/flash.sh` as one identifier — the same path a desk uses in the db/ui:

```bash
cd node && ./flash.sh US-SVL-CRBN100-4-4T434G   # -> /US/SVL/CRBN100/4/4T434G
```

The script splits the id on its **first four dashes** (so the deskId may itself
contain dashes), turns the parts into `-D` build flags, derives a stable
`NODE_ID` (1..254) from the id for the heartbeat stagger + `node_id` telemetry,
and uploads. There is a single generic `[env:node]`; the country/site/office
values in `node/include/config.h` are `#ifndef`-guarded defaults that the flags
override. **Add a desk = just flash it with its id — no env or code editing.**

## Timestamps and the NTP gate

`last_updated` is written as **epoch seconds** (matching the team's pre-existing Firebase
records), not milliseconds. The hub syncs SNTP on boot and refuses to upload anything
until the clock is real (`time(nullptr) > 1700000000`) — otherwise timestamps would be
~0. If you touch the upload path, preserve this gate (`loop()` returns early when
`!timeSynced()`).

## Build / flash / run

Firmware (run from the project subdir, needs PlatformIO `pio` on PATH):

```bash
cd hub                                           # XIAO ESP32S3, platform espressif32
cp include/secrets.h.example include/secrets.h   # fill WiFi + Firebase; gitignored
pio run                    # build only
pio run -t upload          # flash over USB
pio device monitor         # serial logs @ 115200 (start here after any flash)

cd node                    # XIAO nRF52840, platform nordicnrf52 (first build pulls a large toolchain)
./flash.sh US-SVL-CRBN100-4-4T434G   # flash a desk by its id (add "build" to compile only)
```

The hub builds and runs the LoRa side even without `secrets.h` (radio-only, prints
received packets). Watch the `[status]` ticker line first — it appears before WiFi.

Dashboard (Vite; use `bun` per global tooling preference):

```bash
cd dashboard
cp .env.example .env.local # paste VITE_FIREBASE_DATABASE_URL (and optional VITE_OFFICE_PATH)
bun install && bun run dev
bun run build              # tsc --noEmit + vite build
```

There are no automated tests in this repo.

## How to verify the pipeline without desk hardware

The hub runs a **self-test desk** (`config.h: DEMO_DESK_*`): until the first real LoRa
packet arrives it invents a fake desk at `/US/SVL/CRBN100/4/_SELFTEST`, flipping
occupied/free every 10 s through the exact same code path as real traffic. Watching it
toggle on the dashboard proves hub -> WiFi -> Firebase -> dashboard end to end. It retires
and deletes itself from Firebase the moment a real node is heard. The dashboard renders
`_SELFTEST` with a friendlier label (`SELFTEST_DESK_ID` in `dashboard/src/types.ts`).

## Data model (Firebase RTDB) and offline detection

- Desks: `/{country}/{site}/{office}/{floor}/{deskId}` -> `{ occupied, last_updated, distance_mm, battery_mv, seq, node_id, rssi, snr }`.
  Only `occupied`/`last_updated` are the team schema; the rest is DONNA telemetry.
- Hub liveness: `/hub` -> `{ last_updated, ip, wifi_rssi, uptime_s }`.
- "Offline" is inferred from staleness, not a flag: nodes heartbeat ~30 s
  (`NODE_HEARTBEAT_MS`), dashboard calls a desk dead after 90 s (`DESK_OFFLINE_AFTER_MS`);
  hub heartbeats 20 s (`HUB_HEARTBEAT_MS`), dead after 60 s. The dashboard corrects for
  clock drift using `.info/serverTimeOffset`. The distinction between "desk free" and
  "node dead" is the whole reason heartbeats exist.

## Hardware gotchas that will silently break things

These are wired into `config.h` / firmware but easy to regress (see `docs/01-hardware.md`):

- RF switch differs by board. On the **ESP32S3 hub** a single `PIN_LORA_ANT_SW`
  must be driven HIGH or the antenna is unpowered (SPI answers fine but no packet
  moves). On the **nRF52840 node** there is no such pin: `setDio2AsRfSwitch(true)`
  drives the TX side, and `PIN_LORA_RXEN` (held LOW, TX-only) gates the RX side.
- The Wio-SX1262 needs `LORA_TCXO_VOLTS = 1.8` passed to `radio.begin()`, else init
  fails with RadioLib code -707 (no clock). Code -2 = wrong wiring / chip absent.
- `radio.setDio2AsRfSwitch(true)` is required on both boards.
- On the nRF52840 node the radio's CS/RXEN sit on the XIAO's default I2C pads (D4/D5),
  so the VL53L5CX I2C is remapped to D6/D7 via a dedicated `TwoWire` on TWIM1. On
  nRF52840 a TWIM shares silicon with the SPIM of the same index — keep the sensor's
  TWIM index off whatever SPIM RadioLib uses, or one of the two buses goes silent.
- `SPI.begin()` on the nRF52840 takes no pin args (uses the variant's fixed pins);
  the ESP32S3 form `SPI.begin(sck, miso, mosi, ss)` does not compile there.

## Known state / caveats

- The node firmware (nRF52840 + VL53L5CX) is **written but unbuilt/untested** — there
  was no PlatformIO toolchain or hardware available to verify it. The LoRa pins come from
  Meshtastic's board variant for this kit (high confidence); the VL53L5CX I2C wiring
  (D6/D7), the TWIM1 choice, and the TX-only RF-switch handling need confirming on the
  bench. First `pio run` for `node/` downloads the nordicnrf52 toolchain (~hundreds of MB).
- Hub TLS uses `setInsecure()` (no cert verification) as a prototype tradeoff — traffic
  is encrypted but the server isn't authenticated. `docs/05-firebase.md` covers doing it right.
- Battery reporting: `batteryMv` field is reserved in the protocol but unwired (always 0).
- ToF thresholds (`TOF_MIN_MM`, `TOF_OCCUPIED_MM`, `TOF_MIN_ZONES`, debounce timers) need
  tuning at the real desk mounting position — see `docs/06-tof-sensor.md`.
