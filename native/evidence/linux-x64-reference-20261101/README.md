# Linux x64 glibc reference, built on the Linux x64 machine (2026-11-01)

Copyright (c) 2026 Jeremy Ellis and contributors

The correctly rounded Linux x64 reference data that every other platform is judged against (native/DEVICE.md,
"Validation reference policy" and "Reference devices and the publish rule"), made on the dedicated Linux x64 machine
for the merged tree with the bump-correction and arc adapters, the ARM64 light-sampling and motion adapters, the MNEE
record traversal stack and CPU Open Image Denoise, plus the fixes listed in native/DEVICE.md ("Linux x64 validation of
the merged tree", open item 42). This set **supersedes** `native/evidence/linux-x64-reference-20261030/`, which stays as
history. A later run writes a new evidence directory; never edit this one.

## Environment (`environment/`)

| Item | Value |
|---|---|
| Machine | Dell Precision 7770, Intel Core i7-12850HX; GPU Intel UHD Graphics (ADL-S GT1), Mesa 25.0.7-2+deb13u1 (Vulkan) |
| Toolchain | Debian 13, GCC 14.2.0-19, Debian clang 19.1.7 (3+b1), CMake 3.31.6, glibc 2.41 (2.41-12+deb13u4), Python 3.13.5 (`toolchain.txt`) |
| .NET | SDK 10.0.403 |
| Source | commit `f1ed54307` plus the working tree listed in `source-working-tree-status.txt`; `fingerprint-start.txt` is the tree before any change, `fingerprint-end.txt` the tested tree (the code changes, before this evidence and the documentation were written) |
| WebGPU runtime | CodeBrix.WebGpu.Native.MitLicenseForever 1.0.300.210, `runtimes/linux-x64/native/libcodebrix_wgpu.so` SHA-256 `5a61096e...`, loaded from the NuGet cache; 77 of the 79 built binaries link it, and the other two (`codebrix_cycles_atomic_film_gate`, Cycles' `zstd_compress` build tool) use no WebGPU (`ldd.txt`) |
| Binaries | `linux-bin-sha256.txt` (SHA-256 of every built gate, host and tool); `cycles-build.json` (the build record, CPU kernels at the fixed baseline: SSE4.2, `-ffp-contract=off`, `-fno-reciprocal-math`) |

Every target was built from clean by `python3 native/tools/build_cycles.py --jobs 6 --target all`, which regenerates
the WGSL overlay before compiling; a second run compiled nothing. `results/freshness.txt` records the check that every
binary was linked after the overlay it embeds.

## Contents

| Path | What | How it was made |
|---|---|---|
| `reference/linux-overlay-hashes.json` | SHA-256 of the 21 generated WGSL overlay files | `python3 native/tools/build_cycles_macos_x64.py --write-overlay-hashes native/work/cycles-overlay --output ...` |
| `reference/libm-linux-x64-glibc.json` | glibc 2.41 libm record against correctly rounded results | `python3 native/tools/libm_probe.py linux ...` |
| `host-gate/` | the full Linux host gate **including the GPU steps** (`passed`; 3,146 comparisons, 0 practical failures, 153 strict differences, maximum 1.53e-05). The `reload/` and `managed-reload/` rounds are omitted, identified by `omitted-reload-sha256.txt` | `python3 native/tools/host_gate.py --output ... --jobs 6` |
| `oracles/corpus/` | `cpu-x64-linux` and `wgsl-linux-intel` films of every committed oracle scene (231 scene documents, including the pass, sampling, camera, lamp, sky and denoising-feature families); 229 WGSL films are compared with their CPU films: 228 perfect, `passes/cryptomatte` not perfect (native/DEVICE.md open item 44); 0 practical failures in the films, 61,635 strict differences, maximum 7.06e-05. The 2 volume scenes are refused by the WGSL device by design; the 2 bench scenes need the bench bundle and are in `oracles/bench/` | `render_oracles.py --backend cpu-x64-linux --backend wgsl-linux-intel` |
| `oracles/bench/` | both backends' films of `bench_01` and `bench_01-hdri` (pass) | the same, with `--scene` on the generated bench bundle |
| `oracles/bundle-sha256.txt` | SHA-256 of the bench bundle files (not committed; `make_bench_bundle.py` regenerates them) | `native/oracle/scenes/make_bench_bundle.py` |
| `managed-linux-x64/` | Linux glibc CPU films of the managed `GpuRenderTests` scenes | `CODEBRIX_CYCLES_REFERENCE_EXPORT=... dotnet test --configuration Release` |
| `results/` | the run report (`run-report.md`), WGSL against CPU per scene (`oracle-wgsl-vs-cpu.txt`, `corpus-totals.txt`), the CPU films against the previous Linux run (`cmp-cpu.txt`), the build freshness check, compiler warning counts, timeline and budgets | |

## Results

- Build: every target from clean, exit 0; the only compiler warnings are 67 `-Wclass-memaccess` (byte copies into
  GPU-layout structs). `dotnet build` Release and Debug: 0 warnings. `dotnet test` Release and Debug: 203/203.
- Every `cpu-x64-linux` film of a scene the previous Linux run had is bitwise identical to that run's film, except the
  wall-clock render-time pass of `passes/data-passes`.
- Every WGSL film's `meta.json` records `wgslArithmetic` `x64-sse4.2` and the RID.
- Compile envelope (Intel Mesa 25.0.7, 60,000 backend instructions): every distinct entry of the corpus dump (2,449)
  and of the gate dumps is inside it except one Principled superset group's `shade_surface` (61,059; open item 43).
  Size guard: no corpus scene's device build warns.

## Not in this set

- The device-gate scene exchanges of other devices (see `native/tools/reference_exchange_handoff.py`).
- Android results: not part of this Linux set.
