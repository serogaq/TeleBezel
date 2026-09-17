#!/usr/bin/env python3
"""Keep the production dependency/license inventory in sync with lockfiles."""

import json
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
target = root / "docs/licenses/production-dependencies.tsv"
composer = json.loads((root / "backend-api/composer.lock").read_text())
npm = json.loads((root / "app/package-lock.json").read_text())
rows = [("ecosystem", "package", "version", "license")]
for package in composer["packages"]:
    rows.append(("Composer", package["name"], package["version"], ",".join(package.get("license", [])) or "UNKNOWN"))
for path, package in npm["packages"].items():
    if path.startswith("node_modules/") and not package.get("dev"):
        rows.append(("npm", path.removeprefix("node_modules/"), package["version"], package.get("license", "UNKNOWN")))
content = "\n".join("\t".join(row) for row in [rows[0], *sorted(rows[1:])]) + "\n"
if sys.argv[1:] == ["--write"]:
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content)
elif sys.argv[1:] == ["--check"]:
    if not target.exists() or target.read_text() != content:
        raise SystemExit("Production license inventory is stale; run python3 .github/scripts/production_licenses.py --write")
else:
    raise SystemExit("Use --check or --write")
