# Linux ARM64 CPU oracle corpus, Debian arm64 container on an Apple Silicon Mac (2026-10-31)

Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

The Linux ARM64 CPU oracle films of the committed oracle corpus, made by the procedure
in docs/TESTING-LINUX-ARM64-ORACLE.md: backend `cpu-arm64-linux` of
`native/oracle/render_oracles.py`, the unmodified pinned Cycles CPU kernels at the fixed
ARM64 oracle baseline (ARMv8-A base level, `-ffp-contract=off`, `-fno-reciprocal-math`),
GCC 14 and glibc 2.41, in a `linux/arm64` Debian 13 container running natively (no
emulation) under Docker Desktop on a Mac mini M2 Pro.

These films are **evidence, not a gold standard** (native/DEVICE.md): the x64 gold
standard stays `native/evidence/linux-x64-reference-20261030/`. They let the Raspberry Pi 5
compare its WGSL renders against a CPU rendering of the same architecture without rendering
the CPU corpus itself (docs/TESTING-LINUX-ARM64.md).

**Result:** 135 scenes (133 corpus scenes plus the two bench documents), 135 rendered,
**no refusals and no failures**, every film's two repeats **bitwise identical**. Against the
Linux x64 CPU oracle (information only): 104 scenes within the practical criterion (none bit
identical), **14 beyond it** (findings, listed below), 17 without an x64 film.

## Source

| Item | Value |
|---|---|
| Commit | `af0a254dd26966f8223e5c55f70f1866fd25ec47` |
| Working tree | **not clean** (`repoWorkingTreeClean: false` in every `meta.json`). At the start of the render exactly two files differed from the commit, both made in this run and committed with this folder: `native/oracle/render_oracles.py` (SHA-256 `542d3d10f13bb9265b6a919c262a7966f71d4c7ec5330e389839b616b95d448d`) and `docs/TESTING-LINUX-ARM64-ORACLE.md` (SHA-256 `f813024aa328ee6dbfe54f5aa6bd22ec1b82da0fc56062644b43bfd67c2e7f29`); `environment/render-status-start.txt`. Neither file is compiled into the runner or the host library. |
| Why | The first render attempt stopped before any film (`environment/attempt1-render.output.txt`): `render_oracles.py` read `/proc/device-tree/model` unconditionally on Linux ARM64, and a container has no device tree (`FileNotFoundError: [Errno 2] No such file or directory: '/proc/device-tree/model'`). The fix records `deviceModel` as `unavailable: no /proc/device-tree/model (no device tree, for example in a container)` instead. The document now also installs `gcc` and `g++`, so that `c++`, which `render_oracles.py` records as the compiler, is GCC 14 and not clang (see Environment). |

## Environment (`environment/`)

| Item | Value |
|---|---|
| Mac | Mac mini (Mac14,12), Apple M2 Pro, 10 cores (6 performance, 4 efficiency), 16 GB; macOS 27.2 |
| Docker | Docker Desktop, engine 29.9.2, LinuxKit kernel 7.0.14 (`linux/arm64`), VM with 10 CPUs and 7.7 GiB; `uname -m` `aarch64` |
| Image | `debian:trixie`, `debian@sha256:2dc419c6d7fe3dad0a2f9fd469c34cc90b0f9ba642d8fde837fe3d7d34a0e7f1` (created 2026-10-27) |
| Toolchain | GCC 14.2.0-19 (`gcc-14`, `g++-14`), glibc 2.41-12+deb13u4, CMake 3.31.6, Python 3.13.5, git 2.47.3 (`toolchain.txt`); the same Debian release, GCC and glibc builds as the Linux x64 machine |
| Packages | as docs/TESTING-LINUX-ARM64-ORACLE.md section 2. The build ran before `gcc` and `g++` were added; they were installed before the render (`toolchain-after-gpp.txt`), so every film's `meta.json` records `c++ (Debian 14.2.0-19) 14.2.0`. They change no build output: all 456 compile commands of the host build call `/usr/bin/g++-14` or `/usr/bin/gcc-14` |
| WebGPU runtime | CodeBrix.WebGpu.Native.MitLicenseForever 1.0.300.210 from the NuGet cache (mounted read-only), `runtimes/linux-arm64/native/libcodebrix_wgpu.so` SHA-256 `3fb3a382d83dd9ff535829f6abeabdd26837a05decdacbb80f9b442e005c9eec`, the same file as in `device-linux-arm64-20261029`; the host library links it (`RUNPATH`), the CPU oracle does not render with it |
| Device model | `meta.json` `deviceModel` is `unavailable: ...` (no device tree in the container); the machine is the Mac above |

## Build (inside the container)

```sh
git config --global --add safe.directory /work/CodeBrix.Graphics3D.Cycles
python3 tools/dependencies/build_linux_arm64.py --jobs 8                  # 288.6 s wall
python3 native/tools/build_cycles_linux_arm64.py --jobs 8 --device-set   # 1,084.5 s wall
```

Both from an empty `native/work/linux/linux-arm64/`; both passed (`build-exit.txt`,
`wall-*.txt`; logs `dependencies.output.txt`, `cycles-build.output.txt`). The build recipe
checked the CPU kernel baseline; the kernel compile line ends `-ffp-contract=off
-fno-reciprocal-math` and has no `-march`/`-mcpu` (`cpuKernelCompile` in every `meta.json`).

| Binary | SHA-256 |
|---|---|
| `native/work/linux/linux-arm64/host/build/bin/codebrix_cycles_oracle` (runner) | `982f00418954f53dd0edac58e3353fde5f5aa0a01a13a30f0a574d33b9749f8f` |
| `native/work/linux/linux-arm64/host/build/lib/libcodebrix_cycles_host.so` | `ab633d0f17066b6f158a5b94d96c27aad9250ae00b3ced359256d376876173cd` |

## Render (inside the container)

The scene list of docs/TESTING-LINUX-ARM64-ORACLE.md section 5: every `*.json` under
`native/oracle/scenes` except `manifest.json` files and `assets/`, plus the bench bundle's two
documents from `native/work/oracle-assets/bench_01/` (generated on this Mac on 2026-10-23 with
`make_bench_bundle.py`; all 13 generated files were checked against
`native/oracle/scenes/assets/manifest.json` on 2026-10-31 and match). The list is
`environment/render-scenes.txt`.

```sh
python3 native/oracle/render_oracles.py --output native/work/oracles/linux-arm64-20261031 \
  --runner "$RUNNER" --host-library "$HOSTLIB" --backend cpu-arm64-linux "${scenes[@]}"
```

Wall 24.2 s for 135 scenes (2026-10-31 19:12:34 to 19:12:58 UTC; default 4 threads), exit 0
(`render.output.txt`). No scene was refused and none failed; the two shader-volume scenes
render on the CPU backend. `repeatBitwiseIdentical` is `true` for all 135 films. With one
backend, `render_oracles.py` has nothing to compare inside the run: `comparisonsPassed` is
`null` for every scene and no `comparisons/` directory is written.

## Comparison with the Linux x64 CPU oracle (information only)

Reference: `native/evidence/linux-x64-reference-20261030/` (commit `51fc577cc` plus the
working tree recorded there; the newest x64 reference), `oracles/corpus/oracle/<scene>/cpu-x64-linux`
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
| No x64 film (scene added after the x64 reference) | 17 |

Differences between the two CPU oracles are **findings to record, never failures** and never
tuned away (native/DEVICE.md open item 25). Scenes beyond the practical criterion (practical
failures counted per component; worst pixel x, y, channel; reference = x64, candidate = ARM64):

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

The largest is `linux-gates/world-volume` (87 components; at pixel 38, 15 the x64 film has
blue 0 and the ARM64 film 1.3629173). In the other 13 the largest absolute difference is
0.0164 (`light-motion/linking-hdri`). None of these was rerendered or adjusted.

### Cross-check: `linux-gates/world-volume` on the Mac's own CPU

The largest difference was checked on 2026-10-31 by rendering the same scene with the native macOS ARM64 CPU
backend (`cpu-arm64-macos`: Apple clang 21, macOS libm, outside the container; `render_oracles.py --backend
cpu-arm64-macos --scene native/oracle/scenes/linux-gates/world-volume.json`) and comparing
(`cross-check/world-volume/`):

| Candidate | Reference | Practical failures | Strict differences | Max abs | Worst pixel |
|---|---|---|---|---|---|
| cpu-arm64-macos | cpu-arm64-linux (this folder) | 0 | 7 | 1.28e-05 | 18, 5, R: 0.471458733 / 0.471471518 |
| cpu-arm64-macos | cpu-x64-linux | 87 | 494 | 1.36 | 38, 15, B: 0 / 1.36291742 |

The two ARM64 CPU builds (GCC 14 and glibc in the container, Apple clang and macOS natively) agree within the practical
criterion, and both differ from x64 the same way. The difference therefore follows the instruction set of the CPU
kernels (the x64 build compiles Cycles' SSE4.2 code paths, for example `_mm_dp_ps` dot products and `_mm_sqrt_ps`
normalization in `util/math_float3.h`; the ARM64 builds compile the scalar code), not the container, compiler or C
library. The differing pixels are one cluster of 31 of the 3,072 pixels (x 35 to 41, y 12 to 20), and the
difference runs both ways (pixel 41, 15 is 0 on ARM64 and non-zero on x64); both films have negative components there,
and the film means differ by 0.1% (0.35726 and 0.35763). At 8 samples per pixel this is the pattern of a few
volume-scattering paths taking a different branch after a last-bit difference, a different sample of the same
estimate rather than a bias, but that interpretation is not proven here.

Scenes without an x64 film (all 17 were added in commit `af0a254dd`, after `linux-x64-reference-20261030`): `bsdf/ashikhmin-shirley`, `bsdf/beckmann`, `bsdf/glossy-filter`, `bsdf/metallic`, `bsdf/toon`, `bsdf/velvet`, `combinations/moving-transparent-fast-gi`, `displacement/both`, `displacement/bump`, `displacement/true-height`, `displacement/true-vector`, `mnee/glass-sphere-deforming`, `mnee/glass-sphere-fast-gi`, `mnee/glass-sphere-object-motion`, `surface-nodes/bevel`, `surface-nodes/radial-tiling`, `surface-nodes/raycast`.

## Contents

- `oracle/<scene>/cpu-arm64-linux/`: `film-0.f32`, `film-1.f32` (scene-linear RGBA float32,
  little-endian, lower-left origin) and `meta.json`; `oracle/<scene>/cpu-arm64-linux.output.txt`,
  the runner's output.
- `run.json`: the render record (`scenes`, `backends`, `common`, `comparisonsPassed`).
- `vs-x64/`: the section 6 comparison reports, one per scene with an x64 film.
- `cross-check/world-volume/`: the macOS ARM64 CPU render of `linux-gates/world-volume` (`oracle/`, `run.json`)
  and its two comparison reports.
- `environment/`: the container toolchain (`toolchain.txt`, `toolchain-after-gpp.txt`),
  runner and host paths and SHA-256 (`paths.txt`), the scene list, the working-tree status at
  the start of the render, start and end times, walls and exit codes, the build, render and
  compare logs, the failed first attempt's log, the build records (`build/`: the host build's `status.json` with
  every artifact's SHA-256 and the CPU kernel flags, and the dependency build's `status.json`), and the scripts that ran the render
  (`render.sh`) and wrote the table below (`summarize.py`).

Built executables and libraries are not evidence and are not copied here; their SHA-256 are above.

## Per-scene record

Repeat: the two renders of each scene (`repeatBitwiseIdentical`). Render seconds as recorded
in `meta.json` (the first render includes scene setup).

| Scene | Repeat | Film | Render s (1st / 2nd) | vs cpu-x64-linux |
|---|---|---|---|---|
| ao/ao-node | bitwise | 32x24, 8 spp | 0.00552 / 0.00147 | within practical (strict diffs 3, max abs 1.64e-07, max rel 8.71e-07) |
| ao/fast-gi-additive | bitwise | 32x24, 8 spp | 0.00323 / 0.00104 | within practical (strict diffs 0, max abs 1.19e-07, max rel 3.46e-07) |
| assets/bench_01 | bitwise | 64x48, 16 spp | 0.0354 / 0.00631 | **beyond practical**: 32 components, strict diffs 1603, max abs 0.00424, max rel 0.00957, worst (42,22,R) ref 0.443487018 cand 0.447729558 |
| assets/bench_01-hdri | bitwise | 64x48, 16 spp | 0.0297 / 0.00921 | **beyond practical**: 67 components, strict diffs 2078, max abs 0.0118, max rel 0.0208, worst (42,22,R) ref 0.566275716 cand 0.578031242 |
| bsdf/ashikhmin-shirley | bitwise | 32x24, 8 spp | 0.00455 / 0.00141 | no x64 film |
| bsdf/beckmann | bitwise | 32x24, 8 spp | 0.00445 / 0.00195 | no x64 film |
| bsdf/glossy-filter | bitwise | 32x24, 8 spp | 0.00456 / 0.00171 | no x64 film |
| bsdf/metallic | bitwise | 32x24, 8 spp | 0.00498 / 0.00152 | no x64 film |
| bsdf/toon | bitwise | 32x24, 8 spp | 0.004 / 0.00142 | no x64 film |
| bsdf/velvet | bitwise | 32x24, 8 spp | 0.00454 / 0.00134 | no x64 film |
| combinations/camera-motion-ao-node | bitwise | 32x24, 8 spp | 0.00444 / 0.00182 | within practical (strict diffs 15, max abs 6.85e-07, max rel 1.91e-06) |
| combinations/camera-motion-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00318 / 0.00104 | within practical (strict diffs 0, max abs 1.19e-07, max rel 3.18e-07) |
| combinations/enclosure-emissive-panel | bitwise | 64x48, 8 spp | 0.00927 / 0.00808 | **beyond practical**: 60 components, strict diffs 620, max abs 0.00377, max rel 0.0388, worst (52,12,R) ref 0.0971838757 cand 0.100957222 |
| combinations/enclosure-emissive-panel-mix | bitwise | 64x48, 8 spp | 0.011 / 0.0088 | **beyond practical**: 58 components, strict diffs 462, max abs 0.00307, max rel 0.0287, worst (52,12,R) ref 0.107118838 cand 0.110190198 |
| combinations/enclosure-emissive-panel-oren-nayar | bitwise | 64x48, 8 spp | 0.0113 / 0.00886 | **beyond practical**: 58 components, strict diffs 516, max abs 0.00248, max rel 0.027, worst (52,12,R) ref 0.0918805599 cand 0.094358325 |
| combinations/enclosure-emissive-panel-translucent | bitwise | 64x48, 8 spp | 0.0068 / 0.00391 | within practical (strict diffs 0, max abs 5.96e-08, max rel 2.87e-07) |
| combinations/glass-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00267 / 0.00112 | within practical (strict diffs 4, max abs 2.68e-07, max rel 1.06e-06) |
| combinations/glass-receiver-transparent-lighting-4 | bitwise | 32x24, 8 spp | 0.00518 / 0.00217 | within practical (strict diffs 9, max abs 1.04e-05, max rel 2.81e-05) |
| combinations/link-ao-node | bitwise | 32x24, 8 spp | 0.00371 / 0.0014 | within practical (strict diffs 14, max abs 2.15e-06, max rel 5.16e-06) |
| combinations/link-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00248 / 0.000893 | within practical (strict diffs 2, max abs 1.79e-07, max rel 3.84e-07) |
| combinations/link-transparent-lighting-4 | bitwise | 32x24, 8 spp | 0.00307 / 0.00132 | within practical (strict diffs 7, max abs 3.73e-07, max rel 7.8e-06) |
| combinations/link-transparent-lighting-5 | bitwise | 32x24, 8 spp | 0.00307 / 0.00151 | within practical (strict diffs 0, max abs 7.08e-08, max rel 1.97e-05) |
| combinations/moving-transparent-fast-gi | bitwise | 32x24, 8 spp | 0.00297 / 0.00106 | no x64 film |
| combinations/principled-ao-node | bitwise | 32x24, 8 spp | 0.00336 / 0.00169 | within practical (strict diffs 156, max abs 3.61e-06, max rel 2.04e-05) |
| combinations/principled-receiver-transparent-lighting-5 | bitwise | 32x24, 8 spp | 0.00357 / 0.00194 | within practical (strict diffs 23, max abs 3.71e-06, max rel 5.16e-05) |
| combinations/replace-transparent-fast-gi | bitwise | 32x24, 8 spp | 0.00302 / 0.00102 | within practical (strict diffs 0, max abs 1.04e-07, max rel 8.35e-07) |
| combinations/terminator-fast-gi-additive | bitwise | 32x24, 8 spp | 0.0027 / 0.00109 | within practical (strict diffs 2, max abs 2.68e-07, max rel 5.48e-07) |
| combinations/tree-ao-node | bitwise | 32x24, 8 spp | 0.00437 / 0.00159 | within practical (strict diffs 44, max abs 1.34e-06, max rel 3.89e-06) |
| combinations/tree-camera-motion | bitwise | 64x48, 16 spp | 0.0101 / 0.0071 | within practical (strict diffs 492, max abs 3.96e-05, max rel 0.000195) |
| combinations/tree-catcher-approximate | bitwise | 64x48, 16 spp | 0.0105 / 0.00761 | within practical (strict diffs 39, max abs 1.24e-05, max rel 5.29e-06) |
| combinations/tree-catcher-point-light | bitwise | 64x48, 16 spp | 0.0127 / 0.0102 | within practical (strict diffs 118, max abs 2.72e-05, max rel 1.02e-05) |
| combinations/tree-deforming-motion-5 | bitwise | 32x24, 8 spp | 0.00403 / 0.00141 | within practical (strict diffs 249, max abs 1.45e-06, max rel 1.38e-05) |
| combinations/tree-fast-gi-additive | bitwise | 32x24, 8 spp | 0.00363 / 0.00116 | within practical (strict diffs 35, max abs 4.77e-07, max rel 9.67e-07) |
| combinations/tree-glass-receiver-transparent-lighting-6 | bitwise | 32x24, 8 spp | 0.00385 / 0.00155 | within practical (strict diffs 110, max abs 7.53e-07, max rel 1.2e-05) |
| combinations/tree-glass-render | bitwise | 64x48, 8 spp | 0.0227 / 0.0186 | **beyond practical**: 26 components, strict diffs 1132, max abs 0.00457, max rel 0.0297, worst (41,20,R) ref 0.153321236 cand 0.148768812 |
| combinations/tree-glass-sphere | bitwise | 64x48, 16 spp | 0.0122 / 0.0118 | **beyond practical**: 9 components, strict diffs 403, max abs 0.00561, max rel 0.0118, worst (59,9,R) ref 0.476515293 cand 0.470904887 |
| combinations/tree-link-transparent-lighting-6 | bitwise | 32x24, 8 spp | 0.00372 / 0.00167 | within practical (strict diffs 74, max abs 7.53e-07, max rel 2.15e-05) |
| combinations/tree-link-transparent-lighting-7 | bitwise | 32x24, 8 spp | 0.00489 / 0.00159 | within practical (strict diffs 103, max abs 7.53e-07, max rel 1.63e-05) |
| combinations/tree-object-motion-4 | bitwise | 32x24, 8 spp | 0.00601 / 0.00232 | within practical (strict diffs 254, max abs 1.82e-06, max rel 7.46) |
| combinations/tree-principled | bitwise | 64x48, 16 spp | 0.0133 / 0.0121 | **beyond practical**: 8 components, strict diffs 212, max abs 0.00214, max rel 0.0146, worst (59,9,R) ref 0.146555722 cand 0.144416198 |
| combinations/tree-principled-receiver-transparent-lighting-7 | bitwise | 32x24, 8 spp | 0.00433 / 0.00204 | within practical (strict diffs 176, max abs 4.69e-06, max rel 6.5e-05) |
| combinations/tree-replace-transparent-fast-gi | bitwise | 32x24, 8 spp | 0.00331 / 0.00125 | within practical (strict diffs 0, max abs 1.04e-07, max rel 8.35e-07) |
| combinations/tree-shadow-terminator | bitwise | 64x48, 16 spp | 0.0113 / 0.00799 | **beyond practical**: 6 components, strict diffs 343, max abs 0.00561, max rel 0.013, worst (59,9,R) ref 0.433232039 cand 0.427621573 |
| combinations/tree-textured-emitter | bitwise | 64x48, 16 spp | 0.0248 / 0.0133 | **beyond practical**: 8 components, strict diffs 418, max abs 0.00551, max rel 0.0101, worst (59,9,B) ref 0.545186043 cand 0.539673746 |
| combinations/tree-textured-plane-normal-map | bitwise | 64x48, 8 spp | 0.0201 / 0.0135 | within practical (strict diffs 885, max abs 7.63e-06, max rel 0.000222) |
| combinations/tree-transparent-lighting-0 | bitwise | 32x24, 8 spp | 0.00333 / 0.00126 | within practical (strict diffs 0, max abs 3.35e-08, max rel 2.7e-06) |
| combinations/tree-transparent-lighting-1 | bitwise | 32x24, 8 spp | 0.00325 / 0.00124 | within practical (strict diffs 2, max abs 2.38e-07, max rel 4.3e-06) |
| combinations/tree-transparent-lighting-2 | bitwise | 32x24, 8 spp | 0.00345 / 0.00144 | within practical (strict diffs 31, max abs 2.68e-06, max rel 2.18e-05) |
| combinations/tree-transparent-lighting-3 | bitwise | 32x24, 8 spp | 0.00313 / 0.00113 | within practical (strict diffs 4, max abs 2.87e-07, max rel 7.46e-05) |
| combinations/tree-transparent-lighting-4 | bitwise | 32x24, 8 spp | 0.00354 / 0.00158 | within practical (strict diffs 133, max abs 8.2e-07, max rel 1.47e-05) |
| combinations/tree-transparent-lighting-5 | bitwise | 32x24, 8 spp | 0.0039 / 0.00207 | within practical (strict diffs 91, max abs 5.14e-07, max rel 1.28e-05) |
| combinations/tree-transparent-lighting-6 | bitwise | 32x24, 8 spp | 0.00315 / 0.00121 | within practical (strict diffs 119, max abs 1.01e-06, max rel 1.41e-05) |
| combinations/tree-transparent-lighting-7 | bitwise | 32x24, 8 spp | 0.00313 / 0.00127 | within practical (strict diffs 192, max abs 1.01e-06, max rel 1.4e-05) |
| combinations/tree-triangle-lighting-1 | bitwise | 32x24, 8 spp | 0.00349 / 0.0015 | within practical (strict diffs 11, max abs 1.48e-06, max rel 1.84e-05) |
| combinations/tree-triangle-lighting-2 | bitwise | 32x24, 8 spp | 0.00363 / 0.00157 | within practical (strict diffs 20, max abs 1.43e-06, max rel 0.000346) |
| combinations/tree-triangle-lighting-3 | bitwise | 32x24, 8 spp | 0.00334 / 0.00149 | within practical (strict diffs 16, max abs 1.64e-06, max rel 5.85e-06) |
| combinations/tree-triangle-lighting-5 | bitwise | 32x24, 8 spp | 0.00315 / 0.00146 | within practical (strict diffs 11, max abs 9.24e-07, max rel 9.41e-06) |
| displacement/both | bitwise | 32x24, 8 spp | 0.00329 / 0.000509 | no x64 film |
| displacement/bump | bitwise | 32x24, 8 spp | 0.0026 / 0.00053 | no x64 film |
| displacement/true-height | bitwise | 32x24, 8 spp | 0.00325 / 0.000535 | no x64 film |
| displacement/true-vector | bitwise | 32x24, 8 spp | 0.00351 / 0.00051 | no x64 film |
| light-motion/camera-deforming-motion | bitwise | 32x24, 8 spp | 0.00323 / 0.00129 | within practical (strict diffs 0, max abs 9.08e-09, max rel 5.35e-06) |
| light-motion/camera-object-motion | bitwise | 32x24, 8 spp | 0.00314 / 0.00131 | within practical (strict diffs 1, max abs 4.47e-07, max rel 3.08) |
| light-motion/clamps-filters-exclude | bitwise | 64x48, 16 spp | 0.0155 / 0.013 | within practical (strict diffs 153, max abs 1.79e-06, max rel 1.75e-05) |
| light-motion/light-cast-shadow | bitwise | 64x48, 16 spp | 0.0146 / 0.0118 | within practical (strict diffs 659, max abs 1.43e-05, max rel 1.41e-05) |
| light-motion/light-cast-shadow-tree | bitwise | 64x48, 16 spp | 0.0148 / 0.0114 | within practical (strict diffs 996, max abs 1.96e-05, max rel 1.82e-05) |
| light-motion/light-max-bounces | bitwise | 64x48, 16 spp | 0.0118 / 0.00906 | within practical (strict diffs 614, max abs 1.43e-05, max rel 1.41e-05) |
| light-motion/light-max-bounces-tree | bitwise | 64x48, 16 spp | 0.0143 / 0.0114 | within practical (strict diffs 868, max abs 1.96e-05, max rel 2.44e-05) |
| light-motion/linking-glass | bitwise | 64x48, 16 spp | 0.00915 / 0.00603 | within practical (strict diffs 377, max abs 1.23e-05, max rel 0.000147) |
| light-motion/linking-hdri | bitwise | 64x48, 16 spp | 0.0107 / 0.00782 | **beyond practical**: 3 components, strict diffs 344, max abs 0.0164, max rel 0.0598, worst (32,19,R) ref 0.273482919 cand 0.257122606 |
| light-motion/linking-motion-flat | bitwise | 64x48, 16 spp | 0.00764 / 0.00589 | within practical (strict diffs 434, max abs 5.72e-06, max rel 9.64e-06) |
| light-motion/linking-motion-tree | bitwise | 64x48, 16 spp | 0.0112 / 0.00819 | within practical (strict diffs 399, max abs 2.8e-06, max rel 2.05e-05) |
| light-motion/linking-transparent | bitwise | 64x48, 16 spp | 0.0121 / 0.00961 | within practical (strict diffs 345, max abs 1.61e-06, max rel 7.75e-06) |
| light-motion/motion-glass-principled-lamps | bitwise | 64x48, 16 spp | 0.0157 / 0.0142 | within practical (strict diffs 644, max abs 9.73e-05, max rel 3.39e-05) |
| light-motion/motion-hdri-emitter | bitwise | 64x48, 16 spp | 0.0149 / 0.0127 | **beyond practical**: 33 components, strict diffs 176, max abs 0.00373, max rel 0.0203, worst (24,9,R) ref 0.0946976766 cand 0.0966200903 |
| light-tree/lamps-and-mesh-lights | bitwise | 64x48, 16 spp | 0.00914 / 0.00757 | **beyond practical**: 9 components, strict diffs 426, max abs 0.00561, max rel 0.0118, worst (59,9,R) ref 0.476515293 cand 0.470904887 |
| light-tree/light-linking | bitwise | 64x48, 16 spp | 0.00875 / 0.00719 | within practical (strict diffs 384, max abs 1.61e-06, max rel 4.34e-06) |
| linux-gates/broad-lighting-0 | bitwise | 32x24, 8 spp | 0.00424 / 0.00108 | within practical (strict diffs 0, max abs 3.35e-08, max rel 2.7e-06) |
| linux-gates/broad-lighting-1 | bitwise | 32x24, 8 spp | 0.00384 / 0.00139 | within practical (strict diffs 4, max abs 1.44e-07, max rel 8.83e-05) |
| linux-gates/broad-lighting-10 | bitwise | 32x24, 8 spp | 0.00308 / 0.00093 | within practical (strict diffs 20, max abs 5.25e-06, max rel 1.99e-06) |
| linux-gates/broad-lighting-11 | bitwise | 32x24, 8 spp | 0.00376 / 0.00107 | within practical (strict diffs 108, max abs 2.5e-06, max rel 2.62e-05) |
| linux-gates/broad-lighting-2 | bitwise | 32x24, 8 spp | 0.00348 / 0.000993 | within practical (strict diffs 4, max abs 4.06e-07, max rel 0.000662) |
| linux-gates/broad-lighting-3 | bitwise | 32x24, 8 spp | 0.004 / 0.00113 | within practical (strict diffs 0, max abs 6.66e-08, max rel 0.0031) |
| linux-gates/broad-lighting-4 | bitwise | 32x24, 8 spp | 0.00313 / 0.00116 | within practical (strict diffs 55, max abs 1.61e-06, max rel 9.06e-06) |
| linux-gates/broad-lighting-5 | bitwise | 32x24, 8 spp | 0.00297 / 0.00101 | within practical (strict diffs 14, max abs 1.71e-06, max rel 1.73e-05) |
| linux-gates/broad-lighting-6 | bitwise | 32x24, 8 spp | 0.00301 / 0.001 | within practical (strict diffs 14, max abs 8.2e-07, max rel 1.03e-05) |
| linux-gates/broad-lighting-7 | bitwise | 32x24, 8 spp | 0.00293 / 0.000927 | within practical (strict diffs 64, max abs 1.43e-05, max rel 4.35e-05) |
| linux-gates/broad-lighting-8 | bitwise | 32x24, 8 spp | 0.00307 / 0.000944 | within practical (strict diffs 70, max abs 1.74e-05, max rel 5.49e-05) |
| linux-gates/broad-lighting-9 | bitwise | 32x24, 8 spp | 0.0027 / 0.000895 | within practical (strict diffs 95, max abs 1.01e-05, max rel 0.00216) |
| linux-gates/closed-volumes | bitwise | 64x48, 8 spp | 0.0177 / 0.0148 | within practical (strict diffs 480, max abs 7.63e-06, max rel 0.00476) |
| linux-gates/deforming-motion-0 | bitwise | 32x24, 8 spp | 0.00265 / 0.000903 | within practical (strict diffs 0, max abs 5.22e-08, max rel 2.7e-06) |
| linux-gates/deforming-motion-1 | bitwise | 32x24, 8 spp | 0.0031 / 0.00111 | within practical (strict diffs 3, max abs 3.95e-07, max rel 1.84e-05) |
| linux-gates/deforming-motion-2 | bitwise | 32x24, 8 spp | 0.00285 / 0.000948 | within practical (strict diffs 5, max abs 1.49e-07, max rel 3.69e-06) |
| linux-gates/deforming-motion-3 | bitwise | 32x24, 8 spp | 0.00267 / 0.001 | within practical (strict diffs 0, max abs 9.2e-09, max rel 5.5e-06) |
| linux-gates/deforming-motion-4 | bitwise | 32x24, 8 spp | 0.00343 / 0.00109 | within practical (strict diffs 28, max abs 5.66e-07, max rel 1.35e-05) |
| linux-gates/deforming-motion-5 | bitwise | 32x24, 8 spp | 0.00363 / 0.00116 | within practical (strict diffs 16, max abs 4.02e-07, max rel 6.05e-05) |
| linux-gates/deforming-motion-6 | bitwise | 32x24, 8 spp | 0.00267 / 0.00102 | within practical (strict diffs 28, max abs 5.66e-07, max rel 8.15e-06) |
| linux-gates/deforming-motion-7 | bitwise | 32x24, 8 spp | 0.0033 / 0.00162 | within practical (strict diffs 22, max abs 3.73e-07, max rel 6.88e-06) |
| linux-gates/glass-render | bitwise | 64x48, 8 spp | 0.0101 / 0.00765 | within practical (strict diffs 1, max abs 2.31e-07, max rel 1.98e-06) |
| linux-gates/object-motion-0 | bitwise | 32x24, 8 spp | 0.0026 / 0.000942 | within practical (strict diffs 1, max abs 2.91e-07, max rel 0.287) |
| linux-gates/object-motion-1 | bitwise | 32x24, 8 spp | 0.00318 / 0.00107 | within practical (strict diffs 3, max abs 3.8e-07, max rel 2.12) |
| linux-gates/object-motion-2 | bitwise | 32x24, 8 spp | 0.00267 / 0.00102 | within practical (strict diffs 5, max abs 4.54e-07, max rel 5.52) |
| linux-gates/object-motion-3 | bitwise | 32x24, 8 spp | 0.00312 / 0.001 | within practical (strict diffs 0, max abs 9.2e-09, max rel 4.33) |
| linux-gates/object-motion-4 | bitwise | 32x24, 8 spp | 0.00299 / 0.0011 | within practical (strict diffs 24, max abs 3.73e-07, max rel 2.44) |
| linux-gates/object-motion-5 | bitwise | 32x24, 8 spp | 0.0029 / 0.00106 | within practical (strict diffs 10, max abs 3.2e-07, max rel 3.3) |
| linux-gates/object-motion-6 | bitwise | 32x24, 8 spp | 0.00273 / 0.001 | within practical (strict diffs 29, max abs 3.87e-07, max rel 2.99) |
| linux-gates/object-motion-7 | bitwise | 32x24, 8 spp | 0.00275 / 0.0011 | within practical (strict diffs 20, max abs 4.17e-07, max rel 2.18) |
| linux-gates/textured-plane | bitwise | 64x48, 8 spp | 0.00956 / 0.0079 | within practical (strict diffs 440, max abs 7.63e-06, max rel 0.000946) |
| linux-gates/transparent-lighting-0 | bitwise | 32x24, 8 spp | 0.00432 / 0.00147 | within practical (strict diffs 0, max abs 3.35e-08, max rel 2.7e-06) |
| linux-gates/transparent-lighting-1 | bitwise | 32x24, 8 spp | 0.0119 / 0.00317 | within practical (strict diffs 2, max abs 2.38e-07, max rel 4.3e-06) |
| linux-gates/transparent-lighting-2 | bitwise | 32x24, 8 spp | 0.00445 / 0.00878 | within practical (strict diffs 31, max abs 2.68e-06, max rel 2.18e-05) |
| linux-gates/transparent-lighting-3 | bitwise | 32x24, 8 spp | 0.00519 / 0.00115 | within practical (strict diffs 4, max abs 2.87e-07, max rel 7.46e-05) |
| linux-gates/transparent-lighting-4 | bitwise | 32x24, 8 spp | 0.00526 / 0.00226 | within practical (strict diffs 9, max abs 3.73e-07, max rel 7.36e-06) |
| linux-gates/transparent-lighting-5 | bitwise | 32x24, 8 spp | 0.00368 / 0.00189 | within practical (strict diffs 0, max abs 7.45e-08, max rel 1.97e-05) |
| linux-gates/transparent-lighting-6 | bitwise | 32x24, 8 spp | 0.00314 / 0.00138 | within practical (strict diffs 4, max abs 3.73e-07, max rel 7.97e-06) |
| linux-gates/transparent-lighting-7 | bitwise | 32x24, 8 spp | 0.00473 / 0.00195 | within practical (strict diffs 12, max abs 3.73e-07, max rel 1.01e-05) |
| linux-gates/triangle-lighting-0 | bitwise | 32x24, 8 spp | 0.00396 / 0.000796 | within practical (strict diffs 2, max abs 1.94e-07, max rel 2.08e-06) |
| linux-gates/triangle-lighting-1 | bitwise | 32x24, 8 spp | 0.00307 / 0.00116 | within practical (strict diffs 12, max abs 1.35e-06, max rel 1.85e-05) |
| linux-gates/triangle-lighting-2 | bitwise | 32x24, 8 spp | 0.0032 / 0.0012 | within practical (strict diffs 30, max abs 2.82e-06, max rel 6.93e-05) |
| linux-gates/triangle-lighting-3 | bitwise | 32x24, 8 spp | 0.00298 / 0.0011 | within practical (strict diffs 17, max abs 1.1e-06, max rel 4.51e-06) |
| linux-gates/triangle-lighting-5 | bitwise | 32x24, 8 spp | 0.00328 / 0.00127 | within practical (strict diffs 20, max abs 1.5e-06, max rel 1.85e-05) |
| linux-gates/world-volume | bitwise | 64x48, 8 spp | 0.0205 / 0.0146 | **beyond practical**: 87 components, strict diffs 495, max abs 1.36, max rel 84.9, worst (38,15,B) ref 0 cand 1.3629173 |
| mnee/glass-sphere-caustic | bitwise | 32x24, 8 spp | 0.00563 / 0.00317 | within practical (strict diffs 15, max abs 5.07e-07, max rel 2.43e-06) |
| mnee/glass-sphere-deforming | bitwise | 32x24, 8 spp | 0.00485 / 0.00289 | no x64 film |
| mnee/glass-sphere-fast-gi | bitwise | 32x24, 8 spp | 0.00503 / 0.00303 | no x64 film |
| mnee/glass-sphere-object-motion | bitwise | 32x24, 8 spp | 0.00573 / 0.0039 | no x64 film |
| shadow-catcher/catcher-approximate | bitwise | 64x48, 16 spp | 0.00807 / 0.00608 | within practical (strict diffs 19, max abs 1.24e-05, max rel 5.29e-06) |
| shadow-catcher/catcher-point-light | bitwise | 64x48, 16 spp | 0.00872 / 0.0067 | within practical (strict diffs 32, max abs 2.72e-05, max rel 1.04e-05) |
| starter/diffuse-triangle | bitwise | 32x24, 8 spp | 0.00204 / 0.000394 | within practical (strict diffs 0, max abs 2.98e-08, max rel 2.22e-07) |
| starter/glass-and-area-light | bitwise | 64x48, 16 spp | 0.00631 / 0.00493 | within practical (strict diffs 63, max abs 1.88e-06, max rel 4.04e-06) |
| starter/glossy-sphere-point-light | bitwise | 64x48, 16 spp | 0.00677 / 0.00471 | within practical (strict diffs 156, max abs 6.34e-05, max rel 9.09e-06) |
| starter/textured-plane | bitwise | 48x32, 8 spp | 0.00314 / 0.000971 | within practical (strict diffs 67, max abs 2.91e-07, max rel 2.59e-06) |
| surface-nodes/bevel | bitwise | 32x24, 8 spp | 0.00354 / 0.00146 | no x64 film |
| surface-nodes/radial-tiling | bitwise | 32x24, 8 spp | 0.00297 / 0.00105 | no x64 film |
| surface-nodes/raycast | bitwise | 32x24, 8 spp | 0.00324 / 0.0011 | no x64 film |
