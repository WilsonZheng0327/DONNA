# 03 — Flashing firmware: what actually happens

## The mental model

"Flashing" = writing your compiled program into the ESP32-S3's SPI flash
chip, where the CPU boots from. The chip can't overwrite the program it is
currently running, so every ESP32 has a small **ROM bootloader** burned into
silicon at the factory — unerasable, always there. Reset the chip with the
right pin held down (or ask nicely over USB) and instead of booting your app,
it boots that ROM loader, which speaks a simple serial protocol: _erase this
region, write these bytes, verify, reboot_. `esptool` (inside PlatformIO) is
the PC-side speaker of that protocol.

On the XIAO ESP32S3 there's no USB-serial converter chip — the S3 has USB
hardware on-die. Your app firmware presents a USB serial port, and esptool
toggles the port's DTR/RTS control lines in a magic pattern to make the chip
reset into the ROM loader automatically. That's why flashing normally needs
no button presses.

When auto-reset fails (e.g. the current firmware has crashed the USB stack),
you force it by hand: **hold BOOT, tap RESET (or replug USB), release BOOT.**
The chip enumerates as `303a:1002 Espressif USB JTAG/serial debug unit` —
that's the ROM loader itself talking. Flash, then press RESET once to boot.

## One-time Linux setup (Arch)

Serial devices belong to group `uucp` on Arch, and your user isn't in it:

```bash
sudo usermod -aG uucp $USER    # permanent — takes effect after logout/login
sudo chmod a+rw /dev/ttyACM0   # immediate — lasts until unplug, good for today
```

## The actual commands

```bash
cd firmware/hub               # or firmware/node

pio run                       # compile only
pio run -t upload             # compile + flash (auto-detects /dev/ttyACM0)
pio device monitor            # attach to serial log, 115200 baud (Ctrl+C exits)
```

For nodes, pick the desk identity by env: `pio run -e node2 -t upload`.

First `pio run` downloads the whole toolchain (xtensa gcc, ESP-IDF pieces,
esptool) into `~/.platformio/` — a few hundred MB, one time.

What upload prints, decoded: `Connecting....` (DTR/RTS reset dance) →
`Chip is ESP32-S3` (ROM loader answered) → `Writing at 0x00010000...`
(your app image; 0x0 holds the 2nd-stage bootloader, 0x8000 the partition
table) → `Hash of data verified` → `Hard resetting via RTS pin` (reboot into
your code).

## Troubleshooting

| Symptom                           | Meaning / fix                                                                                                                                    |
| --------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| `Permission denied: /dev/ttyACM0` | Group membership above.                                                                                                                          |
| `Connecting........_____` forever | Auto-reset failed → manual BOOT procedure.                                                                                                       |
| Port vanishes after flash         | Normal for ~2 s during reboot; monitor reconnects.                                                                                               |
| No serial output                  | Native-USB boards print only after USB re-enumerates; our firmware waits up to 3 s for the port. Unplug/replug if the monitor attached too late. |
| RadioLib `-707` at boot           | TCXO voltage not configured (we pass 1.8 V — if you see this, the radio board isn't seated).                                                     |
| RadioLib `-2` at boot             | SPI wiring/chip-select wrong — usually means this isn't the board we think it is.                                                                |
| Want Meshtastic back              | https://flasher.meshtastic.org in Chrome, device "Seeed XIAO S3", "Full Erase and Install".                                                      |

## Why flashing replaces Meshtastic completely

The flash chip holds a partition table plus app partitions; "Full erase" or a
normal PlatformIO upload writes our bootloader + table + app over theirs.
The ROM loader in silicon is untouchable, so a bad flash can never brick the
board beyond "hold BOOT and try again".
