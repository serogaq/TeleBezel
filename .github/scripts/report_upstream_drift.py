#!/usr/bin/env python3
"""Report newer upstream source and mutable base-image tags without changing pins."""

import os
import re
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[2]
locks = (root / "backend-tdlib/cmake/dependencies.lock.cmake").read_text()
reports = ["### Upstream pin drift"]

for name, url in (
    ("TDLIB", "https://github.com/tdlib/td.git"),
    ("HTTPLIB", "https://github.com/yhirose/cpp-httplib.git"),
    ("JSON", "https://github.com/nlohmann/json.git"),
):
    pinned = re.search(rf'TELEBEZEL_{name}_COMMIT "([a-f0-9]{{40}})"', locks).group(1)
    result = subprocess.run(["git", "ls-remote", url, "HEAD"], capture_output=True, text=True, timeout=30)
    current = result.stdout.split()[0] if result.returncode == 0 and result.stdout.strip() else None
    reports.append(f"- {name}: {'unchanged' if current == pinned else 'upstream changed' if current else 'lookup unavailable'}"
                   f" (pin `{pinned[:12]}`, HEAD `{current[:12] if current else 'unknown'}`)")

for path, pattern in (
    ("backend-api/Dockerfile", r"^FROM (composer:[^@\s]+)@(sha256:[a-f0-9]{64})"),
    ("backend-api/Dockerfile", r"^FROM (dunglas/frankenphp:[^@\s]+)@(sha256:[a-f0-9]{64})"),
    ("backend-tdlib/Dockerfile", r"^FROM (debian:[^@\s]+)@(sha256:[a-f0-9]{64})"),
    ("compose.yaml", r"^    image: (postgres:[^@\s]+)@(sha256:[a-f0-9]{64})"),
):
    match = re.search(pattern, (root / path).read_text(), re.MULTILINE)
    if match is None:
        raise SystemExit(f"Missing pinned base image in {path}")
    tag, pinned = match.groups()
    result = subprocess.run(
        ["docker", "buildx", "imagetools", "inspect", tag, "--format", "{{.Manifest.Digest}}"],
        capture_output=True, text=True, timeout=60,
    )
    current = result.stdout.strip() if result.returncode == 0 else None
    reports.append(f"- `{tag}`: {'unchanged' if current == pinned else 'tag changed' if current else 'lookup unavailable'}"
                   f" (pin `{pinned[:19]}`, current `{current[:19] if current else 'unknown'}`)")

report = "\n".join(reports) + "\n"
print(report)
if os.environ.get("GITHUB_STEP_SUMMARY"):
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as summary:
        summary.write(report)
