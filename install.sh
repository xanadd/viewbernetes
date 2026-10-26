#!/bin/sh
# Build from GitHub over HTTPS; install for the current user without sudo.
set -eu
repo=https://github.com/xanadd/viewbernetes
ref=${VIEWBERNETES_REF:-main}
prefix=${PREFIX:-"$HOME/.local"}
case "$ref" in ''|*[!a-zA-Z0-9._-]*) echo 'Invalid VIEWBERNETES_REF (use a tag or commit SHA)' >&2; exit 1;; esac
for tool in curl tar make cc install; do
    command -v "$tool" >/dev/null 2>&1 || { echo "Required tool missing: $tool" >&2; exit 1; }
done
case "$prefix" in /*) ;; *) echo 'PREFIX must be an absolute path' >&2; exit 1;; esac
work=$(mktemp -d "${TMPDIR:-/tmp}/viewbernetes.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
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
