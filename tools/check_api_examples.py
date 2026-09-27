#!/usr/bin/env python3
"""Every JSON example in the HTTP API's OpenAPI description matches its schema.

openapi-spec-validator checks that manual/reference/openapi.json is OpenAPI 3.1, but not that
the examples in it -- what the manual, the explorer and Swagger Editor show -- are what the
schemas say. This validates each request and response example, and each shared response's,
against its schema with jsonschema (Draft 2020-12, OpenAPI 3.1's dialect).

Needs the docs venv (jsonschema comes with openapi-spec-validator): `make api-validate`.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

from jsonschema import Draft202012Validator
from referencing import Registry, Resource
from referencing.jsonschema import DRAFT202012

ROOT = Path(__file__).resolve().parent.parent
SPEC = ROOT / "manual" / "reference" / "openapi.json"
BASE = "urn:holo-api"


def main() -> int:
    doc = json.loads(SPEC.read_text(encoding="utf-8"))
    registry = Registry().with_resource(BASE, Resource.from_contents(doc, default_specification=DRAFT202012))
    problems: list[str] = []

    def check(schema: dict, value, where: str) -> None:
        if "$ref" in schema:
            schema = {"$ref": BASE + schema["$ref"]}
        for e in Draft202012Validator(schema, registry=registry).iter_errors(value):
            at = "/".join(str(p) for p in e.absolute_path) or "(the example)"
            problems.append(f"{where}: {at}: {e.message}")

    def media(node: dict, where: str) -> None:
        m = node.get("content", {}).get("application/json")
        if not m or "schema" not in m:
            return
        if "example" in m:
            check(m["schema"], m["example"], where)
        for name, ex in m.get("examples", {}).items():
            check(m["schema"], ex["value"], f"{where} [{name}]")

    for path, item in doc["paths"].items():
        for method, op in item.items():
            where = f"{method.upper()} {path}"
            if "requestBody" in op and "$ref" not in op["requestBody"]:
                media(op["requestBody"], f"{where} request")
            for code, response in op.get("responses", {}).items():
                if "$ref" not in response:
                    media(response, f"{where} {code}")
    for kind in ("responses", "requestBodies"):
        for name, node in doc["components"].get(kind, {}).items():
            media(node, f"components/{kind}/{name}")

    for p in problems:
        print(f"error: {p}", file=sys.stderr)
    print(f"{SPEC.relative_to(ROOT)}: examples {'do not all match' if problems else 'match'} their schemas")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
