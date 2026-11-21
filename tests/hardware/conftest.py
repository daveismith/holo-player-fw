"""
The Cyphal node's hardware tests (manual/use/cyphal.md): a board on a CAN bus with a computer's
gs_usb adapter (candleLight, CANable), and the board's console on USB.

    make can-setup && make can-test
    make can-test ARGS="-k get_info -x"
    make can-test ARGS="-m 'not slow'"
    make can-test CAN_PY=/other/venv/bin/python ARGS="--console-port /dev/cu.usbserial-1"

The board runs this firmware with `link mode can` (the session sets that, and restarts the board,
if it isn't). The session gives the node the fixed node-ID --dut-node-id over the console before it
opens the adapter.

Three rules from the bench, which the fixtures keep:

  * The board acknowledges before the computer sends. An adapter whose frames nobody acknowledges
    retries them, and a candleLight degrades and then wedges (it goes on acknowledging the bus but
    passes nothing to the computer). So the adapter is opened only once the node is running, and
    closed before anything restarts the board.
  * The adapter is opened and closed while the board sends nothing. Closing it while the board was
    sending wedged it on the bench, so every open and close happens just after `cyphal restart`,
    in the 2 s the node listens before it sends. The adapter is opened once a session, and closed
    and opened again only where a test must (a restart of the board, 10 minutes alone). So the
    node always has its fixed node-ID when the adapter opens or closes: the plug-and-play tests
    (test_pnp.py) switch it to plug-and-play once the adapter is open, and back once they have all
    run, since a node waiting for a node-ID sends requests and so has no quiet 2 s.
  * One program holds the adapter at a time: stop cyphal_service first.

Markers: `restarts` (restarts the node or the board; every test restarts the node at least as it
opens the adapter), `slow` (minutes), `ota` (writes the board's spare OTA slot over Wi-Fi: needs
--board-host; never activates it).

A board that restarts itself (uavcan.node.ExecuteCommand's restart and factory reset) does so with the
adapter open. The harness then sends nothing until the board acknowledges again: Net.muted() drops the
pycyphal node's frames (its heartbeat, its port list) at the adapter while the board is down.

The DSDL is two submodules, the revisions the firmware's C headers were generated from
(tools/gen_dsdl.py): public_regulated_data_types (`uavcan`) and r2_cyphal_dsdl (`r2`); pycyphal's
Python for them is compiled once into build/pycyphal-dsdl/, and again when either moves.
"""
from __future__ import annotations

import contextlib
import os
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DSDL = REPO / "external" / "public_regulated_data_types"
R2_DSDL = REPO / "external" / "r2_cyphal_dsdl"
DSDL_OUT = REPO / "build" / "pycyphal-dsdl"


def _compile_dsdl() -> None:
    """pycyphal's Python for the uavcan and r2 namespaces, compiled again when either revision moves.
    Before pycyphal.application is imported: it imports uavcan.node at load."""
    revision = " ".join(subprocess.run(["git", "-C", str(d), "rev-parse", "HEAD"], capture_output=True, text=True,
                                       check=True).stdout.strip() for d in (DSDL, R2_DSDL))
    stamp = DSDL_OUT / "revision"
    if not stamp.exists() or stamp.read_text().strip() != revision:
        import pycyphal.dsdl

        DSDL_OUT.mkdir(parents=True, exist_ok=True)
        pycyphal.dsdl.compile_all([DSDL / "uavcan", R2_DSDL / "r2"], DSDL_OUT)
        stamp.write_text(revision + "\n")
    if str(DSDL_OUT) not in sys.path:
        sys.path.insert(0, str(DSDL_OUT))


_compile_dsdl()

import asyncio  # noqa: E402
from typing import AsyncIterator  # noqa: E402

import pycyphal.application  # noqa: E402
import pycyphal.transport.can  # noqa: E402
import pycyphal.transport.can.media.pythoncan  # noqa: E402
import pytest  # noqa: E402
import usb.core  # noqa: E402

import uavcan.node.GetInfo_1_0 as GetInfo  # noqa: E402
import uavcan.node.Heartbeat_1_0 as Heartbeat  # noqa: E402
import uavcan.primitive.array.Natural16_1_0 as Natural16  # noqa: E402
import uavcan.primitive.array.Natural32_1_0 as Natural32  # noqa: E402
import uavcan.primitive.String_1_0 as String  # noqa: E402
import uavcan.register.Access_1_0 as Access  # noqa: E402
import uavcan.register.List_1_0 as RegisterList  # noqa: E402
import uavcan.register.Name_1_0 as Name  # noqa: E402
import uavcan.register.Value_1_0 as Value  # noqa: E402

# board_console has the console (on the kit's helper, which opens the port without resetting the board
# and answers linenoise's terminal queries), shared with tools/yakut_can.py; can_check has the gs_usb
# workaround for macOS. No bytecode: the submodule stays clean.
sys.dont_write_bytecode = True
sys.path.insert(0, str(REPO / "tools"))
import api_smoke  # noqa: E402
import board_console  # noqa: E402
import can_check  # noqa: E402

can_check.patch_gs_usb_for_macos()


def pytest_collection_modifyitems(config: pytest.Config, items: list[pytest.Item]) -> None:
    """`slow` tests last: the one alone on the bus closes the adapter, which the rest would only
    open again"""
    items.sort(key=lambda item: item.get_closest_marker("slow") is not None)


_exit_status = 0


def pytest_sessionfinish(session: pytest.Session, exitstatus: int) -> None:
    global _exit_status
    _exit_status = int(exitstatus)


@pytest.hookimpl(trylast=True)
def pytest_unconfigure(config: pytest.Config) -> None:
    """After a session in which an open found the adapter gone, Python's own teardown crashed once
    (a segfault in gs_usb/libusb as the objects of the failed open were freed), turning a result
    into exit 139. The results are reported by now, so in that case leave without the teardown,
    with pytest's own exit status."""
    if Net.open_failed:
        sys.stdout.flush()
        sys.stderr.flush()
        os._exit(_exit_status)


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--can-iface", default="gs_usb:0", help="python-can interface:channel (default gs_usb:0)")
    parser.addoption("--can-bitrate", default=1_000_000, type=int, help="bit/s (default 1000000)")
    parser.addoption("--console-port", default=os.environ.get("ESPPORT", "/dev/cu.wchusbserial5B910448341"),
                     help="the board's console (default $ESPPORT, or the bench's CH343P)")
    parser.addoption("--dut-node-id", default=52, type=int, help="the fixed node-ID the board is given (default 52)")
    parser.addoption("--local-node-id", default=10, type=int, help="the test harness's node-ID (default 10)")
    parser.addoption("--board-host", default=None,
                     help="the board's address on Wi-Fi, for the `ota` tests and the HTTP comparisons (an API password "
                          "from $HOLO_PASSWORD)")
    parser.addoption("--clip", default="leia240.mjpeg.mov", help="a clip on the board for the load tests")
    parser.addoption("--holo-minutes", default=3.0, type=float,
                     help="how long the holo's load test runs (default 3; the phase gates run 10)")


# --------------------------------------------------------------------------------------------- board


class Board(board_console.Console):
    """The board's console (board_console.Console: commands, the log between them, `restart`, quiet()),
    and what the tests ask of the node through it"""

    async def restarted(self, mark: int, timeout: float = 20) -> None:
        """Wait for a restart the board was told of some other way (uavcan.node.ExecuteCommand): its boot
        log after mark(), then its console. The event loop runs meanwhile"""
        end = time.monotonic() + timeout
        while "ESP-ROM:" not in self.since(mark):
            if time.monotonic() > end:
                raise AssertionError(f"the board did not restart; it printed:\n{self.since(mark)[-500:]}")
            await asyncio.sleep(0.1)
        await asyncio.sleep(1.5)
        self.prompt_back(timeout, "its restart")

    # ---- the node

    def plug_and_play(self) -> None:
        """`cyphal id auto`, from the node's next start. Only with the adapter open (or closed for good):
        the node is never quiet while it asks, and quiet() fails until it has a fixed node-ID again"""
        out = self.command("cyphal id auto")
        assert "plug-and-play" in out, out

    def restart_asking(self) -> tuple[float, float]:
        """`cyphal restart` with plug-and-play set: the node starts again, asking for a node-ID. When the
        command was sent and when it came back, on asyncio's clock (time.monotonic()): the start is
        between the two"""
        t0 = time.monotonic()
        out = self.command("cyphal restart")
        t1 = time.monotonic()
        assert "waiting for a node-ID" in out, out
        return t0, t1

    def cyphal(self) -> dict:
        return parse_cyphal(self.command("cyphal"))

    def first_line(self, command: str) -> str:
        """A command's first line of output: `leds`'s setting, `screen`'s, ..."""
        return self.command(command).strip().splitlines()[0]

    def in_state(self, state: str, node_id: int | None, end: float) -> dict | None:
        """The status if the node is in `state` (on `node_id`, if given); None to look again, unless
        time.monotonic() is past `end`: then the test fails. For wait_state() and until_state()"""
        st = self.cyphal()
        if st["state"] == state and (node_id is None or st["node_id"] == node_id):
            return st
        if time.monotonic() > end:
            raise AssertionError(f"the node is {st['state']!r}, not {state!r}:\n{st['text']}")
        return None

    def wait_state(self, state: str, node_id: int | None = None, timeout: float = 10) -> dict:
        """Blocking, for code that isn't a coroutine (ensure_node()); a test uses until_state()"""
        end = time.monotonic() + timeout
        while (st := self.in_state(state, node_id, end)) is None:
            time.sleep(0.5)
        return st

    def firmware_version(self) -> str:
        m = re.search(r"App:\S+ (\S+)", self.command("version"))
        assert m, "no version"
        return m.group(1)

    def hostname(self) -> str:
        m = re.search(r"http://([\w-]+)\.local/", self.command("web"))
        return m.group(1) if m else ""


def _int(pattern: str, text: str) -> int | None:
    m = re.search(pattern, text)
    return int(m.group(1)) if m else None


def parse_cyphal(text: str) -> dict:
    """`cyphal`'s report as a dict: state (off, failed, waiting, listening, online, in-use), node_id, ..."""
    first = text.strip().splitlines()[0] if text.strip() else ""
    st: dict = {"text": text, "node_id": None}
    if m := re.match(r"cyphal: node (\d+) \((fixed|allocated)\), ([a-z0-9_.]+), ", first):
        st.update(state="online", node_id=int(m.group(1)), how=m.group(2), name=m.group(3))
    elif m := re.match(r"cyphal: listening, before going online as node (\d+) \(fixed\), ([a-z0-9_.]+), ", first):
        st.update(state="listening", node_id=int(m.group(1)), name=m.group(2))
    elif m := re.match(r"cyphal: stopped: node-ID (\d+) is in use by another node, ([a-z0-9_.]+), ", first):
        st.update(state="in-use", node_id=int(m.group(1)), name=m.group(2))
    elif first.startswith("cyphal: waiting for a node-ID, "):
        st["state"] = "waiting"
    elif first.startswith("cyphal: off"):
        st["state"] = "off"
    elif first.startswith("cyphal: not started"):
        st["state"] = "failed"
    else:
        st["state"] = "unknown"
    if m := re.search(r"unique id ([0-9a-f]{32}), pseudo-ID ([0-9a-f]{12}) \((\d+)\)", text):
        st["unique_id"] = bytes.fromhex(m.group(1))
        st["pseudo_id"] = int(m.group(2), 16)
        assert st["pseudo_id"] == int(m.group(3)), "the pseudo-ID's hex and decimal differ"
    if m := re.search(r"bus: ([a-z-]+)", text):
        st["bus"] = m.group(1)
    for key, pattern in [
        ("tx_errors", r"tx errors (\d+)"), ("rx_errors", r"rx errors (\d+)"), ("bus_off", r"bus-off (\d+) times"),
        ("transfers_sent", r"transfers: sent (\d+)"), ("transfers_received", r"transfers: sent \d+, received (\d+)"),
        ("frames_sent", r"frames: sent (\d+)"), ("frames_received", r"frames: sent \d+, received (\d+)"),
        ("dropped", r"; dropped (\d+)"), ("uptime", r"up (\d+) s"), ("not_sent", r"not sent (\d+)"),
        ("not_read", r"not read \(queue full\) (\d+)"), ("heap_used", r"libcanard heap (\d+) of"),
        ("heap_peak", r"bytes \((\d+) at most"), ("stack_unused", r"stack (\d+) bytes never used"),
        ("internal_free", r"internal heap (\d+) free"), ("internal_lowest", r"(\d+) lowest"),
    ]:
        st[key] = _int(pattern, text)
    st["heap_ran_out"] = "it ran out" in text
    if m := re.search(r"services: registers read (\d+), written (\d+), refused (\d+), listed (\d+); commands (\d+), "
                      r"bad (\d+); busy (\d+); port lists (\d+)", text):
        st["services"] = dict(zip(("register_reads", "register_writes", "register_refused", "register_lists",
                                   "commands", "commands_refused", "busy", "port_lists"), map(int, m.groups())))
    if m := re.search(r"application: requests (\d+), messages (\d+); health (\w+)", text):
        st["application"] = {"requests": int(m.group(1)), "messages": int(m.group(2)), "health": m.group(3)}
    # Each published port: sent, and replaced by a newer one (or refused, a queue being full)
    if m := re.search(r"published: (.*)", text):
        st["published"] = {p.group(1): {"sent": int(p.group(2)), "lost": int(p.group(3))}
                           for p in re.finditer(r"(\w+) (\d+) \((\d+) (?:replaced|refused, full)\)", m.group(1))}
    return st


def ensure_node(board: Board, node_id: int, restart: bool = True) -> dict | None:
    """The node online on `node_id`, fixed: `link mode can` and a restart of the board if it isn't the
    link's mode (nothing holds the adapter yet), then the node-ID and `cyphal restart` if needed.
    restart=False, for a caller about to open the adapter, whose Board.quiet() restarts the node
    anyway: the node-ID is set and left for that restart, and nothing is waited for (None)."""
    st = board.cyphal()
    if st["state"] == "off":
        assert "link mode can" in st["text"], st["text"]
        board.command("link mode can")
        board.restart()
        st = board.cyphal()
    assert st["state"] not in ("off", "failed", "unknown"), st["text"]
    # Running, or about to, on node_id as a fixed node-ID, with nothing else saved: not allocated it
    fixed = st["state"] == "listening" or (st["state"] == "online" and st.get("how") == "fixed")
    if not (fixed and st["node_id"] == node_id and "from the next start" not in st["text"]):
        out = board.command(f"cyphal id {node_id}")
        assert "fixed" in out, out
        if restart:
            board.command("cyphal restart")
    return board.wait_state("online", node_id, timeout=10) if restart else None


@pytest.fixture(scope="session")
def board(request: pytest.FixtureRequest) -> Board:
    b = Board(request.config.getoption("--console-port"))
    yield b
    b.close()


@pytest.fixture(scope="session")
def dut_node_id(request: pytest.FixtureRequest) -> int:
    return request.config.getoption("--dut-node-id")


@pytest.fixture(scope="session")
def http(request: pytest.FixtureRequest) -> api_smoke.Board | None:
    """The board's HTTP API with --board-host (Board.call(method, path, body=...) -> (status, reply)), else None"""
    host = request.config.getoption("--board-host")
    return api_smoke.Board(host, os.environ.get("HOLO_PASSWORD")) if host else None


# ------------------------------------------------------------------------------------------ the bus


class Net:
    """The computer's side of the bus: a pycyphal node on the one adapter. Each open and close is
    made while the board is quiet (Board.quiet()). Close it before the board restarts."""

    REOPEN_S = board_console.ADAPTER_REOPEN_S      # the adapter resets as it closes: back on USB after this
    BACK_S = 20.0       # how long an open keeps trying while the adapter is off USB
    made: list[Net] = []
    open_failed = False     # an open found the adapter gone at least once: see pytest_unconfigure()

    def __init__(self, board: Board, iface: str, bitrate: int, local_node_id: int):
        self.board = board
        self.iface, self.bitrate, self.local_node_id = iface, bitrate, local_node_id
        self.node: pycyphal.application.Node | None = None
        self.media: pycyphal.transport.can.media.pythoncan.PythonCANMedia | None = None
        self.closed_at = 0.0
        self._capture: Capture | None = None
        self.mute = False           # muted(): the harness's frames dropped before the adapter
        self.dropped = 0            # how many, since the open
        Net.made.append(self)

    def _mutable(self, media: pycyphal.transport.can.media.pythoncan.PythonCANMedia) -> None:
        """The media's send, through mute: pycyphal is told the frames went, and nothing goes on the bus"""
        send = media.send

        async def send_unless_muted(frames, monotonic_deadline: float) -> int:
            frames = list(frames)
            if self.mute:
                self.dropped += len(frames)
                return len(frames)
            return await send(frames, monotonic_deadline)

        media.send = send_unless_muted

    @contextlib.asynccontextmanager
    async def muted(self) -> AsyncIterator[None]:
        """Nothing sent by the harness meanwhile: for a board that restarts while the adapter is open. Until
        it acknowledges again, a frame from the harness would go unacknowledged, which the adapter can't
        stand (conftest's rules)"""
        self.mute = True
        try:
            yield
        finally:
            self.mute = False

    @staticmethod
    def _retry(make):
        """python-can's gs_usb resets the adapter as it opens it, and the adapter goes off USB and
        comes back: an open soon after a close can find it gone (ENODEV, errno 19). On the bench that
        once lasted longer than five tries a second apart, so it is tried for BACK_S, every half
        second. Nothing is sent by a failed open."""
        end = time.monotonic() + Net.BACK_S
        while True:
            try:
                return make()
            except usb.core.USBError as e:
                Net.open_failed = True
                if e.errno != 19 or time.monotonic() > end:
                    raise
                time.sleep(0.5)

    async def open(self) -> pycyphal.application.Node:
        assert self.node is None
        if (wait := self.closed_at + self.REOPEN_S - time.monotonic()) > 0:
            await asyncio.sleep(wait)
        self.board.quiet()
        self.media = self._retry(lambda: pycyphal.transport.can.media.pythoncan.PythonCANMedia(
            self.iface, self.bitrate, mtu=8))
        self._mutable(self.media)
        self.dropped = 0
        transport = pycyphal.transport.can.CANTransport(self.media, local_node_id=self.local_node_id)
        self.node = pycyphal.application.make_node(pycyphal.application.NodeInfo(name="holo.tests.harness"),
                                                   transport=transport)
        self.node.start()
        return self.node

    async def close(self) -> None:
        if self.node is not None:
            self.board.quiet()
            self.node.close()
            self.node = None
            self.media = None
            self._capture = None
            self.closed_at = time.monotonic()     # open() waits out the rest of REOPEN_S

    def capture(self) -> Capture:
        """Every frame on the bus from now until Capture.stop(). One Capture for each open of the
        adapter, started again at each call: pycyphal can't end a capture once begun"""
        assert self.node is not None
        if self._capture is None:
            self._capture = Capture(self.node)
        return self._capture.start()

    async def send_raw(self, can_id: int, data: bytes) -> None:
        """One extended frame onto the bus through the pycyphal node's own adapter, as another node
        would send it. Its echo comes back as loopback, not as a frame received."""
        from pycyphal.transport.can.media import DataFrame, Envelope, FrameFormat
        assert self.media is not None
        frame = Envelope(DataFrame(FrameFormat.EXTENDED, can_id, bytearray(data)), loopback=False)
        sent = await self.media.send([frame], asyncio.get_running_loop().time() + 1.0)
        assert sent == 1, "the frame wasn't sent"

    def frames_in(self) -> int:
        assert self.node is not None
        return self.node.presentation.transport.sample_statistics().in_frames


async def open_checked(n: Net, board: Board, dut_node_id: int) -> None:
    """Open the pycyphal node and see the board's heartbeat arrive. If none does, stop the session:
    every test after would fail the same way. A board whose frames are acknowledged (`frames: sent`
    rising) while none reach the computer points at the adapter, which a candleLight does when it
    has wedged."""
    await n.open()
    if not await heartbeats(n.node, dut_node_id, 4.0):     # the listen's 2 s, then a heartbeat a second
        st = board.cyphal()
        await n.close()
        pytest.exit(f"no heartbeat from node {dut_node_id} reached the computer in 4 s. The board says:\n"
                    f"{st['text']}\nFrames sent and acknowledged with nothing received here: the adapter has "
                    "wedged.", returncode=3)


@pytest.fixture(scope="session")
async def net(request: pytest.FixtureRequest, board: Board, dut_node_id: int) -> AsyncIterator[Net]:
    """The board's node online on --dut-node-id first, then the adapter, with a pycyphal node on it"""
    ensure_node(board, dut_node_id, restart=False)
    n = Net(board, request.config.getoption("--can-iface"), request.config.getoption("--can-bitrate"),
            request.config.getoption("--local-node-id"))
    await open_checked(n, board, dut_node_id)
    yield n
    await n.close()


@pytest.fixture
async def node(net: Net, board: Board, dut_node_id: int) -> pycyphal.application.Node:
    """The harness's pycyphal node, reopened if a test left the adapter closed or raw"""
    if net.node is None:
        ensure_node(board, dut_node_id, restart=False)
        await open_checked(net, board, dut_node_id)
    return net.node


@pytest.fixture
async def alone(board: Board, dut_node_id: int) -> None:
    """The node online with nobody else on the bus: the adapter closed if an earlier test opened it,
    and not opened for this one"""
    for n in Net.made:
        await n.close()
    ensure_node(board, dut_node_id)


@pytest.fixture
async def get_info(node: pycyphal.application.Node, dut_node_id: int) -> AsyncIterator[pycyphal.presentation.Client]:
    client = node.make_client(GetInfo, dut_node_id)
    client.response_timeout = 1.0
    yield client
    client.close()


# ---------------------------------------------------------------------------------------- helpers


async def heartbeats(node: pycyphal.application.Node, source: int, seconds: float,
                     until=lambda: False) -> list[tuple[float, int, object]]:
    """Every heartbeat from `source` for `seconds`, or until until(): (arrival, transfer-ID, message)"""
    sub = node.make_subscriber(Heartbeat)
    got = []
    loop = asyncio.get_running_loop()
    end = loop.time() + seconds
    try:
        while (remaining := end - loop.time()) > 0 and not until():
            result = await sub.receive_for(min(remaining, 0.5))
            if result is not None and result[1].source_node_id == source:
                got.append((loop.time(), result[1].transfer_id, result[0]))
    finally:
        sub.close()
    return got


async def until_state(board: Board, state: str, node_id: int | None = None, timeout: float = 10) -> dict:
    """Board.wait_state() for a test: between looks at the console the event loop runs, where
    wait_state()'s sleep would hold it, and with it every frame pycyphal has to send or answer"""
    end = time.monotonic() + timeout
    while (st := board.in_state(state, node_id, end)) is None:
        await asyncio.sleep(0.5)
    return st


HEARTBEAT = 7509
WARNING = "cyphal: node-ID {} is in use by another node; Cyphal stopped"


def heartbeat_frame(source: int, tid: int) -> tuple[int, bytes]:
    """A heartbeat from `source`, as another node would send it: (CAN ID, data)"""
    can_id = (4 << 26) | (3 << 21) | (HEARTBEAT << 8) | source     # nominal priority, a message
    data = struct.pack("<IBBB", 1, 0, 0, 0) + bytes([0xE0 | (tid & 31)])   # one frame: start, end, toggle
    return can_id, data


def warnings(log: str, node_id: int) -> int:
    """How many times the board warned that node_id is in use"""
    return len(re.findall(re.escape(WARNING.format(node_id)), log))


def gaps(beats: list) -> list[float]:
    """The times between each and the next: heartbeats()' (their arrival first) or plain times"""
    times = [b[0] if isinstance(b, tuple) else b for b in beats]
    return [b - a for a, b in zip(times, times[1:])]


class Beats:
    """Every heartbeat from `server` in the background until close(), by its own stamp (pycyphal gives a gs_usb
    adapter's frame the adapter's clock), for the gaps between them over minutes, which a console command holding
    the event loop would otherwise stretch. heartbeats() is the same in the foreground, for a few seconds"""

    def __init__(self, node: pycyphal.application.Node, server: int):
        self.times: list[float] = []
        self._sub = node.make_subscriber(Heartbeat)

        async def keep(msg, transfer) -> None:
            if transfer.source_node_id == server:
                self.times.append(float(transfer.timestamp.monotonic))
        self._sub.receive_in_background(keep)

    def close(self) -> None:
        self._sub.close()


class Frame:
    """A frame the adapter saw: when the adapter took it, on asyncio's clock (time.monotonic(): see
    Capture.stop()); whether the harness sent it; and its Cyphal/CAN fields"""

    def __init__(self, t_host: float, t_adapter: float, own: bool, can_id: int, data: bytes):
        self.t_host, self.t_adapter, self.own, self.id, self.data = t_host, t_adapter, own, can_id, data
        self.t = t_host
        self.service = bool(can_id >> 25 & 1)
        self.anonymous = not self.service and bool(can_id >> 24 & 1)
        self.subject = None if self.service else can_id >> 8 & 0x1FFF
        self.source = can_id & 0x7F       # an anonymous frame's is its discriminator, not a node-ID
        self.tail = data[-1] if data else 0


class Capture:
    """Every frame on the bus, through the harness's adapter, from start() until stop() (Net.capture()).
    pycyphal's capture can't be ended once begun, so the callback stays registered, once for each open
    of the adapter, and keeps frames only between start() and stop().

    pycyphal stamps a gs_usb adapter's frames with the adapter's clock, and the callback's own time is
    late by as long as the event loop was busy (a console command blocks it for half a second). So
    stop() puts the adapter's stamps on the computer's clock, by the frame that reached the callback
    soonest after the adapter took it."""

    def __init__(self, node: pycyphal.application.Node):
        self.frames: list[Frame] = []
        self.on = False
        node.presentation.transport.begin_capture(self._keep)

    def start(self) -> Capture:
        self.frames = []
        self.on = True
        return self

    def _keep(self, c) -> None:
        if self.on:
            self.frames.append(Frame(time.monotonic(), c.timestamp.monotonic_ns / 1e9, c.own, c.frame.identifier,
                                     bytes(c.frame.data)))

    def stop(self) -> list[Frame]:
        self.on = False
        if self.frames:
            offset = min(f.t_host - f.t_adapter for f in self.frames)
            for f in self.frames:
                f.t = f.t_adapter + offset
        return self.frames


def expected_software_version(version: str) -> tuple[int, int, int]:
    """(major, minor, VCS revision) as the firmware derives them from its version (cyphal_util.c)"""
    if m := re.fullmatch(r"v(\d+)\.(\d+)\.(\d+)(-rc\d+)?", version):
        return min(int(m.group(1)), 255), min(int(m.group(2)), 255), 0
    m = re.search(r"-g([0-9a-f]{4,})(?=-|$)", version) or re.fullmatch(r"([0-9a-f]{4,})(?:-.*)?", version)
    return 0, 0, int(m.group(1)[:16], 16) if m else 0


# ------------------------------------------------------------------------------------- registers


def natural16(*values: int) -> Value:
    return Value(natural16=Natural16(list(values)))


def natural32(*values: int) -> Value:
    return Value(natural32=Natural32(list(values)))


def string(text: str) -> Value:
    return Value(string=String(text))


def plain(value: Value) -> int | list[int] | str | None:
    """A register's value as Python: one number, several, a string, or None for empty (no register)"""
    if value.empty is not None:
        return None
    if value.string is not None:
        return bytes(value.string.value).decode()
    for kind in ("natural16", "natural32"):
        if (v := getattr(value, kind)) is not None:
            numbers = [int(x) for x in v.value]
            return numbers[0] if kind == "natural16" and len(numbers) == 1 else numbers
    raise AssertionError(f"a value of a type no register here has: {value}")


async def call(node: pycyphal.application.Node, dtype, server: int, request, timeout: float = 1.0,
               port: int | None = None):
    """One call of the service `dtype` to node `server`, on its fixed port or `port`: the response, which the test
    fails without"""
    client = node.make_client(dtype, server) if port is None else node.make_client(dtype, server, port)
    client.response_timeout = timeout
    try:
        result = await client.call(request)
    finally:
        client.close()
    assert result is not None, f"no response from node {server} to {dtype.__name__}{'' if port is None else f' on {port}'}: {request}"
    return result[0]


async def access(node: pycyphal.application.Node, server: int, name: str, value: Value | None = None) -> Access.Response:
    """uavcan.register.Access: a read, or with `value` a write"""
    return await call(node, Access, server, Access.Request(name=Name(name), value=value or Value()))


async def register_names(node: pycyphal.application.Node, server: int) -> list[str]:
    """uavcan.register.List from index 0 until the empty name"""
    names = []
    for index in range(1000):
        name = bytes((await call(node, RegisterList, server, RegisterList.Request(index=index))).name.name).decode()
        if not name:
            break
        names.append(name)
    return names


def reference_registers() -> dict[str, tuple[str, object, bool]]:
    """The registers manual/reference/cyphal.md gives, as built so far (the node and the ports; not
    the holo's, from a later phase): name -> (type, default, mutable)"""
    page = (REPO / "manual" / "reference" / "cyphal.md").read_text()
    out: dict[str, tuple[str, object, bool]] = {}
    node = page[page.index("**The node:**"):page.index("**The ports**")]
    for m in re.finditer(r"^\| `([a-z0-9_.]+)` \| (natural16\[1\]|natural32\[2\]|string) \|[^|]*\| ([^|]+) \| "
                         r"(mutable|immutable), persistent \|", node, re.M):
        default = m.group(3).strip()
        value = (default.strip("`") if m.group(2) == "string" else
                 [int(x) for x in re.findall(r"\d+", default)] if default.startswith("[") else int(default))
        out[m.group(1)] = (m.group(2), value, m.group(4) == "mutable")
    # The port types, by their default port, from the table of the node's ports
    types = {(m.group(2), int(m.group(3))): m.group(1) for m in re.finditer(
        r"\| `(r2\.holo\.\w+\.\d+\.\d+)` \| (subject|service) (\d+)", page)}
    kinds = {"Published": "pub", "Subscribed": "sub", "Served": "srv"}
    for m in re.finditer(r"^\| `(\w+)` \| (Published|Subscribed|Served) \| (subject|service) (\d+) \|", page, re.M):
        name, kind, default = m.group(1), kinds[m.group(2)], int(m.group(4))
        out[f"uavcan.{kind}.{name}.id"] = ("natural16[1]", default, True)
        out[f"uavcan.{kind}.{name}.type"] = ("string", types[(m.group(3), default)], False)
    return out


def console_registers(text: str) -> dict[str, str]:
    """`cyphal register`'s list: name -> the value as shown"""
    out = {}
    for line in text.splitlines():
        if m := re.match(r"([a-z0-9_.]+) +(.+?) +(natural16\[1\]|natural32\[2\]|string), ", line):
            out[m.group(1)] = m.group(2)
    return out
