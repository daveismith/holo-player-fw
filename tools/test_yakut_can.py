#!/usr/bin/env python3
"""tools/yakut_can.py's refusals and its reading of `cyphal`, which need no adapter, no board and nothing
but the standard library (`make unit-test`). What it does with the adapter is not tested here."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import yakut_can  # noqa: E402


def refused(*args: str) -> bool:
    return yakut_can.refusal(list(args)) is not None


class Refusal(unittest.TestCase):
    def test_execute_command(self):
        # "execute": click 8.2 and later drop the "_command" suffix (Yakut 0.14 under click 8.3.1 registers it so)
        for name in ("execute-command", "execute_command", "execute", "cmd"):
            self.assertTrue(refused(name, "52", "restart"), name)
            self.assertTrue(refused("-v", name, "52", "65535"), name)
            self.assertTrue(refused("--format", "json", name, "52", "identify"), name)
            # --node-info takes a value, which isn't the command
            self.assertTrue(refused("--node-info", "{name: x}", name, "52", "restart"), name)

    def test_node_info_value(self):
        self.assertFalse(refused("--node-info", "cmd", "register-list", "52"))
        self.assertTrue(refused("--node-info", "register-list", "execute", "52", "restart"))

    def test_resolved_by_yakut(self):
        """With Yakut's click group (here a stand-in), a word it resolves to execute is refused whatever its spelling,
        and its own value-taking options are skipped"""
        names = {"exe": "execute", "x": "execute", "rl": "register-list", "call": "call"}
        resolver = yakut_can.Resolver(names.get, {"--new-option"})
        refuse = lambda *args: yakut_can.refusal(list(args), resolver) is not None  # noqa: E731
        self.assertTrue(refuse("exe", "52", "restart"))
        self.assertTrue(refuse("--new-option", "rl", "x", "52", "restart"))
        self.assertFalse(refuse("--new-option", "x", "rl", "52"))
        self.assertFalse(refuse("rl", "52"))
        self.assertTrue(refuse("call", "52", "435", "{}"))
        self.assertFalse(refuse("monitor"))     # resolved to nothing it knows

    def test_call_to_execute_command(self):
        for service in ("uavcan.node.ExecuteCommand.1.3", "uavcan.node.executecommand", "435",
                        "435:uavcan.node.ExecuteCommand.1.3", "435:uavcan.node.executecommand.1.1"):
            for name in ("call", "q"):
                self.assertTrue(refused(name, "52", service, "{command: 65535}"), (name, service))
                self.assertTrue(refused(name, "-T", "2", "52", service), (name, service))
                self.assertTrue(refused("-vv", name, "--timeout=3", "52", service), (name, service))
        # Other services, and the number 435 where it is no service
        self.assertFalse(refused("call", "52", "uavcan.node.GetInfo.1.0", "{}"))
        self.assertFalse(refused("q", "52", "430:uavcan.node.GetInfo.1.0"))
        self.assertFalse(refused("call", "52", "100:r2.holo.Motion.0.1", "{duration_ms: 435}"))
        self.assertFalse(refused("call", "52", "4350:r2.holo.Motion.0.1"))

    def test_node_id_plug_and_play(self):
        for name in ("register-access", "register_access", "r", "reg"):
            for value in ("65535", "[65535]", "0xFFFF", "65_535", "6.5535e4", "65535.0"):
                self.assertTrue(refused(name, "52", "uavcan.node.id", value), (name, value))
            self.assertTrue(refused("-v", name, "52", "uavcan.node.id", "65535"), name)
        # A read, another node-ID, another register
        self.assertFalse(refused("register-access", "52", "uavcan.node.id"))
        self.assertFalse(refused("register-access", "52", "uavcan.node.id", "53"))
        self.assertFalse(refused("r", "52", "uavcan.node.id", "127"))
        self.assertFalse(refused("register-access", "52", "uavcan.srv.ring.id", "65535"))
        self.assertFalse(refused("register-access", "52", "uavcan.pub.status.id", "2310"))

    def test_register_batch(self):
        for name in ("register-batch", "register_batch", "rbat", "rb"):
            self.assertTrue(refused(name, "52", "regs.yaml"), name)

    def test_allowed(self):
        self.assertFalse(refused("register-list", "52"))
        self.assertFalse(refused("rl", "52"))
        self.assertFalse(refused("monitor"))
        self.assertFalse(refused("--format", "yaml", "register-list", "52"))
        # A command word inside another command's arguments isn't the command
        self.assertFalse(refused("subscribe", "7509:uavcan.node.Heartbeat.1.0", "cmd"))
        self.assertFalse(refused("--transport", "cmd", "register-list", "52"))


class Status(unittest.TestCase):
    TAIL = " on GPIO21 (TX), GPIO33 (RX)\n  unique id 686f6c6f706c617900003c0f026f2db0, pseudo-ID 0b3af4648a21 (1)\n"

    def test_fixed(self):
        self.assertEqual(yakut_can.board_status("cyphal: node 52 (fixed), r2.dome.holo.front, 1 Mbit/s" + self.TAIL),
                         (52, 1_000_000))
        self.assertEqual(yakut_can.board_status("\ncyphal: listening, before going online as node 7 (fixed), "
                                                "r2.x, 500 kbit/s" + self.TAIL), (7, 500_000))
        self.assertEqual(yakut_can.board_status("cyphal: node 0 (fixed), a.b, 125 kbit/s" + self.TAIL), (0, 125_000))

    def test_not_fixed(self):
        for first in ("cyphal: node 125 (allocated), r2.dome.holo.front, 1 Mbit/s",
                      "cyphal: waiting for a node-ID, r2.dome.holo.front, 1 Mbit/s",
                      "cyphal: stopped: node-ID 52 is in use by another node, r2.dome.holo.front, 1 Mbit/s",
                      "cyphal: off (`link mode can` starts it)", ""):
            self.assertIsNone(yakut_can.board_status(first + self.TAIL), first)


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=0).result
    print(f"yakut_can: {result.testsRun} tests, {len(result.failures) + len(result.errors)} failed")
    sys.exit(0 if result.wasSuccessful() else 1)
