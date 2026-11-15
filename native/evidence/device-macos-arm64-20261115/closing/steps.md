Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

# Part E closing checks, phase 1 (macOS ARM64), 2026-11-15

Scratch record (git-ignored). docs/KIT-MACOS-ARM64.md section 8, phase 1: everything that writes only to `native/work/`.
Repository `<repo>` = `/Volumes/VideoWork/GitHome/CodeBrix.Graphics3D.Cycles`, branch `cycles-with-metal-v5.3.0`,
HEAD `24b74e095f5f1f4526e095e7ac4ad6dfff62bb4c` (clean); kit base commit `828f6c2f9ee2c57dd379d472a0252edaf3caa87a`.
.NET SDK `11.0.100` (`dotnet --version`, checked at the start). Every command runs from `<repo>`.

A Docker container (`cbc-arm64-oracle-20261113b`, the Linux ARM64 oracle re-render) ran on the same machine during the
whole of phase 1; every wall below is inflated by it.

Times are UTC. Each command's output is in the file named in its row (`*.output.txt` end with the `/usr/bin/time -p`
lines and `exit <status>`); `run_step.sh` writes `<name>.start`, `.end`, `.wall` and `.exit` beside it.

| # | Command | Start (UTC) | Wall (s) | Exit | Output |
| --- | --- | --- | --- | --- | --- |
| 0 | `git status --porcelain` (empty), `git rev-parse HEAD`, `dotnet --version` | 2026-11-15T20:36:26Z | - | 0 | (console: empty, `24b74e09...`, `11.0.100`) |
| 1 | identity commands of TESTING-MACOS-ARM64.md section 7 | 2026-11-15T20:38:23Z | about 7 | 0 (each) | `identity.txt` |
| 2a | `python3 native/work/macos-arm64-closing-20261115/hash_build.py .../pre-delete-build-hashes.json` (the build tree before deletion: 49cd3a4a plus the uncommitted final batch) | 2026-11-15T20:39Z | ~5 | 0 | `pre-delete-build-hashes.json`, `pre-delete-invocation.json`, `pre-delete-status.json` |
| 2b | `rm -rf native/work/macos/osx-arm64/host native/work/macos/osx-arm64/oracle-build native/work/macos/osx-arm64/dependencies` (dependencies-old-20261024 and -20261113 kept) | 2026-11-15T20:39:31Z | 2 | 0 | `delete.*` |
| 2c | `CODEBRIX_ISPC=<repo>/native/work/tools/ispc-v1.30.0-macOS.arm64/bin/ispc python3 tools/build_macos.py --jobs 8` (via `env`, timed with `/usr/bin/time -p`; dependencies rebuilt) | 2026-11-15T20:39:43Z | 367.09 | 0 | `build.output.txt`; copies: `closing-invocation.json`, `closing-status.json`, `closing-kernel-sources.json`, `closing-dependencies-marker.json`, `host-build.output.txt` (= `host/build.log`), `host-configure.output.txt` (= `host/configure.log`) |
| 2d | `python3 .../build_check.py` (warnings by origin, commit record, watch-points; read only) | 2026-11-15T20:46Z | ~3 | 0 | `build-check.json` |
| 2e | `python3 .../hash_build.py .../closing-build-hashes.json`; `python3 .../repro_compare.py ... > repro-compare.md` | 2026-11-15T20:47Z | ~5 | 0 | `closing-build-hashes.json`, `repro-compare.md` |

### Step 2 results

- Build passed (`status.json`: `passed: true`, `complete: true`, `phase: complete`; 65 exports; 292 kernel source files).
- Warnings, `warning:` lines in `host/build.log`, `host/configure.log` and the console: **0 in our own code**. Pinned Cycles, 1:
  `<repo>/native/work/macos/osx-arm64/host/cycles-source/src/device/metal/device_impl.mm:559:13: warning: 'fastMathEnabled' is deprecated: first deprecated in macOS 15.0 - Use mathMode instead [-Wdeprecated-declarations]`.
  Dependencies: 120 `warning:` lines (11 distinct after stripping the location) in the component logs under
  `native/work/macos/osx-arm64/dependencies/`. Host configure: 2 `CMake Warning` blocks (CMP0167 FindBoost policy in
  pinned `src/cmake/external_libs.cmake:443`; unused `CMAKE_OBJCXX_FLAGS`), counted only.
- Commit record: `invocation.json` `sourceCommit` `24b74e095f5f1f4526e095e7ac4ad6dfff62bb4c`, `metalSourceStatus` `[]`
  (clean), `sourceDateEpoch` `1794772111` (= committer time of 24b74e09); `status.json` `sourceAtEnd` `{sourceCommit:
  24b74e09..., metalSourceStatus: []}`. Both name one commit with a clean status.
- Watch-points: 176 compile commands, 0 with `WITH_DLSS`, 0 with `WITH_WEBGPU`, `-DWITH_EMBREE` on `src/bvh/embree.cpp`;
  `arm_neon.h` include once in the work copy of `util/half.h`; `overlaidInClosure` = `["util/types_gpu_compressed.h"]`;
  292 staged kernel files, all byte-identical to `third_party/cycles/src`; `kernel/tables_d65_codebrix.h` staged.
- SHA-256: host library `b804223deae0afce07ad33440b312d3ea907b1f528839b375db628e3a1916e5e`; oracle runner
  `1f90b7df1bac4910b08ddae6a78de82246df3d87f07a75f9ff248f4899e692d2`; kernel closure digest (sorted `name sha` lines)
  `cbd80dc3d9e11e6ef80e65b6158aa26dc0dcfd6330582b950c1374a8b77eac39`; the 19 dylibs of `status.json`
  `dependencyClosure`: `closing-build-hashes.json` `closure`.
- Reproducibility (information, `repro-compare.md`): host library and runner identical to the batch-final build
  (`partD-20261114/final/hashes.txt`). Against the deleted tree (49cd3a4a + the uncommitted final batch; its host
  `sourceDateEpoch` 1794749364 falls on the same UTC day as 24b74e09's 1794772111): host library, 7 executables, 10 Cycles static libraries, 292 kernel files, 11 dependency static libraries,
  runner and 27 of 28 dependency dylibs identical; libjpeg (configure date in its version string; not shipped, not
  loaded) and the prefix record differ, as `native/host/MACOS_ARM64.md` "Not covered" says. Against the C27 scratch
  tables F-a1 / F-bb: runner, 292 kernel files, 11 dependency static libraries and 27/28 dependency dylibs identical
  (libjpeg differs); host library, `codebrix_host_image_reference`, `codebrix_host_session64_reference`,
  `codebrix_host_session_reference` and `libcycles_integrator.a` differ, where host and overlay code changed after C27.
| 3 | `python3 native/tools/macos_host_gate.py --output native/work/macos-validation-20261115b` | 2026-11-15T20:46:53Z | 38.74 | 0 | `gate.output.txt`, `gate-status.json` (copy of the gate's `status.json`); gate folder `native/work/macos-validation-20261115b/` |

### Step 3 results

- **Passed.** `status.json`: `passed: true`, `complete: true`, 3,102 films compared, 0 same-host bitwise failures, 0 Metal
  practical failures, 0 Metal strict differences, Metal maximum absolute error 4.470348358154297e-08,
  `inputsUnchanged: true`; `notApplicable.fileAndRebindCases` "4 per round: this profile reads no image files; texture
  filenames are verified as rejected instead". `binary-review.json` `passed: true`, 65 exports. Every sub-run of
  `results.json` (contract, session and session64 references, cpu, cpu-exit, cpu-progressive, cpu-tiles, cpu-tiled,
  cpu-tiled-exit, managed, metal, metal-metalrt, metal-progressive, metal-tiles, image references a/b/c, reload,
  managed-reload, concurrency) has no `passed: false`. The wall is short because the Metal kernels were already in the
  user's Cycles cache and the macOS shader cache.
| 4.0 | `dotnet --version` | 2026-11-15T20:48:20Z | - | 0 | `step7-dotnet-version.txt` (`11.0.100`); `step7-nugets-before.txt` |
| 4.1 | `dotnet restore CodeBrix.Graphics3D.Cycles.slnx` | 2026-11-15T20:48:20Z | 1.11 | 0 | `step7-1-restore.output.txt` |
| 4.2 | `dotnet build --configuration Release CodeBrix.Graphics3D.Cycles.slnx` | 2026-11-15T20:48:22Z | 10.07 | 0 | `step7-2-build-release.output.txt` |
| 4.3 | `dotnet test --solution CodeBrix.Graphics3D.Cycles.slnx --configuration Release --no-build` | 2026-11-15T20:48:32Z | 156.53 | 0 | `step7-3-test-release.output.txt`, `step7-skips-release.txt` |
| 4.4 | `dotnet pack --configuration Release --no-build CodeBrix.Graphics3D.Cycles.slnx` | 2026-11-15T20:51:09Z | 1.91 | 0 | `step7-4-pack-release.output.txt` |
| 4.5 | `dotnet build --configuration Debug CodeBrix.Graphics3D.Cycles.slnx` | 2026-11-15T20:51:11Z | 4.61 | 0 | `step7-5-build-debug.output.txt` |
| 4.6 | `dotnet test --solution CodeBrix.Graphics3D.Cycles.slnx --configuration Debug --no-build` | 2026-11-15T20:51:15Z | 150.13 | 0 | `step7-6-test-debug.output.txt`, `step7-skips-debug.txt` |
| 4.7 | `rm -rf nugets/Release/1.0.319.1248` (the folder the Release build's pack driver wrote: Core and LinuxX64 packages and `audit`) | 2026-11-15T20:54Z | <1 | 0 | `step7-nugets-after.txt`, `step7-nugets-after-delete.txt` |

### Step 4 (testing document step 7) results

- Release build: 1 warning, the expected one, verbatim: `EXEC : warning : linux-x64: payload is STALE: 437 Cycles-with-WGSL
  source files changed since its source commit d861e1589d1a9572558d4529c36a81f6920cc92a; restage it before publishing
  [<repo>/build/CodeBrix.Graphics3D.Cycles.Build.csproj]`; 0 errors. The pack driver packed Core and LinuxX64 only
  (no `native/binaries/osx-arm64/provenance.json`), both "audit passed", version 1.0.319.1248.
- Release tests: total 525, succeeded 519, failed 0, skipped 6. Debug tests: total 525, succeeded 519, failed 0,
  skipped 6. The same 6 skips in both, all WebGPU-only: `BakingTests.the_webgpu_device_bakes_the_known_results_and_matches_the_cpu_bake`
  and `WebGpuAmbientOcclusionTests.gpu_ao_pass_matches_the_cpu_reference_with_the_practical_criterion` ("This binary's GPU
  backend is not WebGPU."), `SceneSettingsTests.Dispersion_is_refused_by_name_on_the_webgpu_backend` ("CyclesBackend.Gpu
  resolved to Metal, not WebGPU."), and the three cases of
  `GpuRenderTests.webgpu_refuses_the_integer_and_component_nodes_by_name_and_the_cpu_renders_them` ("Cycles' Metal device
  renders these nodes; the refusal is the WGSL device's.").
- `dotnet pack`: exit 0 (Core package into `src/CodeBrix.Graphics3D.Cycles/bin/Release/`, build output). Debug build: 0
  warnings, 0 errors.
- `nugets/Release/1.0.319.1248/` deleted; `nugets/Release/` is back to its five earlier folders. `git status --porcelain`
  empty after the sequence (`step7-git-status-after.txt`).
| 5.0 | `find native/oracle/scenes -name "*.json" ! -name manifest.json ! -path "*/assets/*" \| wc -l` (the kit's find command, all families) | 2026-11-15T20:54:45Z | <1 | 0 | `corpus-find-count.txt`: 791 committed scene documents; 1,586 `--scene` arguments = 793 scenes with the 2 bench documents |
| 5.1 | `git status --porcelain` just before the render (empty) and `git rev-parse HEAD` (`24b74e09...`) | 2026-11-15T20:54:45Z | <1 | 0 | `corpus-git-status-before.txt` (empty), `corpus-head.txt` |
| 5.2 | `bash native/work/macos-arm64-closing-20261115/corpus.sh`: `render_oracles.py --output native/work/oracles/macos-arm64-closing-20261115 --runner native/work/macos/osx-arm64/oracle-build/codebrix_cycles_oracle --host-library native/work/macos/osx-arm64/host/build/lib/libcodebrix_cycles_host.dylib --backend cpu-arm64-macos --backend metal-m2pro --backend metal-m2pro-metalrt` with `--scene` for every document of the find command (sorted) and `native/work/oracle-assets/bench_01/bench_01.json`, `bench_01-hdri.json` | 2026-11-15T20:54:45Z | 2344.73 | 0 | `corpus.output.txt`, `corpus-git-status-after.txt` (empty); run folder `native/work/oracles/macos-arm64-closing-20261115/` (19,126 files, 300 MB) |

### Step 5 results

- 793 scenes (791 committed documents + 2 bench documents) on each of `cpu-arm64-macos`, `metal-m2pro` and
  `metal-m2pro-metalrt`: 793 / 793 / 793 rendered, 1,586 films each, **0 failures** (`run.json` has no `failures`),
  `comparisonsPassed` true for 793 / 793. Every `meta.json` (2,379) records `repoCommit` 24b74e09 and
  `repoWorkingTreeClean: true`.
- `kernelCompileSeconds` (first render with a fresh Cycles cache, scene `ao/ao-node`): `metal-m2pro` 2.35562 s (second
  render 0.327622 s), `metal-m2pro-metalrt` 1.31982 s (second 0.15269 s). The macOS-managed Metal shader cache is not
  cleared (`metalSettings.systemMetalShaderCache`), so this is not a cold compile (Part A: 123.95 s and 115.20 s).
- `meta.json` `warnings` on all three backends, verbatim (one scene each): `WARNING: Maximum number of closures exceeded:
  66 > 64`, `77 > 64`, `96 > 64` (the same three as Part A). No Metal kernel compile diagnostic.

| # | Command | Start (UTC) | Wall (s) | Exit | Output |
| --- | --- | --- | --- | --- | --- |
| 6 | `python3 native/work/macos-arm64-closing-20261115/rule9_closing.py native/work/oracles/macos-arm64-closing-20261115 native/evidence/cycles-oracles-macos-20261113 native/work/oracles/macos-arm64-final-corpus-20261115 > rule9.json` | 2026-11-15T21:34:17Z | 4 | 0 | `rule9.json` |
| 6b | CPU-vs-Metal practical counts on the base scenes and the `meta.json` commit record (inline Python, read only) | 2026-11-15T21:35Z | ~3 | 0 | `metal-practical-info.json` |

### Step 6 results (kit rule 9)

- **PASS.** All 726 scenes of the base run (`native/evidence/cycles-oracles-macos-20261113`) are in the closing run; their
  1,452 `cpu-arm64-macos` films (2 film indices each) are bitwise identical by `filmSha256`, 0 moved, 0 index
  mismatches. The base films' bytes match their own `meta.json`; so do all 1,586 closing CPU films. Passes too: 2,870 of
  2,870 pass films identical; the `RenderTime` pass of `passes/data-passes` (a wall-clock measurement, different between
  any two renders by design) is reported, not compared.
- Ruling 34: `lamps/area-degenerate-infinite-axis` is **not in the base run** (it was a survey phase-2 scene, rendered in
  `native/work/oracles/macos-arm64-survey-20261113-phase2`), so no base film is excluded. It is removed from the corpus
  (deleted in 24b74e09) and absent from the closing run.
- 67 scenes new since the base (closing 793 = base 726 + 67): attributes 6, bake 24, bsdf 1, gsplat 11, images 6, lamps 5,
  points 2, render-settings 4, surface-nodes 5, world 3 (`rule9.json` `newScenes.scenes`), each with 2 films per backend.
- Every closing CPU film is repeat-identical (793 / 793 scenes, `repeatMaxAbsDifference` 0 everywhere).
- Information: all 1,586 closing CPU films are also identical to the batch-final corpus run
  (`native/work/oracles/macos-arm64-final-corpus-20261115`, same 793 scenes).
- Metal repeats (information): `metal-m2pro` 36 scenes repeat-identical, 757 not; maximum `repeatMaxAbsDifference`
  1.220703125e-04 (`lamps/area-degenerate-rect`); histogram 0: 36, <1e-7: 519, 1e-7..1e-6: 207, 1e-6..1e-5: 18,
  1e-5..1e-4: 12, 1e-4..1e-3: 1. `metal-m2pro-metalrt` 36 identical, 757 not; maximum 1.220703125e-04
  (`lamps/area-degenerate-rect`); 0: 36, <1e-7: 508, 1e-7..1e-6: 216, 1e-6..1e-5: 20, 1e-5..1e-4: 12, 1e-4..1e-3: 1.
- CPU against Metal, practical criterion, information only (scenes with a failing pair): CPU vs `metal-m2pro` 365 of 793
  (on the 726 base scenes 335, the same scene set as the base run), CPU vs `metal-m2pro-metalrt` 418 (base scenes 388,
  same set), `metal-m2pro` vs `metal-m2pro-metalrt` 259 (base scenes 242, same set); CPU vs either Metal 421 scenes (391
  on base scenes, as in Part A). Largest CPU-vs-Metal difference 1117.11 (`lamps/area-degenerate-rect`, as in Part A);
  largest Metal-vs-MetalRT 5.975 (`curves/lights-area-sun`).

| 7 | `git status --porcelain` at the end of phase 1 (empty) | 2026-11-15T21:35:51Z | - | 0 | (console) |

## Phase 2 (tracked writes approved after the Linux ARM64 re-render finished)

| # | Command | Start (UTC) | Wall (s) | Exit | Output |
| --- | --- | --- | --- | --- | --- |
| P1 | `python3 native/oracle/render_oracles.py --bundle native/work/oracles/macos-arm64-closing-20261115` | 2026-11-15T21:37:23Z | 30.31 | 0 | `bundle.output.txt`: `bundle/cycles-oracles-macos-20261115.tar.zst`, SHA-256 `b8fe9ecb61eb8db7c396cebadc6210f23451c9512c4af6d6403644f4222b88af`, 19,126 files verified, published as `native/evidence/cycles-oracles-macos-20261115/` |
| P2 | `python3 native/tools/stage_macos_binaries.py --gate native/work/macos-validation-20261115b --oracles native/work/oracles/macos-arm64-closing-20261115` | 2026-11-15T21:39:10Z | 15.2 | 0 | `stage.output.txt`: `native/binaries/osx-arm64` staged, `sourceCommit` 24b74e09, approved, 20 binaries, 335 payload files (+ `provenance.json`), `realStageRefusals` `[]`; standalone render of `starter/diffuse-triangle` from a copy of `runtime/`: CPU film `d429a111...` equal to the oracle run's, Metal film `d22d8cec...` |
| P3 | `dotnet build build/CodeBrix.Graphics3D.Cycles.Build.csproj -c Release` (`pack-nugets-before.txt` lists the earlier folders) | 2026-11-15T21:40:06Z | 12.89 | 0 | `final-pack.output.txt`: `nugets/Release/1.0.319.1300/` with Core (12 files), LinuxX64 (31) and MacOSArm64 (323), each "match the reviewed payload; audit passed"; 1 warning, the linux-x64 STALE notice; no osx-arm64 warning |
| P4 | `python3 tools/licensing/package_check.py nugets/Release/1.0.319.1300 --version 1.0.319.1300` | 2026-11-15T21:40:31Z | 0.84 | 0 | `package-check.output.txt`: the same three lines, `WARNING: linux-x64: payload is STALE` only |
| P5 | `python3 native/work/consumer-macos-arm64/run_matrix.py --phase closing-20261115 --version 1.0.319.1300 --feed native/work/consumer-macos-arm64/feed-1.0.319.1300 --payload native/binaries/osx-arm64 --negative-control` (the feed is a copy of the three packages of P3, same SHA-256; `run_matrix.py` now reads `approvedPayload` when a stage has no `payloadFiles`) | 2026-11-15T21:40:53Z | 60.34 | 0 | `consumer.output.txt`, `consumer-results.json`, `consumer-logs/` |
| P6 | Linux ARM64 evidence: `cp -R native/work/oracles/linux-arm64-20261115/{oracle,run.json}` and the records of `native/work/linux-arm64-oracle-20261115-records/` and `native/work/linux/linux-arm64/` into `native/evidence/linux-arm64-cpu-oracle-20261115/`; `python3 .../linux_compare.py` (all 726 shared scenes bitwise identical to 20261113, 67 new); README | 2026-11-15T21:41Z | - | 0 | the folder's `environment/` |
| P7 | `docs/TESTING-LINUX-ARM64-ORACLE.md` section 5: the render command exports `RUN`, `RUNNER` and `HOSTLIB` | 2026-11-15T21:44Z | - | - | - |
| P8 | Device evidence: `native/evidence/device-macos-arm64-20261115/` (identity redacted, build records, gate without `managed-bin/`, managed outputs, these records via `copy_closing.sh`); README | 2026-11-15T21:48Z | - | 0 | - |
| P9 | Status banners: `docs/TESTING-MACOS-ARM64.md`, `docs/TESTING-LINUX-ARM64-ORACLE.md`, `native/host/MACOS_ARM64.md`, `native/binaries/README.md`, `MAINTAINER-README.txt` (README.md and AGENT-README.txt: no statement wrong, unchanged) | 2026-11-15T21:46Z | - | - | - |
| P10 | `python3 tools/licensing/audit.py` (first pass, after the stage and the new evidence) | 2026-11-15T21:51:44Z | 150.12 | 0 | `audit-1.output.txt`: `passed: true`, `errors: []` |
| P11 | `python3 -m unittest tools/licensing/tests/test_guards.py` | 2026-11-15T21:54:46Z | 2.90 | 0 | `test-guards.output.txt`: 11 tests OK |
| P12 | `git diff --no-index --check /dev/null <file>` for every untracked file (29,631); `.gitattributes`: `whitespace=-trailing-space` for 3 captured files of the device folder; the trailing blank line of `identity.txt` and `repro-compare.md` (records of this run) removed | 2026-11-15T21:58Z | ~300 | 0 | - |
| P13 | Kit report sections 1, 3, 6 and 7 (`report/assemble.py`), then `SHA256SUMS.txt` of the three folders, the final audit, `git diff --check 828f6c2f9...` and the checksum verification | 2026-11-15T22:05Z | - | - | `audit-2.output.txt`, `diff-check-final.txt`, `sums-verify.txt` |
