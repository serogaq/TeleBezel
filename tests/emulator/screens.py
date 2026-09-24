#!/usr/bin/env python3
import argparse
import dbm.dumb
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from control import Emulator
from pebble_tool.sdk.emulator import get_emulator_info
from sheet import sheet

ROOT = Path(__file__).resolve().parents[2]
MOCK = ROOT / "app/tests/e2e/mock_api.js"
UUID = "b91f715e-af74-4a90-9df4-fda0fbcd9762"
PLATFORMS = ("emery", "gabbro")
COLUMNS = 7
POLL = 0.1
QUIET = 0.5
EMULATORS = threading.Lock()
OUTPUT = threading.Lock()


def say(message, error=False):
    with OUTPUT:
        print(message, file=sys.stderr if error else sys.stdout, flush=True)


def running(pid):
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False


class Done(Exception):
    pass


class Run:
    def __init__(self, platform, sdk, port, out, wanted, pbw):
        self.platform = platform
        self.sdk = sdk
        self.port = port
        self.out = out
        self.wanted = wanted
        self.pbw = pbw
        self.taken = []
        self.mock = None
        self.mock_log = None
        self.logs = None
        self.log_path = None
        self.device = None
        self.scenario = None

    def mock_size(self):
        return self.mock_log.stat().st_size if self.mock_log and self.mock_log.exists() else 0

    def settle(self, timeout=10.0, quiet=QUIET):
        deadline = time.monotonic() + timeout
        frame = self.device.frame()
        size = self.mock_size()
        calm = time.monotonic()
        while time.monotonic() < deadline:
            time.sleep(POLL)
            current = self.device.frame()
            now_size = self.mock_size()
            if current != frame or now_size != size:
                frame, size, calm = current, now_size, time.monotonic()
            elif time.monotonic() - calm >= quiet:
                break
        return frame

    def act(self, action, change=4.0, timeout=10.0):
        before = self.device.frame()
        action()
        deadline = time.monotonic() + change
        while time.monotonic() < deadline and self.device.frame() == before:
            time.sleep(POLL)
        return self.settle(timeout)

    def press(self, button, count=1):
        return self.act(lambda: self.device.press(button, count))

    def hold(self, button, seconds=1.2, timeout=10.0):
        return self.act(lambda: self.device.hold(button, seconds), timeout=timeout)

    def shot(self, name, frame=None):
        remaining = self.wanted[self.scenario]
        if name not in remaining:
            return
        frame = frame or self.settle()
        path = self.out / f"{SCREENS.index(name) + 1:02d}-{name}.png"
        frame.save(str(path))
        self.taken.append(path)
        say(f"{self.platform}: {path.relative_to(ROOT)}")
        remaining.discard(name)
        if not remaining:
            raise Done

    def wait_text(self, path, text, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if path and path.exists() and text in path.read_text(errors="replace"):
                return True
            time.sleep(POLL)
        say(f"{self.platform}: '{text}' did not appear within {timeout} s", error=True)
        return False

    def wait_mock_silence(self, seconds, timeout):
        deadline = time.monotonic() + timeout
        size = self.mock_size()
        calm = time.monotonic()
        while time.monotonic() < deadline:
            time.sleep(POLL)
            current = self.mock_size()
            if current != size:
                size, calm = current, time.monotonic()
            elif time.monotonic() - calm >= seconds:
                return

    def storage(self):
        directory = Path(os.path.expanduser(f"~/Library/Application Support/Pebble SDK/{self.sdk}/{self.platform}"))
        if not directory.is_dir():
            directory = Path(os.path.expanduser(f"~/.pebble-sdk/{self.sdk}/{self.platform}"))
        directory = directory / "localstorage"
        directory.mkdir(parents=True, exist_ok=True)
        for suffix in ("", ".dat", ".dir", ".bak"):
            (directory / f"{UUID}{suffix}").unlink(missing_ok=True)
        store = dbm.dumb.open(str(directory / UUID), "c")
        store["telebezel.settings.v1"] = json.dumps({"address": f"127.0.0.1:{self.port}", "ssl": False, "token": "tb_" + "e" * 43,
                                                     "showArchive": True, "unreadMode": "chats"})
        store.close()

    def start_mock(self, environment):
        self.stop_mock()
        self.mock_log = self.out / "logs" / f"mock-{self.scenario}.log"
        env = dict(os.environ, PORT=str(self.port), MOCK_LOG_REQUESTS="1", **environment)
        self.mock = subprocess.Popen(["node", str(MOCK)], env=env, stdout=open(self.mock_log, "w"), stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=0.5).close()
                return
            except OSError:
                time.sleep(0.1)
        raise RuntimeError("the mock API did not start")

    def stop_mock(self):
        if self.mock:
            self.mock.terminate()
            self.mock.wait()
            self.mock = None

    def kill(self):
        info = get_emulator_info(self.platform, self.sdk)
        if not info:
            return
        pids = [info[part]["pid"] for part in ("qemu", "pypkjs") if part in info]
        for pid in pids:
            try:
                os.kill(pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and any(running(pid) for pid in pids):
            time.sleep(0.1)
        for pid in pids:
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        time.sleep(0.5)

    def install(self):
        for attempt in range(6):
            if subprocess.run(["pebble", "install", "--emulator", self.platform, str(self.pbw)],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
                return
            time.sleep(3 + 3 * attempt)
        raise RuntimeError("the app could not be installed")

    def fresh(self):
        self.close_device()
        with EMULATORS:
            self.kill()
            self.storage()
            self.install()
        self.device = Emulator(self.platform)
        self.log_path = self.out / "logs" / f"{self.scenario}.log"
        self.logs = subprocess.Popen(["pebble", "logs", "--emulator", self.platform], stdout=open(self.log_path, "w"),
                                     stderr=subprocess.STDOUT)
        self.wait_text(self.mock_log, "/v1/telegram/accounts", 20)
        self.settle()

    def close_device(self):
        if self.logs:
            self.logs.terminate()
            self.logs.wait()
            self.logs = None
        if self.device:
            self.device.close()
            self.device = None

    def history(self):
        self.press("select")
        self.press("select")

    def compose_template(self):
        self.history()
        self.press("down")
        self.press("select")
        self.press("down")
        self.press("select")


def read(run):
    run.shot("accounts")
    run.press("select")
    run.shot("chats")
    run.press("down", 3)
    run.shot("chats-scrolled")
    run.press("up", 3)
    run.press("select")
    run.shot("history")
    run.press("up", 3)
    run.shot("history-kinds")
    run.press("down", 3)
    run.press("select")
    run.shot("reader")
    run.press("down", 6)
    run.shot("reader-scrolled")
    run.press("back")
    run.hold("up", 3.0, timeout=15)
    run.shot("older")
    run.press("back")
    run.hold("up", 1.5)
    run.shot("toggle")
    run.press("select")
    run.shot("archive")
    run.press("back")
    run.press("down")
    run.press("select")
    run.shot("needs-login")


def updated(run):
    run.press("select")
    loaded = time.time()
    for name, seconds in (("updated-minute", 61), ("updated-minutes", 59 * 60), ("updated-today", 3 * 3600 + 120),
                          ("updated-yesterday", 26 * 3600), ("updated-days", 3 * 86400 + 600)):
        if name not in run.wanted[run.scenario]:
            continue
        run.device.set_time(loaded + seconds)
        run.press("down")
        run.press("up")
        run.shot(name)


def marks(run):
    run.history()
    run.press("up", 7)
    run.shot("mark-reply")
    run.press("up", 3)
    run.shot("mark-failed")
    run.press("up", 5)
    run.shot("mark-pending")


def saved(run):
    run.press("select")
    run.press("down")
    run.press("select")
    run.press("up", 2)
    run.shot("saved-history")
    run.press("down")
    run.press("select")
    run.shot("saved-reader")


def quotes(run):
    run.history()
    run.press("up", 7)
    run.shot("history-quote")
    run.press("up")
    run.press("select")
    run.shot("reader-forward")
    run.press("back")
    run.press("up")
    run.press("select")
    run.shot("reader-reply")


def reader_menu(run):
    run.history()
    run.press("select")
    run.press("select")
    run.shot("reader-actions")


def connecting(run):
    run.press("select")
    run.shot("connecting")
    run.wait_mock_silence(4, 25)
    run.shot("cannot-connect")
    run.press("up", 3)
    run.shot("topbar")


def compose(run):
    run.history()
    run.press("down")
    run.shot("write-row")
    run.press("select")
    run.shot("compose")
    run.press("down")
    run.press("select")
    run.shot("review")
    run.shot("sent", run.act(lambda: run.device.press("select"), timeout=1.0))


def reply(run):
    run.history()
    run.hold("select")
    run.shot("message-menu")
    run.press("select")
    run.shot("reply-compose")
    run.press("down", 2)
    run.press("select")
    run.shot("reply-review")


def dictation(run):
    run.history()
    run.press("down")
    run.press("select")
    run.shot("dictation", run.act(lambda: run.device.press("select"), timeout=2.0))
    run.press("back")
    run.shot("dictation-cancelled")


def pending(run):
    run.compose_template()
    run.press("select")
    run.shot("sending")
    run.shot("card-sending", run.act(lambda: run.device.press("back"), timeout=2.0))
    run.wait_text(run.log_path, "settled sent", 40)
    run.shot("card-sent", run.settle(timeout=2.0))


def refused(run):
    run.compose_template()
    run.press("select")
    run.shot("not-sent")


SCENARIOS = [
    ("read", read, {}, ["accounts", "chats", "chats-scrolled", "history", "history-kinds", "reader", "reader-scrolled", "older",
                        "toggle", "archive", "needs-login"]),
    ("updated", updated, {}, ["updated-minute", "updated-minutes", "updated-today", "updated-yesterday", "updated-days"]),
    ("marks", marks, {}, ["mark-reply", "mark-failed", "mark-pending"]),
    ("reader-menu", reader_menu, {}, ["reader-actions"]),
    ("quotes", quotes, {}, ["history-quote", "reader-forward", "reader-reply"]),
    ("saved", saved, {}, ["saved-history", "saved-reader"]),
    ("connecting", connecting, {"MOCK_CONNECTION": "connecting", "MOCK_PROXY": "1"}, ["connecting", "cannot-connect", "topbar"]),
    ("compose", compose, {"MOCK_SEND_MODES": "sent"}, ["write-row", "compose", "review", "sent"]),
    ("reply", reply, {}, ["message-menu", "reply-compose", "reply-review"]),
    ("dictation", dictation, {}, ["dictation", "dictation-cancelled"]),
    ("pending", pending, {"MOCK_SEND_MODES": "pending", "MOCK_SETTLE_MS": "3000"}, ["sending", "card-sending", "card-sent"]),
    ("refused", refused, {"MOCK_SEND_MODES": "forbidden"}, ["not-sent"]),
]
SCREENS = [screen for _, _, _, screens in SCENARIOS for screen in screens]


def listing():
    lines = ["Platforms: emery, gabbro.", "Cases and their screens:"]
    lines += [f"  {name}: {', '.join(screens)}" for name, _, _, screens in SCENARIOS]
    return "\n".join(lines)


def parse_platforms(value):
    platforms = []
    for item in value.split(","):
        platform = item.strip().lower()
        if platform not in PLATFORMS:
            sys.exit(f"unknown platform '{item.strip()}'\n{listing()}")
        if platform not in platforms:
            platforms.append(platform)
    return platforms


def parse_screens(value):
    names = [item.strip() for item in value.split(",") if item.strip()]
    if not names or names == ["all"]:
        return {name: set(screens) for name, _, _, screens in SCENARIOS}
    wanted = {}
    for item in names:
        found = False
        for name, _, _, screens in SCENARIOS:
            if item == name:
                wanted.setdefault(name, set()).update(screens)
                found = True
            elif item in screens:
                wanted.setdefault(name, set()).add(item)
                found = True
        if not found:
            sys.exit(f"unknown case or screen '{item}'\n{listing()}")
    return wanted


def busy(port):
    try:
        socket.create_connection(("127.0.0.1", port), timeout=0.5).close()
        return True
    except OSError:
        return False


def device(platform, port, arguments, wanted, results):
    started = time.monotonic()
    out = ROOT / "app/build/screens" / platform
    shutil.rmtree(out, ignore_errors=True)
    (out / "logs").mkdir(parents=True)
    run = Run(platform, arguments.sdk, port, out, {name: set(screens) for name, screens in wanted.items()}, arguments.pbw)
    failed = False
    try:
        for name, scenario, environment, _ in SCENARIOS:
            if name not in wanted:
                continue
            run.scenario = name
            for attempt in (1, 2):
                begun = time.monotonic()
                try:
                    run.start_mock(environment)
                    run.fresh()
                    scenario(run)
                except Done:
                    pass
                except (RuntimeError, OSError, ValueError) as error:
                    say(f"{platform}: case '{name}' attempt {attempt} stopped: {error}", error=True)
                    if attempt == 1:
                        continue
                    failed = True
                say(f"{platform}: {name} took {time.monotonic() - begun:.0f} s")
                break
            for missing in sorted(run.wanted[name]):
                say(f"{platform}: screen '{missing}' was not reached", error=True)
                failed = True
    finally:
        run.close_device()
        run.stop_mock()
        with EMULATORS:
            run.kill()
    if run.taken:
        sheet(str(out / "sheet.png"), COLUMNS, [str(path) for path in sorted(run.taken)])
        say(f"{platform}: {len(run.taken)} screens in {time.monotonic() - started:.0f} s, sheet {(out / 'sheet.png').relative_to(ROOT)}")
    else:
        failed = True
    results[platform] = not failed


def main():
    parser = argparse.ArgumentParser(description="Take watch screenshots in QEMU against the mock API and join them into one sheet.",
                                     epilog=listing(), formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--platform", default="emery", help="comma-separated: emery, gabbro; default emery")
    parser.add_argument("--screens", default="all", help="comma-separated case or screen names (default all)")
    parser.add_argument("--sdk", default=os.environ.get("PEBBLE_SDK_VERSION", "4.33.1"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("MOCK_PORT", "8787")))
    parser.add_argument("--pbw", type=Path, default=ROOT / "app/build/app.pbw")
    parser.add_argument("--list", action="store_true", help="print platforms, cases and screens")
    arguments = parser.parse_args()
    if arguments.list:
        print(listing())
        return
    platforms = parse_platforms(arguments.platform)
    wanted = parse_screens(arguments.screens)
    if not arguments.pbw.is_file():
        sys.exit(f"{arguments.pbw} not found; build the app first")
    ports = [arguments.port + index for index in range(len(platforms))]
    for port in ports:
        if busy(port):
            sys.exit(f"port {port} is busy; stop whatever listens there or set MOCK_PORT")
    results = {}
    workers = [threading.Thread(target=device, args=(platform, port, arguments, wanted, results))
               for platform, port in zip(platforms, ports)]
    for worker in workers:
        worker.start()
    for worker in workers:
        worker.join()
    if not all(results.get(platform) for platform in platforms):
        sys.exit("some screens are missing; see the messages above and the logs")


main()
