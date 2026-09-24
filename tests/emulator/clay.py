#!/usr/bin/env python3
import argparse
import http.server
import os
import queue
import signal
import sys
import threading
import time
import webbrowser
from urllib.parse import unquote
from urllib.request import urlopen

from libpebble2.communication.transports.websocket import MessageTargetPhone, WebsocketTransport
from libpebble2.communication.transports.websocket.protocol import (AppConfigCancelled, AppConfigResponse, AppConfigSetup,
                                                                     WebSocketPhonesimAppConfig, WebSocketPhonesimConfigResponse)
from pebble_tool.sdk.emulator import get_emulator_info

NOTICE = (b"<script>if (location.search.indexOf('saved=') >= 0) {"
          b"var failed = location.search.indexOf('saved=0') >= 0; history.replaceState(null, '', '/');"
          b"setTimeout(function() { alert(failed ? 'The emulator is not available any more; the settings were not saved.'"
          b" : 'Settings saved to the emulator.'); }, 100); }</script>")
GONE = (b"<!DOCTYPE html><meta charset=utf-8><title>TeleBezel settings</title>"
        b"<p>The emulator stopped, so the settings are closed. Start make app-qemu and make open-clay again.</p>")


class PageError(Exception):
    pass


def connect(platform):
    info = get_emulator_info(platform)
    if info is None:
        sys.exit(f"No running {platform} emulator. Start it first, for example: make app-qemu PLATFORM={platform}")
    transport = WebsocketTransport(f"ws://localhost:{info['pypkjs']['port']}/")
    try:
        transport.connect()
    except Exception:
        sys.exit(f"The {platform} emulator does not answer. Restart it: make app-qemu PLATFORM={platform}")
    return transport, [info["qemu"]["pid"], info["pypkjs"]["pid"]]


def alive(pids):
    for pid in pids:
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return False
        except PermissionError:
            pass
    return True


class Link:
    def __init__(self, transport, lost):
        self.transport = transport
        self.pages = queue.Queue()
        self.closed = threading.Event()
        self.lost = lost
        threading.Thread(target=self.read, daemon=True).start()

    def read(self):
        try:
            while True:
                origin, message = self.transport.read_packet()
                if isinstance(origin, MessageTargetPhone) and isinstance(message, WebSocketPhonesimConfigResponse):
                    self.pages.put(message.config.data)
        except Exception:
            self.closed.set()
            self.lost()

    def send(self, config):
        self.transport.send_packet(WebSocketPhonesimAppConfig(config=config), target=MessageTargetPhone())


def request_page(link, timeout):
    while not link.pages.empty():
        link.pages.get_nowait()
    link.send(AppConfigSetup())
    deadline = time.monotonic() + timeout
    url = None
    while url is None:
        if link.closed.is_set() or time.monotonic() >= deadline:
            raise PageError("The app did not open its settings page. Is TeleBezel running in the emulator?")
        try:
            url = link.pages.get(timeout=0.2)
        except queue.Empty:
            pass
    if url.startswith("data:"):
        with urlopen(url) as response:
            return response.read()
    if "#" in url:
        return unquote(url.split("#", 1)[1]).encode()
    return f'<!DOCTYPE html><meta http-equiv="refresh" content="0;URL={url}">'.encode()


class Session:
    def __init__(self, link, page, timeout, stop):
        self.link = link
        self.page = page
        self.timeout = timeout
        self.stop = stop
        self.lock = threading.Lock()
        self.open = True
        self.gone = False

    def send(self, config):
        self.link.send(config)

    def lost(self):
        self.open = False
        self.gone = True
        self.stop.set()

    def save(self, response):
        with self.lock:
            if not self.open:
                return False
            try:
                self.send(AppConfigResponse(data=response))
                self.page = request_page(self.link, self.timeout)
                return True
            except Exception:
                self.lost()
                return False

    def cancel(self):
        with self.lock:
            if self.open and not self.gone:
                self.open = False
                try:
                    self.send(AppConfigCancelled())
                except Exception:
                    self.gone = True


def serve(session, port, open_browser):
    close_url = f"http://127.0.0.1:{port}/close?".encode()

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            path, _, query = self.path.partition("?")
            if path == "/":
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(GONE if session.gone and "saved=" not in query else session.page.replace(b"$$RETURN_TO$$", close_url) + NOTICE)
            elif path == "/close" and query:
                saved = session.save(query)
                self.send_response(303)
                self.send_header("Location", "/?saved=1" if saved else "/?saved=0")
                self.end_headers()
            else:
                self.send_response(404)
                self.end_headers()

        def log_message(self, *_):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f"http://127.0.0.1:{server.server_port}/"
    print(f"TeleBezel settings: {url}")
    print("Save sends the settings to the emulator and keeps the page open; Ctrl+C closes them like the phone's back arrow.",
          flush=True)
    if open_browser:
        webbrowser.open_new(url)
    return server


def main():
    parser = argparse.ArgumentParser(description="Open the TeleBezel Clay settings page of a running emulator in the browser.")
    parser.add_argument("--platform", default="emery")
    parser.add_argument("--port", type=int, default=8733)
    parser.add_argument("--no-browser", action="store_true")
    parser.add_argument("--timeout", type=float, default=15)
    arguments = parser.parse_args()
    transport, pids = connect(arguments.platform)
    stop = threading.Event()
    link = Link(transport, stop.set)
    page = None
    deadline = time.monotonic() + arguments.timeout * 2
    while page is None:
        if stop.is_set():
            sys.exit(f"The {arguments.platform} emulator closed the connection. Restart it: make app-qemu PLATFORM={arguments.platform}")
        try:
            page = request_page(link, 5)
        except PageError as error:
            if time.monotonic() >= deadline:
                sys.exit(str(error))
        except Exception as error:
            sys.exit(str(error))
    session = Session(link, page, arguments.timeout, stop)
    link.lost = session.lost
    server = serve(session, arguments.port, not arguments.no_browser)
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    try:
        while not stop.wait(1):
            if not alive(pids):
                session.lost()
        if not alive(pids):
            session.gone = True
        if session.gone:
            print("\nThe emulator stopped; settings closed.")
            time.sleep(2)
        else:
            session.cancel()
            print("\nSettings closed.")
    finally:
        server.shutdown()
        server.server_close()
        try:
            transport.ws.close()
        except Exception:
            pass


main()
