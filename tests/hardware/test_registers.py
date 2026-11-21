"""Tests 2.2 to 2.4: uavcan.register.List and Access over the node's register table, and
uavcan.node.port.List (manual/reference/cyphal.md, "Registers" and "The node's ports"). The registers
are the node's saved settings: a write applies from the node's next start, and the console's `cyphal`
and `cyphal register` show the same values."""
from __future__ import annotations

import asyncio
import random
import statistics

import pycyphal.dsdl
import pytest
import uavcan.node.ExecuteCommand_1_3 as ExecuteCommand
import uavcan.node.GetInfo_1_0 as GetInfo
import uavcan.node.Heartbeat_1_0 as Heartbeat
import uavcan.node.port.List_1_0 as PortList
import uavcan.primitive.array.Natural8_1_0 as Natural8
import uavcan.register.Access_1_0 as Access
import uavcan.register.List_1_0 as RegisterList
import uavcan.register.Name_1_0 as Name

from .conftest import (Value, access, call, console_registers, heartbeats, natural16, natural32, plain,
                       reference_registers, register_names, string, until_state)

# What the node has while online, with the holo's ports at their defaults: the node's own, and the holo's built so
# far (status 2300, axes 2302; setpoint 2303; motion 100, light 101)
PUBLISHED = {pycyphal.dsdl.get_fixed_port_id(t) for t in (Heartbeat, PortList)} | {2300, 2302}
SUBSCRIBED = {2303}
SERVED = {pycyphal.dsdl.get_fixed_port_id(t) for t in (Access, RegisterList, GetInfo, ExecuteCommand)} | {100, 101}


async def test_list_and_read_every_register(node, board, dut_node_id):
    """2.2: List walks every register, Access reads each, and the set is the reference page's: names,
    types, defaults and flags. The console lists the same values."""
    want = reference_registers()
    names = await register_names(node, dut_node_id)
    print(f"\n{len(names)} registers listed: {names}")
    assert len(names) == len(set(names)), "a register listed twice"
    assert set(names) == set(want), f"not on the page: {set(names) - set(want)}; missing: {set(want) - set(names)}"
    shown = console_registers(board.command("cyphal register"))
    assert set(shown) == set(want), shown
    assert want["uavcan.node.id"][1] == 65535
    want["uavcan.node.id"] = ("natural16[1]", dut_node_id, True)    # the session's fixed node-ID, not the default
    for name in names:
        r = await access(node, dut_node_id, name)
        kind, default, mutable = want[name]
        value = plain(r.value)
        assert value == default, f"{name} is {value!r}, not the page's default {default!r}"
        assert bool(r.mutable) == mutable and r.persistent, f"{name}: mutable {r.mutable}, persistent {r.persistent}"
        assert (r.value.string is not None) == (kind == "string"), name
        assert (r.value.natural32 is not None) == (kind == "natural32[2]"), name
        assert (r.value.natural16 is not None) == (kind == "natural16[1]"), name
        shown_value = " ".join(str(v) for v in value) if isinstance(value, list) else str(value)
        assert shown[name] == shown_value, f"the console shows {name} as {shown[name]!r}"
    # Not a register: an empty value
    r = await access(node, dut_node_id, "uavcan.node.description")
    assert plain(r.value) is None and not r.mutable and not r.persistent
    after = board.cyphal()
    assert "registers read" in after["text"], after["text"]
    print(after["text"])


async def test_writes_refused(node, board, dut_node_id):
    """Writes the table refuses leave the register as it was, and the response says so by its value"""
    cases = [
        ("uavcan.pub.status.id", natural16(8192), 2300),            # a subject-ID is 0-8191
        ("uavcan.srv.motion.id", natural16(512), 100),              # a service-ID is 0-511
        ("uavcan.pub.status.id", natural16(2301), 2300),            # event's
        ("uavcan.sub.setpoint.id", natural16(2302), 2303),          # axes' (published): subjects are one set
        ("uavcan.srv.motion.id", natural16(435), 100),              # the node's own ExecuteCommand
        ("uavcan.pub.axes.id", natural16(7509), 2302),              # the node's own heartbeat
        ("uavcan.srv.motion.type", string("r2.holo.Motion.0.2"), "r2.holo.Motion.0.1"),     # immutable
        ("uavcan.node.id", natural16(200), None),                   # 0-127 or 65535
        ("uavcan.node.id", string("52"), None),                     # not a number
        ("r2.node.name", string("R2.Dome"), None),                  # not a name
        ("uavcan.can.bitrate", natural32(800000, 800000), [1000000, 1000000]),
    ]
    refused_before = board.cyphal()["services"]["register_refused"]
    for name, value, keeps in cases:
        current = plain((await access(node, dut_node_id, name)).value)
        r = await access(node, dut_node_id, name, value)
        print(f"\n{name} <- {value}: {plain(r.value)!r}")
        assert plain(r.value) == current, f"{name} changed to {plain(r.value)!r}"
        if keeps is not None:
            assert current == keeps
    # A write to a register that isn't there: empty, and nothing else changes
    r = await access(node, dut_node_id, "uavcan.pub.nope.id", natural16(1))
    assert plain(r.value) is None
    # The console gives the reason for the same rule
    out = board.command("cyphal register uavcan.pub.status.id 2301")
    assert "not written: subject 2301 is uavcan.pub.event.id's" in out, out
    st = board.cyphal()
    print(st["text"])
    assert "from the next start" not in st["text"], f"a refused write was saved:\n{st['text']}"
    assert st["services"]["register_refused"] == refused_before + len(cases), "each refusal counted"


async def test_worker_responses_prompt(node, board, dut_node_id):
    """A response the worker finishes while the node's task is sending something else goes at once, not at
    the task's next timer, up to a heartbeat period (1 s) later: the worker's wake, read while the task
    sends, must not be lost. Each round, a register write (the worker's: a few ms of NVS) and with it six
    GetInfo calls, whose responses (about 66 frames) keep the task sending while the write finishes, and
    then nothing more from the harness until its next heartbeat. The rounds are spread at random over the
    heartbeat period. Each write is of the register's own value, so nothing changes. Every write's response
    well under the old worst case, and the distribution printed"""
    names = ["uavcan.srv.ring.id", "uavcan.srv.scene.id", "uavcan.pub.event.id"]
    values = {n: plain((await access(node, dut_node_id, n)).value) for n in names}
    rounds = 90
    before = board.cyphal()["services"]
    loop = asyncio.get_running_loop()
    rng = random.Random(20261120)
    latencies: list[float] = []

    async def timed(name: str) -> None:
        t0 = loop.time()
        r = await call(node, Access, dut_node_id, Access.Request(name=Name(name), value=natural16(values[name])),
                       timeout=2.0)
        latencies.append(loop.time() - t0)
        assert plain(r.value) == values[name]

    for i in range(rounds):
        await asyncio.gather(timed(names[i % len(names)]),
                             *(call(node, GetInfo, dut_node_id, GetInfo.Request(), timeout=2.0) for _ in range(6)))
        await asyncio.sleep(rng.uniform(0.0, 1.0))
    after = board.cyphal()
    ms = sorted(t * 1000 for t in latencies)
    q = statistics.quantiles(ms, n=100, method="inclusive")
    print(f"\n{len(ms)} register writes, ms: min {ms[0]:.1f}, median {statistics.median(ms):.1f}, p90 {q[89]:.1f}, "
          f"p99 {q[98]:.1f}, max {ms[-1]:.1f}; over 100 ms: {sum(t > 100 for t in ms)}, over 250 ms: "
          f"{sum(t > 250 for t in ms)}\n{after['text']}")
    assert after["services"]["busy"] == before["busy"], "a write answered busy"
    assert after["services"]["register_writes"] - before["register_writes"] == rounds
    assert "from the next start" not in after["text"]
    assert ms[-1] < 250, f"a response took {ms[-1]:.0f} ms"


@pytest.mark.restarts
async def test_port_register_and_port_list(node, net, board, dut_node_id):
    """2.3: a port register written and the node restarted: the register holds the new number, the new number
    is in uavcan.node.port.List and the old one isn't, and Status goes out on the new subject and nothing on
    the old"""
    name = "uavcan.pub.status.id"
    try:
        r = await access(node, dut_node_id, name, natural16(2310))
        assert plain(r.value) == 2310
        assert plain((await access(node, dut_node_id, name)).value) == 2310
        line = board.command(f"cyphal register {name}")
        assert "2310" in line and "2300 until the node's next start" in line, line
        assert f"and {name} 2310" in board.cyphal()["text"]
        # A numeric value of another width converts
        r = await access(node, dut_node_id, "uavcan.srv.ring.id", Value(natural8=Natural8([104])))
        assert plain(r.value) == 104

        sub = node.make_subscriber(PortList)
        board.command("cyphal restart")
        await until_state(board, "online", dut_node_id)
        cap = net.capture()         # from the new start: the old one published Status on 2300 until it stopped
        loop = asyncio.get_running_loop()
        lists, end = [], loop.time() + 12.0
        while (left := end - loop.time()) > 0 and len(lists) < 2:
            got = await sub.receive_for(left)
            if got is not None and got[1].source_node_id == dut_node_id:
                lists.append((got[1].timestamp.monotonic, got[0]))     # its arrival, not when it was read
        sub.close()
        frames = cap.stop()
        assert plain((await access(node, dut_node_id, name)).value) == 2310
        assert "until the node's next start" not in board.command(f"cyphal register {name}")
        assert len(lists) == 2, f"{len(lists)} port lists in 12 s of the node's start"
        period = lists[1][0] - lists[0][0]
        print(f"\nport lists {period:.2f} s apart; {lists[0][1]}")
        assert 9.0 <= period <= 11.0, period
        for _, m in lists:
            assert m.publishers.sparse_list is not None
            assert {int(s.value) for s in m.publishers.sparse_list} == PUBLISHED - {2300} | {2310}
            assert m.subscribers.sparse_list is not None
            assert {int(s.value) for s in m.subscribers.sparse_list} == SUBSCRIBED
            assert not m.clients.mask.any()
            assert {i for i, on in enumerate(m.servers.mask) if on} == SERVED
        subjects = {f.subject for f in frames if not f.own and f.source == dut_node_id and f.subject is not None}
        assert 2310 in subjects and 2300 not in subjects, f"the board published on {sorted(subjects)}"
    finally:
        # From the console, which works whatever became of the node
        board.command(f"cyphal register {name} 2300")
        board.command("cyphal register uavcan.srv.ring.id 104")
        board.command("cyphal restart")
        await until_state(board, "online", dut_node_id)
    assert "from the next start" not in board.cyphal()["text"]


@pytest.mark.restarts
async def test_name_and_node_id_by_register(node, net, board, dut_node_id):
    """2.4: the name and the node-ID written through registers: after a restart of the node, GetInfo and
    the heartbeat's source follow, and the console shows the same values"""
    other, new_name = dut_node_id + 1, "r2.dome.holo.top"
    default = plain((await access(node, dut_node_id, "r2.node.name")).value)
    try:
        assert plain((await access(node, dut_node_id, "r2.node.name", string(new_name))).value) == new_name
        assert plain((await access(node, dut_node_id, "uavcan.node.id", natural16(other))).value) == other
        st = board.cyphal()
        assert f"from the next start (`cyphal restart`): node-ID {other} (fixed), {new_name}" in st["text"], st["text"]
        board.command("cyphal restart")
        st = await until_state(board, "online", other)
        assert st["name"] == new_name and st["how"] == "fixed"
        beats = await heartbeats(node, other, 2.5)
        assert beats, f"no heartbeat from {other}"
        assert await heartbeats(node, dut_node_id, 2.0) == [], f"still heartbeating as {dut_node_id}"
        info = await call(node, GetInfo, other, GetInfo.Request())
        assert bytes(info.name).decode() == new_name
        shown = console_registers(board.command("cyphal register"))
        assert shown["uavcan.node.id"] == str(other) and shown["r2.node.name"] == new_name, shown
        print(f"\nnode {other}, {new_name}: {len(beats)} heartbeats; GetInfo {bytes(info.name).decode()}")
    finally:
        # From the console, which works whatever node-ID the node came back on, or none
        board.command(f"cyphal id {dut_node_id}")
        board.command(f"cyphal register r2.node.name {default}")
        board.command("cyphal restart")
        await until_state(board, "online", dut_node_id)
    st = board.cyphal()
    assert st["name"] == default and "from the next start" not in st["text"]
