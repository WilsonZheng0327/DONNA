#!/usr/bin/env python3
"""DONNA Backend Sync Service.

Periodically queries Firebase, checks calendar status of booked users,
and updates Firebase with meeting status. Also handles 1-hour auto-release.
"""

import datetime
import sys
import time

import google.auth
import googleapiclient.discovery
import googleapiclient.errors

from google3.experimental.users.wilsonzheng.DONNA.src.agent import donna_cli


def get_calendar_service():
  scopes = ["https://www.googleapis.com/auth/calendar.readonly"]
  # Let credentials errors propagate to crash the script if not authenticated
  creds, _ = google.auth.default(scopes=scopes)
  return googleapiclient.discovery.build("calendar", "v3", credentials=creds)


def check_free_busy(service, emails, now_dt):
  # Query window: 5m around now
  time_min = (now_dt - datetime.timedelta(minutes=5)).isoformat()
  time_max = (now_dt + datetime.timedelta(minutes=5)).isoformat()

  body = {
      "timeMin": time_min,
      "timeMax": time_max,
      "items": [{"id": email} for email in emails],
  }

  response = service.freebusy().query(body=body).execute()
  result = {}
  for email in emails:
    busy_list = response.get("calendars", {}).get(email, {}).get("busy", [])
    is_busy = False
    for slot in busy_list:
      # Parse RFC3339 timestamps
      start_str = slot["start"].replace("Z", "+00:00")
      end_str = slot["end"].replace("Z", "+00:00")
      start = datetime.datetime.fromisoformat(start_str)
      end = datetime.datetime.fromisoformat(end_str)

      # Ensure now is offset-aware UTC
      now_utc = now_dt.replace(tzinfo=datetime.timezone.utc)
      if start <= now_utc <= end:
        is_busy = True
        break
    result[email] = is_busy
  return result


def to_email(user):
  clean = user.lstrip("@")
  if "@" in clean:
    return clean
  return f"{clean}@google.com"


def main():
  print("Starting DONNA Backend Sync Service...")
  calendar_service = get_calendar_service()

  # Configure check interval (default 5s)
  interval = 5

  while True:
    try:
      state = donna_cli.load_state()
      if not state:
        time.sleep(interval)
        continue

      now = time.time()
      now_dt = datetime.datetime.now(datetime.timezone.utc)

      emails_to_check = []
      booked_desks = []

      for floor, desks in state.items():
        if not isinstance(desks, dict):
          continue
        for desk_id, rec in desks.items():
          if not isinstance(rec, dict):
            continue

          hold_user = rec.get("hold_user")
          hold_expires = rec.get("hold_expires", 0)
          is_booked = hold_user and hold_expires > now

          if is_booked:
            email = to_email(hold_user)
            emails_to_check.append(email)
            booked_desks.append((floor, desk_id, rec))
          else:
            if "in_meeting" in rec:
              rec.pop("in_meeting", None)

      # Query Calendar
      busy_map = {}
      if emails_to_check:
        unique_emails = list(set(emails_to_check))
        busy_map = check_free_busy(calendar_service, unique_emails, now_dt)

      # Apply State Machine & Auto-Release
      state_changed = False

      for floor, desk_id, rec in booked_desks:
        hold_user = rec.get("hold_user")
        email = to_email(hold_user)
        is_busy = busy_map.get(email, False)

        # Update meeting status
        if rec.get("in_meeting") != is_busy:
          rec["in_meeting"] = is_busy
          rec["last_updated"] = int(now)
          state_changed = True
          print(
              f"[{datetime.datetime.now().isoformat()}] Floor {floor} Desk"
              f" {desk_id} user {hold_user} meeting status: {is_busy}"
          )

        # Auto-release check (1 hour stale hold)
        is_sensed = rec.get("occupied", False)
        hold_start = rec.get("hold_start")

        if not is_sensed and hold_start:
          age_s = now - hold_start
          if age_s > 3600:  # 1 hour
            print(
                f"[{datetime.datetime.now().isoformat()}] Auto-releasing stale"
                f" hold for floor {floor} desk {desk_id} (user {hold_user})"
            )
            rec.pop("hold_user", None)
            rec.pop("hold_start", None)
            rec.pop("hold_expires", None)
            rec.pop("in_meeting", None)
            rec["occupied"] = False
            rec["last_updated"] = int(now)
            state_changed = True

      if state_changed:
        donna_cli.save_state(state)

    except Exception as e:  # pylint: disable=broad-except
      print(f"[error] Error in sync loop: {e}", file=sys.stderr)

    time.sleep(interval)


if __name__ == "__main__":
  main()
