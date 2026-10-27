#!/bin/sh
# Download a verified release binary. No compiler, package manager, or sudo.
set -eu
repo=https://github.com/xanadd/viewbernetes
version=${VIEWBERNETES_VERSION:-latest}
prefix=${PREFIX:-"$HOME/.local"}
case "$version" in ''|*[!a-zA-Z0-9._-]*) echo 'Invalid VIEWBERNETES_VERSION' >&2; exit 1;; esac
case "$prefix" in /*) ;; *) echo 'PREFIX must be an absolute path' >&2; exit 1;; esac
for tool in curl tar install; do
    command -v "$tool" >/dev/null 2>&1 || { echo "Required tool missing: $tool" >&2; exit 1; }
done
if command -v sha256sum >/dev/null 2>&1; then hash_tool=sha256sum
elif command -v shasum >/dev/null 2>&1; then hash_tool=shasum
else echo 'Required tool missing: sha256sum or shasum' >&2; exit 1
fi
case "$(uname -s)" in Linux) platform=linux;; Darwin) platform=darwin;; *) echo 'Supported systems: Linux and macOS' >&2; exit 1;; esac
case "$(uname -m)" in x86_64|amd64) arch=amd64;; aarch64|arm64) arch=arm64;; *) echo 'Supported architectures: x86_64 and ARM64' >&2; exit 1;; esac
asset="viewbernetes-$platform-$arch.tar.gz"
if [ "$version" = latest ]; then base="$repo/releases/latest/download"
else base="$repo/releases/download/$version"
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/viewbernetes.XXXXXXXX")
staged=
trap 'if [ -n "$staged" ]; then rm -f "$staged"; fi; rm -rf "$work"' EXIT HUP INT TERM
fetch() {
    curl --fail --silent --show-error --location --proto '=https' --proto-redir '=https' \
        --connect-timeout 15 --max-time 120 "$1" -o "$2"
}
printf 'Downloading viewbernetes %s (%s/%s)...\n' "$version" "$platform" "$arch"
fetch "$base/$asset" "$work/$asset"
fetch "$base/SHA256SUMS" "$work/SHA256SUMS"
# Match exactly one entry, rather than trusting extra checksum-file paths.
expected=$(awk -v asset="$asset" '$2 == asset {print $1}' "$work/SHA256SUMS")
case "$expected" in ''|*[!a-fA-F0-9]*) echo 'Missing or invalid release checksum' >&2; exit 1;; esac
[ "${#expected}" -eq 64 ] || { echo 'Invalid release checksum length' >&2; exit 1; }
if [ "$hash_tool" = sha256sum ]; then actual=$(sha256sum "$work/$asset")
else actual=$(shasum -a 256 "$work/$asset")
fi
actual=${actual%% *}
[ "$actual" = "$expected" ] || { echo 'Checksum mismatch. Installation aborted; existing binary unchanged.' >&2; exit 1; }
# Extract only the executable from the verified release archive.
tar -xzf "$work/$asset" -C "$work" viewbernetes
[ -f "$work/viewbernetes" ] && [ ! -L "$work/viewbernetes" ] || { echo 'Invalid release executable' >&2; exit 1; }
chmod 755 "$work/viewbernetes"
"$work/viewbernetes" --version
install -d "$prefix/bin"
staged=$(mktemp "$prefix/bin/.viewbernetes.XXXXXXXX")
install -m 755 "$work/viewbernetes" "$staged"
mv -f "$staged" "$prefix/bin/viewbernetes"
printf 'Installed %s/bin/viewbernetes\n' "$prefix"
case ":$PATH:" in *":$prefix/bin:"*) ;; *) printf 'Add to your shell profile: export PATH="%s/bin:$PATH"\n' "$prefix";; esac
printf 'Run: viewbernetes (or viewbernetes --demo)\n'
