#!/usr/bin/env python3

import os
import shutil
import shlex
import subprocess
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def rhsum_bin() -> Path:
    override = os.environ.get("RHSUM_BIN")
    if override:
        return Path(override)

    names = ["rhsum.exe", "rhsum"] if os.name == "nt" else ["rhsum", "rhsum.exe"]
    for name in names:
        candidate = REPO_ROOT / name
        if candidate.exists():
            return candidate
    raise SystemExit("rhsum binary not found; build it first or set RHSUM_BIN")


def run_rhsum(args, cwd=None, timeout=10) -> str:
    wrapper = shlex.split(os.environ.get("RHSUM_WRAPPER", ""))
    effective_timeout = timeout * 10 if wrapper else timeout
    completed = subprocess.run(
        [*wrapper, str(rhsum_bin()), *map(str, args)],
        cwd=str(cwd or REPO_ROOT),
        check=True,
        capture_output=True,
        text=True,
        timeout=effective_timeout,
    )
    return completed.stdout.strip()


def skip(reason: str) -> int:
    print(f"SKIP: {reason}")
    return 0


def can_create_symlinks() -> bool:
    if not hasattr(os, "symlink"):
        return False
    with tempfile.TemporaryDirectory() as tmpdir:
        root = Path(tmpdir)
        target = root / "target"
        link = root / "link"
        target.mkdir()
        try:
            os.symlink(target, link)
        except (OSError, NotImplementedError):
            return False
    return True


def can_follow_directory_symlinks() -> bool:
    if not can_create_symlinks():
        return False

    with tempfile.TemporaryDirectory() as tmpdir:
        base = Path(tmpdir)
        root = base / "tree"
        target = base / "target"

        root.mkdir()
        target.mkdir()
        (target / "payload.bin").write_bytes(b"probe")

        try:
            os.symlink(target, root / "linked")
        except (OSError, NotImplementedError):
            return False

        try:
            no_follow_hash = run_rhsum(["-R", root], cwd=REPO_ROOT)
            follow_hash = run_rhsum(["-R", "-L", root], cwd=REPO_ROOT)
        except Exception:
            return False

        return no_follow_hash != follow_hash


def has_mkfifo() -> bool:
    return hasattr(os, "mkfifo")


def has_valgrind() -> bool:
    return shutil.which("valgrind") is not None


def default_cxx() -> str:
    configured = os.environ.get("CXX")
    if configured:
        return configured

    if os.name == "nt":
        candidates = ("cl", "g++", "clang++")
    else:
        candidates = ("g++", "clang++", "c++")

    for candidate in candidates:
        if shutil.which(candidate):
            return candidate

    raise SystemExit("no suitable C++ compiler found")
