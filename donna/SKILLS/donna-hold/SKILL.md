---
name: donna-hold
description: Temporarily reserve or hold a desk for a user (e.g. 15 minute hold while walking over) so it shows up as reserved on the dashboard. Use when a user asks to hold, claim, or reserve a desk.
license: MIT
---

# DONNA Desk Hold Skill

Use this skill when a user wants to place a temporary hold on an available desk
so nobody else takes it while they walk to the office or grab coffee.

## Command

To hold a desk:

```bash
python3 experimental/users/wilsonzheng/hackathon/src/agent/donna_cli.py hold --desk <DESK_ID> --minutes <MINUTES> --user "<LDAP/NAME>"
```

To release a hold early:

```bash
python3 experimental/users/wilsonzheng/hackathon/src/agent/donna_cli.py release --desk <DESK_ID>
```

## Behavior

1.  Check that the target desk is currently `free`.
2.  Apply the hold via `donna_cli.py hold`.
3.  Confirm the hold duration and exact expiration time in user-friendly
    Markdown.
