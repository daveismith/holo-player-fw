"""Test 2.5: uavcan.node.ExecuteCommand (manual/reference/cyphal.md, "Commands"): restart, factory reset,
identify and store persistent states; every other command refused as a bad command; and, with the
worker's job slots all taken, failure at once rather than a wait.

The board restarts with the adapter open (the command came through it), so the harness sends nothing
from the response until the board is back and acknowledging (Net.muted())."""
from __future__ import annotations

import asyncio
import re

import pycyphal.application.plug_and_play
import pytest
import uavcan.node.ExecuteCommand_1_3 as ExecuteCommand
import uavcan.register.Access_1_0 as Access
import uavcan.register.Name_1_0 as Name

from .conftest import (access, bits, call, console_registers, ensure_node, heartbeats, natural16, plain,
                       reference_registers, string, until_state)

Request, Response = ExecuteCommand.Request, ExecuteCommand.Response
IDENTIFY_S = 5.0
ALLOCATED = 125     # what pycyphal's allocator gives (test_pnp.py)


async def command(node, server: int, number: int, timeout: float = 2.0) -> tuple[int, float]:
    """(the response's status, seconds it took)"""
    loop = asyncio.get_running_loop()
    t0 = loop.time()
    response = await call(node, ExecuteCommand, server, Request(command=number), timeout)
    assert len(response.output) == 0
    return response.status, loop.time() - t0


def leds_and_screen(board) -> tuple[str, str]:
    return board.first_line("leds"), board.first_line("screen")


def worker_stack(log: str) -> str:
    """The worker's stack as it logs it before a restart: the only measure of the restart paths"""
    m = re.search(r"worker: its stack \d+ of \d+ bytes never used, before the restart", log)
    return m.group(0) if m else "(the worker's stack line not seen)"


async def poll(look, done, cap: float):
    """look() every half second until done() says its result will do, or `cap` seconds have passed: the last
    result. The console is looked at between sleeps, so the event loop (pycyphal) runs meanwhile"""
    end = asyncio.get_running_loop().time() + cap
    while not done(seen := look()) and asyncio.get_running_loop().time() < end:
        await asyncio.sleep(0.5)
    return seen


async def test_bad_commands_and_store(node, board, dut_node_id):
    """Power off, begin a software update, emergency stop and a vendor's command: bad command. Store
    persistent states: success, with nothing to do"""
    before = board.cyphal()["services"]
    refused = [Request.COMMAND_POWER_OFF, Request.COMMAND_BEGIN_SOFTWARE_UPDATE, Request.COMMAND_EMERGENCY_STOP, 12345, 0]
    for number in refused:
        status, took = await command(node, dut_node_id, number)
        print(f"\ncommand {number}: status {status} in {took * 1000:.0f} ms")
        assert status == Response.STATUS_BAD_COMMAND
    status, _ = await command(node, dut_node_id, Request.COMMAND_STORE_PERSISTENT_STATES)
    assert status == Response.STATUS_SUCCESS
    after = board.cyphal()["services"]
    assert after["commands"] - before["commands"] == len(refused) + 1
    assert after["commands_refused"] - before["commands_refused"] == len(refused)
    assert board.cyphal()["state"] == "online"


async def test_identify(node, board, dut_node_id):
    """Identify: the ring blinks white and the screen shows the crosshair, for 5 s; then both go back to what
    they were doing. Seen on the console"""
    try:
        board.command("leds colour blue")
        board.command("screen colour green")
        before = leds_and_screen(board)
        status, took = await command(node, dut_node_id, Request.COMMAND_IDENTIFY)
        assert status == Response.STATUS_SUCCESS
        t0 = asyncio.get_running_loop().time()
        during = leds_and_screen(board)
        after = await poll(lambda: leds_and_screen(board), lambda now: now == before, IDENTIFY_S + 3.0)
        print(f"\nanswered in {took * 1000:.0f} ms; back {asyncio.get_running_loop().time() - t0:.1f} s later\n"
              f"before {before}\nduring {during}\nafter  {after}")
        assert before[0].startswith("solid #0000ff") and "#00ff00" in before[1], before
        assert during[0].startswith("blink #ffffff, every 1000 ms") and "calibration" in during[1], during
        assert after == before
    finally:
        board.command("leds off")
        board.command("screen clear")


async def test_busy(node, board, dut_node_id):
    """Six identify commands at once: identify applies a scene, so it is the second worker's, which holds two
    (CYPHAL_SLOW_JOBS), and the rest are answered at once with failure, never waited for. All six answered; the
    board counts the busy ones"""
    before = board.cyphal()["services"]
    try:
        results = await asyncio.gather(*[command(node, dut_node_id, Request.COMMAND_IDENTIFY) for _ in range(6)])
        after = board.cyphal()["services"]      # busy is counted as each is answered
        # Then the identifies over (each a scene), before the ring and the screen are put back
        await poll(lambda: board.command("scene"), lambda text: "running: identify" not in text, IDENTIFY_S + 5.0)
    finally:
        board.command("leds off")
        board.command("screen clear")
    statuses = [s for s, _ in results]
    print(f"\nstatuses {statuses}; seconds {[round(t, 3) for _, t in results]}; busy {after['busy'] - before['busy']}")
    failed = statuses.count(Response.STATUS_FAILURE)
    assert set(statuses) <= {Response.STATUS_SUCCESS, Response.STATUS_FAILURE}, statuses
    assert statuses.count(Response.STATUS_SUCCESS) >= 2 and failed >= 1, statuses
    assert after["busy"] - before["busy"] == failed


async def test_identify_during_register_writes(node, board, dut_node_id):
    """Identify (a scene: the second worker's) answered while register writes (the worker's) are answered, round
    after round: the two workers build their responses at the same moments, and each must build its own in its own
    memory. Every Access response carries the value written (each write is of the register's own value, nonzero, so
    nothing changes and a response zeroed under it shows), every identify its own status, success, with no output
    (command() checks); nothing dropped or answered busy. A clash is a matter of microseconds, so this is many
    rounds, two writes in flight throughout each identify"""
    names = ["uavcan.srv.ring.id", "uavcan.srv.scene.id", "uavcan.pub.event.id", "uavcan.srv.screen.id"]
    values = {n: plain((await access(node, dut_node_id, n)).value) for n in names}
    assert all(values.values()), values
    rounds = 60
    before = board.cyphal()
    wrong: list[str] = []
    statuses: list[int] = []
    writes = 0

    async def writer(first: int, done: asyncio.Event) -> None:
        nonlocal writes
        i = first
        while not done.is_set():
            name = names[i % len(names)]
            i += 2
            r = await call(node, Access, dut_node_id, Access.Request(name=Name(name), value=natural16(values[name])),
                           timeout=2.0)
            writes += 1
            if plain(r.value) != values[name] or not r.persistent:
                wrong.append(f"{name}: {r}")

    try:
        for _ in range(rounds):
            done = asyncio.Event()
            tasks = [asyncio.create_task(writer(k, done)) for k in range(2)]
            await asyncio.sleep(0.01)
            status, _ = await command(node, dut_node_id, Request.COMMAND_IDENTIFY)
            statuses.append(status)
            await asyncio.sleep(0.02)
            done.set()
            await asyncio.gather(*tasks)
        await poll(lambda: board.command("scene"), lambda text: "running: identify" not in text, IDENTIFY_S + 5.0)
    finally:
        board.command("leds off")
        board.command("screen clear")
    after = board.cyphal()
    print(f"\n{rounds} identifies, {writes} register writes during them; statuses {sorted(set(statuses))}; busy "
          f"{after['services']['busy'] - before['services']['busy']}; dropped {after['dropped'] - before['dropped']}")
    assert not wrong, "\n".join(wrong[:20])
    assert statuses == [Response.STATUS_SUCCESS] * rounds, statuses
    assert after["services"]["register_writes"] - before["services"]["register_writes"] == writes
    assert after["services"]["busy"] == before["services"]["busy"]
    assert after["dropped"] == before["dropped"]
    assert writes >= 2 * rounds


@pytest.mark.restarts
async def test_restart(node, net, board, dut_node_id):
    """Restart: the response, then the board restarts, and its node comes back on its node-ID. The response
    must arrive (call() fails without it), and promptly: the board restarts a second after the worker has
    answered, and a response held to the node's next timer would barely make it out, or not at all"""
    mark = board.mark()
    status, took = await command(node, dut_node_id, Request.COMMAND_RESTART)
    assert status == Response.STATUS_SUCCESS
    assert took < 0.25, f"the response took {took * 1000:.0f} ms"
    async with net.muted():
        await board.restarted(mark)
        st = await until_state(board, "online", dut_node_id, timeout=20)
    beats = await heartbeats(node, dut_node_id, 2.5)
    log = board.since(mark)
    print(f"\nanswered in {took * 1000:.0f} ms; frames held back while the board restarted: {net.dropped}; uptime now "
          f"{[b[2].uptime for b in beats]}; {worker_stack(log)}\n{st['text']}")
    assert f"node {net.local_node_id} asks for a restart" in log, log[-2000:]
    assert beats and beats[0][2].uptime < 30, "no heartbeat, or not from a fresh start"


def board_settings(board) -> dict:
    """`settings`: the boot scene ("" for none), the backlight, the LEDs' brightness and count, touch reporting"""
    text = board.command("settings")
    boot = re.search(r"at start: (.+)", text)
    levels = re.search(r"backlight (\d+)%, LED brightness (\d+)%, (\d+) LEDs", text)
    touch = re.search(r"^touch reporting (on|off)", text, re.M)
    assert boot and levels and touch, text
    return {"boot": "" if boot.group(1).strip() == "nothing" else boot.group(1).strip(),
            "backlight": int(levels.group(1)), "brightness": int(levels.group(2)), "leds": int(levels.group(3)),
            "touch": touch.group(1)}


def put_settings_back(board, was: dict) -> None:
    """The board's settings as board_settings() read them, from the console (`settings touch` turns touch reporting
    on or off now too: what is in effect is the caller's to put back after)"""
    board.command(f"scene boot {was['boot']}" if was["boot"] else "scene boot --clear")
    board.command(f"settings backlight {was['backlight']}")
    board.command(f"settings brightness {was['brightness']}")
    board.command(f"settings leds {was['leds']}")
    board.command(f"settings touch {was['touch']}")


def put_registers_back(board, was: dict[str, str], defaults: dict[str, tuple[str, object, bool]]) -> None:
    """After `cyphal reset`: each mutable register but the node-ID (the suite's, ensure_node()'s) that was not
    at its default, written back from the console as `cyphal register` showed it"""
    for name, shown in was.items():
        _, default, mutable = defaults[name]
        default_shown = " ".join(str(int(v)) for v in default) if isinstance(default, list) else str(default)
        if mutable and name != "uavcan.node.id" and shown != default_shown:
            out = board.command(f"cyphal register {name} {shown}")
            assert "not written" not in out, out


@pytest.mark.restarts
async def test_factory_reset(node, net, board, dut_node_id, request):
    """Factory reset: every register back to its default (the node-ID to plug-and-play), and the board's
    settings (touch reporting among them); then a restart. The node comes back asking for a node-ID, which
    pycyphal's allocator gives. Kept: the scenes, and the host link (or the node wouldn't be on the bus at all).
    Everything the reset touches (the board's settings, every register, touch reporting in effect) is read first
    and put back afterwards, from the console.
    Only at 1 Mbit/s: the node comes back at its default bitrate, which the harness has to be at"""
    if request.config.getoption("--can-bitrate") != 1_000_000:
        pytest.skip("the node comes back from a factory reset at 1 Mbit/s, and the harness isn't at that")
    settings_was = board_settings(board)
    registers_was = console_registers(board.command("cyphal register"))
    defaults = reference_registers()
    assert set(registers_was) == set(defaults), registers_was
    scenes = board.command("scene")
    touch_on = "touch on" in board.command("touch")
    allocator = None
    try:
        assert plain((await access(node, dut_node_id, "uavcan.pub.status.id", natural16(2310))).value) == 2310
        assert plain((await access(node, dut_node_id, "r2.node.name", string("r2.dome.holo.top"))).value)
        board.command("settings backlight 50")
        assert plain((await access(node, dut_node_id, "r2.touch.reporting", bits(True))).value) == [True]
        allocator = pycyphal.application.plug_and_play.CentralizedAllocator(node)
        mark = board.mark()
        status, _ = await command(node, dut_node_id, Request.COMMAND_FACTORY_RESET)
        assert status == Response.STATUS_SUCCESS
        async with net.muted():
            await board.restarted(mark)
            await until_state(board, "waiting", timeout=20)     # its controller is up, acknowledging
        st = await until_state(board, "online", ALLOCATED, timeout=10)
        assert st["how"] == "allocated" and st["name"] == "r2.dome.holo.front", st["text"]
        assert plain((await access(node, ALLOCATED, "uavcan.node.id")).value) == 65535
        assert plain((await access(node, ALLOCATED, "uavcan.pub.status.id")).value) == 2300
        assert plain((await access(node, ALLOCATED, "r2.node.name")).value) == "r2.dome.holo.front"
        after = board.command("settings")
        print(f"\n{worker_stack(board.since(mark))}\n{st['text']}\n{after}\nsettings before {settings_was}")
        assert "at start: nothing" in after and "backlight 100%" in after, after
        # Touch reporting back to the build's default (off), one of the board's settings: off from the start
        assert re.search(r"^touch reporting off$", after, re.M) and "touch off" in board.command("touch"), after
        assert board.command("scene") == scenes
        assert f"node {net.local_node_id} asks for a factory reset: done" in board.since(mark)
    finally:
        if allocator is not None:
            for port in (allocator._sub1, allocator._sub2, allocator._pub1, allocator._pub2):
                port.close()
        put_settings_back(board, settings_was)
        board.command("touch on" if touch_on else "touch off")
        board.command("cyphal reset")
        put_registers_back(board, registers_was, defaults)
        ensure_node(board, dut_node_id)
    assert board_settings(board) == settings_was
    shown = console_registers(board.command("cyphal register"))
    assert shown == registers_was, f"registers not as they were: {shown}"
