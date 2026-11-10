#!/usr/bin/env bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# ==============================================================================================
# build.sh - the Linux x64 native build, in the digest-pinned manylinux container
# ==============================================================================================
#
#   native/manylinux/build.sh all [--jobs N]     dependencies, then the host, the oracle runner and
#                                                 every gate and test program (--target all)
#   native/manylinux/build.sh dependencies [...]  tools/dependencies/build.py [...]
#   native/manylinux/build.sh cycles [...]        native/tools/build_cycles.py [...] (what a host-side
#                                                 build_cycles.py run hands over to the container)
#   native/manylinux/build.sh image               only build the derived image
#   native/manylinux/build.sh run CMD [ARG...]    run one command in the build container (diagnostics)
#
# Environment:
#   CONTAINER_ENGINE=podman|docker   default: podman, then docker
#   FORCE_IMAGE_REBUILD=1            rebuild the derived image even if it exists
#   NUGET_PACKAGES                   the NuGet cache (default ~/.nuget/packages)
#
# What it does: makes sure the derived image exists (Containerfile.x86_64; building it is the only
# step that may use the network), then runs container-build.sh in it WITH THE NETWORK DISABLED.
# The checkout is mounted read-write at /src (pins.env CONTAINER_ROOT), the WebGPU runtime package
# read-only at its own NuGet cache path. Build trees land in native/work/ as before, owned by you
# (rootless podman maps the container's root to the calling user).
#
# The WGSL overlay (native/work/cycles-overlay) is generated on THIS machine before the container
# starts (python3 native/tools/build_cycles.py --overlay-only, which needs Clang); the container
# only checks it and refuses to build when it is stale. Running native/tools/build_cycles.py on
# this machine does both steps, so tools that build keep working unchanged.
#
# This script installs nothing on this machine.
# ==============================================================================================

set -euo pipefail
trap 'rc=$?; echo "ERROR: build.sh failed (exit $rc) at line $LINENO: $BASH_COMMAND" >&2' ERR

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../.." && pwd)"
# shellcheck source=pins.env
. "$SCRIPT_DIR/pins.env"

if [ -n "${CODEBRIX_CYCLES_CONTAINER:-}" ]; then
    echo "ERROR: build.sh runs on the build host, not inside the build container." >&2; exit 1
fi
[ "$(uname -s)-$(uname -m)" = "Linux-x86_64" ] || { echo "ERROR: the linux-x64 container build runs on a Linux x86_64 host." >&2; exit 1; }

pick_engine() {
    if [ -n "${CONTAINER_ENGINE:-}" ]; then
        command -v "$CONTAINER_ENGINE" > /dev/null 2>&1 || { echo "ERROR: CONTAINER_ENGINE=$CONTAINER_ENGINE is not on PATH." >&2; exit 1; }
        echo "$CONTAINER_ENGINE"; return
    fi
    if command -v podman > /dev/null 2>&1; then echo podman; return; fi
    if command -v docker > /dev/null 2>&1; then echo docker; return; fi
    echo "ERROR: neither podman nor docker is installed (Debian: sudo apt install podman). This script installs nothing." >&2
    exit 1
}
ENGINE="$(pick_engine)"
DERIVED="$DERIVED_IMAGE_PREFIX-x86_64"
BASE="${IMAGE_X64%%:*}@$IMAGE_X64_DIGEST"

ensure_image() {
    if [ "${FORCE_IMAGE_REBUILD:-0}" != "1" ] && "$ENGINE" image exists "$DERIVED" 2>/dev/null; then
        return 0
    fi
    echo "image: building $DERIVED from $BASE ($IMAGE_X64)" >&2
    echo "       (the only step that may use the network: pulling the base image when it is not local)" >&2
    "$ENGINE" build -f "$SCRIPT_DIR/Containerfile.x86_64" -t "$DERIVED" \
        --label "$LABEL_PREFIX=1" \
        --build-arg BASE_IMAGE="$BASE" \
        --build-arg GCC_VERSION="$GCC_VERSION" \
        --build-arg CMAKE_VERSION="$CMAKE_VERSION" \
        --build-arg GLIBC_VERSION="$GLIBC_VERSION" \
        --build-arg PYTHON_IN_IMAGE="$PYTHON_IN_IMAGE" \
        --build-arg PYTHON_VERSION="$PYTHON_VERSION" \
        "$SCRIPT_DIR" >&2
}

MODE="${1:-all}"; [ $# -gt 0 ] && shift
case "$MODE" in
    image) ensure_image; "$ENGINE" image inspect --format '{{.Id}}' "$DERIVED"; exit 0 ;;
    all|dependencies|cycles|run) ;;
    *) echo "usage: $0 [all|dependencies|cycles|image|run] [arguments]" >&2; exit 2 ;;
esac

PACKAGES="${NUGET_PACKAGES:-$HOME/.nuget/packages}"
PACKAGE="$(cd "$PACKAGES/$WEBGPU_PACKAGE_ID/$WEBGPU_PACKAGE_VERSION" 2> /dev/null && pwd)" || {
    echo "ERROR: $PACKAGES/$WEBGPU_PACKAGE_ID/$WEBGPU_PACKAGE_VERSION is missing; run dotnet restore first." >&2; exit 1; }
runtime_sha="$(sha256sum "$PACKAGE/runtimes/linux-x64/native/libcodebrix_wgpu.so" | cut -d' ' -f1)"
[ "$runtime_sha" = "$WEBGPU_RUNTIME_SHA256" ] || { echo "ERROR: the WebGPU runtime in $PACKAGE is not the pinned file ($runtime_sha)." >&2; exit 1; }
[ -x "$REPO/$ISPC_PATH" ] || { echo "ERROR: ISPC $ISPC_VERSION is missing at $ISPC_PATH (MAINTAINER-README.txt, BUILDING)." >&2; exit 1; }
ispc_sha="$(sha256sum "$REPO/$ISPC_PATH" | cut -d' ' -f1)"
[ "$ispc_sha" = "$ISPC_SHA256" ] || { echo "ERROR: $ISPC_PATH is not the official ISPC $ISPC_VERSION binary ($ispc_sha)." >&2; exit 1; }

ensure_image
IMAGE_ID="$("$ENGINE" image inspect --format '{{.Id}}' "$DERIVED")"

if [ "$MODE" = "all" ] || [ "$MODE" = "cycles" ]; then
    # The overlay is made here, with this machine's Clang; the container checks it (--overlay-check).
    if [ -z "${CODEBRIX_CYCLES_OVERLAY_DONE:-}" ]; then
        python3 "$REPO/native/tools/build_cycles.py" --overlay-only
    fi
fi

started="$SECONDS"
echo "build: $MODE in $DERIVED ($IMAGE_ID), checkout $REPO at $CONTAINER_ROOT, network none" >&2
rc=0
"$ENGINE" run --rm --pull=never --network none \
    --name "$DERIVED_IMAGE_PREFIX-$$-$RANDOM" --label "$LABEL_PREFIX=1" \
    -v "$REPO":"$CONTAINER_ROOT" -v "$PACKAGE":"$PACKAGE":ro -w "$CONTAINER_ROOT" \
    -e CODEBRIX_CYCLES_CONTAINER=1 \
    -e CODEBRIX_WEBGPU_PACKAGE="$PACKAGE" \
    -e CODEBRIX_ISPC="$CONTAINER_ROOT/$ISPC_PATH" \
    -e CODEBRIX_BUILD_IMAGE="$DERIVED" -e CODEBRIX_BUILD_IMAGE_ID="$IMAGE_ID" \
    -e CODEBRIX_BUILD_BASE_IMAGE="$IMAGE_X64" -e CODEBRIX_BUILD_BASE_DIGEST="$IMAGE_X64_DIGEST" \
    -e GCC_VERSION="$GCC_VERSION" -e CMAKE_VERSION="$CMAKE_VERSION" -e GLIBC_VERSION="$GLIBC_VERSION" \
    -e PYTHON_IN_IMAGE="$PYTHON_IN_IMAGE" -e ISPC_SHA256="$ISPC_SHA256" \
    "$DERIVED" bash "$CONTAINER_ROOT/native/manylinux/container-build.sh" "$MODE" "$@" || rc=$?
echo "build: $MODE finished with exit $rc in $(( SECONDS - started )) s" >&2
exit "$rc"
