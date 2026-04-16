#!/usr/bin/env python3

import subprocess
import tempfile
from pathlib import Path

from testlib import rhsum_bin


def expect_failure(args, expected_fragment):
    completed = subprocess.run(
        [str(rhsum_bin()), *args],
        capture_output=True,
        text=True,
        check=False,
        timeout=10,
    )
    if completed.returncode == 0:
        raise SystemExit(f"rhsum should fail for {args!r}")
    if expected_fragment not in completed.stderr:
        raise SystemExit(
            f"missing expected error fragment {expected_fragment!r} for {args!r}: {completed.stderr!r}"
        )
    return completed.stderr.strip()


def main() -> int:
    unknown_option = expect_failure(["--fast"], "Unknown option")
    unknown_short_option = expect_failure(["-t", "8"], "Unknown option")
    too_many_threads = expect_failure(["-T", "1000", "rhsum.cpp"], "streaming buffer budget")

    with tempfile.TemporaryDirectory() as tmpdir:
        dash_file = Path(tmpdir) / "-dash.bin"
        dash_file.write_bytes(b"dash-file\n")
        dash_hash = subprocess.run(
            [str(rhsum_bin()), "--", str(dash_file)],
            capture_output=True,
            text=True,
            check=True,
            timeout=10,
        ).stdout.strip()
        direct_hash = subprocess.run(
            [str(rhsum_bin()), str(dash_file)],
            capture_output=True,
            text=True,
            check=True,
            timeout=10,
        ).stdout.strip()
        if dash_hash != direct_hash:
            raise SystemExit("end-of-options marker should preserve hashing for dash-prefixed paths")

    print(f"unknown_option={unknown_option}")
    print(f"unknown_short_option={unknown_short_option}")
    print(f"too_many_threads={too_many_threads}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
