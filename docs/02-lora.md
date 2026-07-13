# 02 — LoRa in one sitting

## Why LoRa here at all?

WiFi would work for a handful of desks near an access point, but every node
would need WiFi credentials, IT signoff, and ~80 mA whenever radio is up.
LoRa is a _long-range, low-rate_ radio: kilometers of range on milliwatts,
at the price of sending tiny packets slowly. Our payload is 43 bytes a few
times a minute — exactly the regime LoRa was built for. Only the hub touches
the office network.

## What LoRa physically is

LoRa encodes bits as **chirps** — tones that sweep upward through the channel
bandwidth. A receiver can pull chirps out from _below the noise floor_,
which is why LoRa gets absurd range from tiny power. Everything else is
parameters on that idea, and they all live in `shared/protocol.h`:

| Parameter        | Ours      | What it does                                                                                                                                                                                |
| ---------------- | --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Frequency        | 915.0 MHz | Which slice of spectrum. 902–928 MHz is the US/Canada ISM band — license-free. (EU uses 868 MHz; this is law, not preference.)                                                              |
| Bandwidth        | 125 kHz   | Width of each chirp sweep. Wider = faster but less sensitive.                                                                                                                               |
| Spreading factor | SF9       | How _slowly_ each chirp sweeps. Every +1 SF ≈ 2× airtime and ~+2.5 dB link budget. For our packet: SF7 ≈ 55 ms, SF9 ≈ 280 ms, SF12 ≈ 2 s. SF9 is comfortable overkill for one office floor. |
| Coding rate      | 4/5       | Forward error correction: 4 data bits per 5 sent. Cheapest redundancy level.                                                                                                                |
| Sync word        | 0x12      | Network filter. Radios discard packets with a different sync word — keeps us from waking on LoRaWAN (0x34) or Meshtastic traffic.                                                           |
| Preamble         | 8 symbols | The "wake up, packet coming" header the receiver locks onto.                                                                                                                                |
| TX power         | +17 dBm   | The SX1262 tops out at +22; 17 is already more than an office needs.                                                                                                                        |

**The one iron rule:** every parameter above must match on both ends.
A receiver at SF9 literally cannot hear an SF7 transmitter — they're
different waveforms. That's why both firmwares include the same header.

## Airtime, collisions, and why we can be lazy

Our 43-byte packet at SF9/125k/CR4:5 occupies the channel ~280 ms. Two nodes
transmitting simultaneously = both packets usually lost. We don't do
listen-before-talk or acknowledgments because the numbers say we don't have
to: each node transmits ~1% of the time worst-case, collisions are rare,
and a lost heartbeat just means the next one 30 s later lands. The `seq`
field lets the hub _count_ the losses so you can verify this laziness is
justified (watch the `(N lost)` notes in the hub serial log).

Nodes also stagger their heartbeat periods (`+ NODE_ID × 0.7 s`) so two nodes
powered on at the same instant drift apart instead of colliding forever.

## Raw LoRa vs LoRaWAN vs Meshtastic

Three things people mean by "LoRa", worth keeping straight:

- **Raw LoRa (us)** — just the radio layer. You define the packet bytes.
  Perfect for closed point-to-point systems like this one.
- **LoRaWAN** — a full carrier-grade protocol on top: gateways, network
  servers, encryption keys, regional duty-cycle rules. Overkill by ~3 layers.
- **Meshtastic** — a community mesh-chat firmware. It's what your kit shipped
  running (that's why the USB name says `seeed-xiao-s3`). We flash over it;
  nothing is lost — you can always reflash Meshtastic from their web flasher.

## Legality note

US915 has no duty-cycle limit for this kind of frequency-hopping-exempt
low-power use, but rules cap dwell time; our 280 ms bursts are inside
every limit. If this project ever moves countries, change `LORA_FREQ_MHZ`
(and check the local band) before powering up.
