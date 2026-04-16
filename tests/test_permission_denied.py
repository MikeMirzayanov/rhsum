#!/usr/bin/env python3

import os
import stat
import subprocess
import tempfile
from pathlib import Path

from testlib import rhsum_bin, skip


def write_tree(root: Path, with_hidden_file: bool) -> Path:
    (root / "visible").mkdir(parents=True)
    (root / "visible" / "data.txt").write_bytes(b"visible-data\n")
    blocked = root / "blocked"
    blocked.mkdir()
    if with_hidden_file:
        (blocked / "secret.txt").write_bytes(b"secret-data\n")
    return blocked


def main() -> int:
    if os.name == "nt":
        return skip("permission-denied traversal test is Unix-only")
    if hasattr(os, "geteuid") and os.geteuid() == 0:
        return skip("permission-denied traversal test is unreliable as root")

    with tempfile.TemporaryDirectory() as tmpdir:
        root = Path(tmpdir) / "tree"
        root.mkdir()

        blocked = write_tree(root, with_hidden_file=True)

        os.chmod(blocked, 0)
        try:
            completed = subprocess.run(
                [str(rhsum_bin()), "-R", str(root)],
                capture_output=True,
                text=True,
                check=False,
                timeout=10,
            )
        finally:
            os.chmod(blocked, stat.S_IRWXU)

        if completed.returncode == 0:
            raise SystemExit("rhsum should fail on permission-denied subtree")
        if "Error:" not in completed.stderr:
            raise SystemExit(f"missing error message in stderr: {completed.stderr!r}")

    print(f"permission_denied_rc={completed.returncode}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
