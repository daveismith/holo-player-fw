#!/usr/bin/env python3
"""Exercise a board's HTTP API and check every reply against the OpenAPI description.

    tools/api_smoke.py [--host holo-xxxx.local] [--password PW] [--write]

Each call's JSON reply is validated against the schema manual/reference/openapi.json gives
for that operation and status, so a firmware that drifts from its description fails here
even when every call "works". By default only reads, and requests the board must refuse (a
bad path, a missing file); --write also changes things, all under /test-api on the volume,
and puts back what it touched: files uploaded, moved, copied and deleted; a scene saved,
applied and deleted; the LEDs set and turned off.

Needs the docs venv (jsonschema comes with openapi-spec-validator): `make api-smoke
HOST=...`, or .venv/bin/python tools/api_smoke.py.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

from jsonschema import Draft202012Validator
from referencing import Registry, Resource
from referencing.jsonschema import DRAFT202012

ROOT = Path(__file__).resolve().parent.parent
SPEC = ROOT / "manual" / "reference" / "openapi.json"
BASE = "urn:holo-api"


class Board:
    def __init__(self, host: str, password: str | None):
        self.base = f"http://{host}/api/v1"
        self.auth = None
        if password:
            self.auth = "Basic " + base64.b64encode(f"api:{password}".encode()).decode()

    def call(self, method: str, path: str, query: dict | None = None, body=None, raw: bytes | None = None,
             timeout: float = 60) -> tuple[int, object]:
        url = self.base + path
        if query:
            url += "?" + urllib.parse.urlencode(query)
        headers = {}
        data = None
        if raw is not None:
            data = raw
            headers["Content-Type"] = "application/octet-stream"
        elif body is not None:
            data = json.dumps(body).encode()
            headers["Content-Type"] = "application/json"
        if self.auth:
            headers["Authorization"] = self.auth
        req = urllib.request.Request(url, data=data, method=method, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                status, payload, ctype = r.status, r.read(), r.headers.get("Content-Type", "")
        except urllib.error.HTTPError as e:
            status, payload, ctype = e.code, e.read(), e.headers.get("Content-Type", "")
        if "json" in ctype and payload:
            return status, json.loads(payload)
        return status, payload


class Checker:
    def __init__(self, board: Board):
        self.board = board
        self.doc = json.loads(SPEC.read_text(encoding="utf-8"))
        self.registry = Registry().with_resource(BASE, Resource.from_contents(self.doc, default_specification=DRAFT202012))
        self.failures: list[str] = []
        self.calls = 0

    def schema_for(self, path: str, method: str, status: int):
        op = self.doc["paths"].get("/api/v1" + path, {}).get(method.lower())
        if op is None:
            return None, f"{method} {path} is not in the description"
        resp = op.get("responses", {}).get(str(status))
        if resp is None:
            return None, f"{method} {path} answered {status}, which the description does not list"
        if "$ref" in resp:
            name = resp["$ref"].rsplit("/", 1)[1]
            resp = self.doc["components"]["responses"][name]
        schema = resp.get("content", {}).get("application/json", {}).get("schema")
        return schema, None

    def check(self, method: str, path: str, expect: int | tuple, query=None, body=None, raw=None, code=None,
              timeout: float = 60):
        """Call, and fail unless the status is `expect` (and the error `code`, if given) and the reply
        matches the schema for it. Returns the reply."""
        self.calls += 1
        status, reply = self.board.call(method, path, query, body, raw, timeout)
        where = f"{method} {path}" + (f"?{urllib.parse.urlencode(query)}" if query else "")
        expected = expect if isinstance(expect, tuple) else (expect,)
        if status not in expected:
            self.failures.append(f"{where}: {status}, not {expect}: {reply!r:.200}")
            print(f"  FAIL {where}: {status}")
            return reply
        if code is not None and (not isinstance(reply, dict) or reply.get("error") != code):
            self.failures.append(f"{where}: error {reply!r:.120}, not {code}")
        schema, problem = self.schema_for(path, method, status)
        if problem:
            self.failures.append(f"{where}: {problem}")
        elif schema is not None and isinstance(reply, (dict, list)):
            if "$ref" in schema:
                schema = {"$ref": BASE + schema["$ref"]}
            errors = list(Draft202012Validator(schema, registry=self.registry).iter_errors(reply))
            for e in errors[:3]:
                at = "/".join(str(p) for p in e.absolute_path) or "(the reply)"
                self.failures.append(f"{where}: {at}: {e.message}")
        print(f"  {'ok  ' if not problem else 'FAIL'} {where}: {status}")
        return reply


def reads(c: Checker) -> None:
    print("reading")
    for path in ("/info", "/ota", "/fs", "/screen", "/leds", "/holo", "/servos", "/scenes", "/settings",
                 "/network", "/web"):
        c.check("GET", path, 200)
    c.check("GET", "/fs/list", 200, {"path": "/"})
    media = c.check("GET", "/media", 200)
    for item in (media.get("items", []) if isinstance(media, dict) else [])[:3]:
        c.check("GET", "/media/info", 200, {"path": item["path"]})
    print("refusing")
    c.check("GET", "/fs/list", 400, {"path": "/a/../b"}, code="bad_path")
    c.check("GET", "/fs/list", 400, {"path": "relative"}, code="bad_path")
    c.check("GET", "/fs/entry", 404, {"path": "/no/such/file"}, code="not_found")
    c.check("GET", "/media/info", 404, {"path": "/no-such.mov"}, code="not_found")
    c.check("POST", "/screen/show", 404, body={"path": "/no-such.mov"}, code="not_found")
    c.check("POST", "/screen/show", 400, body={"colour": "mauve"}, code="bad_request")
    c.check("PATCH", "/leds", 400, body={"mode": "disco"}, code="bad_request")
    c.check("POST", "/holo/motion", 400, body={"motion": "move", "x": 500, "y": 0}, code="bad_request")
    c.check("POST", "/scenes/apply", 404, body={"name": "no such scene"}, code="unknown_scene")
    c.check("PATCH", "/settings", 400, body={"volume": 11}, code="bad_request")


def writes(c: Checker) -> None:
    print("changing (under /test-api)")
    data = os.urandom(50_000)
    sha = hashlib.sha256(data).hexdigest()
    c.check("DELETE", "/fs/entry", (204, 404), {"path": "/test-api", "recursive": "true"})
    c.check("POST", "/fs/mkdir", 201, body={"path": "/test-api/a", "parents": True})
    c.check("POST", "/fs/mkdir", 409, body={"path": "/test-api/a"}, code="exists")
    c.check("PUT", "/fs/file", 201, {"path": "/test-api/a/f.bin", "sha256": sha}, raw=data)
    c.check("PUT", "/fs/file", 409, {"path": "/test-api/a/f.bin"}, raw=data, code="exists")
    c.check("PUT", "/fs/file", 422, {"path": "/test-api/a/f.bin", "overwrite": "true", "sha256": "0" * 64}, raw=data,
            code="sha256_mismatch")
    entry = c.check("GET", "/fs/entry", 200, {"path": "/test-api/a/f.bin", "sha256": "true"})
    if isinstance(entry, dict) and entry.get("sha256") != sha:
        c.failures.append("GET /fs/entry: the board's SHA-256 is not the file's")
    status, got = c.board.call("GET", "/fs/file", {"path": "/test-api/a/f.bin"})
    c.calls += 1
    if status != 200 or got != data:
        c.failures.append(f"GET /fs/file: {status}, {'the same bytes' if got == data else 'different bytes'}")
    c.check("POST", "/fs/copy", 201, body={"from": "/test-api/a/f.bin", "to": "/test-api/g.bin"})
    c.check("POST", "/fs/move", 200, body={"from": "/test-api/g.bin", "to": "/test-api/a/h.bin"})
    c.check("POST", "/fs/move", 400, body={"from": "/test-api/a", "to": "/test-api/a/b"}, code="bad_request")
    c.check("DELETE", "/fs/entry", 409, {"path": "/test-api"}, code="not_empty")
    c.check("GET", "/fs/list", 200, {"path": "/test-api/a"})

    before = c.check("GET", "/leds", 200)
    c.check("PUT", "/scenes/scene", (200, 201), {"name": "test-api"},
            body={"leds": {"mode": "solid", "colour": "#102030", "brightness": 5}})
    c.check("POST", "/scenes/apply", 200, body={"name": "test-api"})
    c.check("POST", "/scenes/apply", 200, body={"scene": {"leds": {"mode": "off"}}})
    c.check("DELETE", "/scenes/scene", 204, {"name": "test-api"})
    c.check("PATCH", "/leds", 200, body={"mode": "wipe", "colour": "blue"})
    c.check("PATCH", "/leds", 200, body={"mode": "off", **({"brightness": before["brightness"]} if isinstance(before, dict) else {})})
    c.check("DELETE", "/fs/entry", 204, {"path": "/test-api", "recursive": "true"})


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--host", default=os.environ.get("HOST", "holo-2db0.local"))
    parser.add_argument("--password", default=os.environ.get("HOLO_PASSWORD"))
    parser.add_argument("--write", action="store_true", help="also change things, under /test-api")
    args = parser.parse_args()
    c = Checker(Board(args.host, args.password))
    try:
        reads(c)
        if args.write:
            writes(c)
    except (urllib.error.URLError, OSError) as e:
        print(f"error: {args.host}: {e}", file=sys.stderr)
        return 2
    for f in c.failures:
        print(f"error: {f}", file=sys.stderr)
    print(f"{c.calls} calls to {args.host}: {len(c.failures)} problem{'s' if len(c.failures) != 1 else ''}")
    return 1 if c.failures else 0


if __name__ == "__main__":
    sys.exit(main())
