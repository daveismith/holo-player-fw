"""Test 3.7: the holo over Cyphal while a clip plays, for --holo-minutes (3 by default; the phase gates run 10): a
motion every few seconds, a burst of setpoints at 50 a second every 20 s, a light change now and then, with the clip
looping on the screen. The clip keeps its 30 frames a second with no late frame, the heartbeat is never late, Status
and Axes keep their rates, nothing is dropped, and the internal heap is flat: nothing kept at rest afterwards that
wasn't before, and the lowest it reaches stays within one decoded frame's worth of where it was once everything had
run once."""
from __future__ import annotations

import asyncio
import math
import random
import re

import pytest
import r2.holo.Axes_0_1 as Axes

from .conftest import Beats, gaps
from .holo_ports import OK, Listen, light, motion, setpoints

# A 240x240 clip's JPEG decoder (esp_new_jpeg, opened and closed for every frame): the two levels the internal heap's
# free reads at while one plays, measured 2026-11-21 (50,459 and 41,587 bytes)
DECODER_BYTES = 8872


def video(board) -> dict:
    text = board.command("video status")
    shown = re.search(r"shown (\d+) in ([\d.]+) s \(([\d.]+) fps\)", text)
    late = re.search(r"(\d+) late", text)
    return {"text": text, "shown": int(shown.group(1)) if shown else 0, "secs": float(shown.group(2)) if shown else 0,
            "fps": float(shown.group(3)) if shown else 0, "late": int(late.group(1)) if late else None}


@pytest.mark.slow
async def test_holo_during_a_clip(node, board, dut_node_id, request):
    clip = request.config.getoption("--clip")
    minutes = request.config.getoption("--holo-minutes")
    rng = random.Random(20261120)
    listen = Listen(node, dut_node_id)
    beats = Beats(node, dut_node_id)
    pub = setpoints(node)
    loop = asyncio.get_running_loop()
    samples: list[dict] = []
    calls = refused = sent = 0
    # The ring at 30 % at most while the lights change (the board's own brightness if lower), and put back after
    brightness = int(re.search(r"; brightness (\d+)%", board.command("leds")).group(1))
    try:
        board.command(f"leds bright {min(brightness, 30)}")
        # The internal heap at rest, the clip not started, once the ring's pattern task exists: it is made at the
        # ring's first animation and kept (3 KB of stack), the one thing the run would otherwise keep for the first
        # time, which a full suite has made long before and a run alone after a restart hasn't
        board.command("leds pulse")
        await asyncio.sleep(0.3)
        board.command("leds off")
        await asyncio.sleep(0.5)
        rest_before = board.cyphal()["internal_free"]
        out = board.command(f"video play {clip} loop")
        assert "Unrecognized" not in out and "error" not in out.lower(), out
        await asyncio.sleep(2.0)
        start = board.cyphal()
        samples.append(start)
        baseline = None
        first_beat = len(beats.times)
        t0 = loop.time()
        end = t0 + minutes * 60
        next_burst = t0 + 10.0
        next_sample = t0 + 60.0
        kinds = [("twitch", {"range": 60}), ("wag", {"count": 2}), ("nod", {}), ("scan", {"period_ms": 3000}),
                 ("circle", {"period_ms": 1600}), ("center", {}), ("move", {"x": 30, "y": -20}),
                 ("nudge", {"x": -10, "y": 10, "duration_ms": 200})]
        lights = [("pulse", {"period_ms": 1500}), ("blink", {"period_ms": 600}), ("flicker", {}),
                  ("on", {"colour": (255, 128, 0)}), ("on", {"level": 50, "time_limit_ms": 2000}),
                  ("leia", {"time_limit_ms": 3000})]
        i = 0
        while loop.time() < end:
            if loop.time() >= next_burst:
                # 3 s of setpoints at 50 a second
                b0 = loop.time()
                for k in range(150):
                    t = k * 0.02
                    assert await pub.publish(Axes(x=int(7000 * math.sin(t * 2.5)), y=int(5000 * math.cos(t * 2.5))))
                    sent += 1
                    await asyncio.sleep(max(0.0, b0 + (k + 1) * 0.02 - loop.time()))
                next_burst = loop.time() + 20.0
                continue
            if loop.time() >= next_sample:
                samples.append(board.cyphal())
                next_sample += 60.0
            kind, fields = kinds[i % len(kinds)]
            r = await motion(node, dut_node_id, kind, **fields)
            calls += 1
            refused += r.code != OK
            if i % 3 == 0:
                mode, lf = lights[(i // 3) % len(lights)]
                lr = await light(node, dut_node_id, mode, **lf)
                calls += 1
                refused += lr.result.code != OK
            i += 1
            if i == 3 * len(lights) and baseline is None:
                baseline = len(samples)     # every motion and light has run once: the next sample is the measure
            await asyncio.sleep(rng.uniform(2.0, 4.0))
        await asyncio.sleep(1.5)
        after = board.cyphal()
        samples.append(after)
        shown = video(board)
    finally:
        pub.close()
        listen.close()
        beats.close()
        board.command("video stop")
        board.command("holo off")
        board.command("leds off")
        board.command(f"leds bright {brightness}")
    await asyncio.sleep(1.0)
    rest_after = board.cyphal()["internal_free"]
    between = gaps(beats.times[first_beat:])
    status_gaps = gaps(listen.status_stamps)
    free = [s["internal_free"] for s in samples]
    lowest = [s["internal_lowest"] for s in samples]
    msgs = lambda st: st["application"]["messages"]
    print(f"\n{minutes:g} min: {calls} calls ({refused} refused), {sent} setpoints ({msgs(after) - msgs(start)} "
          f"reached the application); {len(between) + 1} heartbeats, largest gap {max(between):.3f} s; {len(listen.status)} "
          f"Status, largest gap {max(status_gaps):.3f} s; {len(listen.axes)} Axes\ninternal heap free by minute: {free}\n"
          f"lowest by minute: {lowest}\nat rest: {rest_before} before, {rest_after} after\n"
          f"libcanard heap at most {after['heap_peak']}; node stack never used "
          f"{after['stack_unused']}\n{shown['text']}\nstart:\n{start['text']}\nend:\n{after['text']}")
    assert refused == 0
    assert msgs(after) - msgs(start) == sent, "a setpoint didn't reach the application"
    assert max(between) < 1.5, f"a heartbeat {max(between):.3f} s after the one before"
    assert max(status_gaps) < 1.3, f"a Status {max(status_gaps):.3f} s after the one before"
    assert after["dropped"] == start["dropped"] and after["not_read"] == start["not_read"]
    assert not after["heap_ran_out"]
    assert shown["late"] == 0, shown["text"]
    assert shown["fps"] >= 29.5, shown["text"]
    # Flat. Nothing kept: at rest after the run the heap is where it was at rest before (the events store keeps the
    # clip's start and end, a few hundred bytes). And the lowest: while the clip plays, its JPEG decoder takes
    # DECODER_BYTES of internal RAM for every frame and gives it back (the free heap reads about 50 KB between frames,
    # 41 KB during one), so anything else that allocates during a frame (a request's work, a console command, a
    # Wi-Fi packet) can set a new lowest at any time: steps of 0.3 to 3.9 KB were seen well after the first minute
    # (2026-11-21), the same before and after the changes under test, with nothing kept. So the lowest stays within
    # one more frame's decoder of where it was once every motion and light had run (the first minute of the
    # 10-minute run; later in a 3-minute one), rather than to the byte
    assert rest_after >= rest_before - 1024, f"at rest the internal heap was {rest_before} before and {rest_after} after"
    base = min(baseline if baseline is not None else len(lowest) - 1, len(lowest) - 1)
    assert lowest[-1] >= lowest[base] - DECODER_BYTES, \
        f"the internal heap's lowest went from {lowest[base]} to {lowest[-1]}"
