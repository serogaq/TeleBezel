#!/usr/bin/env python3
import re
import subprocess
import sys
from pathlib import Path

component = sys.argv[1]
tag = sys.argv[2]
suffix = {"app": "app", "backend-api": "api", "backend-tdlib": "tdlib"}[component]
semver = r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
match = re.fullmatch(semver + "-" + suffix, tag)
if not match:
    raise SystemExit(f"invalid {suffix} release tag: {tag}")
version = ".".join(match.groups())
source = Path(component) / ("package.json" if component == "app" else "VERSION")
actual = __import__("json").loads(source.read_text())["version"] if component == "app" else source.read_text().strip()
if actual != version:
    raise SystemExit(f"tag version {version} does not match {source}: {actual}")
changelog = (Path(component) / "CHANGELOG.md").read_text()
if not re.search(r"^## " + re.escape(version) + r"(?:\s|$)", changelog, re.MULTILINE):
    raise SystemExit("exact version heading is missing from component changelog")
subprocess.run(["git", "merge-base", "--is-ancestor", "HEAD", "origin/main"], check=True)

current = tuple(map(int, match.groups()))
previous = []
for candidate in subprocess.check_output(["git", "tag", "--list", f"*-{suffix}"], text=True).splitlines():
    candidate_match = re.fullmatch(semver + "-" + suffix, candidate)
    if not candidate_match or candidate == tag:
        continue
    commit = subprocess.check_output(["git", "rev-list", "-n", "1", candidate], text=True).strip()
    if subprocess.run(["git", "merge-base", "--is-ancestor", commit, "origin/main"]).returncode == 0:
        previous.append(tuple(map(int, candidate_match.groups())))
if previous and current <= max(previous):
    raise SystemExit(f"version {version} must be greater than the previous {suffix} release")

print(version)
