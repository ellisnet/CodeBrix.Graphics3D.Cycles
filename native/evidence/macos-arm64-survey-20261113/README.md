Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

# Apple Silicon kit report and feature survey (Part C) (2026-11-13/14)

This is the report of [docs/KIT-MACOS-ARM64.md](../../../docs/KIT-MACOS-ARM64.md) (its section 9) and the ONE evidence
folder of its Part C, the feature survey: the survey's films, its patches, its scratch scene documents, its comparisons,
the section 9 repeat of [docs/TESTING-MACOS-ARM64.md](../../../docs/TESTING-MACOS-ARM64.md) (Velvet and Radial Tiling) with
the Metal compute probe, and the row 15 reading table. Parts D and E are still in progress (sections 5 and 6). Facts
first; lines marked **INFERENCE** are conclusions no run confirmed. Absolute host paths in prose are written as `<repo>`
and `<home>`; copied tool output (`run.json`, `meta.json`, comparison JSON, console records) keeps the paths it was written
with, which are under `<repo>` = `/Volumes/VideoWork/GitHome/CodeBrix.Graphics3D.Cycles`. Every comparison is reported,
never judged: CPU-arm64 and Metal are both gold standards (testing document section 5).

## 1. Base commit, branch, machine, tools, walls

- Branch `cycles-with-metal-v5.3.0`. Kit base commit `828f6c2f9ee2c57dd379d472a0252edaf3caa87a` (Part A built and rendered
  there). The survey's phase 1 ran at `0b19d87507ac24f1e55ba6a50d1ac6b05c8d417c` (the first commit of the branch: Part A
  records and documentation; `git diff --stat 828f6c2f9 0b19d8750 -- native/host native/cycles native/tools native/oracle
  third_party tools/licensing/import_cycles.py tools/build_macos.py tools/dependencies` is empty, so every build input is
  the base commit's). Phase 2 (the new committed scenes, this folder) ran at `bfc223792fe1260d76ca578cd7aa1b16253a2df0`,
  which adds Part B's evidence and four approved fixes outside the render path (none in `native/host`, `native/cycles`,
  `native/oracle` or `third_party`).
- Pinned Cycles v5.3.0, revision `b3f6262640f1a2f2d10b55b6ffccbf310ce0f981`.
- Machine: Mac mini (Mac14,12), Apple M2 Pro (10 CPU cores: 6 performance, 4 efficiency; 16-core GPU, Metal 4), 16 GB,
  macOS 27.2.1 (26B131). Tools: Xcode 27.2 (27B52), macOS SDK 27.2, Apple clang 21.0.0 (clang-2100.4.1.1), CMake 4.4.5,
  Python 3.15.2 (`environment/identity.txt`; the host name is replaced by `<host>`).
- Base build used for every base-build render: `<repo>/native/work/macos/osx-arm64/` of Part A (host library
  `118d1e89846d10d553db261d37d94dfa66540728656c8d856e7d6815ae67d340`, runner `486b29f946319f99...`). It was only read and
  run; the SHA-256 of its `host/status.json`, `kernel-sources.json`, `invocation.json`, host library, runner and
  dependency marker are the same before and after the survey (`environment/main-tree-hashes-start.txt`, `-end.txt`).
- Tree state: every phase-1 run records `repoWorkingTreeClean: true` at `0b19d875`. The two phase-2 runs record
  `repoWorkingTreeClean: false` at `bfc22379`: the uncommitted changes then were only this survey's (the six author
  scripts, `native/oracle/README.md` and the 34 new scene documents; `environment/phase2-git-status-after-apply.txt`).
- While phase 1 ran, a Docker container rendered Part B on the same machine, and during phase 2 another agent worked on
  packaging; every wall below is inflated by that. `kernelCompileSeconds` (first Metal render with a fresh Cycles cache)
  was 0.7 to 9.3 s in every survey run (Part A: 124 s and 115 s), because the macOS-managed Metal shader cache is not
  cleared (`metalSettings.systemMetalShaderCache`).

| Step | Wall (real) |
| --- | --- |
| Copy of the tracked tree (`git archive`) / APFS clone of the dependency prefix | 4.56 s / 8.39 s |
| Unpatched control build in the copy | 89.01 s |
| Patched survey build (incremental) | 21.25 s |
| Diagnostic copy build (full, failed) / again (incremental, passed) | 70.28 s / 27.47 s |
| Runs R1, R1b, R2, R3, R4, R10 | 54.90, 21.32, 112.82, 48.42, 44.87, 22.96 s |
| Runs D11 steps 0, 1, 3 | 51.37, 24.49, 20.20 s |
| Phase 2: run `phase2` (34 new committed scenes) / `phase2-scratch` | 67.93 s / 23.21 s |

Start and end times of every run: `environment/walls.txt`; console output: `environment/run-<run>.output.txt`.

## 2. Part A: the base

Recorded in [`native/evidence/device-macos-arm64-20261113/`](../device-macos-arm64-20261113/) (steps 1 to 8 of the testing
document, verbatim messages, findings) and [`native/evidence/cycles-oracles-macos-20261113/`](../cycles-oracles-macos-20261113/)
(the oracle bundle: the whole corpus, 724 committed scene documents and the 2 bench documents, on `cpu-arm64-macos`,
`metal-m2pro` and `metal-m2pro-metalrt`). In short: the build passed after a full dependency rebuild (the existing
prefix predated the Embree snapshot and an Xcode update); the macOS host gate passed; the oracle run passed with 0
failures, `comparisonsPassed` 726/726, every CPU film repeat-identical; the managed build had one warning (the stale
linux-x64 payload), `dotnet pack` of the solution exited 1 (NU5017), two tests that judge Metal against the CPU failed,
and the licensing audit failed on one sentence of the kit document. The CPU-arm64 run of that bundle is the base run of
kit rule 9. The approved fixes of those findings are in commit `bfc22379` (decisions 10 to 13).

## 3. Part B: the Linux ARM64 CPU oracle

Recorded in [`native/evidence/linux-arm64-cpu-oracle-20261113/`](../linux-arm64-cpu-oracle-20261113/): 726 scenes (724
committed documents and the 2 bench documents) at commit `0b19d875` in a Debian arm64 container, no refusal, no failure,
every film repeat-identical; the comparison with Linux x64 is skipped (no v5.3.0 x64 reference is committed). Its
section 10 answers, copied:

1. **The scene count.** 726 = 724 committed scene documents + 2 bench documents. `bsdf/principled-dispersion` rendered
   (32x24, 8 samples), repeat-identical (`repeatMaxAbsDifference` 0), no warnings: dispersion renders on the CPU device
   at this pin.
2. **The mixed-phase volume helpers.** `volumes/phase-mie` and `volumes/mixed-phase-world` (32x24, 16 samples) rendered
   and are repeat-identical. `volumes/mixed-phase-world` carries the closure warning `Maximum number of closures exceeded:
   96 > 64`.
3. **The step count of an unbounded volume segment.** `volumes/heterogeneous-world-sun-tail` rendered and is
   repeat-identical (film-0 means R, G, B, A `[9.909120599986092e-07, 9.187378108288513e-07, 8.057808932500817e-07, 1.0]`;
   the macOS CPU-arm64 film of the base commit, for information, `[9.909120647355607e-07, 9.187378143075501e-07,
   8.057808965437433e-07, 1.0]`). The probe there shows that `(int)roundf(x)` saturates to 2147483647 for `inf`,
   `FLT_MAX`, `1e37`, `2147483648` and `4294967296`, which the clamp of `kernel/integrator/shade_volume.h` makes 1024
   steps: the WGSL assumption holds for every infinite and out-of-range value tested. A NaN converts to 0, which the clamp
   makes one step, not 1024; recorded for the maintainer.
4. **The NaN of a point hit at its own centre.** `0.0f / 0.0f` and `sqrtf(-1.0f)` both give `0x7fc00000` on ARM64: the
   WGSL assumption holds.
5. **Repeat identity.** True for every scene. Only the `RenderTime` pass (a wall-clock measurement) of
   `passes/data-passes` differs between the two renders, by design.

## 4. Part C: the feature survey

### 4.1 The table (one line per row of kit section 6)

CPU = `cpu-arm64-macos`; BVH2 = `metal-m2pro`; RT = `metal-m2pro-metalrt`; "pf" = components that fail the practical
criterion `abs(a - b) <= 5e-5 + 2e-4 * abs(b)` (CPU the reference), reported only. "Host" in an error means the text comes
from the CodeBrix host (`native/host`); no error in the survey came from Cycles or the Metal compiler. Every error is
quoted with the runner's prefix `codebrix_cycles_oracle: <call> returned <status>: ` (`native/oracle/runner.cpp:28-34`);
status 1 is `CBC_INVALID`, 2 `CBC_UNSUPPORTED`, 3 `CBC_FAILED`.

| # | Feature or case | CPU | Metal | Comparison or verbatim error |
| --- | --- | --- | --- | --- |
| 1 | Dispersion | rendered (Part A) | rendered (Part A) | `bsdf/principled-dispersion`: 0 pf BVH2, 1 pf RT (max 1.1e-4) |
| 2 | Boolean Math, Integer Math, Get Vector Component, linked inputs | rendered | rendered | new committed tables: 136 + 63 + 21 cases, CPU = BVH2 = RT bitwise, every case equal to the pinned value; undefined cases (scratch): no crash anywhere, CPU and Metal differ in 6 of 46 |
| 3 | Glass BSDF, multiscatter GGX | rendered | rendered | new `bsdf/glass-multiscatter`: 0 pf (max 1.1e-5 BVH2, 1.4e-5 RT) |
| 4 | Anisotropic transmission | rendered (Part A) | rendered (Part A) | 4 scenes, 0 pf each |
| 5 | Missing inputs; Bump and Tangent on points; emissive curves/points with shadow linking | rendered | rendered | new `surface-nodes/image-missing` 0 pf; `points/geometry-tangent` 5 pf, `-bump` 10 pf; shadow-linking scenes (Part A) 24 to 855 pf |
| 6 | Both displacement on curves and points | rendered, not repeat-identical | rendered, two runs disagree | scratch; Metal run against run 487 to 964 pf (max 0.11 to 0.25); CPU 4 distinct hashes in 4 films |
| 7 | Degenerate area lights, zero-extent portals | rendered | rendered | 9 new scenes, no refusal, no NaN/Inf: 0 pf for 6, 138 / 707-743 / 879 pf for `-tree-transmissive` / `-spread-tiny-volume` / `-infinite-axis` |
| 8 | Zero-area texel bakes; subdivision bake | 17 types rendered; 7 fail | same | 17 new `bake/zero-area-<type>` (0 to 186 pf); flat normal and 6 direct-light types: `cbc_host_bake returned 3: Nonfinite bake output` (host); subdivision: `cbc_host_bake (size) returned 2: A subdivision surface cannot be baked` (host), with the refusal lifted `cbc_host_bake (size) returned 1: The bake mesh has no triangles` (host) |
| 9 | Denoiser upscale factor | refused; with the refusal lifted the factor is ignored | accepted; film not upscaled | CPU: `cbc_host_render returned 2: Unsupported feature: a denoiser upscale factor other than 1 (this backend denoises with Open Image Denoise on the CPU, which does not upscale)` (host); lifted: film bitwise equal to factor 1. Metal, GPU denoising: the low-resolution image in the first W/f x H/f pixels, zeros after, no error. Shadow catcher: factor 2 = factor 1 bitwise |
| 10 | Radial Tiling, two sides; Velvet subnormal paths | rendered | rendered | roundness 0.3 / 0.5 / 0.75: 695 / 704 / 716 pf (max 1.76); roundness 0, 1, three sides, Velvet: 0 pf; Metal flushes subnormals with fast math on and off |
| 11 | BVH time steps | films bitwise unchanged at 1 and 3 | unchanged within repeat noise (max 1.2e-7), BVH2 and RT | survey build; the BVH2 does change (node counts 33/67/193 curves, 31/55/151 points) |
| 12 | Gaussian splats | refused | refused | `cbc_host_render returned 2: Unsupported feature: Gaussian splats are not available in this library (the point cloud input render_as must be points)` (host) |
| 13 | Quaternion and spherical-harmonics attributes | - | - | not renderable: the host has no value kind; left to Part D |
| 14 | dlss; `denoiser_passes` bit outside the guides | refused | refused | `cbc_host_render returned 2: Unsupported feature: the dlss denoiser is not available in this library (use openimagedenoise)`; `cbc_host_render returned 1: Invalid denoiser_passes: a bit that is not a denoiser guide pass (albedo 1, specular albedo 2, normal 4, roughness 8, depth 16, backward motion 32, specular motion 64)` (host) |
| 15 | Tiled rendering, UDIM, progressive results, texture limit, texture cache, spatial split | - | - | reading only (section 4.4) |

Maintainer rulings made on this evidence (decisions 14 and 15 of the session): the host will refuse the denoiser upscale
factor (row 9) and Both displacement on curves and points (row 6) on every backend by name, in a separate later commit.
This folder holds the evidence; the survey changed no host code. No committed scene document relies on either case: a
scan of all 760 documents (726 at the base plus the 34 new) finds no `denoiser_upscale_factor` other than 1 and no Both
displacement shader on a curve or point geometry (`environment/rulings-14-15-scan.txt`).

### 4.2 How the survey was made

**Survey builds** (kit section 6: refusals lifted only in a survey build). The tracked tree of `0b19d875` without
`native/evidence/` and `native/binaries/` was extracted with `git archive` into `<repo>/native/work/macos-arm64-survey-20261113/tree`,
the dependency prefix APFS-cloned into it (`cp -cRp`), and built there with `python3 tools/build_macos.py --jobs 8
--skip-dependencies` (the prefix was reused, nothing rebuilt). The patches were applied with `patch -p1 -d <copy>`, never
to the repository (`survey-build/`):

| Patch (`survey-build/patches/`) | Change (each file starts with copyright, SPDX, purpose and "SURVEY BUILD ONLY, NEVER PRODUCT CODE") |
| --- | --- |
| `row08-subdivision-bake.patch` | deletes the subdivision bake refusal, `native/host/host_bake.inc:60` |
| `row09-cpu-upscale.patch` | deletes the CPU/WebGPU upscale-factor refusal, `native/host/cycles_host.cpp:1160-1164` |
| `row11-bvh-time-steps.patch` | sets `SceneParams::num_bvh_time_steps` from `CODEBRIX_SURVEY_BVH_TIME_STEPS` (unset: Cycles' default 0), after `cycles_host.cpp:614` |
| `row11-diagnostic-log.patch` | a second copy only (`tree-diag`), on top of the three: with `CODEBRIX_SURVEY_CYCLES_LOG` set, Cycles' log level and a stderr line with the time steps in effect |
| `survey-build.patch` | the first three combined |

- **Control** (the unpatched copy against Part A's build, `survey-build/control-sections.txt`, `survey-build.json`): the
  whole-file SHA-256 of all 8 build artifacts differ (host `118d1e89...` against `052bde6b...`). `kernel-sources.json` is
  byte-identical and the dependency closure is identical. Every Mach-O section (21 in the host) and the symbol table
  without its N_OSO debug-map entries are identical in every artifact (`survey-build/macho_sections.py`). The differences
  are only in the debug map: all 132 N_OSO entries carry different object modification times, and two N_OSO paths contain
  the checkout path, for the objects of the sources outside the Cycles tree (`.../CMakeFiles/codebrix_cycles_host.dir/<repo>/native/host/cycles_host.cpp.o`
  and `metal_recovery.mm.o`). The runner differs in its sections because its absolute build rpath
  (`native/oracle/CMakeLists.txt:17`) is longer in the copy.
- **Survey build**: host `247a736b4714c3487ed37aa557693dcf4bae040d9a6eea2aae555eb7db5aa6db`, runner `7b11eca3...`, 0
  warnings in the incremental build. **Diagnostic build**: host `5350f820421dbf81...`, runner `f7139e6e...`; its first
  build failed with `cycles_host.cpp:620:12: error: no member named 'log_level_set' in namespace 'ccl'` (the patch lacked
  `#include "util/log.h"`); the fixed patch built (its log is the one kept). No executable or library is in this folder;
  their SHA-256 are in `survey-build/survey-build.json` and the `*-hashes.txt` files.
- **Equivalence of the survey build** (variable unset, run R2): the CPU films of 14 committed scenes
  (`starter/diffuse-triangle`, `denoise/beckmann-on`, `bake/combined` and the 11 row 11 scenes) are bitwise identical to
  Part A's base-run films; their Metal films are within 0 practical failures (max 5.9e-5) of Part A's.

**Scene documents.** Rows the host accepts and whose pinned result is defined got committed scenes written by the
family author scripts (section 4.3, phase 2). Everything else is a scratch document under `scenes/`, written by
`scenes/make_survey_scenes.py` from committed scenes (`python3 -I make_survey_scenes.py --table-builder native/oracle/scenes`):
the host refuses it, or its result is undefined (rows 2 and 6), or it fails on every backend (row 8a probes). Scratch ids
are `survey/<name>`; `meta.json` names them by their work path `<repo>/native/work/macos-arm64-survey-20261113/scenes/<name>.json`,
which is `scenes/<name>.json` here.

**Runs** (`render_oracles.py --backend cpu-arm64-macos --backend metal-m2pro --backend metal-m2pro-metalrt`,
`starter/diffuse-triangle` first as the Metal warm-up; command lines in `environment/run_survey.sh`). Each run is copied
as written, under `runs/<run>/` (R10 as `row10-metal-comparison/`); in comparison JSON its work path is
`<repo>/native/work/oracles/macos-arm64-survey-20261113-<run>/`.

| Run | Build | Content |
| --- | --- | --- |
| `runs/R1-base` | base | rows 6, 8a (flat normal), 8b, 9, 12, 14 |
| `runs/R1b-base-row6` | base | row 6 again, a second process |
| `runs/R2-survey` | survey, time steps unset | rows 8b, 9 (CPU without the refusal), 11 (steps 0), equivalence controls |
| `runs/R3-steps1`, `runs/R4-steps3` | survey, `CODEBRIX_SURVEY_BVH_TIME_STEPS=1` / `=3` | row 11 |
| `runs/D11-diag-steps0`, `-steps1`, `-steps3` | diagnostic, `CODEBRIX_SURVEY_CYCLES_LOG=debug` | row 11: 4 scenes with the BVH build statistics in `<backend>.output.txt` |
| `row10-metal-comparison` | base | row 10: the frozen section 9 documents and `bsdf/velvet` |
| `runs/phase2` | base | the 34 new committed scenes |
| `runs/phase2-scratch` | base | row 2 undefined cases, row 9 noisy-pass documents, row 8a probes |

Three draft-check runs of phase 1 (renders of the phase-2 scene drafts in the survey copy before they were committed) are
not copied: their documents were superseded by the committed ones, which `runs/phase2` rendered. The CPU films of the 34
committed scenes in `runs/phase2` are bitwise identical to those draft-check renders (31 the same documents, 3 the same
content under a probe id; `compare/summary.json`, `hashEquality` and `crossDocument` entries named `phase2-*`), so each is
repeat-identical across two processes as well as within one.

**Comparisons** (`compare/`): `render_oracles.py` writes each run's `comparisons/<scene>.json`; the cross-run and
cross-document comparisons are `native/oracle/compare.py` (CPU directory first) driven by `compare/cross_compare.py`
(112 reports, stdout in `compare/compare.output.txt`, a summary in `compare/summary.json`). The row 2 tables are decoded by
`compare/decode_tables.py` (`compare/row02-*.txt`, `.json`). No comparison failed for NaN/Inf, shape or hash;
`comparisonsPassed` is false only for documents a CPU refused (no CPU film to compare with).

### 4.3 Per row

**Rows 1 and 4, row 5c.** Committed scenes, rendered in Part A (comparisons in its bundle). CPU against BVH2 / RT,
practical failures: `bsdf/principled-dispersion` 0 / 1 (max 1.1e-4); `bsdf/glass-anisotropic`,
`bsdf/principled-anisotropic-transmission`, `mnee/anisotropic-glass-caustic`, `mnee/anisotropic-principled-caustic` 0 / 0;
`curves-b/linking-emissive` 29 / 41 (max 0.23), `-textured` 30 / 855 (0.22); `points/shadow-linking-emissive` 53 / 45,
`-textured` 55 / 47; `shadow-linking/emissive-textured` 26 / 24. No NaN/Inf.

**New committed scenes (phase 2).** Written by the family scripts (`native/oracle/README.md`, "Scenes added by the Apple
Silicon feature survey"); each family's `--check` passes before and after; none of the 726 committed documents changed
(each byte-compared with `HEAD`, `environment/phase2-scene-checks.txt`); no manifest covers these families. All 34 rendered
in `runs/phase2` on the three backends: no failure, no NaN/Inf, no warning, **every CPU film repeat-identical**.
Columns: Metal repeat max (BVH2 / RT); CPU against BVH2 and CPU against RT as pf / strict differences / max abs; BVH2
against RT as pf / max abs.

| Scene | Metal repeat max | CPU vs BVH2 | CPU vs RT | BVH2 vs RT |
| --- | --- | --- | --- | --- |
| `surface-nodes/integer-math` | 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 |
| `surface-nodes/boolean-math` | 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 |
| `surface-nodes/get-vector-component` | 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 |
| `surface-nodes/get-vector-component-bump` | 6e-8 / 6e-8 | 0 / 471 / 2.9e-6 | 0 / 473 / 2.8e-6 | 0 / 1.8e-7 |
| `surface-nodes/image-missing` | 4.8e-7 / 2.4e-7 | 0 / 580 / 1.9e-5 | 0 / 598 / 2.1e-5 | 0 / 7.4e-6 |
| `bsdf/glass-multiscatter` | 6e-8 / 6e-8 | 0 / 545 / 1.1e-5 | 0 / 555 / 1.4e-5 | 0 / 3.9e-6 |
| `points/geometry-tangent` | 6e-8 / 6e-8 | 5 / 2337 / 2.4e-4 | 5 / 2352 / 2.1e-4 | 0 / 6.3e-5 |
| `points/geometry-tangent-bump` | 6e-8 / 1.2e-7 | 10 / 2164 / 1.7e-3 | 10 / 2189 / 1.7e-3 | 0 / 2.6e-5 |
| `lamps/area-degenerate-spread-zero` | 6.1e-5 / 6.1e-5 | 0 / 550 / 6.6e-3 | 0 / 567 / 4.1e-3 | 0 / 2.5e-3 |
| `lamps/area-degenerate-ellipse-spread-zero` | 6.1e-5 / 6.1e-5 | 0 / 559 / 6.6e-3 | 0 / 569 / 4.1e-3 | 0 / 2.5e-3 |
| `lamps/area-degenerate-spread-tiny` | 6e-8 / 8.9e-8 | 0 / 552 / 1.4e-5 | 0 / 548 / 5.9e-6 | 0 / 1.1e-5 |
| `lamps/area-degenerate-spread-tiny-volume` | 4.8e-7 / 1.9e-6 | 707 / 2168 / 0.16 | 743 / 2169 / 0.16 | 88 / 0.022 |
| `lamps/area-degenerate-infinite-axis` | 1.2e-4 / 1.2e-4 | 879 / 1037 / 1.1e3 | 879 / 1034 / 1.1e3 | 0 / 4.9e-3 |
| `lamps/area-degenerate-tree-transmissive` | 1.2e-7 / 1.2e-7 | 138 / 578 / 1.4 | 138 / 580 / 1.4 | 0 / 1.3e-5 |
| `world/portal-zero-u` | 2.4e-7 / 1.2e-7 | 0 / 675 / 2.4e-5 | 0 / 689 / 3.6e-5 | 0 / 1.3e-5 |
| `world/portal-zero` | 3e-8 / 3e-8 | 0 / 690 / 2.4e-5 | 0 / 691 / 3.6e-5 | 0 / 1.3e-5 |
| `world/portal-underflow` | 2.4e-7 / 6e-8 | 0 / 686 / 2.4e-5 | 0 / 684 / 3.6e-5 | 0 / 1.3e-5 |
| `bake/zero-area-combined` | 3e-8 / 1.2e-7 | 99 / 1035 / 0.068 | 99 / 1034 / 0.068 | 0 / 1.3e-5 |
| `bake/zero-area-emission` | 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 |
| `bake/zero-area-ao` | 0 / 0 | 183 / 183 / 1 | 186 / 186 / 1 | 48 / 0.19 |
| `bake/zero-area-diffuse-indirect` | 8.9e-8 / 1.2e-7 | 100 / 2067 / 0.13 | 100 / 2068 / 0.13 | 0 / 1.7e-5 |
| `bake/zero-area-diffuse-color` | 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 |
| `bake/zero-area-glossy-indirect` | 3e-8 / 3e-8 | 68 / 2037 / 0.17 | 68 / 2037 / 0.17 | 0 / 2.8e-5 |
| `bake/zero-area-glossy-color` | 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 |
| `bake/zero-area-transmission-indirect` | 4.5e-8 / 8.9e-8 | 94 / 2043 / 0.089 | 94 / 2040 / 0.089 | 0 / 1.2e-6 |
| `bake/zero-area-transmission-color` | 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 |
| `bake/zero-area-roughness` | 0 / 0 | 0 / 0 / 1.2e-7 | 0 / 0 / 1.2e-7 | 0 / 0 |
| `bake/zero-area-uv` | 3e-7 / 1.8e-7 | 0 / 1 / 1.8e-7 | 0 / 1 / 2.4e-7 | 0 / 3e-7 |
| `bake/zero-area-object-id` | 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 |
| `bake/zero-area-material-id` | 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 |
| `bake/zero-area-depth` | 0 / 0 | 0 / 0 / 4.8e-7 | 0 / 0 / 4.8e-7 | 0 / 0 |
| `bake/zero-area-mist` | 2.4e-7 / 2.4e-7 | 0 / 128 / 1.8e-7 | 0 / 119 / 2.4e-7 | 0 / 2.4e-7 |
| `bake/zero-area-aov-color` | 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 |
| `bake/zero-area-aov-value` | 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 |

**Row 2: Integer Math, Boolean Math, Get Vector Component with linked inputs.**
- The committed tables (`author_surface_nodes.py`): one 4x4-pixel cell per case under an orthographic camera that maps
  one world unit to one pixel, with a box filter of width 1. Each operand is the Integer Math sum of two face attributes
  that binary32 holds exactly; an integer result leaves through the AOVs `D0`..`D3` (base-256 digits) beside `R` and
  `CELL`. Only cases whose pinned evaluation is defined are committed (the script's `pinned_integer_math` checks each).
  Decoded (`compare/row02-integer-math.txt`, `-boolean-math`, `-get-vector-component`): Integer Math 136/136, Boolean
  Math 63/63, Get Vector Component 21/21 cells uniform and equal to the pinned value on all three backends, repeats
  identical, films and passes bitwise equal across the three backends. The `-0.0` vector component reads back as `+0.0`
  on every backend (film accumulation starts at +0).
- Scratch, undefined behaviour (signed overflow, `INT_MIN / -1`, `divide_round` by `INT_MIN`; the lead's ruling keeps
  them out of the corpus), decoded in `compare/row02-integer-math-traps.txt` and `-wraps.txt`, `runs/phase2-scratch`:
  - `survey/integer-math-traps` (8 cells; the cases that trap in an x86-64 `idiv`, **INFERENCE**): no crash on any
    backend. CPU: `INT_MIN / -1` gives INT_MIN, `INT_MIN % -1` 0, and `divide_round` of 5, INT_MIN and -5 by INT_MIN gives
    0, 0, 0. Metal (BVH2 and RT): the same, except `divide_round` gives -1, 1, 1.
  - `survey/integer-math-wraps` (38 cells of signed overflow): Metal 38/38 equal the two's-complement result. The CPU
    differs in 3: `gcd(INT_MIN, 6)` = 2 (wrap -2), `divide_round(2^30, 3)` = 357913941 (wrap -357913940; the true value is
    357913941), `divide_round(INT_MAX, 1)` = 2147483647 (wrap 0).

**Row 3: Glass BSDF with multiscatter GGX.** `bsdf/glass-multiscatter` (table above). For the WebGPU device (no handler
and no named refusal for this closure), the committed scene will show on Linux what the WGSL path does with it.

**Row 5.** `surface-nodes/image-missing` (Image Texture nodes with no image, constant and linked Missing inputs; an
Attribute node asking for an absent name; an Environment Texture world with no image) is accepted and renders;
`points/geometry-tangent` and `-bump` render (table above).

**Row 6: Both displacement on curves and points** (scratch `survey/curves-thick-diffuse-both`,
`survey/points-diffuse-both`; R1 and R1b, two processes; `compare/row06-*.json`). Accepted by the host at this commit.
- CPU: not repeat-identical in either process (4 distinct film hashes in 4 films per scene; repeat max 0.0067 curves,
  0.0233 / 0.0119 points). CPU R1 against CPU R1b: curves 0 pf (5 strict, max 3.4e-5), points 28 pf (max 0.015).
- BVH2: repeat max 0.148 / 0.138 (curves R1 / R1b), 0.388 / 0.271 (points); R1 against R1b 873 pf (max 0.131) curves,
  964 pf (max 0.249) points. RT: repeat max 0.147 / 0.150, 0.249 / 0.205; R1 against R1b 487 pf (max 0.113), 620 pf
  (max 0.173). CPU against Metal: 994 to 1247 pf (curves), 1643 to 1885 pf (points).
- So the two Metal films of each backend do not agree with each other, bitwise or within Metal's repeat noise: the
  largest BVH2 repeat difference of Part A's whole corpus is 1.2e-4 (RT 0.072). No NaN/Inf. This is the evidence for
  ruling 15.

**Row 7: degenerate area lights, zero-extent portals.** All five sub-cases the WebGPU device refuses by name render on
CPU and Metal with the base build (no survey patch was needed): spread 0 (rectangle and ellipse, with the Glossy Direct
pass), spread 1e-6 (also in a volume), axes of infinities without a NaN, the light tree at a transmissive point; and the
three zero-extent portals (table above). Every CPU film is repeat-identical, so the scenes are committed. Part A's
committed degenerate scenes for comparison: `lamps/area-degenerate-rect` 960 pf (max 1.1e3), `-volume` 1317, `-tree` 138,
the others 0.

**Row 8a: zero-area texels.** The pole sphere of `bake/zero-area-smooth-normal`: 17 shading types render on every backend
(committed, table above). Seven fail on every backend with `codebrix_cycles_oracle: cbc_host_bake returned 3: Nonfinite
bake output` (host, `native/host/host_bake.inc:240`): the flat normal (`survey/bake-zero-area-flat-normal`, R1) and
diffuse, diffuse_direct, glossy, glossy_direct, transmission and transmission_direct (`survey/bake-zero-area-<type>`,
`runs/phase2-scratch`).

**Row 8b: subdivision bake** (`survey/bake-subdivision`: the linear cage of `subdivision/linear-object-attribute` with a
corner UV layout). Base build (R1), every backend: `codebrix_cycles_oracle: cbc_host_bake (size) returned 2: A subdivision
surface cannot be baked` (host, `host_bake.inc:60`). With the refusal lifted (R2), every backend:
`codebrix_cycles_oracle: cbc_host_bake (size) returned 1: The bake mesh has no triangles` (host, `host_bake.inc:61`): a
lifted refusal reaches the next host check, not a Cycles bake.

**Row 9: the denoiser upscale factor** (documents from `denoise/beckmann-on`, 32x24, and
`denoise/catcher-point-light-on-noisy`, 64x48; CPU with the refusal lifted in R2, Metal with the base build in R1;
`compare/row09-*.json`, film hashes and cross-document numbers in `compare/summary.json`).
- CPU, base build: every factor other than 1 refused with the text in the table.
- CPU, refusal lifted: factors 2, 4, 1.5 and factor 2 with `denoise_use_gpu` false all give a film bitwise identical to
  factor 1 (`5c4d2aa3...`) and to Part A's `denoise/beckmann-on` CPU film. The shadow catcher at factor 2 equals factor 1
  bitwise.
- Metal with `denoise_use_gpu` true: factor 1 gives `31122c40...` on BVH2 and RT alike, repeat-identical (GPU Open Image
  Denoise: 1501 pf from Part A's Metal `denoise/beckmann-on`, which uses the CPU denoiser). Factors 2, 4 and 1.5 are
  accepted and render without an error, but the 32x24 film holds the low-resolution image packed into its first pixels and
  RGBA 0 after them: 192 pixels (16x12) for factor 2, 48 (8x6) for 4, 336 (21x16) for 1.5. The packed pixels correlate
  0.91, 0.83 and 0.98 with the factor-1 image resampled to that size. The noisy combined pass is packed the same way
  (`survey/denoise-upscale-2-gpu-noisy`, `runs/phase2-scratch`), and the denoised film differs from it, so denoising ran.
  **INFERENCE:** the render ran at the reduced resolution and the host read the reduced-size buffer without upscaling.
- Metal with `denoise_use_gpu` false, factor 2: a full film, 0 pf from Part A's Metal `denoise/beckmann-on`: the factor
  falls back to 1. Shadow catcher at factor 2: bitwise equal to factor 1 (`dbf7a58c...`): the release forces 1.
- This is the evidence for ruling 14.

**Row 10: section 9 of the testing document at the current pin** (`row10-metal-comparison/`, base build).
- Every frozen document of `native/evidence/metal-comparison-linux-x64-20261031/scenes/` and `bsdf/velvet` rendered on the
  three backends (the Radial Tiling refusal is the WebGPU device's). CPU repeat-identical, no NaN/Inf. CPU against BVH2 /
  RT: `radial-sides2-r0` 0 / 0 pf (max 5.2e-6), `-r0.3` 695 / 695 (max 1.765, max relative 1), `-r0.5` 704 / 704
  (1.762), `-r0.75` 716 / 716 (1.758), `-r1` 0 / 0 (1.1e-5), `radial-sides3-r0.5` 0 / 0 (6.2e-6), `velvet-subnormal`
  0 / 0 (1.9e-6), `bsdf/velvet` 0 / 0 (2.6e-6); BVH2 against RT 0 pf in every scene.
- `bsdf/velvet` against Part B (`compare/row10-bsdf__velvet--partB-cpu-arm64-linux-vs-R10.json`, information): the
  Linux ARM64 CPU film (`native/evidence/linux-arm64-cpu-oracle-20261113/`, the same document) against the Mac CPU film:
  not bitwise equal, 0 pf, 0 strict differences, max 3.0e-8; against BVH2 and RT 0 pf (360 / 371 strict, max 2.6e-6).
  The Mac CPU film is bitwise identical to Part A's.
- **Not made (kit rule 8):** the comparisons of steps 2 and 3 with `cpu-x64-linux/`, `cpu-x64-linux-ftz/` and
  `wgsl-linux-intel/` of the frozen folder (previous revision `a456b761...`). The frozen documents and probe sources were
  read only. This folder replaces step 4's separate evidence folder (lead ruling: one survey folder).
- **The Metal compute probe** (`probes/`; source copied from `native/evidence/cycles-oracles-macos-metal-comparison-20261031/probes/`,
  built with `xcrun --sdk macosx clang -fobjc-arc -O2 metal_velvet_probe.m -framework Metal -framework Foundation`;
  binaries not copied, SHA-256 in `probes/environment.txt`). With `fastMathEnabled = YES` (reported as
  `MTLMathModeFast`, `MTLMathFloatingPointFunctionsFast`, language version 3.2) and with NO (`MTLMathModeSafe`,
  `Precise`): `1e-39 * 1`, `1e-39 + 0` and `FLT_MIN * 0.5` give 0 and `1e-39 != 0` is false, while a plain copy keeps the
  bits: **Metal flushes subnormal inputs and results in both modes.** The velvet chain at sigma 0.3 (the inputs of
  `velvet_chain.c`): the `expf` result is normal in both modes (1.37307603e-38 fast, 1.37305417e-38 safe) and the BSDF
  value is 9.97140401e-37 (fast) and 9.97124796e-37 (safe), not 0; `fast::exp(-87.1811905)` = 1.37306146e-38. The
  products that are subnormal on the CPU (gate paths 534, 1566, 2055) are 0 on Metal. The probe output equals the
  2026-10-31 output apart from the macOS version line (probe output, not a film). The CPU (`velvet_chain.c`,
  `-O2 -ffp-contract=off -fno-reciprocal-math`): sigma 0.3 `expf` 1.37306174e-38, BSDF 9.97129997e-37, the same with
  `fz_arm64.c` (FPCR.FZ set); with Apple Clang's default contraction 9.97122733e-37.
- **What pinned Cycles passes** (`device/metal/device_impl.mm:557-575`): `options.fastMathEnabled = YES`;
  `languageVersion` `MTLLanguageVersion3_0`, 3_1 on macOS 14 and later, **3_2 on macOS 15 and later** (the deployment
  target is 15.0); `enableLogging` only with `MTL_LOG_LEVEL=MTLLogLevelDebug`. Independently, `kernel/device/metal/compat.h:286-292`
  maps `expf`, `sqrtf`, `logf`, `sinf`, `cosf` and `tanf` to `metal::fast::`, and `powf` to `metal::pow` (:264).

**Row 11: BVH time steps** (R2 unset, R3 `=1`, R4 `=3`; `compare/row11-*.json`).
- CPU: for all 11 scenes (`curves-b/motion-deform-thick`, `-ribbon`, `-linear`, `motion-radius-thick`, `shadow-motion`,
  `points/deform-3`, `-5`, `deform-transparent`, `linux-gates/deforming-motion-0`, `light-motion/camera-deforming-motion`,
  `points/object-motion`) the films at steps 1 and 3 are bitwise identical to steps 0.
- BVH2 and RT: steps N against steps 0, 0 practical and 0 strict differences in every scene, max 1.2e-7, the size of
  Metal's repeat differences in each run.
- The setting reaches Cycles (D11): the host reports `num_bvh_time_steps` 0, 1, 3, and the BVH2 statistics change on the
  CPU and on BVH2 alike, "Total number of nodes" 33 / 67 / 193 (`curves-b/motion-deform-thick`), 31 / 55 / 151
  (`points/deform-3`), 5 / 5 / 9 (`linux-gates/deforming-motion-0`, motion triangles), unchanged 13 and 7
  (`points/object-motion`, no motion primitives). RT logs "Using METAL layout" and no BVH2 statistics. The D11 CPU films
  equal R2/R3/R4 bitwise.

**Row 12: Gaussian splats** (`survey/points-gaussian-splats`, R1): the text in the table on every backend, from
`native/host/host_scene.inc:110-111`; the quoted text after the runner prefix is the source literal verbatim. It has no
backend condition in the shared host, so it reads the same on Linux by construction; no v5.3.0 Linux record of it exists
(rule 8). The managed test (`SceneSettingsTests.cs:274`) checks only "Gaussian splats".

**Row 13: quaternion and spherical-harmonics attributes.** Nothing to render: `CBC_VALUE_*` ends at `BYTE4 = 17`
(`native/host/cycles_host.h:32-37`); the runner's value names are float, color, vector, point, normal, point2, transform,
float4 and byte4 (`runner.cpp:170-172`); the pinned kinds are `AttrKernelDataType::QUATERNION` and
`SPHERICAL_HARMONICS_REST` (`scene/attribute.cpp:624,628`). Left to Part D.

**Row 14: dlss and `denoiser_passes`** (`survey/denoise-dlss`, `-passes-128`, `-passes-albedo-normal-256` with 261; R1):
the texts in the table on every backend, from `host_scene.inc:114-115` (CBC_UNSUPPORTED) and `:120-121` (CBC_INVALID),
verbatim; managed tests `SceneSettingsTests.cs:250` ("dlss") and `:633` ("denoiser_passes") check substrings. The same on
Linux by construction (one literal, shared host); no v5.3.0 Linux record exists.

### 4.4 Row 15: what Cycles' Metal device supports, and where the host stops it (reading only)

| Feature | Cycles (pinned) | Metal device | CodeBrix host |
| --- | --- | --- | --- |
| Tiled rendering | `Session::get_effective_tile_size` (`session/session.cpp:499-522`) with `use_auto_tile`; tiles go through temporary EXR files written and read by OIIO (`session/tile.cpp:445-470, 632`) | has it (session-level, generic) | off (`params.use_auto_tile = false`, `cycles_host.cpp:608`). OIIO is built without format plugins (`tools/dependencies/build_macos.py:80-83, 112-120, 162-163`); **INFERENCE:** `ImageOutput::create("...exr")` returns null, so it needs image-file writing |
| UDIM tiles | the multi-loader `ImageManager::add_image` (`scene/image.cpp:333-347`), `kernel_image_interp_with_udim` (`kernel/device/gpu/image.h:168-180`) | has it (the generic GPU image path, `kernel.metal:15`) | not exposed: one `MemoryImageLoader` per node (`cycles_host.cpp:272-278`), no `get_tile_number`, no ABI export |
| Progressive results | `PathTrace::update_display` -> `OutputDriver::update_render_tile` (`integrator/path_trace.cpp:779-801`) | has it | not exposed: `HostOutput` overrides only `write_render_tile` and `read_render_tile` (`cycles_host.cpp:134-170`) |
| Texture limit | `SceneParams::texture_limit` (`scene/scene.h:82,105`), applied at image load (`scene/image.cpp:505-512`, `scene/image_cache.cpp:218-260`) | has it; `MetalDevice::image_alloc` refuses 2D textures above 16384 x 16384 (`device_impl.mm:1093-1101`) while the capability report claims `maxImageDimension` INT_MAX (`host_api.inc:196`) | not exposed (`cycles_host.cpp:611-614`) |
| Texture cache | only with `use_texture_cache`, `has_tiles_and_mipmaps` and `tile_size` from `.tx` files (`scene/image.cpp:493-503`, `scene/image_oiio.cpp:39-70`) | the generic GPU path (**INFERENCE:** no Metal override) | off (`use_texture_cache = false`, `:614`) and unreachable (`MemoryImageLoader` never sets that metadata; `CBC_HOST_NO_IMAGE_FILES`) |
| BVH spatial split | `use_bvh_spatial_split` (default false) -> the BVH2 builder (off for motion-blurred geometry, `bvh/build.cpp:447-456`) or Embree | BVH2: yes (`Device::build_bvh`, `device_impl.mm:1308-1311`); MetalRT: no (`device/metal/bvh.mm` never reads it) | not exposed |
| BVH time steps | `num_bvh_time_steps` (row 11; `scene/geometry_bvh.cpp:80-82, 110-112, 184`) | BVH2: yes; MetalRT: no (Metal motion keyframes; no `prim_time` in `kernel/device/metal/`) | not exposed (survey patch only); row 11 shows no film effect |

For Part D item 3: UDIM tiles, progressive results, the texture limit, spatial split (BVH2 only) and time steps (BVH2
only) are supported by Cycles-with-Metal; tiled rendering and the texture cache are supported by Cycles but need image-file
writing or reading, which this profile excludes.

## 5. Part D: host work

Items 1 (D1) and 2 (D2) are below, and the gaps of item 3 (D3) done so far in section 5.3. The rest of item 3 is in
progress (not part of this folder yet).

### 5.1 D1: quaternion and spherical-harmonics attributes (2026-11-14)

**What was done.** Two new value kinds on the existing attribute calls (`cbc_host_attribute_floats`,
`cbc_host_attribute_get_floats`); no new C export, so ABI 1 keeps its 60 exports (the build records `exports: 60`). Made at
`f2586fc17028b62b6e4702a59a401b8d3f230492` with the D1 changes uncommitted; every run below records
`repoWorkingTreeClean: false` for that reason.

- `native/host/cycles_host.h`: `CBC_VALUE_QUATERNION = 18` (4 floats per element, w, x, y, z, Cycles' `Quaternion`
  order) and `CBC_VALUE_SPHERICAL_HARMONICS_REST = 19` (45 floats per element: the 15 RGB coefficients of bands 1 to 3 in
  Cycles' order L1 M-1, L1 M0, L1 M1, L2 M-2, ..., L3 M3), both marked PROVISIONAL.
- `native/host/host_scene.inc`: the kinds map to Cycles' `TypeQuaternion` and `TypePackedSphericalHarmonicsRest`; the
  host writes them with `make_quaternion` and Cycles' own `spherical_harmonics_rest_set_coefficient`, and reads them back
  with `spherical_harmonics_rest_get_coefficient` (the stored value, as packed normals). Accepted on meshes (mesh, face,
  vertex and corner domains) and point clouds (mesh and vertex domains); on hair and volume geometry `CBC_UNSUPPORTED`
  ("quaternion and spherical-harmonics attributes are accepted on meshes and point clouds only"). A spherical-harmonics
  value outside [-1, 1] is `CBC_INVALID` ("Spherical-harmonics coefficients must lie in [-1, 1] ..."), never clipped. A
  spherical-harmonics subdivision attribute is `CBC_UNSUPPORTED`: `subd/interpolation.cpp` has no case for that type, so
  the diced mesh would keep values nobody set; a quaternion subdivision attribute is accepted (Cycles has
  `SubdQuaternion`). Nothing is adapted from `util/math_float3x3.h`; no rotation math is called, so the Ceres notice rule
  does not apply.
- What an Attribute node returns is Cycles' own reading (`kernel/svm/attribute.h`): for a quaternion, Color and Vector
  are w, x, y, Fac their average and Alpha z, interpolated component by component; a spherical-harmonics attribute is
  found and returns black with Alpha 1 (the pinned kernels have no shader reading of it), which the new scenes show
  against a missing one.
- The WebGPU refusal is the device's own, unchanged: `scene_tables()` in `native/cycles/closest.cpp` refuses non-empty
  `attributes_quaternion` / `attributes_spherical_harmonics_rest` with "WebGPU quaternion attributes not implemented" /
  "WebGPU spherical harmonics attributes not implemented" (`CBC_FAILED`). Cycles uploads a custom attribute only when a
  used shader of its geometry requests it (`scene/geometry_attributes.cpp`, the request sets), so an attribute that no
  shader reads is not refused. No file under `native/cycles` changed. Rule 7: the capability report's WebGPU list
  (`native/host/host_api.inc`), `AGENT-README.txt` "RENDER-TIME REJECTIONS" (first entry of "The WebGPU backend:") and
  `native/host/ABI.md` carry "quaternion and spherical-harmonics attributes read by a shader".
- Generated model and loaders: `tools/managed/generate_model.py` `VALUE_KIND` gains 18 and 19; `python3
  tools/managed/generate_model.py` regenerates nothing (no registry socket has either kind; 187 node types, 188 files,
  `--check` up to date). The oracle runner (`native/oracle/runner.cpp`) and the managed loader
  (`Bundles/OracleSceneBundle.cs`) take the attribute value names `quaternion` and `spherical_harmonics_rest`.
- Managed: `ValueKind.Quaternion` / `.SphericalHarmonicsRest` and `AttributeValue.Quaternion` / `.SphericalHarmonicsRest`
  (18, 19), documented; `SetAttribute` and `GetAttribute` carry them unchanged, so no method was added.
- Documents: `native/host/ABI.md` (the two kinds, the PROVISIONAL marks and their meaning, `CBC_VALUE_*` 0..19, the point
  cloud and splat wording), `AGENT-README.txt` (the kinds under "Geometry attributes", `ValueKind`, the rejection list and
  its "Why."), `native/DEVICE.md` item 186, `docs/PARITY.md`, `native/oracle/README.md`.

**Provisional signatures** (each the maintainer's to approve):

| Signature | Reason | Alternative considered |
| --- | --- | --- |
| `CBC_VALUE_QUATERNION = 18`; `ValueKind.Quaternion`, `AttributeValue.Quaternion` | Cycles' storage type and order (`util/types_quaternion.h`), so readback and the Attribute node follow the stored components without a swizzle | x, y, z, w order (other formats): rejected, a second convention; a dedicated export: rejected, a larger surface that duplicates the attribute exports |
| `CBC_VALUE_SPHERICAL_HARMONICS_REST = 19`; `ValueKind.SphericalHarmonicsRest`, `AttributeValue.SphericalHarmonicsRest` | 45 floats through the float exports, quantized by Cycles' own function, read back as stored (the packed-normal pattern); "REST" says band 0 is not part of it, as in Cycles' `PackedSphericalHarmonicsRest` | 45 raw bytes through `cbc_host_attribute_bytes`: rejected, it makes Cycles' 8-bit encoding part of the ABI and the byte path belongs to `CBC_ATTR_CORNER_BYTE`; the name `..._SPHERICAL_HARMONICS`: rejected, misleading |
| Range rule: a spherical-harmonics value outside [-1, 1] is `CBC_INVALID` (lead ruling D1) | Cycles clips it (`quantize_spherical_harmonics`), which would be an approximation (kit rule 5) | clip as Cycles does |
| Scope: meshes and point clouds only (`CBC_UNSUPPORTED` on hair and volumes); no spherical-harmonics subdivision attribute | the lead ruling's scope; Cycles does not interpolate the second over subdivided faces | every float domain the host has, curves included (the kernels read curve elements generically); spherical harmonics on point-cloud vertices only, their only consumer |
| Scene-document value names `quaternion` and `spherical_harmonics_rest` (runner and managed loader) | the C constant names in lower case, as for the existing names | none smaller |

**Tests added** (managed total 348, was 332):

- `GeometryAttributeTests` (new, CPU, 9 cases): quaternion round trip bit for bit on the mesh, face, vertex and corner
  domains, on the vertex and mesh domains of points and as a subdivision attribute; spherical-harmonics round trip on the
  four mesh domains and on points, equal to Cycles' quantize-unquantize computed in binary32 and within 1/255 of the value
  set (-1, 0 and 1 exact); 1.0001 and -1.5 refused by name with nothing stored, NaN refused; hair, the spherical-harmonics
  subdivision attribute, short arrays, a normal domain and a standard name refused; an Attribute node reading a
  quaternion on a mesh and on points (Color and Vector w, x, y, Fac the average, Alpha z, exactly, 1 sample of emission);
  spherical harmonics found (black, Alpha 1) and missing (the Missing inputs) on a mesh and on points; both kinds set
  after a render read back and render bit for bit like a scene built with them.
- `NativeHeaderConstantMatching`: 4 rows. `CapabilityReportTests`: the new entry is in the README's WebGPU group only and
  in no compiled backend's scope but `webgpu`. `OracleSceneBundleTests`: the committed
  `attributes/quaternion-and-spherical-harmonics` document loads through the managed loader, its quaternions read back
  exactly and its coefficients within 1/255, and it renders repeat-identically.
- `GpuRenderTests.quaternion_and_spherical_harmonics_attributes_render_on_metal_and_are_refused_by_name_on_webgpu`: on
  Metal judged by the reference policy against the new recorded film
  `References/metal-osx-arm64/attribute-quaternion-spherical-harmonics.rgba.f32` (recorded with only this test selected;
  its CPU comparison: max 4.86e-6, 0 practical failures, 12 strict differences; the manifest gained only this entry); on
  WebGPU it expects the two device messages in turn and a render once neither attribute is read.
- Host gate: none. The macOS host gate has no pattern for attribute kinds (its consumers set only a `uv` attribute);
  the attribute-kind checks are in `native/tests/host/abi_consumer.c`, which only the Linux gate builds.

**Oracle scenes.** A new family `native/oracle/scenes/attributes/` written by `author_attributes.py` (`--check` passes
for all five). A new family because the two kinds span meshes and points while every existing family is one geometry or
one feature, and a new script leaves every existing author script and committed document as it is; it reuses the points
family's builders and room (`author_points.py` on `author_sampling.py`), so its documents read like `points/attributes`.
`quaternion-mesh`, `quaternion-points`, `spherical-harmonics-mesh`,
`spherical-harmonics-points` and `quaternion-and-spherical-harmonics` (64x48, 8 samples). Run
`<repo>/native/work/oracles/macos-arm64-d1-20261114/` (scratch, not in this folder), `starter/diffuse-triangle` first:
0 failures, every CPU film repeat-identical (bitwise), every comparison reported:

| Scene | BVH2 vs CPU | RT vs CPU | RT vs BVH2 |
| --- | --- | --- | --- |
| `starter/diffuse-triangle` | 0 pf, max 3e-08 | 0 pf, max 3e-08 | 0 pf, max 1.5e-08 |
| `attributes/quaternion-mesh` | 0 pf, max 9.1e-06 | 0 pf, max 5.4e-06 | 0 pf, max 1e-05 |
| `attributes/quaternion-points` | 9 pf, max 0.005 | 8 pf, max 0.005 | 0 pf, max 8.3e-05 |
| `attributes/spherical-harmonics-mesh` | 0 pf, max 3.3e-06 | 0 pf, max 3.3e-06 | 0 pf, max 2.1e-07 |
| `attributes/spherical-harmonics-points` | 6 pf, max 0.0029 | 3 pf, max 0.0029 | 3 pf, max 0.0025 |
| `attributes/quaternion-and-spherical-harmonics` | 0 pf, max 4e-05 | 0 pf, max 2.2e-05 | 0 pf, max 2.8e-05 |

"pf" as in section 4.1. **INFERENCE:** the point scenes' differences are those of the points family on Metal at this pin
(39 of its 57 scenes have practical failures against the CPU in Part A's run), not of the attribute kinds.

**Rule 9 at D1.** The whole committed corpus (763 documents: the 758 of the base run and the survey plus the 5 new
ones) and the 2 bench documents of `<repo>/native/work/oracle-assets/bench_01/`, rendered on `cpu-arm64-macos` alone into
`<repo>/native/work/oracles/macos-arm64-d1-corpus-20261114/` (765 scenes, 0 failures, every film repeat-identical; 126 s).
Compared by scene path and film index: all 1,452 films of the 726 scenes of the base run
(`native/evidence/cycles-oracles-macos-20261113/`) and all 68 films of the 34 survey scenes (`runs/phase2/` of this folder)
are bitwise identical; nothing moved. The 5 new scenes' films equal those of the run above. Host library of both runs
`f544532f9d9f9d9634e4d4d10b2e5c07771c8a53a7e437e074bf63eb8978eef9`, runner
`af4c3885cae295ecb00fdfdad4d041451f0a776f17cea4bca63d7d649f1a1be3`. A code comment in `native/host/host_scene.inc` was
reworded afterwards; the rebuilt library (`4df6485fefc2dc333fc7d0e3d0401bd9da6c3f094c11301931b6ae24e1d292eb`) has every
Mach-O section and table identical to it (`survey-build/macho_sections.py`: 21 identical, none different, 132 debug-map
entries with the same paths), and the host gate and the managed tests below ran on the rebuilt library.

**Build, gate and managed tests.** `python3 tools/build_macos.py --jobs 8 --skip-dependencies`: passed, 0 warnings in
the host build log and the oracle runner build (20 s, incremental). macOS host gate
(`<repo>/native/work/macos-validation-20261114b/`): passed (3,089 films compared, 0 same-host bitwise failures, Metal 0
practical failures, maximum 4.5e-8; 16 s). Testing document step 7 with .NET SDK 11.0.100 (restore; Release build, test,
pack; Debug build, test): exit 0 throughout; Release 348 tests, 342 passed, 6 skipped, 0 failed; Debug the same. The six
skips are the WebGPU-only tests (the WebGPU AO pass, the WebGPU bake, WebGPU dispersion and the three WebGPU refusals of
the integer and component nodes). The Release build's one warning is the pack driver's, unchanged since Part A
("linux-x64: payload is STALE"); the Debug build has 0 warnings. The pack folder the run wrote was deleted.
`python3 tools/licensing/audit.py`: passed. `git diff --check`: clean.

**Unverified on other platforms.** Everything WebGPU: the device refusals above were read, never run here (this profile
has no WebGPU device); the capability report's WebGPU list is compiled only with `WITH_WEBGPU`, so
`CapabilityReportTests`' word-for-word check of that list and the WebGPU branch of the GPU test run only on a WGSL build.
The Linux x64 build, host gate and CPU oracle (the x64 films of the five new scenes, the reference for any WGSL judgement),
the Linux ARM64 CPU oracle of the new scenes, and the Windows and Android builds. No gate script changed: the export count
and the registry counts did not move.

**What was left.** The standard splat attribute names (`radiance:base`, `scale`, `rotation`, `radiance:sh`) and splat
rendering are D2; the WebGPU port of the two attribute tables is a device item (`native/DEVICE.md` 186). No host-gate case
was added (above). The MacOSArm64 package consumer test of the testing document's step 7 was not repeated (no packaging
input changed).

### 5.2 D2: Gaussian splats on the CPU and Metal devices (2026-11-14)

**What was done.** The host accepts a point cloud whose `render_as` is `gaussian_splats` on the CPU and Metal backends and
refuses it on the WebGPU backend by that device's words. No new C export (ABI 1 keeps its 60 exports; the build records
`exports: 60`), no new value kind and no new managed member. Made at `f2586fc17028b62b6e4702a59a401b8d3f230492` on top of
the uncommitted D1 changes; every run below records `repoWorkingTreeClean: false` for that reason.

- Standard names (`native/host/host_scene.inc`, `standard_attribute`): on point clouds only, `radiance:base`, `scale`,
  `rotation` and `radiance:sh`, mapped to Cycles' `ATTR_STD_GSPLAT_RADIANCE_BASE`, `_SCALE`, `_ROTATION` and
  `_RADIANCE_SPHERICAL_HARMONICS_REST`. Cycles finds splat data by these standards alone (`PointCloud::primitive_type`, the
  forced upload in `scene/geometry_attributes.cpp`, the offsets of `KernelObject.gsplat` in `scene/object.cpp`). The
  existing check of `stage_attribute` requires the standard's own type and element, so the value kinds are `CBC_VALUE_FLOAT4`,
  `CBC_VALUE_VECTOR`, the D1 `CBC_VALUE_QUATERNION` and `CBC_VALUE_SPHERICAL_HARMONICS_REST`, on `CBC_ATTR_VERTEX`; anything
  else is `CBC_INVALID`, and the names on other geometry are `CBC_UNSUPPORTED` as before. `radiance` is not a standard name
  (lead ruling).
- Render-time checks (`validate_gsplats`, called from `validate_geometry` on the CPU and Metal backends): a splat cloud
  without the standard `radiance:base`, `scale` or `rotation` is `CBC_INVALID`, "Gaussian splats need the standard point
  attributes radiance:base, scale and rotation, set with CBC_ATTR_STANDARD (missing: scale)" (every missing name, in that
  order). The pinned Cycles would draw plain points with no message (`scene/pointcloud.cpp`, `primitive_type`), and
  `create_missing_gsplat_attributes` has no caller in the pinned tree (`git grep` of `third_party/cycles`: its definition and
  declaration only), so the host supplies no defaults. A rotation is `CBC_INVALID` when no component's square is a normal
  binary32 number ("Gaussian splat rotation of point N has zero length ...") or when its squared length overflows binary32
  ("... is too long ..."): `quaternion_to_rotation` (`util/math_float3x3.h`) and `gsplat_normal` (`kernel/geom/gsplat.h`)
  divide by `w*w + x*x + y*y + z*z`. The check is done in double from the stored components; it adapts nothing from the
  Ceres-derived functions (no rotation math is called), so the BSD notice rule does not apply.
- WebGPU (`validate_unavailable`): the all-backend refusal ("Unsupported feature: Gaussian splats are not available in this
  library ...") is now a refusal of `CBC_HOST_WEBGPU` only, `CBC_UNSUPPORTED`, "WebGPU Gaussian splats not implemented",
  the device's own text (`scene_tables` in `native/cycles/closest.cpp`, which refuses `KERNEL_FEATURE_GSPLATS`). The host
  refuses whatever attributes the cloud has; the device refuses only a cloud that Cycles makes a splat cloud. No file
  under `native/cycles` changed.
- Radius (`native/host/host_edits.inc`, `keep_splat_radii` / `restore_splat_radius`; `Host::splat_radii` in
  `cycles_host.cpp`): Cycles replaces a splat cloud's radius, centre and motion steps, at each device update of the cloud
  (`PointCloud::update_gsplat_radii`, `scene/geometry.cpp`), and its BVH (BVH2, Embree's `gsplat_bounds_func`, MetalRT's
  AABBs) bounds the splats with that radius from then on. Before a render or bake the host keeps a copy of the authored
  radius (every step) of each splat cloud it does not hold yet; `cbc_host_attribute_get_*` answer from it
  (`authored_attribute`); the next edit of the cloud (`before_edit`, which `cbc_host_point_motion`, the socket setters and
  the attribute calls run first) writes it back, tags the radius modified and drops the copy, so the edit applies to
  authored data and the next device update computes Cycles' radius again, as for a new scene. Between renders the cloud
  keeps Cycles' radius, as Cycles' own scene does (**INFERENCE**: restoring it after each render instead would leave a
  later BVH rebuild, after another object is added for example, bounding splats with the authored radius). The copy is
  dropped with the geometry (`cbc_host_delete_node`) and the host.
- Static transform (`expected_bake`, the host's copy of `ObjectManager::apply_static_transforms`): the splat exclusion of
  `scene/object.cpp` is added; for a cloud rendered as points it changes nothing. `copy_attributes` was split into
  `copy_attribute` and a loop over it (same behaviour) so the radius copy can reuse it.
- Attribute specialisation: the host references no default shader but `default_light` (the world-volume light), adds no
  attribute request and does not touch `default_gsplat`; the WebGPU device's attribute census counts referenced shader
  roots only (`native/cycles/pipelines.cpp`). Nothing in D2 changes either.
- Rule 7: `point clouds rendered as Gaussian splats` left `CBC_HOST_REJECTIONS` (every backend) and is now the first entry
  of the capability report's WebGPU list (`native/host/host_api.inc`, a new line before the D1 entry), of "The WebGPU
  backend:" in `AGENT-README.txt` "RENDER-TIME REJECTIONS"; the `native/host/ABI.md` point-cloud row says "On the WebGPU
  backend, point clouds rendered as Gaussian splats are refused at render" with the device's message.
- Documents: `native/host/ABI.md` (the point-cloud row, PROVISIONAL, and "Edits after a render"), `native/host/cycles_host.h`
  (the attribute and point comments), `AGENT-README.txt` (the point-cloud standard names, a new "GAUSSIAN SPLATS" part of
  SCENE SETTINGS with an example, the rejection list and its "Why."), `native/DEVICE.md` item 186, `docs/PARITY.md`,
  `MAINTAINER-README.txt` (the Gaussian-splat import note), `native/oracle/README.md` (the new family).
- Text D1 had written and D2 had to change: the `AGENT-README.txt` "Why." sentence and the line "PointCloudNode.RenderAs
  stays Points", the `ABI.md` point-cloud row and the first sentences of `native/DEVICE.md` item 186 all said that splats
  cannot be authored; they now describe the accepted case. No D1 code, test or scene changed.

**Provisional signatures** (each the maintainer's to approve):

| Signature | Reason | Alternative considered |
| --- | --- | --- |
| Standard point-cloud names `radiance:base` (`CBC_VALUE_FLOAT4`), `scale` (`CBC_VALUE_VECTOR`), `rotation` (`CBC_VALUE_QUATERNION`), `radiance:sh` (`CBC_VALUE_SPHERICAL_HARMONICS_REST`), on `CBC_ATTR_VERTEX` with `CBC_ATTR_STANDARD` | Cycles' own names and types; it finds splat data by standard only | custom names that the host maps: rejected, a second naming; a dedicated `cbc_host_gsplats` export: rejected, a larger surface that duplicates the attribute calls; `radiance` as a fifth name: not added (lead ruling) |
| A splat cloud without one of the three is `CBC_INVALID` at render, naming every missing one | Cycles would draw plain points with no message (kit rule 5); its defaults have no caller in the pinned source | supply Cycles' defaults with `create_missing_gsplat_attributes`: rejected (lead ruling), a host invention that readback would show |
| A rotation with no normal binary32 square among its components, or whose squared length overflows, is `CBC_INVALID` at render | Cycles divides by the squared length; a subnormal one is zero on a GPU that flushes subnormals (**INFERENCE** for Metal), and an overflowing one gives infinity minus infinity in `quaternion_to_scaled_rotation` | refuse an exact zero only (the minimum of the lead ruling): rejected, the other two cases have no defined result either |
| Reads of a splat cloud's `radius` return the authored radius, `cbc_host_point_motion` compares its centre step with it, and the next edit restores it | The ABI's promise that reads return authored values (`ABI.md`, "Edits after a render") | document that reads return Cycles' bounding radius and that motion must pass it back |
| On the WebGPU backend a point cloud rendered as splats is `CBC_UNSUPPORTED`, "WebGPU Gaussian splats not implemented", whatever its attributes | Kit rule 7; the device's own words | keep the old host text there: rejected, it says "not available in this library", which is no longer true |

Count literals moved: none. The export count (60), the registry counts (187 node types, 1311 sockets, `generate_model.py
--check` up to date, 188 files) and the value-kind range (0..19) are unchanged. In the capability lists one entry moved
from the every-backend group to the WebGPU group; no fixture holds a count of rejections (`git grep` of `native/host`,
`native/tools`, `tools`, `tests`). No gate script changed.

**Tests added** (managed total 365, was 348):

- `GaussianSplatTests` (new, CPU, 14 cases): a splat cloud renders, repeats bit for bit and differs from the same cloud with
  bands 1 to 3, rendered as points and absent, and renders on the Embree CPU backend; each of the three attributes missing
  is refused naming it, and all three set without the standard flag are refused naming all three; a rotation of zero
  length, one of components `1e-20` and one of `3e19` are refused by name and the scene renders once fixed; the authored
  radius reads back after a render (with and without the standard flag), `SetMotion` with those radii as its centre step is
  accepted, a motion-blurred render follows and the radius and positions still read back as authored; a cloud switched to
  points after a splat render renders bit for bit like a new points scene (the authored radius came back); an edit of
  `scale` and of the object transform after a render matches a scene built with them bit for bit; the static transform:
  a single-user splat cloud under a uniform scale and rotation renders bit for bit like the same cloud with a second user
  no ray sees (which keeps any cloud instanced), while the same comparison for the cloud rendered as points differs (Cycles
  bakes that one); the four names with their kinds read back (spherical harmonics within 1/255), `rotation` on a mesh and
  `radiance` as a standard name are `Unsupported`, a wrong kind or domain for each name is `Invalid`; a caller shader that
  reads `radiance` through an Attribute node into an Emission renders bit for bit like Cycles' default splat shader.
- `SceneSettingsTests`: the splat refusal test now expects the missing-attribute refusal (`Invalid`, all three named) and a
  render once the cloud is points again.
- `CapabilityReportTests`: the entry is in the README's WebGPU group only and in no compiled backend's scope but `webgpu`;
  the word-for-word test covers the moved entry.
- `OracleSceneBundleTests`: the committed `gsplat/spherical-harmonics` document loads through the managed loader (the
  standard flag and `render_as`), renders repeat-identically, and its rotations and authored radii read back.
- `GpuRenderTests.gaussian_splats_render_on_metal_and_are_refused_by_name_on_webgpu`: two clouds (Cycles' default splat
  shader with band 0; bands 1 to 3 with a lit caller shader on a rotated, uniformly scaled single user). On Metal it is
  judged by the reference policy against the new film `References/metal-osx-arm64/gaussian-splats.rgba.f32` (recorded with
  only this test selected; the manifest gained only this entry; three later runs: maximum 6e-8 from it, 0 practical
  failures); its CPU comparison is maximum 0.35, 1,050 practical failures, 1,053 strict differences of 6,144 components. On
  WebGPU it expects `Unsupported` with the device text, then a render once both clouds are points.
- Host gate: none (as for D1, the macOS gate has no attribute pattern; `native/tests/host/abi_consumer.c` runs on Linux
  only).

**Why the CPU and Metal films of splats differ (reported, not judged).** Whether a ray hits a splat is decided by
`hash_float3_to_float` of the hit position against the splat's response (`kernel/geom/gsplat_intersect.h`), so a last-bit
difference in that position, which CPU and Metal arithmetic produce, turns a hit into a miss. A scratch check
(`<repo>/native/work/macos-arm64-d2-20261114/noise-run/`, not in this folder) rendered `gsplat/band0` and
`gsplat/spherical-harmonics` at 8, 128 and 1,024 samples on `cpu-arm64-macos` and `metal-m2pro`: the root mean square of
the difference fell from 0.0206 to 0.0049 to 0.0017 (band 0) and from 0.0382 to 0.0087 to 0.0030 (bands 0 to 3), as
sampling noise falls with the square root of the sample count, and at 1,024 samples the mean of each colour channel
agreed within 1.1e-4. **INFERENCE:** the differences are sampling noise of the stochastic hit test, not a fault of either
device.

**A zero scale.** `gsplat_intersect_test` rejects the NaN distance that a scale of 0 on any axis gives ("Gracefully reject
NaN distance"), and Cycles bounds a splat whose scales are all 0 with a radius of 0, so no refusal was added. A scratch
check (`<repo>/native/work/macos-arm64-d2-20261114/zero-scale-run/`) rendered `gsplat/band0` with every z scale 0 and with
every scale 0: both CPU films are bitwise the film of the same room without the cloud, and the four Metal films (BVH2 and
MetalRT) are within 2.7e-6 of it (0 practical failures), although Cycles compiles its Metal kernels with fast math
(`device/metal/device_impl.mm`).

**Oracle scenes.** A new family `native/oracle/scenes/gsplat/` written by `author_gsplat.py` (`--check` passes for all
eleven; `author_points.py`, `author_attributes.py` and `author_sampling.py` `--check` unchanged). It is built on the points
family's builders and room, as `author_attributes.py` is, and reuses D1's axis-angle quaternions. `band0` (default splat
shader, band 0, two splats below opacity 0.01), `spherical-harmonics`, `caller-shader`, `single-user-transform`,
`instanced`, `object-motion`, `deform-3` (object and deformation motion of the positions; the per-attribute motion export
is deferred), `beside-meshes`, `beside-volume`, `transparent-shadows` (an opaque and a transparent splat shader behind a
transparent sheet) and `with-points` (64x48, 8 samples; `beside-volume` keeps its source's 32x24 at 16). Run
`<repo>/native/work/oracles/macos-arm64-d2-20261114/` (scratch, not in this folder), `starter/diffuse-triangle` first, on
`cpu-arm64-macos`, `metal-m2pro` and `metal-m2pro-metalrt` (31 s): 0 failures, every CPU film repeat-identical (bitwise),
Metal repeats within 1.2e-7, every comparison reported:

| Scene | BVH2 vs CPU | RT vs CPU | RT vs BVH2 |
| --- | --- | --- | --- |
| `starter/diffuse-triangle` | 0 pf, max 3e-08 | 0 pf, max 3e-08 | 0 pf, max 1.5e-08 |
| `gsplat/band0` | 3286 pf, max 0.42 | 3336 pf, max 0.42 | 2848 pf, max 0.098 |
| `gsplat/spherical-harmonics` | 3288 pf, max 0.88 | 3338 pf, max 0.86 | 2851 pf, max 0.15 |
| `gsplat/caller-shader` | 3437 pf, max 0.78 | 3499 pf, max 0.76 | 3063 pf, max 0.13 |
| `gsplat/single-user-transform` | 3302 pf, max 0.39 | 3322 pf, max 0.49 | 3115 pf, max 0.59 |
| `gsplat/instanced` | 3714 pf, max 0.68 | 3692 pf, max 0.68 | 3000 pf, max 0.19 |
| `gsplat/object-motion` | 3017 pf, max 1.09 | 3040 pf, max 1.09 | 2369 pf, max 0.22 |
| `gsplat/deform-3` | 3426 pf, max 0.29 | 3429 pf, max 0.31 | 3037 pf, max 0.075 |
| `gsplat/beside-meshes` | 3597 pf, max 0.74 | 3620 pf, max 0.85 | 2714 pf, max 0.83 |
| `gsplat/beside-volume` | 653 pf, max 0.37 | 579 pf, max 0.35 | 605 pf, max 0.14 |
| `gsplat/transparent-shadows` | 3765 pf, max 0.26 | 3726 pf, max 0.26 | 3165 pf, max 0.079 |
| `gsplat/with-points` | 2349 pf, max 0.62 | 2332 pf, max 0.62 | 1843 pf, max 0.15 |

"pf" as in section 4.1, of 12,288 components (3,072 for `beside-volume`). The BVH2 and MetalRT films differ as much as
either does from the CPU, as the hit test above implies. The run was made again after a comment-only rebuild of the host
(below); its 24 CPU films are bitwise those of the first run.

**Rule 9 at D2.** The whole committed corpus (774 documents: the 763 of D1 plus the 11 new ones) and the 2 bench documents
of `<repo>/native/work/oracle-assets/bench_01/`, rendered on `cpu-arm64-macos` alone into
`<repo>/native/work/oracles/macos-arm64-d2-corpus-20261114/` (776 scenes, 0 failures, every film repeat-identical;
139 s). Compared by scene path and film index: all 1,452 films of the 726 scenes of the base run, all 68 films of the 34
survey scenes (`runs/phase2/`) and all 10 films of the 5 D1 scenes (`<repo>/native/work/oracles/macos-arm64-d1-corpus-20261114/`)
are bitwise identical; nothing moved. The 22 films of the 11 new scenes equal those of the run above. The pass films were
compared as well: 2,870 of the base run's 2,872 and all 24 of the survey's are bitwise identical; the two that differ are
`passes/data-passes` pass 8, `RenderTime`, a wall-clock measurement whose two repeats differ within this run too. Host
library `e737bb660fa11a267e89f309248650f4521adad6c57ba03b8a9ce3e1e155b20d`, runner
`af4c3885cae295ecb00fdfdad4d041451f0a776f17cea4bca63d7d649f1a1be3` (the D1 runner; it was not rebuilt).

**Build, gate, managed tests and the package.** `python3 tools/build_macos.py --jobs 8 --skip-dependencies`: passed, 0
warnings in the host build log (25 s, incremental; again after a comment in `host_scene.inc` was reworded, 20 s). macOS
host gate (`<repo>/native/work/macos-validation-20261114c/`, on the final library): passed (3,089 films compared, 0
same-host bitwise failures, Metal 0 practical failures, maximum 4.5e-8; 15 s). Testing document step 7 with .NET SDK
11.0.100 (restore; Release build, test, pack; Debug build, test): exit 0 throughout; Release 365 tests, 359 passed, 6
skipped, 0 failed; Debug the same. The six skips are the WebGPU-only tests of section 5.1. The Release build's one warning
is the pack driver's ("linux-x64: payload is STALE"); the Debug build has 0 warnings. The consumer test of the MacOSArm64
package: a scratch stage of this build (`<repo>/native/work/staging/osx-arm64-d2`, never approved) packed as 1.0.318.942
(`package_check.py`: 323 files match, MacOSArm64 "NOT APPROVED, NOT PUBLISHABLE"); `dotnet build`, `run`, `publish`,
`publish -r osx-arm64 --self-contained false` and `--self-contained` each rendered on the CPU, on Metal and on Metal with
GPU denoising, with the 292 kernel sources beside the library and none flattened; with `codebrix-cycles/` removed, Metal
failed with "Metal kernel sources are missing beside the native library" and the CPU rendered. The runtime pack was the
cached `Microsoft.NETCore.App.Runtime.osx-arm64` 10.0.15 (SHA-512
`UDRfvR+u0bf/7cn/gbFxOG+2Py6pAGXHBK4p//mrEGJopfTQ6cohTPL7LCcXdyaaOSMw0ggYfUwKh5EyjzTQmg==`). Both pack folders the runs
wrote were deleted. `python3 tools/licensing/audit.py`: passed. `git diff --check`: clean.

**Unverified on other platforms.** Everything WebGPU: the host refusal on `CBC_HOST_WEBGPU`, the capability report's
WebGPU list (compiled only with `WITH_WEBGPU`, so its word-for-word check and the new capability test's WebGPU branch run
only on a WGSL build) and the WebGPU branch of the GPU test. The Linux x64 build, host gate, managed tests and CPU oracle
(the x64 films of the eleven new scenes), the Linux ARM64 CPU oracle of the new scenes, and the Windows and Android
builds. The static-transform test's contrast (a single-user cloud rendered as points differs from its instanced film)
was seen on ARM64 only.

**What was left.** The per-attribute motion export (`cbc_host_attribute_motion`; deferred by ruling): splat motion is
object motion and the motion of the positions. Cycles' `radiance` attribute (`ATTR_STD_GSPLAT_RADIANCE`) is not a standard
name (ruling); a custom attribute of that name is found by an Attribute node of a caller shader, which was not tested. No
host-gate case (above). The WebGPU port of splats is a device item (`native/DEVICE.md` 186).

### 5.3 D3: host-only gaps (2026-11-14)

The gaps of kit item 3 that Cycles' Metal device supports (row 15, section 4.4), one after the other, each made at
`f2586fc17028b62b6e4702a59a401b8d3f230492` on top of the uncommitted D1 and D2 changes (every run below records
`repoWorkingTreeClean: false` for that reason). Each gap has its design first, then what was done.

#### Gap 1: UDIM tiles

**Design.** The pinned Cycles builds a UDIM texture from image loaders, with no file: `ImageManager::add_image(vector<
unique_ptr<ImageLoader>> &&, const ImageParams &)` (`scene/image.cpp`) adds one `ImageSingle` per loader and keys it by
`ImageLoader::get_tile_number()`; the kernels map a UV to tile `1001 + 10 * int(v) + int(u)` for `0 <= u < 10` and
`v >= 0` and shift the UV into that tile (`kernel_image_udim_map`, `kernel/util/image_2d.h`), and read the node's Missing
inputs for any other UV or an absent tile. Metal reaches it through the generic GPU image path
(`kernel/device/gpu/image.h`, `kernel_image_interp_with_udim`). Cycles builds UDIM handles for Image Texture nodes only
(the `tiles` socket of `ImageTextureNode`; the Environment Texture node adds one image).

- Native: a new export, `cbc_host_image_bind_tiles(host, texture_node, tiles, images, count)` (**PROVISIONAL**), binds
  `count` image handles to one Image Texture node, `images[i]` at tile `tiles[i]`. A node's binding becomes a list of
  (tile, image) pairs, tile 0 meaning a plain `cbc_host_image_bind`; `prepare_graphs` hands a plain binding to the
  single-loader `add_image` as before and a tiles binding to the multi-loader one. The host's `MemoryImageLoader` gains
  its tile number (`get_tile_number`) and compares it in `equals`: `add_image_texture` returns an existing image whenever
  `ImageLoader::equals` and the parameters match, and the multi-loader `add_image` then reads the tile number from that
  image's loader, so a loader that ignored the tile would make the same image bound at two tiles one device image with
  the first tile's number. With tile 0 the loader compares as before, so plain bindings are unchanged.
- Refusals, all by name and with nothing changed: a count of 0 (`CBC_INVALID`); an Environment Texture node
  (`CBC_UNSUPPORTED`, "UDIM tiles bind to Image Texture nodes only, as in Cycles"); any other node, or one outside an
  authoring graph (`CBC_INVALID`); a tile number below 1001 or above `INT_MAX`, a tile listed twice, an invalid, released
  or foreign image handle, and images of different formats (`CBC_INVALID`, each naming the tile). One format, because
  `ImageTextureNode::compile` takes the sRGB compression flag of the first tile for every tile ("All tiles have the same
  metadata", `scene/shader_nodes.cpp`).
- Edits: a new binding replaces the old one; `cbc_host_image_bind(node, 0)` removes a tiles binding; `cbc_host_image_update`
  of a member image applies at the next render, as for a plain binding (the node's handle is cleared; every new UDIM
  grows the UDIM table, which `device_update_udims` rewrites when its size changes).
- WebGPU (kit rule 7): the WGSL device reads the UDIM table but has never been run with one, and nothing WebGPU runs
  here. The host refuses a tiles binding on `CBC_HOST_WEBGPU` at render and bake (`validate_unavailable`,
  `CBC_UNSUPPORTED`, "WebGPU UDIM tiles not implemented"); the capability report, AGENT-README "RENDER-TIME REJECTIONS"
  and `native/host/ABI.md` carry "UDIM tiles (several images bound to one Image Texture node)". No file under
  `native/cycles` changes.
- Managed (**PROVISIONAL**): `ImageTextureNode.BindTiles(ReadOnlySpan<UdimTile>)` (generated: an extra of
  `tools/managed/generate_model.py` for `image_texture` only), `public readonly record struct UdimTile(int Number,
  CyclesImage Image)`, `CyclesHost.BindImageTiles`, `CyclesAbi.BindImageTiles` and `Native.BindImageTiles`. Scene
  documents: the `image` pseudo-socket also takes an object of tile numbers and image handles, `{"1001": "a", "1002": "b"}`
  (runner and managed loader).
- Export count 60 to 61: `native/host/ABI.md`, the three export lists, and the six gate scripts that assert it.

**What was done.** As designed; the build records `exports: 61`.

- `native/host/host_scene_types.inc`: `MemoryImageLoader` keeps a tile number (0 for a plain binding), overrides
  `get_tile_number`, compares the tile in `equals` and names it in `name()` (unchanged for tile 0); `ImageBinding`, the
  (tile, image) list of one node. `native/host/cycles_host.cpp`: `Graph::images` holds an `ImageBinding` per node;
  `prepare_graphs` uses the single-loader `add_image` for a plain binding and the multi-loader one for tiles.
  `native/host/host_scene.inc`: `cbc_host_image_bind` stores a one-entry binding; the new export validates everything
  before it replaces the binding; `validate_unavailable` refuses a tiles binding on `CBC_HOST_WEBGPU`.
  `native/host/host_api.inc`: `cbc_host_image_update` walks every tile of every binding, and refuses (`CBC_INVALID`, with
  nothing changed) an update that would give one tile of a binding another format than a tile bound to another image (the
  rule of the bind, which an update could otherwise break); the capability report's WebGPU list gains the entry.
- `native/host/cycles_host.h` (the declaration and its PROVISIONAL comment), the three export lists, `native/host/ABI.md`
  (the Images row, "Lifetime and deletion", the Exports table and its count), `AGENT-README.txt` (a "UDIM TILES" part of
  SCENE SETTINGS with an example, the API lists, the rejection entry and its "Why.", and the UDIM phrase removed from WHAT
  THIS PACKAGE DOES NOT DO), `native/DEVICE.md` item 172 (the UDIM bullet RESOLVED), `MAINTAINER-README.txt` NOTES (the
  export map), `native/oracle/README.md` (the object form and the new family), `docs/TESTING-MACOS-ARM64.md` step 3 (the
  gate's new contract checks). `docs/PARITY.md` names no host-only gap and is unchanged.
- Managed: `UdimTile` (new file), `ImageTextureNode.BindTiles` (generated; `generate_model.py` writes it for
  `image_texture` only and reserves the name; `--check` up to date, 188 files, 187 node types, only
  `Generated/Shaders/ImageTextureNode.cs` changed), `CyclesScene.BindImageTiles` (internal: the images' scene and lifetime,
  as `Bind` checks them), `CyclesHost.BindImageTiles`, `CyclesAbi.BindImageTiles` (`Invalid` when the two spans differ in
  length), `Native.BindImageTiles`; the managed loader and the oracle runner take the object form.
- No file under `native/cycles`, `native/shaders`, `native/src` or `third_party/` changed.

**Provisional signatures** (each the maintainer's to approve):

| Signature | Reason | Alternative considered |
| --- | --- | --- |
| `int cbc_host_image_bind_tiles(CBCHost, CBCNode texture_node, const uint32_t *tiles, const CBCImage *images, uint64_t count)` | Cycles' multi-loader UDIM; two parallel arrays as in `cbc_host_set_nodes` and `cbc_host_particles` | a tile number in `CBCImageConfig.reserved` and repeated binds (changes what a bind means); a struct array (a new struct for two fields) |
| Refusals: count 0, a tile below 1001 or above `INT_MAX`, a repeated tile, an invalid, released or foreign handle, two formats (`CBC_INVALID`); an Environment Texture node (`CBC_UNSUPPORTED`) | kit rule 5: Cycles would key a repeated tile twice, decode a second format with the first tile's settings, and has no UDIM for environment textures | accept any `int` tile (Cycles reaches only tiles of 1001 and above) |
| `cbc_host_image_update` refuses a format change that would leave a binding with two formats | the same rule after the bind | refuse at render (later, and the handle would already be changed) |
| On the WebGPU backend a tiles binding is `CBC_UNSUPPORTED` at render and bake, "WebGPU UDIM tiles not implemented" | kit rule 7; the device has not been run with a UDIM table | none (lifting it needs a WebGPU run) |
| `UdimTile(int Number, CyclesImage Image)`, `ImageTextureNode.BindTiles(ReadOnlySpan<UdimTile>)` | one value per tile, beside `Bind`/`Unbind`; `int` as Cycles' tile numbers (a negative number reaches the host as a number above `INT_MAX` and is refused there) | two parallel spans on the facade (error-prone); a method on `CyclesScene` taking the node (less discoverable); `uint Number` (the C type) |
| `CyclesHost.BindImageTiles(Node, ReadOnlySpan<uint>, ReadOnlySpan<Image>)`, `CyclesAbi.BindImageTiles(ulong, ulong, ReadOnlySpan<uint>, ReadOnlySpan<ulong>)`, `Native.BindImageTiles` | the low-level tiers mirror every export | none |
| Scene documents: the `image` pseudo-socket's object form `{"1001": HANDLE, ...}`, bound in document order | no new keyword | an `imageTiles` pseudo-socket |

**Count literals moved.** The export count 60 to 61: `native/host/ABI.md` ("## Exports (ABI 1, 61 functions)"), the
lists `exports.def`, `exports.macos`, `exports.map`, and `native/tools/macos_host_gate.py` (run here),
`native/tools/host_gate.py` (twice: the assertion and `exportCount`), `macos_x64_host_gate.py`, `windows_host_gate.py`,
`linux_arm64_host_gate.py` and `android_host_gate.py` (by reading only, unverified). `git grep -n -w 60` of `native/host`,
`native/tools`, `tools`, `tests` and `src` finds no other export count. The registry counts (187 node types, 1311 sockets)
did not move; the capability report's WebGPU list gained one entry (no fixture counts its entries).

**Tests added** (managed total 375, was 365):

- `UdimTileTests` (new, CPU, 7 cases), on a strip of quads seen by an orthographic camera, one sample of an emission of
  the texture colour: four solid float4 tiles with Closest interpolation (1001, 1002, 1003, 1011) render bit for bit like
  the same strip with one image texture per quad (face shader indices) and like constant emissions of the tile colours;
  six-tile byte4 checkers with Linear interpolation against one image per quad, 0 practical failures (the shift of the
  UV into its tile rounds differently); an unbound tile, u of 10, v and u of -1.25 to -1.75 read the Missing colour,
  and one image at tiles 1003 and 1001 fills both (bound in that order, so a loader that ignored the tile would leave
  1001 without an image), all bit for bit against constant emissions; every refusal by name (an Environment Texture node,
  another shader node, tiles 1000 and -1, a repeated tile, two formats, no tile, a released and a foreign handle through
  `CyclesHost`; a disposed image, another scene's image and two span lengths in managed code) with the film unchanged;
  edits after a render (a member image updated in pixels and size, a refused format change, a rebind, a plain `Bind`,
  `Unbind`, the first binding again), each bit for bit like a scene built with it; one image at every tile may change
  its format; the status-code tier binds and reports a refusal through `LastError`.
- `CapabilityReportTests.udim_tiles_are_a_rejection_of_the_webgpu_backend_only`; the word-for-word test covers the entry.
- `OracleSceneBundleTests.a_committed_udim_scene_binds_its_tiles_through_the_loader`: `images/udim-missing-tile` renders
  repeat-identically, differs without one tile, and a key that is not a number or names no image is refused by name.
- `GpuRenderTests.udim_tiles_render_on_metal_and_are_refused_by_name_on_webgpu`: five byte4 checker tiles and an unbound
  one on a lit quad behind the triangle; on Metal judged by the reference policy against the new film
  `References/metal-osx-arm64/udim-tiles.rgba.f32` (recorded with only this test selected; the manifest gained only this
  entry; three later runs: maximum 6e-8 from it, 0 practical failures); its CPU comparison is maximum 3e-7, 0 practical
  failures, 9 strict differences of 6,144 components. On WebGPU it expects `Unsupported` with the message, then a render
  with one image bound.
- Host gate: the contract block of `native/tools/macos_host_gate.py` gains `udimTiles`: an Environment Texture node 2,
  tile 1000 1, a repeated tile 1, two formats 1, tiles 1001 and 1003 bound 0, and a CPU render with them 0 and finite.

**Oracle scenes.** A new family `native/oracle/scenes/images/` written by `author_images.py` (`--check` compares the three
documents and their 12 raw pixel files; `author_starters.py` rewrites its files unchanged). A new family because the
existing ones are geometry or lighting families and none binds images beyond one texture; the documents use the starter
textured plane's builders. `udim-three-tiles`, `udim-checker-linear` and `udim-missing-tile` (48x32, 8 samples; section
`native/oracle/README.md`). Run `<repo>/native/work/oracles/macos-arm64-d3-udim-20261114/` (scratch, not in this folder),
`starter/diffuse-triangle` first, on `cpu-arm64-macos`, `metal-m2pro` and `metal-m2pro-metalrt` (13 s): 0 failures, every
CPU film repeat-identical (bitwise), Metal repeats within 3e-8, every comparison reported:

| Scene | BVH2 vs CPU | RT vs CPU | RT vs BVH2 |
| --- | --- | --- | --- |
| `starter/diffuse-triangle` | 0 pf, max 3e-08 | 0 pf, max 3e-08 | 0 pf, max 1.5e-08 |
| `images/udim-three-tiles` | 0 pf, max 3e-08 | 0 pf, max 3e-08 | 0 pf, max 3e-08 |
| `images/udim-checker-linear` | 66 pf, max 1.2e-04 | 66 pf, max 1.2e-04 | 0 pf, max 6.1e-05 |
| `images/udim-missing-tile` | 0 pf, max 4.5e-08 | 0 pf, max 4.5e-08 | 0 pf, max 3e-08 |

"pf" as in section 4.1, of 6,144 components. **INFERENCE:** the 66 of `udim-checker-linear` are Metal's hardware bilinear
filtering, which `native/host/ABI.md` (Determinism) records for the linearly filtered starter texture; the two Closest
scenes agree within 4.5e-8.

**Rule 9 at gap 1.** The whole committed corpus (777 documents: the 774 of D2 plus the 3 new ones) and the 2 bench
documents of `<repo>/native/work/oracle-assets/bench_01/`, rendered on `cpu-arm64-macos` alone into
`<repo>/native/work/oracles/macos-arm64-d3-udim-corpus-20261114/` (779 scenes, 0 failures, every film repeat-identical;
109 s). Compared by scene path and film index: all 1,452 films of the 726 scenes of the base run, all 68 films of the 34
survey scenes, and all 10 films of the 5 D1 scenes and all 22 films of the 11 D2 scenes of
`<repo>/native/work/oracles/macos-arm64-d2-corpus-20261114/` are bitwise identical; nothing moved. The 6 films of the 3 new
scenes equal those of the run above. Pass films: 2,870 of the base run's 2,872 and all 24 of the survey's are bitwise
identical; the two that differ are again `passes/data-passes` pass 8, `RenderTime`. Host library
`af6f020b3ca2b63a53deb994dc55b31b41dd356975d9556c6dc97dfb6b58f53f`, runner
`eb72dc33676996d26b3e9d0c7a244df1416bcd9e7de4c80ae43476fc4300dc50` (rebuilt for the object form).

**Build, gate, managed tests and the package.** `python3 tools/build_macos.py --jobs 8 --skip-dependencies`: passed,
0 warnings in the host build log and the build output (three incremental builds, 19 to 25 s). macOS host gate
(`<repo>/native/work/macos-validation-20261114d/`): passed (3,089 films compared, 0 same-host bitwise failures, Metal 0
practical failures, maximum 4.5e-8; 16 s). Testing document step 7 with .NET SDK 11.0.100: restore, both builds, pack and
the Debug test exit 0; Debug 375 tests, 369 passed, 6 skipped (the WebGPU-only tests of section 5.1), 0 failed. The Release
test failed in every one of seven full runs, always one test,
`CyclesSceneTests.RenderAsync_cancels_promptly_and_leaves_the_destination_untouched` ("Expected cancelled.Elapsed to be
less than 10s", 13 to 29 s; 368 passed, 6 skipped). It
passes alone (0.8 s). It is not caused by this gap: with every UDIM test filtered out it still fails, and with a control
build of the D2 state made in a scratch copy (`<repo>/native/work/macos-arm64-d3-udim-20261114/d2-tree`, the D2 patch and
files on `f2586fc17`, 60 exports) swapped in for the run it failed 3 of 3 times, and with D2's staged library 3 of 6 (D2's
own step 7 passed earlier the same day). **INFERENCE** from `integrator/render_scheduler.cpp`: a cancel that reaches the
render after its first one-sample batch waits for the second batch, which the headless scheduler sizes to about 30 s of
samples (`guess_display_update_interval_in_seconds`) and checks only between batches, so the test passes only when its
polling loop sees the render start within the scene update and the first sample, which a loaded test process (a
background XProtect scan ran during the first runs) misses. The Release build's one warning is the pack driver's
("linux-x64: payload is STALE"); the Debug build has 0 warnings; the pack folder was deleted. The consumer test of the
MacOSArm64 package: a scratch stage (`<repo>/native/work/staging/osx-arm64-d3-udim`, never approved) packed as
1.0.318.1026 (323 files match, MacOSArm64 "NOT APPROVED, NOT PUBLISHABLE"); `dotnet build`, `run`, `publish`,
`publish -r osx-arm64 --self-contained false` and `--self-contained` each rendered on the CPU, on Metal and on Metal with
GPU denoising, the 292 kernel sources beside the library and none flattened; with `codebrix-cycles/` removed Metal failed
with "Metal kernel sources are missing beside the native library" and the CPU rendered; the runtime pack was the cached
`Microsoft.NETCore.App.Runtime.osx-arm64` 10.0.15 (SHA-512 as in section 5.2); the pack folder was deleted.
`python3 tools/licensing/audit.py`: passed. `git diff --check`: clean.

**Unverified on other platforms.** Everything WebGPU: the host refusal, the capability report's WebGPU list (compiled only
with `WITH_WEBGPU`) and the WebGPU branch of the GPU test; whether the WGSL device's UDIM lookup is right (it was never
run). The count edits of `host_gate.py`, `macos_x64_host_gate.py`, `windows_host_gate.py`, `linux_arm64_host_gate.py` and
`android_host_gate.py` (read, not run). The Linux x64 build, gates and CPU oracle (the x64 films of the three new scenes),
the Linux ARM64 CPU oracle of them, and the Windows and Android builds.

**What was left.** The Image Texture node's `tiles` socket (Cycles' file-name UDIM list) stays inert on a bound node, as
it was for a plain binding (section 7). Tiled (mip-mapped) tiles come with the texture cache gap. The Release timing test
above (section 7).

#### Gap 2: progressive results

**Design.** During a render the pinned `PathTrace::render` runs, per render work, the path tracing, the adaptive filter,
denoising, `write_tile_buffer`, `update_display` and `progress_update_if_needed` (`integrator/path_trace.cpp`);
`update_display` hands the output driver a `PathTraceTile` through `OutputDriver::update_render_tile` when the work's
`display.update` is set, and `PathTraceTile::get_pass_pixels` copies the render buffers from the device once and reads
them normalised by the samples rendered so far. The host's session is headless (`SessionParams::headless`, set at
`cbc_host_create`), and `RenderScheduler::work_need_update_display` returns false for a headless session
(`integrator/render_scheduler.cpp`), so today only the final work updates the display, and `HostOutput` ignores it. The
scheduler reads `headless` once, in its constructor, and it also selects the batch sizes (about 30 s of samples
headless; 1 s below 32 samples and 2 s after in background mode, with a 2 s, then 15 s, occupancy limit), so the mode is
fixed per Session, that is per host. Metal has no code of its own here; `PathTraceTile` copies its buffers.

- C ABI (**PROVISIONAL**, no new export): `CBCHostConfig` grows by two trailing fields, `flags` and `reserved2` (40 bytes;
  the library accepts 32, flags 0, and 40); `CBC_HOST_PROGRESSIVE_RESULTS = 1` is the one flag, other bits and a nonzero
  `reserved2` are `CBC_INVALID`. A host created with it has `SessionParams::headless` false (background stays true, so
  `session.wait()` returns as before). `CBC_EVENT_FILM = 4`, a new event kind, and `CBCEvent` grows by `film` (a
  `const float *`), `width` and `height` (56 bytes), NULL and 0 for the other kinds. Only flagged hosts send film events,
  so older consumers never see the kind.
- Native: `HostOutput::update_render_tile` forwards to the host, which, for a flagged host during `cbc_host_render` (never
  a bake), reads Combined (4 floats) of the tile into a full film, placing a camera border's region as
  `write_render_tile` does, and marks it pending. The next progress event on the same thread (the session thread's
  `progress_update_if_needed`, right after `update_display`, when `Progress::add_samples` has set the sample count)
  emits it as `CBC_EVENT_FILM`, with `samples_done` the samples in it, under the event lock, so film events never run
  concurrently with the others; a film still pending when `session.wait()` returns is emitted on the calling thread
  before `cbc_host_render` returns. Nonzero from the callback cancels the render (the existing rule); after a cancel no
  further film is sent. `HostOutput::write_render_tile` and every other path are unchanged.
- Bit for bit: the CPU kernels add each pixel's samples in sample order whatever the batches, adaptive filtering is
  scheduled at fixed sample counts, and the volume-guiding denoise at powers of two that both modes reach
  (**INFERENCE** from `render_scheduler.cpp` and `path_trace_work_cpu.cpp`), so the final film of a flagged CPU host should
  equal a plain host's; the tests measure it.
- WebGPU (kit rule 7): a flagged host on `CBC_HOST_WEBGPU` is refused at render and bake (`validate_unavailable`,
  `CBC_UNSUPPORTED`, "WebGPU progressive results not implemented"); the capability report, AGENT-README and ABI.md carry
  "progressive results (intermediate films during a render)"; the capability report's `events` gains `"film"`.
- Managed (**PROVISIONAL**): `[Flags] HostFlags { None = 0, ProgressiveResults = 1 }`; new constructor overloads
  `CyclesHost(Backend, uint, HostFlags)`, `CyclesScene(Backend, uint, HostFlags)`, `CyclesScene(CyclesBackend, uint,
  HostFlags)` and `CyclesAbi.Create(Backend, uint, HostFlags, out ulong)` (the existing signatures stay);
  `RenderEventKind.Film = 4`; `event EventHandler<IntermediateFilm> CyclesScene.IntermediateFilmReceived` with
  `IntermediateFilm(FilmBuffer Film, uint SamplesDone, uint SamplesTotal, TimeSpan Elapsed)`, a copy made in the
  callback (film events are not routed to `RenderEventReceived`); `Native.HostConfig` 40 bytes, `Native.Event` 56.
- Counts: no export (61 stays); `native/host/ABI.md` (`CBCHostConfig` 32 or 40, `CBCEvent` 56, events 1..4),
  `CapabilityReportTests` (`Events`), `NativeHeaderConstantMatching` (two rows, `HostFlags`, `flagNoneMembers` 5), the
  host config size in `CyclesHost.cs` and `CyclesAbi.cs`.

**What was done.** As designed; the build records `exports: 61`.

- `native/host/cycles_host.h`: `CBC_HOST_PROGRESSIVE_RESULTS`, the two trailing `CBCHostConfig` fields, `CBC_EVENT_FILM`
  and the three trailing `CBCEvent` fields, each marked PROVISIONAL. `native/host/cycles_host.cpp`: `cbc_host_create`
  accepts 32 or 40 bytes and reads nothing past `struct_size` (a `static_assert` pins the layout); `HostOutput` overrides
  `update_render_tile` and forwards to `Host::film_update` for a flagged host only; `Host` keeps the flags and the pending
  film (`film_update`, `emit_film`, the film branch of `progress_event`); `cbc_host_render` enables film events for its
  duration and sends a film still pending after `session.wait()`; `emit` fills every `CBCEvent` field.
  `native/host/host_scene.inc`: the WebGPU refusal. `native/host/host_api.inc`: the rejection entry and `"film"` in
  `events`. `HostOutput::write_render_tile`, the bake path and every unflagged host are unchanged.
- **Finding:** growing `CBCHostConfig` makes every positional initializer that ends with
  `{0}}` incomplete, and Apple Clang 21 reports "missing field 'flags' initializer [-Wmissing-field-initializers]", an
  error under the `-Wall -Wextra -Werror` (`/W4 /WX` on Windows) of every in-repository C consumer and of the oracle
  runner, on every platform. The ten initializers (`native/oracle/runner.cpp` and `abi_consumer.c`,
  `concurrency_consumer.c`, `consumer.c`, `example.c`, `gpu_consumer.c`, `pipeline_cache_consumer.c`, `reload_consumer.c`,
  `scene_edit_consumer.c`, `tier_consumer.c` under `native/tests/host`) gained the two trailing zeros, a mechanical change
  the build needs; `native/host/ABI.md` (Conventions and the minimal example) and the header comment say so for
  consumers. Only the macOS build compiled them (the Linux, Windows and Android gates build the same files: unverified).
- `native/tests/host/consumer.c` and `gpu_consumer.c` gain `--progressive` (the host gate's case): unknown flags and a
  nonzero `reserved2` refused, the 32-byte configuration accepted, then the lifecycle on flagged hosts with a callback
  that checks every film's size and sample order and keeps the last one, which must equal the returned film bit for bit;
  without the argument their behaviour and output are unchanged. `native/tools/macos_host_gate.py` runs them as
  `cpu-progressive` (films bitwise against the Session references) and `metal-progressive` (practical).
- Documents: `native/host/ABI.md` (the event table, a "Progressive results" section, the structs, enums, Exports row and
  Conventions), `AGENT-README.txt` (the constructors, the event, `IntermediateFilm`, `HostFlags`, the interop tiers and
  struct sizes, a "PROGRESSIVE RESULTS" part with an example, CALLBACKS AND THREADING, the rejection entry and its "Why.",
  WHAT THIS PACKAGE DOES NOT DO, the quick reference), `native/DEVICE.md` item 172 (the progressive bullet RESOLVED),
  `MAINTAINER-README.txt` NOTES, `docs/TESTING-MACOS-ARM64.md` step 3.
- Managed: `HostFlags` (new file), `Rendering/IntermediateFilm` (new file), `RenderEventKind.Film`, the constructor
  overloads, `CyclesAbi.Create(Backend, uint, HostFlags, out ulong)`, `CyclesScene.IntermediateFilmReceived` (the
  listener copies the native film and raises only this event for the film kind), `Native.HostConfig` (40 bytes; the
  existing constructors now pass 40 with no flags) and `Native.Event` (56 bytes).
- No file under `native/cycles`, `native/shaders`, `native/src` or `third_party/` changed.

**Provisional signatures** (each the maintainer's to approve):

| Signature | Reason | Alternative considered |
| --- | --- | --- |
| `CBCHostConfig.flags`, `.reserved2` (trailing; 40 bytes, 32 still accepted) and `CBC_HOST_PROGRESSIVE_RESULTS = 1` | Cycles reads `SessionParams::headless` once per Session, so the choice belongs to host creation; the trailing-field rule of ABI 1 | `reserved[0]` renamed (the `{0}` of existing initializers becomes a braced scalar); one global switch for every host (changes every host's scheduling); a creation export with a flags argument (+1 export) |
| `CBC_EVENT_FILM = 4` and `CBCEvent.film`, `.width`, `.height` (trailing; 56 bytes), sent only by flagged hosts, only during `cbc_host_render`, never after a cancel | the event callback's lifetime, threading and cancel rules carry over unchanged, and older consumers never see the kind | `cbc_host_set_film_callback` (+1 export, a second callback contract); films during bakes (no defined use; question in section 7) |
| On the WebGPU backend a flagged host is `CBC_UNSUPPORTED` at render and bake, "WebGPU progressive results not implemented" | kit rule 7; intermediate reads of the WGSL device's buffers were never run | refuse at `cbc_host_create` (earlier, but the other refusals are at render) |
| `[Flags] HostFlags : uint { None = 0, ProgressiveResults = 1 }` | the C flags | a `bool` parameter (not extensible) |
| `CyclesHost(Backend, uint, HostFlags)`, `CyclesScene(Backend, uint, HostFlags)`, `CyclesScene(CyclesBackend, uint, HostFlags)`, `CyclesAbi.Create(Backend, uint, HostFlags, out ulong)` | new overloads keep the existing signatures, which the host-gate fixtures and the Android app compile against | an optional parameter on the existing constructors (a binary break) |
| `RenderEventKind.Film = 4`; `event EventHandler<IntermediateFilm> CyclesScene.IntermediateFilmReceived`; `sealed record IntermediateFilm(FilmBuffer Film, uint SamplesDone, uint SamplesTotal, TimeSpan Elapsed)` | a copy outlives the callback; `RenderEventReceived` stays as it is | the film in `RenderProgress` or `RenderEvent` (changes existing records) |
| `Native.HostConfig` 40 bytes (`Flags`, `Reserved4`), `Native.Event` 56 bytes (`Film`, `Width`, `Height`) | the C structs | none |

**Count literals moved.** No export (61 stays). `native/host/ABI.md`: `CBCHostConfig` 32 to "40 (32 accepted)",
`CBCEvent` 40 to 56, events 1..3 to 1..4 and the host flag. `CapabilityReportTests` (`Events` gains `"film"`),
`NativeHeaderConstantMatching` (rows `CBC_EVENT_FILM` and `CBC_HOST_PROGRESSIVE_RESULTS`, `typeof(HostFlags)`,
`flagNoneMembers` 4 to 5, `HostFlags.None` zero), the host config size in `CyclesHost.cs` (32 to `sizeof`, 40) and
`CyclesAbi.cs` (`Create`). No gate script changed for gap 2 beyond the macOS gate's new modes; the ctypes contract
blocks of the gates still pass 32-byte configurations, which are accepted.

**Tests added** (managed total 389, was 375):

- `ProgressiveResultTests` (new, CPU, 10 cases): for a plain, an adaptive-sampling, a volume, a denoised and a
  camera-border scene the returned film of a progressive host is bit for bit a plain host's; at least two films arrive,
  the first after sample 1, in increasing sample order, of the film's size, none after `Render` returns and none
  through `RenderEventReceived`; the last film is the returned film bit for bit (the denoised scene included) and the
  first differs from it; every intermediate film of the border scene is zero outside the region and not inside it.
  Repeated renders and an edit after a render on one progressive host match a plain host bit for bit, with films for
  every render. A plain host sends no film; a bake on a progressive host sends none and equals a plain host's bake bit
  for bit. A film handler's call into the host is `NotReady`; a handler that throws cancels the render, leaves the
  destination untouched and the next render equals the first. A token cancelled at the first film stops the render
  with no further film. Unknown flags are refused by `CyclesHost` and `CyclesAbi` (`Invalid`, "Unknown configuration
  flags"); `CyclesAbi.Create` with the flag creates a host.
- `CapabilityReportTests.progressive_results_are_a_rejection_of_the_webgpu_backend_only` and the `Events` list.
- `NativeHeaderConstantMatching`: two rows and the enum count.
- `GpuRenderTests.progressive_results_render_on_metal_and_are_refused_by_name_on_webgpu`: the diffuse-sun validation
  scene on a progressive Metal host, judged against its existing recorded reference `diffuse-sun` (three runs: maximum
  1.8e-7 to 2.4e-7, 0 practical failures; no reference was recorded or changed) and against a plain Metal render in the
  same process (maximum 2.4e-7, 0 practical failures, 13 to 21 strict differences); its CPU comparison maximum 1.2e-6,
  0 practical failures. At least two films, the first after sample 1, the last at all samples and equal to the returned
  film bit for bit. On WebGPU it expects `Unsupported` with the message and no film.
- Host gate: modes `cpu-progressive` (12 film events; the three films bitwise equal to the Session references) and
  `metal-progressive` (14 film events; practical against the CPU references, maximum 4.5e-8, repeats within the strict
  criterion), both with the last film equal to the returned film bit for bit.

**Oracle scenes.** None: a progressive host's final film is the plain host's, so nothing of the feature is visible in a
film. The starter `starter/diffuse-triangle` was rendered on the three backends into
`<repo>/native/work/oracles/macos-arm64-d3-progressive-20261114/` (the stage's oracle input; Metal 0 practical
failures, maximum 3e-8).

**Rule 9 at gap 2.** The corpus of gap 1 (779 scenes: the 777 committed documents and the 2 bench documents), rendered on
`cpu-arm64-macos` alone into `<repo>/native/work/oracles/macos-arm64-d3-progressive-corpus-20261114/` (0 failures, every
film repeat-identical; 106 s): all 1,452 films of the base run, all 68 of the survey, the 10 D1 and 22 D2 films of the D2
corpus run and the 6 films of the three `images/` scenes are bitwise identical; nothing moved. The SHA-256 over every
film of the run (`ad22cf1ddade2af20d1c46ff9d34aef4cc4642ec0de43b90cd5bcef5cf7ee96b`) equals that of the gap-1 corpus
run. Pass films as at gap 1 (2,870 of 2,872 and 24 of 24; the two `RenderTime` passes differ). Host library
`3d35ac18b37d4965cb77bd6be8477666c74f1cec6fac4e7b9477925b7ca8dfd4`, runner
`74248f82c8e6f430e2ce819722c1330e037d3a92efd2810d8cc19a9f3a7aa43a` (rebuilt for its initializer).

**Build, gate, managed tests and the package.** `python3 tools/build_macos.py --jobs 8 --skip-dependencies`: passed, 0
warnings (three incremental builds, 18 to 25 s), the ten C consumers and the runner included. macOS host gate
(`<repo>/native/work/macos-validation-20261114e/`): passed (3,095 films compared, 0 same-host bitwise failures, Metal 0
practical failures, maximum 4.5e-8; 16 s). Testing document step 7 (run twice, the second time on the final tree):
restore, builds, pack and the Debug test exit 0; Debug 389 tests, 383 passed, 6 skipped (the WebGPU-only tests), 0
failed. The Release test failed only `RenderAsync_cancels_promptly_and_leaves_the_destination_untouched` again, in all
four full runs (14 to 28 s; 382 passed, 6 skipped); a test of a host without the flag, whose code path gap 2 does not
change (gap 1, section 7). The Release build's one warning is the pack driver's; Debug 0 warnings; the pack folders were
deleted. The consumer test of the MacOSArm64 package: a scratch stage (`<repo>/native/work/staging/osx-arm64-d3-progressive`)
packed as 1.0.318.1054 (323 files match, "NOT APPROVED, NOT PUBLISHABLE"); the five modes rendered on the CPU, Metal and
Metal with GPU denoising with the 292 kernel sources beside the library and none flattened; the negative control as at
gap 1; the cached runtime pack 10.0.15. `python3 tools/licensing/audit.py`: passed. `git diff --check`: clean.

**Unverified on other platforms.** Everything WebGPU: the refusal of a flagged host, the capability report's WebGPU list,
the WebGPU branch of the GPU test. The ten C initializers under the GCC 14 (Linux), clang-cl `/W4 /WX` (Windows) and
Android NDK builds, and the gates that build them. The Linux x64 build, gates and managed tests (the x64 CPU film of a
progressive host against a plain one), the Linux ARM64 CPU oracle, and the Windows and Android builds.

**What was left.** Film events during bakes (not sent; section 7). A progressive host renders in smaller batches (about
1 to 2 s), so a GPU render can take longer than on a plain host; not measured beyond the tests. The Release timing test
of gap 1.

#### Note: a cancel stops the batch being rendered (host fix after gap 2)

**Finding.** The Release failures of `CyclesSceneTests.RenderAsync_cancels_promptly_and_leaves_the_destination_untouched`
at gaps 1 and 2 are the host's: `cbc_host_cancel` set only the flag that the session's Progress cancel callback reads
between batches of samples. The path tracer's own flag, which the CPU work reads per pixel and sample and the GPU work per
kernel step (`integrator/path_trace_work_cpu.cpp`, `path_trace_work_gpu.cpp`), is set only by `PathTrace::cancel`, which
the host never called. A headless session renders one sample, then a batch sized to about 30 seconds of samples
(`RenderScheduler::guess_display_update_interval_in_seconds`), so a cancel that arrived during that batch waited for it
(the 13 to 29 s of the failures). The diagnosis and its run log are in `<repo>/native/work/partD-20261114/cancel-investigation/`
(scratch, not in this folder). Not a regression of D1, D2 or D3.

**What was done** (`native/host/cycles_host.cpp`, `native/host/host_bake.inc`; no new export or signature):

- `HostSession`, a subclass of Cycles' `Session` whose `cancel_path_trace()` calls the protected `path_trace_->cancel()`.
  Not `Session::cancel(true)`: it also cancels the device, and `MetalDevice::cancel` removes the device from Metal's active
  list, which drops its pending kernel compilations for the rest of its life, while the host's session renders again after
  a cancel.
- `Host::request_cancel()`, the one route of every cancel request (`cbc_host_cancel`, `cbc_host_destroy`, the exit hook
  and an event callback's nonzero return): it sets the flags under a new lock and signals a condition variable.
- `CancelWatch`, a thread that spans the session's work of every render and bake: created before `session.start()` (a
  thread that cannot start fails the call before anything renders) and joined after `session.wait()`. Until a request it
  only waits, so a render that is not cancelled runs as before; after one it calls `cancel_path_trace()` and repeats it
  every 5 ms until the session has finished, because `PathTrace::cancel` clears its request as soon as no batch is running
  (a request just before a batch starts would otherwise be lost).
- Review of the applied patch: the lock order is event lock before cancel lock (a callback's request) and host-table lock
  before cancel lock (destroy, exit hook); the watch holds no lock while `PathTrace::cancel` waits for the session thread,
  and nothing on the session thread waits for the watch. The watch refers to the host, which the render keeps alive, and
  to its session, which `cbc_host_destroy` and the exit hook reset only after the render has released the host; both
  still call `Session::cancel(true)` then, as before. The patch's one change on review: the watch now starts before
  `session.start()` instead of after it, so no exception can leave the session thread rendering. Every backend:
  `PathTrace::cancel` is Cycles' own quick cancel (`Session::reset` already calls it before every render); the GPU work,
  which the WebGPU device's queue drives as well (`native/cycles/queue.cpp`), stops between kernel steps after a queue
  synchronisation, and the next render resets its path states (`enqueue_reset`). WebGPU is read, not run.
- The sample count after a cancel (kit rule 5). Cycles counts a batch's samples when it schedules the batch, so after a
  cancel during a batch the progress and status events, and the managed `Cancelled` report, carried the scheduled count:
  131,073 for one sample done in the new CPU test below. The host now sends no progress event after a cancel request, and
  its status events carry the `samples_done` of the last progress event (0 if none), a count of finished samples. Chosen
  over documenting the scheduled count because `samples_done` is documented as completed samples; the samples a stopped
  batch did finish are not known to the session (the CPU work stops per pixel, the GPU work per kernel step).
- Documents: `native/host/ABI.md` (threading, and the event rule for `samples_done`), `native/host/README.md` (the
  callbacks row), `AGENT-README.txt` and `RenderProgress` (the count after a cancellation).

**Tests added** (managed total 391, was 389): `CyclesSceneTests.RenderAsync_cancelled_during_a_batch_stops_it_and_reports_only_the_samples_completed_before`
(CPU) and `GpuRenderTests.gpu_render_cancelled_during_a_batch_stops_it_and_the_scene_renders_again_afterwards` (Metal;
WebGPU unverified): a cancel 300 ms after the first sample count, during the second batch, stops the render well within
the existing test's 10 s (the whole CPU test takes 0.4 s), the destination is untouched and the `Cancelled` report
carries the last count before the cancel; on Metal the scene then renders again within the practical criterion of its
recorded reference `diffuse-sun`. With the patch alone the CPU test failed ("Expected reports.Last().CompletedSamples
to be 1u, but found 131073u"); with the count fix both pass.

**Verification.** Build: 0 warnings. macOS host gate (`<repo>/native/work/macos-validation-20261114f/`): passed (3,095
films compared, 0 same-host bitwise failures, Metal 0 practical failures, maximum 4.5e-8; 17 s). Testing document step 7:
Release 391 tests, 385 passed, 6 skipped (the WebGPU-only tests), 0 failed, in each of three full runs (30 s each), so
`RenderAsync_cancels_promptly_and_leaves_the_destination_untouched` passed 3 of 3; Debug the same counts; the Release
build's one warning is the pack driver's; the pack folder was deleted. Rule 9: the corpus (779 scenes) on
`cpu-arm64-macos` alone into `<repo>/native/work/oracles/macos-arm64-d3-cancel-corpus-20261114/` (0 failures, every film
repeat-identical; 125 s): all 1,452 films of the base run, 68 of the survey, 10 of D1, 22 of D2 and 6 of the UDIM scenes
are bitwise identical, and the SHA-256 over every film equals that of gaps 1 and 2 (`ad22cf1d...`); pass films as at gap 1
(the two `RenderTime` passes differ). Host library `7bc45eb1024c3ffd98701c7ee5d8b55a59c84e6fbdb3d659581a1e753b9ca528`,
runner unchanged. The consumer test of the MacOSArm64 package as at gap 2 (scratch stage
`<repo>/native/work/staging/osx-arm64-d3-cancel`, packed as 1.0.318.1160, 323 files, "NOT APPROVED, NOT PUBLISHABLE"; the
five modes on the CPU, Metal and Metal with GPU denoising, the 292 kernel sources beside the library, none flattened; the
negative control; the cached runtime pack 10.0.15). `python3 tools/licensing/audit.py`: passed. `git diff --check`: clean.

**Unverified on other platforms.** A cancel during a batch on the WebGPU device (the GPU work's loop, read only), and the
Linux, Windows and Android builds of the host.

#### Gap 3: the texture limit

**Design.** `SceneParams::texture_limit` (`scene/scene.h`, default 0) is read when the image manager loads an image,
`ImageManager::device_load_image` (`scene/image.cpp`): for a full image whose larger side exceeds the limit the resolution
scale becomes limit / side, `ImageCache::load_full` (`scene/image_cache.cpp`) rounds it down to a power of two, and
`util_image_resize_pixels` (`util/image_impl.h`) box-filters the image on the host, each side times the scale, rounded
down, at least 1 pixel. So a 64-pixel image under a limit of 40 loads as 32, and it applies to every image of the image
manager, the Sky Texture's builtin 512 x 256 image included. It is host work before any device sees the image, so every
device has it, Metal included; and it is the remedy for Metal's own limit, `MetalDevice::image_alloc` refusing a side
above 16384 ("Texture exceeds maximum allowed size of 16384 x 16384", `device/metal/device_impl.mm`). An image already
loaded is not loaded again (`need_load` is false after the first load, and `add_image_texture` returns the existing image
for an equal loader), so a limit changed after a render would leave the loaded images at the old size and an edited host
would not render like a new one.

- Native: a new export, `cbc_host_texture_limit(host, max_size)` (**PROVISIONAL**), the pattern of
  `cbc_host_curve_subdivisions` (a host-level setting that is not a node socket, no getter; the managed layer keeps the
  value). It sets `scene.params.texture_limit`. 0 to `INT_MAX` (Cycles' `int`), else `CBC_INVALID`. The same value is
  always accepted; a different one is `CBC_NOT_READY` ("The texture limit is fixed once a render or bake has loaded
  images ...") once a render or bake has loaded images. The host records that after each render and bake from the image
  manager's device image table (`DeviceScene::image_textures`, which `ImageManager::device_update` sizes when it loads
  images): a render that loaded none (no texture, no sky), or that stopped before the device update, leaves the limit
  free.
- WebGPU (kit rule 7): the limit is host-side image processing and the WGSL device would only see smaller images, but it
  has not been run with them, and nothing WebGPU runs here. The host refuses a limit other than 0 on `CBC_HOST_WEBGPU` at
  render and bake (`validate_unavailable`, `CBC_UNSUPPORTED`, "WebGPU texture limit not implemented"); the capability
  report, AGENT-README "RENDER-TIME REJECTIONS" and `native/host/ABI.md` carry "a texture limit (images reduced when they
  load)". No file under `native/cycles` changes.
- Managed (**PROVISIONAL**): `uint CyclesScene.TextureLimit { get; set; }` (above `int.MaxValue`
  `ArgumentOutOfRangeException`; `NotReady` becomes `InvalidOperationException`, as the other refused edits do),
  `CyclesHost.SetTextureLimit(uint)`, `Native.TextureLimit`. Scene documents: `render.textureLimit` (runner and managed
  loader, set before any image binds).
- Export count 61 to 62.

**What was done.** As designed; the build records `exports: 62`.

- `native/host/cycles_host.cpp`: `Host::images_loaded`, set after each render's and bake's session from the device image
  table (also in `native/host/host_bake.inc`); the export. `native/host/host_scene.inc`: the WebGPU refusal.
  `native/host/host_api.inc`: the rejection entry. `native/host/cycles_host.h`: the declaration and its PROVISIONAL
  comment; the three export lists.
- `native/oracle/runner.cpp`: `render.textureLimit` (0 to `INT_MAX`, else the runner fails naming the key).
- Documents: `native/host/ABI.md` (the Images row, "Device errors", the Exports table and its count), `AGENT-README.txt`
  (the member, `CyclesHost.SetTextureLimit`, a "TEXTURE LIMIT" part of SCENE SETTINGS with an example, the rejection entry
  and its "Why.", WHAT THIS PACKAGE DOES NOT DO, the quick reference), `native/DEVICE.md` item 172 (the texture-limit
  bullet RESOLVED), `MAINTAINER-README.txt` NOTES (the export map), `native/oracle/README.md` (the key and the scenes),
  `docs/TESTING-MACOS-ARM64.md` step 3 (the gate's new contract checks).
- No file under `native/cycles`, `native/shaders`, `native/src` or `third_party/` changed.

**Provisional signatures** (each the maintainer's to approve):

| Signature | Reason | Alternative considered |
| --- | --- | --- |
| `int cbc_host_texture_limit(CBCHost, uint32_t max_size)`; 0 to `INT_MAX`; `CBC_NOT_READY` for a change once a render or bake has loaded images | the `cbc_host_curve_subdivisions` pattern; Cycles never loads an image again, so a later change could not apply as for a new scene | one settings struct for the texture limit, BVH spatial splits and tiles (one export, but not the existing pattern, and three independent changes coupled); a field of `CBCHostConfig` (fixed at creation, stricter than needed); re-adding every image with the limit in the loader's equality (larger, and still partial: Cycles' own images) |
| The rule "fixed once images have loaded", read from the device image table after each render and bake | precise for scenes without images, which can still change it | fixed after any render (simpler, refuses changes that are harmless) |
| On the WebGPU backend a limit other than 0 is `CBC_UNSUPPORTED` at render and bake, "WebGPU texture limit not implemented" | kit rule 7; the device was never run with reduced images | none (lifting it needs a WebGPU run) |
| `uint CyclesScene.TextureLimit { get; set; }`, `CyclesHost.SetTextureLimit(uint)`, `Native.TextureLimit` | a property, as `RenderTimeLimit` | `SetTextureLimit` on the scene, as `SetCurveSubdivisions` (no read-back) |
| Scene documents: `render.textureLimit` | beside `render.curveSubdivisions` | a node entry (it is no node) |

**Count literals moved.** The export count 61 to 62: `native/host/ABI.md` ("## Exports (ABI 1, 62 functions)"), the
lists `exports.def`, `exports.macos`, `exports.map`, and `native/tools/macos_host_gate.py` (run here),
`native/tools/host_gate.py` (twice), `macos_x64_host_gate.py`, `windows_host_gate.py`, `linux_arm64_host_gate.py` and
`android_host_gate.py` (by reading only, unverified). `git grep -n -w 61` of `native/host`, `native/tools`, `tools`, `tests`,
`src` and the documents finds no other export count. The registry counts did not move; the capability report's WebGPU list
gained one entry.

**Tests added** (managed total 399, was 391):

- `TextureLimitTests` (new, CPU, 5 cases), on a quad filling an orthographic view, an emission of an image texture with
  Linear interpolation, one sample: a 64 x 64 byte4 image of uniform 2 x 2 blocks under a limit of 32 renders bit for bit
  like the 32 x 32 image of the block colours and unlike the full image, and so does a limit of 40 (the power-of-two
  rounding), while 64 and 0 keep the full image; a 64 x 64 float4 image of uniform 4 x 4 blocks with values up to 6 under
  limits of 16 and 31 renders bit for bit like the 16 x 16 image (binary fractions, so the box filter is exact); a
  Multiple Scattering sky seen through an equirectangular camera differs under a limit of 8 (Cycles' own image is reduced)
  and repeats bit for bit without; the limit changes freely after a render of a scene without images and then applies to
  an image bound later, bit for bit like a new scene, while after a render that loaded an image a different value throws
  `InvalidOperationException` (and `CyclesHost` `NotReady` with the message) and the same value is accepted; above
  `int.MaxValue` `ArgumentOutOfRangeException` (facade) and `Invalid` (`CyclesHost`); the value reads back.
- `CapabilityReportTests.a_texture_limit_is_a_rejection_of_the_webgpu_backend_only`; the word-for-word test covers the
  entry.
- `OracleSceneBundleTests.a_committed_texture_limit_scene_sets_the_limit_through_the_loader`: `images/texture-limit-blocks`
  through the loader renders bit for bit like its committed twin and unlike the document without the key; the scene's
  `TextureLimit` reads 32.
- `GpuRenderTests.texture_limit_brings_an_oversized_image_within_metal_and_is_refused_by_name_on_webgpu`: a smooth
  16385 x 1 byte4 gradient on a lit quad behind the triangle, which Metal refuses without a limit ("Texture exceeds maximum
  allowed size of 16384 x 16384 (requested: 16385 x 1)"), renders with a limit of 16384 (8192 x 1); judged by the
  reference policy against the new film `References/metal-osx-arm64/texture-limit.rgba.f32` (recorded with only this test
  selected; the manifest gained only this entry; three later runs: maximum 1.2e-7 from it, 0 practical failures); its CPU
  comparison maximum 2.3e-5, 0 practical failures, 207 strict differences of 6,144 components. On WebGPU it expects
  `Unsupported` with the message.
- Host gate: the contract block of `native/tools/macos_host_gate.py` gains `textureLimit`: above `INT_MAX` 1, 0 and 1024
  0 before the UDIM render, then 1024 again 0 and 512 5.

**Oracle scenes.** Three new documents in the `images/` family, written by `author_images.py` (`--check` passes for the six
documents and their 15 raw files; `author_starters.py` rewrites its files unchanged), on the starter textured plane with
UVs 0 to 1 and Linear interpolation (48x32, 8 samples; `native/oracle/README.md`): `texture-limit-blocks` (a 64x64 byte4
image of uniform 2x2 blocks, `render.textureLimit` 32), `texture-limit-blocks-halved` (its twin without a limit, the
32x32 image of the block colours) and `texture-limit-checker` (a 64x64 checker of 16x16 squares, each a checker of single
pixels 24 levels lighter and darker, limit 16: the single-pixel pattern averages away). Run
`<repo>/native/work/oracles/macos-arm64-d3-texture-limit-20261114/` (scratch), `starter/diffuse-triangle` first, on
`cpu-arm64-macos`, `metal-m2pro` and `metal-m2pro-metalrt` (15 s): 0 failures, every CPU film repeat-identical, Metal
repeats within 3e-8. The CPU films of `texture-limit-blocks` equal those of `texture-limit-blocks-halved` bit for bit; on
both Metal backends the two scenes agree within 3e-8 (0 practical failures). Every comparison reported:

| Scene | BVH2 vs CPU | RT vs CPU | RT vs BVH2 |
| --- | --- | --- | --- |
| `starter/diffuse-triangle` | 0 pf, max 3e-08 | 0 pf, max 3e-08 | 0 pf, max 1.5e-08 |
| `images/texture-limit-blocks` | 82 pf, max 1.7e-04 | 81 pf, max 1.7e-04 | 1 pf, max 6.7e-05 |
| `images/texture-limit-blocks-halved` | 82 pf, max 1.7e-04 | 81 pf, max 1.7e-04 | 1 pf, max 6.7e-05 |
| `images/texture-limit-checker` | 68 pf, max 1.6e-04 | 68 pf, max 1.6e-04 | 0 pf, max 1.5e-08 |

"pf" as in section 4.1, of 6,144 components. **INFERENCE:** the CPU-against-Metal failures are Metal's hardware bilinear
filtering of the linearly filtered textures, as for `udim-checker-linear` at gap 1; the limited scene and its twin give
the same counts, so the limit adds none. The one failure between the two Metal backends in the block scenes (also in the
twin without a limit) is a hit point of MetalRT and BVH2 that differs in its last bits at a block edge of the 32x32 image
(**INFERENCE**, not traced).

**Rule 9 at gap 3.** The whole committed corpus (780 documents: the 777 of gap 2 plus the 3 new ones) and the 2 bench
documents, on `cpu-arm64-macos` alone into `<repo>/native/work/oracles/macos-arm64-d3-texture-limit-corpus-20261114/` (782
scenes, 0 failures, every film repeat-identical; 110 s). Compared by scene path and film index: all 1,452 films of the base
run, 68 of the survey, 10 of D1, 22 of D2 and 6 of the UDIM scenes are bitwise identical, and so are all 1,558 films of
the 779 scenes it shares with the corpus run of the cancel fix; nothing moved. The 6 films of the 3 new scenes equal those
of the run above. Pass films as at gap 1 (the two `RenderTime` passes differ). Host library
`788014a4a0161b732a4c078f913e969bffde714e9ab716d0bc44c09e713a8a9c`, runner
`0a0d4c60cd3cec3bee71797216b4e3b4145d86af032a011d8f0e1eb02af6775b` (rebuilt for `render.textureLimit`).

**Build, gate, managed tests and the package.** `python3 tools/build_macos.py --jobs 8 --skip-dependencies`: passed, 0
warnings (incremental builds of 15 to 25 s). macOS host gate (`<repo>/native/work/macos-validation-20261114g/`): passed
(3,095 films compared, 0 same-host bitwise failures, Metal 0 practical failures, maximum 4.5e-8; 17 s), with the new
contract checks. Testing document step 7: Release 399 tests, 393 passed, 6 skipped (the WebGPU-only tests), 0 failed, in
each of three full runs (29 to 31 s), the cancel test among them; Debug the same counts; the Release build's one warning
is the pack driver's; the pack folder was deleted. The consumer test of the MacOSArm64 package as at the cancel fix
(scratch stage `<repo>/native/work/staging/osx-arm64-d3-texture-limit`, packed as 1.0.318.1189, 323 files, "NOT
APPROVED, NOT PUBLISHABLE"; the five modes on the CPU, Metal and Metal with GPU denoising, 292 kernel sources beside the
library, none flattened; the negative control; the cached runtime pack 10.0.15; the stage, the feed copy and the app
copies were deleted afterwards). `python3 tools/licensing/audit.py`: passed. `git diff --check`: clean.

**Unverified on other platforms.** Everything WebGPU: the refusal, the capability report's WebGPU list and the WebGPU
branch of the GPU test; whether the WGSL device renders reduced images right (never run). The count edits of
`host_gate.py`, `macos_x64_host_gate.py`, `windows_host_gate.py`, `linux_arm64_host_gate.py` and `android_host_gate.py`
(read, not run). The runner's new `<climits>` include under the Linux, Windows and Android compilers. The Linux x64 build,
gates and CPU oracle (the x64 films of the three new scenes), the Linux ARM64 CPU oracle of them, and the Windows and
Android builds.

**What was left.** The capability report's `limits.maxImageDimension` still states `INT_MAX`, while Cycles' Metal device
refuses a side above 16384 (section 7). `SceneParams::texture_resolution`, Cycles' other image scale, is not exposed (the
ruling names the limit only). Tiled images skip mip levels under the limit instead (`miplevel_offset`); they come with the
texture cache gap.

#### Gap 4: BVH spatial split

**Design.** `SceneParams::use_bvh_spatial_split` (`scene/scene.h`, default false) feeds `BVHParams::use_spatial_split` of
every object BVH (`Geometry::compute_bvh`, `scene/geometry_bvh.cpp`) and of the scene BVH
(`GeometryManager::device_update_bvh`). The BVH2 builder switches it off for the top level, for objects with motion blur
and for curves with unaligned nodes (`bvh/build.cpp`); Embree reads it as its build quality, high instead of medium for a
static scene (`bvh/embree.cpp`), so on `CBC_HOST_CPU` it applies to every BVH, the top level included. With this host's
BVH2 (`BVH_TYPE_STATIC`) an object BVH exists only for geometry Cycles keeps in object space (`Geometry::is_instanced`:
used by several objects, or with true displacement, subsurface scattering or subdivision; `scene/geometry.cpp`), so on the
CPU reference and on Metal's BVH2 the splits reach only that geometry. Metal's BVH2 (no MetalRT, the default on this M2
Pro) builds through the generic `Device::build_bvh` and honours it; MetalRT never reads it (no use in
`device/metal/bvh.mm`). It changes how rays find the closest hit, not which one, so by design no film changes; the
survey's row 11 question about BVH time steps applies here too (section 7).

Two facts of the pinned source shape the rebuild. A BVH keeps the parameters it was created with: the scene BVH object
survives between renders unless the geometry manager reallocates its data (`GeometryManager::device_update_preprocess`
drops it then), so changing the parameter alone would leave the top level, which Embree builds with the parameter, as it
was. And tagging a geometry modified, as the host's device recovery does, runs its true displacement again
(`GeometryManager::displace` adds the offset to the current, already displaced positions, `scene/mesh_displace.cpp`), so
a rebuild must not touch the geometry. The host therefore sets `need_update_bvh_for_offset` and `need_update_rebuild` on
every geometry but the lights (`GeometryManager::device_update` rebuilds the BVH of such a geometry without displacing or
tessellating it again, both of which need `is_modified`) and tags the geometry manager `GEOMETRY_ADDED`, the flag of a
geometry added after a render, whose reallocation drops the scene BVH so it is created again with the new parameter.

- Native: a new export, `cbc_host_bvh_spatial_split(host, enabled)` (**PROVISIONAL**), the pattern of
  `cbc_host_curve_subdivisions`: 0 or 1, else `CBC_INVALID` ("BVH spatial split must be 0 (off) or 1 (on)"); the same value
  changes nothing; a change has every BVH built again at the next render or bake.
- WebGPU (kit rule 7, and a reason of its own): a split BVH2 references one primitive from several leaves, and the WGSL
  traversal, its handling of a primitive met twice (as in transparent shadows) included, has not been run with one. The
  host refuses it enabled on `CBC_HOST_WEBGPU` at render and bake (`validate_unavailable`, `CBC_UNSUPPORTED`, "WebGPU BVH
  spatial splits not implemented"); the capability report, AGENT-README "RENDER-TIME REJECTIONS" and `native/host/ABI.md`
  carry "BVH spatial splits". No file under `native/cycles` changes.
- Managed (**PROVISIONAL**): `bool CyclesScene.UseBvhSpatialSplit { get; set; }`, `CyclesHost.SetBvhSpatialSplit(bool)`,
  `Native.BvhSpatialSplit`. Scene documents: `render.bvhSpatialSplit` (a boolean; runner and managed loader).
- Export count 62 to 63.

**What was done.** As designed; the build records `exports: 63`.

- `native/host/cycles_host.cpp`: the export. `native/host/host_scene.inc`: the WebGPU refusal. `native/host/host_api.inc`:
  the rejection entry. `native/host/cycles_host.h`: the declaration and its PROVISIONAL comment; the three export lists.
- `native/oracle/runner.cpp`: `render.bvhSpatialSplit` (a boolean, else the runner fails naming the key; checked on two
  scratch documents under `<repo>/native/work/macos-arm64-d3-bvh-spatial-split-20261114/`: the starter triangle with the
  key renders the starter's CPU film bit for bit, and the key set to 1 fails with "render.bvhSpatialSplit must be true or
  false").
- Documents: `native/host/ABI.md` (a BVH row in Scene authoring, the Exports table and its count), `AGENT-README.txt` (the
  member, `CyclesHost.SetBvhSpatialSplit`, a "BVH SPATIAL SPLITS" part of SCENE SETTINGS, the rejection entry and its
  "Why.", WHAT THIS PACKAGE DOES NOT DO, the quick reference), `native/DEVICE.md` item 172 (the spatial-split bullet
  RESOLVED), `MAINTAINER-README.txt` NOTES (the export map), `native/oracle/README.md` (the key),
  `docs/TESTING-MACOS-ARM64.md` step 3 (the gate's new contract checks).
- No file under `native/cycles`, `native/shaders`, `native/src` or `third_party/` changed.

**Provisional signatures** (each the maintainer's to approve):

| Signature | Reason | Alternative considered |
| --- | --- | --- |
| `int cbc_host_bvh_spatial_split(CBCHost, uint32_t enabled)`; 0 or 1 | the `cbc_host_curve_subdivisions` pattern | a settings struct with the texture limit and tiles (not the existing pattern); a socket-like "scene_params" pseudo node (not a Cycles node, the registry would lie); not exposing it at all (it changes no film; decision 17 names it) |
| A change rebuilds every BVH through `need_update_bvh_for_offset` and `GEOMETRY_ADDED` | the scene BVH and Embree's top level keep their build parameters otherwise; a geometry tagged modified would be displaced twice | the device recovery's tagging (`tag_modified` of every geometry), which displaces twice; refusing a change after a render, as for the texture limit (stricter than needed) |
| On the WebGPU backend it is `CBC_UNSUPPORTED` at render and bake when enabled, "WebGPU BVH spatial splits not implemented" | kit rule 7; the WGSL traversal was never run with a split BVH2 | none (lifting it needs a WebGPU run) |
| `bool CyclesScene.UseBvhSpatialSplit { get; set; }`, `CyclesHost.SetBvhSpatialSplit(bool)`, `Native.BvhSpatialSplit` | a property, as `RenderTimeLimit` and `TextureLimit` | a method on the scene |
| Scene documents: `render.bvhSpatialSplit` | beside `render.textureLimit` | none |

**Count literals moved.** The export count 62 to 63 at the same places as gap 3: `native/host/ABI.md`, the three lists,
`native/tools/macos_host_gate.py` (run here), `host_gate.py` (twice), `macos_x64_host_gate.py`, `windows_host_gate.py`,
`linux_arm64_host_gate.py` and `android_host_gate.py` (by reading only, unverified). `git grep -n -w 62` finds no other
export count. The capability report's WebGPU list gained one entry.

**Tests added** (managed total 407, was 399):

- `BvhSpatialSplitTests` (new, CPU, 5 cases), on a scene that gives the splits work: a fan of 48 long thin triangles, half
  transparent, used by two objects (so it has its own BVH), a floor (single user, in the scene BVH), a 12 x 12 grid with
  true displacement (its own BVH; a rebuild must not displace it again), a thick curve and three points, lit by a point
  lamp through the slivers (transparent shadows), 64 x 48, 8 samples: the setting reads back, and the status-code tier
  refuses 2 with the message and accepts 1 and 0; the CPU reference renders the same film bit for bit with and without
  splits, repeat-identically; the Embree backend renders it with high build quality within the practical criterion
  (measured: bit for bit too); after a render, a change has the next render build every BVH again (its status events
  name "Updating Geometry BVH" for the two object BVHs and "Updating Scene BVH", which a render without a change, or after
  setting the same value, does not), and that film equals a new scene built with splits bit for bit, and switching back
  equals the first film; the same on the Embree backend, bit for bit.
- `CapabilityReportTests.bvh_spatial_splits_are_a_rejection_of_the_webgpu_backend_only`; the word-for-word test covers the
  entry.
- `OracleSceneBundleTests.the_bvh_spatial_split_key_switches_the_setting_and_changes_no_film`: the loader sets the
  setting, the film equals the document's without the key bit for bit, and a number for the key is refused.
- `GpuRenderTests.bvh_spatial_splits_render_on_metal_and_are_refused_by_name_on_webgpu`: the same scene with splits on
  Metal (BVH2), judged by the reference policy against the new film `References/metal-osx-arm64/bvh-spatial-split.rgba.f32`
  (recorded with only this test selected; the manifest gained only this entry; three later runs: maximum 3e-8 from it, 0
  practical failures); against the same scene without splits in the same process maximum 3e-8, 0 strict differences; a
  scene switched to splits after a render against the one built with them maximum 3e-8; its CPU comparison maximum
  1.5e-5, 0 practical failures, 3,511 strict differences of 12,288 components. On WebGPU it expects `Unsupported` with the
  message.
- Host gate: the contract block gains `bvhSpatialSplit`: 2 refused (1), 1 accepted after the UDIM render, a render with
  them 0 and finite, then 0 accepted.

**Not verified: that the BVH itself changes.** The product library has no switch for Cycles' log, which reports BVH
statistics, so no node count was read; **INFERENCE** from `bvh/build.cpp` that the fan's BVH2 differs with splits. What the
tests show is that every BVH is built again, from Cycles' own status messages, and that no film changes.

**Oracle scenes.** None: by design nothing of the feature is visible in a film (the CPU films are bit for bit the same),
so a committed twin would only repeat a film. The starter `starter/diffuse-triangle` was rendered on the three backends
into `<repo>/native/work/oracles/macos-arm64-d3-bvh-spatial-split-20261114/` for the stage of the consumer test (Metal 0
practical failures, maximum 3e-8).

**A finding outside the gap: an edit of a displaced mesh after a render displaces it twice.** `GeometryManager::displace`
adds the displacement to the mesh's current positions, and the host restores authored positions only for geometry the
static BVH path baked (`host_edits.inc`), which never includes a mesh with true displacement. Measured with a scratch test
(not kept; its source is `<repo>/native/work/macos-arm64-d3-bvh-spatial-split-20261114/displacement-probe.cs.txt`): a
12 x 12 grid with true displacement whose `smooth` socket is set after a render renders unlike a new scene built with
the same `smooth` (165 practical failures of 6,144 components, maximum 0.19), while the first render equals a new scene
bit for bit. **INFERENCE:** the device recovery (`recover_device`), which tags every geometry modified, does the same. This
contradicts `native/host/ABI.md` "Edits after a render" for such meshes; nothing was changed (section 7). The spatial-split
rebuild avoids it, as the toggle test above shows.

**Rule 9 at gap 4.** The corpus of gap 3 (782 scenes: 780 committed documents and the 2 bench documents), on
`cpu-arm64-macos` alone into `<repo>/native/work/oracles/macos-arm64-d3-bvh-spatial-split-corpus-20261114/` (0 failures,
every film repeat-identical; 111 s): all 1,452 films of the base run, 68 of the survey, 10 of D1, 22 of D2, 6 of the UDIM
scenes and 6 of the texture-limit scenes are bitwise identical, and all 1,564 films equal those of the gap-3 corpus run
(the SHA-256 over every film, `8aad4249...`, is the same); nothing moved. Pass films as at gap 1. Host library
`54fe0882738819a6aeed54e1515969783122888da28ec4862efe5b805abb2a11`, runner
`daa5f1ac127d72a5f40aabf9ba2546377124018d2379c1a7b06f21e46ef7e086` (rebuilt for `render.bvhSpatialSplit`).

**Build, gate, managed tests and the package.** Build: passed, 0 warnings (23 s). macOS host gate
(`<repo>/native/work/macos-validation-20261114h/`): passed (3,095 films compared, 0 same-host bitwise failures, Metal 0
practical failures, maximum 4.5e-8; 16 s), with the new contract checks. Testing document step 7: Release 407 tests, 401
passed, 6 skipped (the WebGPU-only tests), 0 failed, in each of three full runs (31 to 35 s), the cancel test among them;
Debug the same counts; the Release build's one warning is the pack driver's; the pack folder was deleted. The consumer
test as at gap 3 (scratch stage `<repo>/native/work/staging/osx-arm64-d3-bvh-spatial-split`, packed as 1.0.318.1209, 323
files, "NOT APPROVED, NOT PUBLISHABLE"; the five modes on the CPU, Metal and Metal with GPU denoising, 292 kernel sources
beside the library, none flattened; the negative control; the cached runtime pack 10.0.15; the scratch copies were deleted
afterwards). `python3 tools/licensing/audit.py`: passed. `git diff --check`: clean.

**Unverified on other platforms.** Everything WebGPU (the refusal, the capability list, the GPU test's WebGPU branch) and
MetalRT with splits (the managed Metal test ran BVH2, this machine's default; MetalRT does not read the setting). The count
edits of the five other gate scripts (read, not run). The Linux x64 build, gates and managed tests (whether the x64 CPU
reference is bit for bit the same with splits), and the Windows and Android builds.

**What was left.** The BVH node counts (above). BVH time steps stay unexposed (survey row 11). The double displacement of
an edited displaced mesh (above, section 7).

#### Gap 6: the texture cache (tiled, mip-mapped images)

Gap 5 (tiled rendering) waited for the maintainer's ruling and was made after this gap (below); gap 6 was made after
gap 4, so the export count moves from 63 to 64 (the design memo counted from 64).

**Design.** Cycles' texture cache (`scene/image_cache.cpp`) is Cycles' own, not OIIO's TextureSystem: for an image whose
loader reports tiles and mip levels (`ImageMetaData::has_tiles_and_mipmaps`, `tile_size`) and with
`SceneParams::use_texture_cache` on, `ImageManager::device_load_image` (`scene/image.cpp`) sets up one tile descriptor per
tile of every level down to one tile (`ImageCache::load_image_tiled`), and pixels load on demand through the virtual
`ImageLoader::load_pixels_tile(metadata, miplevel, x, y, w, h, x_stride, y_stride, padding, extension, pixels)`
(`scene/image_loader.h`), into a pool image of 64 tiles, each with a 2-pixel border (`ImageCache::load_tile`). The kernel
picks a level per sample from the ray footprint, stochastically within half a level, plus the mip bias
(`kernel_image_tile_map`, `kernel/util/image_2d.h`); its tile rows run top down ("Flip Y convention for tiles to match tx
files"). A missing tile is loaded at once by the CPU kernel's thread (`ImageManager::device_cpu_load_requested`); on a GPU
the kernel marks a request and a cache miss, the path restarts and `PathTraceWorkGPU` loads the requested tiles between
kernel steps (`ImageManager::device_gpu_load_requested`, a `parallel_for`). A tile that fails to load reads the node's
Missing colour (`KERNEL_TILE_LOAD_FAILED`). Only `OIIOImageLoader` reads files; no part of the cache needs one. Metal has
no code of its own here (the generic GPU path; its pool image width `(tile + 4) * 64` must stay at or below 16384, so tiles
above 128 cannot work there). `ImageManager` copies `use_texture_cache` once, at construction (`scene/image.cpp`).

- Native: a new export `cbc_host_image_create_tiled(host, config, read, release, user_data, image)` (**PROVISIONAL**) with
  two callback types, `CBCImageTileReadCallback` (one rectangle of one level, rows bottom first, the ABI's order) and
  `CBCReleaseCallback`. The handle is a `MemoryImageData` with a `TiledImageSource` (the callbacks); `prepare_graphs` hands
  Cycles a host `TiledImageLoader` whose `load_metadata` reports 64-pixel tiles and mip levels (an image smaller than 64 in
  both sides is plain to Cycles and loads whole through `load_pixels`), and whose `load_pixels_tile` converts Cycles'
  top-down tile rows to the caller's bottom-up rows, asks for the tile with its border, converts to Cycles' storage type and
  conforms the pixels (`ImageMetaData::conform_pixels`). The border: the layout of `OIIOImageLoader::load_pixels_tile`
  (`scene/image_oiio.cpp`), filled pixel by pixel, a pixel inside the level from the level and one outside it by the
  texture's extension with the kernel's own wrap rules for full images (clamp, periodic, mirror, zero for Clip). Read at the
  pin, the file loader fills whole neighbouring regions: it writes zero next to an adjacent tile one pixel wide, and for
  Repeat reads before its buffer when a level's width is one more than a multiple of 64; the pixel rule has neither case.
- `use_texture_cache` on for every host at `cbc_host_create` (`auto_texture_cache` off): only a loader that reports tiles
  takes the cached path, so every other image is unchanged (**INFERENCE**, proved by the rule-9 run below). The memo's
  alternative, the creation-time flags of gap 2 (`CBCHostConfig.flags`), was not needed: the setting changes nothing for
  hosts without tiled images, and a flag would make every consumer that wants tiled images create its host differently.
- Callbacks: serialized per host (`TiledReads`: a busy flag and a condition variable, never a lock held while the caller
  runs), with `in_callback` naming the callback, so a re-entrant call is `CBC_NOT_READY` with "Host functions cannot be
  called from a tiled image's read callback" (the event callback's message is unchanged). Cycles calls the loader from the
  CPU kernel's worker threads, the GPU device's tile loading, the scene update's image loading and the session thread.
- Failure by name (kit rule 5): a nonzero read records "Tiled image N: the tile reader failed (returned S; level L, x X,
  y Y, W x H)", stops every further read and calls `Host::request_cancel`, so the render stops without finishing its
  batch, and after `session.wait()` the render or bake fails with `CBC_FAILED` and that message, never a film with the
  Missing colour. Cycles never loads a failed tile again, so a source whose read did not deliver (a failure, or a read a
  cancel stopped) gets a new generation, which the loader's `equals` compares: the next render adds the image anew (as an
  image update does) and Cycles frees the old one.
- Cancel (the host fix after gap 2): `Host::request_cancel` also stops the reads; the threads waiting for the reader give up
  at once, so a render waiting on reads stops after the read in progress.
- Release exactly once: the source's destructor runs `release` in the callback slot when the last of the handle, the
  bindings and Cycles' image lets go (an image release, a rebind, Cycles freeing its image at the next device update,
  `cbc_host_destroy`, the exit hook); a call that fails runs it before it returns (also a call refused from a callback).
- A float or half value that is not finite fails the read by name, as `cbc_host_image_create` refuses it (Cycles' conform
  would set it to zero).
- Refusals, all by name: no reader, and the image checks of `cbc_host_image_create` (`CBC_INVALID`); an update of a tiled
  handle (`CBC_INVALID`, "A tiled image cannot be updated ..."); a UDIM binding mixing tiled and plain images
  (`CBC_INVALID`, naming the tile: Cycles requests the derivatives that select mip levels from the first tile only,
  `ImageTextureNode::update_images`).
- WebGPU (kit rule 7): the WGSL device has no tile cache (kernel error 256, "tiled image cache not implemented",
  `native/cycles/device.cpp`). The host refuses a binding of a tiled image on `CBC_HOST_WEBGPU` at render and bake
  (`validate_unavailable`, `CBC_UNSUPPORTED`, "WebGPU tiled images not implemented"); the capability report's entry
  "sparse or tiled images" stays word for word, as does AGENT-README's; its "Why." gains a sentence. No file under
  `native/cycles` changes.
- Managed (**PROVISIONAL**): `ITiledImageReader.ReadTile(level, x, y, width, height, Span<byte> destination, rowBytes)`,
  `CyclesScene.CreateTiledImage(width, height, format, reader)` (returns a `CyclesImage` with `IsTiled`), the static
  `TiledImage` (`TileSize`, `LevelCount`, `LevelSize`), `CyclesHost.CreateTiledImage` (unmanaged function pointers),
  `CyclesAbi.CreateTiledImage` and `Native.CreateTiledImage`. The scene passes a `GCHandle` to a per-image state (the
  reader, its format and the scene's failure box, never the scene, so an undisposed scene stays collectable) and two
  static `[UnmanagedCallersOnly]` functions; the release frees the handle. A reader's exception is kept, the read returns
  1, and `Render`, `RenderAsync`, `Bake` and `BakeAsync` throw a `CyclesException` (`Failed`) with the native message and
  the reader's exception as `InnerException`. `IsFaulted` does not change for a failed tile read (every such native
  message starts "Tiled image N:"; no device failed).

**What was done.** As designed; the build records `exports: 64`.

- `native/host/host_scene_types.inc`: `TiledReads`, `caller_callback`, `TiledImageSource`, `TiledImageLoader`, the
  `MemoryImageData.tiled` member. `native/host/cycles_host.cpp`: the named `in_callback`, `Host::tiled_reads`, the cancel
  path, the loader choice in `prepare_graphs`, `retry_undelivered_tiles` and `render_tiles_delivered` after each render,
  `use_texture_cache` on. `native/host/host_bake.inc`: the same around each bake. `native/host/host_scene.inc`: the export,
  the WebGPU refusal, the UDIM mix refusal. `native/host/host_api.inc`: the update refusal. `native/host/cycles_host.h`:
  the two typedefs and the export with their PROVISIONAL comments; the three export lists.
- `native/tests/host/consumer.c`: `--tiled` and `--tiled-exit` (the host-gate cases below; without them the consumer is
  unchanged). `native/tools/macos_host_gate.py`: the count, the `tiledImages` contract checks and the two modes.
- Documents: `native/host/ABI.md` (a "Tiled images" section, the Images row, the callback rules, threading, lifetime, the
  Exports table and its count, the callback types), `native/host/README.md` (a row and the callbacks row),
  `AGENT-README.txt` (the API lists, a "TILED IMAGES" part of SCENE SETTINGS with an example, CALLBACKS AND THREADING, the
  "Why." sentence, WHAT THIS PACKAGE DOES NOT DO, the usings and the quick reference), `native/DEVICE.md` item 172 (a
  texture-cache bullet, RESOLVED), `MAINTAINER-README.txt` NOTES (the export map and the reader design),
  `docs/TESTING-MACOS-ARM64.md` step 3. `docs/PARITY.md` (its tiled/mipmap cache entries are the WGSL device's, still open)
  and `native/oracle/README.md` (no scene format change) are unchanged.
- Managed: `ITiledImageReader` and `TiledImage` (new files), `CyclesScene.CreateTiledImage` with the two callbacks and the
  failure box, `CyclesImage.IsTiled`, `CyclesHost.CreateTiledImage` (which runs `release` itself when the host was
  disposed, since the native call is then never made), `CyclesAbi.CreateTiledImage`, `Native.CreateTiledImage`.
- No file under `native/cycles`, `native/shaders`, `native/src` or `third_party/` changed.

**Provisional signatures** (each the maintainer's to approve):

| Signature | Reason | Alternative considered |
| --- | --- | --- |
| `typedef int (*CBCImageTileReadCallback)(void *user_data, uint32_t level, uint32_t x, uint32_t y, uint32_t width, uint32_t height, void *pixels, uint64_t row_bytes)` | Cycles' `ImageLoader::load_pixels_tile` fed by the consumer, in the ABI's bottom-up rows and the image's own format | Cycles' top-down tile rows and border handed to the consumer (exposes Cycles' internals); the consumer passes every level up front (`cbc_host_image_create_levels`: no memory saving) |
| `typedef void (*CBCReleaseCallback)(void *user_data)`, exactly once per call, also when the call fails | one ownership rule the consumer never has to branch on (the managed tier frees its handle only there) | release only after a successful call (the consumer frees on failure; two paths) |
| `int cbc_host_image_create_tiled(CBCHost, const CBCImageConfig *, CBCImageTileReadCallback, CBCReleaseCallback, void *user_data, CBCImage *)` | a handle like `cbc_host_image_create`'s, bound with the existing calls | a texture-node socket for the reader (not a Cycles socket) |
| Tile size fixed at 64 | Cycles' block size and .tx tile size, within Metal's 128 | a tile-size argument (64 or 128; question in section 7) |
| Average colour zero | a GPU cache miss restarts the path, so no film shows it | a consumer-supplied average colour |
| Callbacks serialized per host | CodeBrix.Imaging decoders and simple readers need not be thread-safe | concurrent calls with a thread-safety requirement |
| A failed read cancels and fails the render (`CBC_FAILED`, named), the tile is read again next time | kit rule 5: Cycles would sample the Missing colour | fail only at the end of the render (slower; the same result) |
| `use_texture_cache` on for every host | Cycles reads it once per image manager; no image without tiles changes | `CBCHostConfig.flags` bit (a second way to create a host, for no film difference) |
| On the WebGPU backend `CBC_UNSUPPORTED`, "WebGPU tiled images not implemented"; entry "sparse or tiled images" kept | kit rule 7 | a new capability entry (the existing words already name it) |
| `ITiledImageReader.ReadTile(int, int, int, int, int, Span<byte>, int)`, `CyclesScene.CreateTiledImage(uint, uint, ImageFormat, ITiledImageReader)`, `CyclesImage.IsTiled` | the memo's form; the facade's `CreateImage` beside it | a delegate (no room for later members) |
| `TiledImage.TileSize`, `LevelCount`, `LevelSize` | the consumer must produce exactly Cycles' level sizes | documentation only |
| `CyclesHost.CreateTiledImage(..., delegate* unmanaged[Cdecl]<...> read, delegate* unmanaged[Cdecl]<nint, void> release, nint)`, `CyclesAbi.CreateTiledImage`, `Native.CreateTiledImage` | the low-level tiers mirror every export | none |
| A reader exception is rethrown as `CyclesException(Failed)` with it as `InnerException`; `IsFaulted` does not change for a failed tile read | the error names the image and tile, the cause is kept | rethrow the reader's exception itself (the event-handler pattern: loses the tile) |

**Count literals moved.** The export count 63 to 64: `native/host/ABI.md` ("## Exports (ABI 1, 64 functions)"), the lists
`exports.def`, `exports.macos`, `exports.map`, and `native/tools/macos_host_gate.py` (run here), `native/tools/host_gate.py`
(twice: the assertion and `exportCount`), `macos_x64_host_gate.py`, `windows_host_gate.py`, `linux_arm64_host_gate.py` and
`android_host_gate.py` (by reading only, unverified). `git grep -n -w 63` of `native/host`, `native/tools`, `tools`, `tests`,
`src` and the documents finds no other export count. No enum, flag or struct size moved (`NativeHeaderConstantMatching`
and `flagNoneMembers` unchanged); the capability report's lists did not change.

**Tests added** (managed total 437, was 407):

- `TiledImageTests` (new, CPU, 28 cases):
  - Plain against tiled: a 256 x 256 byte4 pattern as a tiled image (box-filtered levels) and bound plainly, magnified (half
    a texel per pixel, level 0) across the tile edges and the image's corner, for every interpolation and extension (12
    cases). Closest: bit for bit, all four extensions. Linear: maximum 1.6e-4, 0 practical failures. Cubic: maximum
    8.6e-5, 0 practical failures (emission strength 10, values up to 10). The tiled path's pixel coordinates (tile offsets
    added, rows flipped) round differently. Outside the image Cycles' tiled path wraps the UV before it filters
    (`kernel_image_tile_wrap`: Clip returns zero beyond [0, 1], Extend clamps the UV), where the plain path wraps each
    texel. So Linear with Clip (1,595 practical failures, maximum 2.40), Cubic with Clip (5,125, 2.83) and Cubic with
    Extend (113,598, 0.31) differ there, all in the film's 64 pixels below and left of UV 0. They meet the criterion
    inside the image (0 practical failures).
  - Mip levels: a 512 image whose four levels are four solid colours, seen at 0.5, 2, 4 and 8 texels per pixel, renders
    bit for bit like plain images of the colours of levels 0 to 3. 4 reads of the 85 tiles, one per level. A second render
    reads nothing (the cache keeps tiles), and nothing is read after `Render` returns.
  - Under a texture limit of 256, level 0 is never read and the quads show levels 1, 1, 2, 3 bit for bit.
  - Other cases, each bit for bit like the plain image: a 48 x 40 image (one whole read), float4 HDR and one-channel byte
    images, an Environment Texture within 60 degrees of the horizon (towards the poles Cycles selects levels 1 and 2, mip
    mapping a plain image does not have: 2,614 floats differ), UDIM tiles of tiled images, and a bake (Emission).
  - Failures: a reader that throws fails the render by name with its exception inside, the destination is untouched,
    `IsFaulted` stays false, and the next render reads the tile again and matches. A bake fails the same way. A float4
    reader that writes a NaN fails the render by name ("the tile reader wrote a nonfinite value"), as `CreateImage`
    refuses one (Cycles' conform would set it to zero), and the next render matches the plain image.
  - Refusals: a mixed tiled/plain UDIM binding, an update (facade `InvalidOperationException`, host `Invalid`), a reader's
    call into the scene (`NotReady` with the message).
  - Cancel: with reads of 150 ms each, a cancel at the first read stops the render after 162 ms and 1 read (64 needed);
    the next render reads again and matches the plain image bit for bit.
  - Concurrency: two 8-thread CPU scenes rendering at once, each reading its 64 level-0 tiles from 4 to 6 native threads,
    never two calls at once, both films bit for bit the plain image's.
  - Liveness: the scene keeps a reader alive that nothing else references, and lets it go when disposed.
  - Release, through `CyclesHost` with unmanaged callbacks, exactly once on each path: a failed call (bad format, no
    reader); an unbound handle released; a bound image after its handle, the binding and Cycles' image are gone (at the
    next render); a failed render then the destroy; a disposed host; a cancelled render then the destroy.
  - CodeBrix.Imaging: a committed bench texture (`rough_wood_diff_2k.jpg`, 2048 x 2048, CC0) decoded with CodeBrix.Imaging
    1.0.290.858, the version `tools/gltf/CycleGltfRender` references, with its 6 levels made by the reader. Magnified, 4
    reads, bit for bit like the decoded pixels bound plainly; minified, 1 read. The library project references no image
    package.
- `CapabilityReportTests.sparse_or_tiled_images_are_a_rejection_of_the_webgpu_backend_only`; the word-for-word test covers
  the unchanged entry.
- `GpuRenderTests.tiled_images_render_on_metal_and_are_refused_by_name_on_webgpu`: the lit triangle with two quads behind
  it, one minified and one magnified, reading a tiled 256 x 256 checker.
  - Metal read 3 tiles, at levels 0 and 2. Judged by the reference policy against the new film
    `References/metal-osx-arm64/tiled-image.rgba.f32`, recorded with only this test selected; the manifest gained only
    this entry. Three later runs: maximum 6e-8 from it, 0 practical failures.
  - Its CPU comparison: maximum 1.6e-6, 0 practical failures, 7 strict differences of 6,144 components.
  - A reader that throws fails the Metal render by name with its exception inside; the next render matches the first
    within 9e-8.
  - On WebGPU it expects `Unsupported` with the message and no read.
- Host gate:
  - The contract block gains `tiledImages`: without a reader 1 and released at once; created 0, bound 0, not released
    before the destroy, released once by it.
  - `cpu-tiled` (`consumer.c --tiled`): a 128 x 128 image through a C reader renders the plain image's film bit for bit
    (16 reads; compared again by the gate). A failing read fails by name and is read again by the next render. A call into
    the library from the reader and an update are refused. Release runs once per image.
  - `cpu-tiled-exit`: the host stays live at exit, and the release callback prints its row once from the exit hook.
- A scratch probe of the managed process-exit path (`<repo>/native/work/macos-arm64-d3-texture-cache-20261114/exit-probe/`,
  not kept in this folder): a .NET process exits with a live scene holding a tiled image and a low-level tiled image whose
  `[UnmanagedCallersOnly]` release prints. 3 of 3 runs printed the release from the exit hook and exited 0.

**Oracle scenes.** None. The runner expresses an image only as a raw pixel file beside the document (the `file` key of an
`images` entry); a tiled image would need one more raw file per level, or a runner that makes levels. Neither is "without
files", so none was added (section 7). The starter `starter/diffuse-triangle` was rendered on the three backends into
`<repo>/native/work/oracles/macos-arm64-d3-texture-cache-20261114/` for the stage of the consumer test (Metal 0 practical
failures, maximum 3e-8).

**Rule 9 at gap 6.** The corpus of gap 4 (782 scenes: 780 committed documents and the 2 bench documents), on
`cpu-arm64-macos` alone into `<repo>/native/work/oracles/macos-arm64-d3-texture-cache-corpus-20261114/` (0 failures, every
film repeat-identical; 109 s). Compared by scene path and film index:

- The base run's 1,452 films and the survey's 68 are bitwise identical.
- So are all 1,564 films of the 782 scenes of the gap-4 corpus run: the D1, D2, UDIM and texture-limit scenes. The SHA-256
  over every film, `8aad4249...`, is that of gaps 3 and 4. Nothing moved.
- Pass films: 2,870 of the base run's 2,872, all 24 of the survey's and 2,894 of the gap-4 run's 2,896 are bitwise
  identical. The two that differ are again `passes/data-passes` pass 8, `RenderTime`.

Host library `ac8839b455ebbdd5f71737f7780089cf6be558a399eac6e8e599160c7a1610e6`, runner
`daa5f1ac127d72a5f40aabf9ba2546377124018d2379c1a7b06f21e46ef7e086` (unchanged since gap 4).

**Build, gate, managed tests and the package.**

- Build: `python3 tools/build_macos.py --jobs 8 --skip-dependencies` passed, 0 warnings in the host build log and the
  build output (incremental builds of 10 to 25 s). The configure log keeps CMake's developer warning from Cycles' own
  `src/cmake/external_libs.cmake`.
- macOS host gate (`<repo>/native/work/macos-validation-20261114j/`): passed, with the new contract checks and modes:
  3,096 films compared, 0 same-host bitwise failures, Metal 0 practical failures, maximum 4.5e-8; 15 s.
- Testing document step 7 with .NET SDK 11.0.100: restore, both builds, pack and the tests exit 0.
  - Release: 437 tests, 431 passed, 6 skipped (the WebGPU-only tests), 0 failed, in each of three full runs (27 to 32 s).
  - Debug: the same counts (33 s).
  - The Release build's one warning is the pack driver's ("linux-x64: payload is STALE"); Debug has 0 warnings. The pack
    folder was deleted.
- The consumer test of the MacOSArm64 package, as at gap 4:
  - A scratch stage (`<repo>/native/work/staging/osx-arm64-d3-texture-cache`), packed as 1.0.318.1279: 323 files, "NOT
    APPROVED, NOT PUBLISHABLE".
  - The five modes rendered on the CPU, on Metal and on Metal with GPU denoising, with the 292 kernel sources beside the
    library and none flattened; the negative control as before.
  - The cached runtime pack 10.0.15. The stage, the feed, the pack folder and the app copies were deleted afterwards.
- `python3 tools/licensing/audit.py`: passed. `git diff --check`: clean.

**Unverified on other platforms.**

- Everything WebGPU: the refusal, the unchanged capability entry, the GPU test's WebGPU branch.
- MetalRT with tiled images: the managed Metal test ran BVH2, this machine's default; the gate's MetalRT mode has none.
- The count edits of the five other gate scripts (read, not run).
- The new host code and the `consumer.c` modes under GCC 14 (Linux), clang-cl `/W4 /WX` (Windows) and the Android NDK.
- `use_texture_cache` in a development build that reads files: `OIIOImageLoader` then uses an existing `.tx` file (or a
  tiled, mip-mapped TIFF or EXR texture) through the cache, as stock Cycles does (**INFERENCE** that the Linux host gate's
  four file cases are unchanged; not run).
- The Linux x64 build, gates and managed tests, and the Windows and Android builds.

**What was left.**

- Oracle scenes (above).
- The tile size stays fixed at 64, and the average colour of a tiled image is zero (section 7).
- Outside the image (UVs beyond [0, 1]), Linear with Clip and Cubic with Clip or Extend follow Cycles' tiled path, not the
  plain image's: documented in ABI.md and AGENT-README, not refused (section 7).
- A WebGPU tile cache.

#### Gap 5: tiled rendering

Made after gap 6, under maintainer decision 17 and answer A1 (`native/work/macos-arm64-kit-20261113-design/decisions.md`):
the in-memory tile hook goes in `integrator/path_trace.cpp` through the overlay, not in `session/tile.cpp`; no recipe
change; no OpenImageIO `declare_imageio_format` handler. The export count moves from 64 to 65.

**Design.** `Session::get_effective_tile_size` (`session/session.cpp`) renders one tile unless `SessionParams::use_auto_tile`
is on and the tile's area is smaller than the film's (or a side exceeds 8192), with the size of
`TileManager::compute_render_tile_size` (`session/tile.cpp`: up to 128 as given, above rounded up to a multiple of 128, at
most 8192). With several tiles the film gets a sample count pass (`Session::update_scene`), adaptive sampling renders a
4-pixel overscan except when baking (`TileManager::update`), per-tile denoising is skipped, and the time limit applies per
tile. Each finished tile goes through three calls of `PathTrace` (`integrator/path_trace.cpp`) into Cycles' tile file:
`TileManager::write_tile` (`tile_buffer_write_to_disk`), `finish_write_tiles` (`finalize_full_buffer_on_disk`, which then
calls the session's `full_buffer_written_cb` with the file name) and `read_full_buffer_from_disk`
(`process_full_buffer_from_disk`, which rebuilds a full-frame CPU `RenderBuffers`, denoises it and writes it to the output
driver once). The file is an EXR written and read through OpenImageIO, which needs an EXR plugin that the macOS, Windows
and Android builds of this library do not have. Only `device/device.cpp`, `integrator/path_trace.cpp` and
`util/colorspace.cpp` are swapped in by the injections, so `session/tile.cpp` cannot be reached by the overlay without a
recipe change; `inject_cycles.cmake` (Linux x64, Linux ARM64 and Android, and through it the macOS x64 and Windows
injections) and `inject_cycles_macos.cmake` all swap the overlay's `path_trace.cpp` in (read, not changed).

- Overlay (a recorded transform of `native/tools/cycles_overlay.py`, like the earlier `path_trace.cpp` ones): an in-memory
  store in `path_trace.cpp` (`MEMORY_TILES`) replaces the three calls. A frame holds what
  `TileManager::read_full_buffer_from_disk` would read from the file: the full buffer's `BufferParams` and `BufferPass`
  sockets and the integrator's `DenoiseParams` sockets (the node sockets the file's attributes carry, copied as its
  read-back sets them), and each pixel's channels of the passes in use in buffer order, zero where no tile was written.
  The write copies the tile's window to its place (the tile's `full_x` less the frame's, plus `window_x`, as `write_tile`
  places it); finishing names the
  frame and calls `tile_manager_.full_buffer_written_cb` with that name in place of the file name; the read rebuilds the
  parameters with `update_passes`, resets the buffers and copies the pixels. The integrator's denoise parameters are taken
  in `PathTrace::set_denoiser_params` (Session calls it every iteration with `scene->integrator->get_denoise_params()`, the
  value `TileManager::update` stores). A new render (`PathTrace::reset` with `reset_rendering`) and `~PathTrace` drop the
  path tracer's frames; one exported function, `codebrix_memory_tiles(path_trace, discard)`, gives the host the tiles held
  and drops them. The status "Reading full buffer from disk" reads "from memory" and the errors name memory. No file is
  written or read and OpenImageIO is not called for tiles, so every platform behaves the same whatever plugins it has.
- Native: a new export `cbc_host_tile_size(host, tile_size)` (**PROVISIONAL**), the pattern of `cbc_host_render_time_limit`:
  0, 8 to 128, or a multiple of 128 up to 8192, else `CBC_INVALID` ("Tile size must be 0 (one tile), 8 to 128, or a multiple
  of 128 up to 8192"). Each render and bake sets `use_auto_tile` and `tile_size`. The session's `full_buffer_written_cb`
  records the frame's name; after `session.wait()`, and only when the render was neither cancelled nor failed, the host calls
  `Session::process_full_buffer_from_disk` on the calling thread, then checks cancel and errors again; a tiled render that
  produced no frame fails ("Tiled render produced no full frame"). A scope guard drops what any other end left in the store.
  Bakes: `HostOutput::read_render_tile` copies each tile's region of the bake map. Progressive hosts: `film_update` places a
  tile's update in a persistent film, and the returned film is sent as the last film. The device report gains
  `tiledRendering` (`tileSize`, `tilesKeptInMemory`, `framesReadBack`, `tilesHeld`): the hook counter. A status made of a
  substatus only (Cycles' full-frame steps, "Denoising", "Finishing", under an empty layer name) is sent without a leading
  ": ".
- WebGPU (kit rule 7): the WGSL device has not been run with tile offsets and overscan. Refused at render and bake
  (`validate_unavailable`, `CBC_UNSUPPORTED`, "WebGPU tiled rendering not implemented"); the capability report,
  AGENT-README "RENDER-TIME REJECTIONS" and `native/host/ABI.md` carry "tiled rendering (a tile size other than 0)". No file
  under `native/cycles` changes.
- Managed (**PROVISIONAL**): `uint CyclesScene.TileSize { get; set; }` (`ArgumentOutOfRangeException` for a size Cycles would
  change), `CyclesHost.SetTileSize(uint)`, `Native.TileSize`. Scene documents: `render.tileSize` (runner and managed loader).

**What was done.** As designed; the build records `exports: 65`.

- `native/tools/cycles_overlay.py`: the transform (eight anchored edits of `integrator/path_trace.cpp`; the other overlay
  files are byte for byte unchanged). `native/host/cycles_host.cpp`: the export, `read_back_tiles`, `drop_tiles`,
  `DropTiles`, the bake-map regions, the progressive tile film, the device report, the status text. `native/host/host_bake.inc`:
  the bake path. `native/host/host_scene.inc`: the WebGPU refusal. `native/host/host_api.inc`: the rejection entry.
  `native/host/cycles_host.h` (the declaration, PROVISIONAL) and the three export lists.
- `native/tests/host/consumer.c` and `gpu_consumer.c`: `--tiles`. `native/tools/macos_host_gate.py`: the count, the
  `tileSize` contract checks, the `cpu-tiles` and `metal-tiles` modes and the no-tile-file check. `native/oracle/runner.cpp`:
  `render.tileSize`.
- Documents: `native/host/ABI.md` (a "Tiled rendering" section, the Exports table and its count, the device report, the
  progressive films), `native/host/README.md` (a row), `AGENT-README.txt` (the members, a "TILED RENDERING" part of SCENE
  SETTINGS, PROGRESSIVE RESULTS, the rejection entry and its "Why.", WHAT THIS PACKAGE DOES NOT DO, the quick reference),
  `native/DEVICE.md` item 172 (RESOLVED, with the Metal finding below), `MAINTAINER-README.txt` (the overlay table and the
  export map), `native/oracle/README.md` (the key and the new family), `docs/TESTING-MACOS-ARM64.md` step 3.
  `docs/PARITY.md` names no tiled rendering and is unchanged.
- No file under `third_party/`, `native/cycles`, `native/shaders`, `native/src` or any `inject_cycles*.cmake` changed.

**The overlay and the Metal kernel closure.** `native/work/macos/osx-arm64/host/kernel-sources.json` lists 292 files and
`overlaidInClosure` is `["util/types_gpu_compressed.h"]`, as before; `integrator/path_trace.cpp` is a host translation unit
(`compile_commands.json` compiles the overlay copy) and in no closure. The overlay's generated `device/device.h`,
`device/device.cpp`, `util/colorspace.cpp` and `util/types_gpu_compressed.h` are byte for byte the previous ones.

**Provisional signatures** (each the maintainer's to approve):

| Signature | Reason | Alternative considered |
| --- | --- | --- |
| `int cbc_host_tile_size(CBCHost, uint32_t tile_size)`; 0, 8 to 128, or a multiple of 128 up to 8192 | the `render_time_limit` pattern; only sizes Cycles uses as given, so the tile size used is the one asked for | accepting any positive size and documenting Cycles' rounding; a settings struct |
| The in-memory store in the overlay of `integrator/path_trace.cpp`, with `codebrix_memory_tiles(path_trace, discard)` | A1; no file, no plugin, identical on every platform; the host keeps to Cycles' public `full_buffer_written_cb` and `process_full_buffer_from_disk` | an OIIO `declare_imageio_format` handler (rejected by decision 18); an overlay of `session/tile.cpp` (no injection compiles it) |
| The read-back on the calling thread after `session.wait()`, and a frame dropped on cancel or failure | Cycles' intended order (`finalize_full_buffer_on_disk`); one `write_render_tile` | reading back on the session thread (not offered by the pinned Session) |
| Device report `tiledRendering` counters | proof that tiles stayed in memory, with no new export | a test-only export |
| Progressive hosts: a tile's update placed in the film so far, the returned film sent last | the "last film is the returned film" rule of gap 2 | refusing progressive results with tiles |
| On the WebGPU backend `CBC_UNSUPPORTED`, "WebGPU tiled rendering not implemented"; entry "tiled rendering (a tile size other than 0)" | kit rule 7 | none (lifting it needs a WebGPU run) |
| `uint CyclesScene.TileSize { get; set; }`, `CyclesHost.SetTileSize(uint)`, `Native.TileSize`; `render.tileSize` | a property, as `TextureLimit` | a method on the scene |

**Count literals moved.** The export count 64 to 65: `native/host/ABI.md` ("## Exports (ABI 1, 65 functions)"), the lists
`exports.def`, `exports.macos`, `exports.map`, and `native/tools/macos_host_gate.py` (run here), `native/tools/host_gate.py`
(twice: the assertion and `exportCount`), `macos_x64_host_gate.py`, `windows_host_gate.py`, `linux_arm64_host_gate.py` and
`android_host_gate.py` (by reading only, unverified). `git grep -n -w 64` finds no other export count. No enum, flag or
struct size moved; the WebGPU rejection list gained one entry.

**Tests added** (managed total 458, was 437):

- `TiledRenderingTests` (new, CPU, 18 cases), every comparison against the same scene untiled:
  - The size contract: the default 0, sizes refused by the status-code tier with the message (1, 7, 129, 200, 1000, 8193,
    8320, `uint.MaxValue`) and accepted (0, 8, 64, 128, 256, 1024, 8192), the facade's `ArgumentOutOfRangeException`.
  - Bit for bit (measured): the lit triangle at tiles of 8, 16 and 32 (12, 4 and 1 tiles of 32 x 24); named passes (depth,
    normal, diffuse colour, diffuse direct, emission, sample count), a light group and object and material Cryptomatte at 8;
    a camera border (16 x 9, 4 tiles), zero outside; adaptive sampling at 8 and 16 (sample counts 16 to 64, the sample count
    pass bit for bit too); one scene switching between tiles and none with an edit, each film like a new scene's; bakes
    (Combined at 8 and 16, Diffuse Color at 16, Diffuse at 24: 64, 16, 16 and 9 tiles).
  - Denoising at 8 and 16: bit for bit the untiled render with a `SampleCount` pass, and against the plain untiled render
    the practical criterion (bit for bit in this scene). The reason, from `integrator/denoiser_oidn.cpp`: with a sample count
    pass Open Image Denoise receives the noisy image divided by each pixel's count and its output is multiplied back, without
    one it receives the sums. The noisy pass is bit for bit in every case.
  - Status events name "Rendered n/N Tiles", "Reading full buffer from memory" and "Finishing", none "disk" or "file"; the
    device report counts 12, 4 and 0 tiles and holds none after the render; no `cycles-tile-buffer*` or `.exr` file appears
    in the working, temporary or test directories.
  - Cancel: a 1,000,000-sample render in tiles of 8 cancelled at its first progress event stops in the first tile (21 ms),
    throws `OperationCanceledException`, leaves the destination untouched, reads back no frame and holds no tile; the next
    render equals the untiled film.
  - Progressive host in tiles of 16: the first film holds only the first tile, the last film is the returned film, which
    equals a plain host's untiled film bit for bit.
- `CapabilityReportTests.tiled_rendering_is_a_rejection_of_the_webgpu_backend_only`; the word-for-word test covers the entry.
- `OracleSceneBundleTests.the_tile_size_key_renders_in_tiles_and_changes_no_film`: 12 tiles kept, the film bit for bit the
  document's without the key, 7 refused.
- `GpuRenderTests.tiled_rendering_renders_on_metal_and_is_refused_by_name_on_webgpu`: the multi-pass scene at 64 x 48, 16
  samples, in 12 tiles of 16 on Metal, judged by the reference policy against the new film
  `References/metal-osx-arm64/tiled-rendering.rgba.f32` (recorded with only this test selected; the manifest gained only this
  entry; later runs within 1.2e-7 of it, 0 practical failures); against the untiled Metal film maximum 3e-7, 0 practical
  failures, the depth, diffuse colour and emission passes bit for bit and the light group within 6e-8; denoised in tiles
  against untiled bit for bit; the device report counts 12 and 24 tiles. Its CPU comparison: maximum 1.4e-5, 0 practical
  failures, 1,056 strict differences of 12,288 components. On WebGPU it expects `Unsupported` with the message.
- Host gate:
  - The contract block gains `tileSize`: 7, 129 and 8193 refused (1), 256 and 8 accepted, a 32 x 8 render in four tiles
    whose device report reads `{"tileSize":8,"tilesKeptInMemory":4,"framesReadBack":1,"tilesHeld":0}`, then 0 accepted.
  - `cpu-tiles` (`consumer.c --tiles`): the C lifecycle (three rounds with an edit, a sample increase and decrease, a
    cancelled render, a destroy during a render) in tiles of 8; 200 is refused with the message; after every render the
    device report counts 12 more tiles and one more frame and holds none; the films are bit for bit the Session references.
  - `metal-tiles` (`gpu_consumer.c --tiles`): the same on Metal, practical against the CPU references (0 failures).
  - No `cycles-tile-buffer*` file appears in the repository, the gate's folder or the working directory.

**A finding on Metal: adaptive sampling in small render buffers.** The committed scene `render-settings/tiles-glass-passes`
(adaptive sampling, threshold 0.05, minimum 8, 32 samples, first adaptive check at sample 16) renders in tiles of 16 on Metal
with 327 more of its 3,072 pixels stopped at 16 samples than its untiled twin (sample counts 16:3,052 and 32:20 against
16:2,725 and 32:347), 806 practical failures against the untiled Metal film (maximum 3.05) and the same against the CPU,
while the CPU decides identically tiled and untiled and the untiled Metal film's sample counts equal the CPU's exactly.
Scratch probes (`<repo>/native/work/macos-arm64-d3-tiled-20261114/metal-check/`, summary in
`metal-adaptive-findings.txt` there): tiles of 8 and 32 also differ (753 and 45 practical failures), one tile (64) does not;
with no check before the last sample (minimum 16) and without adaptive sampling the tiled Metal film equals the untiled one
within 1.4e-6; a camera border in tiles of 16 stops every tile at 16 (the convergence flags of 83 pixels still say "not
converged"), untiled it equals the CPU; and small UNTILED films (20 x 20, 24 x 20, 40 x 30) never stop early on Metal where
the CPU stops 77 to 86 % of the pixels at 16. The store copies a tile only after its last sample, and the depth and
non-adaptive films are equal, so the store is not the cause; **INFERENCE**: Cycles' Metal adaptive-sampling path decides
differently for small render buffers (the active-pixel count of `PathTraceWorkGPU::adaptive_sampling_convergence_check_count_active`
is the first suspect: it decides whether a tile continues and whether the convergence filter runs). Nothing was refused
(section 7); ABI.md, AGENT-README and `native/DEVICE.md` item 172 state it.

**Oracle scenes.** A new family `native/oracle/scenes/render-settings/` written by `author_render_settings.py` (`--check`
passes): `tiles-glass-passes` (the starter glass slab, a glossy sphere and a diffuse floor under an area light in the light
group "key", adaptive sampling, seven named passes, 64 x 48, 32 samples, `render.tileSize` 16: 12 tiles) and
`tiles-border-denoise` (the starter glossy sphere and a floor under a point light, a camera border of 41 x 24 pixels, Open
Image Denoise, the noisy image and two guides as passes, 64 x 48, 16 samples, `render.tileSize` 8: 18 tiles), each with its
untiled twin (`-untiled`, the same document without the key). Run `<repo>/native/work/oracles/macos-arm64-d3-tiled-20261114/`
(scratch, not in this folder), `starter/diffuse-triangle` first, on the three backends (47 s): 0 failures, every CPU film
repeat-identical (bitwise), Metal repeats within 1e-6, every comparison reported:

| Scene | BVH2 vs CPU | RT vs CPU | RT vs BVH2 |
| --- | --- | --- | --- |
| `starter/diffuse-triangle` | 0 pf, max 3e-08 | 0 pf, max 3e-08 | 0 pf, max 1.5e-08 |
| `render-settings/tiles-glass-passes` | 806 pf, max 3.05 | 806 pf, max 3.05 | 0 pf, max 6.7e-06 |
| `render-settings/tiles-glass-passes-untiled` | 0 pf, max 7e-05 | 0 pf, max 7e-05 | 0 pf, max 2.9e-06 |
| `render-settings/tiles-border-denoise` | 6 pf, max 0.0011 | 6 pf, max 0.0011 | 0 pf, max 7.5e-05 |
| `render-settings/tiles-border-denoise-untiled` | 6 pf, max 0.0011 | 6 pf, max 0.0011 | 0 pf, max 6.7e-05 |

Tiled against untiled twin, outside the comparator: on the CPU `tiles-glass-passes` is bit for bit in its film and all seven
passes, and `tiles-border-denoise` bit for bit in its three passes while its denoised film differs (maximum 2.7e-5, 0
practical failures, 504 strict differences of 12,288; a scratch twin with a sample count pass in both is bit for bit). On
Metal (BVH2 and MetalRT alike) `tiles-border-denoise` is within 5.4e-5 (0 practical failures) and `tiles-glass-passes`
differs as in the finding above.

**Rule 9 at gap 5.** The corpus of gap 6 plus the four new scenes (786 scenes: 784 committed documents and the 2 bench
documents), on `cpu-arm64-macos` alone into `<repo>/native/work/oracles/macos-arm64-d3-tiled-corpus-20261114/` (0 failures,
every film repeat-identical; 139 s). Compared by scene path and film index:

- The base run's 1,452 films and the survey's 68 are bitwise identical.
- So are all 1,564 films of the 782 scenes of the gap-6 corpus run; the SHA-256 over their films, `8aad4249...`, is that of
  gaps 3, 4 and 6. The overlay change moved no film.
- Pass films: 2,870 of the base run's 2,872, all 24 of the survey's and 2,894 of the gap-6 run's 2,896 are bitwise
  identical; the two that differ are again `passes/data-passes` pass 8, `RenderTime`.

Host library `239c4895d4a956a620a755c2a963905ca01003eeb73c803b1722096596eef9c4`, runner
`be7f60441580310ffbfda00919c89cc12ea8dfd6adb7ce630ec92aa327c8b6e3` (rebuilt for `render.tileSize`).

**Build, gate, managed tests and the package.**

- Build: `python3 tools/build_macos.py --jobs 8 --skip-dependencies` passed, 0 warnings in the host build log and the build
  output (incremental builds of 13 to 28 s).
- macOS host gate (`<repo>/native/work/macos-validation-20261114k/`): passed, with the new contract checks and modes: 3,102
  films compared, 0 same-host bitwise failures, Metal 0 practical failures, maximum 4.5e-8; 27 s.
- Testing document step 7 with .NET SDK 11.0.100: restore, both builds, pack and the tests exit 0.
  - Release: 458 tests, 452 passed, 6 skipped (the WebGPU-only tests), 0 failed, in each of three full runs (45 to 56 s).
  - Debug: the same counts (118 s).
  - The Release build's one warning is the pack driver's ("linux-x64: payload is STALE"); Debug has 0 warnings. The pack
    folder was deleted.
- The consumer test of the MacOSArm64 package, as at gap 6:
  - A scratch stage (`<repo>/native/work/staging/osx-arm64-d3-tiled`), packed as 1.0.319.186: 323 files, "NOT APPROVED, NOT
    PUBLISHABLE".
  - The five modes rendered on the CPU, on Metal and on Metal with GPU denoising, with the 292 kernel sources beside the
    library and none flattened; the negative control as before.
  - The cached runtime pack 10.0.15. The stage, the feed, the pack folder and the app copies were deleted afterwards.
- `python3 tools/licensing/audit.py`: passed. `git diff --check`: clean.

**Unverified on other platforms.**

- The overlay transform on Linux x64, Linux ARM64, Android, macOS x64 and Windows: every injection swaps the overlaid
  `path_trace.cpp` in (read), but no other platform compiled it here. The Linux-side overlay records change with it: the
  Android gate's overlay hashes (`android_host_gate.py`, the pin's `overlaySha256`) and the macOS x64 and Windows
  comparisons against a Linux-written overlay (`build_cycles_macos_x64.py --reference-overlay`, and the Windows recipe's)
  must be regenerated on the Linux x64 machine; nothing of them was edited here.
- Everything WebGPU: the refusal, the capability entry, the GPU test's WebGPU branch.
- The count edits of the five other gate scripts (read, not run), and the `--tiles` modes of the two C consumers under GCC
  14, clang-cl `/W4 /WX` and the Android NDK.
- Tiled rendering in a build whose OpenImageIO has an EXR plugin (Linux, Android): the store replaces the tile file there
  too, so no EXR is written (**INFERENCE** from the overlay; not run).
- MetalRT with tiles outside the oracle runs (the managed Metal test and the gate's `metal-tiles` ran BVH2, this machine's
  default).

**What was left.**

- The Metal adaptive-sampling finding (above, section 7).
- The consumer spill hook (`cbc_host_tile_storage`), deferred by answer A1.
- Tiled images read through Cycles' texture cache in a tiled render on Metal: Cycles rebuilds a restarted path's pixel from
  `render_pixel_index` and the camera width (`kernel/integrator/init_from_camera.h`), which does not hold for a tile's or a
  camera border's buffer (**INFERENCE** from reading; not run). Tested since (section 5.4, 28(b)): it renders right.

### 5.4 Rulings after D3 (2026-11-14)

The maintainer's answers to the questions of sections 5.3 and 7 in `<repo>/native/work/macos-arm64-kit-20261113-design/decisions.md`:
the first batch (A2, C26, B8 and B11, and decision 22, C22) below, then C27 (reproducible builds) and the second batch
(B12, B19, B20, D30 and 28(b)) after its verification, then C24 (subdivision-surface baking, decision 26), and last the
final ruling batch (decision 34, "Final rulings"). The first batch
was made in that order on top of the uncommitted Part D tree (D1, D2, the D3 gaps and the cancel fix) at
`f2586fc17028b62b6e4702a59a401b8d3f230492`. Each item's
cumulative state is saved as `<repo>/native/work/partD-20261114/batch1-<item>.*` (scratch: `git diff --binary HEAD`, the
status, and a tar of the untracked files, as the D2 snapshots). Every new or changed public behaviour is **PROVISIONAL**.
No export, enumeration value or struct size moved (65 exports); one managed member was added (B8).

#### A2: a displaced mesh edited after a render

**What was done.** The host keeps the authored attributes of every triangle mesh a render displaces (`DisplacedRecord`,
`Host::displaced`) from that render to the next edit of the mesh, and restores them before Cycles displaces the mesh again
(`reconcile_displacement` and `undisplace` in `native/host/host_edits.inc`, the pattern of the static BVH bake records).
`GeometryManager::displace` adds the displacement to the current positions (every motion step) and recomputes the normals
from them whenever its device update finds the mesh modified (`scene/mesh_displace.cpp`), and tangents and the undisplaced
position and normal it adds only when missing, so the whole authored attribute set comes back. An edit of the mesh restores
it first and drops the copy (`before_edit`); a render, after `prepare_graphs` and after the device recovery (both in
`cbc_host_render` and `cbc_host_bake`), restores it when the mesh is modified (the recovery tags every geometry modified),
when one of its displacement shaders changed (Cycles' own `need_update_displacement`, or a graph `prepare_graphs` compiled
again: an edited value or link, an updated bound image), when the object the displacement is evaluated with changed or was
edited (Cycles takes the first object that uses the mesh; its transform, motion and every other socket reach the
displacement shader), or when the scene's motion mode changed; in the last three cases Cycles would not displace again by
itself, so the restored mesh is tagged modified. A mesh whose shaders no longer displace gets its authored attributes back,
and the bake record `reconcile_bakes` took from the displaced data for the static BVH bake it may now get is taken again
from them (`capture_bake`). Reads (`authored_attribute`) answer from the copy. Subdivision meshes are tessellated from their
cage before each displacement and get no copy.

Two further edits after a render rendered unlike a new scene while the tests were written, both measured on the CPU
reference and both fixed, because an edit of a displaced mesh's shaders and sockets must render like a new scene:

- A change of a shader's `displacement_method` (here `true` to `bump`): 1,189 practical failures of 6,144 components against
  a new scene, also with two users (nothing baked). Cycles finalizes a shader graph once, for the method of that time
  (`ShaderManager::device_update`: the bump from displacement of Bump and Both), and the host had not given Cycles a new
  graph; `before_edit` now marks the shader's authoring graph dirty, so `prepare_graphs` hands over a fresh copy.
- A shader that no node referenced at a render and that a mesh's `used_shaders` lists afterwards: 279 practical failures in
  a plain scene without displacement. Cycles compiles only referenced shaders and expects its integrations to call
  `Shader::tag_used` when they assign one ("if an unused shader suddenly gets used somewhere, it needs to be recompiled",
  `scene/shader.cpp`); `cbc_host_set_nodes` now calls it for every shader it assigns (mesh, light and background sockets).

**Provisional:** the restore rule and its triggers (an edit of the displacing object displaces the mesh again), and that
reads of a displaced mesh return authored values. No new signature. `native/host/ABI.md` ("Edits after a render"),
`native/host/README.md`, `AGENT-README.txt` (EDITS AFTER A RENDER and the pitfall on cheap edits).

**Tests** (CPU, every comparison bit for bit against a new scene that applies the same calls before its first render):

- `DisplacementEditTests` (new, 11 cases), a 16 x 16 grid displaced along its normal by noise of the world position times
  a custom vertex attribute, with a second displaced material: the first render equals a new scene and repeats; edits after
  a render of the `smooth` flags, the face `shader` indices, a material no geometry used before, the attribute the
  displacement reads, the Displacement node's scale, the method (to Bump), the object's `tfm` and a new instance each equal a
  new scene, and a second render equals the first after the edit; a sequence of four edits with displacement switched off
  and on equals new scenes at every step; after a render `GetAttribute("P")` and the custom attribute return the authored
  values, and a replaced mesh renders like a new scene. Control (the restore disabled in a scratch build, the outputs in
  `<repo>/native/work/partD-20261114/batch1-a2-control/`): 9 of the 11 fail, for example `smooth` 1,210 and `transform`
  1,036 practical failures; the first render and the instance pass, as they must.
- `CyclesSceneTests.Shaders_first_referenced_after_a_render_match_a_scene_built_with_them` (a mesh's second material, a
  light's emission shader and a world shader assigned after a render).
- `GpuRenderTests.a_displaced_mesh_after_a_device_recovery_and_edited_after_a_render_renders_like_a_new_scene`: on Metal the
  film after a rejected render (an image of 16385 x 1) and the recovery equals the film before it within 3e-8 (0 practical
  failures), and the film edited after a render (`smooth`, `tfm`, the scale) is judged by the reference policy against the
  new film `References/metal-osx-arm64/displacement-edit.rgba.f32` of a scene built with the edits (recorded with only this
  test selected; the manifest gained only this entry; three later runs within 3e-8 of it) and equals that scene rendered in
  the same process within 3e-8; its CPU comparison is maximum 1.5e-5, 0 practical failures, 879 strict differences of
  6,144. Control: without the restore the recovered film differs from the one before it in 1,208 practical failures
  (maximum 0.16).

**Unverified on other platforms.** The WebGPU branch of the GPU test (recovery from a custom camera), the Linux x64 CPU
films, `native/tools/scene_edit_gate.py` (Linux WebGPU). **What was left:** a displacement shader that reads scene
attributes (Scene Time) is not displaced again when they change; Cycles' own re-tag of every geometry when MetalRT switches
to extended limits (above 16,777,216 instances or 268,435,456 primitives, `MetalDevice::set_bvh_limits`) is not covered
(section 7).

#### C22: area lights with a non-finite axis (stopped)

The rule's precondition failed, so nothing was changed (no host, document, scene or author-script edit), as decision 22
requires. The scan (`<repo>/native/work/partD-20261114/batch1-c22-stop/scan_axes.py` and `scan.txt`, scratch) computes, for
every area light of the 784 committed documents, the axes Cycles writes to the kernel (`AreaLight::copy_to_kernel`,
`scene/light.cpp`: `normalize_len` of the object transform's column times the size, in binary32: `1 / len`, then a
multiply). An axis whose length underflows to zero holds NaN and infinities (for example `(inf, NaN, NaN)` for a size of
1e-23 along x), so 16 area lights have a non-finite axis:

| Scenes | In the base run | Axis |
| --- | --- | --- |
| `lamps/area-degenerate-rect`, `-disk-plane`, `-ellipse-spread`, `-lamp-shader`, `-tree`, `-volume` | yes | NaN and infinities |
| `lamps/area-zero-size` | yes | NaN (size 0; Cycles disables the light before it computes the axis, `LightManager::test_enabled_lights`) |
| `lamps/area-degenerate-spread-zero`, `-ellipse-spread-zero`, `-spread-tiny`, `-spread-tiny-volume`, `-tree-transmissive` | no (survey) | NaN and infinities |
| `world/portal-zero`, `-zero-u`, `-underflow` | no (survey) | NaN (zero-extent portals) |
| `lamps/area-degenerate-infinite-axis` | no (survey) | infinities, no NaN |

Six base-run scenes that Cycles renders have a non-finite axis in that sense, so refusing "an axis that is non-finite
(infinities or NaN)" would refuse them and break the rule-9 bitwise check, and would also refuse the other degenerate lights
and the zero-extent portals the same decision keeps accepted. Only `lamps/area-degenerate-infinite-axis` has an axis of
infinities without a NaN, the sub-case the WebGPU device refuses as "WebGPU degenerate area light with an infinite axis not
implemented" (`native/cycles/lighting.cpp`). The reading is the lead's to settle (section 7); the scene stays committed.

#### C26: the delivered film

**Design.** `HostOutput::write_render_tile` already compared the tile's offset and size with the render's; at the Metal
upscale of survey row 9 they match, because the tile states the size of the render requested. A first version that also
compared Cycles' public render tile parameters (`PathTrace::get_render_tile_params`) and filled each destination with a
marker NaN did not catch row 9 either (a first probe with the ruling-14 refusal lifted in a scratch build, whose run the
second probe below replaced: every Metal upscale render succeeded, factor 1.5 maximum 15.3 from the CPU film): those
parameters are the full-resolution big tile, and the pass accessor reads the
buffers with them, so every pixel is written, the reduced-resolution image packed into the first ones and zeros after
them (192 of 768 pixels at a factor of 2, 336 at 1.5). The parameters the passes are rendered with are the path tracer's
effective big tile (`render_state_.effective_big_tile_params`, scaled by the resolution divider that
`RenderScheduler::get_render_work` multiplies by the upscale factor), which no public interface exposes.

**What was done.** A recorded transform of the overlay of `integrator/path_trace.cpp` (`native/tools/cycles_overlay.py`,
`DELIVERED_PARAMS`): around `PathTrace::tile_buffer_write`'s call of the output driver it sets a thread-local pointer to the
parameters the passes of that write are read with (the full frame of a tiled render, else the effective big tile), which
`codebrix_delivered_buffer_params()` returns while the write runs. The host (`HostOutput::delivered_layout` and `read_pass`
in `native/host/cycles_host.cpp`) then requires the tile at offset 0 with the render's size (the film, or the camera
border's region) and full size; render buffers of that size, a full window, at that place in a film of the requested size;
a four-component Combined pass and every named pass with the components the host reads; and every component of every
pixel of those passes written (the destination is filled with the quiet NaN `0x7fc0c0de` first). Otherwise the render or
bake fails with `CBC_FAILED` and "The film the output driver delivered is not the one requested: ..." (`Result::
delivered_error`, checked before "Missing complete Combined output", in `cbc_host_render` and `cbc_host_bake`), and the
output is not written. The overlay's other generated files are byte for byte those of gap 5 (regenerated from both
versions of the script and compared), `integrator/path_trace.cpp` is a host translation unit and in no kernel closure, and
`kernel-sources.json` still lists 292 files with `overlaidInClosure` `["util/types_gpu_compressed.h"]`.

**Evidence.** With the refusal lifted in a scratch build (`<repo>/native/work/partD-20261114/batch1-c26-probe/`:
`probe-refusal-lifted.patch`, `probe-run.txt`; the main tree was restored and rebuilt), the four Metal renders of survey row 9 with GPU denoising fail by name: factors 2 (also the noisy
pass), 4 and 1.5 with "render buffers of 16 x 12", "8 x 6" and "21 x 16 (window ... ) for a render of 32 x 24"; factor 1, the
shadow catcher at 2 (Cycles forces 1) and CPU denoising at 2 render. A bake does not refuse the factor: a CPU bake at 2 equals
the bake at 1 bit for bit (Cycles' CPU Open Image Denoise resets it), and a Metal bake at 2 with GPU denoising now fails
with "render buffers of 16 x 16 ... for a render of 32 x 32" (`bake-probe.txt`; before the check it returned that output
as the requested bake, **INFERENCE** from the render case, not run).

**Provisional:** the check, its message and status, and the overlay hook (an internal function of the host library, not an
export). `native/host/ABI.md` (Conventions, "Render output"), `native/host/README.md`, `MAINTAINER-README.txt` (the overlay
table), `AGENT-README.txt` (the film contract).

**Tests.** `DeliveredFilmTests` (new, 6 cases): renders with passes of one, three and four components (depth, normal,
diffuse colour, diffuse direct, UV, sample count, a noisy Combined) whole and in a camera border, untiled and in tiles of 8,
deliver every pass; a CPU bake at an upscale factor of 2, untiled and in tiles of 16, equals the bake at 1 bit for bit while
a render at 2 stays refused (ruling 14). `GpuRenderTests.a_gpu_bake_rendered_at_a_reduced_resolution_fails_by_name`: on Metal
the bake at 2 with GPU denoising fails by name, at 1 and with CPU denoising it bakes; on WebGPU it expects the bake to equal
the bake at 1 within the practical criterion (not run). It never fires otherwise: the managed tests, the host gate and the
whole corpus below pass unchanged.

**Unverified on other platforms.** The overlay transform on Linux x64, Linux ARM64, Android, macOS x64 and Windows (every
injection swaps the overlaid `path_trace.cpp` in, read only); the Linux-side overlay records (the Android gate's overlay
hashes and the macOS x64 and Windows comparisons against a Linux-written overlay) must be regenerated on the Linux x64
machine, as after gap 5. The WebGPU branch of the GPU test.

#### B8: the largest image side per backend

**What was done.** Each `backendScope` entry of the capability report states `maxImageDimension` (**PROVISIONAL**,
`native/host/host_api.inc`), the largest width or height the backend's device takes for an image loaded whole:

| Backend | Value | Source |
| --- | --- | --- |
| `metal` | 16384 | `MetalDevice::image_alloc` (`device/metal/device_impl.mm`): "Texture exceeds maximum allowed size of 16384 x 16384" for a side above 16384 |
| `webgpu` | 1073741823 | `WebGPUDevice::mem_alloc` (`native/cycles/device.cpp`): "Invalid resident image dimensions" above `INT32_MAX / 2` (READ; its storage binding also bounds an image's bytes) |
| `cpu`, `cpu-embree` | 2147483647 | Cycles' CPU device checks none: it keeps the side in `KernelImageInfo`'s `uint` (`util/types_image.h`) and its kernels read it as `int` (`kernel/util/image.h`), so the host's own `INT_MAX` holds |

`limits.maxImageDimension` stays `INT_MAX`, the largest side `cbc_host_image_create` accepts. Managed (**PROVISIONAL**):
`long? BackendScope.MaxImageDimension` (null when a report does not state it). A tiled image is loaded in tiles through
Cycles' texture cache and is not bound by the value (READ). Documents: `native/host/ABI.md` (Backends and capability
queries), `AGENT-README.txt` (BackendScope, Limits, TEXTURE LIMIT).

| Signature | Reason | Alternative considered |
| --- | --- | --- |
| `backendScope.<backend>.maxImageDimension` (JSON) | the limit is the device's, so it belongs to the backend's scope | `limits.maxImageDimension` per backend (changes the meaning of a binary-level field); a device-report field (only with a host) |
| `long? BackendScope.MaxImageDimension` | the other scope facts are members of `BackendScope` | `Limits` keys per backend (stringly typed) |

**Tests.** `CapabilityReportTests.the_report_states_the_largest_image_side_of_every_backend` (the values above, the
binary limit, null when absent) and `GpuRenderTests.the_reported_largest_image_side_renders_on_metal_and_one_pixel_more_is_refused`
(on Metal a 16384 x 1 image renders and a 16385 x 1 one fails with Cycles' message; on WebGPU it checks the value only).
**Unverified:** the WebGPU value at run time, and the report under the Linux, Windows and Android compilers.

#### B11: the Image Texture node's tiles socket

**What was done.** `cbc_host_set_ints` refuses a set of the `image_texture` node's `tiles` socket that would change its
value (`CBC_INVALID`, "The Image Texture node's tiles socket is Cycles' list of UDIM tiles read from image files, which this
library does not read; bind images as UDIM tiles with cbc_host_image_bind_tiles"; `file_tiles_socket` in
`native/host/cycles_host.cpp`), the pattern of the sockets a render owns; a set to its current value, the default empty
list included, is accepted, and reads are unchanged. No committed scene, test, gate or tool sets it; the generated
property round trip sets the value it read and passes. Documents: `native/host/ABI.md` (Images, UDIM tiles),
`AGENT-README.txt` (UDIM TILES and the pitfall on sockets a render owns). **Provisional:** the refusal and its message.
**Tests:** `UdimTileTests.a_change_of_the_file_name_tiles_socket_is_refused_by_name_and_its_default_stays_accepted` (the
typed property, `CyclesHost.SetInts` and the status-code tier, on a node with a tiles binding and on an unbound one; the
empty list set before and after a render; the bound tiles render on, bit for bit). **Unverified:** nothing backend-specific
(the refusal is the host's before any device).

#### Verification of the batch

- Build: `python3 tools/build_macos.py --jobs 8 --skip-dependencies` passed, 0 warnings in the host build log and the build
  output (17 s, incremental). Host library `e13d22d3b4c982e412b8572621b1f507e1b9d6011cfdb5496ce82dccf2ddb009`, runner
  `be7f60441580310ffbfda00919c89cc12ea8dfd6adb7ce630ec92aa327c8b6e3` (unchanged since gap 5).
- macOS host gate (`<repo>/native/work/macos-validation-20261114l/`): passed, 3,102 films compared, 0 same-host bitwise
  failures, Metal 0 practical failures, maximum 4.5e-8; 19 s.
- Testing document step 7 with .NET SDK 11.0.100: restore, both builds, pack and the tests exit 0. Release: 481 tests, 475
  passed, 6 skipped (the WebGPU-only tests), 0 failed, in each of three full runs (44 to 47 s). Debug: the same counts
  (43 s). The Release build's one warning is the pack driver's ("linux-x64: payload is STALE"); Debug has 0 warnings. The
  pack folder was deleted.
- Rule 9: the whole committed corpus (784 documents) and the 2 bench documents on `cpu-arm64-macos` alone into
  `<repo>/native/work/oracles/macos-arm64-batch1-corpus-20261114/` (786 scenes, 0 failures, every film repeat-identical;
  134 s). By scene path and film index, all 1,452 films of the 726 base-run scenes, all 68 of the 34 survey scenes and all
  1,572 films of the 786 scenes of the gap-5 corpus run (every D1, D2 and D3 scene) are bitwise identical, and the SHA-256
  over every film (`4155135787c5...`) is that of gap 5; nothing moved, and no scene left the corpus (C22 stopped). Pass
  films: 2,870 of the base run's 2,872, all 24 of the survey's and 2,934 of the gap-5 run's 2,936; the two that differ are
  again `passes/data-passes` pass 8, `RenderTime`.
- The consumer test of the MacOSArm64 package as at gap 5: a scratch stage (`<repo>/native/work/staging/osx-arm64-batch1`,
  never approved), packed as 1.0.319.265 (323 files match, "NOT APPROVED, NOT PUBLISHABLE"); `dotnet build`, `run`,
  `publish`, `publish -r osx-arm64 --self-contained false` and `--self-contained` each rendered on the CPU, on Metal and on
  Metal with GPU denoising, the 292 kernel sources beside the library and none flattened; without `codebrix-cycles/` Metal
  failed with "Metal kernel sources are missing beside the native library" and the CPU rendered; the cached runtime pack
  `Microsoft.NETCore.App.Runtime.osx-arm64` 10.0.15. The stage, the feed, the pack folder and the app copies were deleted.
- `python3 tools/licensing/audit.py`: passed. `git diff --check`: clean.

#### C27: reproducible builds

**What was done.** Decision 27, applied after batch 1. The patch a scratch agent proved in two copies of the tree
(`<repo>/native/work/repro-20261114/repro.patch`; its tables in `tables/IDENTITY-SUMMARY.md` there) applied to the main
tree with `patch --dry-run -p1` and then `patch -p1`, every hunk clean (no offset, no fuzz), although batch 1 had changed
`docs/TESTING-MACOS-ARM64.md` since. It changes the osx-arm64 recipe only:

- `native/tools/build_cycles_macos.py`: `ZERO_AR_DATE=1`, and `SOURCE_DATE_EPOCH` (unless the caller sets it) from the newest
  commit that changed a Cycles-with-Metal source, recorded as `sourceDateEpoch` in `invocation.json`.
- `native/tools/inject_cycles_macos.cmake`: `codebrix_checkout_independent_objects`, CMake's `OBJECT_NAME` for the host's
  sources outside the Cycles tree (repository-relative names) and for object file names that repeat in one Cycles static
  library.
- `tools/build_macos.py`: the runner reaches the host library through `@executable_path/../host/build/lib`.
- `tools/dependencies/build_macos.py` (shared with the osx-x64 and Windows recipes): `ZERO_AR_DATE` and a
  `CMAKE_PROJECT_INCLUDE` for OpenEXR and OIIO (install rpaths, `-fno-vectorize` on OIIO's `imagebufalgo_pixelmath.cpp`), set
  in `main()` only; the definitions `build_macos_x64.py` and `build_windows.py` use are unchanged (the scratch agent's AST
  check, `tables/shared-helper-check.txt`).
- `native/tools/stage_macos_binaries.py`: `dsymutil` gets the binary's checkout-relative path.
- `native/host/MACOS_ARM64.md` ("Reproducible builds") and `docs/TESTING-MACOS-ARM64.md` (step 9: comparing a fresh build
  with the committed payload by hash).

On top of the patch: `MAINTAINER-README.txt` no longer says that a macOS rebuild is not byte-identical; the stated CMake
minimum of the osx-arm64 recipe is 4.2 (`cmake --help-property OBJECT_NAME`: "versionadded 4.2") in
`docs/TESTING-MACOS-ARM64.md` section 2, `native/host/MACOS_ARM64.md` and `MAINTAINER-README.txt`, each saying what an older
CMake does (the injection warns; the host library, its executables and its dSYM then differ with the checkout path, and the
dSYM lacks the line tables of the objects whose file names repeat in one Cycles static library); the patch's check date reads
2026-11-14, the local date. Snapshot `<repo>/native/work/partD-20261114/c27.*` (`c27.patch` is the increment over batch 1).

**Builds.** A full build, `CODEBRIX_ISPC=<repo>/native/work/tools/ispc-v1.30.0-macOS.arm64/bin/ispc python3
tools/build_macos.py --jobs 8`, with the previous dependency tree moved aside so that every component built from scratch
(246 s; the old tree was deleted afterwards). 0 warnings in our own code; the host build log keeps the pinned Cycles' one
warning (`device/metal/device_impl.mm:559`, `fastMathEnabled` deprecated, as in Part A). `sourceDateEpoch` is 1794666748
(2026-11-14 14:32:28 UTC, commit `f2586fc17`), and the host library's version reads 1.0.20261114.0.

**Reproducibility re-check.** After that build `native/work/macos/osx-arm64/host` and `oracle-build` were deleted and the tree
built again with `--skip-dependencies` (78 s, a clean host build). SHA-256 by file:

| Artifacts | Files | Second build = first | First build = the build before C27 |
| --- | --- | --- | --- |
| Host library | 1 | 1 (`266365c287369d5c...`) | 0 (was `e13d22d3b4c982e4...`) |
| Lifecycle and reference executables (`host/build/bin`) | 7 | 7 | 0 |
| Oracle runner | 1 | 1 (`1f90b7df1bac4910...`) | 0 (was `be7f60441580310f...`) |
| Kernel tree beside the host library | 292 | 292 | 292 |
| The host's dependency closure (`host/status.json`) | 19 | 19 (not rebuilt by the second build) | 14 (Iex, IlmThread, OpenEXR and both OIIO libraries changed: their link-time rpaths) |

The identity at two checkout paths is the scratch agent's (the 335 files of a stage, the build tree and the dependency
prefix, except libjpeg, which is neither shipped nor loaded, and the prefix record); it was not repeated on the main tree.

**Gate, tests and rule 9.** macOS host gate (`<repo>/native/work/macos-validation-20261114m/`): passed, 3,102 films compared,
0 same-host bitwise failures, Metal 0 practical failures, maximum 4.5e-8. Managed tests (Release, the whole suite): 481, 475
passed, 6 skipped (the WebGPU-only tests), 0 failed; `tools/licensing/tests/test_guards.py`: 11 passed. Rule 9: the whole
committed corpus (784 documents) and the 2 bench documents on `cpu-arm64-macos` alone into
`<repo>/native/work/oracles/macos-arm64-c27-corpus-20261114/` (786 scenes, 0 failures, every film repeat-identical; 108 s).
By scene path and film index, all 1,452 films of the base run, all 68 of the survey and all 1,572 of the batch-1 corpus run
are bitwise identical, and the SHA-256 over every film is that of batch 1 (`4155135787c5...`): C27 moved no film. Pass films:
2,870 of the base run's 2,872 and 2,934 of the batch-1 run's 2,936; the two that differ are again `passes/data-passes`
pass 8, `RenderTime`.

**Unverified.** A build with CMake older than 4.2 (the warning path) was not run. The other platforms are untouched (their
recipes, `inject_cycles.cmake`, and the definitions of the shared dependency recipe they use).

#### Batch 2: B12, B19, B20, D30 and 28(b)

Decisions 20, 21, 23, D30 and 28(b), made after C27 in the order D30, B19, B12, 28(b), B20, on the same uncommitted tree.
Each item's cumulative state is saved as `<repo>/native/work/partD-20261114/batch2-<item>.*`. Every new public behaviour is
**PROVISIONAL**. No export or struct size moved (65 exports, `CBCImageConfig` 32 bytes); one header constant was added
(B19).

#### D30: the oracle runner's other sources in the Metal profile

`tools/licensing/metal_sources.py` lists `native/oracle/json.h`, `native/oracle/sha256.h` and `native/oracle/CMakeLists.txt`
beside `runner.cpp`, so payload staleness, the build's recorded source status and the stager's clean-tree check cover
everything the runner's build reads. `tools/licensing/tests/test_guards.py` asserts the four runner files in the list (11
tests pass); `MAINTAINER-README.txt` (the profile table) says so. `sourceDateEpoch` is unchanged (the newest commit that
changed a Cycles-with-Metal source is still `f2586fc17`).

#### B19: the bounding radius of each splat

**What was done.** `cbc_host_attribute_get_floats(host, cloud, "radius", CBC_ATTR_SPLAT_BOUNDING_RADIUS, ...)`, a new flag
(4; may be combined with `CBC_ATTR_STANDARD`), no new export: it returns, one float per point (the centre step, as a read
without the flag returns the authored one), the radius Cycles' device update computed for each splat
(`PointCloud::update_gsplat_radii`, `scene/pointcloud.cpp`: 0 below an opacity of 0.01, else the largest component of
`|sqrt(2 ln(opacity / 0.01)) * scale|`), which is the cloud's radius attribute from then until the next edit of the cloud
writes the authored one back. The host holds the authored radius from the start of a render (`keep_splat_radii`) and Cycles
clears a geometry's modified flags only when its device update has finished (`GeometryManager::device_update`), so the value
is ready exactly when the host holds the authored radius and the cloud is not modified; otherwise `CBC_NOT_READY` ("The
bounding radius of a Gaussian splat cloud is Cycles' and is read after a render or bake has computed it for the cloud as it is
(not before the first render, nor after an edit of the cloud)"). Refused by name, `CBC_INVALID`: a geometry that is not a
point cloud with `render_as` `gaussian_splats` ("CBC_ATTR_SPLAT_BOUNDING_RADIUS reads the bounding radius of a Gaussian splat
cloud only ..."), a name other than `radius`, `CBC_ATTR_SUBDIVISION`, and the flag on any other attribute call ("Unknown
attribute flags"). Kit rule 7: on the WebGPU backend, which renders no splats, the read is `CBC_UNSUPPORTED` with the existing
words "WebGPU Gaussian splats not implemented" (capability entry "point clouds rendered as Gaussian splats", unchanged).
Reads without the flag keep returning the authored radius.

Files: `native/host/cycles_host.h` (the constant, PROVISIONAL), `native/host/host_scene.inc` (`splat_bounding_radius`),
`src/CodeBrix.Graphics3D.Cycles/AttributeFlags.cs` (`AttributeFlags.SplatBoundingRadius`, PROVISIONAL; `GetAttribute` and
`CyclesHost.GetAttribute` pass it through), `native/host/ABI.md` (Point clouds, Edits after a render, the enum list),
`AGENT-README.txt` (GAUSSIAN SPLATS, the radius bullet).

| Signature | Reason | Alternative considered |
| --- | --- | --- |
| `CBC_ATTR_SPLAT_BOUNDING_RADIUS` (4) on `cbc_host_attribute_get_floats`; `AttributeFlags.SplatBoundingRadius` | decision 21: the smallest surface, no new export | a new export, or a pseudo-attribute name (would collide with custom names) |
| `CBC_NOT_READY` after an edit, until the next render | the computed radius describes the cloud as it was rendered | keeping a copy of the last render's radius (stale for an edited cloud) |

**Tests.** `GaussianSplatTests` (2 new): not ready before the first render; after it the flag reads exactly (bit for bit) the
radii `BoundingRadii` computes in C# from `radiance:base` and `scale`, also with `CBC_ATTR_STANDARD`, unlike the authored ones,
which plain reads still return; the same after a second render; not ready after a `scale` edit and again after a `radiance:base`
edit, then the new radii, a splat of opacity 0.005 reading 0; the refusals above through the facade and `CyclesHost`, the size
query (`required` 24), and a cloud switched to points. `GpuRenderTests.the_splat_bounding_radius_reads_on_metal_as_on_the_cpu_and_is_refused_by_name_on_webgpu`:
not ready before the Metal render, then both clouds' radii bit for bit the CPU host's and the C# formula's (Cycles computes them
on the host for every device). `NativeHeaderConstantMatching` gains the constant (its count test follows the two lists).

**Note.** `cbc_host_attribute_remove` runs the edit preparation (`before_edit`) before it checks its arguments, so a refused
removal on a splat cloud already writes the authored radius back and makes the flag `CBC_NOT_READY` until the next render
(existing behaviour; the test orders its calls around it).

**Unverified.** The WebGPU branch of the GPU test; the Linux, Windows and Android compilers.

#### B12: the tile size of a tiled image

**Established from the pinned source.** Cycles' texture cache takes square tiles whose side is a power of two
(`ImageCache::load_image_tiled`, `scene/image_cache.cpp`, asserts it; `kernel_image_tile_map`, `kernel/util/image_2d.h`, maps
texels to tiles by shifts) and never uses one below 64: `ImageMetaData::detect_tiles` (`util/image_metadata.cpp`) accepts a
file's power-of-two square tiles of at least `KERNEL_IMAGE_TEX_PADDING * 4` and loads them in blocks of
`DebugFlags().texture_cache.min_tile_size`, 64 (`util/debug.cpp`). The largest is the device's: the tiles of one format and
interpolation share pool images of 64 tiles of (t + 4) x (t + 4) pixels in a row, allocated whole (`ImageCache::alloc_tile`).
Metal refuses an image wider than 16384 (`MetalDevice::image_alloc`), so 128. The CPU device checks no image size (B8); its
kernel finds a texel of the pool by a float coordinate, the level coordinate plus the tile's place in the pool
(`kernel_image_tile_map`), floored by `ImageInterpolator` (`kernel/util/image.h`), and binary32 holds every texel centre only
up to 2^23: tiles of 65536 give a pool 64 x 65540 = 4,194,560 pixels wide, tiles of 131072 one beyond 2^23. So the CPU
backends take 64 to 65536 (Cycles' int arithmetic, the pool width in `alloc_tile`, would go to 2^24; above 2^19 the tile
offsets themselves are no longer floats, and on this machine `posix_memalign` refuses the pool of 2^20 tiles, which Cycles
does not survive: `device_memory::host_alloc` throws `std::bad_alloc`).

**What was done.** `CBCImageConfig.tile_size` (**PROVISIONAL**, the first reserved field; the struct stays 32 bytes):
`cbc_host_image_create_tiled` takes 0 (64, the default) or a power of two from 64 to 128 on the Metal backend and to 65536 on
the others (WebGPU included, where the render is refused anyway); anything else is `CBC_INVALID` ("Invalid tiled image tile size
N: 0 (the default, 64) or a power of two from 64 to M ..."), `release` running before the call returns.
`cbc_host_image_create` and `cbc_host_image_update` need 0 ("A tile size applies to tiled images only ..."). The loader reports
the image's size (`TiledImageSource::tile_size`), and an image smaller than its tile in both sides loads whole. The positional
initializers `{sizeof, w, h, format, {0}}` of the repository's C consumers and of the runner became `{..., format, 0, {0}}`
(`abi_consumer.c` twice, `concurrency_consumer.c`, `consumer.c`, `reload_consumer.c`, `runner.cpp`; the runner's bytes did not
change). Managed (**PROVISIONAL**): `CyclesScene.CreateTiledImage(..., uint tileSize = 64)`, `CyclesHost.CreateTiledImage(...,
uint tileSize = 0)`, `CyclesAbi.CreateTiledImage(..., uint tileSize = 0)`, `Native.ImageConfig.TileSize`, `TiledImage.MinTileSize`
(64) and `MaxTileSize` (65536), `TiledImage.LevelCount` and `LevelSize` with a `tileSize` parameter (64 by default). Documents:
`native/host/ABI.md` ("Tiled images": the rule, its sources, the memory a pool takes, the struct table), `AGENT-README.txt`
(TILED IMAGES, the API lists), `native/host/README.md`, `native/DEVICE.md` item 172. The macOS host gate's contract checks 32,
96 and 131072 refused and released at once, 128 and 65536 created and released once by the destroy, and a plain image with a tile
size refused.

| Signature | Reason | Alternative considered |
| --- | --- | --- |
| `CBCImageConfig.tile_size` (was `reserved[0]`) | a field of the existing config (decision 20, the smallest surface) | a parameter of `cbc_host_image_create_tiled`; the source cost is the positional initializers above (the B3 precedent) |
| 64 to 128 on Metal, 64 to 65536 elsewhere, powers of two | exactly what the pinned Cycles takes on each device (above) | 2^24 on the CPU (Cycles' int bound; wrong texel addressing and allocation failures long before) |
| `TiledImage.MinTileSize`, `MaxTileSize`, `LevelCount`/`LevelSize(..., tileSize)` | a reader must produce the levels of its own tile size | documentation only |

**Tests.** `TiledImageTests` (23 new cases):
- At every CPU size, 64 to 65536: an 8t x 8 image whose four levels are solid colours on the strip of four quads (UV extents
  2^(k+7)/t, so the anisotropy-limited footprint selects level k) renders bit for bit like plain images of the level colours;
  levels 0 to 3 are read, the widest read is t + 2 (a tile and its border; 15 reads at 64 and 128, 11 at 256, 7 at 512, 4 from
  1024 up).
- At every CPU size: a (2t + 64) x 8 pattern magnified across the edge of its first two tiles renders bit for bit like the same
  pixels bound plainly (Closest), level 0 only, both tiles read.
- Refusals: 1, 8, 32, 63, 65, 96, 100, 1000, 65535, 65537, 131072, 2^20, 2^31 and `uint.MaxValue` by name; the level
  helpers at other tile sizes. Above 1024 the size cases skip outside macOS: the pool needs (t + 4)^2 x 256 bytes of address
  space for byte4 (1.1 TB at 65536), which macOS reserves without memory until a page is written and Linux and Windows would
  refuse or commit (here every case ran, in 2 s).
- `GpuRenderTests.tiled_images_at_every_metal_tile_size_select_mip_levels_and_render_like_plain_images` (64 and 128): on Metal
  the strip equals plain level colours and the pattern the plain image, each bit for bit; against the CPU's tiled pattern
  maximum 9.5e-7, 0 practical failures (1,952 and 1,728 strict differences of 65,536); 256 refused by name.
- The rest of `TiledImageTests` and the Metal tiled-image tests pass unchanged.

**Unverified.** The C consumers' new initializers under GCC 14, clang-cl `/W4 /WX` and the Android NDK; the size cases above
1024 on Linux and Windows (skipped there by design); WebGPU (tiled images stay refused at render).

#### 28(b): a tiled image inside a tiled render and a camera-border render

**Reading.** After a texture cache miss on a GPU, Cycles runs the kernel that missed again with the path's own state
(`integrator_path_cache_miss`, `kernel/integrator/state_flow.h`), whose `render_pixel_index` addresses the tile's or border's
buffer. Only a miss of the camera ray itself rebuilds the pixel from the camera width (`integrator_init_from_camera` without a
tile, `kernel/integrator/init_from_camera.h`), and only an OSL camera can miss there (`camera_sample_custom`,
`kernel/camera/camera.h`), which this library does not build. **INFERENCE**: the suspected wrong pixel cannot happen here.

**Test.** `GpuRenderTests.a_tiled_image_renders_inside_a_tiled_render_and_a_camera_border_like_the_image_bound_plainly`, three
variants (tiles of 16; a camera border of 72 x 56 pixels; both), 128 x 128, one sample: an orthographic view of a 256 x 256
pattern magnified to half a texel per pixel across four of its 64-pixel tiles, an emission and no bounce, so only level 0 is
read and the tiled image must equal the plain one. Measured, each against the same render with the same pixels bound plainly:

| Variant | CPU | Metal | Reads (CPU, Metal) | Metal vs the CPU's tiled image |
| --- | --- | --- | --- | --- |
| tiles of 16 | bit for bit | bit for bit | 16, 16 | maximum 9.5e-7, 0 practical failures, 1,612 strict differences |
| camera border | bit for bit | bit for bit | 4, 4 | maximum 9.5e-7, 0, 380 |
| border and tiles | bit for bit | bit for bit | 10, 10 | maximum 9.5e-7, 0, 380 |

It renders right on both devices; nothing is refused. (In tiles the cache reads more; **INFERENCE**: Cycles evicts the tiles
a tile's render did not use, `ImageCache::evict_unused`.) Recorded in `native/host/ABI.md` ("Tiled images") and `AGENT-README.txt` (TILED
IMAGES). A first version of the test used the lit scene of the Metal tiled-image test, whose minified quad reads mip levels a
plain image does not have (414 to 1,147 practical failures on the CPU alone): that is the cache's mip mapping, not a restart,
and the scene was replaced by one that reads level 0 only.

#### B20: quaternion attributes on curves

**What was done.** Quaternion attributes are accepted on hair on `CBC_ATTR_MESH`, `CBC_ATTR_CURVE` and `CBC_ATTR_CURVE_KEY`, the
domains the pinned kernels fetch curve attributes from (`curve_attribute`, `kernel/geom/curve.h`: per key mixed along each
segment, per curve; `primitive_surface_attribute`, `kernel/geom/primitive.h`: the mesh domain); the device upload packs them for
any geometry (`scene/geometry_attributes.cpp`), and the curve-key normal domain takes normals only (`CBC_INVALID`). Volumes keep
refusing quaternions ("Unsupported feature: quaternion attributes are accepted on meshes, point clouds and curves only").
Spherical-harmonics attributes stay refused on curves, by name: the only fetch of `attributes_spherical_harmonics_rest` in the
pinned kernels is the Gaussian splat's (`kernel/geom/gsplat.h`), and the Attribute node returns no value for them on any
geometry (`svm_node_attr_surface_eval`, `kernel/svm/attribute.h`) ("Unsupported feature: spherical-harmonics attributes are
accepted on meshes and point clouds only (the pinned kernels read them only for Gaussian splats, kernel/geom/gsplat.h, and
never on curves)"). Kit rule 7: the WGSL device refuses any non-empty quaternion table, whatever the geometry ("WebGPU
quaternion attributes not implemented", `native/cycles/closest.cpp`, unchanged); the capability entry "quaternion and
spherical-harmonics attributes read by a shader" is unchanged. Files: `native/host/host_scene.inc`, `native/host/cycles_host.h`
(comment), `native/host/ABI.md` (Geometry and Curves rows), `AGENT-README.txt`, `native/oracle/README.md`, the scene below.

**Tests.** `GeometryAttributeTests`: the Attribute-node theory gains curves (thick, one curve covering the view) with the
quaternion per key, per curve and on the mesh domain: Color and Vector w, x, y and Alpha z bit for bit, Fac their average within
1e-6; readback exact on the three domains; per key, the centre of a two-key curve reads the keys' average within 0.03 (mixed one
component at a time); the refusal test now expects spherical harmonics on hair refused with the new words (decision 23 reversed
its previous expectation that a quaternion on hair is refused) and adds the curve-key normal domain, a short count and the curve
domain on points (`CBC_INVALID`). `GpuRenderTests.quaternion_attributes_on_curves_render_on_metal_like_the_cpu_and_are_refused_by_name_on_webgpu`
(3 domains x Color, Fac and Alpha): on Metal every film bit for bit the CPU's, the centre pixels the quaternion's values.

**Oracle scene.** `attributes/quaternion-curves` (`author_attributes.py`, `--check` passes; the other five documents are byte for
byte unchanged): the quaternion material of `quaternion-mesh` on three tufts, per key (not all of unit length) and per curve on
thick curves and on the mesh domain of ribbons. Rendered into `<repo>/native/work/oracles/macos-arm64-batch2-b20-20261114/` with
`starter/diffuse-triangle` on the three backends: the CPU repeat-identical (bitwise), Metal repeats within 1.2e-7; reported:
Metal BVH2 against the CPU 124 practical failures (maximum 0.039), MetalRT 865 (maximum 0.23), as the committed curve scenes
show between the devices (for example `curves/attributes-ribbon` 6 and 702, `curves/hair-info-linear` 6 and 1,354 in the base
run).

**Unverified.** The WebGPU branch of the GPU test; the Linux x64 CPU films of the new scene.

#### Verification of batch 2

- Build: `python3 tools/build_macos.py --jobs 8 --skip-dependencies` passed, 0 warnings in the host build log and the build
  output. Host library `b2448ebb8f6b01f718d76cf1db9ef3073047c2b49f075dc26100e7f96619dc37`, runner
  `1f90b7df1bac4910b08ddae6a78de82246df3d87f07a75f9ff248f4899e692d2` (unchanged since C27), 65 exports.
- macOS host gate (`<repo>/native/work/macos-validation-20261114n/`), with B12's contract checks: passed, 3,102 films compared,
  0 same-host bitwise failures, Metal 0 practical failures, maximum 4.5e-8; 17 s.
- Testing document step 7 with .NET SDK 11.0.100: restore, both builds, pack and the tests exit 0. Release: 521 tests, 515
  passed, 6 skipped (the WebGPU-only tests), 0 failed, in each of three full runs (54 to 59 s). Debug: the same counts (55 s).
  The Release build's one warning is the pack driver's ("linux-x64: payload is STALE", now 435 files: C27's
  `tools/dependencies/build_macos.py` is a Cycles-with-WGSL source too); Debug has 0 warnings. The pack folder was deleted.
- Rule 9: the whole committed corpus (785 documents: batch 1's 784 and `attributes/quaternion-curves`) and the 2 bench
  documents on `cpu-arm64-macos` alone into `<repo>/native/work/oracles/macos-arm64-batch2-corpus-20261114/` (787 scenes, 0
  failures, every film repeat-identical; 110 s). By scene path and film index, all 1,452 films of the 726 base-run scenes, all
  68 of the 34 survey scenes and all 1,572 films of the 786 scenes of the C27 corpus run (every D1, D2, D3 and batch-1 scene)
  are bitwise identical; nothing moved and no scene left the corpus. The new scene's two films equal those of its own run
  above. Pass films: 2,870 of the base run's 2,872, all 24 of the survey's and 2,934 of the C27 run's 2,936; the two that
  differ are again `passes/data-passes` pass 8, `RenderTime`.
- The consumer test of the MacOSArm64 package as before: a scratch stage (`<repo>/native/work/staging/osx-arm64-batch2`, never
  approved; its standalone CPU film equals the oracle run's, the same film hash as batch 1's stage), packed as 1.0.319.330 (323
  files match, "NOT APPROVED, NOT PUBLISHABLE"); `dotnet build`, `run`, `publish`, `publish -r osx-arm64 --self-contained
  false` and `--self-contained` each rendered on the CPU, on Metal and on Metal with GPU denoising, the 292 kernel sources beside
  the library and none flattened, the library reporting 1.0.20261114.0; without `codebrix-cycles/` Metal failed with "Metal
  kernel sources are missing beside the native library" and the CPU rendered; the cached runtime pack
  `Microsoft.NETCore.App.Runtime.osx-arm64` 10.0.15. The stage, the feed, the pack folder and the app copies were deleted.
- Count literals: the export count (65), the struct sizes and the capability report's lists did not move; the one new header
  constant is checked by `NativeHeaderConstantMatching`. The other platforms' gate scripts were read (`host_gate.py`,
  `macos_x64_host_gate.py`, `windows_host_gate.py`, `linux_arm64_host_gate.py`, `android_host_gate.py`, and `bake_gate.py`,
  whose `ImageConfig` passes zeros): none checks anything that moved, so none changed (unverified on those platforms).
- `python3 tools/licensing/audit.py`: passed. `git diff --check`: clean.

#### C24: subdivision-surface baking

Decision 26, made after batch 2 on the same uncommitted tree; the cumulative state is saved as
`<repo>/native/work/partD-20261114/c24.*`. Every new or changed public behaviour is **PROVISIONAL**. No export, enumeration
value or struct moved (65 exports); one entry joins the capability report's every-backend rejection list.

**Established from the pinned source (the design came first).**
- Cycles tessellates a subdivision mesh in the device update of each render or bake: `GeometryManager::device_update`
  (`scene/geometry.cpp`) calls `Mesh::tessellate` (`scene/mesh_subdivision.cpp`) for every modified mesh whose positions or
  dicing settings changed (`Mesh::need_tessellation`), with `SubdParams` of the mesh's `subd_dicing_rate` and `subd_max_level`
  and, only for `subd_adaptive_space` pixel, the scene's dicing camera and `subd_objecttoworld`. Without OpenSubdiv every
  face is a bilinear patch (`LinearQuadPatch`; an n-gon becomes n quads around its centre); `DiagSplit` (`subd/split.cpp`)
  sets each edge's segment count from the patch positions (in pixel space divided by the camera's pixel size,
  `Camera::world_to_raster_size`), and `EdgeDice` (`subd/dice.cpp`) writes the triangles into the mesh in a fixed order. Each
  subdivision attribute is copied into the mesh's attributes and interpolated over the patch (`SubdAttributeInterpolation`,
  `subd/interpolation.cpp`: a corner UV bilinearly per triangle corner). In object space the triangles, their order and
  their UVs therefore follow from the faces, the cage positions and the two settings alone.
- Why the host saw no triangles (survey row 8b): `cbc_host_bake` builds its bake map from the mesh's triangles and UV
  attribute (`host_bake.inc`) before `session.start()`, and a cage has no triangles until the bake's own device update has
  diced it; its UV layout is a subdivision attribute, not a mesh attribute.
- The pinned kernels hold no subdivision code (no `subd` in `kernel/`): every device receives the diced mesh as plain
  triangles. Cycles never bakes a subdivision mesh with its object transform (`ObjectManager::apply_static_transforms`), so
  the bake object stays in object space, and the WGSL bake (`native/cycles/bake.cpp`, `wavefront_bake.wgsl`) places such
  a texel through the object transform (`if(!applied)`, the path of `bake/instanced` and of `bake_gate.py`'s instanced and
  true-displacement cases); the WGSL device already renders the diced triangles of the `subdivision/` scenes
  (`native/DEVICE.md` item 170). **Parity finding**: possible in principle and, by reading, needing no WGSL work; the WGSL
  device receives nothing it does not already take. So no WebGPU refusal was added and `native/cycles` is unchanged.
  Unverified: no WebGPU run of a subdivision bake exists yet (see Questions).

**Design.** For a linear cage the host dices a copy of the cage (faces, positions and the UV attribute) with Cycles' own
`Mesh::tessellate` and the `SubdParams` of `GeometryManager::device_update`, maps every texel through the copy's diced UV
attribute with the existing rasterizer, and bakes as for any mesh; the bake's device update dices the real mesh the same
way, so the map's triangle indices are the device's. True displacement moves the diced vertices afterwards and changes
neither the triangles nor their UVs. Pixel-space dicing is refused by name: it measures in the dicing camera, which is the
camera of the scene's last render (or Cycles' default), while a bake sets the camera to the bake's size, so the triangles
would depend on the scene's history. Catmull-Clark stays refused by name.

**What was done.** `native/host/host_bake.inc`: `bake_request` refuses Catmull-Clark with the render's words ("Unsupported
feature: Catmull-Clark subdivision needs OpenSubdiv, which this library does not include") and pixel space ("Unsupported
feature: bakes of subdivision surfaces diced in pixel space (subd_adaptive_space pixel measures the dicing in pixels of a
render camera, which a bake does not have; use subd_adaptive_space object)"), both `CBC_UNSUPPORTED`, and takes a cage's UV
layout from its subdivision attributes ("The bake mesh has no such UV attribute among its subdivision attributes
(CBC_ATTR_SUBDIVISION)", `CBC_INVALID`); `subdivision_bake_map` dices the copy and maps through it; `cbc_host_bake` and
`cbc_host_bake_map` take the map from the request. A mesh with `subdivision_type` linear and triangles but no cage bakes its
triangles (Cycles dices nothing). `native/host/host_api.inc`: the every-backend rejection "bakes of subdivision surfaces diced
in pixel space (a bake has no render camera to measure pixels in)" (kit rule 7: capability report, `AGENT-README.txt`
RENDER-TIME REJECTIONS and `native/host/ABI.md` together). `native/host/cycles_host.cpp`: `#include "subd/dice.h"`.
Documents: `native/host/ABI.md` (Geometry, Edits after a render, Baking: the config table and a "Subdivision surfaces"
paragraph), `native/host/cycles_host.h` (comments), `AGENT-README.txt` (Subdivision surfaces, BAKING TEXTURES, the
rejection and its reason), `native/DEVICE.md` item 170, `docs/PARITY.md` (Texture baking), `MAINTAINER-README.txt`,
`native/oracle/README.md`, `docs/TESTING-MACOS-ARM64.md` (the gate), and the managed XML documentation
(`CyclesScene.Bake`/`BakeAsync`/`ReadBakeMap`, `MeshNode.SetSubdivisionCage`, `CyclesHost.SubdivisionMesh`,
`BakeMap.GetTriangle`). No managed member was added.

**Two host fixes the tests met** (pre-existing, measured on the base of this item, PROVISIONAL behaviour):
- An edit of a subdivision attribute after a render reached no later render: Cycles dices again only when the positions or
  the dicing settings changed, and the attribute calls tag neither (a render after a cage colour edit was bit for bit the
  render before it, unlike a new host with the edit). `cbc_host_attribute_floats`, `_bytes` and `_remove` with
  `CBC_ATTR_SUBDIVISION` now tag the cage's positions (`subdivision_attribute_edited`, `host_scene.inc`).
- A new cage after a render (`cbc_host_subd_mesh`) kept Cycles' count of the vertices the last dicing added
  (`Mesh::num_subd_added_verts`), which `Mesh::get_num_subd_base_verts` subtracts and the cage attributes are sized by
  (`Attribute::element_size`): the new cage's attributes were refused ("Attribute count does not match geometry") and the
  next render ended the process (signal 10, `SIGBUS`). `cbc_host_subd_mesh` now calls `Mesh::clear_non_sockets` first, as
  `Mesh::clear` does for Cycles' own hosts.

| Behaviour | Reason | Alternative considered |
| --- | --- | --- |
| A linear cage bakes through a host-side copy diced by `Mesh::tessellate` | Cycles' own tessellation, at the scene's settings, before the map; no device or export change | building the map lazily in `HostOutput::read_render_tile` after Cycles' device update (no map without a bake, and a map failure inside the session thread); dicing the real mesh in the host (changes host-side arrays before the device update, also for `cbc_host_bake_map`, which renders nothing) |
| Pixel-space dicing refused at bake, every backend | the dicing camera is history-dependent in a bake (above) | dicing in the bake-sized camera; keeping the last render's camera |
| A cage's UV layout is one of its subdivision attributes | the attribute Cycles interpolates over the faces it dices | also accepting a mesh attribute (it has no elements on a cage) |
| Subdivision attribute edits re-dice; a new cage starts from no diced vertices | an edit renders and bakes like a new scene | tagging only bakes (a bake would then change what a later render shows) |

**Tests.** `SubdivisionBakingTests` (5 new, CPU reference): the subdivision family's cage (a non-planar quad and pentagon,
the quad smooth and the pentagon flat, a corner UV island per face, object space, rate 0.3, 6 levels; 378 diced
triangles) in a lit room with a checker in its UV layout.
- Bitwise, without and with true displacement (a wave in the UV layout): Combined, Diffuse Color, Normal and Position of the
  cage equal, bit for bit, the bakes of an ordinary mesh built from the tessellation read back from the subdivided mesh
  after a bake (`triangles`, `P`, `N`, `uv`, `smooth`, `shader`; the displaced case from the undisplaced tessellation),
  displaced by Cycles in the same way; the two bake maps (triangles and coordinates) are equal, and the map read before and
  after the bakes is the same. The ordinary mesh needs a second, invisible object using it: Cycles keeps every subdivision
  surface in object space, and a single-user ordinary mesh is baked into world space, which rounds differently (measured
  with a scratch probe of the same scene: without the second object the Normal bake differs in 431 of 4,608 values, at
  most 3.0e-7, 0 practical failures, the other types bit for bit; with it every type is bit for bit). The ordinary mesh's
  vertex normals read back exactly. The displacement moved the Position bake. So the criterion is bitwise, and it holds.
- Refusals: pixel space (Cycles' default) for the bake and the map, Catmull-Clark, a missing UV name; a linear-type mesh
  without a cage bakes its triangles bit for bit like the same mesh of type none.
- Edits: a render and a bake after a UV layout edit and after a new cage (one more face and vertex) are each bit for bit
  those of a new host built with the edit; a render after a bake equals a new host's.
- `GpuRenderTests.subdivision_surfaces_bake_on_the_gpu_like_the_mesh_of_their_tessellation` (2 cases, 8 films): on Metal
  every type, with and without displacement, against its new recorded reference (`subdivision-bake-<type>[-displaced]`,
  the manifest's `layout` naming the bake's components; recorded CPU comparisons at most 1.4e-5, 0 practical failures) and
  against the Metal bake of the ordinary mesh (0 practical failures, at most 1.2e-7; Position bit for bit); pixel space
  refused. On WebGPU the same code judges the film against the CPU bake (unverified here). `GpuRenderTests.Judge` takes the
  layout of a non-RGBA film for the manifest; the other recorded entries are unchanged.
- macOS host gate contract: on a CPU host of its own, a cage's bake map and bake refused in pixel space (2), its object-space
  map covering texels with indices beyond the cage's two faces and a finite Combined bake (0), Catmull-Clark refused (2).
- Scenes `bake/subdivision-combined`, `-diffuse-color`, `-normal`, `-position`, `bake/subdivision-displaced-combined`,
  `-normal`, `-position` (new `author_bake_subdivision.py`, `--check` passes; `author_bake.py`, `author_bake_motion.py` and
  `author_subdivision.py` `--check` pass, their documents unchanged), rendered with `starter/diffuse-triangle` into
  `<repo>/native/work/oracles/macos-arm64-c24-20261114/`: each CPU film repeat-identical (bitwise); Metal BVH2 and MetalRT 0
  practical failures against the CPU (at most 3.4e-5, displaced Normal), Diffuse Color bit for bit, Position and displaced
  Position repeat-identical on Metal.

**Unverified.** The WebGPU branch of the GPU test and any WebGPU subdivision bake; the Linux x64 CPU films of the seven
scenes; the Linux, Windows and Android compilers of the host change; `scene_edit_gate.py` and `host_gate.py`, whose
subdivision cases set the cage's attributes before the first render or edit the dicing rate (read: unaffected).

**Verification of C24.**
- Build: `python3 tools/build_macos.py --jobs 8 --skip-dependencies` passed, 0 warnings in the host build log and the build
  output. Host library `e32faa334aedd2686321c8d212bd6b823adfd223241d7b6c7e2d26e80dad7e8c` (the same bytes as the first build
  of the change, before the header's comments were edited), runner `1f90b7df1bac4910b08ddae6a78de82246df3d87f07a75f9ff248f4899e692d2`
  (unchanged since C27), 65 exports.
- macOS host gate (`<repo>/native/work/macos-validation-20261114o/`), with the subdivision contract case: passed, 3,102 films
  compared, 0 same-host bitwise failures, Metal 0 practical failures, maximum 4.5e-8; 19 s.
- Testing document step 7 with .NET SDK 11.0.100: restore, both builds, pack and the tests exit 0. Release: 528 tests, 522
  passed, 6 skipped (the WebGPU-only tests), 0 failed, in each of three full runs (73 to 76 s). Debug: the same counts
  (68 s). The Release build's one warning is the pack driver's ("linux-x64: payload is STALE", 435 files, as in batch 2);
  Debug has 0 warnings. The pack folder was deleted.
- Rule 9: the whole committed corpus (792 documents: batch 2's 785 and the seven `bake/subdivision-*`) and the 2 bench
  documents on `cpu-arm64-macos` alone into `<repo>/native/work/oracles/macos-arm64-c24-corpus-20261114/` (794 scenes, 0
  failures, every film repeat-identical; 113 s). By scene path and film index, all 1,452 films of the 726 base-run scenes,
  all 68 of the 34 survey scenes and all 1,574 films of the 787 scenes of the batch-2 corpus run (every D1, D2, D3, batch-1
  and batch-2 scene) are bitwise identical; nothing moved and no scene left the corpus. The new scenes' 14 films equal
  those of their own run above. Pass films: 2,870 of the base run's 2,872, all 24 of the survey's and 2,934 of the batch-2
  run's 2,936; the two that differ are again `passes/data-passes` pass 8, `RenderTime`.
- The consumer test of the MacOSArm64 package as before: a scratch stage (`<repo>/native/work/staging/osx-arm64-c24`, never
  approved; its standalone CPU film equals the oracle run's, the CPU film hash of batch 2's stage), packed as 1.0.319.383
  (323 files match, "NOT APPROVED, NOT PUBLISHABLE"); `dotnet build`, `run`, `publish`, `publish -r osx-arm64
  --self-contained false` and `--self-contained` each rendered on the CPU, on Metal and on Metal with GPU denoising, the 292
  kernel sources beside the library and none flattened, the library reporting 1.0.20261114.0; without `codebrix-cycles/`
  Metal failed with "Metal kernel sources are missing beside the native library" and the CPU rendered; the cached runtime
  pack `Microsoft.NETCore.App.Runtime.osx-arm64` 10.0.15. The stage, the feed, the pack folder and the app copies were
  deleted.
- Count literals: the export count (65) and the struct sizes did not move; the capability report's every-backend list
  gained one entry, which `CapabilityReportTests` compares with `AGENT-README.txt` for every backend and
  `GpuRenderTests.gpu_capability_report_describes_the_platform_gpu_backend` reads on Metal (both pass). The other platforms'
  gate scripts were read: none asserts that list or the old bake refusal, so none changed (unverified on those platforms).
- `python3 tools/licensing/audit.py`: passed. `git diff --check`: clean.

#### Final rulings (2026-11-15)

Decision 34, the last ruling batch of Part D, made on top of the committed Part D tree
(`49cd3a4a4def9eca2d122cfb7d8d749b881c6869`) in the order 1, 4, 5, 7, 8, 10, 6, 3, 16. Each item's cumulative state is saved
as `<repo>/native/work/partD-20261114/final-<item>.*` with its targeted test output in `final-<item>/` (scratch); the
closing checks are in `final/`. Kept as they were (decision 34): the two A2 fixes, no WebGPU refusal of subdivision bakes
(unverified until Linux), and the two C24 fixes. Every new or changed public behaviour is **PROVISIONAL**. No export,
enumeration value or struct size moved (65 exports); the capability report gained one every-backend rejection, a narrower
WebGPU area-light entry, `maxImageDimension` 1073741823 for `cpu` and `cpu-embree`, and `maxImageTileSize` per backend; one
managed member was added and one constant changed.

**1. Area lights with an axis of infinities (decision 22, narrowed).** `validate_unavailable` (`native/host/host_scene.inc`,
reached by `cbc_host_render`, `cbc_host_bake` and `cbc_host_bake_map`) computes, with Cycles' own `transform_get_column`
and `normalize_len`, each axis `AreaLight::copy_to_kernel` (`scene/light.cpp`) gives the kernels, and refuses on every
backend an area light whose u or v axis has no NaN and is not finite: a column of the object transform times the size with
no zero component whose squared length underflows to zero, so the axis is all infinities (`CBC_UNSUPPORTED`,
"Unsupported feature: area lights with an axis of infinities (the light's u axis (its object transform's first column times
sizeu) has no zero component but a length that underflows to zero, so Cycles divides it by zero; give the light a larger size
or object scale)"). An axis that underflows with a zero component holds a NaN and stays accepted (the six base-run scenes of
section 5.4, C22, and the survey's degenerate lights), as do portals (zero-extent portals stay accepted, decision 22) and
lights `Light::has_contribution` disables by their strength, size or a zero transform column (its test of the emission
shader needs the compiled shader and is not made). Kit rule 7, together: the every-backend entry "area lights with an axis
of infinities (an axis with no zero component whose length underflows to zero)" in `native/host/host_api.inc` and
`AGENT-README.txt` (RENDER-TIME REJECTIONS, with its reason under "Why."), and the WebGPU entry "area lights whose axis
length underflows to zero, with a spread of zero or below 1e-5, or reached by the light tree at a transmissive surface"
without "with an axis of infinities" (the precedent of ruling 15; the WGSL device's own refusal in `native/cycles` is
unchanged and is no longer reached through the host); `native/host/ABI.md` (Lights row), `native/DEVICE.md` item 148.
**The moved scene.** `lamps/area-degenerate-infinite-axis.json` left the committed corpus (deleted from
`native/oracle/scenes/lamps/`); `author_degenerate_lights.py` writes it only with `--scratch DIR` (as `DIR/lamps/<name>`),
refuses to run while a scratch document is in the committed folder, and `--check` passes for the 11 committed documents. The
scratch copy, byte for byte the removed file (SHA-256 `eb2365b8ab13ecbf...`), is
`<repo>/native/work/partD-20261114/final-1/scratch-scenes/lamps/area-degenerate-infinite-axis.json`; `native/oracle/README.md`
says so. Tests: `SceneSettingsTests.Area_lights_with_an_axis_of_infinities_are_refused_by_name_and_other_degenerate_area_lights_render`
(CPU reference and Embree: the skewed light of the scene refused at render and bake naming the u axis, the v axis alone named,
an unrotated 1e-23 scale and a 1e-23 size render finite, render and bake, the portal and the zero-strength light render, and
the light restored renders bit for bit its first film), `GpuRenderTests.gpu_area_lights_with_an_axis_of_infinities_are_refused_by_name_and_a_nan_axis_renders`
(Metal), `CapabilityReportTests.area_lights_with_an_axis_of_infinities_are_a_rejection_of_every_backend` and the word-for-word
list comparison; 15 targeted tests passed.

**4. The denoiser upscale factor at bake (ruling 14 extended).** `cbc_host_bake` refuses it after `validate_geometry`, before
the bake map, any restore or the session (`validate_upscale_factor`, `native/host/cycles_host.cpp`, which `cbc_host_render`
now calls in place of its inline check, unchanged in place and words): with denoising, a factor other than 1 is
`CBC_UNSUPPORTED`, "Unsupported feature: a denoiser upscale factor other than 1 (no backend of this library returns an
upscaled film)", on every backend and whatever the denoise device. The size query (capacity 0) still answers, and
`cbc_host_bake_map`, which renders nothing, is not refused. The C26 delivered-film check stays behind both refusals.
Documents: `native/host/ABI.md` (Conventions, Sampling row, Baking), `AGENT-README.txt` (BAKING TEXTURES, "Why."),
`native/DEVICE.md` item 169. Tests: `DeliveredFilmTests.a_bake_with_a_denoiser_upscale_factor_is_refused_by_name_before_it_bakes_as_a_render_is`
(untiled and in tiles of 16: the bake and the render refused by name with no render event, the bake map equal to a plain
scene's, a bake at 2 without denoising bit for bit the bake at 1 without denoising, and after the refusal the bake at 1 bit
for bit a new scene's; it replaces the test that expected a CPU bake at 2 to equal the bake at 1) and
`GpuRenderTests.a_gpu_bake_with_a_denoiser_upscale_factor_is_refused_by_name_before_it_bakes` (Metal, GPU and CPU denoising
refused, the factor 1 bakes; it replaces the test that expected the delivered-film failure); 23 targeted tests passed.

**5. The largest image side of the CPU backends.** Cycles' CPU kernels compute `2 * width` and `2 * height` in `int` for the
Mirror extension (`wrap_mirror`, `kernel/util/image.h`). At render, bake and bake map on `CBC_HOST_CPU_REFERENCE` and
`CBC_HOST_CPU` the host refuses every image bound whole (each UDIM tile too) whose loaded size exceeds 1073741823 in
either side (`images_within_cpu_limit`, `CBC_UNSUPPORTED`, "Unsupported feature: an image wider or taller than 1073741823
pixels on the CPU backends (...); this image loads at W x H[ with the texture limit]: ..."). The loaded size is the one Cycles
computes from the texture limit (`loaded_image_size`: `ImageManager::device_load_image`, `ImageCache::load_full`,
`util_image_resize_pixels`, in binary32, in which a limit of 1073741823 is 2^30 and leaves an image of 2^30 whole). Tiled
images are not bound by it. `backendScope.cpu.maxImageDimension` and `cpu-embree`'s are 1073741823 (were `INT_MAX`);
`limits.maxImageDimension` stays `INT_MAX`. Documents: `native/host/ABI.md` (Backends and capability queries),
`AGENT-README.txt` (BackendScope, TEXTURE LIMIT), the `BackendScope.MaxImageDimension` XML documentation. Tests:
`TextureLimitTests.an_image_wider_or_taller_than_1073741823_pixels_is_refused_by_name_on_the_cpu_backends` (a 2^30 x 1
and a 1 x 2^30 byte image on the CPU reference, 2^30 x 1 on Embree: refused by name, also under a texture limit of
1073741823) and `CapabilityReportTests.the_report_states_the_largest_image_side_and_tile_size_of_every_backend`; 12
targeted tests passed. Not run: an image above the limit reduced by a smaller texture limit (Cycles' resize of a 1 GiB
image stages 4 GiB).

**7. The CPU tile size of a tiled image.** `tiled_image_largest_tile_size` (`native/host/host_scene.inc`): 128 on Metal,
1024 on every other backend (was 65536); the Metal reason is unchanged, the CPU one is the pool's memory (Cycles allocates
64 tiles of (t + 4)^2 pixels whole: 271 MB for byte4 tiles of 1024, 1.1 GB for float4, four times as much at 2048, 1.1 TB of
address space at 65536). The refusal keeps its words with "from 64 to 1024". The capability report states each backend's
largest as `backendScope.<backend>.maxImageTileSize` (**PROVISIONAL**: `cpu` and `cpu-embree` 1024, `metal` 128, `webgpu`
1024, which refuses tiled images at render). Managed (**PROVISIONAL**): `long? BackendScope.MaxImageTileSize` (null when a
report does not state it) and `TiledImage.MaxTileSize` 1024 (was 65536). Documents: `native/host/ABI.md` (Backends, Tiled
images), `native/host/cycles_host.h`, `AGENT-README.txt` (TiledImage, BackendScope, TILED IMAGES), `native/DEVICE.md` item
172, the XML documentation of `CyclesScene.CreateTiledImage` and `CyclesHost.CreateTiledImage`. The macOS host gate's
contract now expects 32, 96, 2048 and 65536 refused and released at once, 128 and 1024 created and released once by the
destroy. Tests: `TiledImageTests` at every CPU size from 64 to 1024 (the macOS-only cases above 1024 are gone), the refusal
list with 1025, 2048, 4096, 65536 and above, a tiled image of tile size 1024 created, the level helpers refusing 2048 and
65536; `GpuRenderTests` reads 128 on Metal; 48 targeted tests passed.

| Signature | Reason | Alternative considered |
| --- | --- | --- |
| `backendScope.<backend>.maxImageTileSize` (JSON); `long? BackendScope.MaxImageTileSize` | decision 34: each backend's largest tile in the report, beside `maxImageDimension` (B8) | a `limits` entry (one value for every backend) |
| `TiledImage.MaxTileSize` 1024 | the largest tile of any backend | keeping 65536 (no backend takes it now) |

**8. A refused attribute call changes nothing.** `cbc_host_attribute_floats`, `cbc_host_attribute_bytes` and
`cbc_host_attribute_remove` called `before_edit` (which writes a splat cloud's authored radius back, restores a baked or
displaced geometry and tags it) before they checked their arguments; the three now check every argument and stage the value
first, then prepare the edit and apply it. The checks look at the attributes the edit applies to (`edited_attribute`: the
authored copy of a displaced mesh or of a geometry the static BVH path baked, else the live set), so a call is accepted or
refused exactly as before; `cbc_host_attribute_remove` looks the attribute up again after the preparation. The other
attribute calls (`cbc_host_attribute_get_floats`, `_get_bytes`) prepare no edit. Documents: `native/host/ABI.md` (Edits
after a render), `AGENT-README.txt` (EDITS AFTER A RENDER). Test: `GaussianSplatTests.Refused_attribute_calls_change_nothing_and_the_bounding_radius_stays_readable`
(after a render, ten refused calls of the three exports, each by name, leave `CBC_ATTR_SPLAT_BOUNDING_RADIUS` reading Cycles'
radii and plain reads the authored ones, the next render is bit for bit the first, and an accepted removal makes the radius
not ready). Control (the three calls with the preparation first again, in a scratch build of the same tree, then restored
and rebuilt; `final-8/control.patch`, `control-test.txt`): the test fails at the first refused call with "The bounding
radius of a Gaussian splat cloud is Cycles' and is read after a render ...". 82 targeted tests passed (splats, geometry
attributes, displacement edits, the host and ABI tiers, subdivision baking).

**10. Another subdivision surface diced in pixel space in a baked scene.** `validate_bake_dicing`
(`native/host/host_bake.inc`), called by `cbc_host_bake` after the upscale check: a bake of any object is refused while an
object of the scene uses another mesh with `subdivision_type` linear, subdivision faces and `subd_adaptive_space` pixel
(`CBC_UNSUPPORTED`, "Unsupported feature: bakes of subdivision surfaces diced in pixel space (another subdivision surface of
the baked scene has subd_adaptive_space pixel, which a bake would dice in the camera of the scene's last render, ...)"). The
source: `GeometryManager::device_update` (`scene/geometry.cpp`) dices each modified cage whose positions or dicing settings
changed (`Mesh::need_tessellation`, `scene/mesh.cpp`) and hands `Mesh::tessellate` the scene's dicing camera for pixel
space, in whose pixels `DiagSplit::T` (`subd/split.cpp`) measures every edge; `cbc_host_render` keeps the dicing camera
equal to the render camera and a bake sets neither, so the surface is diced in the last render's camera (Cycles' default
before any render), or keeps the last render's dicing when it was not modified since. `cbc_host_bake_map` maps the target
alone and is not refused for another surface; a cage no object uses is diced into nothing a ray meets and is not refused. The
capability entry "bakes of subdivision surfaces diced in pixel space (a bake has no render camera to measure pixels in)" is
unchanged and now covers every surface of the baked scene (`AGENT-README.txt` "Why." and BAKING TEXTURES, `native/host/ABI.md`
Baking, `native/DEVICE.md` item 170). Tests: `SubdivisionBakingTests.a_bake_is_refused_by_name_while_another_subdivision_surface_is_diced_in_pixel_space`
(the bake of the cage and of an ordinary mesh refused, before and after a render, the map not, the second cage without an
object not; switched to object space the bake is bit for bit that of a new scene built so) and the host gate's contract
(`otherPixelSpaceBakeStatus` 2, `otherPixelSpaceMapStatus` 0); 22 targeted tests passed (the GPU subdivision bakes
included).

**6. CMake 4.2 or newer for the osx-arm64 recipe.** `native/tools/inject_cycles_macos.cmake` refuses an older CMake when it
configures the host build (`FATAL_ERROR`, "The osx-arm64 recipe requires CMake 4.2 or newer (this is CMake <version>): it
names the host's objects with the OBJECT_NAME source property (CMake 4.2) for a build that is the same at every checkout
path and a dSYM with the line tables of every object"; it replaces the check for 3.24) instead of warning in
`codebrix_checkout_independent_objects`, which no longer has a fallback. `build_cycles_macos.py` checks no version itself.
Documents: `docs/TESTING-MACOS-ARM64.md` section 2, `native/host/MACOS_ARM64.md` (twice), `MAINTAINER-README.txt`. Checked
with `cmake -P` of the injection under a shadowed `CMAKE_VERSION` (`final-6/version-check.txt`): 3.31.6 and 4.1.2 refused
with that message, 4.2.0 and the installed 4.4.5 past it (to the next check, the profile's, which script mode does not
meet); the build itself configured with 4.4.5.

**3. Displaced meshes: two inputs the host does not track (deferred, documented).** A displacement shader that reads a scene
value (Scene Time) is not displaced again when only that value changes; on Metal with MetalRT, Cycles' switch to its extended
limits (`MetalDevice::set_bvh_limits`, from `Scene::update_kernel_features`) tags every geometry modified inside the render's
scene update without the host's restore, so a displaced mesh could be displaced twice (known from reading only; no film
shows it). Documented as a known limitation in `native/host/ABI.md` ("Edits after a render"), `AGENT-README.txt` (EDITS
AFTER A RENDER), `native/DEVICE.md` (new item 191) and section 7.

**16. `git diff --check` against the base commit.** Run against `828f6c2f9ee2c57dd379d472a0252edaf3caa87a` it reported 546
lines in 33 files, not only the 25 known lines (**INFERENCE**: the earlier "clean" results of this report ran `git diff --check`
without the base commit, against the index). The 25 known: 18 lines with CRLF in `windows-arm64-device-20261025/f1-smooth-normal/**` JSON, 2 + 2 lines with
trailing spaces in the two `ldd.txt` and 2 + 1 in `merge-20261023b/merge6/timeline.log` and `checkpoint-2/status.md`. The
others, in evidence folders added on this branch: 488 CRLF lines of apt output in `linux-arm64-cpu-oracle-20261113`
(`environment/step2-apt.txt` 483, `first-attempt/02-apt.txt` 5) and 33 lines with trailing spaces in
`device-macos-arm64-20261113` (`build/cycles-registration.patch` 13, `build/platform.patch` 2,
`build/host-configure.output.txt` 1, `build/refused-attempts/1-skip-dependencies.stdout.txt` 1,
`managed/3-test-release.output.txt` 5, `6-test-debug.output.txt` 5, `7-record-metal-references.output.txt` 2,
`8-test-release-after-recording.output.txt` 2, `9-test-debug-after-recording.output.txt` 2). Each of the 33 files has
only one kind. `.gitattributes` gained one line per file, after the hash-pinned trees, in the style of its entries:
`whitespace=cr-at-eol` for the 20 CRLF files and `whitespace=-trailing-space` for the 13 others; no byte of them changed.
The check against the base commit is then clean (`final-16/diff-check-before.txt`, `diff-check-after.txt`).

#### Verification of the final rulings

- Build: `python3 tools/build_macos.py --jobs 8 --skip-dependencies` passed, 0 warnings in the host build log (a compile of
  `cycles_host.cpp`) and the build output (18 s). Host library `b804223deae0afce07ad33440b312d3ea907b1f528839b375db628e3a1916e5e`,
  runner `1f90b7df1bac4910b08ddae6a78de82246df3d87f07a75f9ff248f4899e692d2` (unchanged since C27), 65 exports.
- macOS host gate (`<repo>/native/work/macos-validation-20261115a/`), with the new contract cases (tile sizes 32, 96, 2048 and
  65536 refused and released at once, 128 and 1024 accepted; a bake refused beside a second cage in pixel space, its map
  not): passed, 3,102 films compared, 0 same-host bitwise failures, Metal 0 practical failures, maximum 4.5e-8; 28 s.
- Testing document step 7 with .NET SDK 11.0.100: restore, both builds, pack and the tests exit 0. Release: 525 tests, 519
  passed, 6 skipped (the WebGPU-only tests: `BakingTests.the_webgpu_device_bakes_...`, `SceneSettingsTests.Dispersion_is_refused_by_name_on_the_webgpu_backend`,
  `WebGpuAmbientOcclusionTests.gpu_ao_pass_...` and the three cases of `GpuRenderTests.webgpu_refuses_the_integer_and_component_nodes_...`),
  0 failed, in each of three full runs (82 to 87 s). Debug: the same counts (85 s). C24's 528 tests gained 9 and lost the 12
  CPU tile-size cases above 1024. The Release build's one warning is the pack driver's ("linux-x64: payload is STALE", now
  437 files); Debug has 0 warnings. The pack folders were deleted.
- Rule 9: the whole committed corpus (791 documents: C24's 792 without `lamps/area-degenerate-infinite-axis`) and the 2
  bench documents on `cpu-arm64-macos` alone into `<repo>/native/work/oracles/macos-arm64-final-corpus-20261115/` (793
  scenes, 0 failures, every film repeat-identical; 133 s). By scene path and film index: all 1,452 films of the 726 base-run
  scenes (`<repo>/native/evidence/cycles-oracles-macos-20261113/`), the six with a NaN axis and `lamps/area-zero-size`
  among them, all 66 films of the 33 survey scenes that stay committed, and all 1,586 films of the 793 scenes the C24 corpus
  run (`macos-arm64-c24-corpus-20261114`, the newest) shares with this one (every D1, D2, D3, batch and C24 scene) are
  bitwise identical. The one scene that left the run is the moved `lamps/area-degenerate-infinite-axis` (its two survey
  films and two C24-run films have no counterpart); no film moved and no scene was added. SHA-256 over every film:
  `024d03fd12e67f176900fdec6cd852409ebbe0e00f261b690a15b884afcc6a68`. Pass films: 2,870 of the base run's 2,872, all 24 of
  the survey's and 2,934 of the C24 run's 2,936; the two that differ are again `passes/data-passes` pass 8, `RenderTime`.
- The consumer test of the MacOSArm64 package as before: a scratch stage (`<repo>/native/work/staging/osx-arm64-final`, never
  approved; its standalone CPU film equals the oracle run's and has the CPU film hash of C24's stage), packed as 1.0.319.1097
  (323 files match, "NOT APPROVED, NOT PUBLISHABLE"); `dotnet build`, `run`, `publish`, `publish -r osx-arm64 --self-contained
  false` and `--self-contained` each rendered on the CPU, on Metal and on Metal with GPU denoising, with no build
  diagnostics, the 292 kernel sources beside the library and none flattened, the library reporting 1.0.20261115.0 (the date
  of the newest commit that changed a Cycles-with-Metal source, now `49cd3a4a4`); without `codebrix-cycles/` Metal failed with
  "Metal kernel sources are missing beside the native library" and the CPU rendered; the cached runtime pack
  `Microsoft.NETCore.App.Runtime.osx-arm64` 10.0.15. The stage, the feed, the restored packages of that version, the pack
  folder and the app copies were deleted.
- Count literals: the export count (65) and the struct sizes did not move; the every-backend rejection list gained one entry
  and one WebGPU entry was narrowed, which `CapabilityReportTests` compares with `AGENT-README.txt` for every backend and
  `GpuRenderTests.gpu_capability_report_describes_the_platform_gpu_backend` reads on Metal (both pass). The other platforms'
  gate scripts and C consumers were read: none asserts the rejection lists, `maxImageDimension`, a tile size above 1024, a
  bake with an upscale factor or with a second cage, so none changed (unverified on those platforms; the Linux
  `emitter_sample` gate drives the WGSL device's own infinite-axis refusal directly and is unaffected).
- `python3 tools/licensing/audit.py`: passed. `python3 -m unittest tools/licensing/tests/test_guards.py`: 11 passed.
  `git diff --check 828f6c2f9ee2c57dd379d472a0252edaf3caa87a`: clean.

**Unverified on other platforms.** The WebGPU branches of the changed GPU tests (the area light, the bake upscale and the
report's `maxImageTileSize` read 1024 there); the Linux, Windows and Android compilers of the host change; the CPU image
limit and the tile-size cap on Linux and Windows (the same host code); the CMake refusal with a real CMake older than 4.2
(the check was run in script mode with a shadowed version).

## 6. Part E: closing checks

In progress (not part of this folder yet).

## 7. Questions for the maintainer

The survey's control build did not reproduce the validated build byte for byte, although every Mach-O section is
identical. The debug map of every artifact records each object's modification time, and the two host objects built from
sources outside the Cycles tree are named after the checkout's absolute path, so the UUID and the code signature change
with every rebuild and with every checkout location. `native/host/MACOS_ARM64.md` says that no build path reaches the debug
map, while its own history note records the `cycles_host.cpp` object path. Should the recipe zero the object times and
give those two objects a checkout-independent name, so that a fresh build can be compared with a staged payload by hash
(the testing document's missing payload comparison), or is the section-level identity enough? (Answered by decision 27:
done, section 5.4, C27.)

Rows 6, 7, 8 and 9 were marked "Refused" or "Refused by name" in the kit, but at the base commit the host refuses only
the subdivision bake (row 8b) and the CPU upscale factor (row 9); the other refusals belong to the WebGPU device. Rows 6, 7
and 8a therefore rendered with the base build, and no survey build was needed for row 7. Rulings 14 and 15 now close rows
9 and 6. Row 7's degenerate lights and zero-extent portals render repeat-identically on the CPU and were committed as
scenes; their CPU and Metal films differ widely in three of the nine (up to 1.1e3 on the axes of infinities, as for the
committed `lamps/area-degenerate-rect`). Do these stay accepted on CPU and Metal while the WebGPU device keeps refusing
them, or should any of them be refused everywhere as rows 6 and 9 will be?

The planned zero-area bake list assumed that every shading type is finite on zero-area texels; six direct-light types
(diffuse, diffuse direct, glossy, glossy direct, transmission, transmission direct) fail with "Nonfinite bake output" on
every backend, as only the flat Normal bake was known to. They were left out of the committed scenes and kept as scratch
documents. Is the host's refusal of the whole bake the wanted behaviour for them, or should the device's NaN in those
texels be a known limitation documented beside native/DEVICE.md item 149?

Lifting the subdivision-bake refusal only reaches the next host check ("The bake mesh has no triangles"): a real
subdivision bake needs the host to tessellate before it builds its bake map, and to find the UV in the subdivision
attributes. Is that wanted as host work, or does the refusal stay?

The undefined Integer Math cases do not crash the CPU or Metal here, but they give different answers: Metal wraps every
overflow, while Apple Clang returns the mathematically true value in three overflow cases and Metal's integer division by
zero gives -1 or 1 where the CPU gives 0. The committed table keeps only defined cases, under the lead's ruling, which also
moved some power cases the plan had listed (an unused last squaring overflows in `power(2, 30)`, `power(10, 9)` and
`power(INT_MIN, 1)`). Should the WebGPU port follow the CPU or Metal in those cases, given that no gold standard defines
them, or should the host refuse a linked Integer Math node it cannot prove safe? The x86-64 behaviour of the trap cases is
still an inference.

BVH time steps change the BVH2 but no film, beyond Metal's repeat noise, on BVH2 or MetalRT. Part D item 3 lists the
setting "only if survey row 11 shows an effect"; the survey shows none in the films. Is the faster traversal alone a
reason to expose it?

The denoiser upscale factor on Metal returned a wrong film without any error, which ruling 14 now closes. The same check
would apply to any future setting that changes the render resolution: should the host verify the size of what the output
driver delivers against the film it was asked for?

Gaussian splats (D2): whether a ray hits a splat follows a hash of the hit position, so no two devices that differ in the
last bit of that position give the same film, and the CPU and Metal films of every splat scene fail the practical
criterion pixel by pixel while converging with the sample count (section 5.2). How should the WebGPU port of splats be
judged against the CPU, which the WGSL device must otherwise match by that criterion: by reproducing the CPU's hit
arithmetic bit for bit, or by a statistical criterion that the gates do not have yet? The D2 rotation refusal goes beyond
an exact zero (a rotation with no normal binary32 square, or whose squared length overflows): is that the wanted reading
of the ruling? And should the bounding radius Cycles computes for each splat be readable through the ABI, now that reads
of radius return the authored one? (Answered by decision 21: section 5.4, B19.)

Radial Tiling with two sides and a roundness strictly between 0 and 1 renders on Metal but differs from the CPU in about
700 components (up to 1.76), which the WebGPU device answers with a refusal; Velvet's subnormal paths agree within the
practical criterion although Metal flushes subnormals. These are recorded here at the current pin; the previous-revision
films of the frozen folder were not compared (rule 8).

Texture limit (D3 gap 3): the capability report's `limits.maxImageDimension` says `INT_MAX` on every backend, while
Cycles' Metal device refuses an image side above 16384 (a texture limit of 16384 now brings such an image within it).
Should the report state the limit per backend? And is "fixed once a render or bake has loaded images" the wanted rule
for a change of the texture limit, or should the host instead re-add every image so that a later change applies (Cycles'
own Sky Texture images would still keep their size)?

BVH spatial splits (D3 gap 4) change no film by design, as BVH time steps do (row 11); they are exposed because decision 17
names them. Is the faster traversal they may give instanced meshes reason enough to keep the export, and may the WebGPU
refusal be lifted once a Linux WebGPU run has rendered split BVHs within the practical criterion? A mesh with true displacement
whose sockets are edited after a render (measured with its `smooth` socket) renders unlike a new scene built with the
edit, because `GeometryManager::displace` adds the displacement to the already displaced positions (section 5.3, gap 4);
the device recovery tags every geometry modified and so, by reading, does the same. Should the host keep and restore the
authored positions of displaced meshes before such an update, as it does for baked geometry, or refuse those edits?

UDIM tiles (D3 gap 1): the Image Texture node's `tiles` socket is Cycles' list of file-name UDIM tiles and has no
effect on bound pixels, a plain binding or a tiles binding, so a consumer can set it and see nothing happen. Should the
host refuse a changing set of it, pointing to `cbc_host_image_bind_tiles`, as it refuses the sockets a render owns? And
may the WebGPU refusal of UDIM tiles be lifted once a Linux WebGPU run has rendered the three `images/` scenes within the
practical criterion of the x64 CPU films?

The Release run of the managed tests failed one timing test in every full run of gaps 1 and 2,
`RenderAsync_cancels_promptly_and_leaves_the_destination_untouched`. The cause was the host's (section 5.3, the note
after gap 2): a cancel waited for the batch of samples being rendered. The host now stops that batch through the path
tracer's own quick cancel, from a thread that each render and bake starts for the purpose, and the test passes in every
full Release run; the test was not changed. Is one such thread per render acceptable, or should the host keep one per
host? And is the sample count after a cancel, held at the last progress event's, the wanted reading of rule 5?

Progressive results (D3 gap 2): growing `CBCHostConfig` by trailing fields, as ABI 1's rule allows, turned every
positional `{..., {0}}` initializer of the repository's C consumers into a "missing field" error under `-Wextra -Werror`;
the ten were completed with the two new zeros, and the ABI document now tells consumers to list trailing fields or use
designated initializers. Is that source-level cost acceptable for an unreleased ABI, or should the flags travel another
way (for example a creation export with a flags argument)? Bakes on a progressive host send no film (a bake's output is
not an image of the scene): is that the wanted behaviour? And may the WebGPU refusal be lifted once a Linux WebGPU run has
shown that a progressive host's final films meet the practical criterion?

Texture cache (D3 gap 6): a tiled image's tiles are fixed at 64 pixels, Cycles' block size; Cycles' Metal device could also
take 128, and nothing larger. Should the tile size be the consumer's choice? (Answered by decision 20: section 5.4, B12.) A tiled image's average colour, which Cycles
returns for a tile still loading on a GPU before the path restarts, is zero: should a consumer be able to give one? The
release callback runs exactly once for every call that passed it, also when the call fails. Is that the wanted rule, or
should a failed call leave the release to the caller? Outside the image (UVs beyond 0..1), Cycles' tiled path wraps the UV
before it filters and its plain path wraps each texel. A tiled image with Linear filtering and Clip, or Cubic filtering and
Clip or Extend, therefore renders a band at the image's edges unlike the same pixels bound plainly. That is Cycles'
behaviour and is documented: should the host refuse those combinations instead? `use_texture_cache` is now on for every
host. A development build that reads texture files would then use an existing `.tx` file beside an image through the
cache, as stock Cycles does: acceptable? Should the oracle runner express tiled images? It could take an `images` entry's
existing raw level-0 file and make the levels itself, with no new file, or read one raw file per level. And may the WebGPU
refusal stay until the WGSL device has a tile cache?

Tiled rendering (D3 gap 5): with adaptive sampling, Cycles' Metal device stops the pixels of small render buffers at other
sample counts than the CPU device, which decides identically at every size; tiles are small buffers, so a tiled adaptive
Metal render can differ from the untiled one well beyond the practical criterion (806 practical failures in the committed
`render-settings/tiles-glass-passes`), and small untiled films never stop early on Metal (section 5.3, gap 5). Should the
host refuse adaptive sampling with tiles on Metal by name until the cause is known, document it only (as now), or should
the cause be pursued in Cycles' Metal adaptive-sampling path? With denoising, a tiled film is bit for bit an untiled render
with a sample count pass rather than a plain one (Open Image Denoise gets per-pixel-normalised input): is the practical
criterion against the plain untiled render the wanted judgement? The device report's `tiledRendering` counters were added
as the proof that tiles stay in memory: keep them as a consumer-visible fact, or move them to a maintainer-only path? And
may the WebGPU refusal be lifted once a Linux WebGPU run has rendered the `render-settings/` twins within the practical
criterion?

Rulings after D3 (section 5.4). C22 stopped at its precondition: an area light whose axis length underflows to zero has
an axis of NaN and infinities in Cycles' kernel data, and six base-run scenes that Cycles renders have one
(`lamps/area-degenerate-rect`, `-disk-plane`, `-ellipse-spread`, `-lamp-shader`, `-tree`, `-volume`), as do every survey
degenerate light and the three zero-extent portals the same decision keeps accepted. Only the survey scene
`lamps/area-degenerate-infinite-axis` has an axis of infinities without a NaN, the WebGPU device's "infinite axis" case.
Should the rule be read as refusing that case only (the survey scene moves to scratch, no base-run film moves), or is
another reading meant, such as a transform or size that is not finite as authored (which no committed scene has)? (Answered by decision 34: section 5.4, final rulings.)

A2 brought two general fixes with it, because the displaced-mesh tests met them: a change of a shader's displacement method
after a render now gives Cycles a new graph to finalize, and a shader first referenced after a render is compiled
(`Shader::tag_used`), each of which rendered unlike a new scene before, also without displacement. Are they wanted in this
form? Two inputs of a displacement are not tracked: scene attributes a displacement shader reads (Scene Time), and Cycles'
own re-tag of every geometry when MetalRT switches to extended limits (above 16,777,216 instances or 268,435,456
primitives), which displaces again without the host's restore. Should the host track them, or document them? (Answered by decision 34: section 5.4, final rulings.)

C26: bakes accept the denoiser upscale factor, which ruling 14 refuses for renders. A CPU bake ignores it (bit for bit the
bake at 1), and a Metal bake with GPU denoising now fails through the delivered-film check ("render buffers of 16 x 16 ...
for a render of 32 x 32"). Should ruling 14's named refusal cover bakes too, so the request fails before anything renders? (Answered by decision 34: section 5.4, final rulings.)
The check needed a hook in the overlay of `integrator/path_trace.cpp`, because no public interface of the pinned path
tracer gives the resolution its passes are read at; the Linux-side overlay records must be regenerated on the Linux x64
machine.

B8: Cycles' CPU device checks no image size, and the report states `INT_MAX` for it; but its kernels compute `2 * width` in
`int` for the Mirror extension (`wrap_mirror`, `kernel/util/image.h`), which is defined only up to 1073741823, the WGSL
device's own limit. Should the CPU backends refuse a larger side by name and report 1073741823, as WebGPU does? (Answered by decision 34: section 5.4, final rulings.)

C27 and batch 2 (section 5.4). With a CMake older than 4.2 the recipe only warns and builds a host library that differs
with the checkout path, and whose dSYM now lacks the line tables of the objects whose file names repeat in one Cycles static
library (without the modification times that `ZERO_AR_DATE` removes, `dsymutil` cannot tell those archive members apart).
Should the osx-arm64 recipe refuse a CMake older than 4.2 instead of warning? (Answered by decision 34: section 5.4, final rulings.)

B12: the CPU's largest tile, 65536, is where the CPU kernel's float pool coordinates stop holding every texel centre; the
pinned Cycles' own int arithmetic would allow 2^24. Larger tiles cost memory out of proportion: Cycles allocates each pool of
64 tiles whole (1.1 TB of address space for byte4 tiles of 65536, which macOS reserves lazily and Linux or Windows would
refuse, ending the process through Cycles' unhandled `std::bad_alloc`), and every tile is read and staged whole. Is that the
wanted bound, or should the CPU stop lower (Metal's 128 for parity, or 1024, whose pool stays within a few hundred MB)? And
should the capability report state the largest tile size per backend, as it states `maxImageDimension`? (Answered by decision 34: section 5.4, final rulings.)

B19: the bounding radius is `CBC_NOT_READY` after any edit of the cloud until the next render (the value would describe the
cloud as it was), and a refused `cbc_host_attribute_remove` already counts as an edit, because the host prepares the edit before
it checks the arguments. Is that the wanted reading, and should a refused call leave the cloud untouched? (Answered by decision 34: section 5.4, final rulings.)

28(b) needed no change: a tiled image renders bit for bit like the plain image in tiled and bordered renders on the CPU and
Metal, and the camera-width reconstruction is reachable only with an OSL camera. B20 left spherical harmonics on curves
refused, because the pinned kernels never read them there.

C24 (section 5.4). Subdivision surfaces now bake on the CPU and Metal backends, and no WebGPU refusal was added: by
reading, the WGSL device receives the diced triangles of a mesh kept in object space, which its bake already places
through the object transform. The texture limit and UDIM tiles, also host-side, were refused on the WebGPU backend until a
WebGPU run validates them. Should subdivision bakes follow that precedent (refused by name until a Linux WebGPU run of the
GPU test and the `bake/subdivision-*` scenes), or stay accepted as now? The pixel-space refusal covers the bake target
only: another subdivision surface in the scene diced in pixel space is diced, in a bake's device update, in the dicing
camera of the scene's last render (Cycles' default before any render), which a bake does not set; that predates C24 and
applies to every bake. Should such a scene be refused at bake as well? The two fixes the tests met (an edit of a
subdivision attribute re-dices the cage; a new cage after a render starts from no diced vertices, where it used to end the
process) change render behaviour too: are they wanted in this form? (Answered by decision 34: section 5.4, final rulings.)

Known limitation (decision 34, deferred; section 5.4, final rulings, item 3). Two inputs of a displaced mesh's
displacement are not tracked by the host: a displacement shader that reads a scene value the host does not watch (the Scene
Time node: `scene_attributes` `time` and `frame`) is not displaced again when only that value changes, so the mesh keeps the
displacement of the earlier time until the mesh, its shaders or its object are edited; and on the Metal backend with
MetalRT, when the scene grows beyond MetalRT's default limits (16,777,216 instances or 268,435,456 primitives), Cycles
switches to its extended limits and tags every geometry modified (`MetalDevice::set_bvh_limits`, from
`Scene::update_kernel_features`) inside the render's scene update, without the host's restore, so a displaced mesh could be
displaced twice. The second is known only from reading the source; no film shows it. Both are documented in
`native/host/ABI.md` ("Edits after a render"), `AGENT-README.txt` (EDITS AFTER A RENDER) and `native/DEVICE.md` item 191.

## Contents

| Path | What |
| --- | --- |
| `README.md` | this report |
| `SHA256SUMS.txt` | SHA-256 of every other file (`shasum -a 256 -c SHA256SUMS.txt`) |
| `environment/` | identity (`identity.txt`), walls (`walls.txt`, `*.start.txt`, `*.end.txt`), the run script (`run_survey.sh`), every run's console output, the main build's hashes before and after, the phase-2 apply log, scene checks, `git status` after the apply, the comparison table of the 34 new scenes, and the scan for rulings 14 and 15 |
| `survey-build/` | `survey-build.json` (copy commands, control result, build and probe hashes), the patches, the build and patch console output, the copies' `host/status.json`, the incremental build logs, and the section-level control (`control-sections.txt`, `macho_sections.py`) |
| `scenes/` | `make_survey_scenes.py` and the 25 scratch documents |
| `runs/` | the render runs as written by `render_oracles.py` (`run.json`, `oracle/`, `comparisons/`, `warmup/`) |
| `row10-metal-comparison/` | the section 9 repeat, the same layout |
| `probes/` | the Metal Velvet probe source, `velvet_chain.c`, `fz_arm64.c`, their outputs and `environment.txt` |
| `compare/` | the cross-run comparison reports, `compare.output.txt`, `summary.json`, the row 2 decodes, and the scripts that made them |

Renames: `run_phase1.sh` -> `environment/run_survey.sh`; `chain.txt` -> `environment/walls.txt`; the console records
`*.start`/`*.end` -> `*.start.txt`/`*.end.txt`; the copies' `host/build.log` -> `survey-build/survey-host-build.output.txt`
and `diag-host-build.output.txt` (the repository ignores `*.log`). The host name in copied text is replaced by `<host>`.
In the console records of the three D11 runs (`runs/D11-diag-steps*/**/*.output.txt`) the trailing spaces of the empty
Cycles log lines (`00:00.01 | `) are removed, so that `git diff --check` stays clean; nothing else is changed. No
executable or library is in this folder.
