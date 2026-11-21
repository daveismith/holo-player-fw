"""Tests 3.2 to 3.6: the holo's own ports (manual/reference/cyphal.md): r2.holo.Motion, every motion with its
defaults and with every parameter; the setpoint stream, a direct move; the refusals; r2.holo.Light on the LED
ring, every mode, a timed mode and a leia; and Motion's `off`, the release. Each is read back through Status
and Axes, the console's `holo` and `leds`, and, with --board-host, the same request over the HTTP API, which
must come out the same.

The bench's servos have no saved endpoints and this board moves them over their default travel
(manual/use/holo.md), so the holo is always ready here: a refusal for an uncalibrated or missing axis can't be
arranged without changing the saved calibration, which the tests don't touch."""
from __future__ import annotations

import asyncio
import math
import re

import pytest
import r2.holo.Axes_0_1 as Axes

from .conftest import access, natural16, open_checked, plain, until_state
from .holo_ports import (BAD_REQUEST, KINDS, MOTIONS, NEVER, NONE, OK, RING, Listen, holo_console, light, message,
                         motion, motion_json, same_spec, setpoints, spec, spec_text)


def ring_brightness(board) -> int:
    """The ring's brightness now, from the console's `leds`"""
    m = re.search(r"; brightness (\d+)%", board.first_line("leds"))
    assert m, board.first_line("leds")
    return int(m.group(1))


def scaled(brightness: int, level: int) -> int:
    """Status's brightness for a setting at `level` on a ring at `brightness`: the product, rounded"""
    return (brightness * level + 50) // 100


@pytest.fixture
async def listen(node, board, dut_node_id):
    """Status and Axes from the board, the holo at rest (its last motion asked a stop) and the ring off before and
    after, from the console. The ring stays at the board's own brightness (`brightness`, which the expected values
    are scaled by), so that a ring powered from the board is never driven harder by the tests than its owner set
    it; it is put back after, should a test have changed it"""
    brightness = ring_brightness(board)
    board.command("holo stop")
    board.command("leds off")
    l = Listen(node, dut_node_id)
    l.brightness = brightness
    yield l
    l.close()
    board.command("holo off")
    board.command("leds off")
    board.command(f"leds bright {brightness}")


# Hundredths of a percent: how far a position read back can be from the one asked for. The engine places an axis
# by 0.2 % of the travel each side (20), but the pulse goes in whole microseconds, coarser over these servos'
# travel (650 us on tilt: 31 a microsecond), and reading the pulse back against the endpoints truncates (20 more)
STEP = 60


def near(a, x: int, y: int, tol: int = STEP) -> bool:
    return abs(a.x - x) <= tol and abs(a.y - y) <= tol


def near_percent(shown: dict, end: tuple[int, int]) -> bool:
    """The console's position, in whole percent, at `end` give or take the percent the rounding can make"""
    return abs(shown["x"] - end[0]) <= 1 and abs(shown["y"] - end[1]) <= 1


async def settled(listen: Listen, timeout: float = 8.0, after: int | None = None):
    """The first Status in which nothing runs (after the mark `after`, or from now), and the Axes with the place it
    stopped at. The board sends that Axes within 100 ms of the stop if the place changed since the last, and the
    Status within 250 ms: so the first Axes after the Status if one comes within 0.3 s, or else the last before it"""
    s = await listen.next_status(after=after, timeout=timeout, where=lambda s: s.holo.motion == 0)
    since = len(listen.axes)
    try:
        return s, await listen.next_axes(after=since, timeout=0.3)
    except AssertionError:
        return s, listen.axes[-1][1] if listen.axes else await listen.next_axes(timeout=2.0)


async def place(node, dut_node_id, listen: Listen, x: int, y: int) -> None:
    """At (x, y) percent, at once"""
    r = await motion(node, dut_node_id, "move", x=x, y=y, duration_ms=0)
    assert r.code == OK, message(r)
    await listen.next_axes(timeout=2.0, where=lambda a: near(a, x * 100, y * 100))


# What each motion is asked for, and what comes of it: the motion Status shows while it runs, and where it ends
# (x, y in percent), or "ends" for one that ends by itself somewhere it chose, or None for one that runs until
# stopped. `from` puts the holo somewhere first (a nudge is relative)
MOTION_CASES = [
    ("center", {"from": (30, -30)}, "move", (0, 0)),
    ("center", {"duration_ms": 600, "from": (-40, 20)}, "move", (0, 0)),
    ("move", {"x": 40, "y": -30}, "move", (40, -30)),
    ("move", {"x": -50, "y": 20, "duration_ms": 0}, None, (-50, 20)),
    ("nudge", {"x": 10, "y": 10, "from": (-50, 20)}, "move", (-40, 30)),
    ("nudge", {"x": -150, "y": 200, "duration_ms": 300, "from": (20, -20)}, "move", (-100, 100)),
    ("twitch", {}, "twitch", None),
    ("twitch", {"range": 30, "interval_min_ms": 500, "interval_max_ms": 1500, "time_limit_ms": 2500}, "twitch", "ends"),
    ("wag", {}, "wag", "ends"),
    ("wag", {"range": 30, "count": 2, "period_ms": 600}, "wag", "ends"),
    ("nod", {}, "nod", "ends"),
    ("nod", {"range": 20, "count": 1, "period_ms": 500}, "nod", "ends"),
    ("scan", {}, "scan", None),
    pytest.param("scan", {"range": 40, "period_ms": 2000, "time_limit_ms": 2500}, "scan", "ends", marks=pytest.mark.xfail(
        strict=True, reason="the kit's holo engine never ends a timed scan (holo.c, tick_holo(): the time is looked at "
                            "only between segments, and a scan has none); the console and the HTTP API alike")),
    ("circle", {}, "circle", "ends"),
    ("circle", {"range": 30, "count": 2, "period_ms": 1200}, "circle", "ends"),
    ("stop", {}, None, "here"),
]


async def run_motion(how, node, dut_node_id, board, listen: Listen, http, kind: str, fields: dict, running, end):
    """One motion, over Cyphal (how "cyphal") or the HTTP API: the Status that shows it asked, the motion seen
    while it ran, and the end Status and Axes"""
    fields = dict(fields)
    start = fields.pop("from", None)
    want = spec(kind, **fields)
    latest = listen.status[-1][1] if listen.status else None
    if start is not None:
        await place(node, dut_node_id, listen, *start)
    elif (kind == "stop" if latest is None else same_spec(latest.holo.last, want) or latest.holo.motion != 0):
        # Something else asked last, and nothing running, so that a Status from before this request can't pass for
        # its own. Not needed when the last asked is already another (with no Status yet, the listen fixture's stop)
        other = "center" if kind == "stop" else "stop"
        assert (await motion(node, dut_node_id, other, duration_ms=0 if other == "center" else None)).code == OK
        await listen.next_status(timeout=8.0, where=lambda s: KINDS[s.holo.last.kind] == other and s.holo.motion == 0)
    mark = listen.mark()
    if how == "cyphal":
        r = await motion(node, dut_node_id, kind, **fields)
        assert r.code == OK, f"{kind} {fields}: {r.code} {message(r)}"
    else:
        status, reply = http.call("POST", "/holo/motion", body=motion_json(kind, **fields))
        assert status == 202, (status, reply)
    asked = await listen.next_status(after=mark[0], timeout=1.0, where=lambda s: same_spec(s.holo.last, want))
    asked_at = next(i for i in range(mark[0], len(listen.status)) if listen.status[i][1] is asked)
    shown = holo_console(board)
    if running is not None and end is None:
        # Until stopped: it runs, the console agrees, and nothing ends it
        assert MOTIONS[asked.holo.motion] == running, f"{kind}: Status shows {MOTIONS[asked.holo.motion]}"
        assert shown["motion"] == running, shown["text"]
        assert asked.holo.motion_remaining_ms == NEVER and asked.holo.motion_age_ms != NEVER
        await asyncio.sleep(1.0)
        assert holo_console(board)["motion"] == running
        seen = {MOTIONS[s.holo.motion] for _, s in listen.status[mark[0]:]}
        stopping = listen.mark()
        stop = await motion(node, dut_node_id, "stop")
        assert stop.code == OK
        s, a = await settled(listen, after=stopping[0])
        return asked, seen, s, a
    if fields.get("time_limit_ms"):
        assert MOTIONS[asked.holo.motion] == running
        assert 0 < asked.holo.motion_remaining_ms <= fields["time_limit_ms"], asked.holo.motion_remaining_ms
    s, a = await settled(listen, after=asked_at)
    seen = {MOTIONS[s.holo.motion] for _, s in listen.status[mark[0]:]}
    # A move that takes under half a second can come and go between two Status (four a second at most), so it
    # needn't be seen; anything longer must be
    brief = kind in ("center", "move", "nudge") and (fields.get("duration_ms") or 0) < 500
    if running is not None and not brief:
        assert running in seen, f"{kind}: Status never showed {running}, only {seen}"
    return asked, seen, s, a


def _motion_id(i: int, c) -> str:
    kind, fields = (c.values[0], c.values[1]) if hasattr(c, "values") else (c[0], c[1])
    return f"{kind}-{'defaults' if not {k for k in fields if k != 'from'} else 'all'}-{i}"


@pytest.mark.parametrize("kind,fields,running,end", MOTION_CASES, ids=[_motion_id(i, c) for i, c in enumerate(MOTION_CASES)])
async def test_motion(node, board, dut_node_id, listen, http, kind, fields, running, end):
    """3.2: each motion, with its defaults and with every parameter it takes: Status shows it asked (`last`, as
    asked) and running, the console's `holo` agrees, it ends where it should; and the same request over the HTTP
    API's route (--board-host) is asked and ends the same"""
    asked, seen, s, a = await run_motion("cyphal", node, dut_node_id, board, listen, http, kind, fields, running, end)
    shown = holo_console(board)
    print(f"\n{kind} {fields}: asked {spec_text(asked.holo.last)}; seen {sorted(seen)}; ended at Axes {a.x}, {a.y}; "
          f"console: motion {shown['motion']}, x {shown['x']} y {shown['y']}")
    assert s.holo.ready and shown["motion"] == "hold"
    if isinstance(end, tuple):
        # A nudge adds to a position read back, so it can be out by twice as much
        assert near(a, end[0] * 100, end[1] * 100, STEP * (2 if kind == "nudge" else 1)), (a.x, a.y)
        assert near_percent(shown, end) or kind == "nudge", shown["text"]
    else:
        assert a.x != NONE and abs(shown["x"] * 100 - a.x) <= 100 and abs(shown["y"] * 100 - a.y) <= 100
    if http is None:
        return
    asked2, seen2, s2, a2 = await run_motion("http", node, dut_node_id, board, listen, http, kind, fields, running, end)
    print(f"over HTTP: asked {spec_text(asked2.holo.last)}; seen {sorted(seen2)}; ended at Axes {a2.x}, {a2.y}")
    assert same_spec(asked.holo.last, asked2.holo.last)
    if running is not None and running in seen:
        assert running in seen2
    if isinstance(end, tuple):
        assert near(a2, a.x, a.y, 2 * STEP)
    status, reply = http.call("GET", "/holo")
    assert status == 200 and reply["motion"] == "hold" and reply["ready"], reply


async def test_motion_ignores_unused_fields(node, board, dut_node_id, listen):
    """A field the motion doesn't use is ignored, whatever it holds (where the route would refuse it)"""
    for kind, fields in [("center", {"period_ms": 100, "range": 127, "count": 31, "time_limit_ms": 50}),
                         ("wag", {"time_limit_ms": 50, "interval_min_ms": 5, "duration_ms": 65535, "count": 1,
                                  "period_ms": 300}),
                         ("stop", {"x": 32767, "period_ms": 1})]:
        mark = listen.mark()
        r = await motion(node, dut_node_id, kind, **fields)
        assert r.code == OK, f"{kind} {fields}: {message(r)}"
        await listen.next_status(after=mark[0], timeout=8.0,
                                 where=lambda s: KINDS[s.holo.last.kind] == kind and s.holo.motion == 0)


REFUSED = [
    ("wag", {"range": 101}, "`range` is 1..100"),
    ("nod", {"count": 21}, "`count` is 1..20"),
    ("circle", {"period_ms": 100}, "`period_ms` is 200..60000"),
    ("scan", {"period_ms": 60001}, "`period_ms` is 200..60000"),
    ("move", {"x": 101, "y": 0}, "`x` and `y` for move are -100..100"),
    ("nudge", {"x": 0, "y": -201}, "`x` and `y` for nudge are -200..200"),
    ("center", {"duration_ms": 10001}, "`duration_ms` is 0..10000"),
    ("twitch", {"interval_min_ms": 500}, "`interval_s` is [min, max], each 0.1..600, min first"),
    ("twitch", {"interval_max_ms": 500}, "`interval_s` is [min, max], each 0.1..600, min first"),
    ("twitch", {"interval_min_ms": 2000, "interval_max_ms": 1000}, "`interval_s` is [min, max], each 0.1..600, min first"),
    ("twitch", {"interval_min_ms": 50, "interval_max_ms": 1000}, "`interval_s` is [min, max], each 0.1..600, min first"),
    ("scan", {"time_limit_ms": 50}, "`time_s` is 0.1..3600"),
    ("twitch", {"time_limit_ms": 3600001}, "`time_s` is 0.1..3600"),
    (10, {}, "`motion` is center, move, nudge, twitch, wag, nod, scan, circle, stop or off"),
    (15, {}, "`motion` is center, move, nudge, twitch, wag, nod, scan, circle, stop or off"),
]


async def test_motion_refused(node, board, dut_node_id, listen, http):
    """3.4: each motion out of range, and a kind that is none: bad request, with the route's words; nothing moves and
    nothing is asked. The same request over HTTP (--board-host) is refused with the same words. The holo is placed
    once, and every Status and Axes from then on must show it there, still, with the placing move the last asked"""
    await place(node, dut_node_id, listen, 10, -10)
    placed = spec("move", x=10, y=-10, duration_ms=0)
    before = await listen.next_status(timeout=2.0, where=lambda s: same_spec(s.holo.last, placed) and s.holo.motion == 0)
    mark = listen.mark()
    wrong = []
    for kind, fields, words in REFUSED:
        r = await motion(node, dut_node_id, kind, **fields)
        print(f"{kind} {fields}: {r.code} {message(r)!r} reason {r.reason.why}/{r.reason.axis}")
        if not (r.code == BAD_REQUEST and message(r) == words[:64] and r.reason.why == 0 and r.reason.axis == 0):
            wrong.append(f"{kind} {fields}: {r.code} {message(r)!r} reason {r.reason.why}/{r.reason.axis}")
        if http is not None and isinstance(kind, str):
            status, reply = http.call("POST", "/holo/motion", body=motion_json(kind, **fields))
            if not (status == 400 and reply["message"] == words):
                wrong.append(f"over HTTP, {kind} {fields}: {status} {reply}")
    assert not wrong, "\n".join(wrong)        # every message is cut to 64 bytes over Cyphal
    await asyncio.sleep(0.4)                   # a motion started by any of them would show by now
    after = await listen.next_status(timeout=2.0)
    assert all(same_spec(s.holo.last, before.holo.last) and s.holo.motion == 0 for _, s in listen.status[mark[0]:])
    assert same_spec(after.holo.last, before.holo.last)
    assert all(near(a, 1000, -1000) for _, a in listen.axes[mark[1]:])


async def test_setpoints_stop_a_twitch(node, net, board, dut_node_id, listen):
    """3.3: setpoints at 50 a second while a twitch runs: the twitch stops, the axes follow each setpoint at
    once, Axes reports them ten times a second, NONE leaves an axis, beyond ±10000 is held, and nothing is
    dropped: every setpoint reaches the board's application, and the board drops no transfer"""
    r = await motion(node, dut_node_id, "twitch", range=80, interval_min_ms=100, interval_max_ms=200)
    assert r.code == OK
    await listen.next_status(timeout=1.0, where=lambda s: MOTIONS[s.holo.motion] == "twitch")
    before = board.cyphal()
    pub = setpoints(node)
    sent: list[tuple[float, int, int]] = []
    loop = asyncio.get_running_loop()
    mark = listen.mark()
    t0 = loop.time()
    period = 0.02
    n = 250                         # 5 s
    try:
        for i in range(n):
            t = i * period
            x = int(6000 * math.sin(2 * math.pi * t / 2.5))
            y = int(4000 * math.cos(2 * math.pi * t / 2.5))
            assert await pub.publish(Axes(x=x, y=y))
            sent.append((loop.time(), x, y))
            await asyncio.sleep(max(0.0, t0 + (i + 1) * period - loop.time()))
        await asyncio.sleep(0.3)
        # NONE leaves an axis; beyond the range is held
        last_x = sent[-1][1]
        assert await pub.publish(Axes(x=NONE, y=-2500))
        await listen.next_axes(timeout=1.5, where=lambda a: near(a, last_x, -2500))
        assert await pub.publish(Axes(x=20000, y=-20000))
        held = await listen.next_axes(timeout=1.5, where=lambda a: (a.x, a.y) == (10000, -10000))
    finally:
        pub.close()
    after = board.cyphal()
    status = await listen.next_status(timeout=1.5)
    shown = holo_console(board)
    axes = listen.axes[mark[1]:]
    during = [(t, a) for t, a in axes if sent[0][0] + 0.5 <= t <= sent[-1][0]]
    rate = (len(during) - 1) / (during[-1][0] - during[0][0]) if len(during) > 1 else 0
    # Each published position is one of the setpoints sent shortly before (to the step the holo places by)
    near_xy = near
    misses = 0
    for t, a in during:
        recent = [s for s in sent if t - 0.25 <= s[0] <= t]
        if not any(near_xy(a, s[1], s[2]) for s in recent):
            misses += 1
    print(f"\n{n} setpoints in {sent[-1][0] - sent[0][0]:.2f} s; {len(during)} Axes during them, {rate:.1f} a second, "
          f"{misses} not a recent setpoint; held at {held.x}, {held.y}; console at x {shown['x']} y {shown['y']}; "
          f"last asked {KINDS[status.holo.last.kind]}\nbefore:\n{before['text']}\nafter:\n{after['text']}")
    assert MOTIONS[status.holo.motion] == "hold" and KINDS[status.holo.last.kind] == "stop"
    assert (shown["x"], shown["y"]) == (100, -100)
    assert 8.0 <= rate <= 12.0, rate
    assert misses == 0
    assert after["dropped"] == before["dropped"], "the board dropped transfers"
    assert after["not_read"] == before["not_read"]
    # Every setpoint reached the application (two more: NONE, held, and nothing else)
    got = after["application"]["messages"] - before["application"]["messages"]
    assert got == n + 2, f"{got} of {n + 2} setpoints reached the application ({after['application']})"


async def test_setpoint_stops_a_move(node, board, dut_node_id, listen):
    """A setpoint stops whatever runs, even a move on its way: the latest wins"""
    r = await motion(node, dut_node_id, "move", x=-80, y=-80, duration_ms=5000)
    assert r.code == OK
    await listen.next_status(timeout=1.0, where=lambda s: MOTIONS[s.holo.motion] == "move")
    pub = setpoints(node)
    try:
        assert await pub.publish(Axes(x=3000, y=0))
    finally:
        pub.close()
    await listen.next_axes(timeout=1.0, where=lambda a: near(a, 3000, 0))
    s = await listen.next_status(timeout=1.0)
    assert MOTIONS[s.holo.motion] == "hold"
    await asyncio.sleep(1.0)
    assert near_percent(holo_console(board), (30, 0))


@pytest.mark.restarts
async def test_first_setpoint_after_boot(net, board, dut_node_id):
    """The first setpoint after the board starts goes through the engine as a move in whole percent, which places
    the axes, and then again the direct way: so it ends exactly where the same setpoint sent again ends (the same
    pulse, read back the same), not up to half a percent off. The board restarted for it: the adapter closed
    first, and opened again in the node's listen"""
    await net.close()
    board.restart()
    await until_state(board, "online", dut_node_id, timeout=15)
    assert "not moved since boot" in board.command("holo")
    await open_checked(net, board, dut_node_id)
    listen = Listen(net.node, dut_node_id)
    pub = setpoints(net.node)
    want = (2550, -1250)                # each half a percent from a whole one
    try:
        assert await pub.publish(Axes(x=want[0], y=want[1]))
        await listen.next_axes(timeout=2.0, where=lambda a: a.x != NONE)
        await asyncio.sleep(1.5)        # what was to go again has gone, and an Axes at rest came after
        first = listen.axes[-1][1]
        assert await pub.publish(Axes(x=want[0], y=want[1]))
        await asyncio.sleep(1.5)
        again = listen.axes[-1][1]
        s = await listen.next_status(timeout=1.5)
    finally:
        pub.close()
        listen.close()
        board.command("holo off")
    print(f"\nthe first setpoint {want} after a boot: Axes {first.x}, {first.y}; the same again: {again.x}, {again.y}; "
          f"last asked {KINDS[s.holo.last.kind]}")
    assert near(first, *want), (first.x, first.y)
    assert (first.x, first.y) == (again.x, again.y)
    assert KINDS[s.holo.last.kind] == "move" and MOTIONS[s.holo.motion] == "hold"


@pytest.mark.restarts
async def test_status_port_off(node, board, dut_node_id):
    """A published port turned off by its register (65535): from the node's next start nothing goes on it and its
    count stays 0, while Axes goes on at its rate; set back, Status goes again"""
    name = "uavcan.pub.status.id"
    try:
        assert plain((await access(node, dut_node_id, name, natural16(65535))).value) == 65535
        board.command("cyphal restart")
        await until_state(board, "online", dut_node_id)
        listen = Listen(node, dut_node_id)
        try:
            await asyncio.sleep(3.3)
            st = board.cyphal()
        finally:
            listen.close()
        print(f"\nstatus off: {len(listen.status)} Status, {len(listen.axes)} Axes in 3.3 s; {st['published']}")
        assert not listen.status and 2 <= len(listen.axes) <= 5
        assert st["published"]["status"]["sent"] == 0 and st["published"]["axes"]["sent"] >= 2
    finally:
        board.command(f"cyphal register {name} 2300")
        board.command("cyphal restart")
        await until_state(board, "online", dut_node_id)
    listen = Listen(node, dut_node_id)
    try:
        await listen.next_status(timeout=2.0)
    finally:
        listen.close()


async def test_axes_rate_at_rest(node, dut_node_id, listen):
    """Axes once a second at rest, Status once a second, both from the node's mailboxes"""
    await asyncio.sleep(0.5)
    mark = listen.mark()
    await asyncio.sleep(5.2)
    axes = listen.axes[mark[1]:]
    status = listen.status[mark[0]:]
    print(f"\nin 5.2 s at rest: {len(axes)} Axes, {len(status)} Status")
    assert 4 <= len(axes) <= 6 and 4 <= len(status) <= 6


# Each light mode with its defaults and with every parameter: the request, and what the ring is then set to
# (mode, colour, level, period). Status's brightness is the ring's brightness times the level
LIGHT_CASES = [
    ("on", {}, ("solid", "kept", 100, 0)),
    ("on", {"level": 40, "colour": (255, 128, 0)}, ("solid", (255, 128, 0), 40, 0)),
    ("pulse", {}, ("pulse", "kept", 100, 2000)),
    ("pulse", {"period_ms": 800, "level": 60, "colour": (0, 0, 255)}, ("pulse", (0, 0, 255), 60, 800)),
    ("flicker", {}, ("flicker", "kept", 100, 0)),
    ("flicker", {"level": 70, "colour": (0, 255, 0), "period_ms": 100}, ("flicker", (0, 255, 0), 70, 0)),
    ("blink", {}, ("blink", "kept", 100, 1000)),
    ("blink", {"period_ms": 400, "level": 0, "colour": (255, 0, 0)}, ("blink", (255, 0, 0), 0, 400)),
    ("off", {"level": 50}, ("off", "kept", 100, 0)),
]


@pytest.mark.parametrize("mode,fields,want", LIGHT_CASES, ids=[f"{c[0]}-{'all' if c[1] else 'defaults'}" for c in LIGHT_CASES])
async def test_light(node, board, dut_node_id, listen, http, mode, fields, want):
    """3.5: each mode of the holo's light, on the ring: Status's ring, the console's `leds` and the HTTP API's
    GET /leds (--board-host) say the same. Each starts from another mode at another level (50), so that what is
    seen is the request's doing, defaults included"""
    board.command("leds pulse 10,20,30 -l 50" if mode == "on" else "leds colour 10,20,30 -l 50")
    mark = listen.mark()
    r = await light(node, dut_node_id, mode, **fields)
    assert r.result.code == OK and not r.centred, message(r.result)
    ring_mode, colour, level, period = want
    colour = (10, 20, 30) if colour == "kept" else colour
    s = await listen.next_status(after=mark[0], timeout=1.0, where=lambda s: RING[s.ring.now.mode] == ring_mode
                                 and (s.ring.now.colour.r, s.ring.now.colour.g, s.ring.now.colour.b) == colour)
    now = s.ring.now
    shown = board.first_line("leds")
    print(f"\n{mode} {fields}: {RING[now.mode]} {now.colour.r},{now.colour.g},{now.colour.b} at {now.brightness}% "
          f"(the ring at {listen.brightness}%), period {now.period_ms}; console: {shown}")
    want_brightness = scaled(listen.brightness, level)
    assert (now.brightness, now.period_ms, s.ring.remaining_ms, s.ring.leia) == (want_brightness, period, NEVER, False)
    assert shown.startswith(ring_mode)
    # The level itself, whatever the brightness: the console shows one other than 100
    assert (f", at {level}%;" in shown) if level != 100 else (", at " not in shown), shown
    if ring_mode != "off":
        assert f"#{colour[0]:02x}{colour[1]:02x}{colour[2]:02x}" in shown
    if http is not None:
        status, reply = http.call("GET", "/leds")
        assert status == 200 and reply["mode"] == ring_mode and reply["timed"] is None and not reply["leia"]
        assert reply["period_ms"] == period and reply["level"] == level and reply["brightness"] == listen.brightness, reply


async def test_light_timed(node, board, dut_node_id, listen, http):
    """3.5: a timed light goes back to what the ring was doing; one timed over another goes back, in one step,
    to what the first would have gone back to; another light during a timed one ends the wait"""
    r = await light(node, dut_node_id, "pulse", colour=(0, 0, 255), period_ms=1000)
    assert r.result.code == OK
    await listen.next_status(timeout=1.0, where=lambda s: RING[s.ring.now.mode] == "pulse")
    r = await light(node, dut_node_id, "on", colour=(255, 0, 0), time_limit_ms=1500)
    assert r.result.code == OK
    loop = asyncio.get_running_loop()
    t0 = loop.time()
    s = await listen.next_status(timeout=1.0, where=lambda s: RING[s.ring.now.mode] == "solid")
    assert 0 < s.ring.remaining_ms <= 1500 and RING[s.ring.after.mode] == "pulse" and s.ring.after.period_ms == 1000
    assert "for 1." in board.first_line("leds") or "for 0." in board.first_line("leds")
    if http is not None:
        status, reply = http.call("GET", "/leds")
        assert reply["timed"] is not None and reply["timed"]["then"]["mode"] == "pulse", reply
    s = await listen.next_status(timeout=3.0, where=lambda s: RING[s.ring.now.mode] == "pulse")
    back = loop.time() - t0
    print(f"\nback to the pulse after {back:.2f} s")
    assert 1.3 <= back <= 2.1 and s.ring.remaining_ms == NEVER and s.ring.now.period_ms == 1000
    assert (s.ring.now.colour.r, s.ring.now.colour.g, s.ring.now.colour.b) == (0, 0, 255)
    # A chain: the second timed mode goes back to the pulse, not to the first
    await light(node, dut_node_id, "on", colour=(255, 0, 0), time_limit_ms=5000)
    await light(node, dut_node_id, "blink", time_limit_ms=1000)
    s = await listen.next_status(timeout=1.0, where=lambda s: RING[s.ring.now.mode] == "blink")
    assert RING[s.ring.after.mode] == "pulse"
    s = await listen.next_status(timeout=2.5, where=lambda s: RING[s.ring.now.mode] != "blink")
    assert RING[s.ring.now.mode] == "pulse" and s.ring.remaining_ms == NEVER
    # Another light during a timed one replaces it, for good
    await light(node, dut_node_id, "on", time_limit_ms=1000)
    await light(node, dut_node_id, "flicker", level=30)
    s = await listen.next_status(timeout=1.0, where=lambda s: RING[s.ring.now.mode] == "flicker")
    assert s.ring.remaining_ms == NEVER
    await asyncio.sleep(1.5)
    assert board.first_line("leds").startswith("flicker")


async def test_leia(node, board, dut_node_id, listen, http):
    """3.5: a leia: the ring flickers at full for its time, marked as a leia, and the holo turns to the centre;
    then the ring goes back. With no time, 30 s, until another light ends it"""
    await place(node, dut_node_id, listen, 50, 50)
    board.command("leds pulse 0,255,0 -l 30")
    mark = listen.mark()
    r = await light(node, dut_node_id, "leia", time_limit_ms=2000, level=10, period_ms=500, colour=(77, 166, 255))
    assert r.result.code == OK and r.centred and r.result.reason.why == 0 and message(r.result) == ""
    s = await listen.next_status(after=mark[0], timeout=1.0, where=lambda s: s.ring.leia)
    full, thirty = listen.brightness, scaled(listen.brightness, 30)
    assert RING[s.ring.now.mode] == "flicker" and s.ring.now.brightness == full
    assert (s.ring.now.colour.r, s.ring.now.colour.g, s.ring.now.colour.b) == (77, 166, 255)
    assert RING[s.ring.after.mode] == "pulse" and s.ring.after.brightness == thirty and 0 < s.ring.remaining_ms <= 2000
    assert "(a leia)" in board.first_line("leds")
    if http is not None:
        assert http.call("GET", "/leds")[1]["leia"] is True
    await listen.next_axes(after=mark[1], timeout=2.0, where=lambda a: near(a, 0, 0))
    s = await listen.next_status(timeout=3.0, where=lambda s: not s.ring.leia)
    assert RING[s.ring.now.mode] == "pulse" and s.ring.remaining_ms == NEVER and s.ring.now.brightness == thirty
    assert KINDS[s.holo.last.kind] == "center"
    # The default time, and another light ending it
    r = await light(node, dut_node_id, "leia")
    assert r.result.code == OK and r.centred
    s = await listen.next_status(timeout=1.0, where=lambda s: s.ring.leia)
    assert 29000 < s.ring.remaining_ms <= 30000
    await light(node, dut_node_id, "off")
    s = await listen.next_status(timeout=1.0, where=lambda s: not s.ring.leia)
    assert RING[s.ring.now.mode] == "off" and s.ring.remaining_ms == NEVER


LIGHT_REFUSED = [
    ("on", {"level": 101}, "`level` is 0..100"),
    ("pulse", {"period_ms": 100}, "`period_ms` is 200..60000"),
    ("blink", {"period_ms": 60001}, "`period_ms` is 200..60000"),
    ("flicker", {"time_limit_ms": 50}, "`time_s` is 0.1..3600"),
    ("leia", {"time_limit_ms": 3600001}, "`time_s` is 0.1..3600"),
    (6, {}, "`mode` is off, on, pulse, flicker, blink or leia"),
]


@pytest.mark.parametrize("mode,fields,words", LIGHT_REFUSED, ids=[f"{c[0]}-{'-'.join(c[1]) or 'mode'}" for c in LIGHT_REFUSED])
async def test_light_refused(node, board, dut_node_id, listen, mode, fields, words):
    """3.4: a light out of range: bad request, in the JSON API's words, and the ring as it was"""
    board.command("leds colour 10,20,30")
    before = board.first_line("leds")
    r = await light(node, dut_node_id, mode, **fields)
    print(f"\n{mode} {fields}: {r.result.code} {message(r.result)!r}")
    assert r.result.code == BAD_REQUEST and message(r.result) == words and not r.centred
    assert board.first_line("leds") == before


async def test_motion_off(node, board, dut_node_id, listen, http):
    """3.6: Motion `off`, the release: the motion stops, both servos go limp, and the light (the ring) goes off"""
    board.command("leds colour orange")
    r = await motion(node, dut_node_id, "wag", count=5)
    assert r.code == OK
    await listen.next_status(timeout=1.0, where=lambda s: MOTIONS[s.holo.motion] == "wag")
    r = await motion(node, dut_node_id, "off")
    assert r.code == OK
    s = await listen.next_status(timeout=1.0, where=lambda s: KINDS[s.holo.last.kind] == "off")
    shown = holo_console(board)
    servos = board.command("servo_list")
    print(f"\n{shown['text']}\n{servos}\n{board.first_line('leds')}")
    assert MOTIONS[s.holo.motion] == "hold" and RING[s.ring.now.mode] == "off"
    assert shown["limp"] == 2, shown["text"]
    assert board.first_line("leds").startswith("off")
    # The next motion drives them again
    r = await motion(node, dut_node_id, "center", duration_ms=0)
    assert r.code == OK
    await asyncio.sleep(0.3)
    assert holo_console(board)["limp"] == 0
