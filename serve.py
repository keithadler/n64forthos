#!/usr/bin/env python3
"""Serve web/ (and the freshly built cartridge as rom.z64) for browser testing.

    python3 serve.py [port]        # default 8795
"""
import http.server
import os
import shutil
import socketserver
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8795
HERE = os.path.dirname(os.path.abspath(__file__))
ROM = os.path.join(HERE, "build", "n64forthos.z64")
WEB = os.path.join(HERE, "web")


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=WEB, **kw)

    def end_headers(self):
        # The emulator core is happier with no caching between builds.
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        pass


if __name__ == "__main__":
    if os.path.exists(ROM):
        shutil.copyfile(ROM, os.path.join(WEB, "rom.z64"))
        print(f"rom.z64: {os.path.getsize(ROM) // (1024 * 1024)} MiB")
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("127.0.0.1", PORT), Handler) as httpd:
        print(f"http://127.0.0.1:{PORT}")
        httpd.serve_forever()
