"""Agent Evaluation & Integration Tests for DONNA (`_agents/donna/`).

Validates agent metadata, persona instructions, skill configuration, and
executes simulated Critical User Journeys (CUJs) for natural language desk
queries, temporary holds, and LoRa hardware diagnostics.
"""

import io
import json
import os
import unittest
from unittest import mock

try:
  from experimental.users.wilsonzheng.hackathon.src.agent import donna_cli
except ImportError:
  import sys

  sys.path.append(
      os.path.join(os.path.dirname(os.path.dirname(__file__)), "src", "agent")
  )
  import donna_cli

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AGENT_DIR = os.path.join(REPO_ROOT, "_agents", "donna")


class DonnaAgentTest(unittest.TestCase):

  def test_agent_metadata_schema(self):
    """Validate _agents/donna/agent.json config and tool allowlist."""
    agent_json_path = os.path.join(AGENT_DIR, "agent.json")
    self.assertTrue(os.path.exists(agent_json_path))
    with open(agent_json_path, "r", encoding="utf-8") as f:
      cfg = json.load(f)
    self.assertEqual(cfg["name"], "DONNA")
    self.assertIn("customAgent", cfg["config"])
    tools = cfg["config"]["customAgent"]["toolNames"]
    self.assertIn("run_command", tools)

  def test_persona_instructions(self):
    """Validate DONNA persona in _agents/donna/AGENTS.md covers all CUJs."""
    agents_md_path = os.path.join(AGENT_DIR, "AGENTS.md")
    self.assertTrue(os.path.exists(agents_md_path))
    with open(agents_md_path, "r", encoding="utf-8") as f:
      content = f.read()
    self.assertIn("Querying Live Desks", content)
    self.assertIn("Reservations & Holds", content)
    self.assertIn("Hardware & Telemetry Diagnostics", content)

  def test_skills_frontmatter(self):
    """Validate SKILL.md frontmatter for donna-query, hold, and diagnostics."""
    skills = ["donna-query", "donna-hold", "donna-diagnostics"]
    for skill_name in skills:
      skill_path = os.path.join(AGENT_DIR, "SKILLS", skill_name, "SKILL.md")
      self.assertTrue(
          os.path.exists(skill_path), f"Missing SKILL.md for {skill_name}"
      )
      with open(skill_path, "r", encoding="utf-8") as f:
        lines = [line.strip() for line in f.readlines()[:15]]
      self.assertEqual(lines[0], "---")
      names = [line for line in lines if line.startswith("name:")]
      self.assertTrue(
          names, f"{skill_name} missing 'name:' in YAML frontmatter"
      )

  @mock.patch("time.time", return_value=1000.0)
  @mock.patch.object(donna_cli, "load_state")
  def test_cuj1_desk_query_workflow(self, mock_load, _):
    """CUJ 1: User asks for available desks -> agent calls donna_cli list."""
    now = 1000
    mock_load.return_value = {
        "4": {
            "4T01": {"occupied": False, "last_updated": now - 10},
            "4T02": {"occupied": True, "last_updated": now - 10},
        }
    }
    with mock.patch("sys.stdout", new_callable=io.StringIO) as fake_out:
      args = mock.Mock(floor="4", status="FREE")
      donna_cli.cmd_list(args)
      output = fake_out.getvalue()

    self.assertIn("4T01", output)
    self.assertIn("FREE", output)
    self.assertNotIn("4T02", output)

  @mock.patch("time.time", return_value=1000.0)
  @mock.patch.object(donna_cli, "save_state")
  @mock.patch.object(donna_cli, "load_state")
  def test_cuj2_desk_reservation_workflow(self, mock_load, mock_save, _):
    """CUJ 2: User holds desk -> prevents double booking -> releases."""
    now = 1000
    state = {"4": {"4T01": {"occupied": False, "last_updated": now - 10}}}
    mock_load.return_value = state

    # Step 1: Agent applies hold for @alice
    with mock.patch("sys.stdout", new_callable=io.StringIO) as fake_out:
      args = mock.Mock(desk="4T01", minutes=15, user="@alice")
      donna_cli.cmd_hold(args)
      output1 = fake_out.getvalue()
    self.assertIn("reserved** for **@alice**", output1)
    mock_save.assert_called_once()

    # Step 2: Agent blocks conflicting hold attempt by @bob
    with mock.patch("sys.stdout", new_callable=io.StringIO) as fake_out:
      args = mock.Mock(desk="4T01", minutes=15, user="@bob")
      donna_cli.cmd_hold(args)
      output2 = fake_out.getvalue()
    self.assertIn("already reserved by **@alice**", output2)

    # Step 3: @alice releases hold
    with mock.patch("sys.stdout", new_callable=io.StringIO) as fake_out:
      args = mock.Mock(desk="4T01", user="@alice", force=False)
      donna_cli.cmd_release(args)
      output3 = fake_out.getvalue()
    self.assertIn("Desk is now **FREE**", output3)

  @mock.patch("time.time", return_value=1000.0)
  @mock.patch.object(donna_cli, "load_state")
  def test_cuj3_hardware_diagnostics_workflow(self, mock_load, _):
    """CUJ 3: User asks for sensor audit -> flags weak RSSI & dying battery."""
    now = 1000
    mock_load.return_value = {
        "4": {
            "4T_FLAKY": {
                "occupied": False,
                "last_updated": now - 15,
                "rssi": -120,
                "snr": -12.5,
                "battery_mv": 3050,
            }
        }
    }
    with mock.patch("sys.stdout", new_callable=io.StringIO) as fake_out:
      args = mock.Mock(desk="4T_FLAKY")
      donna_cli.cmd_audit(args)
      output = fake_out.getvalue()

    self.assertIn("4T_FLAKY", output)
    self.assertIn("Weak link budget (RSSI -120 dBm)", output)
    self.assertIn("High RF noise/interference (SNR -12.5 dB)", output)
    self.assertIn("Low battery (3050 mV)", output)


if __name__ == "__main__":
  unittest.main()
