"""Test 5.2: the holo's registers (manual/reference/cyphal.md, "Registers", the holo's): an axis calibrated through
r2.holo.<axis>.endpoints_us, then moved, the motion using the new endpoints; the drive policy (release, settle_ms) and
touch reporting likewise; the console and the HTTP API showing the same values, the one store; a change made on the
console or over HTTP read over Cyphal at once (the servos' registers are read on the node's worker, from the servos
themselves); refusals. The servos' calibration and the board's settings are the user's: each test reads them first and
puts them back as they were, a calibration that wasn't saved cleared again (`holo endpoints <axis> clear`, which clears
the policy with it), and every endpoint written is inside the servos' default travel. Needs --board-host for the HTTP
side."""
from __future__ import annotations

import asyncio
import re

import pytest

from .conftest import access, bits, natural16, plain, string
from .holo_ports import Listen, motion

AXES = {"horizontal": ("h", "pan"), "vertical": ("v", "tilt")}


def servo(http, ident: str) -> dict:
    status, reply = http.call("GET", "/servos")
    assert status == 200, reply
    return next(s for s in reply["servos"] if s["id"] == ident)


def console_axis(board, letter: str) -> str:
    """The console's `holo` line for the axis: `closed 1150 open 1900 us, ...`"""
    m = re.search(rf"^\s+{letter}\s+\w+\s+(closed .*)$", board.command("holo"), re.M)
    assert m, board.command("holo")
    return m.group(1)


def endpoints_of(s: dict) -> list[int]:
    l = s["limits"]
    return [l["op_max_us"], l["op_min_us"]] if l["invert"] else [l["op_min_us"], l["op_max_us"]]


@pytest.fixture
def as_found(board, need_http):
    """Both servos' calibration and drive policy, put back after the test as they were found"""
    http = need_http
    found = {ident: servo(http, ident) for _, ident in AXES.values()}
    yield found
    board.command("holo off")
    for letter, ident in AXES.values():
        s = found[ident]
        if not s["limits"]["calibrated"]:
            out = board.command(f"holo holo endpoints {letter} clear")      # the range and the policy, unsaved again
            assert "UNSET" in out, out
        else:
            l = s["limits"]
            status, reply = http.call("PUT", "/servos/calibration", query={"id": ident},
                                      body={"min_us": l["op_min_us"], "max_us": l["op_max_us"], "invert": l["invert"]})
            assert status == 200, reply
            status, reply = http.call("PUT", "/servos/policy", query={"id": ident}, body=s["policy"])
            assert status == 200, reply
        now = servo(http, ident)
        assert now["limits"] == s["limits"] and now["policy"] == s["policy"], (ident, now, s)


async def move_to(node, server: int, http, ident: str, x: int, y: int) -> int:
    """A move at once to (x, y) over Cyphal, then the pulse the servo was given"""
    r = await motion(node, server, "move", x=x, y=y, duration_ms=0)
    assert r.code == 0, r
    await asyncio.sleep(0.3)
    return servo(http, ident)["us"]


@pytest.mark.parametrize("axis", ["horizontal", "vertical"])
async def test_endpoints(node, board, dut_node_id, http, as_found, axis):
    """Calibrate the axis through its register, move it to each end and the centre: the pulses are the new endpoints;
    turned round by closed above open; the console, HTTP and Status agree; refusals leave it alone; [0, 0] clears it"""
    letter, ident = AXES[axis]
    name = f"r2.holo.{axis}.endpoints_us"
    found = as_found[ident]
    default = endpoints_of(found)
    lo, hi = found["limits"]["op_min_us"], found["limits"]["op_max_us"]
    closed, opened = lo + 50, hi - 50           # inside the default travel: nothing driven against a stop
    assert plain((await access(node, dut_node_id, name)).value) == default
    listen = Listen(node, dut_node_id)
    try:
        r = await access(node, dut_node_id, name, natural16(closed, opened))
        assert plain(r.value) == [closed, opened] and r.mutable and r.persistent
        s = servo(http, ident)
        assert s["limits"]["calibrated"] and endpoints_of(s) == [closed, opened] and not s["limits"]["invert"]
        line = console_axis(board, letter)
        assert line.startswith(f"closed {closed} open {opened} us") and "not saved" not in line, line
        st = await listen.next_status(timeout=2.0, where=lambda m: getattr(m.holo, axis).calibrated)
        assert not getattr(st.holo, axis).inverted
        x, y = (100, 0) if axis == "horizontal" else (0, 100)
        assert await move_to(node, dut_node_id, http, ident, x, y) == opened
        assert await move_to(node, dut_node_id, http, ident, -x, -y) == closed
        assert abs(await move_to(node, dut_node_id, http, ident, 0, 0) - (closed + opened) / 2) <= 1
        # Turned round: closed above open
        r = await access(node, dut_node_id, name, natural16(opened, closed))
        assert plain(r.value) == [opened, closed]
        s = servo(http, ident)
        assert s["limits"]["invert"] and endpoints_of(s) == [opened, closed]
        assert console_axis(board, letter).startswith(f"closed {opened} open {closed} us")
        await listen.next_status(timeout=2.0, where=lambda m: getattr(m.holo, axis).inverted)
        assert await move_to(node, dut_node_id, http, ident, x, y) == closed
        # Refused: the register keeps its value
        for bad in (natural16(999, 1500), natural16(1500, 2001), natural16(1500, 1500), natural16(0, 1500),
                    natural16(1500), string("1200 1800")):
            r = await access(node, dut_node_id, name, bad)
            assert plain(r.value) == [opened, closed], (bad, plain(r.value))
        out = board.command(f"cyphal register {name} 1500 1500")
        assert "not written" in out and "not the same" in out, out
        # A change on the console reads over Cyphal at once
        board.command(f"holo holo endpoints {letter} {closed + 10} {opened - 10}")
        assert plain((await access(node, dut_node_id, name)).value) == [closed + 10, opened - 10]
        # The console's register is the same store
        out = board.command(f"cyphal register {name} {closed} {opened}")
        assert f"{closed} {opened}" in out, out
        assert endpoints_of(servo(http, ident)) == [closed, opened]
        # [0, 0]: the default range, not saved
        r = await access(node, dut_node_id, name, natural16(0, 0))
        assert plain(r.value) == default
        s = servo(http, ident)
        assert not s["limits"]["calibrated"] and endpoints_of(s) == default
        assert "default range, not saved" in console_axis(board, letter)
        await listen.next_status(timeout=2.0, where=lambda m: not getattr(m.holo, axis).calibrated)
        assert await move_to(node, dut_node_id, http, ident, x, y) == default[1]
    finally:
        listen.close()


async def test_drive_policy(node, board, dut_node_id, http, as_found):
    """release and settle_ms on each axis: the HTTP API's policy shows them; a settle time under 100 reads 100 (the
    kit's floor); refusals; a policy changed over HTTP reads over Cyphal"""
    for axis, (letter, ident) in AXES.items():
        rel, settle = f"r2.holo.{axis}.release", f"r2.holo.{axis}.settle_ms"
        p = as_found[ident]["policy"]
        assert plain((await access(node, dut_node_id, rel)).value) == [p[k] == "release" for k in ("closed", "open", "mid")]
        assert plain((await access(node, dut_node_id, settle)).value) == p["settle_ms"]
        r = await access(node, dut_node_id, rel, bits(True, False, True))
        assert plain(r.value) == [True, False, True]
        assert servo(http, ident)["policy"] == {**p, "closed": "release", "open": "hold", "mid": "release"}
        r = await access(node, dut_node_id, settle, natural16(500))
        assert plain(r.value) == 500 and servo(http, ident)["policy"]["settle_ms"] == 500
        r = await access(node, dut_node_id, settle, natural16(50))
        assert plain(r.value) == 100 and servo(http, ident)["policy"]["settle_ms"] == 100
        for bad, keeps in ((bits(True), [True, False, True]), (natural16(2, 0, 0), [True, False, True])):
            assert plain((await access(node, dut_node_id, rel, bad)).value) == keeps
        assert plain((await access(node, dut_node_id, settle, natural16(1, 2))).value) == 100
        status, reply = http.call("PUT", "/servos/policy", query={"id": ident},
                                  body={"closed": "hold", "open": "release", "mid": "hold", "settle_ms": 750})
        assert status == 200, reply
        assert plain((await access(node, dut_node_id, rel)).value) == [False, True, False]     # at once
        assert plain((await access(node, dut_node_id, settle)).value) == 750
        # The policy is the servo's: the console's register shows it too
        out = board.command(f"cyphal register {rel}")
        assert re.search(rf"{re.escape(rel)}\s+0 1 0\s+bit\[3\]", out), out


def saved_touch(http) -> bool:
    status, reply = http.call("GET", "/settings")
    assert status == 200, reply
    return reply["touch"]["reporting"]


async def test_touch_reporting(node, board, dut_node_id, need_http):
    """r2.touch.reporting: on and off at once, and saved, as the board's setting (GET /settings' touch.reporting, the
    console's `settings`), shown in Status; the console's `touch on` and `touch off` change what is in effect without
    saving it, which the register reads; PATCH /settings changes the same setting. Put back as found: the saved setting
    over HTTP, and what is in effect"""
    http = need_http
    name = "r2.touch.reporting"
    was_on = "touch on" in board.command("touch")
    saved_before = saved_touch(http)
    listen = Listen(node, dut_node_id)
    try:
        assert plain((await access(node, dut_node_id, name)).value) == [was_on]
        r = await access(node, dut_node_id, name, bits(True))
        assert plain(r.value) == [True] and r.mutable and r.persistent
        assert "touch on" in board.command("touch")
        await listen.next_status(timeout=2.0, where=lambda m: m.reporting_touch)
        assert saved_touch(http) is True
        assert re.search(r"^touch reporting on$", board.command("settings"), re.M)
        r = await access(node, dut_node_id, name, bits(False))
        assert plain(r.value) == [False] and "touch off" in board.command("touch") and saved_touch(http) is False
        await listen.next_status(timeout=2.0, where=lambda m: not m.reporting_touch)
        board.command("touch on")       # for the moment: the register reads what is in effect, the setting stays
        assert plain((await access(node, dut_node_id, name)).value) == [True] and saved_touch(http) is False
        assert "touch reporting off (on until the next start: `touch on`)" in board.command("settings")
        board.command("touch off")
        assert plain((await access(node, dut_node_id, name, natural16(2))).value) == [False]   # not a bit
        out = board.command(f"cyphal register {name} 1")
        assert f"cyphal: {name} = 1" in out, out
        assert "touch on" in board.command("touch") and saved_touch(http) is True
        # The same setting over HTTP: saved and applied
        status, reply = http.call("PATCH", "/settings", body={"touch": {"reporting": False}})
        assert status == 200 and reply["settings"]["touch"]["reporting"] is False, reply
        assert plain((await access(node, dut_node_id, name)).value) == [False] and "touch off" in board.command("touch")
        # A PATCH of something else leaves what is in effect alone
        board.command("touch on")
        status, reply = http.call("PATCH", "/settings", body={"screen": {"backlight": reply["settings"]["screen"]
                                                                                       ["backlight"]}})
        assert status == 200 and "touch on" in board.command("touch"), reply
        status, reply = http.call("PATCH", "/settings", body={"touch": {"reporting": 1}})
        assert status == 400 and "touch" in reply["message"], reply
    finally:
        listen.close()
        status, reply = http.call("PATCH", "/settings", body={"touch": {"reporting": saved_before}})
        assert status == 200, reply
        board.command("touch on" if was_on else "touch off")
        assert saved_touch(http) is saved_before and ("touch on" in board.command("touch")) == was_on
