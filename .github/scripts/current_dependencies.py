#!/usr/bin/env python3
"""Read pinned dependency versions from their authoritative manifests."""
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DIGEST = r"sha256:[a-f0-9]{64}"


def composer_version(root=ROOT):
    dockerfile = (root / "backend-api/Dockerfile").read_text()
    matches = re.findall(
        rf"^FROM composer:([0-9.]+)@{DIGEST} AS composer-bin$",
        dockerfile,
        re.MULTILINE,
    )
    if len(matches) != 1 or len(re.findall(r"^FROM composer:", dockerfile, re.MULTILINE)) != 1:
        raise SystemExit("expected one SHA-pinned Composer image")
    return matches[0]


def postgres_image(root=ROOT):
    compose = (root / "compose.yaml").read_text()
    matches = re.findall(
        rf"^  postgres:\n    image: (postgres:[0-9][A-Za-z0-9._-]*@{DIGEST})$",
        compose,
        re.MULTILINE,
    )
    if len(matches) != 1 or len(re.findall(r"^  postgres:$", compose, re.MULTILINE)) != 1:
        raise SystemExit("expected one SHA-pinned PostgreSQL Compose image")
    return matches[0]


if __name__ == "__main__":
    commands = {"composer-version": composer_version, "postgres-image": postgres_image}
    if len(sys.argv) != 2 or sys.argv[1] not in commands:
        raise SystemExit("usage: current_dependencies.py composer-version|postgres-image")
    print(commands[sys.argv[1]]())
