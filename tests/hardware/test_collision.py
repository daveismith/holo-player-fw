"""Test 3.11 (and D9's listen, and a multi-frame response cut short): another node with the board's
node-ID. The board must stop sending at once, heartbeat included, with one warning, and stay stopped
until its node-ID changes or it is told to start again.

The other node is played by the harness: a Cyphal heartbeat from the board's node-ID, exactly as a
node with that ID would send it, put on the bus through the harness's own adapter at a moment the
test knows. The board's heartbeats are watched by the harness's pycyphal node meanwhile."""
from __future__ import annotations

import asyncio
import math
import re
import struct

import can
import pycyphal.dsdl
import pytest
import uavcan.node.GetInfo_1_0 as GetInfo

from .conftest import heartbeats

HEARTBEAT = 7509
GET_INFO = 430
WARNING = "cyphal: node-ID {} is in use by another node; Cyphal stopped"


def heartbeat_frame(source: int, tid: int) -> tuple[int, bytes]:
    can_id = (4 << 26) | (3 << 21) | (HEARTBEAT << 8) | source     # nominal priority, a message
    data = struct.pack("<IBBB", 1, 0, 0, 0) + bytes([0xE0 | (tid & 31)])   # one frame: start, end, toggle
    return can_id, data


def get_info_request_frame(source: int, destination: int, tid: int) -> tuple[int, bytes]:
    """A GetInfo request, empty, so one frame. Priority 1 (fast), so that its CAN ID is below the
    heartbeat's and the adapter puts it on the bus first when it holds both"""
    can_id = (1 << 26) | (1 << 25) | (1 << 24) | (GET_INFO << 14) | (destination << 7) | source
    return can_id, bytes([0xE0 | (tid & 31)])


def is_get_info_response(can_id: int, source: int, destination: int) -> bool:
    return (can_id >> 24) & 3 == 2 and (can_id >> 14) & 0x1FF == GET_INFO and (can_id >> 7) & 0x7F == destination \
        and can_id & 0x7F == source


def warnings(log: str, node_id: int) -> int:
    return len(re.findall(re.escape(WARNING.format(node_id)), log))


@pytest.mark.restarts
async def test_node_id_taken(node, net, board, dut_node_id):
    other = dut_node_id + 1
    loop = asyncio.get_running_loop()
    try:
        assert len(await heartbeats(node, dut_node_id, 2.5)) >= 2, "the board isn't sending"

        # Running: one frame from its node-ID stops it within a period, for good
        mark = board.mark()
        watch = asyncio.create_task(heartbeats(node, dut_node_id, 4.0))
        await asyncio.sleep(0.2)
        t0 = loop.time()
        await net.send_raw(*heartbeat_frame(dut_node_id, 0))
        late = [round(t - t0, 3) for t, _, _ in await watch if t > t0]
        st = board.cyphal()
        log = board.since(mark)
        print(f"\nboard heartbeats after the other node's: {late}\n{st['text']}")
        assert all(t < 1.0 for t in late), f"the board sent after {late} s"
        assert st["state"] == "in-use" and st["node_id"] == dut_node_id, st["text"]
        assert warnings(log + st["text"], dut_node_id) == 1, log
        mark = board.mark()
        assert await heartbeats(node, dut_node_id, 5.0) == [], "it came back by itself"
        st = board.cyphal()
        assert st["state"] == "in-use"
        assert warnings(board.since(mark) + st["text"], dut_node_id) == 0, "warned again"

        # `cyphal restart`, the other node gone: back
        board.command("cyphal restart")
        assert await heartbeats(node, dut_node_id, 3.5), "not back after `cyphal restart`"
        assert board.cyphal()["state"] == "online"

        # Stopped again; another node-ID brings it back too
        await net.send_raw(*heartbeat_frame(dut_node_id, 1))
        board.wait_state("in-use", dut_node_id, timeout=3)
        board.command(f"cyphal id {other}")
        board.command("cyphal restart")
        assert await heartbeats(node, other, 3.5), f"not sending as node {other}"

        # Before going online: the other node heard in the 2 s listen, and it never starts
        board.command(f"cyphal id {dut_node_id}")
        mark = board.mark()
        out = board.command("cyphal restart")
        watch = asyncio.create_task(heartbeats(node, dut_node_id, 10 * 0.3 + 1.5))
        await asyncio.sleep(0)      # its subscriber made before the first frame
        for tid in range(10):
            await net.send_raw(*heartbeat_frame(dut_node_id, tid))
            await asyncio.sleep(0.3)
        heard = await watch
        st = board.cyphal()
        log = board.since(mark) + out
        print(f"listen: board heartbeats {len(heard)}\n{st['text']}")
        assert heard == [], "it sent during or after the listen"
        assert st["state"] == "in-use" and st["node_id"] == dut_node_id, st["text"]
        assert warnings(log + st["text"], dut_node_id) == 1, log
    finally:
        board.command(f"cyphal id {dut_node_id}")
        board.command("cyphal restart")
    board.wait_state("online", dut_node_id)


# How long after the adapter takes the other node's frame it is surely on the bus and done: a
# board frame in progress then (an 8-byte frame is about 135 us at 1 Mbit/s), then the frame
# itself, which wins the next gap since the board hands its frames over one at a time
COLLISION_DONE_US = 400


@pytest.mark.restarts
async def test_node_id_taken_mid_response(node, net, get_info, board, dut_node_id):
    """D9 while a multi-frame transfer goes out. A GetInfo request, and at once a frame from the
    board's node-ID: once that frame has arrived, the board sends at most the one frame it had
    already handed to its controller, and not the rest of the response.

    The order on the bus is read from the adapter's timestamps: when it took the other node's frame
    (its echo), and when each of the board's frames arrived. A trial where the board sent nothing
    (it read the other node's frame before it began to answer) or finished the response before
    that frame was taken shows nothing, and is tried again."""
    result = await get_info.call(GetInfo.Request())
    assert result is not None, "GetInfo not answered"
    frames = math.ceil((sum(len(b) for b in pycyphal.dsdl.serialize(result[0])) + 2) / 7)
    tid = (result[1].transfer_id + 1) % 32      # the next the board's session expects from us
    local = net.local_node_id
    request = get_info_request_frame(local, dut_node_id, tid)
    other = heartbeat_frame(dut_node_id, 7)

    seen: list = []
    armed = [False]
    node.presentation.transport.begin_capture(lambda c: seen.append(c) if armed[0] else None)
    bus = net.media._bus      # python-can's: the two frames back to back, not through pycyphal's queue

    def send_both() -> None:
        for can_id, data in (request, other):
            bus.send(can.Message(arbitration_id=can_id, is_extended_id=True, data=data), timeout=0.1)

    trials = []
    try:
        for attempt in range(8):
            mark = board.mark()
            seen.clear()
            armed[0] = True
            await asyncio.get_running_loop().run_in_executor(None, send_both)
            await asyncio.sleep(0.3)
            armed[0] = False
            st = board.cyphal()
            log = board.since(mark)
            echo = [c.timestamp.monotonic_ns // 1000 for c in seen if c.own and c.frame.identifier == other[0]
                    and bytes(c.frame.data) == other[1]]
            assert echo, "the adapter's echo of the other node's frame wasn't seen"
            taken = echo[0]
            answer = [c.timestamp.monotonic_ns // 1000 for c in seen if not c.own and
                      is_get_info_response(c.frame.identifier, dut_node_id, local)]
            later = [t - taken for t in answer if t > taken + COLLISION_DONE_US]
            trial = {"response frames": len(answer), "of": frames,
                     "after the other node's frame (us)": later,
                     "since it was taken (us)": [t - taken for t in answer]}
            trials.append(trial)
            print(f"\ntrial {attempt + 1}: {trial}; state {st['state']}, stack {st['stack_unused']} bytes never used")
            assert st["state"] == "in-use" and st["node_id"] == dut_node_id, st["text"]
            assert warnings(log + st["text"], dut_node_id) == 1, log
            assert len(later) <= 1, f"the board sent {len(later)} frames after the other node's frame had arrived"
            if answer and taken < answer[-1]:
                break
            board.command("cyphal restart")
            board.wait_state("online", dut_node_id)
        else:
            pytest.skip(f"no trial in which the board was answering when the other node's frame came: {trials}")
    finally:
        armed[0] = False
        board.command("cyphal restart")
        board.wait_state("online", dut_node_id)
