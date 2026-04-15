#!/usr/bin/env python3

import tempfile
from pathlib import Path

from testlib import run_rhsum

P = 1000000000000037
MOD = 1 << 64
FILE_SIZE = 1024 * 1024
BYTE_VALUE = 42
EXPECTED_RAW_HASH = "d97a894407600000"


def compute_raw_hash(data: bytes) -> str:
    h = 0
    p = 1
    for byte in data:
        h = (h + byte * p) % MOD
        p = (p * P) % MOD
    return f"{h:016x}"


def compute_directory_hash(relative_path: str, data: bytes) -> str:
    path_bytes = relative_path.encode()
    meta = bytes([0]) + len(path_bytes).to_bytes(4, "little") + path_bytes
    meta_hash = compute_raw_hash(meta)
    data_hash = compute_raw_hash(data)
    meta_size = len(meta)
    final_hash = (int(meta_hash, 16) + int(data_hash, 16) * pow(P, meta_size, MOD)) % MOD
    return f"{final_hash:016x}"


def main() -> int:
    data = bytes([BYTE_VALUE]) * FILE_SIZE
    raw_hash = compute_raw_hash(data)
    if raw_hash != EXPECTED_RAW_HASH:
        raise SystemExit(
            f"raw hash mismatch: expected {EXPECTED_RAW_HASH}, got {raw_hash}"
        )

    with tempfile.TemporaryDirectory() as tmpdir:
        file_path = Path(tmpdir) / "test.bin"
        file_path.write_bytes(data)
        file_hash = run_rhsum(["test.bin"], cwd=tmpdir)
        if file_hash != EXPECTED_RAW_HASH:
            raise SystemExit(
                f"file hash mismatch: expected {EXPECTED_RAW_HASH}, got {file_hash}"
            )

        dir_hash = run_rhsum([tmpdir])
        expected_dir_hash = compute_directory_hash("test.bin", data)
        if dir_hash != expected_dir_hash:
            raise SystemExit(
                f"directory hash mismatch: expected {expected_dir_hash}, got {dir_hash}"
            )

    print(f"raw={raw_hash} file={EXPECTED_RAW_HASH} dir={expected_dir_hash}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
