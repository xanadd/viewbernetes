# viewbernetes

An htop-style Kubernetes monitor, written in C with ncurses. See cluster CPU and
memory, per-node meters, and pods ranked by resource usage. It uses `kubectl` and
your currently selected context; no agent, Prometheus, or credentials copied into
the application.

```sh
curl -fsSL https://github.com/xanadd/viewbernetes/releases/latest/download/install.sh | sh
viewbernetes
```

The installer detects Linux/macOS and x86_64/ARM64, downloads the matching
prebuilt executable, checks its SHA-256 checksum, and installs to `~/.local/bin`.
No compiler, ncurses development headers, package installation, or sudo required.
Add that directory to your `PATH` if prompted. Linux binaries statically include
musl and ncurses, plus fallback descriptions for common terminals (xterm,
screen, tmux, Linux console, vt100 and ANSI). macOS binaries use Apple's system
libraries and require macOS 11 or newer. macOS builds are ad-hoc signed, without
Apple Developer ID signing or notarization.

You still need `kubectl` configured for a cluster with metrics-server. Installation
requires `curl`, `tar`, and `sha256sum` (Linux) or `shasum` (macOS).

To inspect the script or select a version and destination:

```sh
curl -fsSLo install-viewbernetes.sh https://github.com/xanadd/viewbernetes/releases/latest/download/install.sh
less install-viewbernetes.sh
PREFIX="$HOME/tools" VIEWBERNETES_VERSION=v1.0.0 sh install-viewbernetes.sh
```

For an entirely pinned download, replace `latest/download` in the URL with
`download/v1.0.0`. Checksums detect corrupted or mismatched downloads; both
archives and checksums are delivered over HTTPS from the release. An update
replaces the old binary only after verification. To uninstall, remove
`~/.local/bin/viewbernetes` (or the chosen prefix).

Building from source remains available with `make`, or `install-source.sh`.
The source installer asks before installing missing build dependencies on
supported Linux package managers; Enter defaults to no. It needs a C11 compiler,
make and ncurses development headers. Debian/Ubuntu: `build-essential
libncurses-dev`; Fedora: `gcc make ncurses-devel`; macOS: Xcode Command Line Tools.

## Use

```sh
viewbernetes                       # current kubectl context, all namespaces
viewbernetes -n production -i 10    # scope pod metrics, refresh every 10 seconds
viewbernetes --once                # plain snapshot for pipes and logs
viewbernetes --demo                # simulated cluster, no kubectl calls
```

Select a different context with `kubectl config use-context NAME` before launching.
The context is resolved once and pinned for the session, so another terminal's
context switch will not unexpectedly change the displayed cluster. Standard
`KUBECONFIG`, kubectl authentication plugins, and RBAC apply.

| Key | Action |
| --- | --- |
| `c` / `m` | Sort pods by CPU / memory |
| `j` / `k`, down / up | Scroll pods |
| `n` / `p` | Scroll nodes |
| `r` | Request a refresh |
| `q`, Ctrl-C | Quit |

Metrics refresh every two seconds by default; use `-i SECONDS` to change the interval.

The interface resizes with the terminal (minimum 70 columns by 18 rows). Metrics
are fetched in a background thread so keyboard controls remain responsive.
Requests time out; a failed refresh preserves the previous snapshot with an
explicit stale warning. `--once` exits nonzero on fetch/parse failures. Missing
individual node samples display `N/A` and are excluded from both numerator and
denominator of cluster percentages. The measured-node count shows coverage.

```text
 viewbernetes 1.0.0 / demo-cluster [DEMO]
 3/3 nodes measured | 12 pods | scope: all namespaces | sort: CPU

 CLUSTER CPU 11.40 / 24 cores        MEMORY 45.0 / 96.0 GiB
 [|||||||||..........] 47.5%         [|||||||||..........] 46.9%

 NODES                              CPU          MEMORY
 worker-01 (8 cores)                 26.2%        28.1%
 worker-02 (8 cores)                 47.5%        46.9%
 worker-03 (8 cores)                 68.8%        65.6%

 NAMESPACE      POD                         CPU m    MEM MiB
 kube-system    api-7c9b4d-0000              2400      1800.0
 production     worker-7c9b4d-0001           1200      1680.0
 production     cache-7c9b4d-0002             800      1560.0
```

Illustrative compact layout; run `--demo` to see the live interface.

## Metrics and access

The cluster must expose the native [Kubernetes Metrics API](https://kubernetes.io/docs/tasks/debug/debug-cluster/resource-metrics-pipeline/),
usually provided by metrics-server. The application only reads:

- `nodes` from the core API (capacity and Ready condition).
- `nodes` and `pods` from `metrics.k8s.io/v1beta1` (usage and sample timestamp).

Your identity needs `list` on core `nodes` and metrics `nodes`, plus `list` on
metrics `pods` across namespaces, or within the selected namespace with `-n`.
It never installs metrics-server or changes cluster resources. Failure messages
omit kubectl stderr because authentication plugins can include sensitive data.

CPU percentages use physical node capacity, not allocatable capacity. A pod's
CPU is shown in millicores (1000m = one core); memory is the Metrics API working
set in MiB. Pod usage sums every reported container. Namespace filtering affects
only the pod table, while node and cluster meters remain cluster-wide. Node
usage includes system activity, so it need not equal summed pod usage.

Native resource metrics aggregate CPU at node/container level: they do **not**
provide individual CPU-core percentages, OS PIDs, or host process names. This
monitor shows real per-node core utilization and a pod table in place of htop's
process table. It does not fabricate per-core data. Samples refresh at the
metrics-server cadence; polling faster does not create new measurements.

## Build and test

```sh
make
make test
./viewbernetes --demo
make install                       # PREFIX defaults to ~/.local
```

Tests run offline with a fake `kubectl` and synthetic fixtures. They cover
quantity conversion, malformed JSON, context pinning, multi-container sums,
missing metrics, namespace scope, error redaction, sorting, terminal sizes,
keyboard/signal exit, slow-request cancellation, and the installer. They never
read kubeconfig or require access to a cluster. CI runs them on macOS and Linux.
For a live smoke test after selecting a test cluster, run `viewbernetes --once`
and compare with `kubectl top nodes` and `kubectl top pods -A`; capacity-based
percentages can differ from kubectl's default allocatable basis.

## Releases

`.github/workflows/release.yml` builds and tests all four platforms on native
runners. Linux uses a checksum-pinned ncurses source release and musl for static
linking. Terminal tests run with external terminfo paths disabled to exercise the
embedded fallbacks. Archives include applicable ncurses and musl license notices.

Run the Release workflow manually to validate builds without publishing. Pushing
a tag matching the C source version (for example `v1.0.0`) publishes the four
archives, `install.sh`, and `SHA256SUMS` only after every build and test passes.
The release is assembled as a draft and then published once all assets exist.
Keep the source version and installer tests in sync when preparing a new version.
