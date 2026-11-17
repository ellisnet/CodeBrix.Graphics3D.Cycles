Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

# macOS ARM64 device run: the Apple Silicon work order at main 3f162bfe (2026-11-17)

The report of the work order in [docs/KIT-MACOS-ARM64.md](../../../docs/KIT-MACOS-ARM64.md), carried out on an Apple Silicon
Mac at the scope the maintainer set for this visit:

1. the build at the base commit, 0 warnings;
2. the macOS host gate, as a single check that the changed host sources did not break the Metal build;
3. the oracle run of the 18 scenes that are new since the last macOS oracle evidence, on the three macOS backends, with the
   CPU-to-Metal comparison of each;
4. the three Metal references (kit part B, unchanged);
5. the closing checks (kit part D) and this report.

The osx-arm64 payload is **not** restaged (kit part C: the maintainer's decision for this visit). Everything the kit asks
for beyond this scope is listed under "What did not run". The oracle bundle was published by the runner as
[`native/evidence/cycles-oracles-macos-20261117/`](../cycles-oracles-macos-20261117/). Facts first; lines marked
**INFERENCE** are conclusions that were not measured directly. Absolute host paths in prose are written as `<repo>`;
copied tool output keeps the paths it was written with (`<repo>` = `/Volumes/VideoWork/GitHome/CodeBrix.Graphics3D.Cycles`).

- Branch `macos-kit-work`, made from `main`. **Base commit** `3f162bfe12a1d23188f087c4d9b5f8947995aa29` (`main` at the start;
  the commit that contains the kit). `git status --porcelain` printed nothing at the start (`identity/identity.txt`), and
  before and after the oracle run (`oracle/oracle-git-status-*.txt`). Every oracle `meta.json` (54) records `repoCommit`
  `3f162bfe...` and `repoWorkingTreeClean: true`; the build records name the same commit with a clean Cycles-with-Metal
  status (`build/build-check.json`, `commitRecord`).
- Pinned Cycles: v5.3.0, revision `b3f6262640f1a2f2d10b55b6ffccbf310ce0f981` (unchanged; nothing under `third_party/` was
  touched).
- Machine: Mac mini (Mac14,12), Apple M2 Pro (10 CPU cores, 6 performance and 4 efficiency; 16-core GPU, Metal 4), 16 GB,
  macOS 27.2.1 (26B131).
- Tools: Xcode 27.2 (27B52), macOS SDK 27.2, Apple clang 21.0.0 (clang-2100.4.1.1), CMake 4.4.5, Python 3.15.2, zstd 1.5.7,
  .NET SDK 11.0.100 (`dotnet --version`, `managed/dotnet-version.txt`; the projects target `net10.0`), ISPC 1.30.0 (the
  binary under `<repo>/native/work/tools/ispc-v1.30.0-macOS.arm64/`, passed as `CODEBRIX_ISPC`). Nothing was installed.
- No WebGPU binary was loaded: the osx-arm64 profile has no WebGPU device.

No executables or libraries are in this folder. Their SHA-256: host library
`891e314ef14a17254e97ece8db0fd5dfe39ab709af0c84ae5ae9e0085802cd33`, oracle runner
`1f90b7df1bac4910b08ddae6a78de82246df3d87f07a75f9ff248f4899e692d2` (both also in the oracle folder's `run.json`; every
build artifact in `build/host-status.json`), and every file of the host gate's `managed-bin/` in
`host-gate.managed-bin-sha256.txt`. `SHA256SUMS.txt` lists every other file of this folder (`shasum -a 256 -c SHA256SUMS.txt`).

## Steps

| Step | Command | Verdict | Wall |
| --- | --- | --- | --- |
| identity | testing document section 7 | recorded (`identity/identity.txt`; serial number and hardware identifiers redacted) | about 7 s |
| build | `native/work/macos/osx-arm64/{host,oracle-build,dependencies}` set aside, then `CODEBRIX_ISPC=... python3 tools/build_macos.py --jobs 8` | passed: `status.json` `passed: true`, 65 exports, 292 kernel files; 0 warnings in this repository's code | 316.56 s |
| host gate | `python3 native/tools/macos_host_gate.py --output native/work/macos-validation-20261117` | passed: 3,102 films, 0 same-host bitwise failures, 0 Metal practical failures, 0 Metal strict differences, Metal maximum 4.47e-8 | 28.00 s |
| oracle run | `render_oracles.py`, three backends, the 18 new scenes passed with `--scene` (`oracle/scenes-new-18.txt`) | 18 scenes per backend, 0 failures, `comparisonsPassed` 18/18 | 54.12 s |
| bundle | `python3 native/oracle/render_oracles.py --bundle native/work/oracles/macos-kit-20261117` | 243 files verified, published as `cycles-oracles-macos-20261117/` | 1.82 s |
| B.1 | restore; `dotnet build --configuration Release`; `dotnet test --solution ... --configuration Release --no-build` | 525 tests: 522 passed, 3 skipped, 0 failed | 1.12, 10.42, 68.87 s |
| B.2 | the test change (below) | - | - |
| B.3 | Release build; `CODEBRIX_CYCLES_RECORD_METAL_REFERENCES=1 dotnet test --project tests/CodeBrix.Graphics3D.Cycles.Tests --configuration Release --no-build --filter-class "*GpuRenderTests"` | 74 tests: 74 passed | 12.58, 67.68 s |
| B.4 | review of the recording (`references/`) | three new entries kept; 21 rewritten existing films not accepted (below) | - |
| B.5 | Release test; Debug build; Debug test (no recording variable) | Release and Debug: 525 tests, 522 passed, 3 skipped, 0 failed | 67.15, 3.81, 75.55 s |
| D | `git diff --check`; `python3 tools/licensing/audit.py`; delete the `nugets/Release/<version>/` folders this run wrote | clean; `"passed": true`, 0 errors; `1.0.321.1242` and `1.0.321.1245` deleted | -, 154.57 s, - |

Each command's exact text, UTC start, wall and exit status are in `closing/commands.tsv` and `closing/walls.txt`.

## Build and host gate

- Warnings (`build/build-check.json`, from `host-build.output.txt`, `host-configure.output.txt` and the console):
  this repository's code **0**; pinned Cycles 1, the same as on 2026-11-15
  (`src/device/metal/device_impl.mm:559:13: warning: 'fastMathEnabled' is deprecated`); dependencies 120 `warning:` lines
  in the component logs under `native/work/macos/osx-arm64/dependencies/` (not copied).
- Watch-points (`build/build-check.json`): 176 compile commands, none with `WITH_DLSS` or `WITH_WEBGPU`; `-DWITH_EMBREE` on
  `src/bvh/embree.cpp`; `overlaidInClosure` exactly `["util/types_gpu_compressed.h"]`; 292 staged kernel files, all
  byte-identical to `third_party/cycles/src`.
- `sourceDateEpoch` `1794946379`; the library version the managed tests report is `1.0.20261117.0`.
- The oracle runner is byte-identical to the 2026-11-15 build (`1f90b7df...`); the host library is not (`891e314e...`
  against `b804223d...`), as expected: five Cycles-with-Metal files changed since then (`managed/stale-osx-arm64-files.txt`).
- The host gate's numbers equal those of 2026-11-15 (`device-macos-arm64-20261115`): 3,102 films compared, Metal maximum
  4.470348358154297e-8. Its records are in `host-gate/` (without `managed-bin/`) and its console in `host-gate.output.txt`.

## Oracle run: the 18 new scenes

The scenes are the documents under `native/oracle/scenes` whose ids are not in the 793 scenes of
`cycles-oracles-macos-20261115` (`oracle/scenes-new-18.txt`). All 18 rendered on `cpu-arm64-macos`, `metal-m2pro` and
`metal-m2pro-metalrt`; no backend refused a scene, no film holds a NaN or Inf, and no `meta.json` records a Metal compile
warning. `kernelCompileSeconds`: `metal-m2pro` 1.28405 s, `metal-m2pro-metalrt` 0.881479 s (warm-up scene
`bsdf/glass-multiscatter-caustics-off`). CPU repeats are bitwise identical; Metal repeats differ by at most 1.91e-6.

None of the 18 has a committed x64 CPU film (`linux-x64-reference-20261114` holds 724 scenes, none of these), so the
CPU-arm64 film is the CPU film each Metal film is compared with. `comparisonsPassed` is true for all 18 because the three
macOS backends are all reference backends (`compare.py` reports their pairs, it does not gate them). The comparisons, from
`oracle/oracle-table.md` (the `compare.py` reports are in the oracle folder's `comparisons/`):

| Scene | CPU vs Metal: practical / strict / max abs | CPU vs MetalRT: practical / strict / max abs | Metal vs MetalRT max abs | Metal repeat max abs | non-finite |
| --- | --- | --- | --- | --- | --- |
| `bsdf/glass-multiscatter-caustics-off` | 0 / 427 / 9.72e-06 | 0 / 440 / 1.34e-05 | 3.7e-06 | 5.96e-08 | 0 |
| `bsdf/glass-multiscatter-extremes` | 0 / 574 / 1.01e-05 | 0 / 554 / 1e-05 | 1.74e-06 | 5.96e-08 | 0 |
| `cameras/border-adaptive` | 0 / 328 / 5.38e-05 | 0 / 325 / 6.5e-06 | 4.73e-05 | 1.49e-07 | 0 |
| `curves/bump-strong-noise-generated` | 1425 / 3735 / 0.0316 | 2264 / 4143 / 0.0542 | 0.0644 | 1.19e-07 | 0 |
| `lamps/sphere-light-inside` | 0 / 276 / 8.79e-06 | 0 / 278 / 8.82e-06 | 5.3e-06 | 8.94e-08 | 0 |
| `mnee/glass-multiscatter-caustic` | 0 / 37 / 5.36e-06 | 0 / 38 / 7.51e-06 | 4.74e-06 | 5.96e-08 | 0 |
| `surface-nodes/bump-emission` | 2 / 571 / 8.58e-05 | 2 / 578 / 8.24e-05 | 1.63e-05 | 4.77e-07 | 0 |
| `surface-nodes/bump-emission-displacement` | 1 / 602 / 0.00288 | 2 / 617 / 0.00942 | 0.00942 | 4.77e-07 | 0 |
| `surface-nodes/bump-emission-lamp` | 0 / 156 / 1.22e-06 | 0 / 155 / 1.22e-06 | 5.96e-08 | 1.19e-07 | 0 |
| `surface-nodes/bump-emission-unsampled` | 1 / 769 / 8.58e-05 | 1 / 774 / 8.23e-05 | 1.85e-05 | 9.54e-07 | 0 |
| `surface-nodes/bump-emission-world` | 0 / 498 / 5.72e-06 | 0 / 498 / 5.72e-06 | 1.19e-07 | 1.19e-07 | 0 |
| `surface-nodes/bump-strong-noise-normal` | 38 / 1500 / 0.00632 | 39 / 1508 / 0.00632 | 0.00099 | 1.19e-07 | 0 |
| `surface-nodes/bump-volume` | 0 / 365 / 1.91e-06 | 0 / 373 / 1.91e-06 | 8.94e-08 | 5.96e-08 | 0 |
| `surface-nodes/raycast-ao-bevel-subsurface-volume` | 2 / 318 / 0.000145 | 5 / 322 / 0.002 | 0.002 | 1.19e-07 | 0 |
| `surface-nodes/tangent-node-emission` | 105 / 521 / 0.125 | 111 / 527 / 0.125 | 0.136 | 1.19e-07 | 0 |
| `surface-nodes/tangent-node-glossy-glass-normal` | 57 / 441 / 0.0406 | 79 / 456 / 0.0415 | 0.0209 | 5.96e-08 | 0 |
| `volumes/heterogeneous-open-boxes` | 497 / 537 / 0.212 | 501 / 537 / 0.212 | 0.122 | 5.96e-08 | 0 |
| `volumes/light-path-heterogeneous-emission` | 354 / 999 / 1.42 | 264 / 1011 / 0.915 | 1.42 | 1.91e-06 | 0 |

**Findings** (testing document section 5: two reference backends that disagree beyond the practical criterion are a
finding for the maintainer): Metal and the CPU disagree beyond the practical criterion on 10 of the 18 scenes, on both
Metal backends. The largest are `volumes/light-path-heterogeneous-emission` (354 components, maximum 1.42),
`volumes/heterogeneous-open-boxes` (497, 0.212), `surface-nodes/tangent-node-emission` (105, 0.125),
`surface-nodes/tangent-node-glossy-glass-normal` (57, 0.0406), `curves/bump-strong-noise-generated` (1,425, 0.0316) and
`surface-nodes/bump-strong-noise-normal` (38, 0.00632). The other eight scenes agree within the practical criterion.
Nothing was changed to make them agree.

For scale, as information: in `cycles-oracles-macos-20261115`, 365 of the 793 scenes had CPU-to-`metal-m2pro` practical
failures, up to a maximum of 1,117 (`lamps/area-degenerate-rect`). **INFERENCE:** the share here (10 of 18) is in line with
that corpus; the scenes with the largest differences (heterogeneous volumes, the Tangent node, strong bump noise) are of
kinds whose CPU and Metal paths are known to differ elsewhere in the corpus, but no cause was measured.

## The three Metal references (kit part B)

**B.1, before the change** (`managed/b1-*.output.txt`): Release build exit 0; Release tests 525: 522 passed, 3 skipped,
0 failed. The three cases of `gpu_renders_the_integer_and_component_nodes_like_the_cpu` passed on Metal by the finite check
alone. The 3 skips are the WebGPU-only tests (`managed/b1-skips-release.txt`):
`BakingTests.the_webgpu_device_bakes_the_known_results_and_matches_the_cpu_bake`,
`SceneSettingsTests.Dispersion_is_refused_by_name_on_the_webgpu_backend` and
`WebGpuAmbientOcclusionTests.gpu_ao_pass_matches_the_cpu_reference_with_the_practical_criterion`. On 2026-11-15 there were 6:
the three `webgpu_refuses_the_integer_and_component_nodes_by_name_and_the_cpu_renders_them` cases no longer exist on this
commit (the refusal was lifted), and the test that replaced them runs on every backend, so the total stays 525.

**B.2, the test change** (`tests/CodeBrix.Graphics3D.Cycles.Tests/GpuRenderTests.cs`, the only source change of this run):
the Metal-only branch of `gpu_renders_the_integer_and_component_nodes_like_the_cpu` is removed, so both backends go through
`Judge` with the names `integer-and-component-nodes-boolean-math`, `integer-and-component-nodes-integer-math` and
`integer-and-component-nodes-get-vector-component`; the comment inside the test that described the finite-only check is
removed, and the comment above the test now says that the GPU film is judged against the authority for this GPU (the CPU
film for WebGPU, the recorded Metal reference for Metal).

**B.3, the recording**: Release build exit 0, then the recording command of the kit: 74 tests of `GpuRenderTests`, all
passed (`managed/b3-*.output.txt`).

**B.4, the review** (`references/reference-review.json`, made by `closing/scripts/reference_review.py`; the recorded
manifest as `references/recorded-manifest.json` and its `git diff` as `references/recorded-manifest.diff.txt`).

The three new entries, as recorded (`os` macOS 27.2.1, device Apple M2 Pro (GPU - 16 cores), backend `metal`, MetalRT unset
(Blender's rule), library version `1.0.20261117.0`, 3,072 components each):

| Name | SHA-256 | CPU comparison: maximum abs / practical failures / strict differences |
| --- | --- | --- |
| `integer-and-component-nodes-boolean-math` | `5671f9a0e5f43116a5dce5b03b14f994a947e4506eeec88e72c84d408bef1cff` | 1.16e-6 / 0 / 259 |
| `integer-and-component-nodes-integer-math` | `1d50de1ef91e5a815506ed27bb43ec40fc1f80d3053f0352d162f2502635e7b1` | 1.19e-6 / 0 / 198 |
| `integer-and-component-nodes-get-vector-component` | `dea1002bb0fa89d8827b27c412acf87959985b97ec6af1746092c3a6445e2a2d` | 1.19e-6 / 0 / 199 |

**Finding: the recording rewrote the existing references.** `GpuReferences.Record` writes the film and manifest entry of
every name a recording run judges, not only of missing names, so the kit's command rewrote all 23 existing entries (new
`recordedUtc`, `libraryVersion` `1.0.20261114.0` or `1.0.20261115.0` → `1.0.20261117.0`) and the films of 21 of them. Every
rewritten film is within the practical criterion of its committed film; the two subdivision position bakes are bitwise
identical:

| Reference | Bytes identical to the committed film | Against the committed film: practical / strict / max abs | CPU comparison max abs, committed → recorded | CPU practical failures, committed → recorded |
| --- | --- | --- | --- | --- |
| `attribute-quaternion-spherical-harmonics` | no | 0 / 0 / 1.19e-07 | 4.86e-06 → 4.86e-06 | 0 → 0 |
| `bvh-spatial-split` | no | 0 / 0 / 2.98e-08 | 1.46e-05 → 1.46e-05 | 0 → 0 |
| `diffuse-sun` | no | 0 / 21 / 2.38e-07 | 1.13e-06 → 1.13e-06 | 0 → 0 |
| `diffuse-sun-adaptive-blue-noise` | no | 0 / 16 / 1.79e-07 | 0.0427 → 0.0427 | 51 → 51 |
| `diffuse-sun-sky-sun-guiding` | no | 0 / 1372 / 9.98e-05 | 0.00143 → 0.00146 | 1 → 1 |
| `displacement-edit` | no | 0 / 0 / 1.49e-08 | 1.48e-05 → 1.48e-05 | 0 → 0 |
| `gaussian-splats` | no | 0 / 0 / 5.96e-08 | 0.35 → 0.35 | 1050 → 1050 |
| `glass` | no | 0 / 27 / 3.58e-07 | 1.09e-05 → 1.09e-05 | 0 → 0 |
| `gltf-sample` | no | 0 / 0 / 1.19e-07 | 9.21e-06 → 9.21e-06 | 0 → 0 |
| `gltf-textured-plane-blender` | no | 0 / 1 / 4.77e-07 | 0.00658 → 0.00658 | 162 → 162 |
| `subdivision-bake-combined` | no | 0 / 0 / 2.98e-08 | 2.09e-07 → 2.09e-07 | 0 → 0 |
| `subdivision-bake-combined-displaced` | no | 0 / 0 / 2.98e-08 | 1.36e-05 → 1.36e-05 | 0 → 0 |
| `subdivision-bake-diffuse-color` | no | 0 / 0 / 5.96e-08 | 5.96e-08 → 5.96e-08 | 0 → 0 |
| `subdivision-bake-diffuse-color-displaced` | no | 0 / 0 / 5.96e-08 | 5.96e-08 → 5.96e-08 | 0 → 0 |
| `subdivision-bake-normal` | no | 0 / 0 / 1.49e-08 | 2.98e-07 → 2.98e-07 | 0 → 0 |
| `subdivision-bake-normal-displaced` | no | 0 / 0 / 1.19e-07 | 6.29e-06 → 6.29e-06 | 0 → 0 |
| `subdivision-bake-position` | yes | 0 / 0 / 0 | 4.77e-07 → 4.77e-07 | 0 → 0 |
| `subdivision-bake-position-displaced` | yes | 0 / 0 / 0 | 6.56e-07 → 6.56e-07 | 0 → 0 |
| `texture-limit` | no | 0 / 0 / 1.19e-07 | 2.26e-05 → 2.26e-05 | 0 → 0 |
| `textured-plane` | no | 0 / 2 / 1.79e-07 | 2.98e-07 → 2.98e-07 | 0 → 0 |
| `tiled-image` | no | 0 / 0 / 5.96e-08 | 1.58e-06 → 1.58e-06 | 0 → 0 |
| `tiled-rendering` | no | 0 / 0 / 1.19e-07 | 1.36e-05 → 1.36e-05 | 0 → 0 |
| `udim-tiles` | no | 0 / 0 / 5.96e-08 | 2.98e-07 → 2.98e-07 | 0 → 0 |

One recorded CPU comparison grew: `diffuse-sun-sky-sun-guiding`, maximum 1.43e-3 → 1.46e-3, still 1 practical failure;
it is also the film furthest from its committed film (9.98e-5, within the practical criterion, 1,372 strict differences).
No cause was measured. **INFERENCE:** the other 20 rewrites are Metal's run-to-run differences (film accumulation uses
atomics, testing document section 5), at most 4.77e-7.

The rewritten films are **not accepted**: the scenes and settings of the existing references did not change, and the kit
expects exactly three new entries. The 23 committed films were written back from `HEAD` and the manifest is the committed
one plus the three new entries in the recorder's own text (`closing/scripts/keep_new_references.py`, which checks that
every committed entry is unchanged and every new entry equals the recorded one). The full recorded set stays outside the
repository in `<repo>/native/work/macos-kit-20261117/recorded/` (git-ignored), and its manifest is in `references/`.
`git diff` of `manifest.json` now shows exactly the three new entries.

**B.5, after the change** (`managed/b5-*.output.txt`), no recording variable: Release tests 525: 522 passed, 3 skipped,
0 failed; Debug build exit 0, 0 warnings; Debug tests 525: 522 passed, 3 skipped, 0 failed. The skips are the three of B.1
(`managed/b5-skips-*.txt`). The integer and component cases are now judged on Metal against the three new references, and
the 23 existing references, as committed, judge this build's Metal films without a failure.

**Warnings of the Release builds**: 0 compiler warnings. Each Release build reports 2 MSBuild warnings, both written by the
pack driver's `package_check.py` (`managed/b1-build-release.output.txt`):

- `osx-arm64: payload is STALE: 5 Cycles-with-Metal source files changed since its source commit 24b74e095f5f1f4526e095e7ac4ad6dfff62bb4c`
  (the five files: `native/host/ABI.md`, `native/host/MACOS_ARM64.md`, `native/host/cycles_host.h`,
  `native/host/host_api.inc`, `native/host/host_scene.inc`; `managed/stale-osx-arm64-files.txt`);
- `linux-x64: payload is STALE: 35 Cycles-with-WGSL source files changed since its source commit 52b680efc90b2adaf62eb965ed2334a2c85a50f0`
  (not this platform's work).

## Staging (kit part C)

Not staged, on the maintainer's decision for this visit. The osx-arm64 payload under `native/binaries/osx-arm64/` is still
the stage of `24b74e09` and is **stale**: the five Cycles-with-Metal files above changed since. The status paragraph at the
top of `docs/TESTING-MACOS-ARM64.md` is left as it is.

## Closing checks (kit part D)

- `git diff --check`: clean.
- `python3 tools/licensing/audit.py`: `"passed": true`, 0 errors (`closing/audit-summary.json`; the full output, 32 MB, is
  not kept), run after the oracle evidence was published and again after this folder was written.
- The `nugets/Release/1.0.321.1242/` and `nugets/Release/1.0.321.1245/` folders written by the two Release builds were
  deleted (`managed/nugets-*.txt`); the six older version folders there were left as they were.
- The working tree holds: the test change, the three new reference films and the manifest change,
  `native/evidence/cycles-oracles-macos-20261117/` and this folder. Nothing else changed.

## What did not run

By the maintainer's scope for this visit:

- the oracle run over the whole corpus (the 793 scenes of `cycles-oracles-macos-20261115`), and so the bitwise check of
  every earlier CPU-arm64 film against that evidence, and the bench documents; step 5 of the testing document (comparison
  with the previous macOS evidence) has nothing to compare: none of the 18 scenes has an earlier film;
- the kit's table for the degenerate area lights, the zero-extent portals and the zero-area bake scenes: those scenes are
  not new (all are in `cycles-oracles-macos-20261115`, rendered on the three backends with no failure) and were not
  rendered again;
- the consumer test of the MacOSArm64 package (testing document step 7) and staging (part C).

## Questions for the maintainer

1. The rewritten existing references were not accepted, and the committed films were written back. If the recorded set
   should replace them instead, it is in `<repo>/native/work/macos-kit-20261117/recorded/` (21 changed films, 23 updated
   entries), with the one grown CPU comparison of `diffuse-sun-sky-sun-guiding` to be explained first.
2. The recorder rewrites every reference a recording run reaches, so the kit's expectation of exactly three new entries
   cannot hold for its command alone. Should the recorder write only missing names unless asked to re-record, or should
   the testing document describe the restore of the unchanged references after a partial recording?
3. Section 1 of the kit names the integer and component nodes, the degenerate area lights and the zero-area bakes among
   oracle scenes never rendered on this platform; all of them are in `cycles-oracles-macos-20261115`. The 18 scenes that
   are new are the glass multiscatter, adaptive border, strong bump noise, sphere light inside, bump emission, Tangent node,
   raycast and open or heterogeneous volume scenes listed above. Was section 1 written against the 2026-11-13 evidence?
4. Do the CPU-to-Metal differences of the ten scenes above decide anything for the WebGPU device (as the degenerate-light
   results were meant to), or are they information only?

## Contents

| Path | What it holds |
| --- | --- |
| `identity/identity.txt` | the identity record of testing document section 7 (serial number and hardware identifiers redacted; paths as `<repo>` and `<home>`) |
| `build/` | `host-status.json`, `invocation.json`, `kernel-sources.json`, `platform.patch`, `cycles-registration.patch`, the host configure and build logs as `.output.txt`, the dependency marker, `build-check.json` and the console of `tools/build_macos.py` |
| `host-gate/` | the gate's output folder without `managed-bin/` (`status.json`, `results.json`, `comparisons.json`, `binary-review.json`, `commands.json`, the films and one `.stdout.txt`/`.stderr.txt` pair per call) |
| `host-gate.output.txt`, `host-gate.managed-bin-sha256.txt` | the gate's console; the SHA-256 of every file of `managed-bin/` |
| `oracle/` | the scene list, the CPU-to-Metal table, the consoles of the render and the bundle, `git status` before and after the render |
| `managed/` | `dotnet --version`, the output of every managed command (B.1, B.3, B.5), the skip lists, the `nugets/Release` lists and the stale osx-arm64 files |
| `references/` | the recording review, the recorded manifest and its `git diff` |
| `closing/` | `commands.tsv` (every command, start, wall, exit), `walls.txt`, `audit-summary.json` and `scripts/` (the scripts that ran or checked the steps) |
