#!/usr/bin/env python3
"""Regenerate the C headers for the Cyphal data types the node uses, in components/cyphal/dsdl/.

The headers are generated here by Nunavut and committed, so that the firmware's build (and the
release workflow's `v6.1` image, and a clean checkout) needs no Python packages. Run this after
adding a type to TYPES, or after moving the DSDL submodule:

    tools/gen_dsdl.py [--nnvg PATH]

What the committed headers were generated with:

    nunavut   2.3.1 (`nnvg`)
    DSDL      OpenCyphal/public_regulated_data_types a229bb78e76c48a3082be163bc240b2c13ff2d89
              ("Extend uavcan si angle and length types (#150)", 2025-08-03), the submodule at
              external/public_regulated_data_types. The hardware tests (tests/hardware/) compile
              the same tree for pycyphal.

The script refuses a different DSDL revision or nunavut version unless told otherwise
(--any-revision, --any-nunavut), so that the headers and the tests cannot drift apart unseen.

nnvg 2.3.1 generates a whole root namespace, so the uavcan namespace is generated into a temporary
directory and only the types in TYPES, the headers they include, and Nunavut's serialization
support header are copied out. Two things nnvg writes into every file are taken out so that the
same inputs give the same bytes: the absolute path of the source file (made relative to this
repository) and the time and machine of the run.

Needs nunavut (`pip install nunavut==2.3.1`); nothing else.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DSDL = ROOT / "external" / "public_regulated_data_types"
OUT = ROOT / "components" / "cyphal" / "dsdl"

DSDL_REVISION = "a229bb78e76c48a3082be163bc240b2c13ff2d89"
NUNAVUT_VERSION = "2.3.1"

# The types the node uses (phase 4's plug-and-play uses NodeIDAllocationData); their
# dependencies follow from the #include lines. The firmware zero-initialises these structs rather
# than calling Nunavut's generated <type>_initialize_(): that deserializes a one-byte buffer, and
# GCC 15 raises false array-bounds errors in it (-Werror makes them fatal).
TYPES = [
    "uavcan/node/Heartbeat_1_0.h",
    "uavcan/node/GetInfo_1_0.h",
    "uavcan/pnp/NodeIDAllocationData_1_0.h",
]
SUPPORT = "nunavut/support/serialization.h"

INCLUDE_RE = re.compile(r"^#include <([^>]+)>", re.M)
# The run's time and machine: "// Generated at: ..." and the "// Platform" block under it
VOLATILE_RE = re.compile(r"^// Generated at:.*\n|^// Platform\n(?://     \S.*\n)+//\n", re.M)


def run(argv: list[str]) -> str:
    return subprocess.run(argv, check=True, capture_output=True, text=True).stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--nnvg", default=shutil.which("nnvg") or "nnvg", help="the nnvg to run (default: from PATH)")
    parser.add_argument("--any-revision", action="store_true", help="accept a DSDL checkout at another revision")
    parser.add_argument("--any-nunavut", action="store_true", help=f"accept a nunavut other than {NUNAVUT_VERSION}")
    args = parser.parse_args()

    if not (DSDL / "uavcan").is_dir():
        sys.exit(f"no DSDL at {DSDL.relative_to(ROOT)}: git submodule update --init external/public_regulated_data_types")
    revision = run(["git", "-C", str(DSDL), "rev-parse", "HEAD"])
    if revision != DSDL_REVISION and not args.any_revision:
        sys.exit(f"the DSDL is at {revision}, not {DSDL_REVISION} (--any-revision to go on, then update this script)")
    version = run([args.nnvg, "--version"])
    if version != NUNAVUT_VERSION and not args.any_nunavut:
        sys.exit(f"nnvg is {version}, not {NUNAVUT_VERSION} (--any-nunavut to go on, then update this script)")

    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([args.nnvg, "--target-language", "c", "--language-standard", "c11", "--outdir", tmp,
                        str(DSDL / "uavcan")], check=True)
        generated = Path(tmp)
        # A name asked for that nnvg didn't write is a mistake (a typo, a type moved or renamed),
        # found before anything is deleted. Only a name from an #include line may be another
        # header (<stdint.h> and the like), and is skipped.
        missing = [name for name in [*TYPES, SUPPORT] if not (generated / name).is_file()]
        if missing:
            sys.exit(f"nnvg wrote no {', '.join(missing)}: check TYPES and SUPPORT in this script "
                     f"({OUT.relative_to(ROOT)} left as it was)")
        wanted, todo = set(), [*TYPES, SUPPORT]
        while todo:
            name = todo.pop()
            if name in wanted:
                continue
            wanted.add(name)
            todo.extend(include for include in INCLUDE_RE.findall((generated / name).read_text())
                        if (generated / include).is_file())

        if OUT.exists():
            shutil.rmtree(OUT)
        prefix = f"{ROOT}/"
        for name in sorted(wanted):
            text = VOLATILE_RE.sub("", (generated / name).read_text()).replace(prefix, "")
            dest = OUT / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(text)
            print(f"  {dest.relative_to(ROOT)}")
    print(f"{len(wanted)} headers from DSDL {revision[:7]}, nunavut {version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
