# 05 — Firebase Realtime Database setup

One-time, ~5 minutes, all in the browser.

## Create the database

1. https://console.firebase.google.com → **Add project** → name it
   (e.g. `deskfinder`) → Google Analytics off → create.
2. Left sidebar **Build → Realtime Database** → **Create database** →
   pick the default US location → start in **locked mode**.
3. Copy the database URL shown at the top, e.g.
   `https://deskfinder-a1b2c-default-rtdb.firebaseio.com/`.
   The hostname part goes into `firmware/hub/include/secrets.h`
   (`FIREBASE_HOST`) and the full URL into `dashboard/.env.local`.

## Rules: public read, hub-only write

**Realtime Database → Rules** tab, replace with:

```json
{
  "rules": {
    ".read": true,
    ".write": false
  }
}
```

Then get the hub its admin credential: ⚙ **Project settings → Service
accounts → Database secrets → Show**, copy into `FIREBASE_AUTH` in
`secrets.h`. Requests carrying the secret bypass rules entirely (that's what
"legacy admin token" means), so the hub can write while `".write": false`
blocks everyone else on the internet. Anyone may read — fine for a
prototype dashboard, revisit before real deployment.

> Database secrets are the deprecated-but-still-supported mechanism. The
> grown-up path is a service account + short-lived OAuth tokens, which is
> heavy on a microcontroller; secret-in-query-string is the standard embedded
> compromise.

## How the hub talks to it (no SDK)

RTDB has a plain REST face: any path + `.json` is an endpoint. The hub does

```
PATCH https://<host>/desks/node-1.json?auth=<secret>
{"occupied":true, "rssi":-62.5, "lastSeenAt":{".sv":"timestamp"}, ...}
```

- **PATCH** merges fields; PUT would replace the object (dropping fields
  written by others), POST would append under a random key.
- **`{".sv":"timestamp"}`** is a *server value*: the database substitutes its
  own epoch-milliseconds on write. The ESP32 has no battery-backed clock, so
  we never trust it to know what time it is.
- The connection is HTTPS with `setInsecure()` — encrypted but the server
  certificate isn't verified, because the MCU lacks a CA store and a correct
  clock at first boot. To harden later: embed Google's root CA and call
  `tls.setCACert(...)` after NTP sync.

## Test it without any hardware

You can fake a node from your shell — great for developing the dashboard
before the LoRa side is up (replace host + secret):

```bash
curl -X PATCH "https://HOST/desks/node-1.json?auth=SECRET" \
  -d '{"nodeId":1,"occupied":true,"distanceMm":640,"rssi":-58.2,"snr":9.5,"seq":1,"batteryMv":0,"lastSeenAt":{".sv":"timestamp"}}'

curl -X PATCH "https://HOST/hub.json?auth=SECRET" \
  -d '{"lastSeenAt":{".sv":"timestamp"},"ip":"fake","wifiRssi":-50,"uptimeS":1}'

# name a desk (read by the dashboard):
curl -X PATCH "https://HOST/config/desks/node-1.json?auth=SECRET" \
  -d '{"name":"Window desk"}'

# read everything back (public, no auth):
curl "https://HOST/.json?print=pretty"
```

## How the dashboard reads

The web app uses the Firebase JS SDK with only `databaseURL` configured and
subscribes with `onValue(ref(db, "desks"), ...)` — the SDK holds a websocket
open and pushes every change in real time. No polling, no backend of ours.

## Free-tier arithmetic

Ten desks heartbeating every 30 s ≈ 29k writes/day of ~150 bytes — around
4 MB/day of raw payload against a 10 GB/month free download quota and
unlimited writes. Not a concern at this scale.
