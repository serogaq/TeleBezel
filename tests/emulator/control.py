import os
import socket
import struct
import sys
import time
import zlib

from libpebble2.communication.transports.qemu.protocol import QemuBluetoothConnection, QemuButton
from libpebble2.communication.transports.websocket import MessageTargetPhone, WebsocketTransport
from libpebble2.communication.transports.websocket.protocol import WebSocketRelayQemu
from pebble_tool.sdk.emulator import get_emulator_info

BUTTONS = {"back": QemuButton.Button.Back, "up": QemuButton.Button.Up, "select": QemuButton.Button.Select,
           "down": QemuButton.Button.Down}


def emulator(platform):
    info = get_emulator_info(platform)
    if info is None:
        sys.exit(f"no running {platform} emulator")
    return info


def relay(platform, protocol, steps):
    transport = WebsocketTransport(f"ws://localhost:{emulator(platform)['pypkjs']['port']}/")
    transport.connect()
    for packet, pause in steps:
        transport.send_packet(WebSocketRelayQemu(protocol=protocol, data=packet.serialise()), target=MessageTargetPhone())
        time.sleep(pause)
    transport.ws.close()


def monitor(platform, command):
    connection = socket.create_connection(("localhost", emulator(platform)["qemu"]["monitor"]), timeout=10)
    time.sleep(0.3)
    connection.recv(65536)
    connection.sendall(command.encode() + b"\n")
    time.sleep(1)
    connection.close()


def png(path, width, height, pixels):
    rows = b"".join(b"\x00" + pixels[row * width * 3:(row + 1) * width * 3] for row in range(height))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    with open(path, "wb") as target:
        target.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def screenshot(platform, path):
    dump = path + ".ppm"
    monitor(platform, f"screendump {dump}")
    with open(dump, "rb") as source:
        data = source.read()
    header, _, rest = data.partition(b"\n")
    size, _, rest = rest.partition(b"\n")
    _, _, pixels = rest.partition(b"\n")
    width, height = (int(value) for value in size.split())
    if header != b"P6":
        sys.exit("unexpected screendump format")
    brightest = max(pixels) or 255
    table = bytes(min(255, round(value * 255 / brightest)) for value in range(256))
    png(path, width, height, pixels.translate(table))
    os.remove(dump)


def main():
    platform, action = sys.argv[1:3]
    if action == "button":
        state = BUTTONS[sys.argv[3]]
        count = int(sys.argv[4]) if len(sys.argv) > 4 else 1
        steps = []
        for _ in range(count):
            steps += [(QemuButton(state=state), 0.1), (QemuButton(state=0), 0.4)]
        relay(platform, 8, steps)
    elif action == "bluetooth":
        relay(platform, 3, [(QemuBluetoothConnection(connected=sys.argv[3] == "yes"), 0.1)])
    elif action == "screenshot":
        screenshot(platform, sys.argv[3])
    else:
        sys.exit(f"unknown action {action}")


main()
