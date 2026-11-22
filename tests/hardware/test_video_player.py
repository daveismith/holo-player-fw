"""The video player's behaviours (components/video/video_player.cpp), over HTTP and the console; the adapter is never
opened. A GIF's passes and the still it leaves; a GIF with a long frame stopped in the middle of it, and replaced by a
still and by a one-frame GIF; a clip that the probe accepts and that fails on the player's task; a colour and the
crosshair over a playing clip; and, last, the player's stack after all of that and many more GIF and clip starts.

The test media are made on the computer with ffmpeg (skipped without it), uploaded over HTTP under MEDIA_DIR and
deleted after; the board's own test files (/testsrc120.gif and the rest, as test_holo_scenes.py uses them) are only
shown. Each test leaves the screen clear.

    make can-test ARGS="--board-host <ip> -k test_video_player"

Not exercised here: the refusal of every show when the previous playback has not stopped within 2 s ("what was playing
did not stop within 2 s"): nothing in a normal build takes that long to stop, so it needs a fault build. Nor a GIF that
cannot get its memory once accepted (its two internal DMA blocks are 7.5 KB for a 240-wide one, which an idle board
always has): only the clip's equivalent, "out of memory for the decode buffers", can be brought about with a file.
And a clip that "cannot read frame": the probe refuses a clip with any frame outside the file ("a frame lies outside the
file"), so a truncated file never reaches the player.
"""
from __future__ import annotations

import re
import shutil
import subprocess
import time

import pytest

from .test_holo_scenes import EventStream

MEDIA_DIR = "/hwtest-media"
KINDS = "clip_started,clip_ended,scene_started,scene_ended"
GIF = "/testsrc120.gif"         # the board's: 120x120, 60 frames at 20 fps, 3 s
CLIP = "/leia240.mjpeg.mov"
CLIP2 = "/testsrc240.mjpeg.mov"
STILL = "/j420_240.jpg"
# The video task's stack is 4,608 bytes; this much never used is the least that is comfortable
STACK_MARGIN = 1536

# name -> ffmpeg arguments after `ffmpeg -v error -y`, the output last
FFMPEG = {
    # Three frames of 5 s each, looping: a stop always lands in the middle of a frame
    "slow.gif": ["-f", "lavfi", "-i", "testsrc=size=120x120:rate=0.2", "-frames:v", "3", "-loop", "0"],
    # One frame: a still, though a GIF
    "one.gif": ["-f", "lavfi", "-i", "testsrc=size=120x120:rate=1", "-frames:v", "1"],
    # One frame of a clip: it plays out at once
    "one.mov": ["-f", "lavfi", "-i", "testsrc=size=240x240:rate=30", "-frames:v", "1", "-c:v", "mjpeg",
                "-pix_fmt", "yuvj420p", "-q:v", "5"],
    # A short clip, 12 frames (0.4 s), and another unlike it, for the scenes
    "short.mov": ["-f", "lavfi", "-i", "testsrc=size=240x240:rate=30", "-frames:v", "12", "-c:v", "mjpeg",
                  "-pix_fmt", "yuvj420p", "-q:v", "5"],
    "short2.mov": ["-f", "lavfi", "-i", "smptebars=size=240x240:rate=30", "-frames:v", "12", "-c:v", "mjpeg",
                   "-pix_fmt", "yuvj420p", "-q:v", "5"],
    # Noise at the best quality: about 75 KB a frame, more than the internal RAM the player's frame buffer must come
    # from (MaxFrameSize(), internal only): the probe accepts it, the player runs out of memory for it
    "noise.mov": ["-f", "lavfi", "-i", "nullsrc=s=240x240:r=30,geq=lum='random(1)*255':cb='random(2)*255':"
                  "cr='random(3)*255'", "-frames:v", "6", "-c:v", "mjpeg", "-pix_fmt", "yuvj420p", "-q:v", "1",
                  "-qmin", "1", "-qmax", "1"],
}


def path_of(name: str) -> str:
    return f"{MEDIA_DIR}/{name}"


def rel(path: str) -> str:
    """A path on the volume as the console takes it (relative to its base)"""
    return path.lstrip("/")


@pytest.fixture(scope="module")
def media(http, tmp_path_factory):
    """FFMPEG's files made and uploaded under MEDIA_DIR: name -> its path on the board. The folder deleted after"""
    if http is None:
        pytest.skip("the media are uploaded over HTTP: --board-host")
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        pytest.skip("the test media are made with ffmpeg, not found")
    out = tmp_path_factory.mktemp("media")
    status, _ = http.call("GET", "/fs/entry", query={"path": MEDIA_DIR})
    if status == 200:       # left by a run that was stopped
        http.call("DELETE", "/fs/entry", query={"path": MEDIA_DIR, "recursive": "true"})
    made = {}
    try:
        for name, args in FFMPEG.items():
            f = out / name
            subprocess.run([ffmpeg, "-v", "error", "-y", *args, str(f)], check=True, timeout=60)
            status, reply = http.call("PUT", "/fs/file", query={"path": path_of(name), "parents": "true",
                                                                 "overwrite": "true"}, raw=f.read_bytes())
            assert status in (200, 201), (name, status, reply)
            made[name] = path_of(name)
        yield made
    finally:
        http.call("DELETE", "/screen")      # nothing of ours left on the screen as the files go
        status, reply = http.call("DELETE", "/fs/entry", query={"path": MEDIA_DIR, "recursive": "true"})
        assert status in (204, 404), reply


@pytest.fixture
def events(need_http):
    """The event stream (clip and scene happenings) from before the test until after it"""
    stream = EventStream(need_http, KINDS)
    time.sleep(0.5)     # its listener in place on the board
    yield stream
    stream.close()


@pytest.fixture
def clear_screen(board, http):
    """The screen clear before and after, and no scene running"""
    board.command("scene end")
    http.call("DELETE", "/screen")
    yield
    board.command("scene end")
    http.call("DELETE", "/screen")


def show(http, body: dict) -> tuple[int, dict, float]:
    """POST /screen/show: (status, reply, seconds it took)"""
    t = time.monotonic()
    status, reply = http.call("POST", "/screen/show", body=body)
    return status, reply, time.monotonic() - t


def screen_now(http) -> dict:
    status, reply = http.call("GET", "/screen")
    assert status == 200, reply
    return reply


def wait_event(stream: EventStream, where, after: int = 0, timeout: float = 5.0) -> dict:
    """The first happening from index `after` of the stream for which where() holds; the test fails without one"""
    end = time.monotonic() + timeout
    while True:
        for e in stream.events[after:]:
            if where(e):
                return e
        if time.monotonic() > end:
            raise AssertionError(f"no such happening in {timeout} s; since then: {stream.events[after:]}")
        time.sleep(0.02)


def started(path: str):
    return lambda e: e["event"] == "clip_started" and e["path"] == path


def ended(path: str):
    return lambda e: e["event"] == "clip_ended" and e["path"] == path


def playback(stream: EventStream, path: str, after: int, timeout: float = 5.0) -> tuple[dict, dict]:
    """A clip's clip_started and its own clip_ended (the next clip_ended after it, which must be for the same path)"""
    s = wait_event(stream, started(path), after, timeout)
    e = wait_event(stream, lambda e: e["event"] == "clip_ended" and e["seq"] > s["seq"], after, timeout)
    assert e["path"] == path, f"after {s}, the next clip_ended was {e}"
    return s, e


def video_stack_unused(board) -> int:
    """`tasks`' high-water mark for the video task: the bytes of its stack never used since the board's start"""
    out = board.command("tasks")
    m = re.search(r"^video\s+\S+\s+\d+\s+(\d+)", out, re.M)
    assert m, out
    return int(m.group(1))


# ---------------------------------------------------------------------------------------------------------- the tests


def test_gif_loops_then_holds(board, need_http, media, clear_screen, events):
    """A GIF with `loops: 2` plays twice (6 s for a 3 s GIF), then holds its last frame as a still; clip_ended says
    `finished: true`. What GET /screen says in the first 100 ms of the play has the right `plays` and `loop`"""
    http = need_http
    for attempt in range(3):
        mark = len(events.events)
        status, reply, _ = show(http, {"path": GIF, "loops": 2})
        assert status == 200, reply
        first = screen_now(http)
        if first.get("clip", {}).get("elapsed_ms", 1e9) <= 100:
            break
        print(f"\nGET /screen came {first.get('clip', {}).get('elapsed_ms')} ms into the play (Wi-Fi); again")
        http.call("DELETE", "/screen")
        time.sleep(0.5)
    else:
        pytest.fail("no GET /screen within the first 100 ms of the play in three tries")
    print(f"\nthe show's reply: {reply}\nGET /screen at {first['clip']['elapsed_ms']} ms: {first}")
    for what in (reply, first):
        assert what["showing"] == "clip" and what["path"] == GIF, what
        assert what["loop"] is False and what["clip"]["plays"] == 2, what
        assert what["clip"]["frames"] == 60 and what["clip"]["loops"] == 0, what
    time.sleep(4.0)
    middle = screen_now(http)
    assert middle["showing"] == "clip" and middle["clip"]["loops"] == 1 and middle["clip"]["plays"] == 2, middle
    s, e = playback(events, GIF, mark, timeout=10.0)
    took = e["t_ms"] - s["t_ms"]
    print(f"clip_started {s}\nclip_ended {e}: {took} ms after")
    assert s["plays"] == 2 and s["loop"] is False and s["frames"] == 60, s
    assert e["finished"] is True, e
    assert 5700 <= took <= 6800, f"two passes of a 3 s GIF took {took} ms"
    held = screen_now(http)
    assert held["showing"] == "image" and held["path"] == GIF and held["powered"] is True, held
    assert "clip" not in held, held
    line = board.command("screen").strip().splitlines()[0]
    assert "testsrc120.gif" in line and not line.startswith("off"), line


def test_gif_stopped_mid_frame_and_replaced(board, need_http, media, clear_screen, events):
    """A GIF whose frames are 5 s long, stopped 1 s into its first frame: by a colour from the console, which comes back
    as promptly as any command (the stop cuts the wait for the next frame short), `finished: false`; and over HTTP,
    replaced by a still and by a one-frame GIF, each taking its place well before the frame would have ended"""
    http = need_http
    slow = media["slow.gif"]
    for i in range(3):
        mark = len(events.events)
        out = board.command(f"image show {rel(slow)}")
        assert "image: playing" in out, out
        time.sleep(1.0)
        t = time.monotonic()
        board.command("screen")
        plain = time.monotonic() - t        # what any command takes on the console
        t = time.monotonic()
        board.command("screen colour blue")
        stop = time.monotonic() - t
        s, e = playback(events, slow, mark)
        print(f"\ncolour over the GIF: {stop * 1000:.0f} ms (a plain command {plain * 1000:.0f} ms); the GIF ran "
              f"{e['t_ms'] - s['t_ms']} ms")
        assert e["finished"] is False, e
        assert stop < plain + 0.5, f"the colour took {stop:.2f} s, a plain command {plain:.2f} s"
        assert e["t_ms"] - s["t_ms"] < 3000, (s, e)
        now = screen_now(http)
        assert now["showing"] == "colour" and now["colour"] == "#0000ff" and now["powered"] is True, now

    for path, what in [(STILL, "a still"), (media["one.gif"], "a one-frame GIF")]:
        mark = len(events.events)
        status, reply, _ = show(http, {"path": slow, "loop": True})
        assert status == 200 and reply["showing"] == "clip", reply
        time.sleep(1.0)
        status, reply, took = show(http, {"path": path})
        assert status == 200, reply
        s, e = playback(events, slow, mark)
        print(f"{what} over the GIF: {took * 1000:.0f} ms over HTTP; the GIF ran {e['t_ms'] - s['t_ms']} ms by the "
              "board's clock")
        assert e["finished"] is False, e
        # The board's clock, not the request's time (Wi-Fi alone has held a request for 2 s on the bench): a stop that
        # waited for the frame would come at 5 s or later
        assert e["t_ms"] - s["t_ms"] < 4500, (s, e)
        now = screen_now(http)
        assert now["showing"] == "image" and now["path"] == path and now["powered"] is True, now
        assert not any(x["event"] == "clip_started" and x["seq"] > e["seq"] for x in events.events[mark:]), \
            "a still started a clip"


def test_clip_failing_after_acceptance(board, need_http, media, clear_screen, events):
    """A clip the probe accepts and the player cannot play (frames larger than the internal RAM its buffer must come
    from): accepted (200), then clip_started and clip_ended `finished: false`, "out of memory for the decode buffers"
    on the console, and the panel asleep with nothing showing -- from a sleeping panel and over a playing clip, whose
    own clip_ended says `finished: false` too"""
    http = need_http
    noise = media["noise.mov"]
    status, info = http.call("GET", "/media/info", query={"path": noise})
    assert status == 200 and info["playable"] is True and info["kind"] == "clip", info
    for over in (None, CLIP):
        mark = len(events.events)
        if over is not None:
            status, reply, _ = show(http, {"path": over, "loop": True})
            assert status == 200, reply
            time.sleep(1.0)
            assert screen_now(http)["powered"] is True
        log = board.mark()
        status, reply, _ = show(http, {"path": noise})
        assert status == 200, reply
        assert reply["showing"] == "clip" and reply["path"] == noise, reply
        s, e = playback(events, noise, mark)
        print(f"\nover {over or 'a sleeping panel'}: {s}\n  {e}")
        assert e["finished"] is False, e
        if over is not None:
            os_, oe = playback(events, over, mark)
            assert oe["finished"] is False and oe["seq"] < s["seq"], (os_, oe, s)
        end = time.monotonic() + 1.0
        while (now := screen_now(http))["showing"] != "nothing" and time.monotonic() < end:
            time.sleep(0.05)
        assert now["showing"] == "nothing" and now["powered"] is False, now
        assert board.wait_for(re.compile(r"video: out of memory for the decode buffers"), log, 2.0), board.since(log)
        assert board.command("screen").strip().startswith("off"), "the console says something is showing"


def test_colour_and_calibration_over_a_clip(board, need_http, media, clear_screen, events):
    """A colour and the crosshair over a playing clip, over HTTP and the console: each shown at once, the panel on, and
    the clip's clip_ended `finished: false`"""
    http = need_http
    cases = [("http", {"colour": "#103050"}, "colour"), ("http", {"calibration": True}, "calibration"),
             ("console", "screen colour 16,48,80", "colour"), ("console", "screen calibration", "calibration")]
    for how, what, showing in cases:
        mark = len(events.events)
        status, reply, _ = show(http, {"path": CLIP2, "loop": True})
        assert status == 200, reply
        time.sleep(0.7)
        if how == "http":
            status, reply, took = show(http, what)
            assert status == 200, reply
            assert reply["showing"] == showing, reply
        else:
            out = board.command(what)
            assert "did not stop" not in out, out
        s, e = playback(events, CLIP2, mark)
        assert e["finished"] is False, e
        now = screen_now(http)
        print(f"\n{how} {what}: {now}; the clip ran {e['t_ms'] - s['t_ms']} ms")
        assert now["showing"] == showing and now["powered"] is True, now
        if showing == "colour":
            assert now["colour"] == "#103050", now


def test_video_task_stack(board, need_http, media, clear_screen, events):
    """The video task's stack after everything above and many GIF and clip starts more (GIFs of every kind the board
    has, opened on the player's task since the GIF change; clips streamed and whole-frame, played out, stopped and
    failing): `tasks`' high-water mark keeps at least STACK_MARGIN of its 4,608 bytes never used. Last in the file, so
    the mark covers the tests before it"""
    http = need_http
    before = video_stack_unused(board)
    gifs = ["testsrc240.gif", "noise25fps.gif", "dispose2.gif", "transparent.gif", "localpal.gif", "trail.gif",
            "once.gif", "testsrc120.gif", "loop3.gif", "nodelay.gif", "solid.gif", rel(media["slow.gif"])]
    clips = ["video play leia240.mjpeg.mov loop", "video play testsrc240.mjpeg.mov frame",
             "video play rainbow_2.mjpeg.mov", f"video play {rel(media['one.mov'])}",
             f"video play {rel(media['noise.mov'])}", f"video play {rel(media['short.mov'])}"]
    for _ in range(2):
        for g in gifs:
            board.command(f"image show {g}")
            time.sleep(1.3)
        for c in clips:
            board.command(c)
            time.sleep(1.3)
    # Rapid GIF starts over HTTP, each over the last
    for i in range(30):
        status, reply, _ = show(http, {"path": ["/testsrc240.gif", "/noise25fps.gif", "/testsrc120.gif"][i % 3],
                                       "loop": True})
        assert status == 200, reply
        time.sleep(0.15)
    time.sleep(1.0)
    http.call("DELETE", "/screen")
    after = video_stack_unused(board)
    print(f"\nthe video task's stack: {before} bytes never used before this test, {after} after (of 4608)")
    assert after >= STACK_MARGIN, f"the video task has used all but {after} bytes of its 4608"
