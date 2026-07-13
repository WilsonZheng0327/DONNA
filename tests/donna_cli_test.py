"""Unit tests for DONNA CLI tool (`donna_cli.py`).

Fast, deterministic tests validating desk status rules, hold expiration,
formatting, and diagnostic audit warnings.
"""

import io
import unittest
from unittest import mock

try:
  from experimental.users.wilsonzheng.hackathon.src.agent import donna_cli
except ImportError:
  import os
  import sys

  sys.path.append(
      os.path.join(os.path.dirname(os.path.dirname(__file__)), "src", "agent")
  )
  import donna_cli


class DonnaCliTest(unittest.TestCase):

  def test_get_desk_status_free(self):
    now = 1000
    rec = {"occupied": False, "last_updated": now - 30}
    self.assertEqual(donna_cli.get_desk_status(rec, now), "FREE")

  def test_get_desk_status_occupied(self):
    now = 1000
    rec = {"occupied": True, "last_updated": now - 10}
    self.assertEqual(donna_cli.get_desk_status(rec, now), "OCCUPIED")

  def test_get_desk_status_offline(self):
    now = 1000
    rec = {"occupied": False, "last_updated": now - 100}
    self.assertEqual(donna_cli.get_desk_status(rec, now), "OFFLINE")

  def test_get_desk_status_reserved_hold(self):
    now = 1000
    rec = {
        "occupied": True,
        "last_updated": now - 5,
        "hold_user": "@wilsonzheng",
        "hold_expires": now + 600,
    }
    self.assertEqual(donna_cli.get_desk_status(rec, now), "RESERVED_HOLD")

  def test_format_duration(self):
    self.assertEqual(donna_cli.format_duration(45), "45s ago")
    self.assertEqual(donna_cli.format_duration(135), "2m 15s ago")

  @mock.patch("time.time", return_value=1000.0)
  @mock.patch.object(donna_cli, "load_state")
  def test_cmd_summary_output(self, mock_load, _):
    now = 1000
    mock_load.return_value = {
        "4": {
            "4T01": {"occupied": False, "last_updated": now - 10},
            "4T02": {"occupied": True, "last_updated": now - 20},
            "4T03": {"occupied": False, "last_updated": now - 200},
        }
    }

    with mock.patch("sys.stdout", new_callable=io.StringIO) as fake_out:
      args = mock.Mock(floor=None)
      donna_cli.cmd_summary(args)
      output = fake_out.getvalue()

    self.assertIn("Total Desks Tracked:** 3", output)
    self.assertIn("Available (Free):** 1", output)
    self.assertIn("In Use / Held:** 1", output)
    self.assertIn("Offline / Unreachable:** 1", output)

  @mock.patch("time.time", return_value=1000.0)
  @mock.patch.object(donna_cli, "load_state")
  def test_cmd_audit_warnings(self, mock_load, _):
    now = 1000
    mock_load.return_value = {
        "4": {
            "4T01": {
                "occupied": False,
                "last_updated": now - 10,
                "rssi": -118,
                "battery_mv": 3050,
            },
            "4T02": {
                "occupied": False,
                "last_updated": now - 10,
                "rssi": -70,
                "battery_mv": 3900,
            },
        }
    }

    with mock.patch("sys.stdout", new_callable=io.StringIO) as fake_out:
      args = mock.Mock(desk=None)
      donna_cli.cmd_audit(args)
      output = fake_out.getvalue()

    self.assertIn("Healthy Sensors:** 1", output)
    self.assertIn("Sensors with Warnings / Alerts:** 1", output)
    self.assertIn("4T01", output)
    self.assertIn("Weak link budget (RSSI -118 dBm)", output)
    self.assertIn("Low battery (3050 mV)", output)

  def test_cmd_hold_invalid_duration(self):
    with mock.patch("sys.stdout", new_callable=io.StringIO) as fake_out:
      args = mock.Mock(desk="4T01", minutes=500, user="@test")
      donna_cli.cmd_hold(args)
      output = fake_out.getvalue()
    self.assertIn("Invalid hold duration", output)

  @mock.patch("time.time", return_value=1000.0)
  @mock.patch.object(donna_cli, "load_state")
  def test_cmd_hold_conflict(self, mock_load, _):
    now = 1000
    mock_load.return_value = {
        "4": {
            "4T01": {
                "occupied": True,
                "last_updated": now - 10,
                "hold_user": "@alice",
                "hold_expires": now + 600,
            }
        }
    }
    with mock.patch("sys.stdout", new_callable=io.StringIO) as fake_out:
      args = mock.Mock(desk="4T01", minutes=15, user="@bob")
      donna_cli.cmd_hold(args)
      output = fake_out.getvalue()
    self.assertIn("already reserved by **@alice**", output)

  @mock.patch("time.time", return_value=1000.0)
  @mock.patch.object(donna_cli, "load_state")
  def test_cmd_release_permission_denied(self, mock_load, _):
    now = 1000
    mock_load.return_value = {
        "4": {
            "4T01": {
                "occupied": True,
                "last_updated": now - 10,
                "hold_user": "@alice",
                "hold_expires": now + 600,
            }
        }
    }
    with mock.patch("sys.stdout", new_callable=io.StringIO) as fake_out:
      args = mock.Mock(desk="4T01", user="@bob", force=False)
      donna_cli.cmd_release(args)
      output = fake_out.getvalue()
    self.assertIn("Permission denied", output)

  @mock.patch("time.time", return_value=1000.0)
  @mock.patch.object(donna_cli, "save_state")
  @mock.patch.object(donna_cli, "load_state")
  def test_cmd_release_force_override(self, mock_load, mock_save, _):
    now = 1000
    mock_load.return_value = {
        "4": {
            "4T01": {
                "occupied": True,
                "last_updated": now - 10,
                "hold_user": "@alice",
                "hold_expires": now + 600,
            }
        }
    }
    with mock.patch("sys.stdout", new_callable=io.StringIO) as fake_out:
      args = mock.Mock(desk="4T01", user="@admin", force=True)
      donna_cli.cmd_release(args)
      output = fake_out.getvalue()
    self.assertIn("Released hold on desk `4T01`", output)
    mock_save.assert_called_once()


if __name__ == "__main__":
  unittest.main()
