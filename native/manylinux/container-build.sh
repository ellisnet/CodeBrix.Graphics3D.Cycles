#!/usr/bin/env bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# ==============================================================================================
# container-build.sh - the Linux x64 native build INSIDE the build container
# ==============================================================================================
#
# Do not run this on your workstation: build.sh runs it in codebrix-cycles-build-x86_64 with the
# network disabled, the checkout mounted at /src and the WebGPU runtime package mounted read-only.
#
#   container-build.sh all [--jobs N]       dependencies, then build_cycles.py --target all
#   container-build.sh dependencies [...]   python3 tools/dependencies/build.py [...]
#   container-build.sh cycles [...]         python3 native/tools/build_cycles.py [...]
#   container-build.sh run CMD [ARG...]     one command (diagnostics)
#
# Everything compiled comes from the checkout: the pinned Cycles import, the dependency snapshots
# in third_party/deps, the CodeBrix sources, the handed-in WGSL overlay (checked, never made here).
# ==============================================================================================

set -euo pipefail
trap 'rc=$?; echo "ERROR: container-build.sh failed (exit $rc) at line $LINENO: $BASH_COMMAND" >&2' ERR

[ -n "${CODEBRIX_CYCLES_CONTAINER:-}" ] || { echo "ERROR: container-build.sh runs only inside the build container (native/manylinux/build.sh)." >&2; exit 1; }
cd /src
[ -f native/manylinux/pins.env ] || { echo "ERROR: /src is not a CodeBrix.Graphics3D.Cycles checkout." >&2; exit 1; }

MODE="${1:-all}"; [ $# -gt 0 ] && shift

# The build may not reach outside the checkout: only the loopback interface exists.
ifaces="$(ls /sys/class/net 2> /dev/null | tr '\n' ' ' | sed 's/ *$//')"
if [ -n "$ifaces" ] && [ "$ifaces" != "lo" ]; then
    echo "ERROR: the build container has a network interface ($ifaces); build.sh runs it with --network none." >&2; exit 1
fi

# The toolchain the pins name, every run.
[ "$(gcc -dumpfullversion)" = "$GCC_VERSION" ] || { echo "ERROR: GCC $(gcc -dumpfullversion), pinned $GCC_VERSION" >&2; exit 1; }
cmake --version | sed -n 1p | grep -qx "cmake version $CMAKE_VERSION" || { echo "ERROR: CMake is not $CMAKE_VERSION" >&2; exit 1; }
ldd --version | sed -n 1p | grep -q " $GLIBC_VERSION\$" || { echo "ERROR: glibc is not $GLIBC_VERSION" >&2; exit 1; }
[ "$(sha256sum "$CODEBRIX_ISPC" | cut -d' ' -f1)" = "$ISPC_SHA256" ] || { echo "ERROR: $CODEBRIX_ISPC is not the pinned ISPC" >&2; exit 1; }
[ -f "$CODEBRIX_WEBGPU_PACKAGE/runtimes/linux-x64/native/libcodebrix_wgpu.so" ] || { echo "ERROR: the WebGPU runtime package is not mounted" >&2; exit 1; }
PY="$PYTHON_IN_IMAGE"

if [ "$MODE" != "run" ]; then
    echo "container: $(grep -m1 PRETTY_NAME /etc/os-release | cut -d= -f2- | tr -d '"'); $(gcc --version | sed -n 1p); $(cmake --version | sed -n 1p); $(ldd --version | sed -n 1p); $("$PY" --version)"
    echo "container: image $CODEBRIX_BUILD_IMAGE ($CODEBRIX_BUILD_IMAGE_ID), base $CODEBRIX_BUILD_BASE_IMAGE@$CODEBRIX_BUILD_BASE_DIGEST; network: ${ifaces:-none}"
fi

jobs_of() {
    # The --jobs value of the arguments (default 6).
    local jobs=6
    while [ $# -gt 0 ]; do
        case "$1" in --jobs) jobs="$2"; shift 2 ;; --jobs=*) jobs="${1#--jobs=}"; shift ;; *) shift ;; esac
    done
    echo "$jobs"
}

case "$MODE" in
    dependencies)
        exec "$PY" tools/dependencies/build.py "$@" ;;
    cycles)
        exec "$PY" native/tools/build_cycles.py "$@" ;;
    all)
        jobs="$(jobs_of "$@")"
        started="$SECONDS"
        "$PY" tools/dependencies/build.py --jobs "$jobs"
        echo "container: dependencies built in $(( SECONDS - started )) s"
        started="$SECONDS"
        "$PY" native/tools/build_cycles.py --jobs "$jobs" --target all
        echo "container: host, oracle runner, gates and tests built in $(( SECONDS - started )) s" ;;
    run)
        [ $# -gt 0 ] || { echo "usage: container-build.sh run CMD [ARG...]" >&2; exit 2; }
        exec "$@" ;;
    *)
        echo "usage: container-build.sh [all|dependencies|cycles|run] [arguments]" >&2; exit 2 ;;
esac
