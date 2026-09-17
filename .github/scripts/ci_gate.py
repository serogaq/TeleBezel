#!/usr/bin/env python3
"""Fail if an expected CI job was skipped or an unexpected job ran."""

import os


def validate(values):
    if values.get("CHANGES") != "success" or values.get("AUDIT") != "success":
        raise ValueError("change detection and workflow audit must succeed")
    for flag, jobs in (
        ("EXPECT_APP", ("APP",)),
        ("EXPECT_API", ("API", "API_IMAGE")),
        ("EXPECT_TDLIB", ("TDLIB", "TDLIB_ANALYSIS", "TDLIB_IMAGE")),
        ("EXPECT_INTEGRATION", ("INTEGRATION",)),
    ):
        expected = values.get(flag)
        if expected not in ("true", "false"):
            raise ValueError(f"missing or invalid {flag}")
        required = "success" if expected == "true" else "skipped"
        for job in jobs:
            if values.get(job) != required:
                raise ValueError(f"{job}: expected {required}, got {values.get(job)}")


if __name__ == "__main__":
    validate(os.environ)
