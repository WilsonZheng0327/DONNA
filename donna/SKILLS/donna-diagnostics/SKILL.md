---
name: donna-diagnostics
description: Run hardware health checks, analyze LoRa link quality (RSSI, SNR), Time-of-Flight calibration (distance_mm), battery levels (battery_mv), and heartbeat gaps. Use when debugging sensor hardware or network issues.
license: MIT
---

# DONNA Hardware Diagnostics Skill

Use this skill when investigating offline nodes, flaky presence reports, or
RF/battery health across the LoRa desk network.

## Command

Run a comprehensive audit across all known sensors:

```bash
python3 experimental/users/wilsonzheng/hackathon/src/agent/donna_cli.py audit
```

Run detailed diagnostics for a specific desk:

```bash
python3 experimental/users/wilsonzheng/hackathon/src/agent/donna_cli.py audit --desk <DESK_ID>
```

## Diagnostic Reference Table

| Metric            | Healthy Range    | Warning /        | Action Needed      |
:                   :                  : Marginal         :                    :
| ----------------- | ---------------- | ---------------- | ------------------ |
| **RSSI**          | > -95 dBm        | -95 to -115 dBm  | < -115 dBm:        |
:                   :                  :                  : Relocate node or   :
:                   :                  :                  : check antenna      :
| **SNR**           | > 0 dB           | -10 to 0 dB      | < -10 dB: Severe   |
:                   :                  :                  : interference /     :
:                   :                  :                  : obstruction        :
| **Heartbeat**     | <= 30 seconds    | 31 to 90 seconds | > 90 seconds       |
:                   :                  :                  : (`OFFLINE`)\:      :
:                   :                  :                  : Check              :
:                   :                  :                  : battery/power      :
| **Battery         | 3300 - 4200 mV   | 3100 - 3299 mV   | < 3100 mV: Replace |
: (`battery_mv`)**  :                  :                  : or recharge node   :
| **ToF             | Empty > 1200     | 800 - 1200 mm    | Hovering in gray   |
: (`distance_mm`)** : mm<br>Occupied < :                  : zone\: check for   :
:                   : 800 mm           :                  : obstacles          :

## Output

Produce an executive health summary followed by a detailed table of any nodes
showing warnings or critical issues.
