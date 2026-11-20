"""Test 3.8: the board alone on the bus for 10 minutes. Nobody acknowledges its frames, so it sits in
error-passive retrying one; the heartbeats behind it expire and are counted as dropped. Nothing else
may suffer: no bus-off, the heaps flat, the console and the web app answering."""
from __future__ import annotations

import asyncio
import time
import urllib.request

import pytest

MINUTES = 10


@pytest.mark.slow
async def test_alone_for_10_minutes(alone, board, request):
    host = request.config.getoption("--board-host")
    samples, web = [], []
    end = time.time() + MINUTES * 60
    while True:
        t0 = time.monotonic()
        st = board.cyphal()
        st["console_s"] = time.monotonic() - t0
        samples.append(st)
        if host:
            t0 = time.monotonic()
            with urllib.request.urlopen(f"http://{host}/api/v1/info", timeout=5) as reply:
                web.append((reply.status, time.monotonic() - t0))
        if time.time() >= end:
            break
        await asyncio.sleep(30)
    first, last = samples[0], samples[-1]
    used = [s["heap_used"] for s in samples]
    free = [s["internal_free"] for s in samples]
    print(f"\n{len(samples)} samples over {MINUTES} min: bus {sorted({s['bus'] for s in samples})}, tx errors "
          f"{sorted({s['tx_errors'] for s in samples})}, bus-off {last['bus_off']}; dropped {first['dropped']} -> "
          f"{last['dropped']}; libcanard heap {min(used)}-{max(used)} (at most {last['heap_peak']}); internal "
          f"free {min(free)}-{max(free)}, lowest {last['internal_lowest']}; console {max(s['console_s'] for s in samples):.2f} s "
          f"at worst; web {[w[0] for w in web][:3]}... {max((w[1] for w in web), default=0):.2f} s at worst")
    assert all(s["state"] == "online" for s in samples)
    assert all(s["bus"] in ("error-active", "error-warning", "error-passive") for s in samples)
    assert last["bus_off"] == first["bus_off"] == 0
    assert last["dropped"] > first["dropped"], "alone, the heartbeats should expire"
    assert max(used) - min(used) <= 256, "libcanard's heap grew"
    assert min(free) >= first["internal_free"] - 2048, "internal heap fell"
    assert max(s["console_s"] for s in samples) < 2
    if host:
        assert all(status == 200 for status, _ in web)
