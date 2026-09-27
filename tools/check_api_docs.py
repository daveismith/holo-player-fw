#!/usr/bin/env python3
"""The HTTP API's OpenAPI description matches the firmware, and the manual covers it.

manual/reference/openapi.json is the one description of the board's HTTP API: the documentation
site renders it (reference/http-api-explorer.md), and the firmware embeds it and serves it at
/api/v1/openapi.json (components/webui). Nothing generates it, so this is what keeps it honest,
as check_command_docs.py does for the console commands. It fails when:

  * the document is not OpenAPI 3.1 as this project writes it: every operation has an
    operationId (unique), a tag, and a 2xx response; every mutating operation (PUT, POST,
    DELETE) declares its security; every $ref resolves;
  * a route the firmware registers -- web_register("/api/v1/...", HTTP_GET, ...) in
    components/ or the esp-console-kit submodule -- is missing from the document, or the
    document describes one the firmware does not register;
  * an operation is marked `x-planned: true` (described ahead of the firmware, for review)
    but the firmware registers it -- the mark is stale -- or, with --final, any is marked;
  * a path is never mentioned in manual/reference/http-api.md.

A full schema validation (openapi-spec-validator, in the docs venv) is `make api-validate`.

Standard library only, so `make docs-check` runs without the docs venv.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SPEC = ROOT / "manual" / "reference" / "openapi.json"
GUIDE = ROOT / "manual" / "reference" / "http-api.md"
SUBMODULE = Path("external/esp-console-kit")
SOURCE_DIRS = [Path("main"), Path("components"), SUBMODULE]

ROUTE_RE = re.compile(r'web_register\(\s*"([^"]+)"\s*,\s*HTTP_([A-Z]+)')
METHODS = {"get", "put", "post", "delete", "patch", "head", "options"}
MUTATING = {"put", "post", "delete", "patch"}


def registered_routes() -> tuple[dict[tuple[str, str], str], bool]:
    """(path, method) -> where the firmware registers it. Also whether the kit is present."""
    found: dict[tuple[str, str], str] = {}
    present = (ROOT / SUBMODULE / ".git").exists() or any((ROOT / SUBMODULE).glob("*/*.c"))
    for rel in SOURCE_DIRS:
        base = ROOT / rel
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*.c")):
            text = path.read_text(encoding="utf-8", errors="replace")
            for m in ROUTE_RE.finditer(text):
                line = text.count("\n", 0, m.start()) + 1
                found[(m.group(1), m.group(2).lower())] = f"{path.relative_to(ROOT)}:{line}"
    return found, present


def resolve(doc: dict, ref: str):
    if not ref.startswith("#/"):
        raise KeyError(ref)
    node = doc
    for part in ref[2:].split("/"):
        node = node[part.replace("~1", "/").replace("~0", "~")]
    return node


def refs(node, trail="#"):
    if isinstance(node, dict):
        for key, value in node.items():
            if key == "$ref" and isinstance(value, str):
                yield trail, value
            else:
                yield from refs(value, f"{trail}/{key}")
    elif isinstance(node, list):
        for i, value in enumerate(node):
            yield from refs(value, f"{trail}/{i}")


def structure_problems(doc: dict) -> list[str]:
    problems = []
    if not str(doc.get("openapi", "")).startswith("3.1"):
        problems.append(f"openapi is {doc.get('openapi')!r}, not 3.1.x")
    for key in ("info", "paths", "components"):
        if key not in doc:
            problems.append(f"no `{key}`")
    ids: dict[str, str] = {}
    tags = {t["name"] for t in doc.get("tags", [])}
    for path, item in doc.get("paths", {}).items():
        for method, op in item.items():
            if method not in METHODS:
                continue
            where = f"{method.upper()} {path}"
            op_id = op.get("operationId")
            if not op_id:
                problems.append(f"{where}: no operationId")
            elif op_id in ids:
                problems.append(f"{where}: operationId {op_id} is also {ids[op_id]}'s")
            else:
                ids[op_id] = where
            if not op.get("tags"):
                problems.append(f"{where}: no tag")
            for tag in op.get("tags", []):
                if tag not in tags:
                    problems.append(f"{where}: tag {tag!r} is not in the top-level tags")
            responses = op.get("responses", {})
            if not any(str(code).startswith("2") for code in responses):
                problems.append(f"{where}: no 2xx response")
            if method in MUTATING and "security" not in op:
                problems.append(f"{where}: changes something, but declares no security")
    for trail, ref in refs(doc):
        try:
            resolve(doc, ref)
        except (KeyError, TypeError):
            problems.append(f"{trail}: $ref {ref} does not resolve")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--final", action="store_true",
                        help="fail if any operation is still marked x-planned")
    args = parser.parse_args()
    try:
        doc = json.loads(SPEC.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as e:
        print(f"error: {SPEC.relative_to(ROOT)}: {e}", file=sys.stderr)
        return 2

    failed = False
    problems = structure_problems(doc)
    if problems:
        failed = True
        print(f"{SPEC.relative_to(ROOT)} is not well formed:", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)

    described = {(path, method) for path, item in doc["paths"].items() for method in item if method in METHODS}
    planned = {(path, method) for path, method in described if doc["paths"][path][method].get("x-planned")}
    routes, present = registered_routes()
    if not routes:
        print("error: no web_register() routes found; has the registration style changed?", file=sys.stderr)
        return 2
    undescribed = sorted(set(routes) - described)
    for path, method in undescribed:
        print(f"error: {method.upper()} {path} is registered at {routes[(path, method)]} but not in "
              f"{SPEC.relative_to(ROOT)}", file=sys.stderr)
    # Without the submodule, most routes are not visible: only check that direction with it.
    unregistered = sorted(described - set(routes) - planned) if present else []
    for path, method in unregistered:
        print(f"error: {method.upper()} {path} is in {SPEC.relative_to(ROOT)} but the firmware does not register it"
              " (mark it x-planned if it is still to come)", file=sys.stderr)
    stale = sorted(planned & set(routes))
    for path, method in stale:
        print(f"error: {method.upper()} {path} is registered at {routes[(path, method)]} but still marked "
              "x-planned: remove the mark", file=sys.stderr)
    unfinished = sorted(planned) if args.final else []
    for path, method in unfinished:
        print(f"error: {method.upper()} {path} is still marked x-planned", file=sys.stderr)
    failed |= bool(undescribed or unregistered or stale or unfinished)

    guide = GUIDE.read_text(encoding="utf-8") if GUIDE.is_file() else ""
    unmentioned = sorted(p for p in doc["paths"] if p not in guide)
    for path in unmentioned:
        print(f"error: {path} is never mentioned in {GUIDE.relative_to(ROOT)}", file=sys.stderr)
    failed |= bool(unmentioned)

    print(f"{len(described)} API operations described ({len(planned)} planned), {len(routes)} registered"
          + ("" if present else f" ({SUBMODULE} not checked out: its routes are not covered)"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
