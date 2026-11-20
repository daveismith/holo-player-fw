"""Test 3.5: GetInfo and the heartbeat while the board is busy: a clip with the LED ring, and an update
written to flash over Wi-Fi (which stalls both cores for each write). A node is thought offline after
3 s without a heartbeat."""
from __future__ import annotations

import asyncio
import http.client
import re
import threading
import time
from pathlib import Path

import pytest
import uavcan.node.GetInfo_1_0 as GetInfo

from .conftest import REPO, gaps, heartbeats

OFFLINE_S = 3.0


async def calls_while(get_info, done) -> tuple[int, int]:
    """GetInfo every half second until done(): (calls, answered)"""
    calls = answered = 0
    while not done():
        calls += 1
        if await get_info.call(GetInfo.Request()) is not None:
            answered += 1
        await asyncio.sleep(0.5)
    return calls, answered


async def test_get_info_during_a_clip(node, get_info, board, dut_node_id, request):
    clip = request.config.getoption("--clip")
    try:
        out = board.command(f"video play {clip} loop")
        assert "Unrecognized" not in out and "error" not in out.lower(), out
        board.command("leds rainbow loop")
        watch = asyncio.create_task(heartbeats(node, dut_node_id, 20.0))
        calls, answered = await calls_while(get_info, watch.done)
        beats = await watch
        status = board.command("video status")
    finally:
        board.command("video stop")
        board.command("leds off")
    st = board.cyphal()
    between = gaps(beats)
    print(f"\n{answered} of {calls} GetInfo answered; {len(beats)} heartbeats, largest gap {max(between):.3f} s; "
          f"internal heap lowest {st['internal_lowest']}\n{status}")
    assert answered == calls
    assert max(between) < OFFLINE_S


@pytest.mark.ota
async def test_get_info_during_an_update(node, get_info, board, dut_node_id, request):
    host = request.config.getoption("--board-host")
    if not host:
        pytest.skip("--board-host not given")
    image = REPO / "build" / "holo-player-fw.bin"
    assert image.exists(), "build the firmware first"
    before = board.command("ota")
    result: dict = {}

    def upload():
        # Staged only: no ?activate, so the board boots what it booted before
        conn = http.client.HTTPConnection(host, 80, timeout=120)
        data = Path(image).read_bytes()
        t0 = time.monotonic()
        conn.request("PUT", "/api/v1/ota/image", body=data,
                     headers={"Content-Type": "application/octet-stream", "Content-Length": str(len(data))})
        reply = conn.getresponse()
        result.update(status=reply.status, body=reply.read().decode(errors="replace"), seconds=time.monotonic() - t0,
                      size=len(data))

    worker = threading.Thread(target=upload, daemon=True)
    worker.start()
    watch = asyncio.create_task(heartbeats(node, dut_node_id, 600.0, until=lambda: not worker.is_alive()))
    calls, answered = await calls_while(get_info, lambda: not worker.is_alive())
    beats = await watch
    after = board.command("ota")
    between = gaps(beats)
    print(f"\nupload: HTTP {result.get('status')} in {result.get('seconds', 0):.1f} s, {result.get('size')} bytes; "
          f"{answered} of {calls} GetInfo answered; {len(beats)} heartbeats, largest gap {max(between):.3f} s, "
          f"gaps over 1.5 s: {[round(g, 3) for g in between if g > 1.5]}\nbefore:\n{before}\nafter:\n{after}")
    assert result.get("status") == 200, result
    assert answered == calls
    assert max(between) < OFFLINE_S
    assert re.search(r"^ota_0 .*running, boots", after, re.M), "the board no longer boots the image it runs"
