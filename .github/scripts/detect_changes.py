#!/usr/bin/env python3
import os
import subprocess
import sys

base, head = sys.argv[1:3]
if base == "ALL":
    paths = ["shared"]
else:
    paths = subprocess.check_output(["git", "diff", "--name-only", f"{base}...{head}"], text=True).splitlines()

shared = any(not p.startswith(("app/", "backend-api/", "backend-tdlib/")) for p in paths)
app = shared or any(p.startswith("app/") for p in paths)
api = shared or any(p.startswith("backend-api/") for p in paths)
tdlib = shared or any(p.startswith("backend-tdlib/") for p in paths)
integration = shared or any(p.startswith(("backend-api/", "backend-tdlib/", "tests/integration/")) for p in paths)
with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
    for key, value in {"app": app, "api": api, "tdlib": tdlib, "integration": integration}.items():
        output.write(f"{key}={str(value).lower()}\n")
