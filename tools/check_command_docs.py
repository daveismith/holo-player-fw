#!/usr/bin/env python3
"""Every console command the firmware registers must be documented in manual/.

magicpanel generates its reference pages from the firmware image; there is no simulator here, so
these pages are hand-written and this check is what keeps them honest. It fails when a command is
registered in the source but never mentioned in the manual -- the usual cause being a command
added to a component without a matching row in manual/reference/console.md.

A command counts as documented only when its name appears *inside backticks*, in an inline code
span or a fenced block. Matching bare prose would pass vacuously: `ip`, `free`, `top`, `version`
and `wifi` are all ordinary English words that occur in ordinary sentences.

Roughly half the command surface lives in the esp-console-kit submodule. When it is absent (a
clone without --recursive) the kit's commands simply are not checked, and the summary says so,
rather than the check failing for a reason that has nothing to do with the docs.

Standard library only, so `make docs-check` runs without the docs venv.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANUAL = ROOT / "manual"
SUBMODULE = Path("external/esp-console-kit")

# Where commands are registered. The submodule is listed last so the summary can report it
# separately when it is missing.
SOURCE_DIRS = [Path("main"), Path("components"), SUBMODULE]
SOURCE_SUFFIXES = {".c", ".cpp"}

# Both registration styles in this tree:
#   .command = "lcd",        designated initialiser  (components/board_ws128/cmd_board.c)
#   cmd.command = "video";   member assignment       (components/video/video_player.cpp)
COMMAND_RE = re.compile(r'\.command\s*=\s*"([^"]+)"')

# Fenced blocks first, then inline spans in what is left, so a backtick inside a fence is not
# mistaken for an inline span.
FENCE_RE = re.compile(r"```[^\n]*\n(.*?)```", re.S)
INLINE_RE = re.compile(r"`([^`\n]+)`")
# Command names use letters, digits, underscores and hyphens: flash-stats, deep_sleep, log_level.
TOKEN_RE = re.compile(r"[A-Za-z0-9_-]+")

# Commands deliberately left out of the manual. Add an entry only with a reason.
KNOWN_UNDOCUMENTED: dict[str, str] = {}


def registered_commands() -> tuple[dict[str, list[str]], bool]:
    """Map each registered command to where it is registered. Also report whether the kit is here."""
    found: dict[str, list[str]] = {}
    submodule_present = (ROOT / SUBMODULE / ".git").exists() or any(
        (ROOT / SUBMODULE).glob("*/*.c")
    )
    for rel in SOURCE_DIRS:
        base = ROOT / rel
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES:
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for match in COMMAND_RE.finditer(text):
                line = text.count("\n", 0, match.start()) + 1
                where = f"{path.relative_to(ROOT)}:{line}"
                found.setdefault(match.group(1), []).append(where)
    return found, submodule_present


def documented_tokens() -> set[str]:
    """Every word appearing inside backticks anywhere in manual/."""
    tokens: set[str] = set()
    for path in sorted(MANUAL.rglob("*.md")):
        text = path.read_text(encoding="utf-8", errors="replace")
        outside: list[str] = []
        pos = 0
        for match in FENCE_RE.finditer(text):
            outside.append(text[pos : match.start()])
            tokens.update(TOKEN_RE.findall(match.group(1)))
            pos = match.end()
        outside.append(text[pos:])
        for chunk in outside:
            for match in INLINE_RE.finditer(chunk):
                tokens.update(TOKEN_RE.findall(match.group(1)))
    return tokens


def main() -> int:
    if not MANUAL.is_dir():
        print(f"error: no manual/ directory at {MANUAL}", file=sys.stderr)
        return 2

    commands, submodule_present = registered_commands()
    if not commands:
        print("error: no console commands found; has the registration style changed?", file=sys.stderr)
        return 2

    tokens = documented_tokens()
    missing = sorted(
        name
        for name in commands
        if name not in tokens and name not in KNOWN_UNDOCUMENTED
    )

    checked = len(commands)
    print(f"{checked} registered commands, {checked - len(missing)} documented in manual/")
    if not submodule_present:
        print(
            f"note: {SUBMODULE} is not checked out, so its commands are not covered; "
            "run `git submodule update --init` for the full check"
        )
    if KNOWN_UNDOCUMENTED:
        for name, why in sorted(KNOWN_UNDOCUMENTED.items()):
            print(f"note: `{name}` deliberately undocumented ({why})")

    if missing:
        print(f"\n{len(missing)} command(s) registered but not documented in manual/:", file=sys.stderr)
        for name in missing:
            where = ", ".join(commands[name])
            print(f"  `{name}`  registered at {where}", file=sys.stderr)
        print(
            "\nAdd each to manual/ inside backticks -- a row in the reference table is enough --\n"
            "or, if it is deliberately undocumented, to KNOWN_UNDOCUMENTED in this script.",
            file=sys.stderr,
        )
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
