"""The scenes runner and the clips it watches (components/scenes/runner.c), over HTTP and the console; the adapter is never
opened. A scene's own clip is known by its playback, not its path, so: a scene whose clip fails once accepted ends at
once, `by: replaced`, without its `then`; a scene whose clip is one frame ends `by: clip` and does its `then`; a scene
applied over another's playing clip, showing the same clip or another, is never ended by the old clip's end; a timed
scene's end and the apply of another never overlap (the first's `restore` never lands on the second); and a scene with
only LEDs, over a clip shown by /screen/show, ends only by its time.

The scenes are the tests' own, saved under names that start with PREFIX (no slot) and deleted after; the user's scenes
are never applied or changed. The media are test_video_player.py's (made with ffmpeg, uploaded under its MEDIA_DIR,
deleted after). The screen, the LEDs and the holo are left off.

    make can-test ARGS="--board-host <ip> -k test_scene_clips"
"""
from __future__ import annotations

import random
import time

import pytest

from .test_video_player import (EventStream, KINDS, CLIP, CLIP2, media, playback, screen_now,  # noqa: F401
                                started, wait_event)

PREFIX = "hwtest-scene"
SHORT_S = 0.4       # short.mov and short2.mov: 12 frames at 30 fps


@pytest.fixture
def events(need_http):
    stream = EventStream(need_http, KINDS)
    time.sleep(0.5)
    yield stream
    stream.close()


@pytest.fixture
def scenes(board, http, media):  # noqa: F811
    """Save the test's scenes: scenes(name=body, ...) -> the full names. Deleted after, with the screen, the LEDs and
    the holo off, and no scene running"""
    status, reply = http.call("GET", "/scenes")
    assert status == 200, reply
    theirs = [s for s in reply["scenes"] if not s["name"].startswith(PREFIX)]
    for s in reply["scenes"]:           # left by a run that was stopped
        if s["name"].startswith(PREFIX):
            http.call("DELETE", "/scenes/scene", query={"name": s["name"]})
    saved = []
    board.command("scene end")
    http.call("DELETE", "/screen")
    board.command("leds off")
    board.command("holo off")

    def save(**bodies) -> dict[str, str]:
        names = {}
        for key, body in bodies.items():
            name = f"{PREFIX}-{key}"
            status, reply = http.call("PUT", "/scenes/scene", query={"name": name}, body=body)
            assert status == 201, (name, status, reply)
            saved.append(name)
            names[key] = name
        return names

    try:
        yield save
    finally:
        board.command("scene end")
        for name in saved:
            http.call("DELETE", "/scenes/scene", query={"name": name})
        http.call("DELETE", "/screen")
        board.command("leds off")
        board.command("holo off")
        status, reply = http.call("GET", "/scenes")
        assert not any(s["name"].startswith(PREFIX) for s in reply["scenes"]), reply
        assert reply["scenes"] == theirs, "the user's scenes changed"


def apply(http, name: str) -> None:
    status, reply = http.call("POST", "/scenes/apply", body={"name": name})
    assert status == 200, (name, status, reply)


def scene_event(kind: str, name: str, seq_after: int = -1):
    return lambda e: e["event"] == kind and e["name"] == name and e["seq"] > seq_after


def active(http):
    status, reply = http.call("GET", "/scenes")
    assert status == 200, reply
    return reply["active"]


def test_scene_clip_fails_after_acceptance(board, need_http, media, scenes, events):  # noqa: F811
    """A `then: restore` scene, no `duration_s`, whose clip the probe accepts and the player cannot play: the apply is
    accepted; clip_ended `finished: false`; scene_ended `by: replaced` (after its scene_started); the restore not done
    (the colour shown before does not come back: the screen is dark); `active` null, and nothing more of it after"""
    http = need_http
    names = scenes(fail={"screen": {"path": media["noise.mov"]}, "then": "restore"})
    for _ in range(3):
        status, reply = http.call("POST", "/screen/show", body={"colour": "#102030"})
        assert status == 200, reply
        time.sleep(0.3)
        mark = len(events.events)
        apply(http, names["fail"])
        s, e = playback(events, media["noise.mov"], mark)
        assert e["finished"] is False, e
        st = wait_event(events, scene_event("scene_started", names["fail"]), mark)
        en = wait_event(events, scene_event("scene_ended", names["fail"]), mark)
        print(f"\n{[(x['event'], x['seq'], x.get('by', x.get('finished', ''))) for x in events.events[mark:]]}")
        assert st["until_clip_ends"] is True and st["then"] == "restore", st
        assert en["by"] == "replaced" and en["seq"] > st["seq"], en
        time.sleep(1.0)
        now = screen_now(http)
        assert now["showing"] == "nothing" and now["powered"] is False, f"restore done, or something shown: {now}"
        assert active(http) is None
        assert [x for x in events.events[mark:] if x["seq"] > en["seq"]] == [], "something happened after its end"


def test_scene_one_frame_clip(board, need_http, media, scenes, events):  # noqa: F811
    """A `then: restore` scene whose clip is one frame: it plays out at once, `finished: true`; scene_ended `by: clip`;
    the restore done (the colour before is back); `active` null"""
    http = need_http
    names = scenes(one={"screen": {"path": media["one.mov"]}, "then": "restore"})
    for _ in range(5):
        status, reply = http.call("POST", "/screen/show", body={"colour": "#203040"})
        assert status == 200, reply
        time.sleep(0.3)
        mark = len(events.events)
        apply(http, names["one"])
        s, e = playback(events, media["one.mov"], mark)
        assert e["finished"] is True, e
        st = wait_event(events, scene_event("scene_started", names["one"]), mark)
        en = wait_event(events, scene_event("scene_ended", names["one"]), mark)
        print(f"\n{[(x['event'], x['seq'], x.get('by', x.get('finished', ''))) for x in events.events[mark:]]}")
        assert en["by"] == "clip" and en["seq"] > st["seq"], en
        end = time.monotonic() + 2.0
        while (now := screen_now(http)).get("colour") != "#203040" and time.monotonic() < end:
            time.sleep(0.05)
        assert now["showing"] == "colour" and now["colour"] == "#203040" and now["powered"] is True, now
        assert active(http) is None


@pytest.mark.parametrize("same", [True, False], ids=["same-clip", "other-clip"])
def test_scene_over_a_scenes_clip(board, need_http, media, scenes, events, same):  # noqa: F811
    """Scene B (`then: off`, its clip once) applied over scene A's looping clip, 50 times, at different points of A's
    loop; B showing the same clip as A, and another. Every A ends `by: replaced`; every B ends `by: clip`, after its own
    clip's clip_ended (`finished: true`) and no sooner than its clip's length after its clip_started"""
    http = need_http
    a_clip = media["short.mov"]
    b_clip = a_clip if same else media["short2.mov"]
    names = scenes(a={"screen": {"path": a_clip, "loop": True}, "then": "restore", "duration_s": 60},
                   b={"screen": {"path": b_clip}, "then": "off"})
    rnd = random.Random(7)
    wrong = []
    shortest = 1e9
    for i in range(50):
        mark = len(events.events)
        apply(http, names["a"])
        a_start = wait_event(events, scene_event("scene_started", names["a"]), mark)
        time.sleep(rnd.uniform(0.05, 0.6))
        b_mark = len(events.events)
        apply(http, names["b"])
        a_end = wait_event(events, scene_event("scene_ended", names["a"], a_start["seq"]), mark)
        b_start = wait_event(events, scene_event("scene_started", names["b"]), b_mark)
        b_end = wait_event(events, scene_event("scene_ended", names["b"], b_start["seq"]), b_mark, timeout=4.0)
        # B's own clip: the clip_started for its path just before its scene_started, and the clip_ended after that
        b_clip_start = [x for x in events.events[b_mark:] if x["event"] == "clip_started" and x["path"] == b_clip
                        and x["seq"] < b_start["seq"]][-1]
        b_clip_end = next(x for x in events.events[b_mark:] if x["event"] == "clip_ended" and x["seq"] > b_clip_start["seq"])
        ran = b_end["t_ms"] - b_clip_start["t_ms"]
        shortest = min(shortest, ran)
        problems = []
        if a_end["by"] != "replaced":
            problems.append(f"A ended by {a_end['by']}")
        if b_end["by"] != "clip":
            problems.append(f"B ended by {b_end['by']}")
        if not (b_clip_end["path"] == b_clip and b_clip_end["finished"] is True and b_clip_end["seq"] < b_end["seq"]):
            problems.append(f"B's clip_ended {b_clip_end} vs its scene_ended {b_end}")
        if ran < SHORT_S * 1000 * 0.8:
            problems.append(f"B ended {ran} ms after its clip started, under the clip's {SHORT_S * 1000:.0f} ms")
        if problems:
            wrong.append((i, problems, [(x["event"], x["seq"], x.get("name", x.get("path")), x.get("by", x.get("finished")))
                                        for x in events.events[mark:]]))
        time.sleep(0.1)
    print(f"\n{'same' if same else 'another'} clip: 50 applies of B over A's clip; B ran at least {shortest} ms from its "
          f"clip's start to its end; {len(wrong)} wrong")
    for w in wrong[:5]:
        print(f"  {w}")
    assert not wrong, wrong[:5]


def test_timed_scene_end_and_apply(board, need_http, media, scenes, events):  # noqa: F811
    """Scene A (`duration_s: 1`, `then: restore`: the screen, the LEDs and the holo) and scene B applied around A's
    1 s mark, 20 times: each time either A ends `by: time` before B starts, or `by: replaced`; and B's screen, LEDs and
    holo are what is left, never what A's restore puts back"""
    http = need_http
    names = scenes(a={"screen": {"colour": "#300000"}, "leds": {"mode": "solid", "colour": "#003000"},
                      "holo": {"motion": "move", "x": 30, "y": 0, "duration_ms": 200}, "then": "restore",
                      "duration_s": 1},
                   b={"screen": {"colour": "#000030"}, "leds": {"mode": "solid", "colour": "#000040"},
                      "holo": {"motion": "move", "x": -30, "y": 0, "duration_ms": 200}})
    rnd = random.Random(11)
    outcomes = {"time": 0, "replaced": 0}
    wrong = []
    for i in range(20):
        http.call("DELETE", "/screen")
        board.command("leds off")
        board.command("holo center")
        time.sleep(0.4)
        mark = len(events.events)
        apply(http, names["a"])
        a_start = wait_event(events, scene_event("scene_started", names["a"]), mark)
        # Around the 1 s mark by the board's clock: A's start is when its apply came back, near enough
        time.sleep(1.0 + rnd.uniform(-0.12, 0.12) - 0.05)
        apply(http, names["b"])
        b_start = wait_event(events, scene_event("scene_started", names["b"]), mark)
        a_end = wait_event(events, scene_event("scene_ended", names["a"], a_start["seq"]), mark)
        outcomes[a_end["by"]] = outcomes.get(a_end["by"], 0) + 1
        time.sleep(0.6)     # B's move done; A's restore, if it were to land late, landed
        now = screen_now(http)
        _, leds = http.call("GET", "/leds")
        _, holo = http.call("GET", "/holo")
        problems = []
        if a_end["by"] == "time" and not a_end["seq"] < b_start["seq"]:
            problems.append(f"A ended by its time after B started: {a_end} {b_start}")
        if a_end["by"] not in ("time", "replaced"):
            problems.append(f"A ended by {a_end['by']}")
        if (now["showing"], now.get("colour")) != ("colour", "#000030"):
            problems.append(f"screen {now}")
        if (leds["mode"], leds["colour"]) != ("solid", "#000040"):
            problems.append(f"leds {leds}")
        if abs(holo.get("position", {}).get("x", 9) - -0.3) > 0.02:     # a fraction of the way: -30 %
            problems.append(f"holo {holo}")
        if active(http) is not None:
            problems.append(f"active {active(http)}")
        if problems:
            wrong.append((i, a_end["by"], problems))
    print(f"\nA's end around B's apply: {outcomes}; {len(wrong)} wrong")
    for w in wrong[:5]:
        print(f"  {w}")
    assert not wrong, wrong[:5]


def test_leds_scene_over_a_shown_clip(board, need_http, media, scenes, events):  # noqa: F811
    """A scene with only LEDs (`duration_s: 2`, `then: restore`) over a clip shown by /screen/show, and the screen changed
    under it (another clip, then a colour): it ends by its time, at 2 s, not by either change; the LEDs go back off and
    the clip from before it plays again (`restore` puts back the screen too)"""
    http = need_http
    names = scenes(leds={"leds": {"mode": "solid", "colour": "#001020"}, "then": "restore", "duration_s": 2})
    for _ in range(3):
        status, reply = http.call("POST", "/screen/show", body={"path": CLIP, "loop": True})
        assert status == 200, reply
        time.sleep(0.5)
        mark = len(events.events)
        apply(http, names["leds"])
        st = wait_event(events, scene_event("scene_started", names["leds"]), mark)
        assert st["until_clip_ends"] is False, st
        time.sleep(0.6)
        status, reply = http.call("POST", "/screen/show", body={"path": CLIP2, "loop": True})
        assert status == 200, reply
        time.sleep(0.5)
        assert active(http) is not None, "the LED scene ended when the screen's clip changed"
        status, reply = http.call("POST", "/screen/show", body={"colour": "#050505"})
        assert status == 200, reply
        en = wait_event(events, scene_event("scene_ended", names["leds"], st["seq"]), mark, timeout=4.0)
        took = en["t_ms"] - st["t_ms"]
        print(f"\nthe LED scene ended by {en['by']} {took} ms after it started")
        assert en["by"] == "time" and 1900 <= took <= 2400, en
        # `restore` puts back the screen too, whatever the scene's parts (manual/reference/http-api.md, "When a scene
        # ends"): the looping clip shown before it plays again, over the colour shown during it
        wait_event(events, lambda e: started(CLIP)(e) and e["seq"] > en["seq"], mark, timeout=2.0)
        time.sleep(0.3)
        _, leds = http.call("GET", "/leds")
        assert leds["mode"] == "off", leds
        now = screen_now(http)
        assert now["showing"] == "clip" and now["path"] == CLIP and now["loop"] is True, now
        http.call("DELETE", "/screen")
        time.sleep(0.3)
