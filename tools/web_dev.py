#!/usr/bin/env python3
"""Work on the board's web app without rebuilding the firmware.

    python tools/web_dev.py <board>          # e.g. 10.0.19.118 or holo-2db0.local
    open http://localhost:8080/

Serves web/ from disk, as the firmware would (plus /lib/inspect.js and /openapi.json, which the
build embeds from manual/), and passes everything under /api/ to a real board. Edit a file,
reload the page. The firmware build embeds the same files (components/webui/embed_assets.py).

Requests are passed on with the board's own Host header, as the board refuses changes that
name another host. Standard library only.
"""

from __future__ import annotations

import argparse
import http.client
import http.server
import mimetypes
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WEB = ROOT / "web"
SHARED = {
    "/lib/inspect.js": ROOT / "manual" / "javascripts" / "installer" / "inspect.js",
    "/openapi.json": ROOT / "manual" / "reference" / "openapi.json",
}
HOP = {"connection", "keep-alive", "transfer-encoding", "te", "trailer", "upgrade", "proxy-authorization", "host"}


def handler_for(board: str):
    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def proxy(self):
            length = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(length) if length else None
            conn = http.client.HTTPConnection(board, timeout=120)
            headers = {k: v for k, v in self.headers.items() if k.lower() not in HOP}
            headers["Host"] = board
            try:
                conn.request(self.command, self.path, body=body, headers=headers)
                res = conn.getresponse()
                data = res.read()
            except OSError as e:
                self.send_error(502, f"board unreachable: {e}")
                return
            finally:
                conn.close()
            self.send_response(res.status, res.reason)
            for k, v in res.getheaders():
                if k.lower() not in HOP and k.lower() != "content-length":
                    self.send_header(k, v)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def static(self):
            path = self.path.split("?", 1)[0]
            if path == "/":
                path = "/index.html"
            file = SHARED.get(path) or (WEB / path.lstrip("/")).resolve()
            if not str(file).startswith(str(ROOT)) or not file.is_file():
                self.send_error(404)
                return
            data = file.read_bytes()
            kind = mimetypes.guess_type(file.name)[0] or "application/octet-stream"
            if kind.startswith("text/") or kind.endswith("javascript"):
                kind += "; charset=utf-8"
            self.send_response(200)
            self.send_header("Content-Type", kind)
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(data)

        def route(self):
            if self.path.startswith("/api/"):
                self.proxy()
            elif self.command in ("GET", "HEAD"):
                self.static()
            else:
                self.send_error(405)

        do_GET = do_HEAD = do_POST = do_PUT = do_PATCH = do_DELETE = route

    return Handler


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("board", help="the board's address or name, as `web` on its console prints it")
    parser.add_argument("-p", "--port", type=int, default=8080)
    parser.add_argument("--bind", default="127.0.0.1", help="address to listen on (default: this machine only)")
    args = parser.parse_args()
    mimetypes.add_type("text/javascript", ".js")
    server = http.server.ThreadingHTTPServer((args.bind, args.port), handler_for(args.board))
    print(f"http://{'localhost' if args.bind == '127.0.0.1' else args.bind}:{args.port}/  (web/ from disk, /api/ from {args.board})")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
