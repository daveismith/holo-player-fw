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
    and opened again only where a test must (a restart of the board, 10 minutes alone).
  * One program holds the adapter at a time: stop cyphal_service first.

Markers: `restarts` (restarts the node or the board; every test restarts the node at least as it
opens the adapter), `slow` (minutes), `ota` (writes the board's spare OTA slot over Wi-Fi: needs
--board-host; never activates it).

The DSDL is the public_regulated_data_types submodule, the revision the firmware's C headers were
generated from (tools/gen_dsdl.py); pycyphal's Python for it is compiled once into
build/pycyphal-dsdl/.
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DSDL = REPO / "external" / "public_regulated_data_types"
DSDL_OUT = REPO / "build" / "pycyphal-dsdl"


def _compile_dsdl() -> None:
    """pycyphal's Python for the uavcan namespace, compiled again when the DSDL revision moves.
    Before pycyphal.application is imported: it imports uavcan.node at load."""
    revision = subprocess.run(["git", "-C", str(DSDL), "rev-parse", "HEAD"], capture_output=True, text=True,
                              check=True).stdout.strip()
    stamp = DSDL_OUT / "revision"
    if not stamp.exists() or stamp.read_text().strip() != revision:
        import pycyphal.dsdl

        DSDL_OUT.mkdir(parents=True, exist_ok=True)
        pycyphal.dsdl.compile_all([DSDL / "uavcan"], DSDL_OUT)
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

# The kit's console helper opens the port without resetting the board and answers linenoise's
# terminal queries; can_check has the gs_usb workaround for macOS. No bytecode: the submodule stays
# clean.
sys.dont_write_bytecode = True
sys.path.insert(0, str(REPO / "external" / "esp-console-kit" / "tools"))
sys.path.insert(0, str(REPO / "tools"))
import can_check  # noqa: E402
import fs_xfer  # noqa: E402

can_check.patch_gs_usb_for_macos()


def pytest_collection_modifyitems(config: pytest.Config, items: list[pytest.Item]) -> None:
    """`slow` tests last: the one alone on the bus closes the adapter, which the rest would only
    open again"""
    items.sort(key=lambda item: item.get_closest_marker("slow") is not None)


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--can-iface", default="gs_usb:0", help="python-can interface:channel (default gs_usb:0)")
    parser.addoption("--can-bitrate", default=1_000_000, type=int, help="bit/s (default 1000000)")
    parser.addoption("--console-port", default=os.environ.get("ESPPORT", "/dev/cu.wchusbserial5B910448341"),
                     help="the board's console (default $ESPPORT, or the bench's CH343P)")
    parser.addoption("--dut-node-id", default=52, type=int, help="the fixed node-ID the board is given (default 52)")
    parser.addoption("--local-node-id", default=10, type=int, help="the test harness's node-ID (default 10)")
    parser.addoption("--board-host", default=None, help="the board's address on Wi-Fi, for the `ota` tests")
    parser.addoption("--clip", default="leia240.mjpeg.mov", help="a clip on the board for the load tests")


# --------------------------------------------------------------------------------------------- board


class Board:
    """The board's console. Commands are sent once each, never repeated: a timeout fails the test.
    Output that arrives between commands (log lines) is kept in `log`."""

    PROMPT_WAIT = 3

    def __init__(self, port: str):
        self.con = fs_xfer.Console(port, 115200)
        self.log = ""

    def close(self) -> None:
        self.con.close()

    def _keep(self) -> None:
        while self.con.pump():
            pass
        self.log += self.con.take().replace("\r", "")

    def mark(self) -> int:
        """A place in `log`, for since()"""
        self._keep()
        return len(self.log)

    def since(self, mark: int) -> str:
        """What was printed outside commands since mark()"""
        self._keep()
        return self.log[mark:]

    def command(self, line: str, timeout: float = 20) -> str:
        self._keep()
        # An empty line first, for a prompt to start from. A log line printed just after that
        # prompt (the node's "online", say) leaves none at the end, and linenoise doesn't print it
        # again: the empty line, unlike the command, is safe to send twice.
        for _ in range(2):
            self.con.s.write(b"\r")
            if self.con.wait_prompt(self.PROMPT_WAIT):
                break
        else:
            self._keep()
            raise AssertionError(f"no console prompt before {line!r}; last output:\n{self.log[-500:]}")
        self._keep()
        self.con.s.write(line.encode() + b"\r")
        ok = self.con.wait_prompt(timeout)
        text = self.con.take().replace("\r", "")
        if not ok:
            raise AssertionError(f"no prompt after {line!r}:\n{text}")
        return fs_xfer.strip_echo(text, line)

    def restart(self, timeout: float = 20) -> None:
        """`restart`, and wait for the console to come back"""
        self._keep()
        self.con.s.write(b"restart\r")
        time.sleep(1.5)
        end = time.time() + timeout
        while time.time() < end:
            self.con.s.write(b"\r")
            if self.con.wait_prompt(1):
                self._keep()
                return
        raise AssertionError("the console did not come back after `restart`")

    # ---- the node

    def quiet(self) -> None:
        """`cyphal restart`: the node then listens for 2 s, sending nothing, which is when the
        adapter may be opened or closed"""
        out = self.command("cyphal restart")
        assert "listening" in out, out

    def cyphal(self) -> dict:
        return parse_cyphal(self.command("cyphal"))

    def wait_state(self, state: str, node_id: int | None = None, timeout: float = 10) -> dict:
        end = time.time() + timeout
        while True:
            st = self.cyphal()
            if st["state"] == state and (node_id is None or st["node_id"] == node_id):
                return st
            if time.time() > end:
                raise AssertionError(f"the node is {st['state']!r}, not {state!r}:\n{st['text']}")
            time.sleep(0.5)

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
    """`cyphal`'s report as a dict: state (off, failed, no-pnp, listening, online, in-use), node_id, ..."""
    first = text.strip().splitlines()[0] if text.strip() else ""
    st: dict = {"text": text, "node_id": None}
    if m := re.match(r"cyphal: node (\d+) \((fixed|allocated)\), ([a-z0-9_.]+), ", first):
        st.update(state="online", node_id=int(m.group(1)), how=m.group(2), name=m.group(3))
    elif m := re.match(r"cyphal: listening, before going online as node (\d+) \(fixed\), ([a-z0-9_.]+), ", first):
        st.update(state="listening", node_id=int(m.group(1)), name=m.group(2))
    elif m := re.match(r"cyphal: stopped: node-ID (\d+) is in use by another node, ([a-z0-9_.]+), ", first):
        st.update(state="in-use", node_id=int(m.group(1)), name=m.group(2))
    elif first.startswith("cyphal: no node-ID"):
        st["state"] = "no-pnp"
    elif first.startswith("cyphal: off"):
        st["state"] = "off"
    elif first.startswith("cyphal: not started"):
        st["state"] = "failed"
    else:
        st["state"] = "unknown"
    if m := re.search(r"unique id ([0-9a-f]{32})", text):
        st["unique_id"] = bytes.fromhex(m.group(1))
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
    if not (st["state"] in ("online", "listening") and st["node_id"] == node_id and "from the next start" not in
            st["text"]):
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


# ------------------------------------------------------------------------------------------ the bus


class Net:
    """The computer's side of the bus: a pycyphal node on the one adapter. Each open and close is
    made while the board is quiet (Board.quiet()). Close it before the board restarts."""

    REOPEN_S = 2.0      # the adapter resets as it closes: what it takes to come back on USB
    made: list[Net] = []

    def __init__(self, board: Board, iface: str, bitrate: int, local_node_id: int):
        self.board = board
        self.iface, self.bitrate, self.local_node_id = iface, bitrate, local_node_id
        self.node: pycyphal.application.Node | None = None
        self.media: pycyphal.transport.can.media.pythoncan.PythonCANMedia | None = None
        self.closed_at = 0.0
        Net.made.append(self)

    @staticmethod
    def _retry(make):
        """python-can's gs_usb resets the adapter as it opens it, and the adapter goes off USB and
        comes back: an open soon after a close can find it gone. Nothing is sent by a failed open."""
        for attempt in range(5):
            try:
                return make()
            except usb.core.USBError as e:
                if e.errno != 19 or attempt == 4:
                    raise
                time.sleep(1.0)

    async def open(self) -> pycyphal.application.Node:
        assert self.node is None
        if (wait := self.closed_at + self.REOPEN_S - time.monotonic()) > 0:
            await asyncio.sleep(wait)
        self.board.quiet()
        self.media = self._retry(lambda: pycyphal.transport.can.media.pythoncan.PythonCANMedia(
            self.iface, self.bitrate, mtu=8))
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
            self.closed_at = time.monotonic()     # open() waits out the rest of REOPEN_S

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


def gaps(beats: list[tuple[float, int, object]]) -> list[float]:
    return [b[0] - a[0] for a, b in zip(beats, beats[1:])]


def expected_software_version(version: str) -> tuple[int, int, int]:
    """(major, minor, VCS revision) as the firmware derives them from its version (cyphal_util.c)"""
    if m := re.fullmatch(r"v(\d+)\.(\d+)\.(\d+)(-rc\d+)?", version):
        return min(int(m.group(1)), 255), min(int(m.group(2)), 255), 0
    m = re.search(r"-g([0-9a-f]{4,})(?=-|$)", version) or re.fullmatch(r"([0-9a-f]{4,})(?:-.*)?", version)
    return 0, 0, int(m.group(1)[:16], 16) if m else 0
