Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

# macOS ARM64 device run: Part E of the Apple Silicon kit, the closing checks (2026-11-15)

Part E ("closing checks") of [docs/KIT-MACOS-ARM64.md](../../../docs/KIT-MACOS-ARM64.md) (section 8) on an Apple Silicon
Mac: a clean build, the macOS host gate, the managed build and tests, the whole corpus on the three macOS backends and the
bitwise check of kit rule 9, then the oracle bundle, the stage of the osx-arm64 payload, the final pack and the consumer
test of the MacOSArm64 package. This folder holds items 3 to 6 of section 6 of
[docs/TESTING-MACOS-ARM64.md](../../../docs/TESTING-MACOS-ARM64.md) for that run; the oracle bundle was published by the
runner as [`native/evidence/cycles-oracles-macos-20261115/`](../cycles-oracles-macos-20261115/), and the staged payload is
[`native/binaries/osx-arm64/`](../../binaries/osx-arm64/). The kit's report is
[`native/evidence/macos-arm64-survey-20261113/README.md`](../macos-arm64-survey-20261113/README.md) (section 6). Facts first;
lines marked **INFERENCE** are conclusions that were not measured directly. Absolute host paths in prose are written as
`<repo>`; copied tool output keeps the paths it was written with (`<repo>` = `/Volumes/VideoWork/GitHome/CodeBrix.Graphics3D.Cycles`).

- Branch `cycles-with-metal-v5.3.0`, commit `24b74e095f5f1f4526e095e7ac4ad6dfff62bb4c`; kit base commit
  `828f6c2f9ee2c57dd379d472a0252edaf3caa87a`. `git status --porcelain` printed nothing at the start
  (`identity/identity.txt`), before and after the corpus render (`closing/corpus-git-status-*.txt`) and after the managed
  sequence; every oracle `meta.json` (2,379) records `repoCommit` `24b74e09...` and `repoWorkingTreeClean: true`. The
  tracked writes of the evidence, the stage and the documents came after the corpus render.
- Pinned Cycles: v5.3.0, revision `b3f6262640f1a2f2d10b55b6ffccbf310ce0f981`.
- Machine: Mac mini (Mac14,12), Apple M2 Pro (10 CPU cores, 6 performance and 4 efficiency; 16-core GPU, Metal 4),
  16 GB, macOS 27.2.1 (26B131).
- Tools: Xcode 27.2 (27B52), macOS SDK 27.2, Apple clang 21.0.0 (clang-2100.4.1.1), CMake 4.4.5, Python 3.15.2,
  zstd 1.5.7, .NET SDK 11.0.100 (`dotnet --version`, `managed/dotnet-version.txt`), ISPC 1.30.0
  (`<repo>/native/work/tools/ispc-v1.30.0-macOS.arm64/bin/ispc`, the binary of Part A).
- A Linux ARM64 container build and render (`native/evidence/linux-arm64-cpu-oracle-20261115/`) ran on the same Mac during
  the build, gate, managed and corpus steps: every wall is inflated (`closing/walls.txt`).

No executables or libraries are in this folder. Their SHA-256 are recorded instead: the host library and the gate
executables in `build/host-status.json` (`artifacts`; host library `b804223deae0afce...`), every build-tree artifact, the
dependency prefix and the 19 dylibs of the host's dependency closure in `build/closing-build-hashes.json`, the oracle
runner there and in the oracle folder's `run.json` (`runnerSha256` `1f90b7df...`), and every file of the host gate's
`managed-bin/` in `host-gate.managed-bin-sha256.txt`. `SHA256SUMS.txt` lists every other file of this folder
(`shasum -a 256 -c SHA256SUMS.txt`).

## Steps

| Step | Command | Verdict | Wall |
| --- | --- | --- | --- |
| 1 | identity record (testing document section 7) | recorded | about 7 s |
| 2 | delete `native/work/macos/osx-arm64/{host,oracle-build,dependencies}`, then `CODEBRIX_ISPC=... python3 tools/build_macos.py --jobs 8` (dependencies rebuilt) | passed (`status.json` `passed: true`, 65 exports, 292 kernel files); 0 warnings in this repository's code | 367.09 s |
| 3 | `python3 native/tools/macos_host_gate.py --output native/work/macos-validation-20261115b` | passed: 3,102 films, 0 same-host bitwise failures, 0 Metal practical failures, Metal maximum 4.47e-8 | 38.74 s |
| 7 | restore, Release build, Release test, pack, Debug build, Debug test | all exit 0; Release and Debug: 525 tests, 519 passed, 6 skipped (WebGPU-only), 0 failed | 1.11, 10.07, 156.53, 1.91, 4.61, 150.13 s |
| 4 | `render_oracles.py`, three backends, the whole committed corpus (791 documents, the kit's find command) and the 2 bench documents | passed: 793 scenes per backend, 0 failures, `comparisonsPassed` 793/793 | 2,344.73 s |
| - | kit rule 9 (`closing/rule9.json`) | PASS: 1,452 / 1,452 base-run CPU films bitwise identical | about 4 s |
| 6 | `python3 native/oracle/render_oracles.py --bundle native/work/oracles/macos-arm64-closing-20261115` | passed: 19,126 files verified, published as `cycles-oracles-macos-20261115/` | 30.31 s |
| 9 | `python3 native/tools/stage_macos_binaries.py --gate native/work/macos-validation-20261115b --oracles native/work/oracles/macos-arm64-closing-20261115` | staged, approved, no refusal | 15.20 s |
| - | `dotnet build build/CodeBrix.Graphics3D.Cycles.Build.csproj -c Release`; `package_check.py` | Core, LinuxX64 and MacOSArm64 packed and checked | 12.89 s, 0.84 s |
| - | consumer test of the MacOSArm64 package, five modes and the negative control | passed | 60.34 s |

Each command's start time, wall and exit status are in `closing/steps.md` and `closing/walls.txt`; the phase-1 summary is
`closing/phase1-summary.md`. Step 5 of the testing document (compare with the previous macOS evidence) compares nothing
valid across the pin (kit rule 8); the comparison that matters here is rule 9 against the Part A base run.

## Step 2: the clean build

- `native/work/macos/osx-arm64/host/`, `oracle-build/` and `dependencies/` were deleted first (`dependencies-old-20261024`
  and `-20261113` kept); the hashes of the deleted tree (49cd3a4a plus the final ruling batch, uncommitted) are in
  `build/pre-delete-build-hashes.json`. The build rebuilt all dependency components and the host from the committed tree.
- Warnings (`build/build-check.json`, from `host-build.output.txt`, `host-configure.output.txt` and the console):
  - this repository's code: **0**;
  - pinned Cycles, 1: `<repo>/native/work/macos/osx-arm64/host/cycles-source/src/device/metal/device_impl.mm:559:13: warning: 'fastMathEnabled' is deprecated: first deprecated in macOS 15.0 - Use mathMode instead [-Wdeprecated-declarations]`;
  - dependencies: 120 `warning:` lines (11 distinct) in the component logs under `native/work/macos/osx-arm64/dependencies/`
    (not copied);
  - host configure: 2 `CMake Warning` blocks, the pinned `src/cmake/external_libs.cmake:443` CMP0167 (FindBoost) and the
    unused `CMAKE_OBJCXX_FLAGS`.
- The commit record the stager needs: `build/invocation.json` `sourceCommit` `24b74e095f5f1f4526e095e7ac4ad6dfff62bb4c`
  with `metalSourceStatus` `[]`, `build/host-status.json` `sourceAtEnd` the same commit with `[]`; `sourceDateEpoch`
  `1794772111`, the committer time of 24b74e09.
- Watch-points of kit section 4 (`build/build-check.json`): 176 compile commands, none with `WITH_DLSS` or `WITH_WEBGPU`;
  `-DWITH_EMBREE` on `src/bvh/embree.cpp`; the `arm_neon.h` include once in the work copy of `util/half.h`;
  `overlaidInClosure` exactly `["util/types_gpu_compressed.h"]`; 292 staged kernel files, all byte-identical to
  `third_party/cycles/src`, `kernel/tables_d65_codebrix.h` among them.
- SHA-256: host library `b804223deae0afce07ad33440b312d3ea907b1f528839b375db628e3a1916e5e`, oracle runner
  `1f90b7df1bac4910b08ddae6a78de82246df3d87f07a75f9ff248f4899e692d2`; kernel closure digest
  `cbd80dc3d9e11e6ef80e65b6158aa26dc0dcfd6330582b950c1374a8b77eac39` (SHA-256 of the sorted `name sha` lines).
- Reproducibility, for information (`build/repro-compare.md`): the host library and the runner are byte-identical to the
  build of the final ruling batch (`<repo>/native/work/partD-20261114/final/hashes.txt`). Against the deleted tree, every
  artifact is byte-identical (host library, 7 executables, 10 Cycles static libraries, 292 kernel files, 11 dependency
  static libraries, 27 of 28 dependency dylibs, runner) except libjpeg and the dependency prefix record, the two that
  `native/host/MACOS_ARM64.md` ("Reproducible builds", "Not covered") names: libjpeg is neither shipped nor loaded and
  carries its configure date. Against the C27 scratch tables (`<repo>/native/work/repro-20261114/tables/F-a1.json`,
  `F-bb.json`), which predate later host changes, the runner, the kernel tree, the dependency static libraries and 27 of
  28 dependency dylibs are identical; the host library, three reference executables and `libcycles_integrator.a`
  differ, as expected where host and overlay code changed after C27.

## Step 3: host gate

`host-gate/status.json`: `passed: true`, `complete: true`, 3,102 films compared, 0 same-host bitwise failures, 0 Metal
practical failures, 0 Metal strict differences, Metal maximum absolute error 4.470348358154297e-08, `inputsUnchanged:
true`; `notApplicable.fileAndRebindCases` "4 per round: this profile reads no image files; texture filenames are verified
as rejected instead". `binary-review.json` `passed: true`, 65 exports. No sub-run of `results.json` failed (contract,
session and session64 references, CPU, process exit, progressive, tiles, tiled images and their exit case, the .NET
wrapper, Metal shipped, MetalRT, progressive and tiles, the three image reference runs, C and managed reload,
concurrency). The wall is short because the Metal kernels were in the Cycles cache and the macOS shader cache.

## Step 7: managed build, tests and package

`managed/`: `1-restore` to `6-test-debug.output.txt`, `dotnet-version.txt` (`11.0.100`), the skip lists and the
`nugets/Release/` listings before and after.

- Release build: 1 warning, the expected one: `EXEC : warning : linux-x64: payload is STALE: 437 Cycles-with-WGSL source
  files changed since its source commit d861e1589d1a9572558d4529c36a81f6920cc92a; restage it before publishing`. Debug: 0
  warnings. The Release build's pack driver packed Core and LinuxX64 only (the osx-arm64 payload was not staged yet);
  its folder `nugets/Release/1.0.319.1248/` was deleted.
- Tests, Release and Debug alike: 525 total, 519 passed, 0 failed, 6 skipped, all WebGPU-only:
  `BakingTests.the_webgpu_device_bakes_the_known_results_and_matches_the_cpu_bake` and
  `WebGpuAmbientOcclusionTests.gpu_ao_pass_matches_the_cpu_reference_with_the_practical_criterion` ("This binary's GPU
  backend is not WebGPU."), `SceneSettingsTests.Dispersion_is_refused_by_name_on_the_webgpu_backend` ("CyclesBackend.Gpu
  resolved to Metal, not WebGPU."), and the three cases of
  `GpuRenderTests.webgpu_refuses_the_integer_and_component_nodes_by_name_and_the_cpu_renders_them` ("Cycles' Metal device
  renders these nodes; the refusal is the WGSL device's."). The Metal references were not recorded again: no Cycles
  revision, toolchain, scene or render setting changed since they were recorded at v5.3.0, and every Metal-authority test
  passes against them.

## Step 4: oracle corpus and kit rule 9

- 793 scenes (791 committed documents and the 2 bench documents of `native/work/oracle-assets/bench_01/`) on
  `cpu-arm64-macos`, `metal-m2pro` and `metal-m2pro-metalrt`: 1,586 films per backend, 0 failures, `comparisonsPassed`
  793/793. `kernelCompileSeconds`: `metal-m2pro` 2.35562 s, `metal-m2pro-metalrt` 1.31982 s (scene `ao/ao-node`; the
  macOS shader cache is not cleared). `meta.json` `warnings` on all three backends: `Maximum number of closures exceeded`
  66, 77 and 96 > 64 (one scene each, as in Part A); no Metal kernel compile diagnostic.
- Kit rule 9 (`closing/rule9.json`, `closing/scripts/rule9_closing.py`): **PASS.** All 726 scenes of the base run
  (`native/evidence/cycles-oracles-macos-20261113/`) are in this run, and all 1,452 of their `cpu-arm64-macos` films are
  bitwise identical by `filmSha256`, index by index; so are all 2,870 base pass films (the wall-clock `RenderTime` pass of
  `passes/data-passes` reported apart). Every film's bytes match its `meta.json` in both runs. The scene ruling 34 moved
  to scratch, `lamps/area-degenerate-infinite-axis`, was not in the base run (it was a survey scene): nothing is excluded.
  67 scenes are new since the base (attributes 6, bake 24, bsdf 1, gsplat 11, images 6, lamps 5, points 2,
  render-settings 4, surface-nodes 5, world 3; listed in `rule9.json`). Every CPU film of this run is repeat-identical.
  All 1,586 CPU films are also identical to the CPU-only corpus run of the final ruling batch.
- Metal repeats, information: 36 of 793 scenes repeat-identical on each Metal backend; maximum `repeatMaxAbsDifference`
  1.220703125e-04 (`lamps/area-degenerate-rect`) on both.
- CPU against Metal, practical criterion, information only (`closing/metal-practical-info.json`): scenes with a failing
  pair, CPU vs `metal-m2pro` 365, CPU vs `metal-m2pro-metalrt` 418, Metal vs MetalRT 259; on the 726 base scenes 335,
  388 and 242, the same scene sets as Part A.

## Steps 6 and 9: bundle and stage

- Bundle (`closing/bundle.output.txt`): `<repo>/native/work/oracles/macos-arm64-closing-20261115/bundle/cycles-oracles-macos-20261115.tar.zst`,
  SHA-256 `b8fe9ecb61eb8db7c396cebadc6210f23451c9512c4af6d6403644f4222b88af`, 19,126 files verified against
  `manifest.json`, published as `native/evidence/cycles-oracles-macos-20261115/`.
- Stage (`closing/stage.output.txt`): `native/binaries/osx-arm64` staged from this build, `sourceCommit` and commit
  `24b74e095f5f1f4526e095e7ac4ad6dfff62bb4c`, `approved: true`, 20 binaries, 335 payload files (runtime, unstripped and
  symbols in full, and the 292 kernel sources), `realStageRefusals` `[]`. Standalone render of `starter/diffuse-triangle`
  from a temporary copy of `runtime/` alone: CPU film `d429a1117596b9641ac544b54db591d8b16017b8e6efc4f1b9a60988ae6bc526`,
  equal to the oracle run's (`cpuFilmEqualsOracleRun: true`); Metal film `d22d8cec36912190feb336061e8379cf990e91b892e8a94eb61bc9090979757a`.

## The final pack and the consumer test

- `dotnet build build/CodeBrix.Graphics3D.Cycles.Build.csproj -c Release` (`closing/final-pack.output.txt`): one family at
  1.0.319.1300, `CodeBrix.Graphics3D.Cycles.Core.ApacheLicenseForever` (12 files), `.LinuxX64` (31) and `.MacOSArm64` (323,
  from the committed payload), each "match the reviewed payload; audit passed"; one warning, the linux-x64 STALE notice.
  `package_check.py` on that folder (`closing/package-check.output.txt`) gives the same three lines and that warning only:
  the osx-arm64 payload is the approved one in `native/binaries/osx-arm64/` and is current for the Cycles-with-Metal
  sources.
- Consumer test (`closing/consumer/`: the matrix script, the scratch app, `results.json` and the build and run logs). A
  scratch console app under `<repo>/native/work/consumer-macos-arm64/` referenced the MacOSArm64 package at 1.0.319.1300
  from a local feed (a copy of the three packages, the same SHA-256; `NUGET_PACKAGES` inside `native/work/`, a fresh copy
  of the app and a fresh `XDG_CACHE_HOME` per mode). The .NET runtime pack of the self-contained publish,
  `Microsoft.NETCore.App.Runtime.osx-arm64` 10.0.15, came from that package cache (no new download).

| Mode | Build / run | Kernel tree beside the host library | CPU | Metal | Metal + GPU denoising |
| --- | --- | --- | --- | --- | --- |
| `dotnet build` + `dotnet bin/.../Consumer.dll` | 0 / 0 | `runtimes/osx-arm64/native/`: 292 of 292, none flattened | rendered | rendered, within 7.45e-8 of the CPU | rendered |
| `dotnet run` | - / 0 | the same | rendered | rendered, 7.45e-8 | rendered |
| `dotnet publish` | 0 / 0 | the same | rendered | rendered, 7.45e-8 | rendered |
| `dotnet publish -r osx-arm64 --self-contained false` | 0 / 0 | beside the app: 292 of 292, none flattened | rendered | rendered, 7.45e-8 | rendered |
| `dotnet publish -r osx-arm64 --self-contained` | 0 / 0 | the same | rendered | rendered, 7.45e-8 | rendered |
| negative control: the `build` output without `codebrix-cycles/` | - / 1 | removed | rendered | `NativeFailure: Metal kernel sources are missing beside the native library` | the same failure |

  No mode had a build diagnostic. Every film is finite; the CPU film is bit for bit the same in every mode
  (`7d1fd7a7...`), and so is the Metal film with GPU denoising (`64c101f0...`); the library reports version
  1.0.20261115.0 and Cycles `b3f62626...`. The final pack folder `nugets/Release/1.0.319.1300/` is git-ignored and kept
  outside this folder.

## Contents

| Path | What |
| --- | --- |
| `identity/identity.txt` | the identity record of section 7 of the testing document. The host name, serial number, hardware UUID and provisioning UDID are replaced by `<host>` and `<redacted>`; nothing else is changed |
| `build/` | the build records of section 3: `host-status.json` (`host/status.json`), `invocation.json`, the dependency marker `dependencies-marker.json` (`dependencies/install/codebrix-osx-arm64-build.json`), `platform.patch`, `cycles-registration.patch`, `kernel-sources.json`, `dependencies-status.json`, `dependencies-provenance.json`; the console output of the build (`build-macos.stdout.txt`), the host's configure and build logs; the warning, commit-record and watch-point check (`build-check.json`), the hashes of the build tree and of the deleted tree (`closing-build-hashes.json`, `pre-delete-build-hashes.json`) and their comparison (`repro-compare.md`) |
| `host-gate/` | the host gate directory `native/work/macos-validation-20261115b/` without `managed-bin/` (the built managed executables) |
| `host-gate.managed-bin-sha256.txt` | SHA-256 of every file of the omitted `managed-bin/` |
| `managed/` | step 7: `dotnet --version`, every managed command's output (`<n>-<step>.output.txt`, in run order), the skip lists and the `nugets/Release/` listings |
| `closing/` | the run records: `steps.md` (every command, start, wall, exit), `phase1-summary.md`, `walls.txt`, the corpus count and `git status` records, the console output of the gate, the corpus render, the bundle, the stage, the final pack and `package_check.py`, `rule9.json`, `metal-practical-info.json`, `consumer/` (the consumer matrix) and `scripts/` (the scripts that ran or checked the steps) |

Renames (the repository ignores `*.log`): `native/work/macos/osx-arm64/host/configure.log` -> `build/host-configure.output.txt`,
`host/build.log` -> `build/host-build.output.txt`; the runner's console records `<name>.output.txt` keep their names
(`build.output.txt` -> `build/build-macos.stdout.txt`, `step7-<n>.output.txt` -> `managed/<n>.output.txt`); the consumer
matrix's `<mode>/build.log` and `run.log` -> `closing/consumer/logs/<mode>-build.output.txt` and `-run.output.txt`. The
copyright and SPDX lines were added at the top of the copied shell scripts (after the `#!` line); nothing else in a copied
file was changed. Three copied files keep the trailing spaces of the tool output they captured (`build/cycles-registration.patch`,
`build/platform.patch`, `build/host-configure.output.txt`); `.gitattributes` gives them `whitespace=-trailing-space`, so that
`git diff --check` does not report those lines, and their bytes are as recorded.
