import socket
import sys
import time

from pebble_tool.sdk.emulator import get_emulator_info


def main():
    platform, path = sys.argv[1:3]
    deadline = time.monotonic() + 30
    info = None
    while time.monotonic() < deadline and not info:
        info = get_emulator_info(platform)
        if not info:
            time.sleep(0.5)
    if not info:
        sys.exit(f"no running {platform} emulator")
    connection = socket.create_connection(("localhost", info["qemu"]["serial"]), timeout=10)
    connection.settimeout(None)
    with open(path, "ab") as target:
        while True:
            chunk = connection.recv(65536)
            if not chunk:
                break
            target.write(chunk)
            target.flush()


if __name__ == "__main__":
    main()
