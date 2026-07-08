# 05 — Firebase Realtime Database setup

One-time, ~5 minutes, all in the browser. (Already done for this project —
kept for reference / recreating the setup.)

## Create the database

1. https://console.firebase.google.com → **Add project** → name it →
   Google Analytics off → create.
2. Left sidebar **Build → Realtime Database** → **Create database** →
   pick the default US location → start in **locked mode**.
3. Copy the database URL shown at the top, e.g.
   `https://donna-ead10-default-rtdb.firebaseio.com/`.
   The hostname part goes into `firmware/hub/include/secrets.h`
   (`FIREBASE_HOST`) and the full URL into `dashboard/.env.local`.

## Rules: public read, hub-only write

**Realtime Database → Rules** tab:

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

## The data layout (team schema)

Desks live under location paths, hub liveness at the top level:

```
/US/SVL/CRBN100/4/4T434G   ← {country}/{site}/{office}/{floor}/{deskId}
    occupied: true
    last_updated: 1783486250        (epoch SECONDS)
    distance_mm, rssi, snr, seq, node_id, battery_mv   (our telemetry)
/hub
    last_updated, ip, wifi_rssi, uptime_s
```

See docs/04-protocol.md for why each field exists.

## How the hub talks to it (no SDK)

RTDB has a plain REST face: any path + `.json` is an endpoint. The hub does

```
PATCH https://<host>/US/SVL/CRBN100/4/4T434G.json?auth=<secret>
{"occupied":true, "last_updated":1783486250, "rssi":-62.5, ...}
```

- **PATCH** merges fields; PUT would replace the object (dropping fields
  written by others), POST would append under a random key.
- **`last_updated`** is stamped by the hub from NTP-synced time, in seconds,
  matching the records your teammate already writes. The hub refuses to
  upload until its clock has synced (it boots thinking it's 1970).
- The connection is HTTPS with `setInsecure()` — encrypted but the server
  certificate isn't verified, because the MCU lacks a CA store. To harden
  later: embed Google's root CA and call `tls.setCACert(...)`.

## Test it without any hardware

**Easiest check — the hub does it for you:** until it hears a first real
node over LoRa, the flashed hub publishes a self-test desk at
`/US/SVL/CRBN100/4/_SELFTEST` that flips occupied/free every 10 seconds,
and refreshes `/hub` every 20 seconds. If that card toggles on the
dashboard, the entire hub → WiFi → Firebase → dashboard chain works. It
disarms and deletes itself once real node traffic arrives
(`DEMO_DESK_ENABLED` in `firmware/hub/include/config.h` turns it off).

You can also fake a desk from your shell (replace host + secret):

```bash
curl -X PATCH "https://HOST/US/SVL/CRBN100/4/FAKE01.json?auth=SECRET" \
  -d '{"occupied":true,"last_updated":'"$(date +%s)"',"distance_mm":640,"node_id":9}'

# read everything back (public, no auth):
curl "https://HOST/US/SVL/CRBN100.json?print=pretty"

# clean up the fake desk:
curl -X DELETE "https://HOST/US/SVL/CRBN100/4/FAKE01.json?auth=SECRET"
```

## How the dashboard reads

The web app uses the Firebase JS SDK with only `databaseURL` configured and
subscribes with `onValue(ref(db, "US/SVL/CRBN100"), ...)` — the SDK holds a
websocket open and pushes every change in real time. Which office it shows
comes from `VITE_OFFICE_PATH` in `.env.local` (defaults to US/SVL/CRBN100).
No polling, no backend of ours.

## Free-tier arithmetic

Ten desks heartbeating every 30 s ≈ 29k writes/day of ~180 bytes — a few
MB/day against a 10 GB/month free download quota and unlimited writes. Not
a concern at this scale.
