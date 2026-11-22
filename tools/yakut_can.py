#!/usr/bin/env python3
"""Run Yakut (https://github.com/OpenCyphal/yakut) against the board through a gs_usb adapter
(candleLight, CANable), under the adapter's rules (CLAUDE.md, "CAN and Cyphal"):

    tools/yakut_can.py register-list 52
    tools/yakut_can.py register-access 52 uavcan.srv.motion.id
    tools/yakut_can.py register-access 52 uavcan.pub.status.id 2310
    tools/yakut_can.py --node-id 11 -- call 52 uavcan.node.GetInfo.1.0 '{}'
    tools/yakut_can.py --flush register-list 52
    make yakut YAKUT_PY=/path/to/python ARGS="register-access 52 uavcan.node.id"

Run it with a Python that has yakut installed (`pip install yakut`, with python-can and gs_usb);
this repository's requirements don't include it. It holds the board's console too, so nothing else
may (not the hardware tests, not `idf.py monitor`).

What it does around Yakut:

  * The board must be running with a fixed node-ID (`cyphal id 52`), at the bitrate given
    (--bitrate): a node with plug-and-play is never quiet, and the adapter is opened and closed only
    while the board sends nothing. The adapter is never opened at another bitrate than the board's.
  * The macOS gs_usb workaround (tools/can_check.py's patch_gs_usb_for_macos()).
  * The adapter is opened just after `cyphal restart`, in the node's 2 s listen, and Yakut starts
    once the node says it is online (it answers nothing before). It is closed the same way, while
    pycyphal still reads it, as the hardware tests' harness does. Each `cyphal restart` is run to its
    prompt: linenoise swallows what is typed until its terminal query is answered. If the node isn't
    listening after that restart (it has been switched to plug-and-play meanwhile), it is given its
    fixed node-ID back and restarted once more; and the adapter is closed whatever happens, so that
    it isn't left to the end of the process.
  * Not emptied for the next program unless asked (--flush): that is another open and close of the
    adapter, the riskiest thing done to it on this bench. Without it, run `tools/can_check.py flush`
    (while the board is quiet) before cyphal_service, as after any python-can session (CLAUDE.md). With
    --flush, once the adapter is back on USB (it resets as it closes), it is emptied in another listen.

Refused before anything is opened (refusal()):

  * `execute-command` (`execute` under click 8.2 and later, `cmd`), and `call` to uavcan.node.ExecuteCommand
    (service 435), the command word read as Yakut's own click group reads it once Yakut is imported. Every command sent
    through Yakut on 2026-11-20 (restart twice, identify once) wedged the candleLight, though the board
    carried it out; register reads, writes and lists, with this wrapper as it is, mostly didn't, nor did
    the hardware tests' commands (restart, factory reset, identify, through pycyphal:
    tests/hardware/test_execute_command.py). The cause wasn't found.
  * A write of 65535 (plug-and-play) to uavcan.node.id: the node would come back from the closing
    `cyphal restart` asking for a node-ID, never quiet. `cyphal id auto` on the console does it, with
    the adapter closed.
  * `register-batch`, which could write anything from a file: one register a run, with register-access.

The transport and the DSDL are set for Yakut: UAVCAN__CAN__IFACE gs_usb:0, MTU 8, the bitrate
given, UAVCAN__NODE__ID the local node-ID, CYPHAL_PATH the two DSDL submodules, and PYCYPHAL_PATH
build/yakut-dsdl (where Yakut compiles them, once).
"""

from __future__ import annotations

import argparse
import os
import re
import signal
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.dont_write_bytecode = True      # the kit's submodule stays clean
if str(REPO / "tools") not in sys.path:
    sys.path.insert(0, str(REPO / "tools"))
# board_console (pyserial), can_check (python-can) and usb (pyusb) are imported where they are used, so
# that the refusals below can be checked with nothing but the standard library (tools/test_yakut_can.py)

ONLINE_LINE = re.compile(r"cyphal: node \d+ \(fixed\): online")
FLUSH_REMINDER = "the adapter wasn't emptied: tools/can_check.py flush, while the board is quiet, before cyphal_service"
# `cyphal`'s first line while the node runs with a fixed node-ID: online, or in the listen before
FIXED_STATUS = re.compile(r"cyphal: (?:node (\d+)|listening, before going online as node (\d+)) \(fixed\), "
                          r"[a-z0-9_.]+, (\d+) (Mbit/s|kbit/s|bit/s) on GPIO")
BITRATE_UNITS = {"Mbit/s": 1_000_000, "kbit/s": 1_000, "bit/s": 1}

# Yakut's commands as it names them, with its aliases (yakut/cmd/*: @yakut.subcommand(aliases=...)). How click names
# a command from its function changed: up to 8.1 the function's `_` became `-` ("execute-command"), and click 8.2 on
# drops a "_command" suffix ("execute", as Yakut 0.14 under click 8.3.1 registers it). Every spelling is refused. This
# static list is the fallback; once Yakut is imported, the command word is also resolved through Yakut's own click
# group (yakut_resolver()), and what it resolves to is what is checked
COMMANDS = {
    "call": {"call", "q"},
    "execute-command": {"execute-command", "execute_command", "execute", "cmd"},
    "register-access": {"register-access", "register_access", "r", "reg"},
    "register-batch": {"register-batch", "register_batch", "rbat", "rb"},
}
# The names Yakut's click group resolves a command word to, as COMMANDS names them
RESOLVED = {"execute": "execute-command", "execute-command": "execute-command", "call": "call",
            "register-access": "register-access", "register-batch": "register-batch"}
EXECUTE_COMMAND_SERVICE = 435       # uavcan.node.ExecuteCommand's fixed service-ID


def log(text: str) -> None:
    print(f"[yakut_can] {text}", file=sys.stderr, flush=True)


# Yakut's global options (before the command) that take a value as the next word (yakut/main.py, yakut/param/), as
# Yakut 0.14 has them; with Yakut imported, its click group's own are added (yakut_resolver())
GLOBAL_VALUE_OPTIONS = {"--format", "-F", "--transport", "-i", "--node-info", "--heartbeat-vssc", "--vssc",
                        "--heartbeat-period", "--heartbeat-priority"}


class Resolver:
    """A command word as Yakut's click group resolves it, and the group's global options that take a value"""

    def __init__(self, resolve, value_options: set[str]):
        self.resolve, self.value_options = resolve, value_options


def yakut_resolver() -> Resolver | None:
    """Yakut's own click group, once Yakut is imported (it is only after the environment is set: it compiles the DSDL
    as it loads): None if it isn't, or doesn't look as expected, and then the static lists above stand alone"""
    try:
        import click
        group = sys.modules["yakut.main"]._click_main
        ctx = click.Context(group)
    except (ImportError, KeyError, AttributeError):
        return None
    options = {o for p in group.params if isinstance(p, click.Option) and not p.is_flag and not p.count
               for o in (*p.opts, *p.secondary_opts)}

    def resolve(word: str) -> str | None:
        try:
            name, _, _ = group.resolve_command(ctx, [word])
        except click.UsageError:
            return None
        return name

    return Resolver(resolve, options)


def command_of(yakut_args: list[str], resolver: Resolver | None = None) -> tuple[str | None, list[str]]:
    """(the Yakut command, by its name in COMMANDS, or None for any other; the words after it): the first
    word that is neither a global option nor such an option's value. With Yakut's resolver, the word is also
    resolved as Yakut would, and a name in RESOLVED counts whatever its spelling"""
    value_options = GLOBAL_VALUE_OPTIONS | (resolver.value_options if resolver else set())
    skip = False
    for i, word in enumerate(yakut_args):
        if skip:
            skip = False
        elif word in value_options:
            skip = True
        elif not word.startswith("-"):
            for name, spellings in COMMANDS.items():
                if word in spellings:
                    return name, yakut_args[i + 1:]
            resolved = resolver.resolve(word) if resolver else None
            return RESOLVED.get(resolved), yakut_args[i + 1:]
    return None, []


def positionals(words: list[str]) -> list[str]:
    """The words that aren't options. Every option of `call` and `register-access` that takes a value
    is given as one word (--timeout=2) or by its short letter and then the value: those values are
    skipped too"""
    out, skip = [], False
    for word in words:
        if skip:
            skip = False
        elif word in ("-T", "--timeout", "-P", "--priority"):
            skip = True
        elif not word.startswith("-") or re.fullmatch(r"-\d+", word):
            out.append(word)
    return out


NUMBER = re.compile(r"0[xX][0-9a-fA-F_]+|0[oO][0-7_]+|0[bB][01_]+|\d[\d_]*(?:\.\d*)?(?:[eE][-+]?\d+)?")


def numbers(text: str) -> list[float]:
    """Every number in a register value as Yakut takes it (YAML, or words): 65535, 0xFFFF, 6.5535e4, 65_535"""
    out = []
    for m in NUMBER.findall(text):
        m = m.replace("_", "")
        out.append(float(int(m, 0)) if m[:2].lower() in ("0x", "0o", "0b") else float(m))
    return out


def refusal(yakut_args: list[str], resolver: Resolver | None = None) -> str | None:
    """Why this wrapper won't run these Yakut arguments, or None. Asked twice: before anything is imported or opened,
    by the static lists alone, and again once Yakut is imported, with its own click group (yakut_resolver())"""
    command, rest = command_of(yakut_args, resolver)
    if command == "execute-command":
        return ("not execute-command, which wedged the adapter here (see --help); the hardware tests send "
                "commands (tests/hardware/test_execute_command.py)")
    if command == "call":
        for word in positionals(rest)[1:2]:      # SERVER_NODE_ID SERVICE [FIELDS]
            service = word.lower()
            port = service.split(":", 1)[0]
            if service.startswith("uavcan.node.executecommand") or ":uavcan.node.executecommand" in service or \
                    (port.isdigit() and int(port) == EXECUTE_COMMAND_SERVICE):
                return (f"not a call to uavcan.node.ExecuteCommand ({word}), which wedged the adapter here as "
                        "execute-command did (see --help)")
    if command == "register-batch":
        return "not register-batch, which could write anything: one register a run, with register-access"
    if command == "register-access":
        words = positionals(rest)       # NODE_IDS REGISTER_NAME [VALUE...]
        if len(words) >= 3 and words[1] == "uavcan.node.id" and 65535 in numbers(" ".join(words[2:])):
            return ("not uavcan.node.id 65535 (plug-and-play): the node would come back from the closing "
                    "`cyphal restart` asking for a node-ID, never quiet, and the adapter can't be closed safely. "
                    "Use `cyphal id auto` on the console, with the adapter closed")
    return None


def board_status(status: str) -> tuple[int, int] | None:
    """(the node-ID, the bitrate) from `cyphal` while the node runs with a fixed node-ID; None otherwise"""
    m = FIXED_STATUS.match(status.strip())
    if m is None:
        return None
    return int(m.group(1) or m.group(2)), int(m.group(3)) * BITRATE_UNITS[m.group(4)]


def adapter_back(timeout: float) -> bool:
    """Whether a gs_usb adapter is on USB again within `timeout`, after the time it takes to go and come
    back (board_console.ADAPTER_REOPEN_S): looked for, not opened"""
    import board_console
    from gs_usb.gs_usb import GsUsb

    time.sleep(board_console.ADAPTER_REOPEN_S)
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if GsUsb.scan():
            return True
        time.sleep(0.25)
    return False


def restart_node(board, node_id: int | None = None) -> float:
    """`cyphal restart`, to its prompt (board_console.Console.quiet()): the open or close that follows falls
    inside the 2 s listen. If the node isn't listening after it and `node_id` is given, it is given that
    fixed node-ID back and restarted once more. When the restart that worked was sent"""
    t0 = time.monotonic()
    try:
        board.quiet()
        return t0
    except AssertionError as e:
        if node_id is None:
            raise SystemExit(f"yakut_can.py: `cyphal restart` didn't leave the node listening:\n{e}") from None
        log(f"`cyphal restart` didn't leave the node listening; giving it node-ID {node_id} again:\n{e}")
    out = board.command(f"cyphal id {node_id}")
    t0 = time.monotonic()
    try:
        board.quiet()
    except AssertionError as e:
        raise SystemExit(f"yakut_can.py: `cyphal id {node_id}` ({out.strip()}) and `cyphal restart` didn't leave "
                         f"the node listening:\n{e}") from None
    return t0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                     usage="%(prog)s [options] [--] <yakut arguments>")
    parser.add_argument("-p", "--port", default=os.environ.get("ESPPORT", "/dev/cu.wchusbserial5B910448341"),
                        help="the board's console (default $ESPPORT, or the bench's CH343P)")
    parser.add_argument("--node-id", type=int, default=10, help="Yakut's own node-ID (default 10)")
    parser.add_argument("-b", "--bitrate", type=int, default=1_000_000,
                        help="bit/s (default 1000000): the board's, which is checked before the adapter is opened")
    parser.add_argument("--flush", action="store_true",
                        help="empty the adapter for the next program afterwards (another open and close of it); "
                             "otherwise run tools/can_check.py flush before cyphal_service")
    args, yakut_args = parser.parse_known_args()
    if yakut_args[:1] == ["--"]:
        yakut_args = yakut_args[1:]
    if not yakut_args:
        parser.error("no Yakut command")
    if (why := refusal(yakut_args)) is not None:
        sys.exit(f"yakut_can.py: {why}")

    # Before Yakut is imported: it compiles the DSDL it finds on CYPHAL_PATH as it loads
    os.environ.update({
        "UAVCAN__CAN__IFACE": "gs_usb:0",
        "UAVCAN__CAN__MTU": "8",
        "UAVCAN__CAN__BITRATE": f"{args.bitrate} {args.bitrate}",
        "UAVCAN__NODE__ID": str(args.node_id),
        "CYPHAL_PATH": os.pathsep.join(str(REPO / "external" / d) for d in (
            "esp-console-kit/third_party/public_regulated_data_types", "r2_cyphal_dsdl")),
        "PYCYPHAL_PATH": str(REPO / "build" / "yakut-dsdl"),
    })
    try:
        from pycyphal.transport.can.media.pythoncan import PythonCANMedia
        from yakut import main as yakut_main
        import board_console
    except ImportError as e:
        sys.exit(f"yakut_can.py needs yakut, python-can, gs_usb and pyserial in this Python ({sys.executable}): {e}")
    # Again, as Yakut itself reads the words (its click version names commands its own way), before the board or the
    # adapter is touched
    if (why := refusal(yakut_args, yakut_resolver())) is not None:
        sys.exit(f"yakut_can.py: {why}")

    board = board_console.Console(args.port)
    try:
        return run(board, args, yakut_args, PythonCANMedia, yakut_main)
    finally:
        board.close()


def run(board, args, yakut_args: list[str], PythonCANMedia, yakut_main) -> int:
    import can_check
    import usb.core

    status = board.command("cyphal")
    found = board_status(status)
    if found is None:
        print(f"yakut_can.py: the board's node must be running with a fixed node-ID (`cyphal id 52`, "
              f"`cyphal restart`); it says:\n{status}", file=sys.stderr)
        return 1
    node_id, bitrate = found
    if bitrate != args.bitrate:
        print(f"yakut_can.py: the board runs at {bitrate} bit/s, not {args.bitrate} (--bitrate): the adapter would "
              f"send unacknowledged frames, which it can't stand", file=sys.stderr)
        return 1

    can_check.patch_gs_usb_for_macos()
    # Hooked in pycyphal's media rather than python-can's bus: its close runs while pycyphal's reader still
    # reads the adapter, as the hardware tests' Net.close() does
    original_init, original_close = PythonCANMedia.__init__, PythonCANMedia.close

    def init(self, *a, **kw):
        t0 = restart_node(board)
        mark = board.mark()
        original_init(self, *a, **kw)
        log(f"adapter open {time.monotonic() - t0:.2f} s into the node's listen (2 s)")
        # The board answers nothing until the listen is over: Yakut's first request waits for that
        if not board.wait_for(ONLINE_LINE, mark, 4.0):
            log("the node didn't say it was online: going on")

    def close(self):
        # Closed whatever happens: a failure here would otherwise leave the adapter to the end of the
        # process, closed then while the board sends
        try:
            t0 = restart_node(board, node_id)
        except BaseException:
            original_close(self)
            log("adapter closed without a quiet moment from the board: check it (tools/can_check.py flush)")
            raise
        original_close(self)
        log(f"adapter closed {time.monotonic() - t0:.2f} s into the node's listen (2 s)")
        if not args.flush:
            log(FLUSH_REMINDER.replace("the adapter wasn't emptied", "not emptied (no --flush)"))
            return
        # It resets as it closes and leaves USB for a moment: once it is back, emptied for the next program in
        # a listen of its own (opening it is a reset too)
        for attempt in range(3):
            if not adapter_back(10.0):
                break
            t0 = restart_node(board, node_id)
            try:
                can_check.flush(args.bitrate)
            except usb.core.USBError as e:     # gone again as it was opened: nothing was sent
                if e.errno != 19:
                    raise
                log(f"the adapter left USB as it was opened ({attempt + 1}): again")
                continue
            log(f"adapter emptied {time.monotonic() - t0:.2f} s into the node's listen")
            return
        log(FLUSH_REMINDER)

    PythonCANMedia.__init__ = init
    PythonCANMedia.close = close

    signal.signal(signal.SIGINT, signal.default_int_handler)
    sys.argv = ["yakut", *yakut_args]
    try:
        yakut_main()
    except SystemExit as e:
        return int(e.code or 0) if not isinstance(e.code, str) else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
