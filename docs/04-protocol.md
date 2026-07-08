# 04 — The wire protocol, and why each byte exists

## The packet

Everything a node ever says fits in 13 bytes (`shared/protocol.h`):

```
offset  size  field       why it exists
0       4     magic       "DSK1". A 915 MHz receiver triggers on ANY LoRa-shaped
                          signal with our radio settings; magic filters strangers.
4       1     version     If the struct ever changes shape, old and new nodes
                          would parse each other's bytes as garbage. Version lets
                          the hub reject-and-log instead of showing wrong data.
5       1     nodeId      Which desk. Assigned at build time (-DNODE_ID=n).
6       2     seq         +1 per transmit. Gaps at the hub = packets lost in the
                          air. Free packet-loss telemetry, and it lets Firebase
                          watchers spot a rebooted node (seq resets to 0).
8       1     occupied    The one bit the whole system exists to move.
9       2     distanceMm  Raw sensor reading behind the decision — for tuning
                          thresholds from the dashboard instead of at the desk.
11      2     batteryMv   Reserved (0 today). Cheaper to reserve 2 bytes now
                          than to bump `version` later.
```

`__attribute__((packed))` matters: without it the compiler may pad fields to
aligned addresses, and the two MCUs could disagree about where fields sit.
The struct's bytes go on the air verbatim — layout is the protocol.

## Who talks when

Nodes transmit in exactly two situations:

1. **State change** — occupied↔free flip, sent immediately. This is the
   low-latency path: dashboard reacts ~1 s after the debounce settles.
2. **Heartbeat** — every ~30 s regardless. This is the liveness path.

The heartbeat solves an unsolvable-by-silence problem: a desk that says
nothing could be *free and quiet* or *unplugged*. With heartbeats, silence
longer than ~3 periods can only mean "node dead", and the dashboard shows
OFFLINE instead of a stale, confidently wrong FREE.

There are no ACKs and no retries. A lost state-change packet self-heals at
the next heartbeat — worst case the dashboard is 30 s behind for one desk.
That failure mode is acceptable; the complexity of an ARQ scheme is not.

## What the hub adds

The hub is a pure translator, but it enriches each packet with things only
it can know: **RSSI** (received signal strength, dBm — how loud) and **SNR**
(signal-to-noise, dB — how clean), stamped into the Firebase record. Watch
these to place nodes: RSSI above about −110 dBm at SF9 is comfortable.

The hub also writes its own `/hub` heartbeat every 20 s. Layered liveness:

```
node silent  > 90 s  → that desk shows OFFLINE
/hub stale   > 60 s  → hub/WiFi/power problem → dashboard banners "hub offline"
                       (desk data is then untrusted regardless of freshness)
```

## Firebase data shape

```
/desks/node-1: { nodeId, occupied, distanceMm, batteryMv, seq,
                 rssi, snr, lastSeenAt }      ← written by hub only
/hub:          { lastSeenAt, ip, wifiRssi, uptimeS }
/config/desks/node-1: { name: "Window desk" } ← written by you, read by dashboard
```

`lastSeenAt` uses RTDB's `{".sv":"timestamp"}` server value: the *database*
stamps arrival time. Neither MCU has a trustworthy clock, and the dashboard
compares against server time too (via `.info/serverTimeOffset`), so staleness
math never depends on anyone's local clock being right.
