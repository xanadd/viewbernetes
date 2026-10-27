First stable release of viewbernetes: a native C, htop-style Kubernetes monitor.

- Cluster and per-node CPU/memory meters using the selected kubectl context.
- Pod usage table with CPU/memory sorting, namespace filtering and scrolling.
- Two-second default refresh, responsive controls and stale-data warnings.
- Plain snapshots with `--once` and an offline `--demo` mode.
- Prebuilt binaries for Linux x86_64/ARM64 and macOS Intel/Apple Silicon.
- Static Linux binaries include ncurses, musl and common terminal descriptions.
- Binary installer verifies SHA-256 checksums; no compiler or sudo required.

Install:

```sh
curl -fsSL https://github.com/xanadd/viewbernetes/releases/latest/download/install.sh | sh
```

Requires configured `kubectl` and access to the Kubernetes Metrics API (usually
metrics-server). macOS 11+; macOS builds are ad-hoc signed without Developer ID
signing or notarization. Native metrics report aggregate node/container CPU
usage, not individual CPU cores or host processes.
