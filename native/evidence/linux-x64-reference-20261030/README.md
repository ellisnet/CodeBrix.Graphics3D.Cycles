# Linux x64 glibc reference, built on the Linux x64 machine (2026-10-30)

Copyright (c) 2026 Jeremy Ellis and contributors

The correctly rounded Linux x64 reference data that every other platform is judged against (native/DEVICE.md,
"Validation reference policy" and "Reference devices and the publish rule"), made on the dedicated Linux x64 machine
after the 2026-10-30 Linux fixes and coverage gates: the shadow catcher with ambient occlusion, AO with motion, the
feature-combination gates and oracle scenes, and the surface-shading arithmetic matched to the pinned SSE4.2 kernel
(native/DEVICE.md, Resolved: item 27 and the remaining surface-shading sites). This set **supersedes**
`native/evidence/linux-x64-reference-20261029/`, which stays as history. A later run writes a new evidence directory;
never edit this one.

## Environment (`environment/`)

| Item | Value |
|---|---|
| Machine | Dell Precision 7770, Intel Core i7-12850HX; GPU Intel UHD Graphics (ADL-S GT1), Mesa 25.0.7-2+deb13u1 (Vulkan) |
| Toolchain | Debian 13, GCC 14.2.0-19, Debian clang 19.1.7 (3+b1), CMake 3.31.6, glibc 2.41 (2.41-12+deb13u4), Python 3.13.5 (`toolchain.txt`) |
| .NET | SDK 10.0.403 |
| Source | commit `51fc577cc` plus the uncommitted working tree listed in `source-working-tree-status.txt`; the tree fingerprint (SHA-256 over HEAD and every modified or untracked file) was the same at the start and the end of the run (`fingerprint-start.txt`, `fingerprint-end.txt`) |
| WebGPU runtime | CodeBrix.WebGpu.Native.MitLicenseForever 1.0.300.210, `runtimes/linux-x64/native/libcodebrix_wgpu.so` SHA-256 `5a61096e...`, loaded from the NuGet cache; 64 of the 66 built binaries link it, and the other two (`codebrix_cycles_atomic_film_gate`, Cycles' `zstd_compress` build tool) use no WebGPU (`ldd.txt`) |
| Binaries | `linux-bin-sha256.txt` (SHA-256 of every built gate, host and tool); `cycles-build.json` (the build record, CPU kernels at the fixed baseline: SSE4.2, `-ffp-contract=off`, `-fno-reciprocal-math`) |

Every target was built from clean by `python3 native/tools/build_cycles.py --jobs 6 --target all`, which regenerates
the WGSL overlay before compiling. `results/freshness.txt` records the check that every binary was linked after the
overlay it embeds.

## Contents

| Path | What | How it was made |
|---|---|---|
| `reference/linux-overlay-hashes.json` | SHA-256 of the 19 generated WGSL overlay files | `python3 native/tools/build_cycles_macos_x64.py --write-overlay-hashes native/work/cycles-overlay --output ...` |
| `reference/libm-linux-x64-glibc.json` | glibc 2.41 libm record against correctly rounded results | `python3 native/tools/libm_probe.py linux ...` |
| `host-gate/` | the full Linux host gate **including the GPU steps** (`passed`; 3,146 comparisons, 0 practical failures, 149 strict differences, maximum 1.53e-05). The `reload/` and `managed-reload/` rounds are omitted, identified by `omitted-reload-sha256.txt`; the 831 reference files that `--references` consumers verify are all present | `python3 native/tools/host_gate.py --output ... --jobs 6` |
| `oracles/corpus/` | `cpu-x64-linux` and `wgsl-linux-intel` films of every oracle scene, including the `combinations/` (with the four `enclosure-emissive-panel*` scenes) and `light-motion/` families; 114 WGSL films pass against their CPU films (0 practical failures, 30,813 strict differences, maximum 7.08e-05). The 2 volume scenes are refused by the WGSL device by design; the 2 bench scenes need the bench bundle and are in `oracles/bench/` | `render_oracles.py --backend cpu-x64-linux --backend wgsl-linux-intel` |
| `oracles/bench/` | both backends' films of `bench_01` and `bench_01-hdri` (pass; maximum 9.17e-05) | the same, with `--scene` on the generated bench bundle |
| `oracles/bundle-sha256.txt` | SHA-256 of the 17 bench bundle files (99 MB, not committed; `make_bench_bundle.py` regenerates them) | `native/oracle/scenes/make_bench_bundle.py` |
| `managed-linux-x64/` | Linux glibc CPU films of the managed `GpuRenderTests` scenes (`android_app_gate.py --linux-reference`) | `CODEBRIX_CYCLES_REFERENCE_EXPORT=... dotnet test --configuration Release` |
| `results/` | the run report (`run-report.md`), the regression comparison with the 2026-10-29 run (`cmp-vs-r29.txt`), WGSL against CPU (`oracle-wgsl-vs-cpu.txt`), the per-scene `compare.py` verdicts (`o-compare-summary.txt`), the build freshness check, compiler warning counts, timeline and budgets | |

## Results (2026-10-30)

- Build: every target from clean, exit 0; the only compiler warnings are 49 `-Wclass-memaccess` (byte copies into
  GPU-layout structs). `dotnet build` Release and Debug: 0 warnings. `dotnet test` Release and Debug: 185/185.
- Overlay: 6 of 19 files differ from the 2026-10-29 set (`background_shader.inc`, `shader_data_shader.inc`,
  `shadow_shader.inc`, `surface_shader.inc`, `svm_shader.inc`, `volume_stack_shader.inc`): the modules that embed the
  shaders changed by the 2026-10-30 fixes.
- Every `cpu-x64-linux` film of a scene the 2026-10-29 set has is bitwise identical to that set's film: the fixes
  changed only the WGSL device.
- Linux regression set (the merge-wave-8 job set plus the 2026-10-30 gate cases), every job passes except the
  by-design Adreno 650 refusal of `sim650 nodes-world`. Among them: shadow_catcher_gate 23 cases and 18 replays,
  light_tree_gate 8 cases and 8 replays, ao_state_gate 20 cases, ao_render_gate 24 cases and 24 replays, the MNEE gate,
  the size guard and the Intel compile envelope over every module dump (worst entry 59,274 instructions of 60,000:
  `shade_surface` of an ao_render surface module). Also passing: the licensing audit, `notices.py --check`, the test inventory and guards,
  and the atomic_film, scene_edit, lighting, microfacet (native) and object_motion gates.

## Not in this set

- The device-gate scene exchanges of other devices: they need that device's own scene dump (see
  `native/tools/reference_exchange_handoff.py`). The Intel Mac's exchange of 2026-10-27 remains in
  `native/evidence/linux-x64-docker-reference-20261027/`.
- Android results: not part of this Linux set.
