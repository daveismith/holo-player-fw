#!/usr/bin/env python3
"""Serve this copy of the Holo Player documentation on your own machine, and open it.

Shipped as serve.py at the top of the offline documentation bundle. Opened straight from disk,
the pages read fine, but a browser will not flash the board from them: Web Serial and the
installer's scripts only run on a page served over HTTPS or from localhost. This serves the
folder it sits in on 127.0.0.1 -- nothing else on the network can reach it, and nothing is
fetched from the internet -- and opens the Install page.

    python3 serve.py            # any free port
    python3 serve.py 8000       # a chosen port

Stop it with Ctrl-C. Standard library only; needs Python 3.8 or later.
"""

import functools
import http.server
import sys
import threading
import webbrowser
from pathlib import Path

HERE = Path(__file__).resolve().parent
START_PAGE = "install/flashing.html"


class Handler(http.server.SimpleHTTPRequestHandler):
    # Explicit, because the system's MIME table decides otherwise (on Windows the registry can
    # map .js to text/plain, and the browser then refuses to run the installer as a module).
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".html": "text/html; charset=utf-8",
        ".js": "text/javascript; charset=utf-8",
        ".css": "text/css; charset=utf-8",
        ".json": "application/json",
        ".svg": "image/svg+xml",
        ".bin": "application/octet-stream",
        ".png": "image/png",
    }

    def end_headers(self):
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()

    def log_message(self, format, *args):
        pass


def main() -> int:
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    handler = functools.partial(Handler, directory=str(HERE))
    try:
        server = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
    except OSError as error:
        sys.exit(f"serve.py: cannot listen on port {port}: {error.strerror}")
    url = f"http://localhost:{server.server_address[1]}/{START_PAGE}"
    print(f"Holo Player documentation at {url}")
    print("The installer needs Chrome, Edge or Opera. Press Ctrl-C to stop.", flush=True)
    threading.Timer(0.3, webbrowser.open, (url,)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
