import os
import socket
import struct
import sys
import tempfile
import time
import zlib

from libpebble2.communication.transports.qemu.protocol import QemuBluetoothConnection, QemuButton
from libpebble2.communication.transports.websocket import MessageTargetPhone, WebsocketTransport
from libpebble2.communication.transports.websocket.protocol import WebSocketRelayQemu
from libpebble2.protocol.system import SetUTC, TimeMessage
from pebble_tool.sdk.emulator import get_emulator_info

BUTTONS = {"back": QemuButton.Button.Back, "up": QemuButton.Button.Up, "select": QemuButton.Button.Select,
           "down": QemuButton.Button.Down}
PROMPT = b"(qemu)"


def emulator(platform):
    info = get_emulator_info(platform)
    if info is None:
        sys.exit(f"no running {platform} emulator")
    return info


def png(path, width, height, pixels):
    rows = b"".join(b"\x00" + pixels[row * width * 3:(row + 1) * width * 3] for row in range(height))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    with open(path, "wb") as target:
        target.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


class Frame:
    def __init__(self, width, height, pixels):
        self.width = width
        self.height = height
        self.pixels = pixels

    def __eq__(self, other):
        return isinstance(other, Frame) and self.pixels == other.pixels

    def save(self, path):
        brightest = max(self.pixels) or 255
        table = bytes(min(255, round(value * 255 / brightest)) for value in range(256))
        png(path, self.width, self.height, self.pixels.translate(table))


class Emulator:
    def __init__(self, platform):
        info = emulator(platform)
        self.platform = platform
        self.transport = WebsocketTransport(f"ws://localhost:{info['pypkjs']['port']}/")
        self.transport.connect()
        self.monitor = socket.create_connection(("localhost", info["qemu"]["monitor"]), timeout=10)
        self.read_prompt()
        self.dump = os.path.join(tempfile.mkdtemp(prefix=f"telebezel-{platform}-"), "screen.ppm")

    def close(self):
        self.transport.ws.close()
        self.monitor.close()

    def read_prompt(self):
        received = b""
        while PROMPT not in received:
            chunk = self.monitor.recv(65536)
            if not chunk:
                raise ConnectionError("the QEMU monitor closed the connection")
            received += chunk

    def relay(self, protocol, steps):
        for packet, pause in steps:
            self.transport.send_packet(WebSocketRelayQemu(protocol=protocol, data=packet.serialise()), target=MessageTargetPhone())
            time.sleep(pause)

    def press(self, button, count=1):
        state = BUTTONS[button]
        steps = []
        for _ in range(count):
            steps += [(QemuButton(state=state), 0.1), (QemuButton(state=0), 0.4)]
        self.relay(8, steps)

    def double(self, button):
        state = BUTTONS[button]
        self.relay(8, [(QemuButton(state=state), 0.05), (QemuButton(state=0), 0.12), (QemuButton(state=state), 0.05),
                       (QemuButton(state=0), 0.1)])

    def hold(self, button, seconds=1.2):
        self.relay(8, [(QemuButton(state=BUTTONS[button]), seconds), (QemuButton(state=0), 0.1)])

    def set_time(self, unix_time):
        offset = (-time.altzone if time.localtime(unix_time).tm_isdst and time.daylight else -time.timezone) // 60
        packet = TimeMessage(message=SetUTC(unix_time=int(unix_time), utc_offset=offset, tz_name="UTC%+d" % (offset // 60)))
        self.transport.send_packet(packet.serialise_packet())

    def bluetooth(self, connected):
        self.relay(3, [(QemuBluetoothConnection(connected=connected), 0.1)])

    def frame(self):
        if os.path.exists(self.dump):
            os.remove(self.dump)
        self.monitor.sendall(f"screendump {self.dump}\n".encode())
        self.read_prompt()
        with open(self.dump, "rb") as source:
            data = source.read()
        header, _, rest = data.partition(b"\n")
        size, _, rest = rest.partition(b"\n")
        _, _, pixels = rest.partition(b"\n")
        if header != b"P6":
            raise ValueError("unexpected screendump format")
        width, height = (int(value) for value in size.split())
        return Frame(width, height, pixels)


def main():
    platform, action = sys.argv[1:3]
    device = Emulator(platform)
    try:
        if action == "button":
            device.press(sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 1)
        elif action == "double":
            device.double(sys.argv[3])
        elif action == "hold":
            device.hold(sys.argv[3])
            time.sleep(0.3)
        elif action == "bluetooth":
            device.bluetooth(sys.argv[3] == "yes")
        elif action == "screenshot":
            device.frame().save(sys.argv[3])
        else:
            sys.exit(f"unknown action {action}")
    finally:
        device.close()


if __name__ == "__main__":
    main()
