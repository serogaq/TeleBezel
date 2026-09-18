import runpy
import re
import unittest
from pathlib import Path
from unittest.mock import patch

root = Path(__file__).resolve().parents[2]
script = root / ".github/scripts/check_toolchain_drift.py"
read_text = Path.read_text


class ToolchainDriftTest(unittest.TestCase):
    def run_with_change(self, path, before, after, count=1):
        file_path = root / path

        def altered(file, *args, **kwargs):
            content = read_text(file, *args, **kwargs)
            if file == file_path:
                self.assertGreaterEqual(content.count(before), count)
                return content.replace(before, after, count)
            return content

        with patch.object(Path, "read_text", altered):
            runpy.run_path(str(script))

    def test_consistent_pins(self):
        runpy.run_path(str(script))

    def test_dependabot_updates_are_checked_at_their_new_versions(self):
        debian = re.search(r"^FROM (debian:[^\s]+) AS build$", read_text(root / "backend-tdlib/Dockerfile"), re.MULTILINE).group(1)
        self.run_with_change(
            "backend-tdlib/Dockerfile", debian,
            "debian:13.6-slim@sha256:" + "a" * 64, count=2,
        )
        composer = re.search(r"^FROM (composer:[^\s]+) AS composer-bin$", read_text(root / "backend-api/Dockerfile"), re.MULTILINE).group(1)
        self.run_with_change(
            "backend-api/Dockerfile", composer,
            "composer:2.10.4@sha256:" + "b" * 64,
        )
        postgres = re.search(r"^    image: (postgres:[^\s]+)$", read_text(root / "compose.yaml"), re.MULTILINE).group(1)
        self.run_with_change(
            "compose.yaml", postgres,
            "postgres:18.7-bookworm@sha256:" + "c" * 64,
        )
        lock = read_text(root / "backend-api/composer.lock")
        framework = re.search(r'"name": "laravel/framework",\s+"version": "[^"]+"', lock).group(0)
        self.run_with_change("backend-api/composer.lock", framework,
                             '"name": "laravel/framework",\n            "version": "v99.99.99"')

    def test_real_inconsistencies_fail_before_build(self):
        debian = re.search(r"^FROM (debian:[^\s]+) AS build$", read_text(root / "backend-tdlib/Dockerfile"), re.MULTILINE).group(1)
        composer = re.search(r"^FROM (composer:[^\s]+) AS composer-bin$", read_text(root / "backend-api/Dockerfile"), re.MULTILINE).group(1)
        postgres = re.search(r"^    image: (postgres:[^\s]+)$", read_text(root / "compose.yaml"), re.MULTILINE).group(1)
        tdlib_commit = re.search(r'TELEBEZEL_TDLIB_COMMIT "([a-f0-9]{40})"', read_text(root / "backend-tdlib/cmake/dependencies.lock.cmake")).group(1)
        cmake_version = re.search(r'^ARG CMAKE_VERSION=([0-9.]+)$', read_text(root / "backend-tdlib/Dockerfile"), re.MULTILINE).group(1)
        cases = (
            ("backend-api/Dockerfile", composer, "composer:2.10.4"),
            ("backend-tdlib/Dockerfile", debian, "debian:13.6-slim@sha256:" + "a" * 64),
            ("compose.yaml", postgres, "postgres:18.7-bookworm"),
            ("backend-tdlib/cmake/dependencies.lock.cmake", tdlib_commit, "0" * 40),
            ("backend-tdlib/Dockerfile", f"ARG CMAKE_VERSION={cmake_version}", "ARG CMAKE_VERSION=0.0.0"),
        )
        for path, before, after in cases:
            with self.subTest(path=path, before=before):
                with self.assertRaises(SystemExit):
                    self.run_with_change(path, before, after)


if __name__ == "__main__":
    unittest.main()
