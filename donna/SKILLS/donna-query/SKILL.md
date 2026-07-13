---
name: donna-query
description: Query real-time desk availability, find free desks by floor or section, and generate occupancy summaries. Use when checking which desks are free or occupied.
license: MIT
---

# DONNA Desk Query Skill

Use this skill whenever a user asks about desk availability, free desks, floor
maps, or desk occupancy summary.

## Usage

Run the DONNA CLI tool to inspect current desks:

```bash
python3 experimental/users/wilsonzheng/hackathon/src/agent/donna_cli.py list
```

Or for a formatted floor summary:

```bash
python3 experimental/users/wilsonzheng/hackathon/src/agent/donna_cli.py summary --floor 4
```

## Desk Status Rules

-   `free`: Heartbeat fresh (`last_updated` within 90 seconds) and `occupied ==
    false` and no active reservation hold.
-   `occupied`: Either physical sensor detects presence (`occupied == true`) or
    an active temporary reservation hold exists.
-   `offline`: No packet received in over 90 seconds (`serverNow -
    last_updated > 90s`).

## Output Format

Always display results in a clear Markdown table containing:

-   Desk ID
-   Floor / Location
-   Status (`FREE`, `OCCUPIED`, `OFFLINE`)
-   Last Heartbeat (seconds/minutes ago)
-   Hold Info (if held)
