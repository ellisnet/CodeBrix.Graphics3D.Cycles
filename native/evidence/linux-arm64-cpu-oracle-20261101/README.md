# Linux ARM64 CPU oracle corpus at commit e782069a3, Debian arm64 container on an Apple Silicon Mac (2026-11-01)

Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

**This folder is the ARM64 gold standard for commit
`e782069a3f657b93afe606a2aab03dcc2e6b9073`** (docs/TESTING-LINUX-ARM64-ORACLE.md section 1):
Android ARM64 WGSL films of that commit in the product default arithmetic (`wgslArithmetic`
`arm64-scalar`) are judged against these films (native/DEVICE.md, "Validation reference
policy", Option A). It was made by the re-rendering checklist of section 9: backend
`cpu-arm64-linux` of `native/oracle/render_oracles.py`, the unmodified pinned Cycles CPU
kernels at the fixed ARM64 oracle baseline (ARMv8-A base level, `-ffp-contract=off`,
`-fno-reciprocal-math`), GCC 14 and glibc 2.41, in a `linux/arm64` Debian 13 container running
natively (no emulation) under Docker Desktop on a Mac mini M2 Pro.

| Checklist item (section 9) | Result |
|---|---|
| 1. Commit, clean tree | `e782069a3f657b93afe606a2aab03dcc2e6b9073`; `git status --porcelain` empty before the build and at the start of the render (`environment/status-before-build.txt`, `environment/render-status-start.txt`); every `meta.json` records `repoWorkingTreeClean: true` |
| 2. Whole committed corpus plus the bench bundle | **233 scenes rendered** = 231 committed scene documents (`find native/oracle/scenes -name "*.json" ! -name manifest.json ! -path "*/assets/*" \| wc -l`) + 2 bench bundle documents; 98 of them are new since `linux-arm64-cpu-oracle-20261031`. **Refused: none. Failed: none.** |
| 3. Repeat identity | `repeatBitwiseIdentical` **true for all 233** scenes. **Non-identical: none.** |
| 4. Compared with | `native/evidence/linux-x64-reference-20261030/` (commit `51fc577cc` plus its recorded working tree; the newest Linux x64 reference, of an earlier commit): 118 scenes compared in `vs-x64/`, 115 without an x64 film |
| 5. Layout | section 7: `oracle/`, `run.json`, `vs-x64/`, this README; earlier `linux-arm64-cpu-oracle-*` folders untouched |

Against the x64 CPU oracle (information only): 104 scenes within the practical criterion
(none bit identical), **14 beyond it** (findings, the same 14 as in
`linux-arm64-cpu-oracle-20261031`, with the same numbers), 115 without an x64 film.

**Unchanged since the previous ARM64 folder:** for the 135 scenes that
`linux-arm64-cpu-oracle-20261031` (commit `af0a254dd`) also rendered, every film here is
**bitwise identical** to the film there (`environment/vs-20261031.txt`); no scene document of
those 135 changed. The other 98 scenes are new.

## Environment (`environment/`)

| Item | Value |
|---|---|
| Mac | Mac mini (Mac14,12), Apple M2 Pro, 10 cores (6 performance, 4 efficiency), 16 GB; macOS 27.2 |
| Docker | Docker Desktop, engine 29.9.2, LinuxKit kernel 7.0.14 (`linux/arm64`), VM with 10 CPUs and 7.7 GiB; `uname -m` `aarch64` |
| Image | `debian:trixie`, `debian@sha256:2dc419c6d7fe3dad0a2f9fd469c34cc90b0f9ba642d8fde837fe3d7d34a0e7f1` (created 2026-10-27; `docker pull` on 2026-11-01 reported it up to date) |
| Packages | section 2: `gcc-14 g++-14 gcc g++ clang cmake make time python3 git ca-certificates file` |
| Toolchain | GCC 14.2.0-19 (`gcc-14`, `g++-14`; `c++` is GCC, as every `meta.json` records), glibc 2.41-12+deb13u4, CMake 3.31.6, Python 3.13.5, git 2.47.3 (`toolchain.txt`); the same Debian release, GCC and glibc builds as the Linux x64 machine |
| ISPC | 1.30.0 for Linux aarch64, the official release archive `ispc-v1.30.0-linux.aarch64.tar.gz` from github.com/ispc/ispc/releases (SHA-256 `509399c399ec162d746889458a10cc13797a1aed1c0164b2bd3faddf7d023f13`, as published), unpacked in the ignored `native/work/ispc/` and named with `CODEBRIX_ISPC`; `bin/ispc` SHA-256 `1079bf9e0734777e108fd55e195365df0ebb200c65f532078bba751ad9c4a3de`, `Intel(r) ISPC, 1.30.0 (build commit 3fc6d50cf24dc8b4 @ 20260204, LLVM 21.1.8)` (`ispc.txt`). Build-time only: it compiles Open Image Denoise's CPU kernels, which the oracle does not use |
| WebGPU runtime | CodeBrix.WebGpu.Native.MitLicenseForever 1.0.300.210 from the NuGet cache (mounted read-only), `runtimes/linux-arm64/native/libcodebrix_wgpu.so` SHA-256 `3fb3a382d83dd9ff535829f6abeabdd26837a05decdacbb80f9b442e005c9eec`; the host library links it (`paths.txt`), the CPU oracle does not render with it |
| Device model | `meta.json` `deviceModel` is `unavailable: ...` (no device tree in a container); the machine is the Mac above |

## Build (inside the container)

```sh
git config --global --add safe.directory /work/CodeBrix.Graphics3D.Cycles
export CODEBRIX_ISPC=/work/CodeBrix.Graphics3D.Cycles/native/work/ispc/extracted/ispc-v1.30.0-linux.aarch64/bin/ispc
python3 tools/dependencies/build_linux_arm64.py --jobs 8                  # 269.8 s wall
python3 native/tools/build_cycles_linux_arm64.py --jobs 8 --device-set   # 1,106.0 s wall
```

From an empty `native/work/linux/linux-arm64/` (the previous build tree was moved aside), both
passed (`build-exit.txt`, `wall-*.txt`, logs `dependencies.output.txt` and
`cycles-build.output.txt`; build records in `build/`). The first attempt stopped in the
dependency build before compiling anything, because no ISPC was installed:
`RuntimeError: ISPC 1.30.0 is required: set CODEBRIX_ISPC to its executable or put ispc on PATH`
(`attempt1-dependencies.output.txt`). The CPU kernel compile line ends `-ffp-contract=off
-fno-reciprocal-math` and has no `-march`/`-mcpu` (`cpuKernelCompile` in every `meta.json`).

| Binary (not copied here) | SHA-256 |
|---|---|
| `native/work/linux/linux-arm64/host/build/bin/codebrix_cycles_oracle` (runner) | `ea7e800832d90936f1c766c663d77790675375087ad3293a626aafd2c528da8c` |
| `native/work/linux/linux-arm64/host/build/lib/libcodebrix_cycles_host.so` | `2deea0c1c74833e547d06966a68333683a6586b055b57e91bab3c5e84c5435c1` |

## Render (inside the container)

The scene list of section 5 (`environment/render-scenes.txt`, its first line the count check
`expected (count rule) 233; listed 233`): every `*.json` under `native/oracle/scenes` except
`manifest.json` files and `assets/`, plus the bench bundle's two documents from
`native/work/oracle-assets/bench_01/` (generated on this Mac on 2026-10-23; on 2026-11-01 all 13
generated files matched `native/oracle/scenes/assets/manifest.json` and both bundle documents
were byte-identical to `native/oracle/scenes/assets/bench_01.json` and `bench_01-hdri.json`).

```sh
python3 native/oracle/render_oracles.py --output native/work/oracles/linux-arm64-20261101 \
  --runner "$RUNNER" --host-library "$HOSTLIB" --backend cpu-arm64-linux "${scenes[@]}"
```

Wall 19.2 s for 233 scenes (2026-11-02 01:56:47 to 01:57:07 UTC; default 4 threads), exit 0
(`render.output.txt`); `run.json` has 233 scenes and no `failures`. With one backend,
`render_oracles.py` compares nothing inside the run: `comparisonsPassed` is `null` for every
scene and no `comparisons/` directory is written.

## Comparison with the Linux x64 CPU oracle (information only)

Reference: `native/evidence/linux-x64-reference-20261030/`, `oracles/corpus/oracle/<scene>/cpu-x64-linux`
and, for the two bench scenes, `oracles/bench/oracle/<scene>/cpu-x64-linux`. Reference first:

```sh
python3 native/oracle/compare.py <x64 cpu-x64-linux dir> <this run's cpu-arm64-linux dir> --json vs-x64/<scene with / as __>.json
```

`vs-x64/` holds the 118 reports (`compare.output.txt` the printed tables). Every report has
`problems: []`: scene SHA-256, film shape and sample counts match. `compare.py` treats a
CPU-versus-CPU pair across instruction sets as *reported*, not gating, so every report's
`verdict` is `perfect`; the numbers below are the pair's own.

| Result | Scenes |
|---|---|
| Bit identical | 0 |
| Within the practical criterion `abs(a-b) <= 5e-5 + 2e-4*abs(b)` | 104 |
| **Beyond the practical criterion** | **14** |
| No x64 film (scene newer than the x64 reference) | 115: bsdf 7, cameras 13, combinations 1, displacement 4, lamps 10, mnee 3, passes 47, sampling 20, sky 7, surface-nodes 3 |

Differences between the two CPU oracles are **findings to record, never failures** and never
tuned away (native/DEVICE.md open item 25). The 14 scenes beyond the practical criterion
(practical failures counted per component; worst pixel x, y, channel; reference = x64,
candidate = ARM64):

| Scene | Film | Practical failures, strict differences, maxima, worst pixel |
|---|---|---|
| assets/bench_01 | 64x48, 16 spp | 32 components, strict diffs 1603, max abs 0.00424, max rel 0.00957, worst (42,22,R) ref 0.443487018 cand 0.447729558 |
| assets/bench_01-hdri | 64x48, 16 spp | 67 components, strict diffs 2078, max abs 0.0118, max rel 0.0208, worst (42,22,R) ref 0.566275716 cand 0.578031242 |
| combinations/enclosure-emissive-panel | 64x48, 8 spp | 60 components, strict diffs 620, max abs 0.00377, max rel 0.0388, worst (52,12,R) ref 0.0971838757 cand 0.100957222 |
| combinations/enclosure-emissive-panel-mix | 64x48, 8 spp | 58 components, strict diffs 462, max abs 0.00307, max rel 0.0287, worst (52,12,R) ref 0.107118838 cand 0.110190198 |
| combinations/enclosure-emissive-panel-oren-nayar | 64x48, 8 spp | 58 components, strict diffs 516, max abs 0.00248, max rel 0.027, worst (52,12,R) ref 0.0918805599 cand 0.094358325 |
| combinations/tree-glass-render | 64x48, 8 spp | 26 components, strict diffs 1132, max abs 0.00457, max rel 0.0297, worst (41,20,R) ref 0.153321236 cand 0.148768812 |
| combinations/tree-glass-sphere | 64x48, 16 spp | 9 components, strict diffs 403, max abs 0.00561, max rel 0.0118, worst (59,9,R) ref 0.476515293 cand 0.470904887 |
| combinations/tree-principled | 64x48, 16 spp | 8 components, strict diffs 212, max abs 0.00214, max rel 0.0146, worst (59,9,R) ref 0.146555722 cand 0.144416198 |
| combinations/tree-shadow-terminator | 64x48, 16 spp | 6 components, strict diffs 343, max abs 0.00561, max rel 0.013, worst (59,9,R) ref 0.433232039 cand 0.427621573 |
| combinations/tree-textured-emitter | 64x48, 16 spp | 8 components, strict diffs 418, max abs 0.00551, max rel 0.0101, worst (59,9,B) ref 0.545186043 cand 0.539673746 |
| light-motion/linking-hdri | 64x48, 16 spp | 3 components, strict diffs 344, max abs 0.0164, max rel 0.0598, worst (32,19,R) ref 0.273482919 cand 0.257122606 |
| light-motion/motion-hdri-emitter | 64x48, 16 spp | 33 components, strict diffs 176, max abs 0.00373, max rel 0.0203, worst (24,9,R) ref 0.0946976766 cand 0.0966200903 |
| light-tree/lamps-and-mesh-lights | 64x48, 16 spp | 9 components, strict diffs 426, max abs 0.00561, max rel 0.0118, worst (59,9,R) ref 0.476515293 cand 0.470904887 |
| linux-gates/world-volume | 64x48, 8 spp | 87 components, strict diffs 495, max abs 1.36, max rel 84.9, worst (38,15,B) ref 0 cand 1.3629173 |

The largest is `linux-gates/world-volume` (87 components; at pixel 38, 15 the x64 film has blue 0 and the ARM64
film 1.3629173). `native/evidence/linux-arm64-cpu-oracle-20261031/README.md` ("Cross-check") shows that the native
macOS ARM64 CPU build reproduces this ARM64 film within the practical criterion, so the difference follows the
instruction set of the CPU kernels, not the container, compiler or C library. None of these scenes was rerendered or
adjusted.

## Contents

- `oracle/<scene>/cpu-arm64-linux/`: `film-0.f32`, `film-1.f32` (scene-linear RGBA float32, little-endian, lower-left
  origin) and `meta.json`; `oracle/<scene>/cpu-arm64-linux.output.txt`, the runner's output.
- `run.json`: the render record (`scenes`, `backends`, `common`, `comparisonsPassed`).
- `vs-x64/`: the section 6 comparison reports, one per scene with an x64 film.
- `environment/`: the container toolchain (`toolchain.txt`), ISPC (`ispc.txt`), runner and host paths and SHA-256
  (`paths.txt`), the commit and working-tree status before the build and at the start of the render, the scene list
  with the count check, start and end times, walls and exit codes, the build, render and compare logs, the failed
  first attempt's logs (`attempt1-*`), the comparison with the previous ARM64 folder (`vs-20261031.txt`), the build
  records (`build/`: the host build's `status.json` with every artifact's SHA-256 and the CPU kernel flags, and the
  dependency build's `status.json`), and the scripts that ran the build, render and comparison (`run-all.sh`) and
  wrote the table below (`summarize.py`).

Built executables and libraries are not evidence and are not copied here; their SHA-256 are above.

## Per-scene record

Repeat: the two renders of each scene (`repeatBitwiseIdentical`). Render seconds as recorded in `meta.json` (the
first render includes scene setup).

| Scene | Repeat | Film | Render s (1st / 2nd) | vs cpu-x64-linux |
|---|---|---|---|---|
| ao/ao-node | bitwise | 32x24, 8 spp | 0.00444 / 0.00152 | within practical (strict diffs 3, max abs 1.64e-07, max rel 8.71e-07) |
| ao/fast-gi-additive | bitwise | 32x24, 8 spp | 0.00264 / 0.000867 | within practical (strict diffs 0, max abs 1.19e-07, max rel 3.46e-07) |
| assets/bench_01 | bitwise | 64x48, 16 spp | 0.0282 / 0.00519 | **beyond practical**: 32 components, strict diffs 1603, max abs 0.00424, max rel 0.00957, worst (42,22,R) ref 0.443487018 cand 0.447729558 |
| assets/bench_01-hdri | bitwise | 64x48, 16 spp | 0.0251 / 0.00655 | **beyond practical**: 67 components, strict diffs 2078, max abs 0.0118, max rel 0.0208, worst (42,22,R) ref 0.566275716 cand 0.578031242 |
| bsdf/ashikhmin-shirley | bitwise | 32x24, 8 spp | 0.00351 / 0.00118 | no x64 film |
| bsdf/beckmann | bitwise | 32x24, 8 spp | 0.00387 / 0.00141 | no x64 film |
| bsdf/glossy-filter | bitwise | 32x24, 8 spp | 0.00394 / 0.00143 | no x64 film |
| bsdf/metallic | bitwise | 32x24, 8 spp | 0.00371 / 0.00124 | no x64 film |
| bsdf/metallic-film | bitwise | 32x24, 8 spp | 0.00399 / 0.00134 | no x64 film |
| bsdf/toon | bitwise | 32x24, 8 spp | 0.00352 / 0.00273 | no x64 film |
| bsdf/velvet | bitwise | 32x24, 8 spp | 0.0036 / 0.00113 | no x64 film |
| cameras/central-cylindrical | bitwise | 32x24, 8 spp | 0.00302 / 0.000758 | no x64 film |
| cameras/equiangular-cubemap-face | bitwise | 32x24, 8 spp | 0.0038 / 0.00067 | no x64 film |
| cameras/equirectangular | bitwise | 32x24, 8 spp | 0.00376 / 0.00128 | no x64 film |
| cameras/equirectangular-range-dof | bitwise | 32x24, 8 spp | 0.00296 / 0.000597 | no x64 film |
| cameras/fisheye-dof-sobol-burley | bitwise | 32x24, 8 spp | 0.00291 / 0.000824 | no x64 film |
| cameras/fisheye-equidistant | bitwise | 32x24, 8 spp | 0.00291 / 0.000785 | no x64 film |
| cameras/fisheye-equisolid | bitwise | 32x24, 8 spp | 0.00317 / 0.000749 | no x64 film |
| cameras/fisheye-lens-polynomial | bitwise | 32x24, 8 spp | 0.00302 / 0.000694 | no x64 film |
| cameras/mirrorball | bitwise | 32x24, 8 spp | 0.00307 / 0.000766 | no x64 film |
| cameras/orthographic | bitwise | 32x24, 8 spp | 0.00426 / 0.0018 | no x64 film |
| cameras/orthographic-dof | bitwise | 32x24, 8 spp | 0.00447 / 0.00213 | no x64 film |
| cameras/perspective-bokeh | bitwise | 32x24, 8 spp | 0.00385 / 0.00138 | no x64 film |
| cameras/perspective-dof | bitwise | 32x24, 8 spp | 0.0037 / 0.00141 | no x64 film |
| combinations/camera-motion-ao-node | bitwise | 32x24, 8 spp | 0.00361 / 0.00144 | within practical (strict diffs 15, max abs 6.85e-07, max rel 1.91e-06) |
| combinations/camera-motion-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00255 / 0.00084 | within practical (strict diffs 0, max abs 1.19e-07, max rel 3.18e-07) |
| combinations/enclosure-emissive-panel | bitwise | 64x48, 8 spp | 0.00808 / 0.00682 | **beyond practical**: 60 components, strict diffs 620, max abs 0.00377, max rel 0.0388, worst (52,12,R) ref 0.0971838757 cand 0.100957222 |
| combinations/enclosure-emissive-panel-mix | bitwise | 64x48, 8 spp | 0.00943 / 0.00757 | **beyond practical**: 58 components, strict diffs 462, max abs 0.00307, max rel 0.0287, worst (52,12,R) ref 0.107118838 cand 0.110190198 |
| combinations/enclosure-emissive-panel-oren-nayar | bitwise | 64x48, 8 spp | 0.00863 / 0.00722 | **beyond practical**: 58 components, strict diffs 516, max abs 0.00248, max rel 0.027, worst (52,12,R) ref 0.0918805599 cand 0.094358325 |
| combinations/enclosure-emissive-panel-translucent | bitwise | 64x48, 8 spp | 0.0045 / 0.00319 | within practical (strict diffs 0, max abs 5.96e-08, max rel 2.87e-07) |
| combinations/glass-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00258 / 0.000917 | within practical (strict diffs 4, max abs 2.68e-07, max rel 1.06e-06) |
| combinations/glass-receiver-transparent-lighting-4 | bitwise | 32x24, 8 spp | 0.00311 / 0.00148 | within practical (strict diffs 9, max abs 1.04e-05, max rel 2.81e-05) |
| combinations/link-ao-node | bitwise | 32x24, 8 spp | 0.00304 / 0.0013 | within practical (strict diffs 14, max abs 2.15e-06, max rel 5.16e-06) |
| combinations/link-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00242 / 0.000759 | within practical (strict diffs 2, max abs 1.79e-07, max rel 3.84e-07) |
| combinations/link-transparent-lighting-4 | bitwise | 32x24, 8 spp | 0.00299 / 0.00117 | within practical (strict diffs 7, max abs 3.73e-07, max rel 7.8e-06) |
| combinations/link-transparent-lighting-5 | bitwise | 32x24, 8 spp | 0.00313 / 0.00135 | within practical (strict diffs 0, max abs 7.08e-08, max rel 1.97e-05) |
| combinations/moving-transparent-fast-gi | bitwise | 32x24, 8 spp | 0.00256 / 0.000973 | no x64 film |
| combinations/principled-ao-node | bitwise | 32x24, 8 spp | 0.00334 / 0.00159 | within practical (strict diffs 156, max abs 3.61e-06, max rel 2.04e-05) |
| combinations/principled-receiver-transparent-lighting-5 | bitwise | 32x24, 8 spp | 0.00342 / 0.00178 | within practical (strict diffs 23, max abs 3.71e-06, max rel 5.16e-05) |
| combinations/replace-transparent-fast-gi | bitwise | 32x24, 8 spp | 0.00267 / 0.000938 | within practical (strict diffs 0, max abs 1.04e-07, max rel 8.35e-07) |
| combinations/terminator-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00262 / 0.000997 | within practical (strict diffs 2, max abs 2.68e-07, max rel 5.48e-07) |
| combinations/tree-ao-node | bitwise | 32x24, 8 spp | 0.00343 / 0.00158 | within practical (strict diffs 44, max abs 1.34e-06, max rel 3.89e-06) |
| combinations/tree-camera-motion | bitwise | 64x48, 16 spp | 0.00894 / 0.0063 | within practical (strict diffs 492, max abs 3.96e-05, max rel 0.000195) |
| combinations/tree-catcher-approximate | bitwise | 64x48, 16 spp | 0.00976 / 0.00689 | within practical (strict diffs 39, max abs 1.24e-05, max rel 5.29e-06) |
| combinations/tree-catcher-point-light | bitwise | 64x48, 16 spp | 0.00991 / 0.00754 | within practical (strict diffs 118, max abs 2.72e-05, max rel 1.02e-05) |
| combinations/tree-deforming-motion-5 | bitwise | 32x24, 8 spp | 0.00287 / 0.00115 | within practical (strict diffs 249, max abs 1.45e-06, max rel 1.38e-05) |
| combinations/tree-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00284 / 0.00104 | within practical (strict diffs 35, max abs 4.77e-07, max rel 9.67e-07) |
| combinations/tree-glass-receiver-transparent-lighting-6 | bitwise | 32x24, 8 spp | 0.00294 / 0.00128 | within practical (strict diffs 110, max abs 7.53e-07, max rel 1.2e-05) |
| combinations/tree-glass-render | bitwise | 64x48, 8 spp | 0.0181 / 0.0158 | **beyond practical**: 26 components, strict diffs 1132, max abs 0.00457, max rel 0.0297, worst (41,20,R) ref 0.153321236 cand 0.148768812 |
| combinations/tree-glass-sphere | bitwise | 64x48, 16 spp | 0.0102 / 0.00836 | **beyond practical**: 9 components, strict diffs 403, max abs 0.00561, max rel 0.0118, worst (59,9,R) ref 0.476515293 cand 0.470904887 |
| combinations/tree-link-transparent-lighting-6 | bitwise | 32x24, 8 spp | 0.00298 / 0.00102 | within practical (strict diffs 74, max abs 7.53e-07, max rel 2.15e-05) |
| combinations/tree-link-transparent-lighting-7 | bitwise | 32x24, 8 spp | 0.00317 / 0.00117 | within practical (strict diffs 103, max abs 7.53e-07, max rel 1.63e-05) |
| combinations/tree-object-motion-4 | bitwise | 32x24, 8 spp | 0.00325 / 0.0012 | within practical (strict diffs 254, max abs 1.82e-06, max rel 7.46) |
| combinations/tree-principled | bitwise | 64x48, 16 spp | 0.011 / 0.00868 | **beyond practical**: 8 components, strict diffs 212, max abs 0.00214, max rel 0.0146, worst (59,9,R) ref 0.146555722 cand 0.144416198 |
| combinations/tree-principled-receiver-transparent-lighting-7 | bitwise | 32x24, 8 spp | 0.00346 / 0.00147 | within practical (strict diffs 176, max abs 4.69e-06, max rel 6.5e-05) |
| combinations/tree-replace-transparent-fast-gi | bitwise | 32x24, 8 spp | 0.00263 / 0.000892 | within practical (strict diffs 0, max abs 1.04e-07, max rel 8.35e-07) |
| combinations/tree-shadow-terminator | bitwise | 64x48, 16 spp | 0.00929 / 0.00773 | **beyond practical**: 6 components, strict diffs 343, max abs 0.00561, max rel 0.013, worst (59,9,R) ref 0.433232039 cand 0.427621573 |
| combinations/tree-textured-emitter | bitwise | 64x48, 16 spp | 0.0083 / 0.00636 | **beyond practical**: 8 components, strict diffs 418, max abs 0.00551, max rel 0.0101, worst (59,9,B) ref 0.545186043 cand 0.539673746 |
| combinations/tree-textured-plane-normal-map | bitwise | 64x48, 8 spp | 0.0124 / 0.0104 | within practical (strict diffs 885, max abs 7.63e-06, max rel 0.000222) |
| combinations/tree-transparent-lighting-0 | bitwise | 32x24, 8 spp | 0.00256 / 0.00108 | within practical (strict diffs 0, max abs 3.35e-08, max rel 2.7e-06) |
| combinations/tree-transparent-lighting-1 | bitwise | 32x24, 8 spp | 0.00273 / 0.000988 | within practical (strict diffs 2, max abs 2.38e-07, max rel 4.3e-06) |
| combinations/tree-transparent-lighting-2 | bitwise | 32x24, 8 spp | 0.00294 / 0.00129 | within practical (strict diffs 31, max abs 2.68e-06, max rel 2.18e-05) |
| combinations/tree-transparent-lighting-3 | bitwise | 32x24, 8 spp | 0.00256 / 0.00109 | within practical (strict diffs 4, max abs 2.87e-07, max rel 7.46e-05) |
| combinations/tree-transparent-lighting-4 | bitwise | 32x24, 8 spp | 0.00314 / 0.00137 | within practical (strict diffs 133, max abs 8.2e-07, max rel 1.47e-05) |
| combinations/tree-transparent-lighting-5 | bitwise | 32x24, 8 spp | 0.00348 / 0.00182 | within practical (strict diffs 91, max abs 5.14e-07, max rel 1.28e-05) |
| combinations/tree-transparent-lighting-6 | bitwise | 32x24, 8 spp | 0.0028 / 0.00112 | within practical (strict diffs 119, max abs 1.01e-06, max rel 1.41e-05) |
| combinations/tree-transparent-lighting-7 | bitwise | 32x24, 8 spp | 0.00289 / 0.00107 | within practical (strict diffs 192, max abs 1.01e-06, max rel 1.4e-05) |
| combinations/tree-triangle-lighting-1 | bitwise | 32x24, 8 spp | 0.00295 / 0.00116 | within practical (strict diffs 11, max abs 1.48e-06, max rel 1.84e-05) |
| combinations/tree-triangle-lighting-2 | bitwise | 32x24, 8 spp | 0.0029 / 0.00122 | within practical (strict diffs 20, max abs 1.43e-06, max rel 0.000346) |
| combinations/tree-triangle-lighting-3 | bitwise | 32x24, 8 spp | 0.00272 / 0.00104 | within practical (strict diffs 16, max abs 1.64e-06, max rel 5.85e-06) |
| combinations/tree-triangle-lighting-5 | bitwise | 32x24, 8 spp | 0.00282 / 0.00108 | within practical (strict diffs 11, max abs 9.24e-07, max rel 9.41e-06) |
| displacement/both | bitwise | 32x24, 8 spp | 0.003 / 0.000439 | no x64 film |
| displacement/bump | bitwise | 32x24, 8 spp | 0.00265 / 0.00037 | no x64 film |
| displacement/true-height | bitwise | 32x24, 8 spp | 0.00284 / 0.000374 | no x64 film |
| displacement/true-vector | bitwise | 32x24, 8 spp | 0.00301 / 0.000503 | no x64 film |
| lamps/area-zero-size | bitwise | 32x24, 16 spp | 0.00501 / 0.00277 | no x64 film |
| lamps/lamp-area-parametric | bitwise | 32x24, 16 spp | 0.00541 / 0.00329 | no x64 film |
| lamps/lamp-point-falloff | bitwise | 32x24, 16 spp | 0.0053 / 0.00301 | no x64 film |
| lamps/lamp-point-ies | bitwise | 32x24, 16 spp | 0.0046 / 0.00225 | no x64 film |
| lamps/lamp-point-normal | bitwise | 32x24, 16 spp | 0.00502 / 0.00286 | no x64 film |
| lamps/lamp-spot-parametric | bitwise | 32x24, 16 spp | 0.00493 / 0.00273 | no x64 film |
| lamps/lamp-sun-parametric | bitwise | 32x24, 16 spp | 0.00502 / 0.00282 | no x64 film |
| lamps/light-falloff-constant | bitwise | 32x24, 16 spp | 0.0053 / 0.00309 | no x64 film |
| lamps/light-falloff-linear | bitwise | 32x24, 16 spp | 0.00475 / 0.00251 | no x64 film |
| lamps/light-falloff-quadratic | bitwise | 32x24, 16 spp | 0.00496 / 0.00278 | no x64 film |
| light-motion/camera-deforming-motion | bitwise | 32x24, 8 spp | 0.00264 / 0.00108 | within practical (strict diffs 0, max abs 9.08e-09, max rel 5.35e-06) |
| light-motion/camera-object-motion | bitwise | 32x24, 8 spp | 0.00253 / 0.00112 | within practical (strict diffs 1, max abs 4.47e-07, max rel 3.08) |
| light-motion/clamps-filters-exclude | bitwise | 64x48, 16 spp | 0.0122 / 0.0108 | within practical (strict diffs 153, max abs 1.79e-06, max rel 1.75e-05) |
| light-motion/light-cast-shadow | bitwise | 64x48, 16 spp | 0.00923 / 0.00729 | within practical (strict diffs 659, max abs 1.43e-05, max rel 1.41e-05) |
| light-motion/light-cast-shadow-tree | bitwise | 64x48, 16 spp | 0.012 / 0.00977 | within practical (strict diffs 996, max abs 1.96e-05, max rel 1.82e-05) |
| light-motion/light-max-bounces | bitwise | 64x48, 16 spp | 0.0102 / 0.00769 | within practical (strict diffs 614, max abs 1.43e-05, max rel 1.41e-05) |
| light-motion/light-max-bounces-tree | bitwise | 64x48, 16 spp | 0.0123 / 0.0101 | within practical (strict diffs 868, max abs 1.96e-05, max rel 2.44e-05) |
| light-motion/linking-glass | bitwise | 64x48, 16 spp | 0.00672 / 0.00489 | within practical (strict diffs 377, max abs 1.23e-05, max rel 0.000147) |
| light-motion/linking-hdri | bitwise | 64x48, 16 spp | 0.00865 / 0.00648 | **beyond practical**: 3 components, strict diffs 344, max abs 0.0164, max rel 0.0598, worst (32,19,R) ref 0.273482919 cand 0.257122606 |
| light-motion/linking-motion-flat | bitwise | 64x48, 16 spp | 0.00633 / 0.00449 | within practical (strict diffs 434, max abs 5.72e-06, max rel 9.64e-06) |
| light-motion/linking-motion-tree | bitwise | 64x48, 16 spp | 0.00807 / 0.00638 | within practical (strict diffs 399, max abs 2.8e-06, max rel 2.05e-05) |
| light-motion/linking-transparent | bitwise | 64x48, 16 spp | 0.0112 / 0.0087 | within practical (strict diffs 345, max abs 1.61e-06, max rel 7.75e-06) |
| light-motion/motion-glass-principled-lamps | bitwise | 64x48, 16 spp | 0.013 / 0.0115 | within practical (strict diffs 644, max abs 9.73e-05, max rel 3.39e-05) |
| light-motion/motion-hdri-emitter | bitwise | 64x48, 16 spp | 0.0126 / 0.0102 | **beyond practical**: 33 components, strict diffs 176, max abs 0.00373, max rel 0.0203, worst (24,9,R) ref 0.0946976766 cand 0.0966200903 |
| light-tree/lamps-and-mesh-lights | bitwise | 64x48, 16 spp | 0.00771 / 0.00615 | **beyond practical**: 9 components, strict diffs 426, max abs 0.00561, max rel 0.0118, worst (59,9,R) ref 0.476515293 cand 0.470904887 |
| light-tree/light-linking | bitwise | 64x48, 16 spp | 0.0077 / 0.00581 | within practical (strict diffs 384, max abs 1.61e-06, max rel 4.34e-06) |
| linux-gates/broad-lighting-0 | bitwise | 32x24, 8 spp | 0.00232 / 0.000752 | within practical (strict diffs 0, max abs 3.35e-08, max rel 2.7e-06) |
| linux-gates/broad-lighting-1 | bitwise | 32x24, 8 spp | 0.00243 / 0.000862 | within practical (strict diffs 4, max abs 1.44e-07, max rel 8.83e-05) |
| linux-gates/broad-lighting-10 | bitwise | 32x24, 8 spp | 0.00247 / 0.00074 | within practical (strict diffs 20, max abs 5.25e-06, max rel 1.99e-06) |
| linux-gates/broad-lighting-11 | bitwise | 32x24, 8 spp | 0.00246 / 0.000905 | within practical (strict diffs 108, max abs 2.5e-06, max rel 2.62e-05) |
| linux-gates/broad-lighting-2 | bitwise | 32x24, 8 spp | 0.00251 / 0.000882 | within practical (strict diffs 4, max abs 4.06e-07, max rel 0.000662) |
| linux-gates/broad-lighting-3 | bitwise | 32x24, 8 spp | 0.00235 / 0.000828 | within practical (strict diffs 0, max abs 6.66e-08, max rel 0.0031) |
| linux-gates/broad-lighting-4 | bitwise | 32x24, 8 spp | 0.00261 / 0.000941 | within practical (strict diffs 55, max abs 1.61e-06, max rel 9.06e-06) |
| linux-gates/broad-lighting-5 | bitwise | 32x24, 8 spp | 0.00262 / 0.000849 | within practical (strict diffs 14, max abs 1.71e-06, max rel 1.73e-05) |
| linux-gates/broad-lighting-6 | bitwise | 32x24, 8 spp | 0.00245 / 0.000827 | within practical (strict diffs 14, max abs 8.2e-07, max rel 1.03e-05) |
| linux-gates/broad-lighting-7 | bitwise | 32x24, 8 spp | 0.00239 / 0.000773 | within practical (strict diffs 64, max abs 1.43e-05, max rel 4.35e-05) |
| linux-gates/broad-lighting-8 | bitwise | 32x24, 8 spp | 0.00247 / 0.000764 | within practical (strict diffs 70, max abs 1.74e-05, max rel 5.49e-05) |
| linux-gates/broad-lighting-9 | bitwise | 32x24, 8 spp | 0.00242 / 0.000811 | within practical (strict diffs 95, max abs 1.01e-05, max rel 0.00216) |
| linux-gates/closed-volumes | bitwise | 64x48, 8 spp | 0.0155 / 0.0126 | within practical (strict diffs 480, max abs 7.63e-06, max rel 0.00476) |
| linux-gates/deforming-motion-0 | bitwise | 32x24, 8 spp | 0.00239 / 0.000818 | within practical (strict diffs 0, max abs 5.22e-08, max rel 2.7e-06) |
| linux-gates/deforming-motion-1 | bitwise | 32x24, 8 spp | 0.00259 / 0.000881 | within practical (strict diffs 3, max abs 3.95e-07, max rel 1.84e-05) |
| linux-gates/deforming-motion-2 | bitwise | 32x24, 8 spp | 0.00256 / 0.000912 | within practical (strict diffs 5, max abs 1.49e-07, max rel 3.69e-06) |
| linux-gates/deforming-motion-3 | bitwise | 32x24, 8 spp | 0.00255 / 0.000865 | within practical (strict diffs 0, max abs 9.2e-09, max rel 5.5e-06) |
| linux-gates/deforming-motion-4 | bitwise | 32x24, 8 spp | 0.00261 / 0.000992 | within practical (strict diffs 28, max abs 5.66e-07, max rel 1.35e-05) |
| linux-gates/deforming-motion-5 | bitwise | 32x24, 8 spp | 0.00253 / 0.000886 | within practical (strict diffs 16, max abs 4.02e-07, max rel 6.05e-05) |
| linux-gates/deforming-motion-6 | bitwise | 32x24, 8 spp | 0.0025 / 0.000823 | within practical (strict diffs 28, max abs 5.66e-07, max rel 8.15e-06) |
| linux-gates/deforming-motion-7 | bitwise | 32x24, 8 spp | 0.00262 / 0.00101 | within practical (strict diffs 22, max abs 3.73e-07, max rel 6.88e-06) |
| linux-gates/glass-render | bitwise | 64x48, 8 spp | 0.00915 / 0.00667 | within practical (strict diffs 1, max abs 2.31e-07, max rel 1.98e-06) |
| linux-gates/object-motion-0 | bitwise | 32x24, 8 spp | 0.00256 / 0.000914 | within practical (strict diffs 1, max abs 2.91e-07, max rel 0.287) |
| linux-gates/object-motion-1 | bitwise | 32x24, 8 spp | 0.00264 / 0.000919 | within practical (strict diffs 3, max abs 3.8e-07, max rel 2.12) |
| linux-gates/object-motion-2 | bitwise | 32x24, 8 spp | 0.00255 / 0.00101 | within practical (strict diffs 5, max abs 4.54e-07, max rel 5.52) |
| linux-gates/object-motion-3 | bitwise | 32x24, 8 spp | 0.0025 / 0.000876 | within practical (strict diffs 0, max abs 9.2e-09, max rel 4.33) |
| linux-gates/object-motion-4 | bitwise | 32x24, 8 spp | 0.00261 / 0.00101 | within practical (strict diffs 24, max abs 3.73e-07, max rel 2.44) |
| linux-gates/object-motion-5 | bitwise | 32x24, 8 spp | 0.00272 / 0.000911 | within practical (strict diffs 10, max abs 3.2e-07, max rel 3.3) |
| linux-gates/object-motion-6 | bitwise | 32x24, 8 spp | 0.00284 / 0.00105 | within practical (strict diffs 29, max abs 3.87e-07, max rel 2.99) |
| linux-gates/object-motion-7 | bitwise | 32x24, 8 spp | 0.00255 / 0.000863 | within practical (strict diffs 20, max abs 4.17e-07, max rel 2.18) |
| linux-gates/textured-plane | bitwise | 64x48, 8 spp | 0.00929 / 0.00657 | within practical (strict diffs 440, max abs 7.63e-06, max rel 0.000946) |
| linux-gates/transparent-lighting-0 | bitwise | 32x24, 8 spp | 0.0024 / 0.000865 | within practical (strict diffs 0, max abs 3.35e-08, max rel 2.7e-06) |
| linux-gates/transparent-lighting-1 | bitwise | 32x24, 8 spp | 0.00278 / 0.00107 | within practical (strict diffs 2, max abs 2.38e-07, max rel 4.3e-06) |
| linux-gates/transparent-lighting-2 | bitwise | 32x24, 8 spp | 0.00291 / 0.00108 | within practical (strict diffs 31, max abs 2.68e-06, max rel 2.18e-05) |
| linux-gates/transparent-lighting-3 | bitwise | 32x24, 8 spp | 0.00265 / 0.000989 | within practical (strict diffs 4, max abs 2.87e-07, max rel 7.46e-05) |
| linux-gates/transparent-lighting-4 | bitwise | 32x24, 8 spp | 0.00304 / 0.00133 | within practical (strict diffs 9, max abs 3.73e-07, max rel 7.36e-06) |
| linux-gates/transparent-lighting-5 | bitwise | 32x24, 8 spp | 0.00315 / 0.00151 | within practical (strict diffs 0, max abs 7.45e-08, max rel 1.97e-05) |
| linux-gates/transparent-lighting-6 | bitwise | 32x24, 8 spp | 0.00266 / 0.000985 | within practical (strict diffs 4, max abs 3.73e-07, max rel 7.97e-06) |
| linux-gates/transparent-lighting-7 | bitwise | 32x24, 8 spp | 0.00261 / 0.000924 | within practical (strict diffs 12, max abs 3.73e-07, max rel 1.01e-05) |
| linux-gates/triangle-lighting-0 | bitwise | 32x24, 8 spp | 0.00211 / 0.000695 | within practical (strict diffs 2, max abs 1.94e-07, max rel 2.08e-06) |
| linux-gates/triangle-lighting-1 | bitwise | 32x24, 8 spp | 0.0025 / 0.000984 | within practical (strict diffs 12, max abs 1.35e-06, max rel 1.85e-05) |
| linux-gates/triangle-lighting-2 | bitwise | 32x24, 8 spp | 0.00251 / 0.000946 | within practical (strict diffs 30, max abs 2.82e-06, max rel 6.93e-05) |
| linux-gates/triangle-lighting-3 | bitwise | 32x24, 8 spp | 0.00246 / 0.000981 | within practical (strict diffs 17, max abs 1.1e-06, max rel 4.51e-06) |
| linux-gates/triangle-lighting-5 | bitwise | 32x24, 8 spp | 0.00262 / 0.00109 | within practical (strict diffs 20, max abs 1.5e-06, max rel 1.85e-05) |
| linux-gates/world-volume | bitwise | 64x48, 8 spp | 0.0154 / 0.0127 | **beyond practical**: 87 components, strict diffs 495, max abs 1.36, max rel 84.9, worst (38,15,B) ref 0 cand 1.3629173 |
| mnee/glass-sphere-caustic | bitwise | 32x24, 8 spp | 0.00442 / 0.00258 | within practical (strict diffs 15, max abs 5.07e-07, max rel 2.43e-06) |
| mnee/glass-sphere-deforming | bitwise | 32x24, 8 spp | 0.00399 / 0.00219 | no x64 film |
| mnee/glass-sphere-fast-gi | bitwise | 32x24, 8 spp | 0.00413 / 0.00227 | no x64 film |
| mnee/glass-sphere-object-motion | bitwise | 32x24, 8 spp | 0.00514 / 0.00344 | no x64 film |
| passes/aovs | bitwise | 48x36, 8 spp | 0.00405 / 0.00266 | no x64 film |
| passes/cryptomatte | bitwise | 48x36, 8 spp | 0.00482 / 0.00306 | no x64 film |
| passes/data-passes | bitwise | 48x36, 8 spp | 0.00464 / 0.00295 | no x64 film |
| passes/denoising-features | bitwise | 48x36, 8 spp | 0.00474 / 0.00316 | no x64 film |
| passes/denoising-features-weighted | bitwise | 48x36, 8 spp | 0.00469 / 0.00308 | no x64 film |
| passes/denoising-motion | bitwise | 48x36, 8 spp | 0.00463 / 0.00269 | no x64 film |
| passes/holdout-transparent-film | bitwise | 48x36, 8 spp | 0.00456 / 0.0027 | no x64 film |
| passes/light-groups | bitwise | 48x36, 8 spp | 0.00447 / 0.00286 | no x64 film |
| passes/light-passes | bitwise | 48x36, 8 spp | 0.0053 / 0.00323 | no x64 film |
| passes/motion | bitwise | 48x36, 8 spp | 0.00444 / 0.00263 | no x64 film |
| passes/sample-count | bitwise | 48x36, 8 spp | 0.00433 / 0.00285 | no x64 film |
| passes/with-ao-ao-node | bitwise | 32x24, 8 spp | 0.00348 / 0.00171 | no x64 film |
| passes/with-ao-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00277 / 0.00107 | no x64 film |
| passes/with-bsdf-ashikhmin-shirley | bitwise | 32x24, 8 spp | 0.00393 / 0.00168 | no x64 film |
| passes/with-bsdf-beckmann | bitwise | 32x24, 8 spp | 0.00407 / 0.00178 | no x64 film |
| passes/with-bsdf-glossy-filter | bitwise | 32x24, 8 spp | 0.0041 / 0.00199 | no x64 film |
| passes/with-bsdf-metallic | bitwise | 32x24, 8 spp | 0.00383 / 0.0016 | no x64 film |
| passes/with-bsdf-toon | bitwise | 32x24, 8 spp | 0.00388 / 0.0017 | no x64 film |
| passes/with-bsdf-velvet | bitwise | 32x24, 8 spp | 0.00391 / 0.00158 | no x64 film |
| passes/with-combinations-enclosure-emissive-panel-mix | bitwise | 64x48, 8 spp | 0.0108 / 0.00828 | no x64 film |
| passes/with-combinations-glass-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00272 / 0.00117 | no x64 film |
| passes/with-combinations-link-transparent-lighting-4 | bitwise | 32x24, 8 spp | 0.00336 / 0.00175 | no x64 film |
| passes/with-combinations-tree-principled | bitwise | 64x48, 16 spp | 0.0134 / 0.011 | no x64 film |
| passes/with-combinations-tree-textured-emitter | bitwise | 64x48, 16 spp | 0.0101 / 0.00772 | no x64 film |
| passes/with-combinations-tree-transparent-lighting-3 | bitwise | 32x24, 8 spp | 0.00326 / 0.00143 | no x64 film |
| passes/with-displacement-both | bitwise | 32x24, 8 spp | 0.00336 / 0.000672 | no x64 film |
| passes/with-displacement-true-vector | bitwise | 32x24, 8 spp | 0.00349 / 0.000637 | no x64 film |
| passes/with-light-motion-clamps-filters-exclude | bitwise | 64x48, 16 spp | 0.015 / 0.013 | no x64 film |
| passes/with-light-motion-linking-hdri | bitwise | 64x48, 16 spp | 0.0108 / 0.00849 | no x64 film |
| passes/with-light-motion-motion-hdri-emitter | bitwise | 64x48, 16 spp | 0.0152 / 0.0126 | no x64 film |
| passes/with-light-tree-lamps-and-mesh-lights | bitwise | 64x48, 16 spp | 0.0105 / 0.00792 | no x64 film |
| passes/with-light-tree-light-linking | bitwise | 64x48, 16 spp | 0.00943 / 0.00742 | no x64 film |
| passes/with-linux-gates-broad-lighting-0 | bitwise | 32x24, 8 spp | 0.00293 / 0.0011 | no x64 film |
| passes/with-linux-gates-broad-lighting-5 | bitwise | 32x24, 8 spp | 0.00285 / 0.00109 | no x64 film |
| passes/with-linux-gates-deforming-motion-0 | bitwise | 32x24, 8 spp | 0.0028 / 0.00112 | no x64 film |
| passes/with-linux-gates-glass-render | bitwise | 64x48, 8 spp | 0.0113 / 0.00893 | no x64 film |
| passes/with-linux-gates-object-motion-0 | bitwise | 32x24, 8 spp | 0.00292 / 0.0012 | no x64 film |
| passes/with-linux-gates-triangle-lighting-1 | bitwise | 32x24, 8 spp | 0.00276 / 0.00138 | no x64 film |
| passes/with-mnee-glass-sphere-caustic | bitwise | 32x24, 8 spp | 0.00505 / 0.00331 | no x64 film |
| passes/with-mnee-glass-sphere-object-motion | bitwise | 32x24, 8 spp | 0.00586 / 0.0037 | no x64 film |
| passes/with-shadow-catcher-catcher-approximate | bitwise | 64x48, 16 spp | 0.00842 / 0.00674 | no x64 film |
| passes/with-shadow-catcher-catcher-point-light | bitwise | 64x48, 16 spp | 0.00933 / 0.00685 | no x64 film |
| passes/with-starter-glass-and-area-light | bitwise | 64x48, 16 spp | 0.00699 / 0.00481 | no x64 film |
| passes/with-starter-textured-plane | bitwise | 48x32, 8 spp | 0.00358 / 0.00111 | no x64 film |
| passes/with-surface-nodes-bevel | bitwise | 32x24, 8 spp | 0.00441 / 0.0019 | no x64 film |
| passes/with-surface-nodes-radial-tiling | bitwise | 32x24, 8 spp | 0.00325 / 0.00133 | no x64 film |
| passes/with-surface-nodes-raycast | bitwise | 32x24, 8 spp | 0.00325 / 0.00128 | no x64 film |
| sampling/adaptive-auto | bitwise | 32x24, 96 spp | 0.0173 / 0.0143 | no x64 film |
| sampling/adaptive-blue-noise | bitwise | 32x24, 64 spp | 0.00582 / 0.00368 | no x64 film |
| sampling/adaptive-coarse | bitwise | 32x24, 48 spp | 0.00473 / 0.00277 | no x64 film |
| sampling/adaptive-sobol-burley | bitwise | 32x24, 64 spp | 0.00654 / 0.00463 | no x64 film |
| sampling/adaptive-threshold | bitwise | 32x24, 64 spp | 0.00586 / 0.00346 | no x64 film |
| sampling/blue-noise-first | bitwise | 32x24, 16 spp | 0.00474 / 0.00272 | no x64 film |
| sampling/blue-noise-first-ao-ao-node | bitwise | 32x24, 8 spp | 0.00278 / 0.00139 | no x64 film |
| sampling/blue-noise-first-shadow-catcher-catcher-point-light | bitwise | 64x48, 16 spp | 0.00792 / 0.00644 | no x64 film |
| sampling/blue-noise-pure | bitwise | 32x24, 16 spp | 0.00499 / 0.00309 | no x64 film |
| sampling/blue-noise-pure-ao-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00211 / 0.000917 | no x64 film |
| sampling/blue-noise-pure-light-motion-linking-hdri | bitwise | 64x48, 16 spp | 0.00893 / 0.00703 | no x64 film |
| sampling/blue-noise-pure-light-tree-lamps-and-mesh-lights | bitwise | 64x48, 16 spp | 0.00867 / 0.00705 | no x64 film |
| sampling/blue-noise-round | bitwise | 32x24, 12 spp | 0.00445 / 0.00233 | no x64 film |
| sampling/scrambling-distance | bitwise | 32x24, 16 spp | 0.00494 / 0.00253 | no x64 film |
| sampling/sobol-burley | bitwise | 32x24, 16 spp | 0.00478 / 0.00278 | no x64 film |
| sampling/sobol-burley-combinations-camera-motion-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00214 / 0.000863 | no x64 film |
| sampling/sobol-burley-combinations-tree-textured-plane-normal-map | bitwise | 64x48, 8 spp | 0.0128 / 0.0105 | no x64 film |
| sampling/sobol-burley-light-motion-clamps-filters-exclude | bitwise | 64x48, 16 spp | 0.0119 / 0.011 | no x64 film |
| sampling/sobol-burley-mnee-glass-sphere-caustic | bitwise | 32x24, 8 spp | 0.004 / 0.00258 | no x64 film |
| sampling/sobol-burley-surface-nodes-bevel | bitwise | 32x24, 8 spp | 0.00296 / 0.00126 | no x64 film |
| shadow-catcher/catcher-approximate | bitwise | 64x48, 16 spp | 0.00785 / 0.00601 | within practical (strict diffs 19, max abs 1.24e-05, max rel 5.29e-06) |
| shadow-catcher/catcher-point-light | bitwise | 64x48, 16 spp | 0.00743 / 0.00576 | within practical (strict diffs 32, max abs 2.72e-05, max rel 1.04e-05) |
| sky/hosek-wilkie | bitwise | 32x24, 16 spp | 0.00772 / 0.00354 | no x64 film |
| sky/multiple-scattering | bitwise | 32x24, 16 spp | 0.308 / 0.00422 | no x64 film |
| sky/multiple-scattering-sun | bitwise | 32x24, 16 spp | 0.302 / 0.00348 | no x64 film |
| sky/multiple-scattering-sun-guiding | bitwise | 32x24, 16 spp | 0.304 / 0.00298 | no x64 film |
| sky/preetham | bitwise | 32x24, 16 spp | 0.00728 / 0.00309 | no x64 film |
| sky/single-scattering | bitwise | 32x24, 16 spp | 0.178 / 0.00294 | no x64 film |
| sky/single-scattering-sun-guiding | bitwise | 32x24, 16 spp | 0.18 / 0.0029 | no x64 film |
| starter/diffuse-triangle | bitwise | 32x24, 8 spp | 0.00173 / 0.000312 | within practical (strict diffs 0, max abs 2.98e-08, max rel 2.22e-07) |
| starter/glass-and-area-light | bitwise | 64x48, 16 spp | 0.00588 / 0.00435 | within practical (strict diffs 63, max abs 1.88e-06, max rel 4.04e-06) |
| starter/glossy-sphere-point-light | bitwise | 64x48, 16 spp | 0.00626 / 0.00395 | within practical (strict diffs 156, max abs 6.34e-05, max rel 9.09e-06) |
| starter/textured-plane | bitwise | 48x32, 8 spp | 0.00259 / 0.000763 | within practical (strict diffs 67, max abs 2.91e-07, max rel 2.59e-06) |
| surface-nodes/bevel | bitwise | 32x24, 8 spp | 0.00314 / 0.00127 | no x64 film |
| surface-nodes/radial-tiling | bitwise | 32x24, 8 spp | 0.00247 / 0.00088 | no x64 film |
| surface-nodes/raycast | bitwise | 32x24, 8 spp | 0.00261 / 0.000942 | no x64 film |
