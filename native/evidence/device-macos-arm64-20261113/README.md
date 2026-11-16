Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

# macOS ARM64 device run: Part A of the Apple Silicon kit (2026-11-13)

Part A ("the base") of [docs/KIT-MACOS-ARM64.md](../../../docs/KIT-MACOS-ARM64.md) on an Apple Silicon Mac: steps 1 to 8
of [docs/TESTING-MACOS-ARM64.md](../../../docs/TESTING-MACOS-ARM64.md) at the base commit. This folder holds items 3 to 6
of section 6 of the testing document; the oracle bundle of step 6 was published by the runner as
[`native/evidence/cycles-oracles-macos-20261113/`](../cycles-oracles-macos-20261113/). Facts first; lines marked
**INFERENCE** are conclusions that were not measured directly. Absolute host paths in prose are written as `<repo>`;
copied tool output keeps the paths it was written with.

- Branch `cycles-with-metal-v5.3.0`, base commit `828f6c2f9ee2c57dd379d472a0252edaf3caa87a`. `git status --porcelain`
  printed nothing before the build (`identity/identity.txt`), and every oracle `meta.json` records
  `repoWorkingTreeClean: true`.
- Pinned Cycles: v5.3.0, revision `b3f6262640f1a2f2d10b55b6ffccbf310ce0f981`.
- Machine: Mac mini (Mac14,12), Apple M2 Pro (10 CPU cores, 6 performance and 4 efficiency; 16-core GPU, Metal 4),
  16 GB, macOS 27.2.1 (26B131).
- Tools: Xcode 27.2 (27B52), macOS SDK 27.2, Apple clang 21.0.0 (clang-2100.4.1.1), CMake 4.4.5, Python 3.15.2,
  zstd 1.5.7, .NET SDK 11.0.100 (`dotnet --version`, recorded before every managed run), ISPC 1.30.0
  (`kit/ispc.txt`: `<repo>/native/work/tools/ispc-v1.30.0-macOS.arm64/bin/ispc`, SHA-256 `a03c7ab5...`).

No executables or libraries are in this folder. Their SHA-256 are recorded instead: the host library and the gate
executables in `build/host-status.json` (`artifacts`; host library `118d1e89846d10d5...`), the dependency libraries in
`build/dependencies-marker.json` (`libraries`), the oracle runner in the oracle folder's `run.json` (`runnerSha256`
`486b29f9...`), and every file of the host gate's `managed-bin/` in `host-gate.managed-bin-sha256.txt`.
`SHA256SUMS.txt` lists every other file of this folder (`shasum -a 256 -c SHA256SUMS.txt`).

## Contents

| Path | What |
| --- | --- |
| `identity/identity.txt` | the identity record of section 7 of the testing document. The host name, serial number, hardware UUID and provisioning UDID are replaced by `<host>` and `<redacted>`; nothing else is changed |
| `build/` | the build records of section 3: `host-status.json` (`host/status.json`), `invocation.json`, the dependency marker `dependencies-marker.json` (`dependencies/install/codebrix-osx-arm64-build.json`), `platform.patch`, `cycles-registration.patch`, `kernel-sources.json`, `dependencies-status.json`, `dependencies-provenance.json`; the console output of the passed build (`build-macos.stdout.txt`) and of the two refused attempts (`refused-attempts/`) |
| `host-gate/` | the host gate directory `native/work/macos-validation-20261113a/` without `managed-bin/` (the built managed executables) |
| `host-gate.managed-bin-sha256.txt` | SHA-256 of every file of the omitted `managed-bin/` |
| `managed/` | step 7: `dotnet --version`, every managed command's output (`<n>-<step>.output.txt`, numbered in run order), the Metal reference manifest before re-recording, its diff, the SHA-256 of the reference folder before and after, and the film review (`metal-reference-review.txt`) |
| `kit/` | the kit run records: `steps.md` (copied with the copyright and SPDX lines added at the top), `watch-points.txt`, the console output of steps 3, 4 and 6, the oracle-run summary (`step4-summary.txt`), the practical-criterion list (`practical-fail-cpu-vs-metal.txt`), the corpus count, the staged Metal kernel files and their comparison with the pinned source (`kernel-compare.txt`, empty: no difference), and the check of the hashes pinned for `research/translate.py` (`translator-pin-check.txt`) |

Renames (the repository ignores `*.log`): `native/work/macos/osx-arm64/host/configure.log` ->
`build/host-configure.output.txt`, `native/work/macos/osx-arm64/host/build.log` -> `build/host-build.output.txt`.
The kit's console records were renamed: `build-full-2.txt` -> `build/build-macos.stdout.txt`, `build-skipdeps.txt` ->
`build/refused-attempts/1-skip-dependencies.stdout.txt`, `build-full.txt` ->
`build/refused-attempts/2-full-build-stale-source-copy.stdout.txt`, `step3.txt` -> `kit/step3-host-gate.stdout.txt`,
`step4.txt` -> `kit/step4-oracle-run.stdout.txt`, `step6.txt` -> `kit/step6-bundle.stdout.txt`; the managed outputs
`<n>-<step>.txt` -> `managed/<n>-<step>.output.txt`. The 24 MB output of step 8 (it lists every source file and every
path in the history) is not copied; its verdict is quoted below.

## Steps

| Step | Command | Verdict | Wall |
| --- | --- | --- | --- |
| 1 | identity record (section 7) | recorded | not timed |
| 2 | `python3 tools/build_macos.py --jobs 8 --skip-dependencies` | refused: "macOS ARM64 dependency inputs/toolchain changed; rebuild the prefix" | 8.58 s |
| 2 | `python3 tools/build_macos.py --jobs 8` (existing prefix) | failed: `FileNotFoundError` for `dependencies/sources/embree/CHANGELOG.md` in `verify_copy` | 10.74 s |
| 2 | `python3 tools/build_macos.py --jobs 8` (prefix moved aside) | passed (`status.json` `passed: true`) | 459.90 s |
| 3 | `python3 native/tools/macos_host_gate.py --output native/work/macos-validation-20261113a` | passed | 391.74 s |
| 4 | `render_oracles.py`, three backends, whole corpus plus the bench | passed: 0 failures, `comparisonsPassed` 726/726 | 2,823.27 s |
| 5 | compare with the previous macOS evidence | skipped: all earlier evidence belongs to the previous Cycles revision (kit rule 8) | - |
| 6 | `python3 native/oracle/render_oracles.py --bundle native/work/oracles/macos-arm64-base-20261113` | passed: 18,063 files verified | 28.13 s |
| 7 | managed restore, build, test, pack (Release, then Debug) | see "Step 7": 1 build warning, pack exit 1, 2 tests fail | 1.91 s restore, 9.86 s build, 54.75 s test, 2.24 s pack, 4.45 s build, 49.84 s test |
| 7 | Metal references recorded again, tests run again | 5 films recorded; 2 tests still fail (not Metal-authority tests) | 37.50 s record, 35.81 s Release, 35.81 s Debug |
| 8 | `python3 tools/licensing/audit.py` | FAILED: 1 error (see "Step 8") | 130.80 s |
| 9 | staging | not done: staging is the maintainer's decision and not part of the kit | - |

Each step's start time, exit status and output are in `kit/steps.md` and the copied console records. Every build ran
with `CODEBRIX_ISPC=<repo>/native/work/tools/ispc-v1.30.0-macOS.arm64/bin/ispc` exported. Every command was timed
with `/usr/bin/time -p` from `<repo>`. The oracle run of step 4, exactly:

```sh
bash -c 'scenes=(); for f in $(find native/oracle/scenes -name "*.json" ! -name manifest.json ! -path "*/assets/*" | sort) native/work/oracle-assets/bench_01/bench_01.json native/work/oracle-assets/bench_01/bench_01-hdri.json; do scenes+=(--scene "$f"); done; python3 native/oracle/render_oracles.py --output native/work/oracles/macos-arm64-base-20261113 --runner native/work/macos/osx-arm64/oracle-build/codebrix_cycles_oracle --host-library native/work/macos/osx-arm64/host/build/lib/libcodebrix_cycles_host.dylib --backend cpu-arm64-macos --backend metal-m2pro --backend metal-m2pro-metalrt "${scenes[@]}"'
```

## Step 2: the dependency prefix

Kit section 4 states (READ) that the dependency snapshots did not change with the pin, so an existing dependency prefix
stays valid for `--skip-dependencies`. That did not hold on this machine. The prefix there (marker of 2026-11-01) was
built before `third_party/dependency-imports.json` gained Embree (2026-11-05): its source copy has no `embree/`, and
`sourceManifestSha256`, `profileRecipeSha256` and `macosRecipeSha256` of its identity differ from the current recipe.
The toolchain had also moved: Xcode 27.1 -> 27.2, SDK 27.0 -> 27.2, Apple clang 21.0.0 clang-2100.3.34.2 ->
clang-2100.4.1.1. The recipe refused it with `--skip-dependencies`, and a full build without the
flag stopped in `verify_copy` on the missing Embree file. The prefix was moved aside (to
`native/work/macos/osx-arm64/dependencies-old-20261113`, kept) and the full build rebuilt all 19 components. The kit's
statement is true of the pin change itself (the re-pin commit changed nothing under `third_party/deps` or
`third_party/dependency-imports.json`); it does not cover a prefix older than the later dependency changes, or a
toolchain update.

## Watch-points (kit section 4)

All checked on the passed build; details in `kit/watch-points.txt`.

- The `arm_neon.h` include is in the work copy of `src/util/half.h` once.
- 0 of 176 compile commands contain `WITH_DLSS`; 0 contain `WITH_WEBGPU`; `-DWITH_EMBREE` is on the compile line of
  `src/bvh/embree.cpp`.
- `stage_kernels` passed; `kernel-sources.json` `overlaidInClosure` is exactly `["util/types_gpu_compressed.h"]`.
- `kernel/tables_d65_codebrix.h` is in the 292-file staged Metal kernel closure, and all 292 staged files are
  byte-identical to `third_party/cycles/src`. No Metal compile error names `cie_d65_spd`, `sample_wavelength` or
  `wavelength_to_rgb_d65`.
- Every Metal kernel compiled once with an empty Cycles cache in the oracle run: `kernelCompileSeconds` 123.95 s
  (`metal-m2pro`) and 115.20 s (`metal-m2pro-metalrt`).
- Compiler warnings of the host build (`build/host-build.output.txt`), verbatim:
  - own code, 1: `<repo>/native/tests/host/image_inputs.h:41:18: warning: unused function 'write_image' [-Wunused-function]`.
    Recorded, not fixed: `image_inputs.h` is a host-gate fixture. Part A's "0 warnings in our own code" is therefore
    not met by this build.
  - pinned Cycles, 1: `<repo>/native/work/macos/osx-arm64/host/cycles-source/src/device/metal/device_impl.mm:559:13: warning: 'fastMathEnabled' is deprecated: first deprecated in macOS 15.0 - Use mathMode instead [-Wdeprecated-declarations]`.
  - dependencies: 120 warning lines (12 distinct) in the dependency component logs.

## Step 3: host gate

`host-gate/status.json`: `passed: true`, 3,089 films compared, 0 same-host bitwise failures, 0 Metal practical
failures, 0 Metal strict differences, Metal maximum absolute error 4.47e-8, `inputsUnchanged: true`; the four file and
rebind cases per round are `notApplicable` ("this profile reads no image files; texture filenames are verified as
rejected instead"). Binary review (`binary-review.json`, `passed: true`): 60 exports; minimum OS 15.0 and ad-hoc
signatures on the four reviewed binaries (the host library, `codebrix_host_cpu_test`, `codebrix_host_gpu_test`,
`codebrix_host_session_reference`); load closure within `@rpath/`, `/usr/lib/` and `/System/Library/`. Public-API
contract: `CBC_HOST_WEBGPU` and texture `filename` rejected with `CBC_UNSUPPORTED`. Every sub-run in
`results.json` passed (session references, C consumers, process exit, thin .NET wrapper, Metal with shipped settings
and with `CYCLES_METALRT=1`, three image reference runs of 256 cases, C and managed reload, guarded concurrency).

## Steps 4 and 6: oracle run and bundle

- Corpus: 724 committed scenes (`find native/oracle/scenes -name "*.json" ! -name manifest.json ! -path "*/assets/*"`)
  and the two bench documents from `native/work/oracle-assets/bench_01`, on `cpu-arm64-macos`, `metal-m2pro` and
  `metal-m2pro-metalrt`: 726 scenes, 2,178 films, 0 failures, `comparisonsPassed` 726/726. This CPU-arm64 run is the
  base run of kit rule 9.
- Every CPU film is repeat-identical. Metal repeat maxima (`repeatMaxAbsDifference`): `metal-m2pro` 1.22e-4
  (`lamps/area-degenerate-rect`), `metal-m2pro-metalrt` 0.0718 (`curves-b/subsurface-on-curves`).
- `meta.json` `warnings`, verbatim, on all three backends: `WARNING: Maximum number of closures exceeded: 66 > 64`
  (`linux-gates/closed-volumes`), `77 > 64` (`volumes/bsdf-in-volume`), `96 > 64` (`volumes/mixed-phase-world`). No
  Metal kernel compile diagnostic was recorded.
- `CYCLES_METALRT=1`: the `metal-m2pro-metalrt` backend rendered all 57 `points/` scenes (and every other point-cloud
  scene of the corpus) that way.
- CPU against Metal, reported as information only (section 5 of the testing document; never a verdict): 723
  CPU-against-Metal pairs in 391 scenes are beyond the practical criterion (335 against `metal-m2pro`, 388 against
  `metal-m2pro-metalrt`); the largest is `lamps/area-degenerate-rect`, maximum absolute difference 1117.11. 242 further
  `metal-m2pro` against `metal-m2pro-metalrt` pairs are beyond it. Every pair: `kit/practical-fail-cpu-vs-metal.txt`.
- Bundle: `<repo>/native/work/oracles/macos-arm64-base-20261113/bundle/cycles-oracles-macos-20261113.tar.zst`,
  16,705,392 bytes, SHA-256 `1ccf6f2926296b6eef6fb4cd3258bc4eac450a5f6aad2134add431d69af27ca3`, 18,063 files verified
  against `manifest.json`; published as `native/evidence/cycles-oracles-macos-20261113/` (`kit/step6-bundle.stdout.txt`).

## Step 7: managed build, tests and package

Commands exactly as listed in step 7 of the testing document; `dotnet --version` printed `11.0.100`.

| Command | Exit | Result |
| --- | --- | --- |
| `dotnet restore` | 0 | |
| `dotnet build --configuration Release` | 0 | 0 compiler warnings; **1 warning** from the packaging driver's package check, verbatim: `linux-x64: payload is STALE: 433 native source files changed since it was staged at d861e1589d1a9572558d4529c36a81f6920cc92a; restage it before publishing`. The driver packed `.Core` and `.LinuxX64` into the git-ignored `nugets/Release/1.0.318.359/` and both passed the payload check |
| `dotnet test ... --configuration Release --no-build` | 2 | 332 total, 321 passed, 5 failed, 6 skipped |
| `dotnet pack --configuration Release --no-build` | 1 | `.Core` packed; `error NU5017: Cannot create a package that has no dependencies nor content.` for `build/nuget-pack-shim/CodeBrix.Pack.Shim.csproj` |
| `dotnet build --configuration Debug` | 0 | 0 warnings |
| `dotnet test ... --configuration Debug --no-build` | 2 | 332 total, 321 passed, 5 failed, 6 skipped |
| `CODEBRIX_CYCLES_RECORD_METAL_REFERENCES=1 dotnet test --project tests/CodeBrix.Graphics3D.Cycles.Tests --configuration Release --no-build --filter-class "*GpuRenderTests"` | 2 | 49 total, 44 passed, 2 failed, 3 skipped; 5 films and `manifest.json` rewritten |
| `dotnet test ... --configuration Release --no-build` (after recording) | 2 | 332 total, 324 passed, 2 failed, 6 skipped |
| `dotnet test ... --configuration Debug --no-build` (after recording) | 2 | 332 total, 324 passed, 2 failed, 6 skipped |

The STALE warning is a fact of the base commit, not of this machine: `native/binaries/linux-x64/provenance.json`
records `sourceCommit` `d861e1589` and `cyclesRevision` `a456b761...` (the previous pin), and the re-pin commit
followed it. NU5017 also comes from the tree: the solution contains the pack shim, which is packable and has no nuspec
unless the driver passes one.

Skips (6, all WebGPU-only on a Metal binary): `WebGpuAmbientOcclusionTests.gpu_ao_pass_matches_the_cpu_reference_with_the_practical_criterion`
("CyclesBackend.Gpu resolved to Metal, not WebGPU."); `BakingTests.the_webgpu_device_bakes_the_known_results_and_matches_the_cpu_bake`
and `SceneSettingsTests.Dispersion_is_refused_by_name_on_the_webgpu_backend` ("This binary's GPU backend is not
WebGPU."); `GpuRenderTests.webgpu_refuses_the_integer_and_component_nodes_by_name_and_the_cpu_renders_them` for Boolean
Math, Integer Math and Get Vector Component ("Cycles' Metal device renders these nodes; the refusal is the WGSL
device's."). The `Texture exceeds maximum allowed size of 16384 x 16384 (requested: 16385 x 1)` lines in the output come
from the device-error recovery test, which renders that image on purpose and passed.

Failures before re-recording: the three Metal-authority tests whose void references differed
(`glass` 15 practical failures, maximum 0.0302; `gltf-sample` 3, 0.00522; `gltf-textured-plane-blender` 4, 0.00712;
`diffuse-sun` and `textured-plane` were within the criterion of their void films), and the two tests below. After
re-recording, every Metal-authority test passes in Release and Debug.

**Two tests judge the Metal film against the CPU.** They fail in all four runs (Release and Debug, before and after
re-recording) with nearly the same maxima; neither goes through the Metal reference policy (`GpuReferences.Judge`):

- `GpuRenderTests.gpu_cameras_and_sampling_features_match_the_cpu_reference(variant: "adaptive-blue-noise")`: 51
  practical failures, maximum absolute error 0.0427245 (`GpuRenderTests.cs:512`, `practical.Should().Be(0)`).
- `GpuRenderTests.gpu_lamp_shaders_conductor_thin_films_and_sky_worlds_match_the_cpu_reference(variant: "sky-sun-guiding")`:
  1 practical failure, maximum 0.00146 (Debug after recording 0.00143) (`GpuRenderTests.cs:677`).

Both tests were added on 2026-11-01, after the last managed run on this platform (references of 2026-10-24); no record
of an earlier run of them on Metal is in the repository. Section 5 of the testing document says a Metal render is never
failed only for differing from the CPU. The oracle run shows the same kind of difference on the corresponding scenes,
as information: `sampling/adaptive-blue-noise` 177 CPU-against-Metal practical failures (maximum 0.645),
`sky/multiple-scattering-sun` 2, `sky/multiple-scattering` 1, `sky/sun-guiding-emissive-panel` and
`sky/sun-guiding-light-tree-panel` 3 each. The tests, tolerances and scenes are unchanged. **INFERENCE:** adaptive sampling stops per pixel on a threshold, so small
CPU/Metal differences change the sample count of some pixels; the multiple-scattering sky is evaluated from a
precomputed image, which Metal samples with hardware filtering (the texture-filtering note of section 5).

## Metal references recorded again

The command of step 7 rewrote all five films of `tests/CodeBrix.Graphics3D.Cycles.Tests/References/metal-osx-arm64/` and
their `manifest.json` entries (recorded 2026-11-14 UTC, macOS 27.2.1, Apple M2 Pro (GPU - 16 cores), backend `metal`,
MetalRT unset, library version 1.0.20261114.0). Review (`managed/metal-reference-review.txt`, `managed/metal-manifest.diff`):
every new film has the recorded length and SHA-256, no NaN or Inf, and hundreds of distinct values.

| Film | New against previous Metal film | CPU comparison before (max / practical / strict) | CPU comparison after |
| --- | --- | --- | --- |
| `diffuse-sun` | max 1.13e-6, 0 practical | 2.98e-7 / 0 / 168 | 1.13e-6 / 0 / 270 (grew) |
| `glass` | max 0.0302, 15 practical | 9.42e-6 / 0 / 203 | 1.09e-5 / 0 / 276 (grew) |
| `textured-plane` | max 1.79e-7, 0 practical | 2.98e-7 / 0 / 73 | 2.98e-7 / 0 / 63 |
| `gltf-sample` | max 0.00522, 3 practical | 9.54e-7 / 0 / 471 | 9.21e-6 / 0 / 482 (grew) |
| `gltf-textured-plane-blender` | max 0.00712, 4 practical | 6.5746e-3 / 162 / 1653 | 6.5756e-3 / 162 / 1660 (grew by 1e-6) |

Facts: four of the five CPU comparisons grew; all stay within the practical criterion except
`gltf-textured-plane-blender`, whose 162 practical failures (texture filtering, section 5) are unchanged in number. The
other four new films are at most 1.1e-5 from their CPU films. The CPU films changed with the pin as well: the new
`glass` Metal film is 0.0302 from the previous one while both are within 1.1e-5 of their own CPU film, so the `glass`
CPU film moved by at least 0.030 (and the `gltf-sample` CPU film by at least 0.0052); before and after are measured
against different CPU films. For context, the oracle run's `linux-gates/glass-render` (a different glass scene): CPU
against Metal, maximum 1.01e-5, 0 practical failures.
**INFERENCE:** the growth (largest factor 9.7 on `gltf-sample`, absolute at most 1.1e-5) fits the Metal change of this
release that kit section 4 names: 18 integrator-state fields (ray time and differential, pass weights, subsurface
albedo and anisotropy) are stored in half precision on Metal, so Metal sits a little further from the CPU than before;
a newer Metal compiler (macOS 27.0.1 -> 27.2.1) and atomic film accumulation may add to it. No growth was traced to a
specific field.

## Step 8: licensing audit

`python3 tools/licensing/audit.py`, run with `native/evidence/cycles-oracles-macos-20261113/` in place and the
approved `research/translate.py` change applied: exit 1, `passed: false`, one error, verbatim:
`Unreviewed restricted-license wording: docs/KIT-MACOS-ARM64.md`. The audit's licence-keyword check matches line 47 of
that document (the sentence of kit rule 4 that names the licence of Blender outside Cycles); the file is
unchanged from the base commit and has no entry in `third_party/license-review-exceptions.json`.
Run again with this folder in place (124.89 s, exit 1): the same single error. The new oracle evidence, this folder,
the re-recorded references and the translator change raise none.

## Translator change applied after the base run

After the oracle run, `native/work/translate-experiment-20261113/proposed.patch` (approved) was applied to
`research/translate.py`: `-U__ARM_NEON` in the clang AST command, so an ARM64 build host reads the same generic branches
of the pinned `util/half.h` as x86-64. SHA-256 `e853b3ab...` -> `c3951413a6d60dfb...`. None of the runs in this folder
uses the translator (the osx-arm64 recipes do not run it). `compatibility/evidence.json` pins the old hash in 11 of its
24 checkpoints; all 24 already fail `verify_evidence` at the base commit for other changed sources, and
`python3 tools/compatibility/inventory.py check` stops before them ("Compatibility baseline differs from reviewed Cycles
revision", exit 1; the ledger is recorded at the previous revision, compatibility/README.md). The pinned record is not
edited (`kit/translator-pin-check.txt`).

## Part A against its completion criteria (kit section 4)

- Build with 0 warnings in our own code: not met, 1 warning (the host-gate fixture above).
- macOS host gate: passed.
- Whole committed corpus and the bench on CPU-arm64 and Metal, every film repeat-identical or recorded: met.
- Managed build with 0 warnings and tests passing with freshly recorded Metal references: not met. The Release build
  reports the package check's STALE warning for the linux-x64 payload; two tests that judge Metal against the CPU fail;
  `dotnet pack` of the solution exits 1 with NU5017.
- Licence audit: not met, one error on `docs/KIT-MACOS-ARM64.md`.
- Staging (step 9): not done by the kit.

## Findings for the maintainer

1. The kit's READ statement on the dependency prefix did not hold here (prefix older than the Embree addition, and an
   Xcode update); a full dependency rebuild was needed.
2. `native/tests/host/image_inputs.h:41` (`write_image` unused) is a warning in our own code on Apple Clang.
3. The committed linux-x64 payload is stale against the current pin, so every Release build of the solution reports a
   warning until it is restaged on the Linux x64 machine.
4. `dotnet pack` of the solution fails with NU5017 on the pack shim.
5. Two managed GPU tests require Metal to match the CPU, against the reference policy of section 5.
6. `docs/KIT-MACOS-ARM64.md` fails the licensing audit's wording check.
