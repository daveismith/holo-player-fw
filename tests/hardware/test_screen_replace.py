"""What is on the screen replaced several times a second over HTTP (POST /api/v1/screen/show): clip by clip, GIF by
GIF, clip by GIF and GIF by clip, and clips with stills between. Every show is accepted, the screen's happenings come in
order (each clip_started followed by its own clip_ended before the next starts, the sequence numbers unbroken), the
screen settles on the last show, and the internal heap at rest afterwards is where it was. The adapter is never opened:
HTTP and the console only.

The video player used to make an 8 KB task for each clip and GIF, and give it back only once the idle task got round to
it, so one clip replacing another could briefly hold two stacks (CLAUDE.md, "Memory"). And a 240-wide GIF shown over a
playing clip or GIF was always refused ("it could not be drawn"): it was opened, its decoder and its two internal DMA
paint blocks taken, before the playback it replaced had stopped and given back its own. It is now opened on the
player's task once that playback has stopped (gif_open(), the blocks first), so the GIF cases are ordinary ones.

    make can-test ARGS="--board-host <ip> -k test_screen_replace"
"""
from __future__ import annotations

import time

import pytest

from .test_holo_scenes import EventStream

RATE_HZ = 8
SECONDS = 10
KINDS = "clip_started,clip_ended"

CASES = {
    "clip-clip": [{"path": "/leia240.mjpeg.mov", "loop": True}, {"path": "/testsrc240.mjpeg.mov", "loop": True}],
    "gif-gif": [{"path": "/testsrc240.gif", "loop": True}, {"path": "/testsrc120.gif", "loop": True}],
    # A 240-wide GIF over a playing clip, and the clip over the GIF
    "clip-gif": [{"path": "/leia240.mjpeg.mov", "loop": True}, {"path": "/testsrc240.gif", "loop": True}],
    "clip-still-clip": [{"path": "/leia240.mjpeg.mov", "loop": True}, {"path": "/j420_240.jpg"},
                        {"path": "/testsrc240.mjpeg.mov", "loop": True}, {"path": "/rgb240.png"}],
}
STILLS = (".jpg", ".png")


def at_rest(board, http) -> int:
    """The internal heap's free with nothing on the screen, a second after it cleared"""
    status, _ = http.call("POST", "/screen/show", body={"colour": "#000000"})
    assert status == 200
    board.command("screen clear")
    time.sleep(1.0)
    return board.cyphal()["internal_free"]


def check_order(events: list[dict], shown: list[str]) -> None:
    """Each clip_started is the next clip shown, and is followed by its own clip_ended before anything else starts;
    the sequence numbers run on without a gap (nothing else happens meanwhile)"""
    clips_shown = [p for p in shown if not p.endswith(STILLS)]
    assert events or not clips_shown, f"{len(clips_shown)} clips shown and no clip_started or clip_ended came"
    seqs = [e["seq"] for e in events]
    assert seqs == list(range(seqs[0], seqs[0] + len(seqs))), f"a gap in the sequence numbers: {seqs}"
    clips = iter(p for p in shown if not p.endswith(STILLS))
    playing = None
    for e in events:
        if e["event"] == "clip_started":
            assert playing is None, f"{e['path']} started while {playing} had not ended"
            assert e["path"] == next(clips), f"started {e['path']}, not the next clip shown"
            playing = e["path"]
        else:
            assert e["path"] == playing, f"clip_ended for {e['path']}, while {playing} was playing"
            assert e["finished"] is False, f"{e['path']} ended by itself, not replaced"
            playing = None
    started = [e["path"] for e in events if e["event"] == "clip_started"]
    assert started == clips_shown, f"started {len(started)} clips, shown {len(clips_shown)}: {started} vs {clips_shown}"
    assert playing is None, f"{playing} started and never ended"


@pytest.mark.parametrize("case", list(CASES))
def test_rapid_replacement(board, need_http, case):
    http = need_http
    shows = CASES[case]
    rest_before = at_rest(board, http)
    stream = EventStream(http, KINDS)
    time.sleep(0.5)                 # the stream's listener in place on the board
    refused, shown = [], []
    try:
        t0 = time.monotonic()
        n = 0
        while time.monotonic() - t0 < SECONDS:
            due = t0 + n / RATE_HZ
            time.sleep(max(0.0, due - time.monotonic()))
            show = shows[n % len(shows)]
            status, reply = http.call("POST", "/screen/show", body=show)
            if status == 200:
                shown.append(show["path"])
            else:
                refused.append((n, show["path"], status, reply))
            n += 1
        rate = n / (time.monotonic() - t0)
        # Settled: the screen shows the last of them
        end = time.monotonic() + 3.0
        while True:
            status, screen = http.call("GET", "/screen")
            if not shown or screen.get("path") == shown[-1] or time.monotonic() > end:
                break
            time.sleep(0.1)
        lowest = board.cyphal()["internal_lowest"]
    finally:
        time.sleep(0.5)
        rest_after = at_rest(board, http)
        stream.close()
    events = [e for e in stream.events if e["event"] in KINDS.split(",")]
    print(f"\n{case}: {n} shows at {rate:.1f}/s, {len(refused)} refused; {len(events)} happenings; internal heap at "
          f"rest {rest_before} before, {rest_after} after; lowest since the board's start {lowest}")
    assert shown, f"every one of the {n} shows was refused: {refused[:5]}"
    assert screen.get("path") == shown[-1], f"the screen settled on {screen}, not {shown[-1]}"
    want = "image" if shown[-1].endswith(STILLS) else "clip"
    assert screen["showing"] == want, screen
    check_order(events, shown)
    # The heap at rest where it was, give or take the record of happenings, which grew by the new ones' JSON
    # (a record of 32 at most, each at most 200 bytes, so it can't grow by more than 6.4 KB)
    assert rest_after >= rest_before - 32 * 200, f"the internal heap at rest fell from {rest_before} to {rest_after}"
    assert not refused, f"refused: {refused[:5]}"
