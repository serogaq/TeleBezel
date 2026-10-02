#!/usr/bin/env python3
"""Print the watch app's static footprint against the stage budget and its largest functions."""
import json
import struct
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sdk = sys.argv[1]
nm = Path.home() / f"Library/Application Support/Pebble SDK/SDKs/{sdk}/toolchain/arm-none-eabi/bin/arm-none-eabi-nm"
if not nm.exists():
    nm = Path.home() / f".pebble-sdk/SDKs/{sdk}/toolchain/arm-none-eabi/bin/arm-none-eabi-nm"
budget = json.loads((root / "tests/size-budget.json").read_text())
for platform in ("emery", "gabbro"):
    binary = (root / f"app/build/{platform}/pebble-app.bin").read_bytes()
    (size,) = struct.unpack_from("<H", binary, 128)
    limit = budget["static"][platform]
    print(f"{platform}: {size} B, stage {budget['stage']} budget {limit} B ({limit - size:+d} B), hard limit 59392 B ({59392 - size:+d} B)")
    symbols = subprocess.run([str(nm), "--size-sort", "-S", "-r", str(root / f"app/build/{platform}/pebble-app.elf")],
                             capture_output=True, text=True, check=True).stdout.splitlines()
    for line in symbols[:int(sys.argv[2]) if len(sys.argv) > 2 else 15]:
        parts = line.split()
        if len(parts) == 4:
            print(f"  {int(parts[1], 16):6d}  {parts[3]}")
