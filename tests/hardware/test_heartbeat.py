"""Test 3.2: uavcan.node.Heartbeat once a second from the fixed node-ID (manual/use/cyphal.md, "What it
reports")."""
from __future__ import annotations

import uavcan.node.Health_1_0 as Health
import uavcan.node.Mode_1_0 as Mode

from .conftest import gaps, heartbeats


async def test_heartbeat_for_30_s(node, dut_node_id):
    beats = await heartbeats(node, dut_node_id, 30.0)
    uptimes = [b[2].uptime for b in beats]
    between = gaps(beats)
    print(f"\n{len(beats)} heartbeats; uptime {uptimes[0] if uptimes else '-'} to {uptimes[-1] if uptimes else '-'}; "
          f"gaps {min(between):.3f} to {max(between):.3f} s" if len(beats) > 1 else "")
    assert 28 <= len(beats) <= 32, f"{len(beats)} heartbeats in 30 s"
    assert all(b - a == 1 for a, b in zip(uptimes, uptimes[1:])), f"uptime not rising by 1: {uptimes}"
    assert max(between) <= 1.5, f"a gap of {max(between):.3f} s"
    tids = [b[1] for b in beats]
    assert all((b - a) % 32 == 1 for a, b in zip(tids, tids[1:])), f"transfer-IDs not in step: {tids}"
    assert all(b[2].health.value == Health.NOMINAL for b in beats)
    assert all(b[2].mode.value == Mode.OPERATIONAL for b in beats)
    assert all(b[2].vendor_specific_status_code == 0 for b in beats)
