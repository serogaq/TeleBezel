#!/usr/bin/env python3
"""Test-only loopback proxy that loses one account-creation response."""

import http.client
import json
import socket
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


listen_port, upstream_port, capture_path = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
capture_lock = threading.Lock()
lost_first = False


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        self.forward()

    def do_POST(self):
        self.forward()

    def do_PUT(self):
        self.forward()

    def do_DELETE(self):
        self.forward()

    def forward(self):
        global lost_first
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        headers = {key: value for key, value in self.headers.items() if key.lower() not in {"connection", "content-length"}}
        headers["Content-Length"] = str(len(body))
        connection = http.client.HTTPConnection("127.0.0.1", upstream_port, timeout=15)
        try:
            connection.request(self.command, self.path, body=body, headers=headers)
            response = connection.getresponse()
            payload = response.read()
            create = self.command == "POST" and self.path == "/v1/owner/telegram/accounts"
            if create:
                with capture_lock:
                    with open(capture_path, "a", encoding="utf-8") as output:
                        output.write(json.dumps({
                            "body": json.loads(body),
                            "key": self.headers.get("Idempotency-Key"),
                            "status": response.status,
                            "id": json.loads(payload)["data"]["id"],
                        }) + "\n")
                    if not lost_first:
                        lost_first = True
                        self.connection.shutdown(socket.SHUT_RDWR)
                        self.connection.close()
                        return
            self.send_response(response.status, response.reason)
            for key, value in response.getheaders():
                if key.lower() not in {"connection", "content-length", "transfer-encoding"}:
                    self.send_header(key, value)
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(payload)
        finally:
            connection.close()

    def log_message(self, *_args):
        pass


ThreadingHTTPServer(("127.0.0.1", listen_port), Handler).serve_forever()
