#!/usr/bin/env python3
"""Dev helper: serve ps5/webui/index.html at / and forward every other path to a PS5SD server.

    python scripts/webui-dev.py [--port 8787] [--target http://10.0.0.101:7860]

The page is re-read from disk on every request, so an edit shows after a reload.
"""
import argparse
import http.client
import os
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(HERE, "..", "ps5", "webui", "index.html")


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    target = None

    def log_message(self, fmt, *args):
        pass

    def _page(self):
        with open(PAGE, "rb") as f:
            body = f.read()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _proxy(self):
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else None
        t = urllib.parse.urlparse(self.target)
        try:
            conn = http.client.HTTPConnection(t.hostname, t.port or 80, timeout=600)
            headers = {k: v for k, v in self.headers.items()
                       if k.lower() not in ("host", "connection", "accept-encoding")}
            conn.request(self.command, self.path, body=body, headers=headers)
            r = conn.getresponse()
            data = r.read()
        except Exception as e:  # server down: behave like a dropped connection
            msg = ("proxy error: %s" % e).encode()
            self.send_response(502)
            self.send_header("Content-Length", str(len(msg)))
            self.end_headers()
            self.wfile.write(msg)
            return
        self.send_response(r.status)
        for k, v in r.getheaders():
            if k.lower() in ("transfer-encoding", "connection", "content-length", "keep-alive"):
                continue
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _dispatch(self):
        if self.command == "GET" and self.path.split("?")[0] in ("/", "/index.html"):
            self._page()
        else:
            self._proxy()

    do_GET = do_POST = do_PUT = do_DELETE = do_OPTIONS = _dispatch


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8787)
    ap.add_argument("--target", default="http://10.0.0.101:7860")
    a = ap.parse_args()
    Handler.target = a.target
    print("serving %s on http://127.0.0.1:%d -> %s" % (PAGE, a.port, a.target))
    ThreadingHTTPServer(("127.0.0.1", a.port), Handler).serve_forever()
