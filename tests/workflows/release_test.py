import contextlib
import io
import runpy
import subprocess
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

root = Path(__file__).resolve().parents[2]
script = root / ".github/scripts/release.py"
version = (root / "backend-api/VERSION").read_text().strip()
major, minor, patch_version = map(int, version.split("."))
newer = f"{major}.{minor + 1}.0-api"


def execute(tag, tags):
    def output(command, **_kwargs):
        if command[:3] == ["git", "tag", "--list"]:
            return tags
        if command[:3] == ["git", "rev-list", "-n"]:
            return "later-main-commit\n"
        raise AssertionError(command)

    with patch.object(sys, "argv", [str(script), "backend-api", tag]), \
         patch("subprocess.run", return_value=subprocess.CompletedProcess([], 0)), \
         patch("subprocess.check_output", side_effect=output), \
         contextlib.redirect_stdout(io.StringIO()) as stdout:
        runpy.run_path(str(script), run_name="__main__")
        return stdout.getvalue().strip()


class ReleaseTest(unittest.TestCase):
    def test_valid_release_without_prior_tag(self):
        self.assertEqual(version, execute(f"{version}-api", ""))

    def test_late_older_tag_cannot_downgrade_latest(self):
        with self.assertRaises(SystemExit):
            execute(f"{version}-api", newer + "\n")

    def test_semver_leading_zero_is_rejected(self):
        with self.assertRaises(SystemExit):
            execute(f"0{major}.{minor}.{patch_version}-api", "")


if __name__ == "__main__":
    unittest.main()
