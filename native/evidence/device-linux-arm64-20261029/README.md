# Linux ARM64 device run on a Raspberry Pi 5 (2026-10-28 to 2026-10-29)

Copyright (c) 2026 Jeremy Ellis and contributors

The first Linux ARM64 GPU evidence: the CodeBrix WGSL/WebGPU device on the
Raspberry Pi 5's V3D 7.1 GPU (Mesa V3DV, Vulkan), with the unchanged shipped
CodeBrix.WebGpu.Native.MitLicenseForever **1.0.300.210** runtime
(`runtimes/linux-arm64/native/libcodebrix_wgpu.so`, SHA-256 `3fb3a382...`;
package SHA-256 `ddfd92f8...`, equal to the `packageSha256` pin in
`third_party/dependencies.json`). Everything was built natively on the Pi from
HEAD `9388e9878` plus the uncommitted Linux ARM64 work described in
docs/TESTING-LINUX-ARM64.md ("Device run 2026-10-28"). The WebGPU runtime was
not built. Write-up, findings L1 and L2 and the verdict: that document.

No executables or libraries are in this directory: `build/host-status.json`
lists the SHA-256 of every built binary, `build/dependencies-marker.json` every
dependency library. Files named `loader-libcodebrix_cycles_host.so.*.txt` are
the loader-closure logs of that library, not the library. Build logs are
renamed from `.log` to `.output.txt` (the repository ignores `*.log`):
`host/configure.log` -> `build/host-configure.output.txt`, `host/build.log` ->
`build/host-build.output.txt`.

## Device

`identity/identity.txt` (section 7 of the test document): Raspberry Pi 5 Model B
Rev 1.1, 4 x Cortex-A76, 16 GB, Debian 13 (Raspberry Pi OS), glibc 2.41, GCC
14.2.0, Debian clang 19.1.7, CMake 3.31.6, Python 3.13.5, .NET SDK 10.0.403;
Mesa 26.2.2 (`mesa-vulkan-drivers` 26.2.2-1~bpo13+0~rpt1), Vulkan loader
1.4.309. Adapter as the host reports it: "V3D 7.1.10.2 / Mesa
26.2.2-1~bpo13+0~rpt1", Vulkan, integrated GPU, tier `full`, workgroup size 64.

## Contents

| Path | What |
|---|---|
| `identity/` | the device identity record |
| `build/` | native build records: dependency prefix (18 components, 806 s), host + every device gate + oracle runner (65 artifacts, 2,572 s; later incremental rebuilds `build-host-2..6`), `platform.patch`, `webgpu-compatibility.json`, and `overlay-check.json` (the generated WGSL against the Linux x64 overlay hashes) |
| `gate-run-4/` | `native/tools/linux_arm64_host_gate.py --device-gates` (native mode), warm Mesa cache; stopped at `abi-gpu` (step limit 3,600 s, cold compile, L2). The 1,560 `reload/` films (checked bitwise in the gate) and the 780 image-reference films (compared with the Linux x64 set in `comparisons.json`; hashes in `invocation.json`) are omitted |
| `device-runs/` | `gpu-cold` (the GPU lifecycle test, empty Mesa cache: 4,571 s, pipeline log), `gpu-warm` (the same, warm: 17.6 s), the libm probe (P2), the managed build (SDK 10.0.403) and the stopped managed test run, the lane run's console output |
| `lanes-1/` | `native/tools/linux_arm64_lanes.py`, three lanes, 10-hour cap, stopped at the maintainer's request after 9.5 hours (`manual-stop.json`: per job, the pipelines compiled and their cold times, the GPU films written before the stop and their comparison, the kernel log of the window). The host gate's `reload/`, `references/`, `binary-review.json` and `invocation.json` (same as `gate-run-4`) are omitted |
| `stopped-attempts/` | gate runs 1 (binary review: kernel UAPI header), 2 (build record stale after a gate edit), 3 (`codebrix_host_gpu_test`: "Requested device is unavailable", L1); `gpu-diag-webgpu-log-info` (the same with wgpu's log); `gpu-first-try-after-L1` (stopped at 1,800 s while compiling) |
| `probe/` | the scratch WebGPU adapter-limit probes that found L1, with their output |

## Results

- Binary review (native): 48 exports, ARM64 ELF, loader closure only within the
  build, the dependency prefix, the pinned runtime and the C/C++ runtime; 547
  system headers, all from `libc6-dev`, `libstdc++-14-dev`, `libgcc-14-dev` and
  `linux-libc-dev`.
- CPU (gate run 4): every same-target film bitwise identical (1,568);
  cross-platform comparisons against the Linux x64 reference not reached (they
  follow the GPU steps).
- libm (P2): bit-identical to Linux x64 glibc for all 11 probed functions.
- WGSL: byte-identical to the Linux x64 reference overlay except the three
  modules containing the F1 fix (`overlay-check.json`).
- GPU films against the same-host CPU references, all within the practical
  criterion: `gpu` 6 and `gpu-exit` 5 (gate run 4: 0 practical failures, 0 strict
  differences, max 1.19e-7); ABI `base` and the four light films (lanes run: 0
  practical failures; max 1.5e-5; 159/22/1/0 strict differences, reported,
  as on Linux x64 Intel).
- Not reached: ABI motion films, example, driver coverage, every device gate
  (cold compiles of hours per pipeline, L2).
