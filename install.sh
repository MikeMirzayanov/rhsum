#!/usr/bin/env bash
set -euo pipefail

REPO_OWNER="${RHSUM_REPO_OWNER:-MikeMirzayanov}"
REPO_NAME="${RHSUM_REPO_NAME:-rhsum}"
REPO_REF="${RHSUM_REF:-main}"
DEFAULT_TARBALL_URL="https://codeload.github.com/${REPO_OWNER}/${REPO_NAME}/tar.gz/refs/heads/${REPO_REF}"
TARBALL_URL="${RHSUM_TARBALL_URL:-$DEFAULT_TARBALL_URL}"

MODE="system"
KEEP_WORKTREE=0
TARGET_NAME="rhsum"
USER_PREFIX="${RHSUM_USER_PREFIX:-$HOME/.local}"
USER_BIN_DIR="${RHSUM_USER_BIN_DIR:-$USER_PREFIX/bin}"
USER_RC_FILES="${RHSUM_USER_RC_FILES:-$HOME/.profile:$HOME/.bashrc}"
SYSTEM_PREFIX="${RHSUM_SYSTEM_PREFIX:-/usr/local}"
SYSTEM_BIN_DIR="${RHSUM_SYSTEM_BIN_DIR:-$SYSTEM_PREFIX/bin}"
SYSTEM_PROFILED="${RHSUM_SYSTEM_PROFILED:-/etc/profile.d/rhsum-path.sh}"

usage() {
    cat <<'EOF'
Usage: install.sh [--user] [--system] [--keep]

Options:
  --user     Install for the current user into ~/.local/bin.
  --system   Install system-wide into /usr/local/bin. Default mode.
  --keep     Do not delete the temporary build directory.
  --help     Show this help.

Environment:
  RHSUM_REPO_OWNER   GitHub owner. Default: MikeMirzayanov
  RHSUM_REPO_NAME    Repository name. Default: rhsum
  RHSUM_REF          Git ref to download. Default: main
  RHSUM_TARBALL_URL  Override the tarball URL completely.
  RHSUM_USER_BIN_DIR Override the user installation bin directory.
  RHSUM_SYSTEM_BIN_DIR Override the system installation bin directory.
  RHSUM_SYSTEM_PROFILED Override the system profile.d path.
EOF
}

download_to_stdout() {
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$1"
        return 0
    fi
    if command -v wget >/dev/null 2>&1; then
        wget -qO- "$1"
        return 0
    fi
    echo "Error: neither curl nor wget is available" >&2
    return 1
}

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Error: required command not found: $1" >&2
        exit 1
    fi
}

while [ $# -gt 0 ]; do
    case "$1" in
        --user)
            MODE="user"
            ;;
        --system)
            MODE="system"
            ;;
        --keep)
            KEEP_WORKTREE=1
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "Error: unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
    shift
done

require_cmd tar
require_cmd python3

if ! command -v g++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1 && ! command -v c++ >/dev/null 2>&1; then
    echo "Error: no supported C++ compiler found in PATH" >&2
    exit 1
fi

if [ "$MODE" = "system" ] && [ "$(id -u)" -ne 0 ]; then
    echo "Error: system installation requires root. Re-run via sudo bash or use --user." >&2
    exit 1
fi

workdir="$(mktemp -d)"
cleanup() {
    if [ "$KEEP_WORKTREE" -eq 0 ]; then
        rm -rf "$workdir"
    else
        echo "Kept work tree: $workdir" >&2
    fi
}
trap cleanup EXIT

echo "Downloading ${REPO_OWNER}/${REPO_NAME}@${REPO_REF} ..." >&2
download_to_stdout "$TARBALL_URL" | tar -xzf - -C "$workdir"

srcdir=""
if [ -f "$workdir/Makefile" ]; then
    srcdir="$workdir"
else
    srcdir="$(find "$workdir" -mindepth 1 -maxdepth 2 -type f -name Makefile -print | head -n 1)"
    srcdir="${srcdir%/Makefile}"
fi

if [ -z "${srcdir:-}" ] || [ ! -f "$srcdir/scripts/build.py" ] || [ ! -f "$srcdir/scripts/run_tests.py" ]; then
    echo "Error: downloaded archive does not look like the rhsum project" >&2
    exit 1
fi

echo "Building and testing in $srcdir ..." >&2
python3 "$srcdir/scripts/build.py"
python3 "$srcdir/scripts/run_tests.py"
python3 "$srcdir/scripts/run_tests.py" --valgrind

install_user() {
    mkdir -p "$USER_BIN_DIR"
    cp "$srcdir/$TARGET_NAME" "$USER_BIN_DIR/$TARGET_NAME"

    old_ifs="$IFS"
    IFS=':'
    for rc in $USER_RC_FILES; do
        touch "$rc"
        if ! grep -Fq "# rhsum-install-path $USER_BIN_DIR" "$rc"; then
            printf '\n# rhsum-install-path %s\nexport PATH="%s:$PATH"\n' "$USER_BIN_DIR" "$USER_BIN_DIR" >> "$rc"
        fi
    done
    IFS="$old_ifs"

    echo "Installed $TARGET_NAME to $USER_BIN_DIR" >&2
    echo "Open a new shell or run: export PATH=\"$USER_BIN_DIR:\$PATH\"" >&2
}

install_system() {
    mkdir -p "$SYSTEM_BIN_DIR"
    mkdir -p "$(dirname "$SYSTEM_PROFILED")"
    cp "$srcdir/$TARGET_NAME" "$SYSTEM_BIN_DIR/$TARGET_NAME"
    printf '# rhsum system path\nexport PATH="%s:$PATH"\n' "$SYSTEM_BIN_DIR" > "$SYSTEM_PROFILED"

    echo "Installed $TARGET_NAME to $SYSTEM_BIN_DIR" >&2
    echo "Installed system PATH snippet to $SYSTEM_PROFILED" >&2
    echo "This affects new login shells for all users." >&2
}

if [ "$MODE" = "system" ]; then
    echo "Installing system-wide ..." >&2
    install_system
else
    echo "Installing for the current user ..." >&2
    install_user
fi

echo "rhsum installation completed." >&2
