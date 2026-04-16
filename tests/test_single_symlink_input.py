#!/usr/bin/env python3

import os
import tempfile
from pathlib import Path

from testlib import can_follow_single_file_symlinks, run_rhsum, skip


def main() -> int:
    if not can_follow_single_file_symlinks():
        return skip("single-file symlink traversal via -L is unavailable on this platform")

    with tempfile.TemporaryDirectory() as tmpdir:
        root = Path(tmpdir)
        target = root / "target.bin"
        link = root / "alias.bin"

        target.write_bytes(b"single-symlink-payload\n")
        os.symlink(target, link)

        target_hash = run_rhsum([target])
        no_follow_hash = run_rhsum([link])
        follow_hash = run_rhsum(["-L", link])

        if no_follow_hash == target_hash:
            raise SystemExit("single symlink input should not follow target without -L")
        if follow_hash != target_hash:
            raise SystemExit("single symlink input should follow target with -L")
        if no_follow_hash != "0000000000000000":
            raise SystemExit(f"single symlink without -L should hash as empty special entry: {no_follow_hash}")

    print(f"target={target_hash} no_follow={no_follow_hash} follow={follow_hash}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
