# Linux x64 reference toolchain for CodeBrix.Graphics3D.Cycles (the dedicated Linux reference
# machine: Debian 13 trixie, GCC 14.2.0, Debian clang 19.1.7, CMake 3.31.6, glibc 2.41).
FROM debian:trixie
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential cmake clang python3 nasm pkg-config zstd file git ca-certificates xz-utils \
    && rm -rf /var/lib/apt/lists/*
