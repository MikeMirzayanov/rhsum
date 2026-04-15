#!/usr/bin/env python3

import os
import shutil
import tempfile
from pathlib import Path

from testlib import can_follow_directory_symlinks, run_rhsum, skip


def main() -> int:
    if not can_follow_directory_symlinks():
        return skip("directory symlink traversal via -L is unavailable on this platform")

    with tempfile.TemporaryDirectory() as symlink_tmp, tempfile.TemporaryDirectory() as expanded_tmp:
        symlink_root = Path(symlink_tmp) / "tree"
        expanded_root = Path(expanded_tmp) / "tree"
        external_root = Path(symlink_tmp) / "external"

        symlink_root.mkdir()
        expanded_root.mkdir()
        external_root.mkdir()

        (symlink_root / "local").mkdir()
        (symlink_root / "local" / "root.txt").write_bytes(b"local-root\n")
        (external_root / "nested").mkdir()
        (external_root / "nested" / "payload.bin").write_bytes(bytes(range(64)))
        os.symlink(external_root, symlink_root / "linked")

        (expanded_root / "local").mkdir()
        (expanded_root / "local" / "root.txt").write_bytes(b"local-root\n")
        shutil.copytree(external_root, expanded_root / "linked")

        no_follow_hash = run_rhsum(["-R", symlink_root])
        follow_hash = run_rhsum(["-R", "-L", symlink_root])
        expanded_hash = run_rhsum(["-R", expanded_root])

        if no_follow_hash == follow_hash:
            raise SystemExit("hash should change when -L starts traversing a symlinked directory")

        if follow_hash != expanded_hash:
            raise SystemExit(
                f"-L hash mismatch: expected {expanded_hash}, got {follow_hash}"
            )

    print(
        f"no_follow={no_follow_hash} follow={follow_hash} expanded={expanded_hash}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
