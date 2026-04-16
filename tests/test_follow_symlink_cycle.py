#!/usr/bin/env python3

import os
import subprocess
import tempfile
from pathlib import Path

from testlib import can_follow_directory_symlinks, rhsum_bin, skip


def main() -> int:
    if not can_follow_directory_symlinks():
        return skip("directory symlink traversal via -L is unavailable on this platform")

    with tempfile.TemporaryDirectory() as tmpdir:
        root = Path(tmpdir) / "tree"
        nested = root / "nested"
        nested.mkdir(parents=True)
        (nested / "payload.txt").write_bytes(b"payload\n")
        os.symlink(root, nested / "loop")

        completed = subprocess.run(
            [str(rhsum_bin()), "-L", str(root)],
            capture_output=True,
            text=True,
            check=False,
            timeout=10,
        )

        if completed.returncode == 0:
            raise SystemExit("rhsum should fail on a symlink traversal cycle")
        if "Symlink cycle detected" not in completed.stderr:
            raise SystemExit(f"missing symlink cycle error in stderr: {completed.stderr!r}")

    print("symlink_cycle_detected=1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
