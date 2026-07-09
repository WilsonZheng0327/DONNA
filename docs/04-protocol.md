# 04 — The wire protocol, and why each byte exists

## The packet

Everything a node ever says fits in 43 bytes (`shared/protocol.h`):

```
offset  size  field       why it exists
0       4     magic       "DSK1". A 915 MHz receiver triggers on ANY LoRa-shaped
                          signal with our radio settings; magic filters strangers.
4       1     version     If the struct ever changes shape, old and new nodes
                          would parse each other's bytes as garbage. Version lets
                          the hub reject-and-log instead of showing wrong data.
5       1     nodeId      Compact per-node id for logs/diagnostics.
6       2     seq         +1 per transmit. Gaps at the hub = packets lost in the
                          air. Free packet-loss telemetry, and it exposes a
                          rebooted node (seq resets to 0).
8       1     occupied    The one bit the whole system exists to move.
9       2     distanceMm  Raw sensor reading behind the decision — for tuning
                          thresholds from the dashboard instead of at the desk.
11      2     batteryMv   Reserved (0 today).
13      2     country     "US"        ─┐
15      4     site        "SVL"        │ the desk's address in the world.
19      8     office      "CRBN100"    │ Fixed-width, zero-padded strings;
27      4     floor       "4"          │ together they form the database path.
31      12    deskId      "4T434G"    ─┘
```

`__attribute__((packed))` matters: without it the compiler may pad fields to
aligned addresses, and the two MCUs could disagree about where fields sit.
The struct's bytes go on the air verbatim — layout is the protocol.

**Why location lives in the node, not the hub:** each packet fully describes
where it came from, so the hub is a stateless translator — it never holds a
floor plan. Installing or moving a sensor means reflashing that one node
(its env in `firmware/node/platformio.ini`); the hub and dashboard adapt
automatically. The cost is 30 bytes of airtime per packet, which at our
transmit rates is irrelevant.

**Path safety:** the hub writes to Firebase with admin rights, and these
strings become the write path. The hub therefore only accepts letters,
digits, `_` and `-` in location fields — a corrupted packet containing `/`
or `.` is dropped, not written somewhere surprising.

## Who talks when

Nodes transmit in exactly two situations:

1. **State change** — occupied↔free flip, sent immediately. This is the
   low-latency path: dashboard reacts ~1 s after the debounce settles.
2. **Heartbeat** — every ~30 s regardless. This is the liveness path.

The heartbeat solves an unsolvable-by-silence problem: a desk that says
nothing could be _free and quiet_ or _unplugged_. With heartbeats, silence
longer than ~3 periods can only mean "node dead", and the dashboard shows
OFFLINE instead of a stale, confidently wrong FREE.

There are no ACKs and no retries. A lost state-change packet self-heals at
the next heartbeat — worst case the dashboard is 30 s behind for one desk.
That failure mode is acceptable; the complexity of an ARQ scheme is not.

## What the hub adds

The hub enriches each record with things only it can know: **RSSI**
(received signal strength, dBm — how loud) and **SNR** (signal-to-noise,
dB — how clean). Watch these to place nodes: RSSI above about −110 dBm at
SF9 is comfortable.

It also stamps `last_updated` — epoch **seconds**, to match the team's
existing records. The ESP32 boots thinking it's 1970, so the hub runs SNTP
(network time) after WiFi comes up and refuses to upload anything until the
clock is sane. The dashboard compares against Firebase's server clock (via
`.info/serverTimeOffset`), so staleness math survives a wrong wall-machine
clock too.

## Firebase data shape

```
/{country}/{site}/{office}/{floor}/{deskId}:
    { occupied: true,              ← team schema
      last_updated: 1783486250,    ← team schema, epoch seconds
      distance_mm: 615,            ← ours: threshold tuning
      rssi: -62.5, snr: 9.5,       ← ours: radio link quality
      seq: 41, node_id: 1,         ← ours: loss/reboot diagnostics
      battery_mv: 0 }

/hub: { last_updated, ip, wifi_rssi, uptime_s }   ← hub liveness
```

Example real path: `/US/SVL/CRBN100/4/4T434G`. The hub's self-test desk
(before any real node exists) is `/US/SVL/CRBN100/4/_SELFTEST` — obviously
named, and deleted automatically when real traffic starts.

Layered liveness, computed by the dashboard:

```
desk silent > 90 s  → that desk shows OFFLINE
/hub stale  > 60 s  → hub/WiFi/power problem → dashboard banners "hub offline"
                      (desk data is then untrusted regardless of freshness)
```
