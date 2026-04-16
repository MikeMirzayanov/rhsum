#!/usr/bin/env python3

import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from testlib import has_mkfifo, run_rhsum, skip


def build_tree(root: Path, fifo_name: str) -> Path:
    (root / "data").mkdir(parents=True)
    (root / "data" / "payload.txt").write_bytes(b"payload\n")
    fifo_path = root / fifo_name
    os.mkfifo(fifo_path)
    return fifo_path


def main() -> int:
    if not has_mkfifo():
        return skip("mkfifo is unavailable on this platform")

    with tempfile.TemporaryDirectory() as left_tmp, tempfile.TemporaryDirectory() as right_tmp:
        left_root = Path(left_tmp) / "tree"
        right_root = Path(right_tmp) / "tree"
        left_root.mkdir()
        right_root.mkdir()

        left_fifo = build_tree(left_root, "pipe-a")
        build_tree(right_root, "pipe-b")

        left_hash = run_rhsum([left_root])
        right_hash = run_rhsum([right_root])
        if left_hash == right_hash:
            raise SystemExit(
                "hash should change when only the name of a non-file special entry changes"
            )

        writer = subprocess.Popen(
            [
                sys.executable,
                "-c",
                (
                    "import os, sys, time; "
                    "fd = os.open(sys.argv[1], os.O_RDWR | os.O_NONBLOCK); "
                    "os.write(fd, b'special-bytes'); "
                    "time.sleep(2); "
                    "os.close(fd)"
                ),
                str(left_fifo),
            ],
        )
        try:
            time.sleep(0.1)
            left_hash_with_writer = run_rhsum([left_root])
        finally:
            writer.wait(timeout=5)

        if left_hash_with_writer != left_hash:
            raise SystemExit(
                "hash should not change when bytes are written into a special non-file entry"
            )

    print(
        f"pipe_a={left_hash} pipe_b={right_hash} pipe_a_with_writer={left_hash_with_writer}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
