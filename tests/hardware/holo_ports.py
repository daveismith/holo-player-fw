"""The holo's own Cyphal ports, for the tests (manual/reference/cyphal.md): r2.holo.Motion, Light, Scene, Screen and
Ring called, Status, Axes and Event received, setpoints sent, and the board's own view of the same through its
console. Imported by test_holo.py, test_holo_load.py and test_holo_scenes.py; conftest.py has compiled the DSDL by
then, and has the console, the calls, the heartbeats and, with --board-host, the HTTP API (its `http` fixture)."""
from __future__ import annotations

import asyncio
import re

import pycyphal.application
import pycyphal.transport
import r2.holo.Axes_0_1 as Axes
import r2.holo.Colour_0_1 as Colour
import r2.holo.Event_0_1 as Event
import r2.holo.Light_0_1 as Light
import r2.holo.Motion_0_1 as Motion
import r2.holo.MotionSpec_0_1 as MotionSpec
import r2.holo.Ring_0_1 as Ring
import r2.holo.Scene_0_1 as Scene
import r2.holo.Screen_0_1 as Screen
import r2.holo.Status_0_1 as Status
import uavcan.file.Path_2_0 as Path

from .conftest import call

# The default ports (r2_cyphal_dsdl's ports/holo.csv)
STATUS, EVENT, AXES, SETPOINT, MOTION, LIGHT, SCENE, SCREEN, RING_PORT = 2300, 2301, 2302, 2303, 100, 101, 102, 103, 104
NEVER = 0xFFFFFFFF
NONE = -32768
KINDS = ["center", "move", "nudge", "twitch", "wag", "nod", "scan", "circle", "stop", "off"]
MOTIONS = ["hold", "move", "twitch", "wag", "nod", "scan", "circle"]        # state.Holo's numbers
RING = ["off", "solid", "wipe", "rainbow", "flicker", "pulse", "blink"]       # state.RingSetting's
OK, BAD_REQUEST, NOT_FOUND, NOT_PLAYABLE, NOT_READY, BUSY, FAILED = range(7)
LIGHT_MODES = {"off": 0, "on": 1, "pulse": 2, "flicker": 3, "blink": 4, "leia": 5}


def spec(kind: str | int, **fields) -> MotionSpec:
    """A MotionSpec: the kind by name (or a number, for one that isn't a kind), `duration_ms` as a number or
    None for empty, every other field as MotionSpec has it (0 for the default)"""
    duration = fields.pop("duration_ms", None)
    if "range" in fields:
        fields["range_"] = fields.pop("range")     # pycyphal's name: `range` is Python's
    return MotionSpec(kind=KINDS.index(kind) if isinstance(kind, str) else kind,
                      duration_ms=[] if duration is None else [duration], **fields)


async def motion(node, server: int, kind: str | int, **fields):
    """r2.holo.Motion: the response's result (r2.holo.Error)"""
    return (await call(node, Motion, server, Motion.Request(motion=spec(kind, **fields)), 2.0, MOTION)).result


async def light(node, server: int, mode: str | int, *, period_ms: int = 0, time_limit_ms: int = 0,
                level: int | None = None, colour: tuple[int, int, int] | None = None):
    """r2.holo.Light: the response"""
    request = Light.Request(mode=LIGHT_MODES[mode] if isinstance(mode, str) else mode, period_ms=period_ms,
                            time_limit_ms=time_limit_ms, level=[] if level is None else [level],
                            colour=[] if colour is None else [Colour(r=colour[0], g=colour[1], b=colour[2])])
    return await call(node, Light, server, request, 2.0, LIGHT)


async def scene(node, server: int, *, slot: int = 0, name: str = "", end: bool = False, action: int | None = None,
                timeout: float = 5.0):
    """r2.holo.Scene: apply by slot, or by name with slot 0, or end. The response's result"""
    request = Scene.Request(action=action if action is not None else Scene.Request.END if end else Scene.Request.APPLY,
                            slot=slot, name=name.encode())
    return (await call(node, Scene, server, request, timeout, SCENE)).result


SHOWS = {"keep": 0, "file": 1, "colour": 2, "calibration": 3, "off": 4}


async def screen(node, server: int, show: str | int, *, path: str | bytes = "", loop: bool = False, loops: int = 0,
                 whole_frame: bool = False, colour: tuple[int, int, int] = (0, 0, 0), backlight: int | None = None,
                 timeout: float = 5.0):
    """r2.holo.Screen: the response's result"""
    request = Screen.Request(show=SHOWS[show] if isinstance(show, str) else show, loop=loop, loops=loops,
                             whole_frame=whole_frame, colour=Colour(r=colour[0], g=colour[1], b=colour[2]),
                             backlight=[] if backlight is None else [backlight],
                             path=Path(path=path.encode() if isinstance(path, str) else path))
    return (await call(node, Screen, server, request, timeout, SCREEN)).result


async def ring(node, server: int, *, mode: str | int | None = None, colour: tuple[int, int, int] | None = None,
               loop: bool | None = None, brightness: int = 0, period_ms: int = 0, timeout: float = 2.0):
    """r2.holo.Ring: only what is given. The response's result"""
    request = Ring.Request(mode=[] if mode is None else [RING.index(mode) if isinstance(mode, str) else mode],
                           colour=[] if colour is None else [Colour(r=colour[0], g=colour[1], b=colour[2])],
                           loop=[] if loop is None else [loop], brightness=brightness, period_ms=period_ms)
    return (await call(node, Ring, server, request, timeout, RING_PORT)).result


def text(elements) -> str:
    """A DSDL byte array (a name, a path, a message) as text"""
    return bytes(elements).decode()


def message(error) -> str:
    return text(error.message)


def setpoints(node: pycyphal.application.Node):
    """A publisher of setpoints (r2.holo.Axes on the subscribed port), at the priority the board gives the port;
    close it after"""
    pub = node.make_publisher(Axes, SETPOINT)
    pub.priority = pycyphal.transport.Priority.FAST
    return pub


class Listen:
    """Every Status, Axes and Event from the board, as they arrive, until close(): (time, message), on asyncio's
    clock, as the event loop got them. And each one's own stamp (status_stamps, axes_stamps, event_stamps), which
    pycyphal gives a gs_usb adapter's frame from the adapter's clock: for the gaps between them, which a console
    command holding the event loop would otherwise stretch"""

    def __init__(self, node: pycyphal.application.Node, server: int):
        self.server = server
        self.status: list[tuple[float, Status]] = []
        self.axes: list[tuple[float, Axes]] = []
        self.events: list[tuple[float, Event]] = []
        self.status_stamps: list[float] = []
        self.axes_stamps: list[float] = []
        self.event_stamps: list[float] = []
        self._subs = [(node.make_subscriber(Status, STATUS), self.status, self.status_stamps),
                      (node.make_subscriber(Axes, AXES), self.axes, self.axes_stamps),
                      (node.make_subscriber(Event, EVENT), self.events, self.event_stamps)]
        for sub, into, stamps in self._subs:
            sub.receive_in_background(self._keep(into, stamps))

    def _keep(self, into: list, stamps: list):
        async def keep(msg, transfer) -> None:
            if transfer.source_node_id == self.server:
                into.append((asyncio.get_running_loop().time(), msg))
                stamps.append(float(transfer.timestamp.monotonic))
        return keep

    def close(self) -> None:
        for sub, _, _ in self._subs:
            sub.close()

    def mark(self) -> tuple[int, int]:
        return len(self.status), len(self.axes)

    @staticmethod
    async def _next(got: list, after: int | None, timeout: float, where, what: str):
        """The first message in `got` after the mark `after` (now, if None) for which where() holds"""
        start = len(got) if after is None else after
        loop = asyncio.get_running_loop()
        end = loop.time() + timeout
        while True:
            for _, m in got[start:]:
                if where(m):
                    return m
            start = len(got)
            if loop.time() > end:
                raise AssertionError(f"no such {what} in {timeout} s; the last was:\n{got[-1][1] if got else None}")
            await asyncio.sleep(0.02)

    async def next_status(self, after: int | None = None, timeout: float = 2.0, where=lambda s: True) -> Status:
        return await self._next(self.status, after, timeout, where, "Status")

    async def next_axes(self, after: int | None = None, timeout: float = 2.0, where=lambda a: True) -> Axes:
        return await self._next(self.axes, after, timeout, where, "Axes")

    async def next_event(self, after: int | None = None, timeout: float = 2.0, where=lambda e: True) -> Event:
        return await self._next(self.events, after, timeout, where, "Event")


def same_spec(a: MotionSpec, b: MotionSpec) -> bool:
    return (a.kind, a.range_, a.count, a.x, a.y, a.period_ms, a.interval_min_ms, a.interval_max_ms, a.time_limit_ms,
            list(a.duration_ms)) == (b.kind, b.range_, b.count, b.x, b.y, b.period_ms, b.interval_min_ms,
                                     b.interval_max_ms, b.time_limit_ms, list(b.duration_ms))


def spec_text(m: MotionSpec) -> str:
    return (f"{KINDS[m.kind] if m.kind < len(KINDS) else m.kind} range {m.range_} count {m.count} x {m.x} y {m.y} "
            f"period {m.period_ms} interval {m.interval_min_ms}-{m.interval_max_ms} time {m.time_limit_ms} "
            f"duration {list(m.duration_ms)}")


def holo_console(board) -> dict:
    """The console's `holo`: motion, x and y (None before anything has moved it), and each axis's line"""
    text = board.command("holo")
    m = re.search(r"motion (\w+)(?:, at x ([+-]\d+)% y ([+-]\d+)%)?", text)
    assert m, text
    return {"text": text, "motion": m.group(1), "x": int(m.group(2)) if m.group(2) else None,
            "y": int(m.group(3)) if m.group(3) else None, "limp": text.count(", limp")}


def motion_json(kind: str, **fields) -> dict:
    """The HTTP API's body for the same motion as spec(kind, **fields): only the fields given"""
    body: dict = {"motion": kind}
    for key in ("x", "y", "range", "count", "period_ms", "duration_ms"):
        if fields.get(key) is not None and (fields[key] != 0 or key in ("x", "y", "duration_ms")):
            body[key] = fields[key]
    if fields.get("interval_min_ms") or fields.get("interval_max_ms"):
        body["interval_s"] = [fields.get("interval_min_ms", 0) / 1000, fields.get("interval_max_ms", 0) / 1000]
    if fields.get("time_limit_ms"):
        body["time_s"] = fields["time_limit_ms"] / 1000
    return body


HAPPENINGS = ["clip_started", "clip_ended", "scene_started", "scene_ended", "screen_touch"]
THEN = ["stay", "restore", "off"]
BY = ["clip", "time", "end", "replaced"]


def event_kind(e: Event) -> str:
    """Which of r2.holo.events.Happening an Event holds, by its JSON name (screen_touch is the JSON's `touch`)"""
    for kind in HAPPENINGS:
        if getattr(e.what, kind) is not None:
            return "touch" if kind == "screen_touch" else kind
    raise AssertionError(f"an Event with nothing in it: {e}")


def event_fields(e: Event) -> dict:
    """An Event as the JSON event it carries would be, field for field (fps to two places, as the JSON's)"""
    kind = event_kind(e)
    out: dict = {"event": kind, "seq": e.seq}
    if kind == "clip_started":
        c = e.what.clip_started
        out.update(path=text(c.path.path), plays=c.plays, frames=c.frames, fps=round(float(c.fps), 1))
    elif kind == "clip_ended":
        c = e.what.clip_ended
        out.update(path=text(c.path.path), finished=bool(c.finished))
    elif kind == "scene_started":
        c = e.what.scene_started
        out.update(name=text(c.name), slot=c.slot, then=THEN[c.then], until_clip_ends=bool(c.until_clip_ends),
                   duration_ms=c.duration_ms)
    elif kind == "scene_ended":
        c = e.what.scene_ended
        out.update(name=text(c.name), slot=c.slot, then=THEN[c.then], by=BY[c.by])
    else:
        c = e.what.screen_touch
        out.update(action=["down", "up"][c.action], x=c.x, y=c.y)
    return out


def json_fields(j: dict) -> dict:
    """A JSON event (GET /api/v1/events) as event_fields() gives an Event: what the Event carries of it"""
    kind = j["event"]
    out: dict = {"event": kind, "seq": j["seq"]}
    if kind == "clip_started":
        out.update(path=j["path"], plays=j["plays"], frames=j["frames"], fps=round(j["fps"], 1))
    elif kind == "clip_ended":
        out.update(path=j["path"], finished=j["finished"])
    elif kind == "scene_started":
        out.update(name=j["name"], slot=j.get("slot", 0), then=j["then"], until_clip_ends=j["until_clip_ends"],
                   duration_ms=round(j.get("duration_s", 0) * 1000))
    elif kind == "scene_ended":
        out.update(name=j["name"], slot=j.get("slot", 0), then=j["then"], by=j["by"])
    else:
        out.update(action=j["action"], x=j["x"], y=j["y"])
    return out


def screen_line(board) -> str:
    """The console's `screen`: what shows"""
    return board.first_line("screen")


def ring_brightness(board) -> int:
    """The ring's brightness now, from the console's `leds`"""
    m = re.search(r"; brightness (\d+)%", board.first_line("leds"))
    assert m, board.first_line("leds")
    return int(m.group(1))


def backlight_now(board) -> int:
    """The backlight from the console's `lcd` (on or asleep)"""
    m = re.search(r"backlight (\d+)%", board.first_line("lcd"))
    assert m, board.first_line("lcd")
    return int(m.group(1))
