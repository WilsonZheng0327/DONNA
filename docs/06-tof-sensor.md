# 06 — The ToF sensor: how it works, where to point it

## What a VL53L0X actually does

(Firmware assumes the ST **VL53L0X** — the near-universal hobbyist ToF
module, including Seeed's Grove version. If your module says VL53L1X or
VL53L3CX the story is identical and the driver swap is ~5 lines; tell me
and I'll switch it.)

"Time of flight" is meant literally: the chip fires invisible 940 nm laser
pulses and times photons' round trip with an array of single-photon
avalanche diodes (SPADs). Light travels ~0.3 mm per **picosecond** — the
chip is genuinely timing that, on silicon, for a few dollars. Practical
consequences:

- Range ~30 mm to ~1.2 m (VL53L0X), independent of target color to first order.
- Narrow ~25° cone — it measures where you *aim* it, unlike PIR's whole-room view.
- Sunlight contains 940 nm; direct sun on the target degrades range. Indoors: fine.
- Returns a clean "out of range" instead of garbage when nothing's in the cone.

## Why ToF beats PIR for desks

PIR sensors detect *changes* in infrared — motion. A person typing barely
moves; PIR-based systems mark them absent mid-email (the classic
office-lights-off-while-you-work failure). ToF measures *geometry*: a body
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
decision in firmware is a window comparison:

```
presence = TOF_MIN_MM (40) ≤ distance ≤ TOF_OCCUPIED_MM (1000)
```

Tune `TOF_OCCUPIED_MM` to your mount: it must be *shorter* than the distance
to whatever sits behind the target (floor, wall), or empty space reads as
occupied. The raw `distanceMm` is shipped in every packet precisely so you
can watch real readings on the dashboard/serial log and set the threshold
empirically: point it, sit down, note the number, add margin.

`TOF_MIN_MM` exists because readings of a few mm are usually the sensor's
own cover glass reflecting — noise, not people.

## Debounce: two directions, two speeds

Raw presence flickers (lean back, stretch, drop a pen). The node firmware
requires evidence *duration* before flipping state, asymmetrically:

- free → occupied after **2 s** of continuous presence (sit down → dashboard
  updates fast; walking past the cone doesn't trigger)
- occupied → free after **30 s** of continuous absence (coffee refills don't
  release your desk)

The asymmetry mirrors the asymmetric cost of being wrong: showing an
occupied desk as free sends a person to a taken desk; the reverse merely
hides one desk for 30 s.

## Wiring

Grove I2C cable to the node's I2C pins (GPIO5=SDA, GPIO6=SCL on the XIAO —
the D4/D5 pads / the Grove base's I2C socket). Address 0x29, 2.8–5 V
tolerant on Grove modules. Firmware runs it in continuous mode at 10 Hz and
halts with a clear serial message if it can't find it at boot — loose cable
is the #1 failure, and a silent zero would look exactly like an empty desk.
