#!/usr/bin/env bash
set -euo pipefail

MODE="system"
TARGET_NAME="rhsum"
USER_PREFIX="${RHSUM_USER_PREFIX:-$HOME/.local}"
USER_BIN_DIR="${RHSUM_USER_BIN_DIR:-$USER_PREFIX/bin}"
USER_RC_FILES="${RHSUM_USER_RC_FILES:-$HOME/.profile:$HOME/.bashrc}"
SYSTEM_PREFIX="${RHSUM_SYSTEM_PREFIX:-/usr/local}"
SYSTEM_BIN_DIR="${RHSUM_SYSTEM_BIN_DIR:-$SYSTEM_PREFIX/bin}"
SYSTEM_PROFILED="${RHSUM_SYSTEM_PROFILED:-/etc/profile.d/rhsum-path.sh}"

usage() {
    cat <<'EOF'
Usage: uninstall.sh [--user] [--system]

Options:
  --user     Remove the user installation from ~/.local/bin.
  --system   Remove the system-wide installation. Default mode.
  --help     Show this help.

Environment:
  RHSUM_USER_BIN_DIR Override the user installation bin directory.
  RHSUM_SYSTEM_BIN_DIR Override the system installation bin directory.
  RHSUM_SYSTEM_PROFILED Override the system profile.d path.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --user)
            MODE="user"
            ;;
        --system)
            MODE="system"
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

if [ "$MODE" = "system" ] && [ "$(id -u)" -ne 0 ]; then
    echo "Error: system uninstall requires root. Re-run via sudo bash or use --user." >&2
    exit 1
fi

remove_user_path_entry() {
    local rc="$1"
    local marker="# rhsum-install-path $USER_BIN_DIR"
    local pathline="export PATH=\"$USER_BIN_DIR:\$PATH\""

    [ -f "$rc" ] || return 0
    awk -v marker="$marker" -v pathline="$pathline" '
        $0 == marker { skip=1; next }
        skip && $0 == pathline { skip=0; next }
        { skip=0; print }
    ' "$rc" > "$rc.tmp" && mv "$rc.tmp" "$rc"
}

uninstall_user() {
    rm -f "$USER_BIN_DIR/$TARGET_NAME"

    local old_ifs="$IFS"
    IFS=':'
    for rc in $USER_RC_FILES; do
        remove_user_path_entry "$rc"
    done
    IFS="$old_ifs"

    echo "Removed $TARGET_NAME from $USER_BIN_DIR" >&2
}

uninstall_system() {
    rm -f "$SYSTEM_BIN_DIR/$TARGET_NAME"
    rm -f "$SYSTEM_PROFILED"

    echo "Removed $TARGET_NAME from $SYSTEM_BIN_DIR" >&2
    echo "Removed system PATH snippet $SYSTEM_PROFILED" >&2
}

if [ "$MODE" = "system" ]; then
    uninstall_system
else
    uninstall_user
fi

echo "rhsum uninstall completed." >&2
