import runpy
import unittest
from pathlib import Path
from unittest.mock import patch

root = Path(__file__).resolve().parents[2]
script = root / ".github/scripts/check_toolchain_drift.py"
read_text = Path.read_text


class ToolchainDriftTest(unittest.TestCase):
    def test_consistent_pins(self):
        runpy.run_path(str(script))

    def test_mismatches_fail_before_build(self):
        cases = (
            ("backend-api/Dockerfile", "FROM composer:2.10.3", "FROM composer:2.10.2"),
            ("backend-tdlib/cmake/dependencies.lock.cmake", "d1085f9cebc5a62379991ae1652673954f229c1f", "0" * 40),
            ("backend-tdlib/Dockerfile", "ARG CMAKE_VERSION=4.4.3", "ARG CMAKE_VERSION=4.4.2"),
            ("backend-api/Dockerfile", "sha256:d8f6343d3fae98107426bc49163ccad46ef85aabd4a27d80a74401fab4aba332", "sha256:" + "0" * 64),
        )
        for path, before, after in cases:
            with self.subTest(path=path, before=before):
                def altered(file, *args, **kwargs):
                    content = read_text(file, *args, **kwargs)
                    if file == root / path:
                        self.assertIn(before, content)
                        return content.replace(before, after, 1)
                    return content

                with patch.object(Path, "read_text", altered), self.assertRaises(SystemExit):
                    runpy.run_path(str(script))


if __name__ == "__main__":
    unittest.main()
