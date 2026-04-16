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
    default_output = "rhsum.exe" if os.name == "nt" else "rhsum"
    output = REPO_ROOT / os.environ.get("RHSUM_OUTPUT_NAME", default_output)
    opt_flag = os.environ.get("RHSUM_OPT_FLAG", "/O2" if compiler in {"cl", "cl.exe"} else "-O3")
    debug_flag = os.environ.get("RHSUM_DEBUG_FLAG", "")
    arch_flag = os.environ.get("RHSUM_ARCH_FLAG", "-march=native")

    if compiler in {"cl", "cl.exe"}:
        cmd = [
            cxx,
            "/nologo",
            "/std:c++20",
            opt_flag,
            "/EHsc",
            f"/Fe:{output}",
            str(REPO_ROOT / "rhsum.cpp"),
        ]
        if debug_flag:
            cmd.insert(4, debug_flag)
    else:
        cmd = [
            cxx,
            opt_flag,
            "-std=c++20",
            "-pthread",
            str(REPO_ROOT / "rhsum.cpp"),
            "-o",
            str(output),
        ]
        if debug_flag:
            cmd.insert(3, debug_flag)
        if arch_flag:
            cmd.insert(3, arch_flag)

    subprocess.run(cmd, cwd=REPO_ROOT, check=True)
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
