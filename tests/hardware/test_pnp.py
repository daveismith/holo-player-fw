"""Tests 4.2 and 4.10, and plug-and-play's requests (manual/use/cyphal.md, "The node-ID"): with no fixed
node-ID the board asks for one with uavcan.pnp.NodeIDAllocationData.1.0, anonymously, and takes the one
an allocator gives its pseudo-ID once it has listened for a second since its start; it heartbeats and
answers GetInfo on it, and stops (D9) if another node turns out to have it.

The allocator is pycyphal's own (CentralizedAllocator, its table in memory) on the harness's node: it
gives the highest free node-ID, 125. The board is switched to plug-and-play once the adapter is open, for
all of this module's tests, and back to its fixed node-ID once they have run (conftest.py: the adapter
opens and closes only in a fixed node-ID's listen)."""
from __future__ import annotations

import asyncio
import statistics

import pycyphal.application.plug_and_play
import pytest
import uavcan.node.GetInfo_1_0 as GetInfo
import uavcan.node.Heartbeat_1_0 as Heartbeat

from .conftest import ensure_node, gaps, heartbeat_frame, heartbeats, open_checked, until_state, warnings

ALLOCATION = 8166
ALLOCATED = 125     # what CentralizedAllocator gives: the highest node-ID not set aside for tools
# Trequest. Intervals are measured on the adapter's clock; SLACK is the node task's own lateness
T_MIN, T_MAX, SLACK = 0.150, 1.000, 0.015
LISTEN = 1.0        # how long the board listens from its start before it takes up an allocated node-ID


def requests_from_board(frames, pseudo_id: int) -> list:
    """The board's allocation requests among frames: anonymous, on 8166, its pseudo-ID, single frames"""
    out = []
    for f in frames:
        if not f.own and f.anonymous and f.subject == ALLOCATION:
            assert len(f.data) == 8, f"a request is 7 bytes and the tail byte: {f.data.hex()}"
            assert f.data[:6] == pseudo_id.to_bytes(6, "little") and f.data[6] == 0, f.data.hex()
            assert f.tail & 0xE0 == 0xE0, f"not a single frame: tail {f.tail:02x}"
            out.append(f)
    return out


def other_allocatee_frame(pseudo_id: int, tid: int) -> tuple[int, bytes]:
    """Another allocatee's request: anonymous, its own pseudo-ID and discriminator"""
    can_id = (4 << 26) | (1 << 24) | (3 << 21) | (ALLOCATION << 8) | 0x55
    return can_id, pseudo_id.to_bytes(6, "little") + bytes([0, 0xE0 | (tid & 31)])


@pytest.fixture(scope="module")
async def pnp(net, board, dut_node_id):
    """Plug-and-play set, with the adapter open, for the module's tests (each restarts the node itself);
    the node back on its fixed node-ID, online, once they have run, whatever they did"""
    if net.node is None:        # as the `node` fixture would: it can't reopen it once this has run
        ensure_node(board, dut_node_id, restart=False)
        await open_checked(net, board, dut_node_id)
    board.plug_and_play()
    yield
    ensure_node(board, dut_node_id)


@pytest.fixture
async def allocator(node):
    """Starts pycyphal's allocator on the harness's node when first called; later calls leave it running"""
    made = []

    def start() -> None:
        if not made:
            made.append(pycyphal.application.plug_and_play.CentralizedAllocator(node))

    yield start
    # It has no close of its own short of the node's: its ports, so that a later test starts clean
    for a in made:
        for port in (a._sub1, a._sub2, a._pub1, a._pub2):
            port.close()


async def first_heartbeat(node, source: int, t0: float, timeout: float) -> float | None:
    """When the first heartbeat from `source` since t0 (asyncio's clock) arrived, or None. By its uptime:
    one sent before t0 may be handed over later, held up by a console command that blocked the loop"""
    sub = node.make_subscriber(Heartbeat)
    loop = asyncio.get_running_loop()
    end = loop.time() + timeout
    try:
        while (left := end - loop.time()) > 0:
            r = await sub.receive_for(min(left, 0.2))
            if r is not None and r[1].source_node_id == source and r[0].uptime <= loop.time() - t0 + 1:
                return loop.time()
    finally:
        sub.close()
    return None


@pytest.mark.restarts
async def test_requests_with_no_allocator(pnp, node, net, board):
    """No allocator: requests at random intervals of 150 ms to 1 s, nothing else; another allocatee's
    request restarts the timer (rule C), so a stream of them faster than 150 ms holds the board's back.
    An allocation message with nothing in it but its tail byte is one more such message, not a heap
    run out (libcanard asks for 0 bytes for it)."""
    pseudo_id = board.cyphal()["pseudo_id"]
    cap = net.capture()
    loop = asyncio.get_running_loop()
    t0, t1 = board.restart_asking()     # the node starts between these two: the console's round trip
    await asyncio.sleep(20)
    t_spam = loop.time()
    empty_id, _ = other_allocatee_frame(0x123456789ABC, 0)
    await net.send_raw(empty_id, bytes([0xE0]))     # anonymous, empty: start, end, toggle, transfer-ID 0
    for tid in range(40):                       # 3.2 s of another allocatee, every 80 ms
        await net.send_raw(*other_allocatee_frame(0x123456789ABC, tid))
        await asyncio.sleep(0.08)
    t_spam_end = loop.time()
    await asyncio.sleep(3)
    st = board.cyphal()
    frames = cap.stop()
    reqs = requests_from_board(frames, pseudo_id)
    board_frames = [f for f in frames if not f.own and f.t > t1]
    quiet = [r for r in reqs if t0 < r.t < t_spam]
    between = gaps([(r.t_adapter,) for r in quiet])
    held = [round(r.t - t_spam, 3) for r in reqs if t_spam + 0.02 < r.t <= t_spam_end]
    after = [r.t - t_spam_end for r in reqs if r.t > t_spam_end]
    tids = [r.tail & 31 for r in quiet]
    print(f"\nfirst request {quiet[0].t - t1:.3f} to {quiet[0].t - t0:.3f} s after the start (the console took "
          f"{t1 - t0:.3f} s); {len(quiet)} requests in "
          f"{t_spam - t0:.0f} s; intervals {min(between):.3f} to {max(between):.3f} s, mean "
          f"{statistics.mean(between):.3f}; CAN ID {quiet[0].id:08x}; during the other allocatee's requests: "
          f"{held}; first after them {f'{after[0]:.3f} s' if after else 'none'}\n{st['text']}")
    other = [(round(f.t - t1, 3), f"{f.id:08x}") for f in board_frames if not (f.anonymous and f.subject == ALLOCATION)]
    assert other == [], f"the board sent something else: {other}"
    assert st["state"] == "waiting"
    assert quiet[0].t - t0 >= T_MIN and quiet[0].t - t1 <= T_MAX + SLACK   # rule A: a Trequest after the start
    assert all(T_MIN - SLACK <= g <= T_MAX + SLACK for g in between), between
    # 0.575 s for uniform 150-1000 ms (sd 0.245 s); over ~34 intervals these bounds are 4 standard errors out
    assert 0.40 <= statistics.mean(between) <= 0.75, statistics.mean(between)
    assert st["transfers_received"] == 41 and not st["heap_ran_out"], "the empty allocation message, and 40 others"
    assert len({r.id for r in reqs}) == 1, "one CAN ID: the discriminator comes from the payload"
    assert all((b - a) % 32 == 1 for a, b in zip(tids, tids[1:])), f"transfer-IDs: {tids}"
    assert held == [], f"requests while another allocatee's came every 80 ms: {held}"
    assert after and after[0] <= T_MAX + 0.05, "no request after the other allocatee went quiet"


@pytest.mark.restarts
async def test_allocation(pnp, node, net, board, allocator):
    """4.2, then 3.2 and 3.3 on the allocated node-ID, then three node restarts: the same node-ID each
    time, and nothing saved"""
    pseudo_id = board.cyphal()["pseudo_id"]
    allocator()
    times = []
    for attempt in range(4):
        cap = net.capture()
        mark = board.mark()
        t0, _ = board.restart_asking()
        t = await first_heartbeat(node, ALLOCATED, t0, 5.0)
        frames = cap.stop()
        assert t is not None, f"no heartbeat from {ALLOCATED} within 5 s:\n{board.cyphal()['text']}"
        reqs = requests_from_board(frames, pseudo_id)
        responses = [f for f in frames if f.own and f.subject == ALLOCATION]
        times.append((round(t - t0, 3), len(reqs), len(responses)))
        st = await until_state(board, "online", ALLOCATED, timeout=3)
        log = board.since(mark)
        assert st["how"] == "allocated", st["text"]
        assert f"cyphal: node {ALLOCATED} (allocated by node {net.local_node_id}): online" in log, log
        # A late answer to an earlier request would do too (rule D asks for no request of its own), so
        # each round lets the allocator's answers to the last one go by first
        assert len(reqs) >= 1, [(round(f.t - t0, 3), f.own, f"{f.id:08x}", f.data.hex()) for f in frames]
        await asyncio.sleep(2)
    print(f"\n(seconds from `cyphal restart` to the first heartbeat, requests, allocator's frames): {times}")
    # Never sooner than the listen: the node starts after the command is sent
    assert all(LISTEN <= t < 3.0 for t, _, _ in times), times
    assert "from the next start" not in board.cyphal()["text"], "plug-and-play is the setting"

    # 3.2 on the allocated node-ID
    beats = await heartbeats(node, ALLOCATED, 30.0)
    uptimes = [b[2].uptime for b in beats]
    between = gaps(beats)
    print(f"{len(beats)} heartbeats from {ALLOCATED}; gaps {min(between):.3f} to {max(between):.3f} s")
    assert 28 <= len(beats) <= 32
    assert all(b - a == 1 for a, b in zip(uptimes, uptimes[1:])), uptimes
    assert max(between) <= 1.5

    # 3.3 on the allocated node-ID
    client = node.make_client(GetInfo, ALLOCATED)
    client.response_timeout = 1.0
    try:
        result = await client.call(GetInfo.Request())
    finally:
        client.close()
    assert result is not None, "GetInfo not answered on the allocated node-ID"
    st = board.cyphal()
    uid = bytes(result[0].unique_id)
    print(f"GetInfo: {bytes(result[0].name).decode()}, unique id {uid.hex()}\n{st['text']}")
    assert bytes(result[0].name).decode() == st["name"]
    assert uid == st["unique_id"] and uid[:8] == b"holoplay"
    assert st["dropped"] == 0 and st["not_read"] == 0 and not st["heap_ran_out"]


@pytest.mark.restarts
async def test_allocated_node_id_in_use(pnp, node, net, board, allocator):
    """4.10: the allocator gives a node-ID that another node is using. (a) Heard before the allocation:
    the board stops on it without ever sending as that node-ID. (b) Heard only from about when the
    allocation comes, inside the second the board listens from its start before it takes a node-ID up:
    the same. (c) Turning up once the board runs: it stops within a heartbeat period, as with a fixed
    node-ID (3.11)."""
    loop = asyncio.get_running_loop()

    async def other_node(stop: asyncio.Event, first: float, period: float, sent: list[float]) -> None:
        await asyncio.sleep(first)
        tid = 0
        while not stop.is_set():
            await net.send_raw(*heartbeat_frame(ALLOCATED, tid))
            sent.append(loop.time())
            tid += 1
            await asyncio.sleep(period)

    async def stopped_by_other_node(t1: float, other_after: float, period: float,
                                    allocator_after: float) -> tuple[list, str, list[float]]:
        """The other node heartbeating every `period` s from `other_after` s from now; the allocator
        started after allocator_after s (if it isn't yet); the board stopped on ALLOCATED, and 1 s more.
        The frames captured since t1 (the console held the loop until then, so earlier ones reach the
        capture late), the log, and when the other node's frames went. The other node stops however
        this ends"""
        cap = net.capture()
        mark = board.mark()
        stop, sent = asyncio.Event(), []
        task = asyncio.create_task(other_node(stop, other_after, period, sent))
        try:
            await asyncio.sleep(allocator_after)
            allocator()
            st = await until_state(board, "in-use", ALLOCATED, timeout=5)
            await asyncio.sleep(1)
        finally:
            stop.set()
            await task
        return [f for f in cap.stop() if f.t > t1], board.since(mark) + st["text"], sent

    def as_it(frames) -> list:
        return [f for f in frames if not f.own and not f.anonymous and f.source == ALLOCATED]

    # (a) The board asking first, with no allocator (the last test left it online on ALLOCATED, which
    # the other node would stop), then the other node, then the allocator 1.2 s later
    _, t1 = board.restart_asking()
    frames, log, _ = await stopped_by_other_node(t1, 0, 0.5, 1.2)
    st = board.cyphal()
    print(f"\n(a) {len(requests_from_board(frames, st['pseudo_id']))} requests; frames sent as {ALLOCATED}: "
          f"{len(as_it(frames))}\n{st['text']}")
    assert as_it(frames) == [], "the board sent as a node-ID another node was using"
    assert warnings(log, ALLOCATED) == 1, log
    assert st["state"] == "in-use" and st["node_id"] == ALLOCATED, st["text"]

    # (b) The allocator already running, and the other node's first frame 50 ms after the restart command
    # returns. The console holds the event loop, and the allocator with it, so the board's first request
    # (0.15 to 1 s after its start) is answered no sooner than the command returns: the allocation comes
    # about then, or later, and the other node is first heard about then too, inside the board's first
    # second (the start is after t0). Taken up at once, as before the listen, the allocation coming first
    # would have had the board send as ALLOCATED until the other node's frame
    for _ in range(3):
        t0, t1 = board.restart_asking()
        if t1 - t0 < 0.9:       # else the start may be too close to t1 for the case: again
            break
    frames, log, sent = await stopped_by_other_node(t1, 0.05, 0.25, 0)
    st = board.cyphal()
    allocations = [f.t for f in frames if f.own and f.subject == ALLOCATION and not f.anonymous]
    print(f"(b) the console took {t1 - t0:.3f} s; after it returned: the allocator's first frame "
          f"{allocations[0] - t1 if allocations else float('nan'):+.3f} s, the other node's first "
          f"{sent[0] - t1 if sent else float('nan'):+.3f} s; frames sent as {ALLOCATED}: {len(as_it(frames))}\n"
          f"{st['text']}")
    assert as_it(frames) == [], "the board sent as a node-ID another node was using"
    assert warnings(log, ALLOCATED) == 1, log
    assert st["state"] == "in-use" and st["node_id"] == ALLOCATED, st["text"]

    # (c) the other node gone for longer than the heartbeat's offline timeout
    await asyncio.sleep(3.5)
    mark = board.mark()
    board.restart_asking()
    await until_state(board, "online", ALLOCATED, timeout=5)
    assert await heartbeats(node, ALLOCATED, 2.5), "not heartbeating on the allocated node-ID"
    watch = asyncio.create_task(heartbeats(node, ALLOCATED, 4.0))     # ends by itself
    await asyncio.sleep(0.3)
    t0 = loop.time()
    await net.send_raw(*heartbeat_frame(ALLOCATED, 0))
    late = [round(t - t0, 3) for t, _, _ in await watch if t > t0]
    st = board.cyphal()
    log = board.since(mark) + st["text"]
    print(f"(c) board heartbeats after the other node's: {late}\n{st['text']}")
    assert all(t < 1.0 for t in late), late
    assert st["state"] == "in-use" and st["node_id"] == ALLOCATED, st["text"]
    assert warnings(log, ALLOCATED) == 1, log
    assert await heartbeats(node, ALLOCATED, 3.0) == [], "it came back by itself"
