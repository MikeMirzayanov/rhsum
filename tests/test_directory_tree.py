#!/usr/bin/env python3

import tempfile
from pathlib import Path

from testlib import run_rhsum


def write_tree(root: Path, with_empty_dir: bool) -> None:
    (root / "alpha").mkdir(parents=True)
    (root / "beta" / "nested").mkdir(parents=True)
    (root / "zeta").mkdir(parents=True)

    (root / "root.txt").write_bytes(b"root-data\n")
    (root / "alpha" / "a.bin").write_bytes(bytes(range(32)))
    (root / "beta" / "nested" / "note.txt").write_bytes(b"nested-note\n")
    (root / "zeta" / "empty.txt").write_bytes(b"")

    if with_empty_dir:
        (root / "beta" / "vacant").mkdir()


def main() -> int:
    with tempfile.TemporaryDirectory() as left_tmp, tempfile.TemporaryDirectory() as right_tmp:
        left_root = Path(left_tmp) / "tree"
        right_root = Path(right_tmp) / "tree"
        left_root.mkdir()
        right_root.mkdir()

        write_tree(left_root, with_empty_dir=False)
        write_tree(right_root, with_empty_dir=True)

        left_hash = run_rhsum([left_root])
        right_hash = run_rhsum([right_root])

        if left_hash == right_hash:
            raise SystemExit(
                "directory hash should change when an empty directory is present"
            )

    print(f"without_empty_dir={left_hash} with_empty_dir={right_hash}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
