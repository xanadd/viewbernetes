#!/bin/sh
# Build from GitHub over HTTPS; ask before installing system dependencies.
set -eu
repo=https://github.com/xanadd/viewbernetes
ref=${VIEWBERNETES_REF:-main}
prefix=${PREFIX:-"$HOME/.local"}
case "$ref" in ''|*[!a-zA-Z0-9._-]*) echo 'Invalid VIEWBERNETES_REF (use a tag or commit SHA)' >&2; exit 1;; esac
case "$prefix" in /*) ;; *) echo 'PREFIX must be an absolute path' >&2; exit 1;; esac
work=$(mktemp -d "${TMPDIR:-/tmp}/viewbernetes.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/check.c" <<'CHECK'
#include <ncurses.h>
#include <pthread.h>
int main(void) { initscr(); endwin(); return 0; }
CHECK
check_dependencies() {
    for tool in curl tar make cc install; do
        command -v "$tool" >/dev/null 2>&1 || return 1
    done
    cc -std=c11 "$work/check.c" -o "$work/check" -lncurses -lpthread -lm >"$work/check.log" 2>&1
}
# Fixed argument lists only: do not evaluate package-manager output as shell code.
package_command() {
    case "$manager" in
        apt-get) run_privileged apt-get update && run_privileged apt-get install -y build-essential libncurses-dev curl tar ;;
        dnf) run_privileged dnf install -y gcc make ncurses-devel curl tar ;;
        yum) run_privileged yum install -y gcc make ncurses-devel curl tar ;;
        apk) run_privileged apk add build-base ncurses-dev curl tar ;;
        pacman) run_privileged pacman -S --needed --noconfirm base-devel ncurses curl tar ;;
    esac
}
run_privileged() {
    if [ "$(id -u)" -eq 0 ]; then "$@" < /dev/tty
    else sudo "$@" < /dev/tty
    fi
}
if ! check_dependencies; then
    echo 'Missing build dependencies: C compiler, make, curl, tar, install, or ncurses headers/libraries.' >&2
    manager=
    for candidate in apt-get dnf yum apk pacman; do
        if command -v "$candidate" >/dev/null 2>&1; then manager=$candidate; break; fi
    done
    if [ -z "$manager" ]; then
        echo 'Install a C compiler, make, curl, tar and ncurses development headers, then rerun.' >&2
        echo 'macOS: xcode-select --install; Debian/Ubuntu: sudo apt-get install build-essential libncurses-dev curl tar' >&2
        exit 1
    fi
    case "$manager" in
        apt-get) packages='build-essential libncurses-dev curl tar' ;;
        dnf|yum) packages='gcc make ncurses-devel curl tar' ;;
        apk) packages='build-base ncurses-dev curl tar' ;;
        pacman) packages='base-devel ncurses curl tar' ;;
    esac
    printf 'Install dependencies with %s (%s)? [y/N] ' "$manager" "$packages" >&2
    # stdin contains this script when invoked with curl | sh; use the terminal.
    answer=
    if ! { read -r answer < /dev/tty; } 2>/dev/null; then
        printf '\nNo interactive terminal available. Install dependencies manually and rerun.\n' >&2
        exit 1
    fi
    case "$answer" in
        y|Y|yes|YES|Yes) ;;
        *) echo 'Dependency installation declined. No packages changed.' >&2; exit 1 ;;
    esac
    if [ "$(id -u)" -ne 0 ] && ! command -v sudo >/dev/null 2>&1; then
        echo 'sudo is required to install system packages. Ask an administrator to install the dependencies.' >&2
        exit 1
    fi
    package_command
    if ! check_dependencies; then
        echo 'Build dependency check still failed. Check compiler and ncurses installation before retrying.' >&2
        exit 1
    fi
fi
printf 'Downloading viewbernetes (%s)...\n' "$ref"
curl --fail --silent --show-error --location --proto '=https' --proto-redir '=https' \
    --connect-timeout 15 --max-time 120 "$repo/archive/$ref.tar.gz" -o "$work/source.tar.gz"
mkdir "$work/source"
tar -xzf "$work/source.tar.gz" -C "$work/source" --strip-components=1
printf 'Building (requires a C compiler, make, and ncurses development headers)...\n'
make -C "$work/source"
install -d "$prefix/bin"
# Stage beside the destination so replacement is atomic, including upgrades.
staged=$(mktemp "$prefix/bin/.viewbernetes.XXXXXXXX")
trap 'rm -f "$staged"; rm -rf "$work"' EXIT HUP INT TERM
install -m 755 "$work/source/viewbernetes" "$staged"
mv -f "$staged" "$prefix/bin/viewbernetes"
printf 'Installed %s/bin/viewbernetes\n' "$prefix"
case ":$PATH:" in *":$prefix/bin:"*) ;; *) printf 'Add to your shell profile: export PATH="%s/bin:$PATH"\n' "$prefix";; esac
printf 'Run: viewbernetes (or viewbernetes --demo)\n'
