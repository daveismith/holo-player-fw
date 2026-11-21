"""The board's console for scripts that share the bench with a gs_usb adapter: the Cyphal node's hardware
tests (tests/hardware/conftest.py) and tools/yakut_can.py.

On the kit's fs_xfer.Console (opened without resetting the board; linenoise's terminal queries
answered, here even when a query is split between two reads: _Port), it adds the console's second trap (CLAUDE.md, "Flashing"): a log line printed after a
command's prompt leaves the output not ending at a prompt, and linenoise won't print another, so an
empty line is sent once the output goes quiet. And the one step every open or close of the adapter
waits for: `cyphal restart`, after which a node with a fixed node-ID listens for 2 s, sending nothing.
"""
from __future__ import annotations

import re
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.dont_write_bytecode = True      # the kit's submodule stays clean
if str(REPO / "external" / "esp-console-kit" / "tools") not in sys.path:
    sys.path.insert(0, str(REPO / "external" / "esp-console-kit" / "tools"))
import fs_xfer  # noqa: E402

# The adapter resets as it closes and goes off USB for a moment: what it takes to come back
ADAPTER_REOPEN_S = 2.0

# linenoise's terminal queries and the answers fs_xfer gives them
QUERIES = ((b"\x1b[6n", b"\x1b[24;80R"), (b"\x1b[5n", b"\x1b[0n"))


# TEMPORARY, a known exception to CLAUDE.md's kit rule ("esp-console-kit"): this is a fix to the kit's
# fs_xfer.Console.pump(), kept here, not in the kit, until the user decides on kit changes. The kit change is this
# _tail and count logic moved into fs_xfer.Console.pump(); once the kit has it and its pin is bumped, delete _Port and
# open fs_xfer.Console in Console.__init__() again.
class _Port(fs_xfer.Console):
    """The kit's console, answering linenoise's queries even when one arrives split across two reads. fs_xfer looks
    for each query within one read's bytes, and a read ends wherever the serial driver's buffer did: a query cut in
    two by that went unanswered, and linenoise, which waits for the answer before it takes any more input, swallowed
    the command typed next and never gave a prompt (the console's stuck-prompt trap, seen three times in the
    hardware tests). Here the last bytes of each read are kept, and a query that ends in the next is answered then;
    each query once, however many come in one read."""

    def __init__(self, port: str, baud: int):
        super().__init__(port, baud)
        self._tail = b""

    def pump(self) -> bytes:
        data = self.s.read(4096)
        if data:
            seen = self._tail + data
            for query, answer in QUERIES:
                for _ in range(seen.count(query) - self._tail.count(query)):
                    self.s.write(answer)
            self._tail = seen[-3:]
            self.buf += data
        return data


class Console:
    """The board's console. Commands are sent once each, never repeated: a timeout fails the caller.
    Output that arrives between commands (log lines) is kept in `log`."""

    PROMPT_WAIT = 3
    QUIET_S = 0.3       # no output for this long after a prompt and a log line: the log line is whole
    # A prompt at the start of a line with more after it on the same line: a log line printed after it
    PROMPT_THEN_LOG = re.compile(r"(?:^|\n)\S+> (?=\S)")

    def __init__(self, port: str):
        self.con = _Port(port, 115200)
        self.log = ""

    def close(self) -> None:
        self.con.close()        # its own order of DTR and RTS, which doesn't reset the board

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

    def wait_for(self, pattern: re.Pattern, mark: int, timeout: float) -> bool:
        """Whether `pattern` is printed outside commands after mark() within `timeout`"""
        end = time.monotonic() + timeout
        while not pattern.search(self.since(mark)):
            if time.monotonic() > end:
                return False
            time.sleep(0.01)
        return True

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
        ok, nudged = self._wait_end(timeout)
        text = self.con.take().replace("\r", "")
        if not ok:
            raise AssertionError(f"no prompt after {line!r}:\n{text}")
        if nudged:
            # The command's output ends at its prompt; what came after that (a log line, then the
            # prompt the nudge brought) is the log's
            text, rest = self._split_at_prompt(text)
            self.log += rest
        return fs_xfer.strip_echo(text, line)

    def _wait_end(self, timeout: float) -> tuple[bool, bool]:
        """(whether the command's prompt came to be the last thing printed, whether it took a nudge).
        The console's second trap: the command has finished, and a log line came after its prompt
        (Wi-Fi joining, after a restart of the board), so the output doesn't end at a prompt and
        linenoise won't print it again. Then an empty line, safe to send (the command is over), brings
        a fresh prompt to end on."""
        end = time.monotonic() + timeout
        last_data = nudged_at = time.monotonic()
        nudged = False
        while time.monotonic() < end:
            if self.con.pump():
                last_data = time.monotonic()
            if self.con.at_prompt():
                return True, nudged
            now = time.monotonic()
            if now - last_data >= self.QUIET_S and now - nudged_at >= 2 * self.QUIET_S and self._prompt_then_log():
                self.con.s.write(b"\r")
                nudged, nudged_at = True, now
                end = max(end, now + self.PROMPT_WAIT)
        return False, nudged

    @staticmethod
    def _echo_end(text: str) -> int:
        """Where the command's own line ends: its newline, from which a search for a prompt at the start
        of a line after it begins"""
        return text.find("\n") if "\n" in text else len(text)

    def _prompt_then_log(self) -> bool:
        """Whether the output since the command's own line holds a prompt with a log line after it"""
        text = fs_xfer.ANSI.sub(b"", self.con.buf).decode("utf-8", "replace").replace("\r", "")
        return self.PROMPT_THEN_LOG.search(text, self._echo_end(text)) is not None

    @classmethod
    def _split_at_prompt(cls, text: str) -> tuple[str, str]:
        """(the echo, the output and its prompt; what came after that prompt), for output that took a
        nudge to end"""
        m = cls.PROMPT_THEN_LOG.search(text, cls._echo_end(text))
        if m is None:
            return text, ""
        return text[:m.end()], text[m.end():]

    def restart(self, timeout: float = 20) -> None:
        """`restart`, and wait for the console to come back"""
        self._keep()
        self.con.s.write(b"restart\r")
        time.sleep(1.5)
        self.prompt_back(timeout, "`restart`")

    def prompt_back(self, timeout: float, after: str) -> None:
        """The console back after a restart of the board: a prompt within `timeout`"""
        end = time.time() + timeout
        while time.time() < end:
            self.con.s.write(b"\r")
            if self.con.wait_prompt(1):
                self._keep()
                return
        raise AssertionError(f"the console did not come back after {after}")

    def quiet(self) -> str:
        """`cyphal restart`, run to its prompt: the node then listens for 2 s, sending nothing, which is when
        the adapter may be opened or closed. Only with a fixed node-ID (a node with plug-and-play is never
        quiet): the output, which says so"""
        out = self.command("cyphal restart")
        assert "listening" in out, out
        return out
