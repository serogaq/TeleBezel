import importlib.util
import unittest
from pathlib import Path

path = Path(__file__).resolve().parents[2] / ".github/scripts/ci_gate.py"
spec = importlib.util.spec_from_file_location("ci_gate", path)
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


def values(app=False, api=False, tdlib=False, integration=False):
    result = {"CHANGES": "success", "AUDIT": "success"}
    for flag, enabled, jobs in (
        ("EXPECT_APP", app, ("APP",)),
        ("EXPECT_API", api, ("API", "API_IMAGE")),
        ("EXPECT_TDLIB", tdlib, ("TDLIB", "TDLIB_ANALYSIS", "TDLIB_IMAGE")),
        ("EXPECT_INTEGRATION", integration, ("INTEGRATION",)),
    ):
        result[flag] = str(enabled).lower()
        for job in jobs:
            result[job] = "success" if enabled else "skipped"
    return result


class CiGateTest(unittest.TestCase):
    def test_component_scopes_and_full_suite(self):
        for case in (
            values(app=True),
            values(api=True, integration=True),
            values(tdlib=True, integration=True),
            values(app=True, api=True, tdlib=True, integration=True),
        ):
            gate.validate(case)

    def test_expected_job_cannot_be_skipped(self):
        case = values(api=True, integration=True)
        case["API_IMAGE"] = "skipped"
        with self.assertRaises(ValueError):
            gate.validate(case)

    def test_failed_change_detection_and_unknown_expectation_fail(self):
        case = values()
        case["CHANGES"] = "failure"
        with self.assertRaises(ValueError):
            gate.validate(case)
        case = values()
        case["EXPECT_APP"] = ""
        with self.assertRaises(ValueError):
            gate.validate(case)


if __name__ == "__main__":
    unittest.main()
