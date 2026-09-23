#!/usr/bin/env python3
"""Vendor the documentation site's third-party JavaScript, and check what is vendored.

The installer page flashes the board from the browser with esptool-js. It has to work from the
published site and from the offline documentation bundle in a building with no internet, so
nothing is loaded from a CDN: the library is committed under manual/javascripts/vendor/, and
this script is the only way it gets there.

  * `update` downloads each pinned package from the npm registry, refuses it unless it matches
    the sha512 integrity recorded below (the same value npm puts in a lockfile), extracts the
    listed files, and rewrites vendor/SHA256SUMS. Run it only to change a version, after
    updating VENDORED.
  * `--check` (the default; `make docs-check`) needs no network. Every vendored file must match
    SHA256SUMS, nothing may be missing or extra, and no vendored script may reach out to the
    network with an absolute http(s) import, fetch or src -- which would break the air-gapped
    bundle without failing any build.

Standard library only.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import io
import re
import sys
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VENDOR = ROOT / "manual" / "javascripts" / "vendor"
SUMS = VENDOR / "SHA256SUMS"

# (package, version, npm dist.integrity, {path in the tarball: path under vendor/}).
# `npm view <package>@<version> dist.integrity` gives the integrity value.
VENDORED = [
    (
        "esptool-js",
        "0.7.0",
        "sha512-9wHuW/s9t4Vs6HKcr3pra3gbRVWwaN0eabMWodWH4KEJHEYZxgmWwZ3s0F4xcWVCZOc/L98bGZ4uWoZvR8KBKA==",
        {
            "package/bundle.js": "esptool-js/bundle.js",
            "package/LICENSE": "esptool-js/LICENSE",
        },
    ),
]

# A script that loads anything by absolute URL would fail silently offline. Comments (licence
# headers, source links) are fine; these are the forms that actually fetch.
NETWORK_RE = re.compile(
    r"""(?:\bimport\s*\(?\s*|\bfrom\s*|\bfetch\s*\(\s*|\bsrc\s*=\s*|\bimportScripts\s*\(\s*)["'`]https?://""",
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def update() -> int:
    for package, version, integrity, files in VENDORED:
        url = f"https://registry.npmjs.org/{package}/-/{package}-{version}.tgz"
        print(f"fetching {package} {version}")
        with urllib.request.urlopen(url, timeout=60) as response:
            tarball = response.read()
        algorithm, _, expected = integrity.partition("-")
        actual = base64.b64encode(hashlib.new(algorithm, tarball).digest()).decode()
        if actual != expected:
            sys.exit(f"error: {package} {version} does not match its recorded integrity")
        with tarfile.open(fileobj=io.BytesIO(tarball), mode="r:gz") as archive:
            for member, target in files.items():
                source = archive.extractfile(member)
                if source is None:
                    sys.exit(f"error: {package} {version} has no {member}")
                destination = VENDOR / target
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(source.read())
                print(f"  {target}")

    lines = [f"{sha256(VENDOR / target)}  {target}\n"
             for _, _, _, files in VENDORED for target in files.values()]
    SUMS.write_text("".join(sorted(lines, key=lambda line: line.split()[1])), encoding="utf-8")
    print(f"wrote {SUMS.relative_to(ROOT)}")
    return 0


def check() -> int:
    if not SUMS.is_file():
        sys.exit(f"error: no {SUMS.relative_to(ROOT)}; run tools/vendor_js.py update")
    listed = {}
    for line in SUMS.read_text(encoding="utf-8").splitlines():
        digest, _, name = line.partition("  ")
        listed[name] = digest

    expected = {target for _, _, _, files in VENDORED for target in files.values()}
    present = {str(path.relative_to(VENDOR)) for path in VENDOR.rglob("*")
               if path.is_file() and path != SUMS}

    errors = []
    for name in sorted(expected - listed.keys()):
        errors.append(f"{name}: vendored but not in SHA256SUMS")
    for name in sorted(listed.keys() - expected):
        errors.append(f"{name}: in SHA256SUMS but not in VENDORED")
    for name in sorted(listed.keys() - present):
        errors.append(f"{name}: missing")
    for name in sorted(present - listed.keys()):
        errors.append(f"{name}: not vendored by this script")
    for name in sorted(listed.keys() & present):
        path = VENDOR / name
        if sha256(path) != listed[name]:
            errors.append(f"{name}: does not match SHA256SUMS (edited by hand?)")
        if path.suffix == ".js" and NETWORK_RE.search(path.read_text(encoding="utf-8", errors="replace")):
            errors.append(f"{name}: loads something by absolute URL, which fails offline")

    for error in errors:
        print(f"error: vendor/{error}", file=sys.stderr)
    if errors:
        return 1
    print(f"vendored JavaScript: {len(listed)} files, all match")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("action", nargs="?", choices=["check", "update"], default="check")
    parser.add_argument("--check", dest="action", action="store_const", const="check")
    args = parser.parse_args()
    return update() if args.action == "update" else check()


if __name__ == "__main__":
    sys.exit(main())
