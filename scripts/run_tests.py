#!/usr/bin/env python3

import os
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

TESTS = [
    "tests/test_constant_1mb_42.py",
    "tests/test_directory_tree.py",
    "tests/test_permission_denied.py",
    "tests/test_follow_symlinks.py",
    "tests/test_follow_symlink_name.py",
    "tests/test_special_name_only.py",
]


def main() -> int:
    env = os.environ.copy()
    if "--valgrind" in sys.argv[1:]:
        if os.name == "nt":
            print("SKIP: valgrind is unavailable on Windows")
            return 0
        if subprocess.run(["sh", "-lc", "command -v valgrind >/dev/null"]).returncode != 0:
            print("SKIP: valgrind is not installed")
            return 0
        env["RHSUM_WRAPPER"] = (
            "valgrind --quiet --tool=memcheck --leak-check=full "
            "--show-leak-kinds=all --errors-for-leak-kinds=all "
            "--track-origins=yes --error-exitcode=101"
        )

    for test_path in TESTS:
        subprocess.run([sys.executable, test_path], cwd=REPO_ROOT, check=True, env=env)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
