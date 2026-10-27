# Linux x64 glibc reference, built in Docker (2026-10-27)

Copyright (c) 2026 Jeremy Ellis and contributors

Correctly rounded Linux x64 reference data for validating the Intel Mac mini
(2018, osx-x64) as a reference device (docs/TESTING-MACOS-X64.md; native/DEVICE.md,
"Validation reference policy"). Produced on that Mac in a Docker Desktop Linux VM
whose toolchain is the dedicated Linux reference machine's. **Replaceable:** a
run of the same commands on the dedicated Linux hardware supersedes this set
(write it to a new evidence directory; never edit this one).

## Environment (`environment/`)

| Item | Value |
|---|---|
| Image | `environment/Dockerfile`: `debian:trixie@sha256:4c25275446ca371766b8b093c696782eabce06263fccacb8d5d813b3dc44b308` + build-essential, cmake, clang, python3, nasm, pkg-config, zstd, file, git |
| Toolchain | Debian 13.7, GCC 14.2.0-19, Debian clang 19.1.7 (3+b1) (the overlay's AST dump), CMake 3.31.6, glibc 2.41 (2.41-12+deb13u4), Python 3.13.5, NASM 2.16.03 - the dedicated Linux machine's versions |
| Kernel / CPU | Linux 7.0.14-linuxkit (Docker Desktop VM), the Mac's Intel Core i7-8700B |
| Source | commit 0e95e5ce3 plus the uncommitted osx-x64 work of 2026-10-27 (`environment/source-working-tree-status.txt`); copied into a Docker volume (case-sensitive ext4) |
| Runtime | CodeBrix.WebGpu.Native.MitLicenseForever 1.0.300.210 from the Mac's NuGet cache, mounted read-only |
| Binaries | `environment/toolchain.txt` (SHA-256 of the host library, the oracle runner and the exchanged gates; the fixed-baseline CPU kernel line: SSE4.2, `-ffp-contract=off`, `-fno-reciprocal-math`) |

Two container-only additions, neither of which reaches a pixel value:

- **`environment/rdtscp_shim.c`** (preloaded with `LD_PRELOAD`). The Docker
  Desktop VM does not expose the RDTSCP instruction (the host CPU has it), and
  Cycles' profiling timer `ccl::time_fast_tick` executes it, so every CPU render
  died with SIGILL. The shim catches that SIGILL, emulates RDTSCP with RDTSC and
  resumes; any other illegal instruction is re-raised. The timer feeds only
  timing statistics.
- **Mesa lavapipe** (`mesa-vulkan-drivers` 25.0.7-2+deb13u1, `libvulkan1`
  1.4.309.0-1) with `CODEBRIX_CYCLES_WEBGPU_ADAPTER=software`, installed after
  the image for the scene-exchange passes only: the gate binaries create a
  WebGPU device even in their CPU-only reference pass, which does no GPU work.

## Contents

| Path | What | How it was made |
|---|---|---|
| `linux-overlay-hashes.json` | SHA-256 of the 19 generated WGSL overlay files | `python3 native/tools/build_cycles_macos_x64.py --write-overlay-hashes native/work/cycles-overlay --output ...` after `tools/dependencies/build.py --jobs 8` and `native/tools/build_cycles.py --jobs 6` |
| `host-gate-cpu-only/` | the Linux host gate's CPU steps (`passed`, `cpuOnly`; 1,568 comparisons, 0 practical failures) - session references, image references a/b/c, consumers, concurrency; the `reload/` rounds (18 MB, checked bitwise against `references/`) are omitted | `python3 native/tools/host_gate.py --output ... --cpu-only` (no GPU, no .NET in the container) |
| `oracles/linux-docker-2/` | `cpu-x64-linux` films of every oracle scene (the in-corpus bench entries need the generated bundle and fail there, as on every platform) | `render_oracles.py --backend cpu-x64-linux` |
| `oracles/linux-docker-bench-2/` | `cpu-x64-linux` films of `assets/bench_01` and `bench_01-hdri` | the same, with `--scene` on the bundle `make_bench_bundle.py` generated and verified on the Mac |
| `exchange/light_tree/`, `exchange/microfacet_render/` | the device-scene flow (DEVICE.md): the Mac's dumped scene tables (`mac-scenes/`), the glibc references computed from exactly those tables (`linux-glibc-references/`, provenance with the Linux-versus-Mac input deviation), the Mac GPU's verdict against them, and for `microfacet_render` the Linux stage judge of all 149 recorded GPU stages (`microfacet_render.stage-judge.json`; the 25 MB stages record is identified by `mac-stages.bin.sha256`) | `android_reference.py scene-reference` and `stage-judge` |
| `exchange/mnee/` | the MNEE stage-state exchange (new in `mnee_gate.cpp`): the scene tables the Mac's record pass used (`mac-scenes/`), its report (1,131 checks, 3 local Apple-libm differences as a diagnostic), the 155 MB record identified by `mac-stages.bin.sha256`, the gate binaries' and source's hashes, and the Linux judge's verdict (`mnee.stage-judge.json`) | Mac: `CODEBRIX_CYCLES_STAGE_RECORD=...`; Linux: `android_reference.py stage-judge --gate mnee` |
| `comparisons/` | every Mac film (WGSL and Apple-libm CPU) against these Linux films; `SUMMARY.json` | `native/oracle/compare.py` |

## Results (2026-10-27)

- Overlay: the Mac's Apple Clang build generated the same 19 files (`--reference-overlay`: identical).
- Oracle corpus and bench: **56 of 56 WGSL films from the Mac GPU pass** against
  these glibc CPU films (maximum error 9.2e-5, `bench_01-hdri`); the two volume
  scenes are refused on WebGPU (every platform). The Mac's own CPU film fails
  one scene against glibc: `linux-gates/triangle-lighting-2` (3 practical
  failures) - Apple's libm, not the GPU.
- `light_tree`, `microfacet_render` and `mnee` device gates: pass against the
  glibc references and stage judges, where they failed against the Mac's
  in-process Apple-libm reference (`mnee`: 1,131 checks, 2.36 million exact
  integer and 5.48 million float checks).
