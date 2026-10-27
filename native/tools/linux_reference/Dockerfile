# Linux x64 reference environment for CodeBrix.Graphics3D.Cycles (native/DEVICE.md, "Reference devices and
# the publish rule"): the dedicated Linux reference machine's toolchain - Debian 13 (trixie), GCC 14.2.0,
# Debian clang 19.1.7 (the WGSL overlay's AST dump), CMake 3.31.6, glibc 2.41 - in a container, so a machine
# without that hardware (the Intel Mac reference device) can compute the correctly rounded references.
# Used by native/tools/macos_x64_reference_exchange.py. Copyright (c) 2026 Jeremy Ellis and contributors.
FROM debian:trixie@sha256:4c25275446ca371766b8b093c696782eabce06263fccacb8d5d813b3dc44b308
# mesa-vulkan-drivers + libvulkan1: Mesa lavapipe, so the gate binaries can create their WebGPU device in their
# CPU-only reference passes (they do no GPU work there; CODEBRIX_CYCLES_WEBGPU_ADAPTER=software selects it).
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential cmake clang python3 nasm pkg-config zstd file git ca-certificates xz-utils \
      mesa-vulkan-drivers libvulkan1 \
    && rm -rf /var/lib/apt/lists/*
# The Docker Desktop VM on an Intel Mac does not expose RDTSCP; see rdtscp_shim.c (LD_PRELOAD).
COPY rdtscp_shim.c /opt/codebrix/rdtscp_shim.c
RUN gcc -O2 -shared -fPIC -o /opt/codebrix/librdtscp_shim.so /opt/codebrix/rdtscp_shim.c
