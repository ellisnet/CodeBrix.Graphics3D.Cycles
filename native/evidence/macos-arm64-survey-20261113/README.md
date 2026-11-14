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

In progress (not part of this folder yet).

## 6. Part E: closing checks

In progress (not part of this folder yet).

## 7. Questions for the maintainer

The survey's control build did not reproduce the validated build byte for byte, although every Mach-O section is
identical. The debug map of every artifact records each object's modification time, and the two host objects built from
sources outside the Cycles tree are named after the checkout's absolute path, so the UUID and the code signature change
with every rebuild and with every checkout location. `native/host/MACOS_ARM64.md` says that no build path reaches the debug
map, while its own history note records the `cycles_host.cpp` object path. Should the recipe zero the object times and
give those two objects a checkout-independent name, so that a fresh build can be compared with a staged payload by hash
(the testing document's missing payload comparison), or is the section-level identity enough?

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

Radial Tiling with two sides and a roundness strictly between 0 and 1 renders on Metal but differs from the CPU in about
700 components (up to 1.76), which the WebGPU device answers with a refusal; Velvet's subnormal paths agree within the
practical criterion although Metal flushes subnormals. These are recorded here at the current pin; the previous-revision
films of the frozen folder were not compared (rule 8).

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
