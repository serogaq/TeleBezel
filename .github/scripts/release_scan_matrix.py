#!/usr/bin/env python3
"""Build the scheduled GHCR scan matrix from published GitHub release tags."""

import json
import re
import sys

entries = [
    {"component": component, "version": "latest"}
    for component in ("backend-api", "backend-tdlib")
]
seen = {(entry["component"], entry["version"]) for entry in entries}
for tag in sys.stdin:
    match = re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)-(api|tdlib)", tag.strip())
    if match is None:
        continue
    component = "backend-api" if match.group(4) == "api" else "backend-tdlib"
    version = ".".join(match.group(i) for i in (1, 2, 3))
    if (component, version) not in seen:
        entries.append({"component": component, "version": version})
        seen.add((component, version))
if len(entries) > 256:
    raise SystemExit("Too many release images for one scheduled matrix; batch the scan")
print(json.dumps(entries, separators=(",", ":")))
