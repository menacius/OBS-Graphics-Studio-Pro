#!/usr/bin/env python3
"""Run every source-level Python regression contract in deterministic order."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    tests = sorted((root / "tests").glob("*.py"))
    if not tests:
        print("No Python contract tests found", file=sys.stderr)
        return 2

    failures: list[str] = []
    for test in tests:
        result = subprocess.run(
            [sys.executable, str(test)],
            cwd=root,
            text=True,
            capture_output=True,
            check=False,
        )
        if result.returncode == 0:
            print(f"PASS {test.name}")
            continue
        failures.append(test.name)
        print(f"FAIL {test.name}", file=sys.stderr)
        if result.stdout:
            print(result.stdout, file=sys.stderr, end="")
        if result.stderr:
            print(result.stderr, file=sys.stderr, end="")

    print(f"Python contracts: {len(tests) - len(failures)}/{len(tests)} passed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
