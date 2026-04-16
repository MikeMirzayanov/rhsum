#!/usr/bin/env python3

import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tests"))
from testlib import REPO_ROOT, default_cxx  # noqa: E402


def main() -> int:
    cxx = default_cxx()
    compiler = Path(cxx).name.lower()
    output = REPO_ROOT / ("rhsum.exe" if os.name == "nt" else "rhsum")

    if compiler in {"cl", "cl.exe"}:
        cmd = [
            cxx,
            "/nologo",
            "/std:c++20",
            "/O2",
            "/EHsc",
            f"/Fe:{output}",
            str(REPO_ROOT / "rhsum.cpp"),
        ]
    else:
        cmd = [
            cxx,
            "-O3",
            "-std=c++20",
            "-march=native",
            "-pthread",
            str(REPO_ROOT / "rhsum.cpp"),
            "-o",
            str(output),
        ]

    subprocess.run(cmd, cwd=REPO_ROOT, check=True)
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
