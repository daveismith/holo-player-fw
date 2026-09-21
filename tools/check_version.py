#!/usr/bin/env python3
"""Validate a release tag, and derive the documentation version from it.

The firmware carries no version constant. ESP-IDF sets PROJECT_VER from
`git describe --always --tags --dirty`, so on a tagged commit the tag *is* the version baked
into the image (see RELEASING.md). That makes the tag the single source of truth, and this
script the one place the release contract is checked:

  * the tag is shaped vMAJOR.MINOR.PATCH, optionally -rcN;
  * the tag is actually on the commit being built, so `git describe` will produce it rather
    than a bare hash;
  * the resulting version fits the firmware's version field. esp_app_desc_t.version is
    char[32] and ESP-IDF asserts the fit at compile time (esp_app_desc.c), so an over-long
    tag breaks the build rather than being truncated.

It then emits the docs version for mike: 0.4 for a release, 0.4-rc for a pre-release. A patch
release updates its minor's documentation in place, which is why the patch number is dropped.

Usage:
    check_version.py <tag>            validate, and write GitHub outputs
    check_version.py --notes <tag>    print that version's CHANGELOG.md section

Standard library only.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CHANGELOG = ROOT / "CHANGELOG.md"

TAG_RE = re.compile(r"^v(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)(?:-rc(?P<rc>\d+))?$")

# esp_app_desc_t.version is char[32]; 31 characters plus the terminator.
VERSION_MAX = 31


def parse_tag(tag: str) -> tuple[str, bool]:
    """Return (docs_version, prerelease), or exit with a diagnostic."""
    match = TAG_RE.match(tag)
    if not match:
        sys.exit(
            f"error: tag {tag!r} is not vMAJOR.MINOR.PATCH or vMAJOR.MINOR.PATCH-rcN\n"
            "       examples: v0.1.0, v0.2.1, v0.2.0-rc1"
        )
    if len(tag) > VERSION_MAX:
        sys.exit(
            f"error: tag {tag!r} is {len(tag)} characters; the firmware version field holds "
            f"{VERSION_MAX}.\n"
            "       ESP-IDF asserts this at compile time, so the build would fail."
        )
    prerelease = match.group("rc") is not None
    docs_version = f"{match.group('major')}.{match.group('minor')}"
    if prerelease:
        docs_version += "-rc"
    return docs_version, prerelease


def check_tag_is_on_head(tag: str) -> None:
    """`git describe` must yield exactly this tag, or the image gets the wrong version."""
    try:
        described = subprocess.run(
            ["git", "describe", "--exact-match", "--tags", "HEAD"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
    except FileNotFoundError:
        sys.exit("error: git not found")
    except subprocess.CalledProcessError:
        sys.exit(
            f"error: HEAD is not tagged {tag}.\n"
            "       `git describe` would fall back to a bare commit hash and the release would\n"
            "       carry an unversioned image. In CI this usually means the checkout was\n"
            "       shallow: set fetch-depth: 0 so the tags are present."
        )
    if described != tag:
        sys.exit(f"error: HEAD is tagged {described!r}, but the release is for {tag!r}")


def changelog_section(tag: str) -> str:
    """The CHANGELOG.md body for this version, for use as release notes."""
    if not CHANGELOG.is_file():
        sys.exit(f"error: no {CHANGELOG.name}")
    version = tag.lstrip("v")
    text = CHANGELOG.read_text(encoding="utf-8")
    # Headings look like `## [0.1.0] - 2026-09-20`; accept the version with or without brackets.
    pattern = re.compile(
        rf"^##\s+\[?{re.escape(version)}\]?.*?$(.*?)(?=^##\s|\Z)",
        re.S | re.M,
    )
    match = pattern.search(text)
    if not match:
        sys.exit(f"error: no section for {version} in {CHANGELOG.name}")
    body = match.group(1).strip()
    if not body:
        sys.exit(f"error: the {version} section of {CHANGELOG.name} is empty")
    return body


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--notes", action="store_true", help="print the CHANGELOG section instead")
    parser.add_argument("tag", help="release tag, e.g. v0.1.0")
    args = parser.parse_args()

    if args.notes:
        print(changelog_section(args.tag))
        return 0

    docs_version, prerelease = parse_tag(args.tag)
    check_tag_is_on_head(args.tag)

    print(f"tag {args.tag}: docs version {docs_version}, prerelease {str(prerelease).lower()}")

    github_output = os.environ.get("GITHUB_OUTPUT")
    if github_output:
        with open(github_output, "a", encoding="utf-8") as handle:
            handle.write(f"docs_version={docs_version}\n")
            handle.write(f"prerelease={str(prerelease).lower()}\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())
