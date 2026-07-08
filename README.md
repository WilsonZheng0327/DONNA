# DONNA

Find a free desk without walking the floor. A time-of-flight sensor under
each desk decides *occupied or not*, shouts it over LoRa to a hub, the hub
mirrors it into Firebase, and a wall dashboard shows the whole office at a
glance.

```
 per desk                          one per office                cloud            any browser
┌────────────┐  I2C  ┌─────────┐   LoRa 915 MHz   ┌──────────────┐  HTTPS  ┌──────────┐  ws  ┌───────────┐
│ VL53L0X ToF├───────┤ node MCU│ ))) 43 bytes ((( │ hub: XIAO    ├─────────┤ Firebase ├──────┤ dashboard │
│ sensor     │       │ (LoRa)  │                  │ ESP32S3 +    │  PATCH  │ RTDB     │      │ React     │
└────────────┘       └─────────┘                  │ Wio-SX1262   │         └──────────┘      └───────────┘
                                                  └──────────────┘
```

## Repo map

| Path | What |
|------|------|
| `shared/protocol.h` | Radio settings + packet layout, compiled into both firmwares |
| `firmware/hub/` | PlatformIO project: LoRa RX → Firebase (the board on USB now) |
| `firmware/node/` | PlatformIO project: ToF sensing → LoRa TX (one env per desk) |
| `dashboard/` | Vite + React live board, reads Firebase directly |
| `docs/` | **Read these to learn how the hardware works** — numbered in order |

## Docs / learning path

1. [The hardware, wire by wire](docs/01-hardware.md) — boards, every pin, the two traps
2. [LoRa in one sitting](docs/02-lora.md) — chirps, SF/BW/CR, airtime, legality
3. [Flashing: what actually happens](docs/03-flashing.md) — ROM bootloader, esptool, troubleshooting
4. [The wire protocol](docs/04-protocol.md) — why each of the 13 bytes exists
5. [Firebase setup](docs/05-firebase.md) — console steps, rules, curl tests
6. [The ToF sensor](docs/06-tof-sensor.md) — how it times photons, mounting, tuning

## Getting running

```bash
# 0. one-time: serial port access (Arch)
sudo usermod -aG uucp $USER        # then log out/in; for right now instead:
sudo chmod a+rw /dev/ttyACM0

# 1. hub firmware
cd firmware/hub
cp include/secrets.h.example include/secrets.h   # fill in WiFi + Firebase
pio run -t upload
pio device monitor                 # watch it connect and receive

# 2. Firebase — follow docs/05-firebase.md (one-time, ~5 min, browser)

# 3. dashboard
cd ../../dashboard
cp .env.example .env.local         # paste database URL
npm install && npm run dev

# 4. desk nodes (after hub works)
cd ../firmware/node
pio run -e node1 -t upload         # node2, node3... one env per desk
```

Desk identity lives in the node: each env in `firmware/node/platformio.ini`
carries that desk's floor + desk ID, and the packet lands in the database at
`/{country}/{site}/{office}/{floor}/{deskId}` (e.g. `/US/SVL/CRBN100/4/4T434G`).
Add a desk = add an env block and flash it. Building-wide location constants
are in `firmware/node/include/config.h`.

## Status / open items

- [x] Hub firmware — builds; flash + fill `secrets.h`
- [x] Node firmware — builds **targeting the same XIAO kit as the hub**;
      the real node boards are still unidentified. Plug one in over USB and
      run `udevadm info -q property /dev/ttyACM0` — if it's not a XIAO
      ESP32S3, retarget `firmware/node/platformio.ini` + pin config.
- [x] Dashboard — builds; needs `.env.local`
- [x] Hub self-test — with no nodes built yet, the hub publishes fake desk
      `/US/SVL/CRBN100/4/_SELFTEST` flipping every 10 s; watching it toggle
      on the dashboard verifies hub→WiFi→Firebase→dashboard end to end. It
      retires (and deletes itself from Firebase) when a real node is heard.
- [x] Team schema adopted — packets carry country/site/office/floor/deskId;
      `last_updated` is epoch seconds via NTP, matching existing records.
- [ ] Firebase project (manual, needs your Google account)
- [ ] Sensor threshold tuning at the real desk (`docs/06-tof-sensor.md`)
- [ ] Battery reporting (protocol field reserved, unwired)
