"""Tests 3.3 and 3.4: uavcan.node.GetInfo (manual/use/cyphal.md, "What it reports", "The unique ID")."""
from __future__ import annotations

import math

import pycyphal.dsdl
import uavcan.node.GetInfo_1_0 as GetInfo

from .conftest import expected_software_version


async def test_get_info(get_info, board, net):
    st = board.cyphal()
    version = board.firmware_version()
    hostname = board.hostname()
    before = net.frames_in()
    result = await get_info.call(GetInfo.Request())
    after = net.frames_in()
    assert result is not None, "no response"
    resp, _ = result
    name = bytes(resp.name).decode()
    uid = bytes(resp.unique_id)
    major, minor, vcs = expected_software_version(version)
    size = sum(len(b) for b in pycyphal.dsdl.serialize(resp))
    frames = math.ceil((size + 2) / 7)     # 7 bytes a frame after the tail byte, and the transfer CRC
    print(f"\nname {name}, unique id {uid.hex()}, software {resp.software_version.major}."
          f"{resp.software_version.minor} vcs {resp.software_vcs_revision_id:x} (firmware {version}), hardware "
          f"{resp.hardware_version.major}.{resp.hardware_version.minor}, protocol {resp.protocol_version.major}."
          f"{resp.protocol_version.minor}; {size} bytes, {frames} frames; {after - before} frames in meanwhile")
    assert name == st["name"]
    assert uid == st["unique_id"]
    # D4's layout: "holoplay", two zero bytes, the base MAC, whose last two bytes name the board
    assert uid[:8] == b"holoplay" and uid[8:10] == b"\0\0"
    assert hostname == f"holo-{uid[14:16].hex()}", hostname
    assert (resp.software_version.major, resp.software_version.minor) == (major, minor)
    assert resp.software_vcs_revision_id == vcs
    assert (resp.hardware_version.major, resp.hardware_version.minor) == (1, 0)
    assert (resp.protocol_version.major, resp.protocol_version.minor) == (1, 0)
    assert len(resp.software_image_crc) == 0 and len(resp.certificate_of_authenticity) == 0
    # Multi-frame, and pycyphal accepted its transfer CRC, or there would be no response
    assert frames > 1
    assert after - before >= frames


async def test_500_get_info_calls(get_info, board):
    before = board.cyphal()
    tids = []
    for i in range(500):
        result = await get_info.call(GetInfo.Request())
        assert result is not None, f"call {i} not answered"
        tids.append(result[1].transfer_id)
    after = board.cyphal()
    print(f"\nreceived {after['transfers_received'] - before['transfers_received']}, dropped "
          f"{after['dropped'] - before['dropped']}, not read {after['not_read']}; libcanard heap at most "
          f"{after['heap_peak']} bytes; stack {after['stack_unused']} bytes never used")
    assert all((b - a) % 32 == 1 for a, b in zip(tids, tids[1:])), "transfer-IDs out of step"
    assert after["transfers_received"] - before["transfers_received"] == 500
    assert after["dropped"] == before["dropped"]
    assert after["not_read"] == 0 and after["not_sent"] == 0
    assert not after["heap_ran_out"]
