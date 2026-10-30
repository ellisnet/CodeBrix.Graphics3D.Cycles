# Linux x64 glibc reference, built on the Linux x64 machine (2026-10-29)

Copyright (c) 2026 Jeremy Ellis and contributors

The correctly rounded Linux x64 reference data that every other platform is judged against (native/DEVICE.md,
"Validation reference policy" and "Reference devices and the publish rule"). From 2026-10-29, Linux x64 and Android
testing and validation are done on the dedicated Linux x64 machine only, and the reference files other platforms
compare against come from that machine. This set **supersedes**
`native/evidence/linux-x64-docker-reference-20261027/` (made in Docker on the Intel Mac), which stays as history.
A later run writes a new evidence directory; never edit this one.

## Environment (`environment/`)

| Item | Value |
|---|---|
| Machine | Dell Precision 7770, Intel Core i7-12850HX; GPU Intel UHD Graphics (ADL-S GT1), Mesa 25.0.7-2+deb13u1 (Vulkan) |
| Toolchain | Debian 13, GCC 14.2.0-19, Debian clang 19.1.7 (3+b1), CMake 3.31.6, glibc 2.41 (2.41-12+deb13u4), Python 3.13.5 (`toolchain.txt`) |
| .NET | SDK 10.0.403 |
| Source | commit `686f79282`; the working tree differed only in `docs/TESTING-*.md` (`source-working-tree-status.txt`) |
| WebGPU runtime | CodeBrix.WebGpu.Native.MitLicenseForever 1.0.300.210, `runtimes/linux-x64/native/libcodebrix_wgpu.so` SHA-256 `5a61096e...`, loaded from the NuGet cache; every one of the 76 built binaries links it (`ldd.txt`) |
| Binaries | `linux-bin-sha256.txt` (SHA-256 of every built gate, host and tool); `cycles-build.json` (the build record, CPU kernels at the fixed baseline: SSE4.2, `-ffp-contract=off`, `-fno-reciprocal-math`) |

No container-only additions: the gates ran on the machine's own GPU.

## Contents

| Path | What | How it was made |
|---|---|---|
| `reference/linux-overlay-hashes.json` | SHA-256 of the 19 generated WGSL overlay files | `python3 native/tools/build_cycles_macos_x64.py --write-overlay-hashes native/work/cycles-overlay --output ...` after `python3 tools/build.py --jobs 8` |
| `reference/libm-linux-x64-glibc.json` | glibc 2.41 libm record against correctly rounded results | `python3 native/tools/libm_probe.py linux ...` |
| `host-gate/` | the full Linux host gate **including the GPU steps** (`passed`; 3,146 comparisons, 0 practical failures, 152 strict differences, maximum 1.53e-05). The `reload/` and `managed-reload/` rounds (38 MB) are omitted, identified by `omitted-reload-sha256.txt`; the 837 reference films that `--references` consumers verify are all present | `python3 native/tools/host_gate.py --output ... --jobs 6` |
| `oracles/corpus/` | `cpu-x64-linux` and `wgsl-linux-intel` films of every oracle scene; 54 WGSL films pass against their CPU films (0 practical, maximum 4.77e-05) | `render_oracles.py --backend cpu-x64-linux --backend wgsl-linux-intel` |
| `oracles/bench/` | both backends' films of `bench_01` and `bench_01-hdri` (pass; maximum 9.16e-05) | the same, with `--scene` on the generated bench bundle |
| `oracles/bundle-sha256.txt` | SHA-256 of the 17 bench bundle files (99 MB, not committed; `make_bench_bundle.py` regenerates them) | `native/oracle/scenes/make_bench_bundle.py` |
| `managed-linux-x64/` | Linux glibc CPU films of the managed `GpuRenderTests` scenes (`android_app_gate.py --linux-reference`) | `CODEBRIX_CYCLES_REFERENCE_EXPORT=... dotnet test --configuration Release` |
| `results/` | the run report (`run-report.md`), the regression comparison with the 2026-10-27 run (`cmp-vs-w300.txt`), the CPU films against the Docker set (`cmp_docker.txt`), WGSL against CPU (`oracle-wgsl-vs-cpu.txt`), timeline and budgets | |

## Results (2026-10-29)

- Build: every target, including the two MNEE gates, exit 0. `dotnet test` Release and Debug: 185/185.
- Overlay: 3 of 19 files differ from the Docker set (`background_shader.inc`, `shader_data_shader.inc`,
  `volume_stack_shader.inc`): the modules that embed `native/shaders/wavefront_shader_data.wgsl`, changed by the
  Windows ARM64 F1 fix after the Docker set was made.
- Every `cpu-x64-linux` film is bitwise identical to the Docker set's film of the same scene: the container's
  CPU reference was correct, and this set replaces it without changing any CPU result.
- Linux regression set (the merge-wave-8 job set): 132 job rows, 129 pass. The 3 non-zero rows are the known ones
  (`light_tree_gate.py` at the case-2 replay, the same replay alone, and the by-design Adreno 650 refusal of
  `sim650 nodes-world`). The MNEE gate passes (23,757 paths, 458 strict, maximum 0.125), measured with the current
  runtime for the first time. Strict counts otherwise equal 2026-10-27 up to the known run-to-run atomic family.

## Not in this set

- The device-gate scene exchanges of other devices: they need that device's own scene dump (see
  `native/tools/reference_exchange_handoff.py`). The Intel Mac's exchange of 2026-10-27 remains in the Docker set.
