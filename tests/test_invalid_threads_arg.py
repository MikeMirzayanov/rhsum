#!/usr/bin/env python3

import subprocess

from testlib import rhsum_bin


def run_invalid(args):
    completed = subprocess.run(
        [str(rhsum_bin()), *args],
        capture_output=True,
        text=True,
        check=False,
        timeout=10,
    )
    if completed.returncode == 0:
        raise SystemExit(f"rhsum should fail for {args!r}")
    if "Error:" not in completed.stderr:
        raise SystemExit(f"missing error message for {args!r}: {completed.stderr!r}")
    return completed.stderr.strip()


def main() -> int:
    invalid_value = run_invalid(["-T", "abc", "rhsum.cpp"])
    missing_value = run_invalid(["-T"])
    zero_value = run_invalid(["-T", "0", "rhsum.cpp"])

    print(f"invalid_threads={invalid_value}")
    print(f"missing_threads={missing_value}")
    print(f"zero_threads={zero_value}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
