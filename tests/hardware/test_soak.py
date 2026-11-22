"""Test 5.3 (b), the soak with pycyphal: for --soak-minutes (none by default: the test is skipped; the phase run is 15,
the full one 60), a clip looping on the screen, the web app's calls polled over HTTP (--board-host), a scene applied over
Cyphal every 30 s (the ring and the holo: the screen is the clip's), a burst of setpoints at 50 a second every minute,
the lists every 10 s (ListScenes, uavcan.file.GetInfo, MediaInfo of the clip in play) with a light and the ring, and the
whole root listed with uavcan.file.List once a minute. Passes when the heartbeat is never late, the controller never
goes bus-off, nothing is dropped (no transfer, no setpoint, no response), every call is answered, Status keeps its rate,
the clip keeps 30 frames a second with none late, the board doesn't restart (its uptime runs on), and the internal heap
is flat (at rest afterwards where it was before, but for the board's record of happenings filling up; during the run,
no fall once that is full). Prints the memory by minute.

    make can-test ARGS="--board-host <ip> --soak-minutes 15 -k test_soak -s"

Part (a) of the soak, with cyphal_service holding the adapter, can't be a test here (one program on the adapter): its
script is named in the phase's report."""
from __future__ import annotations

import asyncio
import threading

import pytest
import r2.holo.Axes_0_1 as Axes

from .conftest import Beats
from .holo_ports import (OK, Listen, light, load_checked, ring, ring_made, scene, setpoint_burst, setpoints,
                         under_load, video)
from .test_holo_lists import file_info, file_list, list_scenes, media_info

SCENE_NAME = "cyphal-soak"
# The board's record of happenings full: 32 of them, a scene's or a clip's JSON at 200 bytes at most each
EVENTS_RING_BYTES = 32 * 200
POLLED = ["/screen", "/leds", "/holo", "/scenes", "/events", "/servos"]


@pytest.fixture
def soak_scene(http, request):
    """A scene for the soak, made over HTTP in a free slot: the ring and a wag, back to what was before after 5 s.
    Deleted after. The soak is skipped without --soak-minutes and --board-host"""
    if request.config.getoption("--soak-minutes") <= 0:
        pytest.skip("the soak runs only when asked: --soak-minutes")
    if http is None:
        pytest.skip("the soak polls the web app's calls: --board-host")
    status, reply = http.call("GET", "/scenes")
    assert status == 200, reply
    assert not any(s["name"] == SCENE_NAME for s in reply["scenes"]), "the soak's scene was left from an earlier run"
    slot = next(n for n in range(239, 0, -1) if n not in {s.get("slot") for s in reply["scenes"]})
    body = {"leds": {"mode": "pulse", "colour": "#0040ff", "period_ms": 800}, "holo": {"motion": "wag", "count": 2},
            "then": "restore", "duration_s": 5, "slot": slot}
    status, reply = http.call("PUT", "/scenes/scene", query={"name": SCENE_NAME}, body=body)
    assert status == 201, reply
    try:
        yield slot
    finally:
        http.call("DELETE", "/scenes/scene", query={"name": SCENE_NAME})


class Poller(threading.Thread):
    """The web app's calls, every 2 s, on a thread of its own: how many, and any that failed"""

    def __init__(self, http):
        super().__init__(daemon=True)
        self.http, self.calls, self.failed, self.stop = http, 0, [], threading.Event()

    def run(self):
        while not self.stop.wait(2.0):
            for path in POLLED:
                try:
                    status, reply = self.http.call("GET", path)
                    if status != 200:
                        self.failed.append((path, status))
                except Exception as e:   # noqa: BLE001 -- counted, not raised on the thread
                    self.failed.append((path, repr(e)))
                self.calls += 1


async def root_walk(node, server: int) -> list[str]:
    """The root's entries by uavcan.file.List, index 0 until the empty name"""
    names = []
    for index in range(1000):
        name = await file_list(node, server, "/", index)
        if not name:
            return names
        names.append(name)
    raise AssertionError("the root's listing never ended")


@pytest.mark.slow
async def test_soak(node, board, dut_node_id, http, soak_scene, request):
    minutes = request.config.getoption("--soak-minutes")
    clip = request.config.getoption("--clip")
    listen = Listen(node, dut_node_id)
    beats = Beats(node, dut_node_id)
    pub = setpoints(node)
    poller = Poller(http)
    loop = asyncio.get_running_loop()
    samples: list[tuple[float, dict]] = []
    calls = refused = sent = scenes_applied = walks = 0
    lost: list[str] = []
    with under_load(board):
        try:
            # Everything the soak does, once, before the heap at rest is read: what is made the first time and kept
            # (the ring's pattern task, the web server's and the scenes' first allocations, the events kept) isn't a
            # leak
            await ring_made(board)
            assert (await scene(node, dut_node_id, slot=soak_scene)).code == OK
            await list_scenes(node, dut_node_id, 0)
            await media_info(node, dut_node_id, f"/{clip}")
            root = await root_walk(node, dut_node_id)
            for path in POLLED:
                http.call("GET", path)
            for k in range(25):
                await pub.publish(Axes(x=100 * k, y=-100 * k))
                await asyncio.sleep(0.02)
            await asyncio.sleep(6.0)            # the scene back to what was before
            board.command("holo off")
            board.command("leds off")
            await asyncio.sleep(0.5)
            rest_before = board.cyphal()["internal_free"]
            out = board.command(f"video play {clip} loop")
            assert "Unrecognized" not in out and "error" not in out.lower(), out
            board.command("leds rainbow loop")
            await asyncio.sleep(2.0)
            start = board.cyphal()
            samples.append((0.0, start))
            first_beat = len(beats.times)
            poller.start()
            t0 = loop.time()
            end = t0 + minutes * 60
            next_scene, next_burst, next_lists, next_walk, next_sample = t0 + 5, t0 + 20, t0 + 2, t0 + 40, t0 + 60
            while loop.time() < end:
                now = loop.time()
                if now >= next_burst:
                    sent += await setpoint_burst(pub, 6000, 4000)       # 3 s at 50 a second
                    next_burst += 60
                elif now >= next_scene:
                    r = await scene(node, dut_node_id, slot=soak_scene)
                    calls += 1
                    scenes_applied += 1
                    refused += r.code != OK
                    next_scene += 30
                elif now >= next_lists:
                    for what, coro in (("ListScenes", list_scenes(node, dut_node_id, 0)),
                                       ("GetInfo", file_info(node, dut_node_id, f"/{clip}")),
                                       ("MediaInfo", media_info(node, dut_node_id, f"/{clip}")),
                                       ("Light", light(node, dut_node_id, "pulse", period_ms=1200, time_limit_ms=1500)),
                                       ("Ring", ring(node, dut_node_id, mode="rainbow", loop=True))):
                        try:
                            await coro
                            calls += 1
                        except AssertionError:
                            lost.append(what)
                    next_lists += 10
                elif now >= next_walk:
                    try:
                        names = await root_walk(node, dut_node_id)
                        calls += len(names) + 1
                        walks += 1
                        if names != root:
                            lost.append(f"List: the root read {len(names)} entries, not {len(root)}")
                    except AssertionError:
                        lost.append("List")
                    next_walk += 60
                elif now >= next_sample:
                    samples.append((now - t0, board.cyphal()))
                    next_sample += 60
                else:
                    await asyncio.sleep(0.1)
            await asyncio.sleep(6.0)        # the last scene back to what was before
            after = board.cyphal()
            samples.append((loop.time() - t0, after))
            shown = video(board)
        finally:
            poller.stop.set()
            pub.close()
            listen.close()
            beats.close()
    poller.join(5)
    await asyncio.sleep(1.0)
    rest_after = board.cyphal()["internal_free"]
    print(f"\n{minutes:g} min: {calls} calls ({refused} refused, {len(lost)} unanswered or wrong: {lost}), "
          f"{scenes_applied} scenes, {walks} walks of the root's {len(root)} entries; {poller.calls} HTTP calls "
          f"({len(poller.failed)} failed: {poller.failed[:5]})")
    print("minute  internal free  lowest  uptime")
    for at, s in samples:
        print(f"{at / 60:6.1f}  {s['internal_free']:13}  {s['internal_lowest']:6}  {s['uptime']}")
    print(f"at rest: {rest_before} before, {rest_after} after\nworker stacks never used: the node task's "
          f"{after['stack_unused']}, the worker's {after['worker_unused']}, the second worker's {after['slow_unused']}\n"
          f"start:\n{start['text']}\nend:\n{after['text']}")
    load_checked(beats, first_beat, listen, start, after, shown, sent)
    assert refused == 0 and not lost
    assert not poller.failed
    assert after["uptime"] >= start["uptime"] + minutes * 60 - 5, "the board restarted"
    # Flat. The board's record of happenings (the kit's events, the HTTP API's /events) keeps its last 32 as JSON
    # strings in internal RAM, so it grows until it is full, about 3.5 KB of a soak's scenes and clips (2026-11-21: the
    # free heap fell by minute for the first 4 minutes, then held to the byte): at rest the heap may be that much lower
    # after than before, and no more. Once the record is full (the last two thirds of the run) the free heap, taken at
    # its best each third (a clip's frame being decoded holds 8.9 KB more for a moment, which a sample may catch),
    # doesn't fall
    assert rest_after >= rest_before - 1024 - EVENTS_RING_BYTES, \
        f"at rest the internal heap was {rest_before} before and {rest_after} after"
    free = [s["internal_free"] for _, s in samples]
    if len(free) >= 6:
        third = len(free) // 3
        assert max(free[2 * third:]) >= max(free[third:2 * third]) - 1024, f"the free heap fell during the run: {free}"
