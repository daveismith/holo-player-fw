"""Test 5.1: the lists over Cyphal against the HTTP API's (manual/reference/cyphal.md): r2.holo.ListScenes against
GET /api/v1/scenes, uavcan.file.List and GetInfo against GET /api/v1/media (and the console's `fs ls`), r2.holo.MediaInfo
against GET /api/v1/media/info. The same names, slots, sizes and details; an index past the end; a missing path; a path
refused; List unanswered (not answered empty) while the board's queue is full. Each call is timed from the computer:
ListScenes runs on the node's worker, the file services and MediaInfo on its second worker (they wait for the volume,
which an upload holds). The HTTP comparisons need --board-host."""
from __future__ import annotations

import asyncio
import statistics
import time

import pytest
import r2.holo.ListScenes_0_1 as ListScenes
import r2.holo.MediaInfo_0_1 as MediaInfo
import uavcan.file.Error_1_0 as FileError
import uavcan.file.GetInfo_0_2 as GetInfo
import uavcan.file.List_0_2 as FileList
import uavcan.file.Path_2_0 as Path
import uavcan.node.GetInfo_1_0 as NodeInfo

from .conftest import call
from .holo_ports import BAD_REQUEST, LIST_SCENES, MEDIA_INFO, NOT_FOUND, NOT_PLAYABLE, OK, message, text
from .test_holo_scenes import test_scenes  # noqa: F401  (the fixture)

KINDS = ["none", "clip", "image", "animation"]
FORMATS = ["none", "mov", "png", "jpeg", "gif"]


async def timed(coro) -> tuple[object, float]:
    t0 = time.monotonic()
    r = await coro
    return r, time.monotonic() - t0


async def list_scenes(node, server: int, index: int):
    return await call(node, ListScenes, server, ListScenes.Request(index=index), 2.0, LIST_SCENES)


async def file_list(node, server: int, directory: str, index: int) -> str:
    r = await call(node, FileList, server, FileList.Request(entry_index=index, directory_path=Path(path=directory.encode())),
                   2.0)
    return text(r.entry_base_name.path)


async def file_info(node, server: int, path: str | bytes):
    return await call(node, GetInfo, server, GetInfo.Request(path=Path(path=path.encode() if isinstance(path, str) else path)),
                      2.0)


async def media_info(node, server: int, path: str | bytes, timeout: float = 5.0):
    return await call(node, MediaInfo, server,
                      MediaInfo.Request(path=Path(path=path.encode() if isinstance(path, str) else path)), timeout, MEDIA_INFO)


async def baseline(node, server: int, n: int = 20) -> float:
    """The median round trip of uavcan.node.GetInfo, which the node's own task answers at once: the bus and the
    adapter's share of every call's time"""
    times = []
    for _ in range(n):
        _, dt = await timed(call(node, NodeInfo, server, NodeInfo.Request(), 1.0))
        times.append(dt)
    return statistics.median(times)


async def test_list_scenes(node, board, dut_node_id, test_scenes, need_http):  # noqa: F811
    """ListScenes from 0 until the empty name: the same names and slots as GET /scenes, in its order, and `count` its
    length; past the end, the empty name with the same count"""
    status, reply = need_http.call("GET", "/scenes")
    assert status == 200, reply
    want = [(s["name"], s.get("slot", 0)) for s in reply["scenes"]]
    base = await baseline(node, dut_node_id)
    got, times = [], []
    for index in range(20):
        r, dt = await timed(list_scenes(node, dut_node_id, index))
        times.append(dt)
        assert r.result.code == OK, message(r.result)
        assert r.count == len(want), (r.count, want)
        if not text(r.name):
            assert r.slot == 0
            break
        got.append((text(r.name), r.slot))
    print(f"\n{len(got)} scenes: {got}\nper call {min(times) * 1e3:.1f} to {max(times) * 1e3:.1f} ms (GetInfo's round "
          f"trip {base * 1e3:.1f} ms)")
    assert got == want
    r = await list_scenes(node, dut_node_id, 255)
    assert r.result.code == OK and r.count == len(want) and not text(r.name) and r.slot == 0


async def walk(node, server: int, directory: str, times: list) -> dict[str, bool]:
    """Every entry under `directory` by uavcan.file.List: path -> is a directory"""
    out: dict[str, bool] = {}
    for index in range(1000):
        name, dt = await timed(file_list(node, server, directory, index))
        times.append((directory, index, dt))
        if not name:
            break
        is_dir = name.endswith("/")
        path = directory + name.rstrip("/")
        out[path] = is_dir
        if is_dir:
            out.update(await walk(node, server, path + "/", times))
    return out


async def test_files_against_http(node, board, dut_node_id, need_http):
    """The tree by uavcan.file.List, each clip and image's size by GetInfo: the same paths and sizes as GET /media,
    whose walk is by name as List's order is (littlefs keeps a directory in name order, and List walks readdir() with
    nothing sorted); directories with "/" after them, a directory's GetInfo saying so; the root as "/" and as ""; List's
    time at the root's last index a few milliseconds more than at index 0"""
    status, reply = need_http.call("GET", "/media")
    assert status == 200, reply
    want = {i["path"]: i["size"] for i in reply["items"]}
    base = await baseline(node, dut_node_id)
    times: list = []
    tree = await walk(node, dut_node_id, "/", times)
    root = [p for p in tree if p.count("/") == 1]
    print(f"\n{len(tree)} entries, {len(root)} in the root, {sum(tree.values())} directories")
    # The root's names in byte order: littlefs's own, which readdir() and so List give unsorted
    names = [p[1:] for p in root]
    assert names == sorted(names)
    media = {}
    info_times = []
    for path, is_dir in tree.items():
        r, dt = await timed(file_info(node, dut_node_id, path + ("/" if is_dir else "")))
        info_times.append(dt)
        assert r.error.value == FileError.OK, (path, r.error.value)
        assert bool(r.is_file_not_directory) == (not is_dir) and r.is_readable and not r.is_writeable and not r.is_link
        if is_dir:
            assert r.size == 0
        elif path.lower().rsplit(".", 1)[-1] in ("mov", "png", "jpg", "jpeg", "gif"):
            media[path] = r.size
    assert media == want, {"only over Cyphal": set(media) - set(want), "only over HTTP": set(want) - set(media),
                           "sizes": {p: (media[p], want[p]) for p in set(media) & set(want) if media[p] != want[p]}}
    # The root, both ways, and a directory's trailing "/" optional
    assert await file_list(node, dut_node_id, "", 0) == await file_list(node, dut_node_id, "/", 0)
    first_dir = next((p for p, d in tree.items() if d), None)
    if first_dir is not None:
        assert await file_list(node, dut_node_id, first_dir, 0) == await file_list(node, dut_node_id, first_dir + "/", 0)
    r = await file_info(node, dut_node_id, "/")
    assert r.error.value == FileError.OK and not r.is_file_not_directory
    # The times: the root's every index, and GetInfo
    root_times = {i: dt for d, i, dt in times if d == "/"}
    last = max(root_times)
    print(f"List of the root ({last} entries): index 0 {root_times[0] * 1e3:.1f} ms, index {min(40, last)} "
          f"{root_times[min(40, last)] * 1e3:.1f} ms, median {statistics.median(root_times.values()) * 1e3:.1f} ms, "
          f"most {max(root_times.values()) * 1e3:.1f} ms; GetInfo median {statistics.median(info_times) * 1e3:.1f} ms, "
          f"most {max(info_times) * 1e3:.1f} ms; uavcan.node.GetInfo's round trip {base * 1e3:.1f} ms")
    # A call walks readdir() to its index (about 0.1 ms an entry on the board): the last of the root's few dozen costs a
    # few milliseconds more than the first, no more
    late = [dt for i, dt in root_times.items() if i >= last - 4]
    early = [dt for i, dt in root_times.items() if i < 5]
    assert statistics.median(late) < statistics.median(early) + 0.010, (early, late)


async def test_files_missing_and_refused(node, dut_node_id):
    """A missing path is NOT_FOUND; a path the board refuses INVALID_VALUE; List of a missing directory, of a file, or
    past the end gives the empty name"""
    r = await file_info(node, dut_node_id, "/no-such-file.mov")
    assert r.error.value == FileError.NOT_FOUND and r.size == 0 and not r.is_file_not_directory
    for bad in ("/../etc", "/a//b", "/a\\b", b"/a\x01b"):
        r = await file_info(node, dut_node_id, bad)
        assert r.error.value == FileError.INVALID_VALUE, (bad, r.error.value)
    assert await file_list(node, dut_node_id, "/no-such-dir/", 0) == ""
    assert await file_list(node, dut_node_id, "/../", 0) == ""
    first_file = await file_list(node, dut_node_id, "/", 0)
    index = 0
    while first_file.endswith("/"):
        index += 1
        first_file = await file_list(node, dut_node_id, "/", index)
    assert first_file
    assert await file_list(node, dut_node_id, "/" + first_file, 0) == ""
    assert await file_list(node, dut_node_id, "/", 100000) == ""


def close_enough(a: float, b: float) -> bool:
    """A float16 against the JSON's float: within float16's precision"""
    return abs(a - b) <= max(0.01, abs(b) * 0.001)


async def test_media_info_against_http(node, dut_node_id, need_http):
    """MediaInfo for every clip and image GET /media lists, against GET /media/info: kind, format, size, frames, rate
    and length; playable or why not (NOT_PLAYABLE with the HTTP API's words); then a missing file, a directory, a bad
    path, and paths of 160 and 255 bytes (too long, in the board's words, not the control-character message)"""
    http = need_http
    status, reply = http.call("GET", "/media")
    assert status == 200, reply
    times = []
    for item in reply["items"]:
        path = item["path"]
        r, dt = await timed(media_info(node, dut_node_id, path))
        times.append((dt, path))
        status, j = http.call("GET", "/media/info", query={"path": path})
        if status == 422:       # not a clip or an image after all: described as nothing
            assert r.result.code == NOT_PLAYABLE and r.kind == 0 and r.width == 0, (path, r)
            assert message(r.result) == j["message"][:64], (message(r.result), j)
            continue
        assert status == 200, (path, status, j)
        assert KINDS[r.kind] == j["kind"] and FORMATS[r.format_] == j["format"], (path, r, j)
        assert (r.width, r.height) == (j["width"], j["height"]), (path, r, j)
        if j["kind"] == "image":
            assert r.frames == 1 and float(r.fps) == 0 and r.duration_ms == 0, (path, r)
        else:
            assert r.frames == j["frames"] and close_enough(float(r.fps), j["fps"]), (path, r, j)
            assert abs(r.duration_ms - j["duration_s"] * 1000) <= 1, (path, r, j)
        if j["playable"]:
            assert r.result.code == OK and not message(r.result), (path, message(r.result))
        else:
            assert r.result.code == NOT_PLAYABLE, (path, r.result)
            assert message(r.result) == f"{path}: {j['why']}"[:64], (message(r.result), j["why"])
    times.sort(reverse=True)
    print(f"\nMediaInfo for {len(times)} files: median {statistics.median(t for t, _ in times) * 1e3:.0f} ms; the "
          f"slowest {', '.join(f'{p} {t * 1e3:.0f} ms' for t, p in times[:4])}")
    r = await media_info(node, dut_node_id, "/no-such-file.mov")
    assert r.result.code == NOT_FOUND and message(r.result) == "no such file or directory: /no-such-file.mov"
    status, j = http.call("GET", "/media/info", query={"path": "/no-such-file.mov"})
    assert status == 404 and j["message"] == message(r.result)
    r = await media_info(node, dut_node_id, "/../a.png")
    status, j = http.call("GET", "/media/info", query={"path": "/../a.png"})
    assert r.result.code == BAD_REQUEST and status == 400 and message(r.result) == j["message"][:64]
    r = await media_info(node, dut_node_id, "")
    assert r.result.code == BAD_REQUEST and "give `path`" in message(r.result)
    r = await media_info(node, dut_node_id, "/bench")
    assert r.result.code == NOT_PLAYABLE and r.kind == 0, r
    for n in (160, 230, 255):           # past the routes' 159, and past FS_ABS_MAX (224), to the type's 255
        path = "/" + "a" * (n - 1)
        r = await media_info(node, dut_node_id, path)
        status, j = http.call("GET", "/media/info", query={"path": path})
        assert r.result.code == BAD_REQUEST and status == 400 and message(r.result) == j["message"][:64], \
            (n, message(r.result), j)
        assert "control characters" not in message(r.result), message(r.result)
        r = await file_info(node, dut_node_id, path)
        assert r.error.value == FileError.INVALID_VALUE, (n, r.error.value)


def largest_gif(http) -> tuple[str, int]:
    """The largest GIF GET /media lists, and its size: skipped if there is none"""
    status, reply = http.call("GET", "/media")
    assert status == 200, reply
    gifs = sorted(((i["size"], i["path"]) for i in reply["items"] if i["path"].lower().endswith(".gif")), reverse=True)
    if not gifs:
        pytest.skip("no GIF on the board to keep the second worker busy")
    return gifs[0][1], gifs[0][0]


async def test_lists_meanwhile(node, dut_node_id, need_http):
    """ListScenes while MediaInfo probes the largest GIF on the second worker: it doesn't wait for the probe (it runs on
    the worker); a GetInfo, on the second worker too, is answered once the probe is done"""
    gif, size = largest_gif(need_http)
    t0 = time.monotonic()
    probe = asyncio.ensure_future(media_info(node, dut_node_id, gif, timeout=10.0))
    await asyncio.sleep(0.02)
    times = []
    for _ in range(5):
        r, dt = await timed(list_scenes(node, dut_node_id, 0))
        assert r.result.code == OK
        times.append(dt)
    info, info_s = await timed(file_info(node, dut_node_id, gif))
    r = await probe
    probe_s = time.monotonic() - t0
    print(f"\nMediaInfo of {gif} ({size // 1024} KB, {r.frames} frames) took {probe_s * 1e3:.0f} ms; meanwhile ListScenes "
          f"{min(times) * 1e3:.1f} to {max(times) * 1e3:.1f} ms, and GetInfo {info_s * 1e3:.0f} ms")
    assert r.result.code in (OK, NOT_PLAYABLE)
    assert info.error.value == FileError.OK and info.size == size
    assert max(times) < max(0.05, probe_s / 2), (times, probe_s)


async def test_file_list_busy(node, board, dut_node_id, need_http):
    """With the second worker's two places held (two MediaInfo probes of the largest GIF), a List isn't answered at
    all, rather than with the empty name that ends a listing, and a GetInfo is answered IO_ERROR (try again); each
    counts as busy. Asked again afterwards, both are answered"""
    gif, _ = largest_gif(need_http)
    busy = board.cyphal()["services"]["busy"]
    probes = [asyncio.ensure_future(media_info(node, dut_node_id, gif, timeout=10.0)) for _ in range(2)]
    await asyncio.sleep(0.03)
    client = node.make_client(FileList, dut_node_id)
    client.response_timeout = 0.5
    try:
        listed = asyncio.ensure_future(client.call(FileList.Request(entry_index=0, directory_path=Path(path=b"/"))))
        info = await file_info(node, dut_node_id, gif)      # at once, while both places are still held
        unanswered = await listed
    finally:
        client.close()
    for p in probes:
        assert (await p).result.code in (OK, NOT_PLAYABLE)
    print(f"\nList while busy: {unanswered}; GetInfo while busy: error {info.error.value}")
    assert unanswered is None, "a List with the queue full was answered"
    assert info.error.value == FileError.IO_ERROR, info.error.value
    assert board.cyphal()["services"]["busy"] >= busy + 2
    assert await file_list(node, dut_node_id, "/", 0)
    assert (await file_info(node, dut_node_id, gif)).error.value == FileError.OK
