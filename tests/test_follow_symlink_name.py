#!/usr/bin/env python3

import os
import tempfile
from pathlib import Path

from testlib import can_follow_directory_symlinks, run_rhsum, skip


def populate_tree(root: Path, link_name: str, external_root: Path) -> None:
    (root / "local").mkdir(parents=True)
    (root / "local" / "root.txt").write_bytes(b"local-root\n")
    os.symlink(external_root, root / link_name)


def main() -> int:
    if not can_follow_directory_symlinks():
        return skip("directory symlink traversal via -L is unavailable on this platform")

    with tempfile.TemporaryDirectory() as tmpdir:
        base = Path(tmpdir)
        external_root = base / "external"
        left_root = base / "left"
        right_root = base / "right"

        external_root.mkdir()
        (external_root / "nested").mkdir()
        (external_root / "nested" / "payload.bin").write_bytes(bytes(range(64)))

        left_root.mkdir()
        right_root.mkdir()
        populate_tree(left_root, "linked", external_root)
        populate_tree(right_root, "alias", external_root)

        left_hash = run_rhsum(["-L", left_root])
        right_hash = run_rhsum(["-L", right_root])

        if left_hash == right_hash:
            raise SystemExit(
                "hash should change when only the symlink name changes under -L"
            )

    print(f"linked={left_hash} alias={right_hash}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
