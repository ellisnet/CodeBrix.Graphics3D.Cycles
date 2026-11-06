# Linux ARM64 CPU oracle corpus at commit ea3218b80, Debian arm64 container on an Apple Silicon Mac (2026-11-05)

Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

**This folder is the ARM64 gold standard for commit
`ea3218b80d3016b81d38a3df7bf9a91e26023407`** (docs/TESTING-LINUX-ARM64-ORACLE.md section 1):
Android ARM64 WGSL films of that commit in the product default arithmetic (`wgslArithmetic`
`arm64-scalar`) are judged against these films (native/DEVICE.md, "Validation reference
policy", Option A). It was made by the re-rendering checklist of section 9: backend
`cpu-arm64-linux` of `native/oracle/render_oracles.py`, the unmodified pinned Cycles CPU
kernels at the fixed ARM64 oracle baseline (ARMv8-A base level, `-ffp-contract=off`,
`-fno-reciprocal-math`), GCC 14 and glibc 2.41, in a `linux/arm64` Debian 13 container running
natively (no emulation) under Docker Desktop on a Mac mini M2 Pro.

| Checklist item (section 9) | Result |
|---|---|
| 1. Commit, clean tree | `ea3218b80d3016b81d38a3df7bf9a91e26023407`; `git status --porcelain` empty before the build and at the start of the render (`environment/status-before-build.txt`, `environment/render-status-start.txt`); every `meta.json` records `repoWorkingTreeClean: true` |
| 2. Whole committed corpus plus the bench bundle | **275 scenes rendered** = 273 committed scene documents (`find native/oracle/scenes -name "*.json" ! -name manifest.json ! -path "*/assets/*" \| wc -l`) + 2 bench bundle documents; 42 of them are new since `linux-arm64-cpu-oracle-20261101`. **Refused: none. Failed: none.** |
| 3. Repeat identity | `repeatBitwiseIdentical` **true for all 275** scenes. **Non-identical: none.** One render pass (not the film) differs between repeats by design: see "Unchanged since the previous ARM64 folder" |
| 4. Compared with | `native/evidence/linux-x64-reference-20261101/` (commit `f1ed54307` plus its recorded working tree; the newest Linux x64 reference, of an earlier commit): 233 scenes compared in `vs-x64/`, 42 without an x64 film |
| 5. Layout | section 7: `oracle/`, `run.json`, `vs-x64/`, this README; earlier `linux-arm64-cpu-oracle-*` folders untouched |

Against the x64 CPU oracle (information only): 209 scenes within the practical criterion
(none bit identical), **24 beyond it** (findings, listed below), 42 without an x64 film.

## Unchanged since the previous ARM64 folder

Against `linux-arm64-cpu-oracle-20261101` (commit `e782069a3`; `environment/vs-20261101.txt`):
of the 233 scenes both folders render, **232 have every film and pass file bitwise identical**,
and no scene document changed. The other 42 scenes are new: denoise 6, passes 2, sky 3,
subsurface 6, volumes 25.

The one exception is `passes/data-passes`: 18 of its 20 `.f32` files are identical, the two
`film-*.f32` among them; only `pass-8-0.f32` and `pass-8-1.f32` differ
(`environment/data-passes-vs-20261101.txt`). Pass 8 is `RenderTime`, a wall-clock measurement. It
is the only pass in the corpus whose two repeats differ within this run, so it differs between
any two renders; native/DEVICE.md records the same for the x64 films. It is not a rendering
difference, and a comparison of that pass against any other run is meaningless.

## Environment (`environment/`)

| Item | Value |
|---|---|
| Mac | Mac mini (Mac14,12), Apple M2 Pro, 10 cores (6 performance, 4 efficiency), 16 GB; macOS 27.2 |
| Docker | Docker Desktop, engine 29.9.2, LinuxKit kernel 7.0.14 (`linux/arm64`), VM with 10 CPUs and 7.7 GiB; `uname -m` `aarch64` |
| Image | `debian:trixie`, `debian@sha256:9c6e381421d0f48d1b8133e573f33c185ee16cbf505eda9c4a8a4791b25ea8a6` (created 2026-11-04; a newer image than the previous folder's, with the same toolchain) |
| Packages | section 2: `gcc-14 g++-14 gcc g++ clang cmake make time python3 git ca-certificates file` |
| Toolchain | GCC 14.2.0-19 (`gcc-14`, `g++-14`; `c++` is GCC, as every `meta.json` records), glibc 2.41-12+deb13u4, CMake 3.31.6, Python 3.13.5, git 2.47.3 (`toolchain.txt`) |
| ISPC | 1.30.0 for Linux aarch64, the official release archive `ispc-v1.30.0-linux.aarch64.tar.gz` from github.com/ispc/ispc/releases (SHA-256 `509399c399ec162d746889458a10cc13797a1aed1c0164b2bd3faddf7d023f13`), unpacked in the ignored `native/work/ispc/` and named with `CODEBRIX_ISPC`; `bin/ispc` SHA-256 `1079bf9e0734777e108fd55e195365df0ebb200c65f532078bba751ad9c4a3de`, `Intel(r) ISPC, 1.30.0 (build commit 3fc6d50cf24dc8b4 @ 20260204, LLVM 21.1.8)` (`ispc.txt`). Build-time only |
| WebGPU runtime | CodeBrix.WebGpu.Native.MitLicenseForever 1.0.300.210 from the NuGet cache (mounted read-only), `runtimes/linux-arm64/native/libcodebrix_wgpu.so`; the host library links it (`paths.txt`), the CPU oracle does not render with it |
| Device model | `meta.json` `deviceModel` is `unavailable: ...` (no device tree in a container); the machine is the Mac above |

## Build (inside the container)

```sh
git config --global --add safe.directory /work/CodeBrix.Graphics3D.Cycles
export CODEBRIX_ISPC=/work/CodeBrix.Graphics3D.Cycles/native/work/ispc/extracted/ispc-v1.30.0-linux.aarch64/bin/ispc
python3 tools/dependencies/build_linux_arm64.py --jobs 8                  # 312.0 s wall
python3 native/tools/build_cycles_linux_arm64.py --jobs 8 --device-set   # 1,158.4 s wall
```

From an empty `native/work/linux/linux-arm64/` (the previous build tree was moved aside, so no
stale `host/cycles-source`), both passed (`build-exit.txt`, `wall-*.txt`, logs
`dependencies.output.txt` and `cycles-build.output.txt`; build records in `build/`). The CPU
kernel compile line ends `-ffp-contract=off -fno-reciprocal-math` and has no `-march`/`-mcpu`
(`cpuKernelCompile` in every `meta.json`).

| Binary (not copied here) | SHA-256 |
|---|---|
| `native/work/linux/linux-arm64/host/build/bin/codebrix_cycles_oracle` (runner) | `181f2ffc5c666d4550d06b0be0cc01d3601cbc46342e503ae55d8203147c5c5e` |
| `native/work/linux/linux-arm64/host/build/lib/libcodebrix_cycles_host.so` | `c34a4cd7fdd7b8a1e13e2b530a74f88c3613e1085cf0f2af9e17352436d5a06d` |

## Render (inside the container)

The scene list of section 5 (`environment/render-scenes.txt`, its first line the count check
`expected (count rule) 275; listed 275`): every `*.json` under `native/oracle/scenes` except
`manifest.json` files and `assets/`, plus the bench bundle's two documents from
`native/work/oracle-assets/bench_01/` (generated on this Mac on 2026-10-23; on 2026-11-05 all 13
generated files matched `native/oracle/scenes/assets/manifest.json` and both bundle documents
were byte-identical to `native/oracle/scenes/assets/bench_01.json` and `bench_01-hdri.json`).

```sh
python3 native/oracle/render_oracles.py --output native/work/oracles/linux-arm64-20261105 \
  --runner "$RUNNER" --host-library "$HOSTLIB" --backend cpu-arm64-linux "${scenes[@]}"
```

Wall 28.2 s for 275 scenes (2026-11-06 03:01:42 to 03:02:10 UTC; default 4 threads), exit 0
(`render.output.txt`); `run.json` has 275 scenes and no `failures`. With one backend,
`render_oracles.py` compares nothing inside the run: `comparisonsPassed` is `null` for every
scene and no `comparisons/` directory is written.

## Comparison with the Linux x64 CPU oracle (information only)

Reference: `native/evidence/linux-x64-reference-20261101/`, `oracles/corpus/oracle/<scene>/cpu-x64-linux`
and, for the two bench scenes, `oracles/bench/oracle/<scene>/cpu-x64-linux`. Reference first:

```sh
python3 native/oracle/compare.py <x64 cpu-x64-linux dir> <this run's cpu-arm64-linux dir> --json vs-x64/<scene with / as __>.json
```

`vs-x64/` holds the 233 reports (`compare.output.txt` the printed tables). Every report has
`problems: []`: scene SHA-256, film shape and sample counts match. `compare.py` treats a
CPU-versus-CPU pair across instruction sets as *reported*, not gating, so every report's
`verdict` is `perfect`; the numbers below are the pair's own.

| Result | Scenes |
|---|---|
| Bit identical | 0 |
| Within the practical criterion `abs(a-b) <= 5e-5 + 2e-4*abs(b)` | 209 |
| **Beyond the practical criterion** | **24** |
| No x64 film (scene newer than the x64 reference) | 42: denoise 6, passes 2, sky 3, subsurface 6, volumes 25 |

Differences between the two CPU oracles are **findings to record, never failures** and never
tuned away (native/DEVICE.md open item 25). Fourteen of the 24 are the same 14 as in
`linux-arm64-cpu-oracle-20261101`, with the same numbers. The other 10 are not new differences:
those scenes had no film in the older x64 reference that folder used, and their ARM64 films here
are bitwise identical to that folder's. Practical failures counted per component; worst pixel x,
y, channel; reference = x64, candidate = ARM64:

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
| lamps/lamp-spot-parametric | 32x24, 16 spp | 3 components, strict diffs 33, max abs 0.00104, max rel 0.000533, worst (21,10,B) ref 1.95158732 cand 1.95054734 |
| lamps/lamp-sun-parametric | 32x24, 16 spp | 2 components, strict diffs 35, max abs 0.000485, max rel 0.000704, worst (15,10,R) ref 0.689506531 cand 0.68902117 |
| light-motion/linking-hdri | 64x48, 16 spp | 3 components, strict diffs 344, max abs 0.0164, max rel 0.0598, worst (32,19,R) ref 0.273482919 cand 0.257122606 |
| light-motion/motion-hdri-emitter | 64x48, 16 spp | 33 components, strict diffs 176, max abs 0.00373, max rel 0.0203, worst (24,9,R) ref 0.0946976766 cand 0.0966200903 |
| light-tree/lamps-and-mesh-lights | 64x48, 16 spp | 9 components, strict diffs 426, max abs 0.00561, max rel 0.0118, worst (59,9,R) ref 0.476515293 cand 0.470904887 |
| linux-gates/world-volume | 64x48, 8 spp | 87 components, strict diffs 495, max abs 1.36, max rel 84.9, worst (38,15,B) ref 0 cand 1.3629173 |
| passes/with-combinations-enclosure-emissive-panel-mix | 64x48, 8 spp | 58 components, strict diffs 462, max abs 0.00307, max rel 0.0287, worst (52,12,R) ref 0.107118838 cand 0.110190198 |
| passes/with-combinations-tree-principled | 64x48, 16 spp | 8 components, strict diffs 212, max abs 0.00214, max rel 0.0146, worst (59,9,R) ref 0.146555722 cand 0.144416198 |
| passes/with-combinations-tree-textured-emitter | 64x48, 16 spp | 8 components, strict diffs 418, max abs 0.00551, max rel 0.0101, worst (59,9,B) ref 0.545186043 cand 0.539673746 |
| passes/with-light-motion-linking-hdri | 64x48, 16 spp | 3 components, strict diffs 344, max abs 0.0164, max rel 0.0598, worst (32,19,R) ref 0.273482919 cand 0.257122606 |
| passes/with-light-motion-motion-hdri-emitter | 64x48, 16 spp | 33 components, strict diffs 176, max abs 0.00373, max rel 0.0203, worst (24,9,R) ref 0.0946976766 cand 0.0966200903 |
| passes/with-light-tree-lamps-and-mesh-lights | 64x48, 16 spp | 9 components, strict diffs 426, max abs 0.00561, max rel 0.0118, worst (59,9,R) ref 0.476515293 cand 0.470904887 |
| sampling/adaptive-sobol-burley | 32x24, 64 spp | 1 components, strict diffs 38, max abs 0.000105, max rel 0.000457, worst (16,10,R) ref 0.229488119 cand 0.22938323 |
| sampling/blue-noise-pure-light-tree-lamps-and-mesh-lights | 64x48, 16 spp | 3 components, strict diffs 399, max abs 0.00118, max rel 0.00535, worst (57,8,R) ref 0.220365345 cand 0.219186872 |

The largest is `linux-gates/world-volume` (87 components; at pixel 38, 15 the x64 film has blue 0 and the ARM64
film 1.3629173). `native/evidence/linux-arm64-cpu-oracle-20261031/README.md` ("Cross-check") shows that the native
macOS ARM64 CPU build reproduces this ARM64 film within the practical criterion, so the difference follows the
instruction set of the CPU kernels, not the container, compiler or C library. None of these scenes was rerendered or
adjusted.

## Contents

- `oracle/<scene>/cpu-arm64-linux/`: `film-0.f32`, `film-1.f32` (scene-linear RGBA float32, little-endian, lower-left
  origin), `pass-<index>-<repeat>.f32` where the scene writes render passes, and `meta.json`;
  `oracle/<scene>/cpu-arm64-linux.output.txt`, the runner's output.
- `run.json`: the render record (`scenes`, `backends`, `common`, `comparisonsPassed`).
- `vs-x64/`: the section 6 comparison reports, one per scene with an x64 film.
- `environment/`: the image digest (`image.txt`), the container toolchain (`toolchain.txt`), ISPC (`ispc.txt`), runner
  and host paths and SHA-256 (`paths.txt`), the commit and working-tree status before the build and at the start of the
  render, the scene list with the count check, start and end times, walls and exit codes, the build, render and compare
  logs, the comparison with the previous ARM64 folder (`vs-20261101.txt`, `data-passes-vs-20261101.txt`), the build
  records (`build/`: the host build's `status.json` with every artifact's SHA-256 and the CPU kernel flags, and the
  dependency build's `status.json`), and the scripts that ran the build, render and comparison (`run-all.sh`) and wrote
  the table below (`summarize.py`).

Built executables and libraries are not evidence and are not copied here; their SHA-256 are above.

## Per-scene record

Repeat: the two renders of each scene (`repeatBitwiseIdentical`). Render seconds as recorded in `meta.json` (the
first render includes scene setup).

| Scene | Repeat | Film | Render s (1st / 2nd) | vs cpu-x64-linux |
|---|---|---|---|---|
| ao/ao-node | bitwise | 32x24, 8 spp | 0.00359 / 0.00139 | within practical (strict diffs 3, max abs 1.64e-07, max rel 8.71e-07) |
| ao/fast-gi-additive | bitwise | 32x24, 8 spp | 0.00255 / 0.000804 | within practical (strict diffs 0, max abs 1.19e-07, max rel 3.46e-07) |
| assets/bench_01 | bitwise | 64x48, 16 spp | 0.0223 / 0.00543 | **beyond practical**: 32 components, strict diffs 1603, max abs 0.00424, max rel 0.00957, worst (42,22,R) ref 0.443487018 cand 0.447729558 |
| assets/bench_01-hdri | bitwise | 64x48, 16 spp | 0.0228 / 0.00676 | **beyond practical**: 67 components, strict diffs 2078, max abs 0.0118, max rel 0.0208, worst (42,22,R) ref 0.566275716 cand 0.578031242 |
| bsdf/ashikhmin-shirley | bitwise | 32x24, 8 spp | 0.00352 / 0.00124 | within practical (strict diffs 44, max abs 7.56e-05, max rel 4.34e-05) |
| bsdf/beckmann | bitwise | 32x24, 8 spp | 0.00368 / 0.00156 | within practical (strict diffs 50, max abs 4.92e-06, max rel 3.78e-05) |
| bsdf/glossy-filter | bitwise | 32x24, 8 spp | 0.00366 / 0.00129 | within practical (strict diffs 26, max abs 5.96e-06, max rel 8.46e-06) |
| bsdf/metallic | bitwise | 32x24, 8 spp | 0.00387 / 0.00123 | within practical (strict diffs 39, max abs 3.81e-06, max rel 1.33e-05) |
| bsdf/metallic-film | bitwise | 32x24, 8 spp | 0.00375 / 0.00132 | within practical (strict diffs 37, max abs 6.14e-06, max rel 1.53e-05) |
| bsdf/toon | bitwise | 32x24, 8 spp | 0.0035 / 0.00106 | within practical (strict diffs 18, max abs 3.34e-06, max rel 4.72e-06) |
| bsdf/velvet | bitwise | 32x24, 8 spp | 0.00384 / 0.00111 | within practical (strict diffs 12, max abs 4.92e-07, max rel 8.79e-06) |
| cameras/central-cylindrical | bitwise | 32x24, 8 spp | 0.00316 / 0.000773 | within practical (strict diffs 6, max abs 1.37e-05, max rel 4.2e-05) |
| cameras/equiangular-cubemap-face | bitwise | 32x24, 8 spp | 0.00303 / 0.000527 | within practical (strict diffs 3, max abs 1.91e-06, max rel 1.88e-06) |
| cameras/equirectangular | bitwise | 32x24, 8 spp | 0.00363 / 0.00128 | within practical (strict diffs 11, max abs 7.87e-06, max rel 2.86e-05) |
| cameras/equirectangular-range-dof | bitwise | 32x24, 8 spp | 0.00305 / 0.000604 | within practical (strict diffs 1, max abs 1.49e-07, max rel 4.59e-07) |
| cameras/fisheye-dof-sobol-burley | bitwise | 32x24, 8 spp | 0.00283 / 0.00089 | within practical (strict diffs 9, max abs 4.32e-07, max rel 5.59e-06) |
| cameras/fisheye-equidistant | bitwise | 32x24, 8 spp | 0.00315 / 0.000887 | within practical (strict diffs 3, max abs 6.11e-07, max rel 2.73e-06) |
| cameras/fisheye-equisolid | bitwise | 32x24, 8 spp | 0.00303 / 0.000688 | within practical (strict diffs 3, max abs 1.79e-06, max rel 8.17e-06) |
| cameras/fisheye-lens-polynomial | bitwise | 32x24, 8 spp | 0.00326 / 0.000745 | within practical (strict diffs 2, max abs 5.07e-07, max rel 2.38e-06) |
| cameras/mirrorball | bitwise | 32x24, 8 spp | 0.00306 / 0.000742 | within practical (strict diffs 8, max abs 2.32e-05, max rel 4.18e-05) |
| cameras/orthographic | bitwise | 32x24, 8 spp | 0.0046 / 0.00201 | within practical (strict diffs 13, max abs 6.91e-06, max rel 2.8e-05) |
| cameras/orthographic-dof | bitwise | 32x24, 8 spp | 0.0047 / 0.0021 | within practical (strict diffs 20, max abs 5.41e-05, max rel 0.000232) |
| cameras/perspective-bokeh | bitwise | 32x24, 8 spp | 0.00385 / 0.00137 | within practical (strict diffs 22, max abs 7.96e-06, max rel 2.51e-05) |
| cameras/perspective-dof | bitwise | 32x24, 8 spp | 0.00385 / 0.00131 | within practical (strict diffs 14, max abs 6.08e-06, max rel 1.2e-05) |
| combinations/camera-motion-ao-node | bitwise | 32x24, 8 spp | 0.00319 / 0.00146 | within practical (strict diffs 15, max abs 6.85e-07, max rel 1.91e-06) |
| combinations/camera-motion-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00259 / 0.000856 | within practical (strict diffs 0, max abs 1.19e-07, max rel 3.18e-07) |
| combinations/enclosure-emissive-panel | bitwise | 64x48, 8 spp | 0.00849 / 0.00664 | **beyond practical**: 60 components, strict diffs 620, max abs 0.00377, max rel 0.0388, worst (52,12,R) ref 0.0971838757 cand 0.100957222 |
| combinations/enclosure-emissive-panel-mix | bitwise | 64x48, 8 spp | 0.00868 / 0.00728 | **beyond practical**: 58 components, strict diffs 462, max abs 0.00307, max rel 0.0287, worst (52,12,R) ref 0.107118838 cand 0.110190198 |
| combinations/enclosure-emissive-panel-oren-nayar | bitwise | 64x48, 8 spp | 0.00885 / 0.00727 | **beyond practical**: 58 components, strict diffs 516, max abs 0.00248, max rel 0.027, worst (52,12,R) ref 0.0918805599 cand 0.094358325 |
| combinations/enclosure-emissive-panel-translucent | bitwise | 64x48, 8 spp | 0.00476 / 0.00316 | within practical (strict diffs 0, max abs 5.96e-08, max rel 2.87e-07) |
| combinations/glass-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00248 / 0.000972 | within practical (strict diffs 4, max abs 2.68e-07, max rel 1.06e-06) |
| combinations/glass-receiver-transparent-lighting-4 | bitwise | 32x24, 8 spp | 0.00318 / 0.00144 | within practical (strict diffs 9, max abs 1.04e-05, max rel 2.81e-05) |
| combinations/link-ao-node | bitwise | 32x24, 8 spp | 0.00285 / 0.00128 | within practical (strict diffs 14, max abs 2.15e-06, max rel 5.16e-06) |
| combinations/link-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00236 / 0.000816 | within practical (strict diffs 2, max abs 1.79e-07, max rel 3.84e-07) |
| combinations/link-transparent-lighting-4 | bitwise | 32x24, 8 spp | 0.00281 / 0.00112 | within practical (strict diffs 7, max abs 3.73e-07, max rel 7.8e-06) |
| combinations/link-transparent-lighting-5 | bitwise | 32x24, 8 spp | 0.00309 / 0.00132 | within practical (strict diffs 0, max abs 7.08e-08, max rel 1.97e-05) |
| combinations/moving-transparent-fast-gi | bitwise | 32x24, 8 spp | 0.00271 / 0.000892 | within practical (strict diffs 0, max abs 1.04e-07, max rel 8.35e-07) |
| combinations/principled-ao-node | bitwise | 32x24, 8 spp | 0.00321 / 0.00153 | within practical (strict diffs 156, max abs 3.61e-06, max rel 2.04e-05) |
| combinations/principled-receiver-transparent-lighting-5 | bitwise | 32x24, 8 spp | 0.00354 / 0.00165 | within practical (strict diffs 23, max abs 3.71e-06, max rel 5.16e-05) |
| combinations/replace-transparent-fast-gi | bitwise | 32x24, 8 spp | 0.00258 / 0.000919 | within practical (strict diffs 0, max abs 1.04e-07, max rel 8.35e-07) |
| combinations/terminator-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00279 / 0.000938 | within practical (strict diffs 2, max abs 2.68e-07, max rel 5.48e-07) |
| combinations/tree-ao-node | bitwise | 32x24, 8 spp | 0.00326 / 0.0015 | within practical (strict diffs 44, max abs 1.34e-06, max rel 3.89e-06) |
| combinations/tree-camera-motion | bitwise | 64x48, 16 spp | 0.0089 / 0.00685 | within practical (strict diffs 492, max abs 3.96e-05, max rel 0.000195) |
| combinations/tree-catcher-approximate | bitwise | 64x48, 16 spp | 0.00926 / 0.00682 | within practical (strict diffs 39, max abs 1.24e-05, max rel 5.29e-06) |
| combinations/tree-catcher-point-light | bitwise | 64x48, 16 spp | 0.01 / 0.00746 | within practical (strict diffs 118, max abs 2.72e-05, max rel 1.02e-05) |
| combinations/tree-deforming-motion-5 | bitwise | 32x24, 8 spp | 0.00308 / 0.00122 | within practical (strict diffs 249, max abs 1.45e-06, max rel 1.38e-05) |
| combinations/tree-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00251 / 0.000951 | within practical (strict diffs 35, max abs 4.77e-07, max rel 9.67e-07) |
| combinations/tree-glass-receiver-transparent-lighting-6 | bitwise | 32x24, 8 spp | 0.00284 / 0.00124 | within practical (strict diffs 110, max abs 7.53e-07, max rel 1.2e-05) |
| combinations/tree-glass-render | bitwise | 64x48, 8 spp | 0.0181 / 0.0159 | **beyond practical**: 26 components, strict diffs 1132, max abs 0.00457, max rel 0.0297, worst (41,20,R) ref 0.153321236 cand 0.148768812 |
| combinations/tree-glass-sphere | bitwise | 64x48, 16 spp | 0.0101 / 0.00837 | **beyond practical**: 9 components, strict diffs 403, max abs 0.00561, max rel 0.0118, worst (59,9,R) ref 0.476515293 cand 0.470904887 |
| combinations/tree-link-transparent-lighting-6 | bitwise | 32x24, 8 spp | 0.00274 / 0.00102 | within practical (strict diffs 74, max abs 7.53e-07, max rel 2.15e-05) |
| combinations/tree-link-transparent-lighting-7 | bitwise | 32x24, 8 spp | 0.00283 / 0.00116 | within practical (strict diffs 103, max abs 7.53e-07, max rel 1.63e-05) |
| combinations/tree-object-motion-4 | bitwise | 32x24, 8 spp | 0.00303 / 0.00111 | within practical (strict diffs 254, max abs 1.82e-06, max rel 7.46) |
| combinations/tree-principled | bitwise | 64x48, 16 spp | 0.0107 / 0.00887 | **beyond practical**: 8 components, strict diffs 212, max abs 0.00214, max rel 0.0146, worst (59,9,R) ref 0.146555722 cand 0.144416198 |
| combinations/tree-principled-receiver-transparent-lighting-7 | bitwise | 32x24, 8 spp | 0.0031 / 0.00137 | within practical (strict diffs 176, max abs 4.69e-06, max rel 6.5e-05) |
| combinations/tree-replace-transparent-fast-gi | bitwise | 32x24, 8 spp | 0.00261 / 0.000892 | within practical (strict diffs 0, max abs 1.04e-07, max rel 8.35e-07) |
| combinations/tree-shadow-terminator | bitwise | 64x48, 16 spp | 0.00906 / 0.0073 | **beyond practical**: 6 components, strict diffs 343, max abs 0.00561, max rel 0.013, worst (59,9,R) ref 0.433232039 cand 0.427621573 |
| combinations/tree-textured-emitter | bitwise | 64x48, 16 spp | 0.00795 / 0.00637 | **beyond practical**: 8 components, strict diffs 418, max abs 0.00551, max rel 0.0101, worst (59,9,B) ref 0.545186043 cand 0.539673746 |
| combinations/tree-textured-plane-normal-map | bitwise | 64x48, 8 spp | 0.0123 / 0.0109 | within practical (strict diffs 885, max abs 7.63e-06, max rel 0.000222) |
| combinations/tree-transparent-lighting-0 | bitwise | 32x24, 8 spp | 0.00252 / 0.000919 | within practical (strict diffs 0, max abs 3.35e-08, max rel 2.7e-06) |
| combinations/tree-transparent-lighting-1 | bitwise | 32x24, 8 spp | 0.00256 / 0.00098 | within practical (strict diffs 2, max abs 2.38e-07, max rel 4.3e-06) |
| combinations/tree-transparent-lighting-2 | bitwise | 32x24, 8 spp | 0.00271 / 0.00123 | within practical (strict diffs 31, max abs 2.68e-06, max rel 2.18e-05) |
| combinations/tree-transparent-lighting-3 | bitwise | 32x24, 8 spp | 0.00245 / 0.00108 | within practical (strict diffs 4, max abs 2.87e-07, max rel 7.46e-05) |
| combinations/tree-transparent-lighting-4 | bitwise | 32x24, 8 spp | 0.00307 / 0.00139 | within practical (strict diffs 133, max abs 8.2e-07, max rel 1.47e-05) |
| combinations/tree-transparent-lighting-5 | bitwise | 32x24, 8 spp | 0.00332 / 0.00169 | within practical (strict diffs 91, max abs 5.14e-07, max rel 1.28e-05) |
| combinations/tree-transparent-lighting-6 | bitwise | 32x24, 8 spp | 0.00266 / 0.0011 | within practical (strict diffs 119, max abs 1.01e-06, max rel 1.41e-05) |
| combinations/tree-transparent-lighting-7 | bitwise | 32x24, 8 spp | 0.00277 / 0.00121 | within practical (strict diffs 192, max abs 1.01e-06, max rel 1.4e-05) |
| combinations/tree-triangle-lighting-1 | bitwise | 32x24, 8 spp | 0.00283 / 0.00126 | within practical (strict diffs 11, max abs 1.48e-06, max rel 1.84e-05) |
| combinations/tree-triangle-lighting-2 | bitwise | 32x24, 8 spp | 0.00282 / 0.00127 | within practical (strict diffs 20, max abs 1.43e-06, max rel 0.000346) |
| combinations/tree-triangle-lighting-3 | bitwise | 32x24, 8 spp | 0.0028 / 0.00115 | within practical (strict diffs 16, max abs 1.64e-06, max rel 5.85e-06) |
| combinations/tree-triangle-lighting-5 | bitwise | 32x24, 8 spp | 0.00287 / 0.00122 | within practical (strict diffs 11, max abs 9.24e-07, max rel 9.41e-06) |
| denoise/beckmann-off | bitwise | 32x24, 8 spp | 0.00362 / 0.0015 | no x64 film |
| denoise/beckmann-on | bitwise | 32x24, 8 spp | 0.0706 / 0.0103 | no x64 film |
| denoise/catcher-point-light-off-noisy | bitwise | 64x48, 16 spp | 0.00838 / 0.00588 | no x64 film |
| denoise/catcher-point-light-on-noisy | bitwise | 64x48, 16 spp | 0.0892 / 0.039 | no x64 film |
| denoise/enclosure-emissive-panel-off | bitwise | 64x48, 8 spp | 0.00876 / 0.00683 | no x64 film |
| denoise/enclosure-emissive-panel-on | bitwise | 64x48, 8 spp | 0.0728 / 0.0264 | no x64 film |
| displacement/both | bitwise | 32x24, 8 spp | 0.00296 / 0.000442 | within practical (strict diffs 8, max abs 2.98e-07, max rel 1.51e-06) |
| displacement/bump | bitwise | 32x24, 8 spp | 0.00215 / 0.00039 | within practical (strict diffs 20, max abs 8.14e-06, max rel 3.07e-05) |
| displacement/true-height | bitwise | 32x24, 8 spp | 0.00283 / 0.000413 | within practical (strict diffs 1, max abs 1.94e-07, max rel 1.27e-06) |
| displacement/true-vector | bitwise | 32x24, 8 spp | 0.00301 / 0.000431 | within practical (strict diffs 2, max abs 1.94e-07, max rel 9.22e-07) |
| lamps/area-zero-size | bitwise | 32x24, 16 spp | 0.00447 / 0.0027 | within practical (strict diffs 12, max abs 2.09e-06, max rel 6.27e-06) |
| lamps/lamp-area-parametric | bitwise | 32x24, 16 spp | 0.005 / 0.00282 | within practical (strict diffs 45, max abs 4.77e-06, max rel 9.21e-06) |
| lamps/lamp-point-falloff | bitwise | 32x24, 16 spp | 0.00477 / 0.00268 | within practical (strict diffs 29, max abs 3.81e-06, max rel 8.19e-06) |
| lamps/lamp-point-ies | bitwise | 32x24, 16 spp | 0.0043 / 0.00232 | within practical (strict diffs 0, max abs 7.45e-08, max rel 4.45e-07) |
| lamps/lamp-point-normal | bitwise | 32x24, 16 spp | 0.00505 / 0.00278 | within practical (strict diffs 10, max abs 7.54e-06, max rel 1.55e-05) |
| lamps/lamp-spot-parametric | bitwise | 32x24, 16 spp | 0.00455 / 0.00246 | **beyond practical**: 3 components, strict diffs 33, max abs 0.00104, max rel 0.000533, worst (21,10,B) ref 1.95158732 cand 1.95054734 |
| lamps/lamp-sun-parametric | bitwise | 32x24, 16 spp | 0.00489 / 0.00277 | **beyond practical**: 2 components, strict diffs 35, max abs 0.000485, max rel 0.000704, worst (15,10,R) ref 0.689506531 cand 0.68902117 |
| lamps/light-falloff-constant | bitwise | 32x24, 16 spp | 0.00493 / 0.00286 | within practical (strict diffs 213, max abs 6.1e-05, max rel 1.43e-05) |
| lamps/light-falloff-linear | bitwise | 32x24, 16 spp | 0.00512 / 0.00282 | within practical (strict diffs 107, max abs 7.63e-06, max rel 4.26e-06) |
| lamps/light-falloff-quadratic | bitwise | 32x24, 16 spp | 0.00497 / 0.00289 | within practical (strict diffs 16, max abs 2.09e-06, max rel 4.18e-06) |
| light-motion/camera-deforming-motion | bitwise | 32x24, 8 spp | 0.00264 / 0.00109 | within practical (strict diffs 0, max abs 9.08e-09, max rel 5.35e-06) |
| light-motion/camera-object-motion | bitwise | 32x24, 8 spp | 0.00266 / 0.000971 | within practical (strict diffs 1, max abs 4.47e-07, max rel 3.08) |
| light-motion/clamps-filters-exclude | bitwise | 64x48, 16 spp | 0.0124 / 0.0109 | within practical (strict diffs 153, max abs 1.79e-06, max rel 1.75e-05) |
| light-motion/light-cast-shadow | bitwise | 64x48, 16 spp | 0.0113 / 0.00814 | within practical (strict diffs 659, max abs 1.43e-05, max rel 1.41e-05) |
| light-motion/light-cast-shadow-tree | bitwise | 64x48, 16 spp | 0.0113 / 0.00965 | within practical (strict diffs 996, max abs 1.96e-05, max rel 1.82e-05) |
| light-motion/light-max-bounces | bitwise | 64x48, 16 spp | 0.00951 / 0.00792 | within practical (strict diffs 614, max abs 1.43e-05, max rel 1.41e-05) |
| light-motion/light-max-bounces-tree | bitwise | 64x48, 16 spp | 0.0123 / 0.00945 | within practical (strict diffs 868, max abs 1.96e-05, max rel 2.44e-05) |
| light-motion/linking-glass | bitwise | 64x48, 16 spp | 0.00683 / 0.00507 | within practical (strict diffs 377, max abs 1.23e-05, max rel 0.000147) |
| light-motion/linking-hdri | bitwise | 64x48, 16 spp | 0.00917 / 0.00659 | **beyond practical**: 3 components, strict diffs 344, max abs 0.0164, max rel 0.0598, worst (32,19,R) ref 0.273482919 cand 0.257122606 |
| light-motion/linking-motion-flat | bitwise | 64x48, 16 spp | 0.00651 / 0.00451 | within practical (strict diffs 434, max abs 5.72e-06, max rel 9.64e-06) |
| light-motion/linking-motion-tree | bitwise | 64x48, 16 spp | 0.00822 / 0.0063 | within practical (strict diffs 399, max abs 2.8e-06, max rel 2.05e-05) |
| light-motion/linking-transparent | bitwise | 64x48, 16 spp | 0.01 / 0.00805 | within practical (strict diffs 345, max abs 1.61e-06, max rel 7.75e-06) |
| light-motion/motion-glass-principled-lamps | bitwise | 64x48, 16 spp | 0.0133 / 0.0109 | within practical (strict diffs 644, max abs 9.73e-05, max rel 3.39e-05) |
| light-motion/motion-hdri-emitter | bitwise | 64x48, 16 spp | 0.0128 / 0.0106 | **beyond practical**: 33 components, strict diffs 176, max abs 0.00373, max rel 0.0203, worst (24,9,R) ref 0.0946976766 cand 0.0966200903 |
| light-tree/lamps-and-mesh-lights | bitwise | 64x48, 16 spp | 0.0076 / 0.00591 | **beyond practical**: 9 components, strict diffs 426, max abs 0.00561, max rel 0.0118, worst (59,9,R) ref 0.476515293 cand 0.470904887 |
| light-tree/light-linking | bitwise | 64x48, 16 spp | 0.00705 / 0.00534 | within practical (strict diffs 384, max abs 1.61e-06, max rel 4.34e-06) |
| linux-gates/broad-lighting-0 | bitwise | 32x24, 8 spp | 0.0022 / 0.000715 | within practical (strict diffs 0, max abs 3.35e-08, max rel 2.7e-06) |
| linux-gates/broad-lighting-1 | bitwise | 32x24, 8 spp | 0.00233 / 0.000827 | within practical (strict diffs 4, max abs 1.44e-07, max rel 8.83e-05) |
| linux-gates/broad-lighting-10 | bitwise | 32x24, 8 spp | 0.00225 / 0.000679 | within practical (strict diffs 20, max abs 5.25e-06, max rel 1.99e-06) |
| linux-gates/broad-lighting-11 | bitwise | 32x24, 8 spp | 0.00237 / 0.000836 | within practical (strict diffs 108, max abs 2.5e-06, max rel 2.62e-05) |
| linux-gates/broad-lighting-2 | bitwise | 32x24, 8 spp | 0.00232 / 0.000819 | within practical (strict diffs 4, max abs 4.06e-07, max rel 0.000662) |
| linux-gates/broad-lighting-3 | bitwise | 32x24, 8 spp | 0.00227 / 0.00075 | within practical (strict diffs 0, max abs 6.66e-08, max rel 0.0031) |
| linux-gates/broad-lighting-4 | bitwise | 32x24, 8 spp | 0.00237 / 0.00102 | within practical (strict diffs 55, max abs 1.61e-06, max rel 9.06e-06) |
| linux-gates/broad-lighting-5 | bitwise | 32x24, 8 spp | 0.00241 / 0.000909 | within practical (strict diffs 14, max abs 1.71e-06, max rel 1.73e-05) |
| linux-gates/broad-lighting-6 | bitwise | 32x24, 8 spp | 0.00245 / 0.000847 | within practical (strict diffs 14, max abs 8.2e-07, max rel 1.03e-05) |
| linux-gates/broad-lighting-7 | bitwise | 32x24, 8 spp | 0.00236 / 0.000786 | within practical (strict diffs 64, max abs 1.43e-05, max rel 4.35e-05) |
| linux-gates/broad-lighting-8 | bitwise | 32x24, 8 spp | 0.00232 / 0.000832 | within practical (strict diffs 70, max abs 1.74e-05, max rel 5.49e-05) |
| linux-gates/broad-lighting-9 | bitwise | 32x24, 8 spp | 0.00213 / 0.000821 | within practical (strict diffs 95, max abs 1.01e-05, max rel 0.00216) |
| linux-gates/closed-volumes | bitwise | 64x48, 8 spp | 0.0154 / 0.0136 | within practical (strict diffs 480, max abs 7.63e-06, max rel 0.00476) |
| linux-gates/deforming-motion-0 | bitwise | 32x24, 8 spp | 0.00239 / 0.000756 | within practical (strict diffs 0, max abs 5.22e-08, max rel 2.7e-06) |
| linux-gates/deforming-motion-1 | bitwise | 32x24, 8 spp | 0.00244 / 0.00099 | within practical (strict diffs 3, max abs 3.95e-07, max rel 1.84e-05) |
| linux-gates/deforming-motion-2 | bitwise | 32x24, 8 spp | 0.00239 / 0.000816 | within practical (strict diffs 5, max abs 1.49e-07, max rel 3.69e-06) |
| linux-gates/deforming-motion-3 | bitwise | 32x24, 8 spp | 0.00266 / 0.000888 | within practical (strict diffs 0, max abs 9.2e-09, max rel 5.5e-06) |
| linux-gates/deforming-motion-4 | bitwise | 32x24, 8 spp | 0.00258 / 0.000895 | within practical (strict diffs 28, max abs 5.66e-07, max rel 1.35e-05) |
| linux-gates/deforming-motion-5 | bitwise | 32x24, 8 spp | 0.00263 / 0.000924 | within practical (strict diffs 16, max abs 4.02e-07, max rel 6.05e-05) |
| linux-gates/deforming-motion-6 | bitwise | 32x24, 8 spp | 0.0025 / 0.000832 | within practical (strict diffs 28, max abs 5.66e-07, max rel 8.15e-06) |
| linux-gates/deforming-motion-7 | bitwise | 32x24, 8 spp | 0.00273 / 0.00103 | within practical (strict diffs 22, max abs 3.73e-07, max rel 6.88e-06) |
| linux-gates/glass-render | bitwise | 64x48, 8 spp | 0.00905 / 0.00683 | within practical (strict diffs 1, max abs 2.31e-07, max rel 1.98e-06) |
| linux-gates/object-motion-0 | bitwise | 32x24, 8 spp | 0.00255 / 0.000922 | within practical (strict diffs 1, max abs 2.91e-07, max rel 0.287) |
| linux-gates/object-motion-1 | bitwise | 32x24, 8 spp | 0.003 / 0.000995 | within practical (strict diffs 3, max abs 3.8e-07, max rel 2.12) |
| linux-gates/object-motion-2 | bitwise | 32x24, 8 spp | 0.00264 / 0.00103 | within practical (strict diffs 5, max abs 4.54e-07, max rel 5.52) |
| linux-gates/object-motion-3 | bitwise | 32x24, 8 spp | 0.00263 / 0.000946 | within practical (strict diffs 0, max abs 9.2e-09, max rel 4.33) |
| linux-gates/object-motion-4 | bitwise | 32x24, 8 spp | 0.00264 / 0.00102 | within practical (strict diffs 24, max abs 3.73e-07, max rel 2.44) |
| linux-gates/object-motion-5 | bitwise | 32x24, 8 spp | 0.00243 / 0.000895 | within practical (strict diffs 10, max abs 3.2e-07, max rel 3.3) |
| linux-gates/object-motion-6 | bitwise | 32x24, 8 spp | 0.00256 / 0.000958 | within practical (strict diffs 29, max abs 3.87e-07, max rel 2.99) |
| linux-gates/object-motion-7 | bitwise | 32x24, 8 spp | 0.00254 / 0.000971 | within practical (strict diffs 20, max abs 4.17e-07, max rel 2.18) |
| linux-gates/textured-plane | bitwise | 64x48, 8 spp | 0.00806 / 0.00711 | within practical (strict diffs 440, max abs 7.63e-06, max rel 0.000946) |
| linux-gates/transparent-lighting-0 | bitwise | 32x24, 8 spp | 0.00239 / 0.000856 | within practical (strict diffs 0, max abs 3.35e-08, max rel 2.7e-06) |
| linux-gates/transparent-lighting-1 | bitwise | 32x24, 8 spp | 0.00258 / 0.00106 | within practical (strict diffs 2, max abs 2.38e-07, max rel 4.3e-06) |
| linux-gates/transparent-lighting-2 | bitwise | 32x24, 8 spp | 0.00275 / 0.00114 | within practical (strict diffs 31, max abs 2.68e-06, max rel 2.18e-05) |
| linux-gates/transparent-lighting-3 | bitwise | 32x24, 8 spp | 0.00261 / 0.000998 | within practical (strict diffs 4, max abs 2.87e-07, max rel 7.46e-05) |
| linux-gates/transparent-lighting-4 | bitwise | 32x24, 8 spp | 0.0028 / 0.00124 | within practical (strict diffs 9, max abs 3.73e-07, max rel 7.36e-06) |
| linux-gates/transparent-lighting-5 | bitwise | 32x24, 8 spp | 0.0028 / 0.00133 | within practical (strict diffs 0, max abs 7.45e-08, max rel 1.97e-05) |
| linux-gates/transparent-lighting-6 | bitwise | 32x24, 8 spp | 0.00253 / 0.000985 | within practical (strict diffs 4, max abs 3.73e-07, max rel 7.97e-06) |
| linux-gates/transparent-lighting-7 | bitwise | 32x24, 8 spp | 0.00247 / 0.000958 | within practical (strict diffs 12, max abs 3.73e-07, max rel 1.01e-05) |
| linux-gates/triangle-lighting-0 | bitwise | 32x24, 8 spp | 0.00209 / 0.0007 | within practical (strict diffs 2, max abs 1.94e-07, max rel 2.08e-06) |
| linux-gates/triangle-lighting-1 | bitwise | 32x24, 8 spp | 0.00252 / 0.000982 | within practical (strict diffs 12, max abs 1.35e-06, max rel 1.85e-05) |
| linux-gates/triangle-lighting-2 | bitwise | 32x24, 8 spp | 0.00261 / 0.00102 | within practical (strict diffs 30, max abs 2.82e-06, max rel 6.93e-05) |
| linux-gates/triangle-lighting-3 | bitwise | 32x24, 8 spp | 0.00251 / 0.000903 | within practical (strict diffs 17, max abs 1.1e-06, max rel 4.51e-06) |
| linux-gates/triangle-lighting-5 | bitwise | 32x24, 8 spp | 0.00256 / 0.00103 | within practical (strict diffs 20, max abs 1.5e-06, max rel 1.85e-05) |
| linux-gates/world-volume | bitwise | 64x48, 8 spp | 0.0152 / 0.0129 | **beyond practical**: 87 components, strict diffs 495, max abs 1.36, max rel 84.9, worst (38,15,B) ref 0 cand 1.3629173 |
| mnee/glass-sphere-caustic | bitwise | 32x24, 8 spp | 0.00426 / 0.00276 | within practical (strict diffs 15, max abs 5.07e-07, max rel 2.43e-06) |
| mnee/glass-sphere-deforming | bitwise | 32x24, 8 spp | 0.00404 / 0.0022 | within practical (strict diffs 4, max abs 2.09e-07, max rel 2.16e-06) |
| mnee/glass-sphere-fast-gi | bitwise | 32x24, 8 spp | 0.00391 / 0.00218 | within practical (strict diffs 5, max abs 2.09e-07, max rel 5.03e-07) |
| mnee/glass-sphere-object-motion | bitwise | 32x24, 8 spp | 0.00498 / 0.00315 | within practical (strict diffs 10, max abs 3.87e-07, max rel 1.82e-06) |
| passes/aovs | bitwise | 48x36, 8 spp | 0.00433 / 0.00267 | within practical (strict diffs 206, max abs 0.000247, max rel 9.58e-05) |
| passes/cryptomatte | bitwise | 48x36, 8 spp | 0.00507 / 0.00295 | within practical (strict diffs 213, max abs 0.000247, max rel 9.58e-05) |
| passes/data-passes | bitwise | 48x36, 8 spp | 0.00507 / 0.00301 | within practical (strict diffs 213, max abs 0.000247, max rel 9.58e-05) |
| passes/denoising-features | bitwise | 48x36, 8 spp | 0.00487 / 0.00311 | within practical (strict diffs 214, max abs 0.000247, max rel 9.58e-05) |
| passes/denoising-features-weighted | bitwise | 48x36, 8 spp | 0.00506 / 0.00317 | within practical (strict diffs 214, max abs 0.000247, max rel 9.58e-05) |
| passes/denoising-motion | bitwise | 48x36, 8 spp | 0.00442 / 0.00257 | within practical (strict diffs 203, max abs 0.000247, max rel 9.58e-05) |
| passes/denoising-motion-deforming | bitwise | 48x36, 8 spp | 0.00457 / 0.00297 | no x64 film |
| passes/holdout-transparent-film | bitwise | 48x36, 8 spp | 0.00458 / 0.003 | within practical (strict diffs 16, max abs 3.93e-06, max rel 2.13e-05) |
| passes/light-groups | bitwise | 48x36, 8 spp | 0.00433 / 0.00271 | within practical (strict diffs 203, max abs 0.000247, max rel 9.58e-05) |
| passes/light-passes | bitwise | 48x36, 8 spp | 0.00456 / 0.0027 | within practical (strict diffs 203, max abs 0.000247, max rel 9.58e-05) |
| passes/motion | bitwise | 48x36, 8 spp | 0.00407 / 0.00275 | within practical (strict diffs 203, max abs 0.000247, max rel 9.58e-05) |
| passes/motion-deforming | bitwise | 48x36, 8 spp | 0.0041 / 0.00262 | no x64 film |
| passes/sample-count | bitwise | 48x36, 8 spp | 0.00426 / 0.00255 | within practical (strict diffs 203, max abs 0.000247, max rel 9.58e-05) |
| passes/with-ao-ao-node | bitwise | 32x24, 8 spp | 0.0033 / 0.00162 | within practical (strict diffs 3, max abs 1.64e-07, max rel 8.71e-07) |
| passes/with-ao-fast-gi-additive | bitwise | 32x24, 8 spp | 0.0027 / 0.00109 | within practical (strict diffs 0, max abs 1.19e-07, max rel 3.46e-07) |
| passes/with-bsdf-ashikhmin-shirley | bitwise | 32x24, 8 spp | 0.00381 / 0.00165 | within practical (strict diffs 44, max abs 7.56e-05, max rel 4.34e-05) |
| passes/with-bsdf-beckmann | bitwise | 32x24, 8 spp | 0.00421 / 0.00173 | within practical (strict diffs 50, max abs 4.92e-06, max rel 3.78e-05) |
| passes/with-bsdf-glossy-filter | bitwise | 32x24, 8 spp | 0.00419 / 0.00174 | within practical (strict diffs 26, max abs 5.96e-06, max rel 8.46e-06) |
| passes/with-bsdf-metallic | bitwise | 32x24, 8 spp | 0.00414 / 0.00167 | within practical (strict diffs 39, max abs 3.81e-06, max rel 1.33e-05) |
| passes/with-bsdf-toon | bitwise | 32x24, 8 spp | 0.00391 / 0.00146 | within practical (strict diffs 18, max abs 3.34e-06, max rel 4.72e-06) |
| passes/with-bsdf-velvet | bitwise | 32x24, 8 spp | 0.00396 / 0.00171 | within practical (strict diffs 12, max abs 4.92e-07, max rel 8.79e-06) |
| passes/with-combinations-enclosure-emissive-panel-mix | bitwise | 64x48, 8 spp | 0.0105 / 0.00833 | **beyond practical**: 58 components, strict diffs 462, max abs 0.00307, max rel 0.0287, worst (52,12,R) ref 0.107118838 cand 0.110190198 |
| passes/with-combinations-glass-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00295 / 0.00124 | within practical (strict diffs 4, max abs 2.68e-07, max rel 1.06e-06) |
| passes/with-combinations-link-transparent-lighting-4 | bitwise | 32x24, 8 spp | 0.00343 / 0.00158 | within practical (strict diffs 7, max abs 3.73e-07, max rel 7.8e-06) |
| passes/with-combinations-tree-principled | bitwise | 64x48, 16 spp | 0.0132 / 0.0111 | **beyond practical**: 8 components, strict diffs 212, max abs 0.00214, max rel 0.0146, worst (59,9,R) ref 0.146555722 cand 0.144416198 |
| passes/with-combinations-tree-textured-emitter | bitwise | 64x48, 16 spp | 0.0104 / 0.00803 | **beyond practical**: 8 components, strict diffs 418, max abs 0.00551, max rel 0.0101, worst (59,9,B) ref 0.545186043 cand 0.539673746 |
| passes/with-combinations-tree-transparent-lighting-3 | bitwise | 32x24, 8 spp | 0.00316 / 0.00132 | within practical (strict diffs 4, max abs 2.87e-07, max rel 7.46e-05) |
| passes/with-displacement-both | bitwise | 32x24, 8 spp | 0.00303 / 0.000613 | within practical (strict diffs 8, max abs 2.98e-07, max rel 1.51e-06) |
| passes/with-displacement-true-vector | bitwise | 32x24, 8 spp | 0.00346 / 0.000642 | within practical (strict diffs 2, max abs 1.94e-07, max rel 9.22e-07) |
| passes/with-light-motion-clamps-filters-exclude | bitwise | 64x48, 16 spp | 0.0151 / 0.0126 | within practical (strict diffs 153, max abs 1.79e-06, max rel 1.75e-05) |
| passes/with-light-motion-linking-hdri | bitwise | 64x48, 16 spp | 0.0111 / 0.00846 | **beyond practical**: 3 components, strict diffs 344, max abs 0.0164, max rel 0.0598, worst (32,19,R) ref 0.273482919 cand 0.257122606 |
| passes/with-light-motion-motion-hdri-emitter | bitwise | 64x48, 16 spp | 0.0156 / 0.0123 | **beyond practical**: 33 components, strict diffs 176, max abs 0.00373, max rel 0.0203, worst (24,9,R) ref 0.0946976766 cand 0.0966200903 |
| passes/with-light-tree-lamps-and-mesh-lights | bitwise | 64x48, 16 spp | 0.0104 / 0.00809 | **beyond practical**: 9 components, strict diffs 426, max abs 0.00561, max rel 0.0118, worst (59,9,R) ref 0.476515293 cand 0.470904887 |
| passes/with-light-tree-light-linking | bitwise | 64x48, 16 spp | 0.0113 / 0.00768 | within practical (strict diffs 384, max abs 1.61e-06, max rel 4.34e-06) |
| passes/with-linux-gates-broad-lighting-0 | bitwise | 32x24, 8 spp | 0.00291 / 0.00112 | within practical (strict diffs 0, max abs 3.35e-08, max rel 2.7e-06) |
| passes/with-linux-gates-broad-lighting-5 | bitwise | 32x24, 8 spp | 0.00291 / 0.00113 | within practical (strict diffs 14, max abs 1.71e-06, max rel 1.73e-05) |
| passes/with-linux-gates-deforming-motion-0 | bitwise | 32x24, 8 spp | 0.0027 / 0.00115 | within practical (strict diffs 0, max abs 5.22e-08, max rel 2.7e-06) |
| passes/with-linux-gates-glass-render | bitwise | 64x48, 8 spp | 0.0114 / 0.00908 | within practical (strict diffs 1, max abs 2.31e-07, max rel 1.98e-06) |
| passes/with-linux-gates-object-motion-0 | bitwise | 32x24, 8 spp | 0.00303 / 0.00121 | within practical (strict diffs 1, max abs 2.91e-07, max rel 0.287) |
| passes/with-linux-gates-triangle-lighting-1 | bitwise | 32x24, 8 spp | 0.00294 / 0.00126 | within practical (strict diffs 12, max abs 1.35e-06, max rel 1.85e-05) |
| passes/with-mnee-glass-sphere-caustic | bitwise | 32x24, 8 spp | 0.00498 / 0.00313 | within practical (strict diffs 15, max abs 5.07e-07, max rel 2.43e-06) |
| passes/with-mnee-glass-sphere-object-motion | bitwise | 32x24, 8 spp | 0.00547 / 0.00408 | within practical (strict diffs 10, max abs 3.87e-07, max rel 1.82e-06) |
| passes/with-shadow-catcher-catcher-approximate | bitwise | 64x48, 16 spp | 0.00908 / 0.00637 | within practical (strict diffs 19, max abs 1.24e-05, max rel 5.29e-06) |
| passes/with-shadow-catcher-catcher-point-light | bitwise | 64x48, 16 spp | 0.0103 / 0.00778 | within practical (strict diffs 32, max abs 2.72e-05, max rel 1.04e-05) |
| passes/with-starter-glass-and-area-light | bitwise | 64x48, 16 spp | 0.0068 / 0.00473 | within practical (strict diffs 63, max abs 1.88e-06, max rel 4.04e-06) |
| passes/with-starter-textured-plane | bitwise | 48x32, 8 spp | 0.0034 / 0.00136 | within practical (strict diffs 67, max abs 2.91e-07, max rel 2.59e-06) |
| passes/with-surface-nodes-bevel | bitwise | 32x24, 8 spp | 0.00369 / 0.00167 | within practical (strict diffs 9, max abs 5.36e-07, max rel 1.57e-06) |
| passes/with-surface-nodes-radial-tiling | bitwise | 32x24, 8 spp | 0.00279 / 0.0013 | within practical (strict diffs 67, max abs 2.03e-06, max rel 3.46e-06) |
| passes/with-surface-nodes-raycast | bitwise | 32x24, 8 spp | 0.00334 / 0.00137 | within practical (strict diffs 2, max abs 1.19e-07, max rel 7.08e-07) |
| sampling/adaptive-auto | bitwise | 32x24, 96 spp | 0.0156 / 0.0144 | within practical (strict diffs 44, max abs 5.39e-05, max rel 0.000176) |
| sampling/adaptive-blue-noise | bitwise | 32x24, 64 spp | 0.0058 / 0.00402 | within practical (strict diffs 33, max abs 7.16e-05, max rel 0.000332) |
| sampling/adaptive-coarse | bitwise | 32x24, 48 spp | 0.00465 / 0.0025 | within practical (strict diffs 12, max abs 2.09e-06, max rel 6.27e-06) |
| sampling/adaptive-sobol-burley | bitwise | 32x24, 64 spp | 0.00611 / 0.00423 | **beyond practical**: 1 components, strict diffs 38, max abs 0.000105, max rel 0.000457, worst (16,10,R) ref 0.229488119 cand 0.22938323 |
| sampling/adaptive-threshold | bitwise | 32x24, 64 spp | 0.00571 / 0.00377 | within practical (strict diffs 8, max abs 1.28e-06, max rel 6.27e-06) |
| sampling/blue-noise-first | bitwise | 32x24, 16 spp | 0.00469 / 0.00299 | within practical (strict diffs 16, max abs 2.35e-06, max rel 6.24e-06) |
| sampling/blue-noise-first-ao-ao-node | bitwise | 32x24, 8 spp | 0.00288 / 0.00147 | within practical (strict diffs 22, max abs 5.44e-06, max rel 4.06e-05) |
| sampling/blue-noise-first-shadow-catcher-catcher-point-light | bitwise | 64x48, 16 spp | 0.00789 / 0.00647 | within practical (strict diffs 42, max abs 1.88e-05, max rel 1e-05) |
| sampling/blue-noise-pure | bitwise | 32x24, 16 spp | 0.0057 / 0.00341 | within practical (strict diffs 28, max abs 6.68e-06, max rel 1.27e-05) |
| sampling/blue-noise-pure-ao-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00222 / 0.00101 | within practical (strict diffs 0, max abs 1.19e-07, max rel 3.23e-07) |
| sampling/blue-noise-pure-light-motion-linking-hdri | bitwise | 64x48, 16 spp | 0.00893 / 0.00751 | within practical (strict diffs 365, max abs 1.66e-05, max rel 3.19e-05) |
| sampling/blue-noise-pure-light-tree-lamps-and-mesh-lights | bitwise | 64x48, 16 spp | 0.00817 / 0.00677 | **beyond practical**: 3 components, strict diffs 399, max abs 0.00118, max rel 0.00535, worst (57,8,R) ref 0.220365345 cand 0.219186872 |
| sampling/blue-noise-round | bitwise | 32x24, 12 spp | 0.00446 / 0.00233 | within practical (strict diffs 23, max abs 8.94e-06, max rel 1.5e-05) |
| sampling/scrambling-distance | bitwise | 32x24, 16 spp | 0.00458 / 0.00253 | within practical (strict diffs 25, max abs 1.43e-05, max rel 1.92e-05) |
| sampling/sobol-burley | bitwise | 32x24, 16 spp | 0.00436 / 0.00277 | within practical (strict diffs 26, max abs 4.41e-06, max rel 1.38e-05) |
| sampling/sobol-burley-combinations-camera-motion-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00228 / 0.000883 | within practical (strict diffs 0, max abs 1.19e-07, max rel 2.39e-07) |
| sampling/sobol-burley-combinations-tree-textured-plane-normal-map | bitwise | 64x48, 8 spp | 0.0131 / 0.011 | within practical (strict diffs 785, max abs 7.66e-06, max rel 0.000494) |
| sampling/sobol-burley-light-motion-clamps-filters-exclude | bitwise | 64x48, 16 spp | 0.012 / 0.0112 | within practical (strict diffs 176, max abs 1.24e-06, max rel 2.91e-05) |
| sampling/sobol-burley-mnee-glass-sphere-caustic | bitwise | 32x24, 8 spp | 0.0039 / 0.00254 | within practical (strict diffs 13, max abs 7.09e-06, max rel 7.52e-06) |
| sampling/sobol-burley-surface-nodes-bevel | bitwise | 32x24, 8 spp | 0.00316 / 0.00139 | within practical (strict diffs 11, max abs 8.94e-07, max rel 3.87e-06) |
| shadow-catcher/catcher-approximate | bitwise | 64x48, 16 spp | 0.00693 / 0.00518 | within practical (strict diffs 19, max abs 1.24e-05, max rel 5.29e-06) |
| shadow-catcher/catcher-point-light | bitwise | 64x48, 16 spp | 0.00758 / 0.00616 | within practical (strict diffs 32, max abs 2.72e-05, max rel 1.04e-05) |
| sky/hosek-wilkie | bitwise | 32x24, 16 spp | 0.00739 / 0.00344 | within practical (strict diffs 15, max abs 4.83e-06, max rel 2.33e-05) |
| sky/multiple-scattering | bitwise | 32x24, 16 spp | 0.304 / 0.00342 | within practical (strict diffs 338, max abs 0.00011, max rel 8.2e-05) |
| sky/multiple-scattering-sun | bitwise | 32x24, 16 spp | 0.307 / 0.00336 | within practical (strict diffs 366, max abs 0.00011, max rel 8.12e-05) |
| sky/multiple-scattering-sun-guiding | bitwise | 32x24, 16 spp | 0.305 / 0.0034 | within practical (strict diffs 378, max abs 0.000851, max rel 9.16e-05) |
| sky/preetham | bitwise | 32x24, 16 spp | 0.00714 / 0.00339 | within practical (strict diffs 14, max abs 4.83e-06, max rel 1.97e-05) |
| sky/single-scattering | bitwise | 32x24, 16 spp | 0.18 / 0.00311 | within practical (strict diffs 356, max abs 1.36e-05, max rel 1.19e-05) |
| sky/single-scattering-sun-guiding | bitwise | 32x24, 16 spp | 0.181 / 0.00295 | within practical (strict diffs 336, max abs 6.56e-06, max rel 2.36e-05) |
| sky/sun-guiding-emissive-panel | bitwise | 32x24, 16 spp | 0.309 / 0.0037 | no x64 film |
| sky/sun-guiding-light-tree | bitwise | 32x24, 16 spp | 0.305 / 0.0034 | no x64 film |
| sky/sun-guiding-light-tree-panel | bitwise | 32x24, 16 spp | 0.18 / 0.00377 | no x64 film |
| starter/diffuse-triangle | bitwise | 32x24, 8 spp | 0.00164 / 0.000311 | within practical (strict diffs 0, max abs 2.98e-08, max rel 2.22e-07) |
| starter/glass-and-area-light | bitwise | 64x48, 16 spp | 0.00584 / 0.00404 | within practical (strict diffs 63, max abs 1.88e-06, max rel 4.04e-06) |
| starter/glossy-sphere-point-light | bitwise | 64x48, 16 spp | 0.00659 / 0.00403 | within practical (strict diffs 156, max abs 6.34e-05, max rel 9.09e-06) |
| starter/textured-plane | bitwise | 48x32, 8 spp | 0.0024 / 0.000817 | within practical (strict diffs 67, max abs 2.91e-07, max rel 2.59e-06) |
| subsurface/node-methods | bitwise | 32x24, 16 spp | 0.00642 / 0.0046 | no x64 film |
| subsurface/node-skin | bitwise | 32x24, 16 spp | 0.0066 / 0.00445 | no x64 film |
| subsurface/principled-marble | bitwise | 32x24, 16 spp | 0.00561 / 0.00393 | no x64 film |
| subsurface/principled-random-walk | bitwise | 32x24, 16 spp | 0.00572 / 0.00373 | no x64 film |
| subsurface/principled-skin | bitwise | 32x24, 16 spp | 0.00607 / 0.00399 | no x64 film |
| subsurface/principled-wax | bitwise | 32x24, 16 spp | 0.00517 / 0.00304 | no x64 film |
| surface-nodes/bevel | bitwise | 32x24, 8 spp | 0.00316 / 0.00146 | within practical (strict diffs 9, max abs 5.36e-07, max rel 1.57e-06) |
| surface-nodes/radial-tiling | bitwise | 32x24, 8 spp | 0.00252 / 0.000871 | within practical (strict diffs 67, max abs 2.03e-06, max rel 3.46e-06) |
| surface-nodes/raycast | bitwise | 32x24, 8 spp | 0.00277 / 0.00102 | within practical (strict diffs 2, max abs 1.19e-07, max rel 7.08e-07) |
| volumes/absorption-point | bitwise | 32x24, 16 spp | 0.00744 / 0.00503 | no x64 film |
| volumes/heterogeneous-point | bitwise | 32x24, 16 spp | 1.38 / 0.00798 | no x64 film |
| volumes/heterogeneous-point-distance | bitwise | 32x24, 16 spp | 1.39 / 0.00719 | no x64 film |
| volumes/heterogeneous-point-equiangular | bitwise | 32x24, 16 spp | 1.39 / 0.0104 | no x64 film |
| volumes/heterogeneous-sun | bitwise | 32x24, 16 spp | 1.42 / 0.00658 | no x64 film |
| volumes/phase-draine | bitwise | 32x24, 16 spp | 0.00938 / 0.00707 | no x64 film |
| volumes/phase-fournier-forand | bitwise | 32x24, 16 spp | 0.00919 / 0.00692 | no x64 film |
| volumes/phase-mie | bitwise | 32x24, 16 spp | 0.0098 / 0.00773 | no x64 film |
| volumes/phase-mie-small | bitwise | 32x24, 16 spp | 0.00954 / 0.0073 | no x64 film |
| volumes/phase-rayleigh | bitwise | 32x24, 16 spp | 0.0091 / 0.00691 | no x64 film |
| volumes/principled-volume | bitwise | 32x24, 16 spp | 0.00975 / 0.00703 | no x64 film |
| volumes/ray-marching-heterogeneous-distance | bitwise | 32x24, 16 spp | 0.0135 / 0.011 | no x64 film |
| volumes/ray-marching-heterogeneous-equiangular | bitwise | 32x24, 16 spp | 0.016 / 0.0135 | no x64 film |
| volumes/ray-marching-heterogeneous-point | bitwise | 32x24, 16 spp | 0.0146 / 0.0124 | no x64 film |
| volumes/ray-marching-heterogeneous-sun | bitwise | 32x24, 16 spp | 0.0112 / 0.0088 | no x64 film |
| volumes/ray-marching-homogeneous-point | bitwise | 32x24, 16 spp | 0.00864 / 0.00712 | no x64 film |
| volumes/ray-marching-max-steps | bitwise | 32x24, 16 spp | 0.0131 / 0.0106 | no x64 film |
| volumes/scatter-point | bitwise | 32x24, 16 spp | 0.0087 / 0.00666 | no x64 film |
| volumes/scatter-point-distance | bitwise | 32x24, 16 spp | 0.0086 / 0.00653 | no x64 film |
| volumes/scatter-point-equiangular | bitwise | 32x24, 16 spp | 0.00867 / 0.00631 | no x64 film |
| volumes/scatter-sun | bitwise | 32x24, 16 spp | 0.00818 / 0.00598 | no x64 film |
| volumes/scatter-world | bitwise | 32x24, 16 spp | 0.00317 / 0.00125 | no x64 film |
| volumes/volume-coefficients | bitwise | 32x24, 16 spp | 0.00937 / 0.0065 | no x64 film |
| volumes/volume-emission | bitwise | 32x24, 16 spp | 0.00842 / 0.00584 | no x64 film |
| volumes/world-volume-point | bitwise | 32x24, 16 spp | 0.01 / 0.00803 | no x64 film |
