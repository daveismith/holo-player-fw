"""Tests 4.2 to 4.6: the board's own features on Cyphal (manual/reference/cyphal.md): r2.holo.Scene, by slot and by
name, and its end; r2.holo.Screen, a clip, a still, a colour, the crosshair, nothing, the backlight, and its
refusals; r2.holo.Ring, every mode and the brightness; r2.holo.Event against the HTTP API's event stream; a
service called while GetInfo is asked at full rate; and setpoints at 50 a second while the screen shows stills and
clips. Each is read back through Status, Event, the console and, with --board-host, the HTTP API, whose answers and
refusals must be the same.

The scenes are the tests' own, made over HTTP (so the scene tests want --board-host) under names that start with
`cyphal-test` and in slots no saved scene has, and deleted after; the user's scenes are never applied or changed.
The ring runs at the board's own brightness or below it, and the screen's backlight, the ring and the holo are put
back as they were."""
from __future__ import annotations

import asyncio
import json
import threading

import api_smoke        # tools/, on the path conftest.py gives
import pytest
import r2.holo.Axes_0_1 as Axes
import uavcan.node.GetInfo_1_0 as GetInfo

from .holo_ports import (BAD_REQUEST, NEVER, NOT_FOUND, NOT_PLAYABLE, OK, RING, Listen, backlight_now,
                         event_fields, event_kind, json_fields, light, message, ring, ring_brightness, scene, screen,
                         screen_line, setpoints, text)

PREFIX = "cyphal-test"
CLIP = "/testsrc240.mjpeg.mov"      # 300 frames at 30 fps, 240x240
GIF = "/testsrc120.gif"             # an animation: 60 frames at 20 fps, 3 s
PNG = "/rgb240.png"
PNG_WORST = "/worstcase.png"
JPEG = "/j420_240.jpg"
MISSING = "/cyphal-test/none.png"
SHOWING = ["nothing", "colour", "calibration", "image", "clip"]


@pytest.fixture
async def listen(node, board, dut_node_id):
    """Status, Axes and Event from the board; the ring off, the screen clear and the holo at rest before; and after,
    any scene ended, the screen cleared at the backlight it had, the ring off at the brightness it had, the holo
    released"""
    brightness = ring_brightness(board)
    backlight = backlight_now(board)
    board.command("scene end")
    board.command("holo stop")
    board.command("leds off")
    board.command("screen clear")
    l = Listen(node, dut_node_id)
    l.brightness = brightness
    l.backlight = backlight
    yield l
    l.close()
    board.command("scene end")
    board.command("screen clear")
    board.command(f"lcd bl {backlight}")
    board.command("holo off")
    board.command("leds off")
    board.command(f"leds bright {brightness}")


@pytest.fixture
def test_scenes(http):
    """The tests' scenes, made over HTTP in slots no saved scene has: name -> (slot or 0, its JSON). Deleted after"""
    if http is None:
        pytest.skip("the scenes are made over HTTP: --board-host")
    status, reply = http.call("GET", "/scenes")
    assert status == 200, reply
    taken = {s.get("slot") for s in reply["scenes"]}
    assert not any(s["name"].startswith(PREFIX) for s in reply["scenes"]), "a test scene was left from an earlier run"
    free = [n for n in range(240, 256) if n not in taken]
    assert len(free) >= 5, taken
    slots = {"time": free[0], "long": free[1], "clip": free[2], "quick": free[3]}
    scenes = {
        # Ends by its time, and puts back what was before
        f"{PREFIX}-time": {"screen": {"colour": "#102030"}, "leds": {"mode": "solid", "colour": "#203040"},
                           "then": "restore", "duration_s": 1.5, "slot": slots["time"]},
        # Runs a minute unless ended or replaced, then all off
        f"{PREFIX}-long": {"screen": {"colour": "#300000"}, "leds": {"mode": "pulse", "colour": "#003000"},
                           "then": "off", "duration_s": 60, "slot": slots["long"]},
        # Ends when its clip (an animation, 3 s) has played
        f"{PREFIX}-clip": {"screen": {"path": GIF}, "then": "restore", "slot": slots["clip"]},
        # A short one with a still (decoded as the scene is applied), for the event comparison
        f"{PREFIX}-quick": {"screen": {"path": PNG}, "then": "restore", "duration_s": 0.6, "slot": slots["quick"]},
        # Applied and done
        f"{PREFIX}-stay": {"screen": {"calibration": True}},
        # Its file isn't there
        f"{PREFIX}-missing": {"screen": {"path": MISSING}},
    }
    made = []
    try:
        for name, body in scenes.items():
            status, reply = http.call("PUT", "/scenes/scene", query={"name": name}, body=body)
            assert status == 201, (name, status, reply)
            made.append(name)
        yield {name: (body.get("slot", 0), body) for name, body in scenes.items()}
    finally:
        for name in made:
            http.call("DELETE", "/scenes/scene", query={"name": name})
        status, reply = http.call("GET", "/scenes")
        assert not any(s["name"].startswith(PREFIX) for s in reply["scenes"]), reply


async def until_scene(listen: Listen, after: int, timeout: float, where):
    return await listen.next_status(after=after, timeout=timeout, where=lambda s: where(s.scene))


def scene_of(s) -> tuple:
    """Status's scene part, as (running, name, slot, then)"""
    sc = s.scene
    return bool(sc.running), text(sc.name), sc.slot, sc.then


async def test_scene_apply_and_end(node, board, dut_node_id, listen, test_scenes, http):
    """4.2: a scene applied by slot and by name, ended by its time, by Scene END, by another scene and by its clip:
    Status shows it running (the same as GET /scenes's `active`), Event carries scene_started and scene_ended with
    the right `by`, and what follows happens; END with nothing running, a slot and a name that don't exist, a bad
    request, and a scene whose file is missing are refused as the HTTP API refuses them"""
    t_name, l_name, c_name = f"{PREFIX}-time", f"{PREFIX}-long", f"{PREFIX}-clip"
    t_slot, l_slot, c_slot = test_scenes[t_name][0], test_scenes[l_name][0], test_scenes[c_name][0]
    e0 = len(listen.events)

    # By slot, ended by its time, and the screen put back (it was clear: nothing)
    mark = listen.mark()
    r = await scene(node, dut_node_id, slot=t_slot)
    assert r.code == OK, message(r)
    s = await until_scene(listen, mark[0], 1.0, lambda sc: sc.running)
    # From here every Status shows it running until it ends: a mark for the waits after its end, which may find the
    # Status that carried both facts (scene over, screen put back) already in, rather than wait for the next (a second)
    running = listen.mark()
    assert scene_of(s) == (True, t_name, t_slot, 1) and 0 < s.scene.remaining_ms <= 1500 and not s.scene.until_clip_ends
    assert SHOWING[s.screen.showing] == "colour" and (s.screen.colour.r, s.screen.colour.g, s.screen.colour.b) == (16, 32, 48)
    status, reply = http.call("GET", "/scenes")
    active = reply["active"]
    assert active["name"] == t_name and active["slot"] == t_slot and active["then"] == "restore", active
    started = await listen.next_event(after=e0, timeout=1.0, where=lambda e: event_kind(e) == "scene_started")
    f = event_fields(started)
    assert (f["name"], f["slot"], f["then"], f["until_clip_ends"], f["duration_ms"]) == (t_name, t_slot, "restore", False, 1500), f
    ended = await listen.next_event(after=e0, timeout=3.0, where=lambda e: event_kind(e) == "scene_ended")
    f = event_fields(ended)
    assert (f["name"], f["slot"], f["then"], f["by"]) == (t_name, t_slot, "restore", "time"), f
    assert ended.seq > started.seq
    s = await until_scene(listen, running[0], 1.0, lambda sc: not sc.running)
    assert (s.scene.slot, s.scene.then, s.scene.remaining_ms, text(s.scene.name)) == (0, 0, 0, "")
    await listen.next_status(after=running[0], timeout=1.0, where=lambda s: s.screen.showing == 0)

    # By name, ended by Scene END: then off
    e1 = len(listen.events)
    mark = listen.mark()
    r = await scene(node, dut_node_id, name=l_name)
    assert r.code == OK, message(r)
    s = await until_scene(listen, mark[0], 1.0, lambda sc: sc.running)
    assert scene_of(s) == (True, l_name, l_slot, 2) and 58000 < s.scene.remaining_ms <= 60000
    mark = listen.mark()    # a Status after it showing the scene over can only be the end's
    r = await scene(node, dut_node_id, end=True)
    assert r.code == OK, message(r)
    ended = await listen.next_event(after=e1, timeout=1.0, where=lambda e: event_kind(e) == "scene_ended")
    assert event_fields(ended)["by"] == "end" and event_fields(ended)["then"] == "off"
    s = await until_scene(listen, mark[0], 1.0, lambda sc: not sc.running)
    s = await listen.next_status(after=mark[0], timeout=1.0,
                                 where=lambda s: s.screen.showing == 0 and RING[s.ring.now.mode] == "off")

    # Replaced: the long one by slot, then the timed one by name
    e2 = len(listen.events)
    assert (await scene(node, dut_node_id, slot=l_slot)).code == OK
    await listen.next_event(after=e2, timeout=1.0, where=lambda e: event_kind(e) == "scene_started")
    assert (await scene(node, dut_node_id, name=t_name)).code == OK
    ended = await listen.next_event(after=e2, timeout=1.0, where=lambda e: event_kind(e) == "scene_ended")
    assert (event_fields(ended)["name"], event_fields(ended)["by"]) == (l_name, "replaced"), event_fields(ended)
    await listen.next_event(after=e2, timeout=3.0,
                            where=lambda e: event_kind(e) == "scene_ended" and event_fields(e)["name"] == t_name)

    # Ended by its clip: the clip starts just before the scene, and the scene ends before what follows
    await asyncio.sleep(0.5)
    e3 = len(listen.events)
    mark = listen.mark()
    assert (await scene(node, dut_node_id, slot=c_slot)).code == OK
    s = await until_scene(listen, mark[0], 1.0, lambda sc: sc.running)
    assert scene_of(s) == (True, c_name, c_slot, 1) and s.scene.until_clip_ends and s.scene.remaining_ms == NEVER
    assert SHOWING[s.screen.showing] == "clip" and text(s.screen.path.path) == GIF and s.screen.plays == 1
    ended = await listen.next_event(after=e3, timeout=6.0, where=lambda e: event_kind(e) == "scene_ended")
    kinds = [(event_kind(e), event_fields(e)) for _, e in listen.events[e3:]]
    print("\nthe clip's scene: " + "; ".join(f"{k} {f}" for k, f in kinds))
    order = [k for k, _ in kinds]
    assert order[:4] == ["clip_started", "scene_started", "clip_ended", "scene_ended"], order
    assert kinds[0][1]["path"] == GIF and kinds[0][1]["plays"] == 1 and kinds[0][1]["frames"] == 60
    assert kinds[2][1]["finished"] is True and event_fields(ended)["by"] == "clip"
    seqs = [e.seq for _, e in listen.events[e1:]]
    assert seqs == list(range(seqs[0], seqs[0] + len(seqs))), f"a gap in the sequence numbers: {seqs}"

    # A stay scene: applied and done
    e4 = len(listen.events)
    mark = listen.mark()
    assert (await scene(node, dut_node_id, name=f"{PREFIX}-stay")).code == OK
    started = await listen.next_event(after=e4, timeout=1.0, where=lambda e: event_kind(e) == "scene_started")
    assert event_fields(started)["then"] == "stay" and event_fields(started)["slot"] == 0
    s = await listen.next_status(after=mark[0], timeout=1.0, where=lambda s: SHOWING[s.screen.showing] == "calibration")
    assert not s.scene.running

    # The refusals, each as the HTTP API's
    status, reply = http.call("GET", "/scenes")
    empty = next(n for n in range(255, 0, -1) if n not in {sc.get("slot") for sc in reply["scenes"]})
    refusals = [
        (dict(end=True), NOT_FOUND, "no scene is running to its end", ("POST", "/scenes/end", {}, 409, "not_running")),
        (dict(name=f"{PREFIX}-none"), NOT_FOUND, f"no scene '{PREFIX}-none'",
         ("POST", "/scenes/apply", {"name": f"{PREFIX}-none"}, 404, "unknown_scene")),
        (dict(slot=empty), NOT_FOUND, f"no scene with slot {empty}", None),
        (dict(name=f"{PREFIX}-missing"), NOT_PLAYABLE, None,
         ("POST", "/scenes/apply", {"name": f"{PREFIX}-missing"}, 422, "not_playable")),
        (dict(name="no/such"), BAD_REQUEST, "give `name`: 1-32 letters, digits, spaces, _ . and -", None),
        (dict(), BAD_REQUEST, "give `slot` (1..255), or `name` with slot 0", None),
        (dict(action=2), BAD_REQUEST, "`action` is 0 (apply) or 1 (end)", None),
    ]
    wrong = []
    for kwargs, code, words, via_http in refusals:
        r = await scene(node, dut_node_id, **kwargs)
        print(f"Scene {kwargs}: {r.code} {message(r)!r}")
        if via_http is not None:
            method, path, body, want_status, error = via_http
            status, reply = http.call(method, path, body=body)
            print(f"  over HTTP: {status} {reply}")
            if (status, reply.get("error")) != (want_status, error) or message(r) != reply["message"][:64]:
                wrong.append(f"{kwargs}: {r.code} {message(r)!r}, HTTP {status} {reply}")
        if r.code != code or (words is not None and message(r) != words):
            wrong.append(f"{kwargs}: {r.code} {message(r)!r}, not {code} {words!r}")
    assert not wrong, "\n".join(wrong)


async def test_screen(node, board, dut_node_id, listen, http):
    """4.3: a clip, a clip played twice, a still (PNG and JPEG), a colour, the crosshair, the backlight alone and with
    a file, and nothing: each done, and Status agrees with GET /screen and the console; a file that is missing, files
    that can't be shown, paths the board refuses and requests out of range: refused with the HTTP API's code and
    words, and nothing changed, the backlight included"""
    async def shows(where, after=None, timeout=3.0):
        return await listen.next_status(after=after, timeout=timeout, where=lambda s: where(s.screen))

    def agrees(s) -> None:
        """Status's screen part against GET /screen"""
        if http is None:
            return
        status, reply = http.call("GET", "/screen")
        sc = s.screen
        assert reply["showing"] == SHOWING[sc.showing] and reply["backlight"] == sc.backlight, (reply, sc)
        if reply["showing"] in ("image", "clip"):
            assert reply["path"] == text(sc.path.path), (reply, sc)
        if reply["showing"] == "colour":
            assert reply["colour"] == f"#{sc.colour.r:02x}{sc.colour.g:02x}{sc.colour.b:02x}", (reply, sc)
        if reply["showing"] == "clip":
            c = reply["clip"]
            assert (c["frames"], c["plays"], round(c["fps"])) == (sc.frames, sc.plays, round(float(sc.fps))), (c, sc)

    e0 = len(listen.events)
    r = await screen(node, dut_node_id, "file", path=CLIP, loop=True)
    assert r.code == OK, message(r)
    s = await shows(lambda sc: SHOWING[sc.showing] == "clip")
    assert text(s.screen.path.path) == CLIP and (s.screen.frames, s.screen.plays, round(float(s.screen.fps))) == (300, 0, 30)
    agrees(s)
    line = screen_line(board)
    assert CLIP in line, line
    started = await listen.next_event(after=e0, timeout=1.0, where=lambda e: event_kind(e) == "clip_started")
    assert event_fields(started)["path"] == CLIP and event_fields(started)["plays"] == 0
    # Its progress moves, and isn't a change: the once-a-second Status keeps it current
    await asyncio.sleep(1.2)
    later = listen.status[-1][1]
    assert later.screen.shown > s.screen.shown

    # Twice through
    r = await screen(node, dut_node_id, "file", path=CLIP, loops=2)
    assert r.code == OK, message(r)
    s = await shows(lambda sc: SHOWING[sc.showing] == "clip" and sc.plays == 2)
    agrees(s)

    # A still, with the backlight
    for path, backlight in [(PNG, 60), (JPEG, None)]:
        r = await screen(node, dut_node_id, "file", path=path, backlight=backlight)
        assert r.code == OK, message(r)
        s = await shows(lambda sc: SHOWING[sc.showing] == "image" and text(sc.path.path) == path)
        assert s.screen.frames == 0 and s.screen.plays == 0
        if backlight is not None:
            s = await shows(lambda sc: sc.backlight == backlight)
        agrees(s)
        assert path in screen_line(board), screen_line(board)
    ended = [e for _, e in listen.events[e0:] if event_kind(e) == "clip_ended"]
    assert ended and not event_fields(ended[-1])["finished"], [event_fields(e) for e in ended]

    # A colour, the crosshair, the backlight alone
    r = await screen(node, dut_node_id, "colour", colour=(10, 200, 30))
    assert r.code == OK
    s = await shows(lambda sc: SHOWING[sc.showing] == "colour" and (sc.colour.r, sc.colour.g, sc.colour.b) == (10, 200, 30))
    agrees(s)
    assert "#0ac81e" in screen_line(board)
    r = await screen(node, dut_node_id, "calibration")
    assert r.code == OK
    s = await shows(lambda sc: SHOWING[sc.showing] == "calibration")
    agrees(s)
    r = await screen(node, dut_node_id, "keep", backlight=35)
    assert r.code == OK
    s = await shows(lambda sc: sc.backlight == 35 and SHOWING[sc.showing] == "calibration")
    agrees(s)
    assert backlight_now(board) == 35
    r = await screen(node, dut_node_id, "keep")       # nothing at all
    assert r.code == OK

    # The refusals: nothing changes, the backlight included
    before = screen_line(board)
    refusals = [
        (dict(path=MISSING), NOT_FOUND, f"no such file: {MISSING}", 404),
        (dict(path="/progressive.jpg"), NOT_PLAYABLE, None, 422),
        (dict(path="/big400.png"), NOT_PLAYABLE, None, 422),
        (dict(path="/notes.png"), NOT_PLAYABLE, None, 422),
        (dict(path="/../rgb240.png"), BAD_REQUEST, None, 400),
        (dict(path="rgb240.png"), BAD_REQUEST, None, 400),
        (dict(path="/" + "a" * 50 + "/" + "b" * 50 + "/" + "c" * 60 + ".png"), BAD_REQUEST,
         "a path may be at most 159 bytes", 400),
        (dict(path="/" + "/".join(["d" * 60] * 4)[:254]), BAD_REQUEST, "a path may be at most 159 bytes", 400),
        (dict(path=CLIP, loop=True, loops=3), BAD_REQUEST,
         "give `loop` (forever) or `loops` (a number of times), not both", 400),
        (dict(path=CLIP, loops=1001), BAD_REQUEST, "`loops` is how many times it plays: 1..1000", 400),
        (dict(path=b"/rgb\x00240.png"), BAD_REQUEST, "control characters and \\ are not allowed in a path", None),
    ]
    wrong = []
    for kwargs, code, words, http_status in refusals:
        r = await screen(node, dut_node_id, "file", backlight=90, **kwargs)
        print(f"\nScreen file {kwargs}: {r.code} {message(r)!r}")
        if r.code != code or (words is not None and message(r) != words):
            wrong.append(f"{kwargs}: {r.code} {message(r)!r}, not {code} {words!r}")
        if http is not None and http_status is not None:
            body = {"path": kwargs["path"]}
            if kwargs.get("loop"):
                body["loop"] = True
            if kwargs.get("loops"):
                body["loops"] = kwargs["loops"]
            status, reply = http.call("POST", "/screen/show", body=body)
            print(f"  over HTTP: {status} {reply}")
            if status != http_status or reply["message"][:64] != message(r):
                wrong.append(f"{kwargs}: HTTP {status} {reply}, Cyphal {message(r)!r}")
    for show, kwargs, words in [(9, {}, "`show` is 0 (keep), 1 (a file), 2 (a colour), 3 (the crosshair) or 4 (nothing)"),
                                ("colour", {"backlight": 101}, "`backlight` is 0..100")]:
        r = await screen(node, dut_node_id, show, **kwargs)
        print(f"Screen {show} {kwargs}: {r.code} {message(r)!r}")
        if r.code != BAD_REQUEST or message(r)[:64] != words[:64]:
            wrong.append(f"{show} {kwargs}: {r.code} {message(r)!r}")
    assert not wrong, "\n".join(wrong)
    assert screen_line(board) == before and backlight_now(board) == 35

    # Nothing: the panel asleep
    r = await screen(node, dut_node_id, "off")
    assert r.code == OK
    s = await shows(lambda sc: SHOWING[sc.showing] == "nothing")
    agrees(s)
    assert screen_line(board).startswith("off")


RING_CASES = [
    (dict(mode="solid", colour=(255, 128, 0)), ("solid", (255, 128, 0), False, 0)),
    (dict(mode="wipe", colour=(0, 0, 255), loop=True), ("wipe", (0, 0, 255), True, 0)),
    (dict(mode="rainbow", loop=True), ("rainbow", "kept", True, 0)),
    (dict(mode="flicker", colour=(0, 255, 0)), ("flicker", (0, 255, 0), False, 0)),
    (dict(mode="pulse", period_ms=800), ("pulse", "kept", False, 800)),
    (dict(mode="pulse"), ("pulse", "kept", False, 2000)),
    (dict(mode="blink", colour=(255, 0, 0), period_ms=400), ("blink", (255, 0, 0), False, 400)),
    (dict(mode="blink"), ("blink", "kept", False, 1000)),
    (dict(colour=(10, 20, 30)), ("same", (10, 20, 30), "same", "same")),
    (dict(mode="off"), ("off", "kept", False, 0)),
]


async def test_ring(node, board, dut_node_id, listen, http):
    """4.4: every mode of the ring with and without its options, a colour alone, the brightness (below the board's
    own), and a request with nothing in it: Status's ring, the console's `leds` and GET /leds agree; Ring and Light
    keep one meaning of brightness and level; the refusals are PATCH /leds's"""
    board.command("leds colour 50,60,70")
    colour = (50, 60, 70)
    last_mode = "solid"
    for kwargs, (mode, want_colour, want_loop, period) in RING_CASES:
        mark = listen.mark()
        r = await ring(node, dut_node_id, **kwargs)
        assert r.code == OK, f"{kwargs}: {message(r)}"
        mode = last_mode if mode == "same" else mode
        colour = colour if want_colour == "kept" else want_colour

        def is_it(s, mode=mode, colour=colour, want_loop=want_loop, period=period) -> bool:
            n = s.ring.now
            return (RING[n.mode] == mode and (mode == "rainbow" or (n.colour.r, n.colour.g, n.colour.b) == colour) and
                    (want_loop == "same" or bool(n.loop) == want_loop) and (period == "same" or n.period_ms == period))
        s = await listen.next_status(after=mark[0], timeout=1.0, where=is_it)
        now = s.ring.now
        shown = board.first_line("leds")
        print(f"\nRing {kwargs}: {RING[now.mode]} {now.colour.r},{now.colour.g},{now.colour.b} loop {now.loop} period "
              f"{now.period_ms} at {now.brightness}%; console: {shown}")
        assert shown.startswith(mode), shown
        assert now.brightness == listen.brightness, (now.brightness, listen.brightness)
        if want_loop != "same":
            assert bool(now.loop) == want_loop
        if period != "same":
            assert now.period_ms == period
        if http is not None:
            status, reply = http.call("GET", "/leds")
            assert reply["mode"] == mode and reply["brightness"] == listen.brightness and reply["period_ms"] == now.period_ms
            assert reply["loop"] == bool(now.loop) and reply["level"] == 100, reply
        last_mode = RING[now.mode]

    # The brightness alone: the ring's own (leds bright), which Light's level is a share of
    lower = max(1, listen.brightness // 2)
    assert (await ring(node, dut_node_id, mode="solid", colour=(200, 100, 0))).code == OK
    # Each wait from a mark before its request, so that a Status already in counts (Ring and Light run on the worker,
    # whose Status follows the response, but a mark costs nothing)
    mark = listen.mark()
    r = await ring(node, dut_node_id, brightness=lower)
    assert r.code == OK
    s = await listen.next_status(after=mark[0], timeout=1.0, where=lambda s: s.ring.now.brightness == lower)
    assert RING[s.ring.now.mode] == "solid"
    assert f"brightness {lower}%" in board.first_line("leds")
    # Light at level 50 over it, then a Ring colour alone: the level stays (as PATCH /leds keeps it); a Ring mode
    # puts it back to 100
    mark = listen.mark()
    r = await light(node, dut_node_id, "on", level=50)
    assert r.result.code == OK
    await listen.next_status(after=mark[0], timeout=1.0, where=lambda s: s.ring.now.brightness == (lower * 50 + 50) // 100)
    mark = listen.mark()
    assert (await ring(node, dut_node_id, colour=(0, 200, 100))).code == OK
    s = await listen.next_status(after=mark[0], timeout=1.0, where=lambda s: s.ring.now.colour.g == 200)
    assert s.ring.now.brightness == (lower * 50 + 50) // 100
    mark = listen.mark()
    assert (await ring(node, dut_node_id, mode="flicker")).code == OK
    s = await listen.next_status(after=mark[0], timeout=1.0, where=lambda s: RING[s.ring.now.mode] == "flicker")
    assert s.ring.now.brightness == lower
    r = await ring(node, dut_node_id, brightness=listen.brightness)
    assert r.code == OK

    # Nothing in it: nothing changes
    before = board.first_line("leds")
    assert (await ring(node, dut_node_id)).code == OK
    assert board.first_line("leds") == before

    # Refusals, with PATCH /leds's words
    wrong = []
    for kwargs, body, words in [
        (dict(mode=7), None, "`mode` is off, solid, wipe, rainbow, flicker, pulse or blink"),
        (dict(brightness=101), {"brightness": 101}, "`brightness` is 1..100"),
        (dict(mode="pulse", period_ms=100), {"mode": "pulse", "period_ms": 100}, "`period_ms` is 200..60000"),
        (dict(period_ms=60001), {"period_ms": 60001}, "`period_ms` is 200..60000"),
    ]:
        r = await ring(node, dut_node_id, **kwargs)
        print(f"Ring {kwargs}: {r.code} {message(r)!r}")
        if r.code != BAD_REQUEST or message(r) != words:
            wrong.append(f"{kwargs}: {r.code} {message(r)!r}")
        if http is not None and body is not None:
            status, reply = http.call("PATCH", "/leds", body=body)
            if status != 400 or reply["message"] != words:
                wrong.append(f"HTTP {body}: {status} {reply}")
    assert not wrong, "\n".join(wrong)
    assert board.first_line("leds") == before


class EventStream:
    """The HTTP event stream (tools/api_smoke.py's Stream: GET /api/v1/events?kinds=...) read in a thread: every
    happening it sends, as JSON, until close()"""

    def __init__(self, http, kinds: str):
        self.events: list[dict] = []
        self._s = api_smoke.Stream(http, kinds)
        self._stop = False
        self._t = threading.Thread(target=self._read, daemon=True)
        self._t.start()

    def _read(self) -> None:
        try:
            while not self._stop:
                line = self._s.r.readline().decode().rstrip("\n")
                if line.startswith("data: "):
                    data = json.loads(line[6:])
                    if "seq" in data:
                        self.events.append(data)
        except Exception:       # closed under it
            pass

    def close(self) -> None:
        self._stop = True
        try:
            self._s.close()
        except Exception:
            pass


async def test_events_against_http(node, board, dut_node_id, listen, test_scenes, http):
    """4.5: twenty scene and clip changes, over Cyphal and the console: the Events are the HTTP event stream's
    happenings, the same ones in the same order with the same sequence numbers and fields"""
    loop = asyncio.get_running_loop()

    async def settled(since: int, timeout: float, until=lambda e: True, quiet: float = 0.25) -> None:
        """Until an Event after `since` for which until() holds has come and then none for `quiet` (or `timeout`: a
        change that makes none)"""
        end = loop.time() + timeout
        while loop.time() < end:
            if any(until(e) for _, e in listen.events[since:]) and loop.time() - listen.events[-1][0] >= quiet:
                return
            await asyncio.sleep(0.02)

    def quick_ended(e) -> bool:
        return event_kind(e) == "scene_ended" and event_fields(e)["name"] == f"{PREFIX}-quick"

    stream = EventStream(http, "clip_started,clip_ended,scene_started,scene_ended")
    await asyncio.sleep(0.5)        # the stream's listener in place on the board
    e0 = len(listen.events)
    q, l = test_scenes[f"{PREFIX}-quick"][0], test_scenes[f"{PREFIX}-long"][0]
    changes = [
        ("screen", dict(show="file", path=GIF)), ("scene", dict(slot=q)), ("screen", dict(show="file", path=CLIP, loop=True)),
        ("scene", dict(slot=l)), ("screen", dict(show="colour", colour=(1, 2, 3))), ("scene", dict(slot=q)),
        ("console", "video play testsrc240.mjpeg.mov loop"), ("scene", dict(name=f"{PREFIX}-long")),
        ("scene", dict(end=True)), ("screen", dict(show="file", path=GIF, loops=2)), ("scene", dict(slot=q)),
        ("screen", dict(show="file", path=PNG)), ("scene", dict(slot=l)), ("scene", dict(slot=q)),
        ("screen", dict(show="file", path=CLIP, loops=1)), ("console", "screen colour red"), ("scene", dict(slot=l)),
        ("scene", dict(end=True)), ("screen", dict(show="file", path=GIF)), ("screen", dict(show="off")),
    ]
    assert len(changes) == 20
    try:
        for how, what in changes:
            before = len(listen.events)
            if how == "screen":
                what = dict(what)
                r = await screen(node, dut_node_id, what.pop("show"), **what)
            elif how == "scene":
                r = await scene(node, dut_node_id, **what)
            else:
                board.command(what)
                r = None
            assert r is None or r.code == OK, f"{how} {what}: {message(r)}"
            if what == dict(slot=q):
                await settled(before, 2.0, quick_ended)     # ended by its time and what it puts back, as it would be
            else:
                await settled(before, 0.6)
        # The last of them on both (a quick scene's end, 0.6 s after it started, and what it puts back), then quiet
        end = loop.time() + 5.0
        while loop.time() < end:
            last = listen.events[-1][1].seq if listen.events else None
            if stream.events and stream.events[-1]["seq"] == last and loop.time() - listen.events[-1][0] >= 1.0:
                break
            await asyncio.sleep(0.05)
    finally:
        stream.close()
    got = [event_fields(e) for _, e in listen.events[e0:]]
    first = got[0]["seq"] if got else 0
    want = [json_fields(j) for j in stream.events if j["seq"] >= first]
    print(f"\n{len(got)} Events, {len(want)} HTTP happenings from seq {first}")
    for a, b in zip(got, want):
        if a != b:
            print(f"  differ: Cyphal {a}\n          HTTP   {b}")
    assert len(got) >= 30, got
    assert got == want
    seqs = [g["seq"] for g in got]
    assert seqs == list(range(seqs[0], seqs[0] + len(seqs))), seqs
    assert board.cyphal()["published"]["event"]["lost"] == 0


async def test_service_during_get_info(node, board, dut_node_id, listen, get_info):
    """4.6: Ring, Screen and Scene called while GetInfo is asked back to back, as fast as the node answers: every
    call to both answered, none lost"""
    loop = asyncio.get_running_loop()
    stop = False
    counts = {"calls": 0, "answered": 0}

    async def flood():
        while not stop:
            counts["calls"] += 1
            if await get_info.call(GetInfo.Request()) is not None:
                counts["answered"] += 1

    before = board.cyphal()
    task = asyncio.create_task(flood())
    services = []
    t0 = loop.time()
    try:
        i = 0
        while loop.time() - t0 < 5.0:
            i += 1
            r = await ring(node, dut_node_id, mode="solid", colour=(i * 8 % 256, 0, 255 - i * 8 % 256))
            services.append(("ring", r.code))
            if i % 3 == 0:
                r = await screen(node, dut_node_id, "colour", colour=(0, i * 8 % 256, 0))
                services.append(("screen", r.code))
            if i % 5 == 0:
                r = await scene(node, dut_node_id, end=True)
                services.append(("scene", r.code))
    finally:
        stop = True
        await task
    took = loop.time() - t0
    after = board.cyphal()
    print(f"\n{len(services)} service calls and {counts['answered']} of {counts['calls']} GetInfo in {took:.1f} s "
          f"({counts['calls'] / took:.0f} a second); busy {after['services']['busy'] - before['services']['busy']}; "
          f"dropped {after['dropped'] - before['dropped']}; send queue at most {after['text'].split('send queue at most ')[1].split(' ')[0]}")
    assert counts["answered"] == counts["calls"] and counts["calls"] > 100
    assert all(code == (NOT_FOUND if what == "scene" else OK) for what, code in services), services
    assert after["dropped"] == before["dropped"]


async def test_setpoints_while_the_screen_changes(node, board, dut_node_id, listen):
    """Setpoints at 50 a second while Screen shows stills (PNG, JPEG) and clips, one after another: the stills are
    decoded on the node's second worker, so the setpoints and Axes never wait for them. Each Screen request's time
    to its response is the work it is (what it would have held the worker for); the largest gap between two Axes and
    the longest from a setpoint to the Axes that reports it say whether the worker waited"""
    loop = asyncio.get_running_loop()
    pub = setpoints(node)
    sent: list[tuple[float, int]] = []
    held: dict[str, list[float]] = {}
    done = False
    before = board.cyphal()

    async def stream():
        i = 0
        t0 = loop.time()
        while not done:
            x = (i % 150) * 100 - 7500           # each a percent from the last: a position names its setpoint
            assert await pub.publish(Axes(x=x, y=-x // 2))
            sent.append((loop.time(), x))
            i += 1
            await asyncio.sleep(max(0.0, t0 + i * 0.02 - loop.time()))

    requests = [("png", dict(show="file", path=PNG)), ("png worst", dict(show="file", path=PNG_WORST)),
                ("png interlaced", dict(show="file", path="/interlaced240.png")), ("jpeg", dict(show="file", path=JPEG)),
                ("clip", dict(show="file", path=CLIP, loop=True)), ("colour", dict(show="colour", colour=(0, 0, 80))),
                ("gif", dict(show="file", path=GIF)), ("calibration", dict(show="calibration")),
                ("off", dict(show="off"))]
    mark = listen.mark()
    task = asyncio.create_task(stream())
    try:
        await asyncio.sleep(1.0)
        for _ in range(3):
            for kind, what in requests:
                what = dict(what)
                t = loop.time()
                r = await screen(node, dut_node_id, what.pop("show"), **what)
                held.setdefault(kind, []).append(loop.time() - t)
                assert r.code == OK, f"{kind}: {message(r)}"
                await asyncio.sleep(0.3)
        await asyncio.sleep(0.5)
    finally:
        done = True
        await task
        pub.close()
    after = board.cyphal()
    axes = listen.axes[mark[1]:]
    stamps = listen.axes_stamps[mark[1]:]
    during = [(t, a, st) for (t, a), st in zip(axes, stamps) if sent[0][0] + 0.3 <= t <= sent[-1][0]]
    gap = max(b[2] - a[2] for a, b in zip(during, during[1:]))
    delays = []
    for t, a, _ in during:
        match = [ts for ts, x in sent if ts <= t and abs(a.x - x) <= 60]
        if match:
            delays.append(t - match[-1])
    print(f"\n{len(sent)} setpoints, {len(during)} Axes during them; largest gap between Axes {gap * 1000:.0f} ms; "
          f"setpoint to its Axes: median {sorted(delays)[len(delays) // 2] * 1000:.0f} ms, worst {max(delays) * 1000:.0f} ms")
    for kind, times in held.items():
        print(f"  Screen {kind}: response in {', '.join(f'{x * 1000:.0f}' for x in times)} ms")
    print(f"  second worker's stack {after['text'].split('the second worker ')[1].strip()}")
    got = after["application"]["messages"] - before["application"]["messages"]
    assert got == len(sent), f"{got} of {len(sent)} setpoints reached the application"
    assert gap < 0.2, f"Axes stopped for {gap * 1000:.0f} ms"
    assert max(delays) < 0.25, max(delays)
    assert after["dropped"] == before["dropped"]
