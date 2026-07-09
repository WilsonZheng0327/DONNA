# 06 — The ToF sensor: how it works, where to point it

## What a VL53L5CX actually does

(Firmware uses the ST **VL53L5CX**, a _multizone_ ToF sensor: instead of one
distance it returns a whole grid of zones per frame — 4x4 or 8x8, set by
`TOF_RESOLUTION`. The node reduces that grid to a boolean: occupied when at
least `TOF_MIN_ZONES` zones fall inside the [`TOF_MIN_MM`, `TOF_OCCUPIED_MM`]
window. That grid is why a person is caught regardless of exactly where they
sit in the cone. Range reaches ~4 m; field of view is a ~45deg square, wider
than the VL53L0X's ~25deg cone this project first used.)

"Time of flight" is meant literally: the chip fires invisible 940 nm laser
pulses and times photons' round trip with an array of single-photon
avalanche diodes (SPADs). Light travels ~0.3 mm per **picosecond** — the
chip is genuinely timing that, on silicon, for a few dollars. Practical
consequences:

- Range ~30 mm to ~1.2 m (VL53L0X), independent of target color to first order.
- Narrow ~25° cone — it measures where you _aim_ it, unlike PIR's whole-room view.
- Sunlight contains 940 nm; direct sun on the target degrades range. Indoors: fine.
- Returns a clean "out of range" instead of garbage when nothing's in the cone.

## Why ToF beats PIR for desks

PIR sensors detect _changes_ in infrared — motion. A person typing barely
moves; PIR-based systems mark them absent mid-email (the classic
office-lights-off-while-you-work failure). ToF measures _geometry_: a body
in the cone changes the distance reading for as long as it sits there,
motion or not.

## Mounting geometry (pick one, tune two numbers)

```
A. Under the desk, aimed at the chair       B. On the monitor, aimed at the torso
   ┌─────────────────┐                          ▄ sensor on bezel, aimed out
   │ desk            │                          │ ~500–800 mm to a seated person
   │   [s]→ → → 🪑   │
   └───│─────────────┘
   sensor ~300–700 mm from chair
```

A is invisible and theft-proof; B reads the person directly. Either way the
decision in firmware counts how many zones land inside a distance window:

```
zone in range  = TOF_MIN_MM (40) ≤ zone distance ≤ TOF_OCCUPIED_MM (1200)
presence       = (zones in range) ≥ TOF_MIN_ZONES
```

Tune `TOF_OCCUPIED_MM` to your mount: it must be _shorter_ than the distance
to whatever sits behind the target (floor, wall), or empty space reads as
occupied. `TOF_MIN_ZONES` is how many grid cells must agree — 1 is most
sensitive, raise it to reject a lone stray reflection (chair arm, cable). The
`distanceMm` shipped in each packet is the _nearest in-range zone_, so you can
watch real readings on the dashboard/serial log and set thresholds empirically:
point it, sit down, note the number, add margin.

`TOF_MIN_MM` exists because readings of a few mm are usually the sensor's
own cover glass reflecting — noise, not people.

## Debounce: two directions, two speeds

Raw presence flickers (lean back, stretch, drop a pen). The node firmware
requires evidence _duration_ before flipping state, asymmetrically:

- free → occupied after **2 s** of continuous presence (sit down → dashboard
  updates fast; walking past the cone doesn't trigger)
- occupied → free after continuous absence — currently **500 ms** for bench
  testing (near-instant free), intended to be raised (e.g. 30 s) for deployment
  so coffee refills don't release your desk

The asymmetry mirrors the asymmetric cost of being wrong: showing an
occupied desk as free sends a person to a taken desk; the reverse merely
hides one desk briefly.

## Wiring (XIAO nRF52840 node)

Four wires, I2C, address 0x29. The XIAO's _default_ I2C pads (D4/D5) are taken
by the Wio-SX1262's CS/RXEN on this kit, so the sensor goes on the free D6/D7
pads and the firmware remaps `Wire` there (a dedicated `TwoWire` on TWIM1).

| VL53L5CX | XIAO nRF52840 pad |
| -------- | ----------------- |
| VIN/3V3  | 3V3               |
| GND      | GND               |
| SDA      | **D7**            |
| SCL      | **D6**            |

Leave the breakout's INT/LPn/RST pins unconnected — the firmware polls with
`isDataReady()`, so no interrupt or shutdown line is needed. The VL53L5CX
draws sharp ~200 mA bursts while ranging; keep the 3V3 wire short and add a
10 uF cap across VIN/GND if it drops out at 8x8 / high frequency. The node
halts with a clear serial message if the sensor is absent at boot — a loose
cable is the #1 failure, and a silent zero would look exactly like an empty desk.
