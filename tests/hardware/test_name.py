"""Test 3.9: the node's name (D7): a new one reported after the node restarts, and after the board
restarts; a bad one refused with its reason, the old one kept."""
from __future__ import annotations

import pytest
import uavcan.node.GetInfo_1_0 as GetInfo

from .conftest import ensure_node, open_checked, until_state

DEFAULT = "r2.dome.holo.front"
NEW = "r2.dome.holo.top"
BAD = {
    '""': "1 to 50 characters, and this is 0",
    "abcdefghij.abcdefghij.abcdefghij.abcdefghij.abcdefg": "and this is 51",
    "R2.Dome": "`R` is not allowed",
    "r2..holo": "a part between dots is empty",
    "r2.1holo": "can't start with a digit: `1holo`",
}


async def name_reported(net, dut_node_id) -> str:
    client = net.node.make_client(GetInfo, dut_node_id)
    client.response_timeout = 1.0
    try:
        result = await client.call(GetInfo.Request())
    finally:
        client.close()
    assert result is not None, "GetInfo not answered"
    return bytes(result[0].name).decode()


async def test_bad_names_refused(board):
    before = board.cyphal()
    for name, reason in BAD.items():
        out = board.command(f"cyphal name {name}")
        print(f"\ncyphal name {name}: {out.strip()}")
        assert "not a name" in out and reason in out, out
    after = board.cyphal()
    assert after["name"] == before["name"]
    assert "from the next start" not in after["text"], "a bad name was saved"


@pytest.mark.restarts
async def test_new_name_after_restarts(node, net, board, dut_node_id):
    assert await name_reported(net, dut_node_id) == DEFAULT
    try:
        out = board.command(f"cyphal name {NEW}")
        assert NEW in out, out
        assert await name_reported(net, dut_node_id) == DEFAULT, "a name applies from the node's next start"
        board.command("cyphal restart")
        await until_state(board, "online", dut_node_id)
        assert await name_reported(net, dut_node_id) == NEW
        # The board's restart: the adapter closed first, so that nothing goes unacknowledged, and
        # opened again in the node's listen
        await net.close()
        board.restart()
        st = await until_state(board, "online", dut_node_id, timeout=15)
        assert st["name"] == NEW
        await open_checked(net, board, dut_node_id)
        assert await name_reported(net, dut_node_id) == NEW
    finally:
        board.command("cyphal name --reset")
        board.command("cyphal restart")
        await until_state(board, "online", dut_node_id)
    if net.node is None:
        ensure_node(board, dut_node_id, restart=False)
        await open_checked(net, board, dut_node_id)
    assert await name_reported(net, dut_node_id) == DEFAULT
