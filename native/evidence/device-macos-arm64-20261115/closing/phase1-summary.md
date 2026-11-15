Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

# Part E closing checks, phase 1: summary (2026-11-15)

Scratch record (git-ignored). Every command, its start time, wall and exit status: `steps.md` in this folder; every
output beside it. Phase 1 wrote only under `native/work/` (and the build outputs `bin/`, `obj/` and the
`nugets/Release/1.0.319.1248/` folder of step 7, which is git-ignored and was deleted). `git status --porcelain` was
empty at the start, before the corpus render, after it and at the end. A Linux ARM64 container render ran during the
whole of phase 1, so every wall is inflated.

## Identity (`identity.txt`)

HEAD `24b74e095f5f1f4526e095e7ac4ad6dfff62bb4c`, branch `cycles-with-metal-v5.3.0`, tree clean; kit base commit
`828f6c2f9ee2c57dd379d472a0252edaf3caa87a`. Mac mini Mac14,12, Apple M2 Pro (10 CPU cores: 6P + 4E; 16-core GPU,
Metal 4), 16 GB, macOS 27.2.1 (26B131); Xcode 27.2 (27B52), SDK 27.2, Apple clang 21.0.0 (clang-2100.4.1.1), CMake 4.4.5,
Python 3.15.2, zstd 1.5.7, .NET SDK 11.0.100. `identity.txt` contains the host name and the hardware serial and UUID as
the commands print them; scrub them before anything is published.

## Step 2: clean build

- `host/`, `oracle-build/` and `dependencies/` of `native/work/macos/osx-arm64/` deleted (the `dependencies-old-*`
  folders kept; the deleted tree's hashes are in `pre-delete-build-hashes.json`). `CODEBRIX_ISPC=<repo>/native/work/tools/ispc-v1.30.0-macOS.arm64/bin/ispc
  python3 tools/build_macos.py --jobs 8`: exit 0, 367.09 s, dependencies rebuilt; `status.json` `passed: true`, 65 exports,
  292 kernel files.
- Warnings: **0 in our own code.** 1 in pinned Cycles (`device/metal/device_impl.mm:559:13`, `fastMathEnabled`
  deprecated); 120 `warning:` lines (11 distinct) in the dependency logs; 2 `CMake Warning` blocks in the host configure
  (pinned CMP0167; unused `CMAKE_OBJCXX_FLAGS`).
- Commit record: `invocation.json` `sourceCommit` 24b74e09, `metalSourceStatus` `[]`; `status.json` `sourceAtEnd`
  24b74e09, `[]`; `sourceDateEpoch` 1794772111 (24b74e09's committer time). Clean at 24b74e09, as the stager needs.
- Watch-points hold: no `WITH_DLSS` or `WITH_WEBGPU` in the 176 compile commands, `-DWITH_EMBREE` on `bvh/embree.cpp`,
  `overlaidInClosure` = `util/types_gpu_compressed.h` only, 292 staged kernel files byte-identical to
  `third_party/cycles/src`, `tables_d65_codebrix.h` staged.
- SHA-256: host library `b804223deae0afce07ad33440b312d3ea907b1f528839b375db628e3a1916e5e`, runner
  `1f90b7df1bac4910b08ddae6a78de82246df3d87f07a75f9ff248f4899e692d2`, the 19 closure dylibs in
  `closing-build-hashes.json` (`closure`).
- Reproducibility (information, `repro-compare.md`): host library and runner identical to the batch-final build. Against
  the deleted tree, everything is identical except libjpeg (not shipped, not loaded; configure date) and the prefix
  record, as documented. Against the C27 tables F-a1 and F-bb: runner, kernel tree, dependency static libraries and 27 of
  28 dependency dylibs identical; host library, 3 reference executables and `libcycles_integrator.a` differ, because the
  code changed after C27.

## Step 3: macOS host gate

`native/work/macos-validation-20261115b`: **passed**, 38.74 s. 3,102 films compared, 0 same-host bitwise failures, 0 Metal
practical failures, 0 strict differences, Metal maximum 4.47e-8; binary review passed (65 exports); no sub-run failed.

## Step 4: managed sequence (testing document step 7)

restore 1.11 s, Release build 10.07 s (1 warning: the linux-x64 STALE payload notice, verbatim in `steps.md`), Release
tests 156.53 s: 525 total, 519 passed, 0 failed, 6 skipped. pack 1.91 s, Debug build 4.61 s (0 warnings), Debug tests
150.13 s: 525 / 519 / 0 / 6. The same 6 skips in both, all WebGPU-only (2 bake/AO "GPU backend is not WebGPU", 1
dispersion refusal "resolved to Metal", 3 integer/component node refusals "the refusal is the WGSL device's"). The pack
driver packed Core and LinuxX64 only; `nugets/Release/1.0.319.1248/` deleted.

## Step 5: corpus

Find count 791 committed documents + 2 bench documents = 793 scenes. `native/work/oracles/macos-arm64-closing-20261115`,
2,344.73 s, exit 0, tree clean before and after. 793 / 793 / 793 scenes rendered on `cpu-arm64-macos`, `metal-m2pro`,
`metal-m2pro-metalrt` (1,586 films each), **0 failures**, `comparisonsPassed` 793 / 793. `kernelCompileSeconds`:
`metal-m2pro` 2.36 s, `metal-m2pro-metalrt` 1.32 s (macOS shader cache warm). Closure-count warnings only (66, 77 and 96
> 64, as in Part A); no Metal compile diagnostic.

## Step 6: kit rule 9

**PASS.** 726 / 726 base scenes present; 1,452 / 1,452 base CPU films bitwise identical by `filmSha256` (film indices 0
and 1), 0 moved; 2,870 / 2,870 pass films identical (`RenderTime` of `passes/data-passes` reported apart, wall clock).
Film bytes match `meta.json` in both runs. The ruling-34 scene `lamps/area-degenerate-infinite-axis` was never in the
base run (it was a survey phase-2 scene), so nothing is excluded; it is removed from the corpus and absent from the
closing run. 67 new scenes (attributes 6, bake 24, bsdf 1, gsplat 11, images 6, lamps 5, points 2, render-settings 4,
surface-nodes 5, world 3). Every closing CPU film is repeat-identical (793 / 793). Information: all 1,586 closing CPU
films are identical to the batch-final corpus run. Metal repeats: 36 of 793 scenes repeat-identical on each Metal
backend, maximum 1.22e-4 on both (`lamps/area-degenerate-rect`). CPU against Metal, practical criterion, information:
365 scenes (BVH2) and 418 (MetalRT), 259 Metal against MetalRT; on the 726 base scenes 335, 388 and 242, the same scene
sets as Part A.
