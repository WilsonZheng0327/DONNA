#!/usr/bin/env python3
"""DONNA CLI Tool — Desk Occupancy Notification & Navigation Assistant.

Queries and manages live desk state from Firebase Realtime Database
(/{country}/{site}/{office}/{floor}/{deskId}) or a local mock state file
when offline/testing.
"""

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request

DEFAULT_STATE_FILE = "/tmp/donna_mock_state.json"
OFFLINE_THRESHOLD_S = 90


def get_config():
  """Load configuration from src/agent/config.json."""
  config_file = os.path.join(
      os.path.dirname(os.path.abspath(__file__)), "config.json"
  )
  if os.path.exists(config_file):
    try:
      with open(config_file, "r", encoding="utf-8") as f:
        return json.load(f)
    except (OSError, ValueError):
      pass
  return {}


def get_firebase_url():
  """Retrieve Firebase URL strictly from src/agent/config.json."""
  cfg = get_config()
  url = cfg.get("default_firebase_url", "").strip().rstrip("/")
  if url and not url.startswith(("http://", "https://")):
    url = "https://" + url
  return url


def get_office_path():
  """Retrieve office path strictly from src/agent/config.json."""
  cfg = get_config()
  path = cfg.get("office_path", "US/SVL/CRBN100").strip().strip("/")
  return path or "US/SVL/CRBN100"


def load_state():
  """Load desk state from live Firebase RTDB or local fallback file."""
  url = get_firebase_url()
  office = get_office_path()
  if url:
    try:
      target = f"{url}/{office}.json"
      req = urllib.request.Request(target)
      with urllib.request.urlopen(req, timeout=5) as resp:
        data = json.loads(resp.read().decode("utf-8"))
        if data:
          return data
    except (OSError, ValueError) as e:
      sys.stderr.write(
          f"[warn] Firebase request failed ({e}), falling back to local file.\n"
      )

  if os.path.exists(DEFAULT_STATE_FILE):
    try:
      with open(DEFAULT_STATE_FILE, "r", encoding="utf-8") as f:
        return json.load(f)
    except (OSError, ValueError) as e:
      sys.stderr.write(f"[warn] Failed reading {DEFAULT_STATE_FILE}: {e}\n")
  return {}


def save_state(state):
  """Save desk state to live Firebase RTDB and local fallback file."""
  url = get_firebase_url()
  office = get_office_path()
  if url:
    try:
      target = f"{url}/{office}.json"
      req = urllib.request.Request(
          target,
          data=json.dumps(state).encode("utf-8"),
          method="PUT",
          headers={"Content-Type": "application/json"},
      )
      with urllib.request.urlopen(req, timeout=5):
        pass
    except (OSError, ValueError) as e:
      sys.stderr.write(f"[warn] Firebase PUT failed: {e}\n")

  with open(DEFAULT_STATE_FILE, "w", encoding="utf-8") as f:
    json.dump(state, f, indent=2)


def get_desk_status(rec, now_s):
  last_ts = rec.get("last_updated", 0)
  if not last_ts or (now_s - last_ts) > OFFLINE_THRESHOLD_S:
    return "OFFLINE"
  if rec.get("hold_user") and rec.get("hold_expires", 0) > now_s:
    return "RESERVED_HOLD"
  if rec.get("occupied", False):
    return "OCCUPIED"
  return "FREE"


def format_duration(seconds_ago):
  if seconds_ago < 0:
    seconds_ago = 0
  if seconds_ago < 60:
    return f"{int(seconds_ago)}s ago"
  mins = int(seconds_ago // 60)
  secs = int(seconds_ago % 60)
  return f"{mins}m {secs}s ago"


def matches_floor(floor_code, target_floor):
  if target_floor is None or type(target_floor).__name__ == "Mock":
    return True
  return str(floor_code) == str(target_floor)


def cmd_list(args):
  state = load_state()
  now = time.time()

  rows = []
  for floor, desks in sorted(state.items()):
    if not isinstance(desks, dict):
      continue
    if not matches_floor(floor, getattr(args, "floor", None)):
      continue
    for desk_id, rec in sorted(desks.items()):
      if not isinstance(rec, dict):
        continue
      status = get_desk_status(rec, now)
      if args.status and status.upper() != args.status.upper():
        continue
      last_ts = rec.get("last_updated", 0)
      seen_ago = now - last_ts if last_ts else 999999
      rssi = rec.get("rssi", "N/A")
      snr = rec.get("snr", "N/A")
      dist = rec.get("distance_mm", "N/A")
      batt = rec.get("battery_mv", "N/A")
      hold = ""
      if rec.get("hold_user") and rec.get("hold_expires", 0) > now:
        rem_m = int((rec["hold_expires"] - now) // 60)
        hold = f"Hold: {rec['hold_user']} ({rem_m}m left)"
      rows.append(
          f"| {desk_id} | Floor {floor} | **{status}** |"
          f" {format_duration(seen_ago)} | {dist} mm | {rssi} dBm | {snr} dB |"
          f" {batt} mV | {hold} |"
      )

  office = get_office_path()
  print(f"### DONNA Live Desk Status (`/{office}`)")
  print(
      "| Desk ID | Floor | Status | Last Seen | ToF Dist | RSSI | SNR |"
      " Battery | Notes |"
  )
  print("|---|---|---|---|---|---|---|---|---|")
  if not rows:
    print("| _No desks matching criteria_ | | | | | | | | |")
  else:
    for r in rows:
      print(r)


def cmd_summary(args):
  state = load_state()
  now = time.time()

  counts = {"FREE": 0, "OCCUPIED": 0, "RESERVED_HOLD": 0, "OFFLINE": 0}
  floor_desks = {}

  for floor, desks in sorted(state.items()):
    if not isinstance(desks, dict):
      continue
    if not matches_floor(floor, getattr(args, "floor", None)):
      continue
    floor_desks[floor] = []
    for desk_id, rec in sorted(desks.items()):
      if not isinstance(rec, dict):
        continue
      st = get_desk_status(rec, now)
      counts[st] = counts.get(st, 0) + 1
      floor_desks[floor].append((desk_id, st))

  print("### DONNA Workplace Occupancy Summary")
  total = sum(counts.values())
  free = counts["FREE"]
  occ = counts["OCCUPIED"] + counts["RESERVED_HOLD"]
  off = counts["OFFLINE"]

  print(f"- **Total Desks Tracked:** {total}")
  print(f"- **Available (Free):** {free}")
  print(f"- **In Use / Held:** {occ}")
  print(f"- **Offline / Unreachable:** {off}\n")

  for floor, items in floor_desks.items():
    print(f"#### Floor {floor}")
    symbols = []
    for did, st in items:
      if st == "FREE":
        icon = "🟢"
      elif st in ("OCCUPIED", "RESERVED_HOLD"):
        icon = "🔴"
      else:
        icon = "⚪"
      symbols.append(f"{icon} `{did}` ({st})")
    print(" ".join(symbols) if symbols else "_No desks on this floor_")


def cmd_hold(args):
  """Place a temporary hold on an available desk."""
  if args.minutes <= 0 or args.minutes > 120:
    print(
        f"❌ Invalid hold duration (`{args.minutes}` mins). Holds must be"
        " between 1 and 120 minutes."
    )
    return

  state = load_state()
  now = time.time()

  found = False
  for floor, desks in state.items():
    if not isinstance(desks, dict):
      continue
    if not matches_floor(floor, getattr(args, "floor", None)):
      continue
    if args.desk in desks:
      rec = desks[args.desk]
      st = get_desk_status(rec, now)

      if st == "OCCUPIED":
        print(
            f"❌ Cannot hold desk `{args.desk}` — someone is currently sitting"
            " there (`OCCUPIED`)."
        )
        return
      if st == "OFFLINE":
        print(
            f"❌ Cannot hold desk `{args.desk}` — sensor is currently"
            " `OFFLINE`."
        )
        return
      if st == "RESERVED_HOLD" and rec.get("hold_user") != args.user:
        holder = rec.get("hold_user", "another user")
        rem_m = int((rec.get("hold_expires", 0) - now) // 60)
        print(
            f"❌ Cannot hold desk `{args.desk}` — already reserved by"
            f" **{holder}** ({rem_m}m left)."
        )
        return

      rec["occupied"] = True
      rec["hold_user"] = args.user
      rec["hold_start"] = int(now)
      rec["hold_expires"] = int(now + args.minutes * 60)
      rec["last_updated"] = int(now)
      found = True
      break

  if not found:
    print(
        f"❌ Desk `{args.desk}` not found in current inventory. Check `list`"
        " first."
    )
    return

  save_state(state)
  print(
      f"✅ **Desk `{args.desk}` reserved** for **{args.user}**"
      f" (`{args.minutes}`"
      " mins)."
  )
  print(f"Expiration: <t:{int(now + args.minutes * 60)}:R>")


def cmd_release(args):
  """Release an existing temporary hold on a desk."""
  state = load_state()
  now = time.time()

  found = False
  for floor, desks in state.items():
    if not isinstance(desks, dict):
      continue
    if not matches_floor(floor, getattr(args, "floor", None)):
      continue
    if args.desk in desks:
      rec = desks[args.desk]
      st = get_desk_status(rec, now)

      if st != "RESERVED_HOLD":
        print(
            f"ℹ️ Desk `{args.desk}` does not have an active reservation hold."
        )
        return

      holder = rec.get("hold_user")
      if (
          args.user
          and holder
          and args.user != holder
          and not getattr(args, "force", False)
      ):
        print(
            f"❌ Permission denied: Desk `{args.desk}` is reserved by"
            f" **{holder}** (you are `{args.user}`).\nPass `--force` to"
            " override as admin."
        )
        return

      rec.pop("hold_user", None)
      rec.pop("hold_start", None)
      rec.pop("hold_expires", None)
      rec["occupied"] = False
      rec["last_updated"] = int(now)
      found = True
      break

  if not found:
    print(f"❌ Desk `{args.desk}` not found.")
    return

  save_state(state)
  print(f"✅ Released hold on desk `{args.desk}`. Desk is now **FREE**.")


def cmd_audit(args):
  """Audit hardware and RF link health across tracked desks."""
  state = load_state()
  now = time.time()

  warnings = []
  healthy_count = 0

  for floor, desks in sorted(state.items()):
    if not isinstance(desks, dict):
      continue
    if not matches_floor(floor, getattr(args, "floor", None)):
      continue
    for desk_id, rec in sorted(desks.items()):
      if not isinstance(rec, dict):
        continue
      if args.desk and desk_id != args.desk:
        continue

      issues = []
      st = get_desk_status(rec, now)
      last_ts = rec.get("last_updated", 0)
      seen_ago = now - last_ts if last_ts else 999999

      if st == "OFFLINE":
        issues.append(f"Offline ({int(seen_ago)}s since last packet)")
      rssi = rec.get("rssi")
      if rssi is not None and rssi < -115:
        issues.append(f"Weak link budget (RSSI {rssi} dBm)")
      snr = rec.get("snr")
      if snr is not None and snr < -10:
        issues.append(f"High RF noise/interference (SNR {snr} dB)")
      batt = rec.get("battery_mv")
      if batt is not None and batt < 3100:
        issues.append(f"Low battery ({batt} mV)")

      if issues:
        warnings.append((desk_id, floor, st, issues))
      else:
        healthy_count += 1

  print("### DONNA Hardware & RF Diagnostics Audit")
  print(f"- **Healthy Sensors:** {healthy_count}")
  print(f"- **Sensors with Warnings / Alerts:** {len(warnings)}\n")

  if warnings:
    print("| Desk ID | Floor | Status | Detected Hardware/RF Issues |")
    print("|---|---|---|---|")
    for did, fl, st, iss in warnings:
      print(f"| `{did}` | {fl} | **{st}** | " + "; ".join(iss) + " |")
  else:
    print("✅ _All tracked sensors are operating within nominal thresholds._")


def main():
  """CLI entrypoint for DONNA desk agent."""
  parser = argparse.ArgumentParser(description="DONNA Desk Agent CLI")
  sub = parser.add_subparsers(dest="command", required=True)

  p_list = sub.add_parser("list", help="List desk status")
  p_list.add_argument("--floor", help="Filter by floor code")
  p_list.add_argument(
      "--status", help="Filter by status (FREE/OCCUPIED/OFFLINE)"
  )

  p_sum = sub.add_parser("summary", help="Show occupancy summary")
  p_sum.add_argument("--floor", help="Filter by floor code")

  p_hold = sub.add_parser("hold", help="Place a hold on a desk")
  p_hold.add_argument("--desk", required=True, help="Desk ID")
  p_hold.add_argument("--floor", help="Filter by floor code")
  p_hold.add_argument(
      "--minutes", type=int, default=15, help="Duration in minutes"
  )
  p_hold.add_argument("--user", required=True, help="User name or LDAP")

  p_rel = sub.add_parser("release", help="Release a hold")
  p_rel.add_argument("--desk", required=True, help="Desk ID")
  p_rel.add_argument("--floor", help="Filter by floor code")
  p_rel.add_argument(
      "--user", help="User requesting release (checked against holder)"
  )
  p_rel.add_argument(
      "--force", action="store_true", help="Force release hold as admin"
  )

  p_aud = sub.add_parser("audit", help="Run hardware diagnostics")
  p_aud.add_argument("--desk", help="Specific desk ID to audit")
  p_aud.add_argument("--floor", help="Filter by floor code")

  args = parser.parse_args()
  if args.command == "list":
    cmd_list(args)
  elif args.command == "summary":
    cmd_summary(args)
  elif args.command == "hold":
    cmd_hold(args)
  elif args.command == "release":
    cmd_release(args)
  elif args.command == "audit":
    cmd_audit(args)


if __name__ == "__main__":
  main()
