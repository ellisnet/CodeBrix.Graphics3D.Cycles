Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

# Linux ARM64 CPU oracle corpus at commit 24b74e095, Debian arm64 container on an Apple Silicon Mac (2026-11-15)

**This folder is the ARM64 gold standard for commit `24b74e095f5f1f4526e095e7ac4ad6dfff62bb4c`**
(docs/TESTING-LINUX-ARM64-ORACLE.md section 1) at the pinned Cycles v5.3.0 (revision
`b3f6262640f1a2f2d10b55b6ffccbf310ce0f981`). Android ARM64 WGSL films of that commit in the product default arithmetic
(`wgslArithmetic` `arm64-scalar`) are judged against these films (native/DEVICE.md, "Validation reference policy",
Option A). It is the re-render of section 9 of that document at the closing commit of docs/KIT-MACOS-ARM64.md: the host
work of the kit's Part D changed host scene-preparation code and added scene documents since
`linux-arm64-cpu-oracle-20261113` (commit `0b19d8750`). Backend `cpu-arm64-linux` of `native/oracle/render_oracles.py`,
the unmodified pinned Cycles CPU kernels at the fixed ARM64 oracle baseline (ARMv8-A base level, `-ffp-contract=off`,
`-fno-reciprocal-math`), GCC 14 and glibc 2.41, in a `linux/arm64` Debian 13 container running natively (no emulation)
under Docker Desktop on a Mac mini M2 Pro. Facts first; lines marked **INFERENCE** are conclusions that were not measured
directly. Absolute host paths are written as `<repo>`; copied tool output keeps the container paths
(`/work/CodeBrix.Graphics3D.Cycles` is `<repo>` mounted in the container).

| Checklist item (section 9) | Result |
|---|---|
| 1. Commit, clean tree | `24b74e095f5f1f4526e095e7ac4ad6dfff62bb4c` (branch `cycles-with-metal-v5.3.0`); `git status` empty before the build (`environment/step0.txt`), before the render and after it (`environment/step5-status.txt`); all 793 `meta.json` record `repoCommit` `24b74e09...` and `repoWorkingTreeClean: true`; `run.json` `common.cyclesRevision` is `b3f62626...` |
| 2. Whole committed corpus plus the bench bundle | **793 scenes rendered** = 791 committed scene documents (`find native/oracle/scenes -name "*.json" ! -name manifest.json ! -path "*/assets/*" \| wc -l`; every one of them is in `run.json`) + 2 bench bundle documents. **Refused: none. Failed: none** (`run.json` has no `failures`) |
| 3. Repeat identity | `repeatBitwiseIdentical` **true for all 793** scenes. **Non-identical: none.** One render pass (not the film) differs between the two renders by design: the `RenderTime` pass of `passes/data-passes` (below) |
| 4. Compared with | **SKIPPED.** No `native/evidence/linux-x64-reference-<date>/` folder of Cycles v5.3.0 is committed (the newest, `linux-x64-reference-20261107`, is of the previous pin), and a comparison with another Cycles revision is not made (section 6). The comparison is made later on the Linux x64 machine. There is no comparison JSON and no `vs-x64/` folder |
| 5. Layout | section 7: `oracle/`, `run.json`, this README; plus the run records (`environment/`), the comparison with the 20261113 folder (`environment/comparison-20261113.json`) and `SHA256SUMS.txt`. Earlier `linux-arm64-cpu-oracle-*` folders are untouched |

Warnings recorded by the runner (`meta.json` `warnings`), verbatim, in 3 scenes; the films rendered and are
repeat-identical. They are the warnings of `linux-arm64-cpu-oracle-20261113`, with the same numbers:

| Scene | Warning |
|---|---|
| `linux-gates/closed-volumes` | `00:00.00 \| WARNING: Maximum number of closures exceeded: 66 > 64` |
| `volumes/bsdf-in-volume` | `00:00.00 \| WARNING: Maximum number of closures exceeded: 77 > 64` |
| `volumes/mixed-phase-world` | `00:00.00 \| WARNING: Maximum number of closures exceeded: 96 > 64` |

## Comparison with `linux-arm64-cpu-oracle-20261113`

`environment/comparison-20261113.json` (by `filmSha256` and the passes' `filmSha256` of each `meta.json`, after checking
every film and pass file against its own `meta.json`):

- All **726 scenes of the 20261113 folder** are rendered here, and all are **bitwise identical** to it: 1,452 of 1,452
  films (two renders each) and 2,870 of 2,870 pass films. The `RenderTime` pass of `passes/data-passes` (pass 8, a
  wall-clock measurement) is not compared; it differs between any two renders.
- No scene of the 20261113 folder is missing here.
- **67 scenes are new**, the scene documents added by the kit's Parts C and D since commit `0b19d8750`. By family:
  attributes 6, bake 24, bsdf 1, gsplat 11, images 6, lamps 5, points 2, render-settings 4, surface-nodes 5, world 3:
  - `attributes/`: `quaternion-and-spherical-harmonics`, `quaternion-curves`, `quaternion-mesh`, `quaternion-points`,
    `spherical-harmonics-mesh`, `spherical-harmonics-points`;
  - `bake/`: `subdivision-combined`, `subdivision-diffuse-color`, `subdivision-displaced-combined`,
    `subdivision-displaced-normal`, `subdivision-displaced-position`, `subdivision-normal`, `subdivision-position`,
    `zero-area-ao`, `zero-area-aov-color`, `zero-area-aov-value`, `zero-area-combined`, `zero-area-depth`,
    `zero-area-diffuse-color`, `zero-area-diffuse-indirect`, `zero-area-emission`, `zero-area-glossy-color`,
    `zero-area-glossy-indirect`, `zero-area-material-id`, `zero-area-mist`, `zero-area-object-id`, `zero-area-roughness`,
    `zero-area-transmission-color`, `zero-area-transmission-indirect`, `zero-area-uv`;
  - `bsdf/glass-multiscatter`;
  - `gsplat/`: `band0`, `beside-meshes`, `beside-volume`, `caller-shader`, `deform-3`, `instanced`, `object-motion`,
    `single-user-transform`, `spherical-harmonics`, `transparent-shadows`, `with-points`;
  - `images/`: `texture-limit-blocks`, `texture-limit-blocks-halved`, `texture-limit-checker`, `udim-checker-linear`,
    `udim-missing-tile`, `udim-three-tiles`;
  - `lamps/`: `area-degenerate-ellipse-spread-zero`, `area-degenerate-spread-tiny`, `area-degenerate-spread-tiny-volume`,
    `area-degenerate-spread-zero`, `area-degenerate-tree-transmissive`;
  - `points/`: `geometry-tangent`, `geometry-tangent-bump`;
  - `render-settings/`: `tiles-border-denoise`, `tiles-border-denoise-untiled`, `tiles-glass-passes`,
    `tiles-glass-passes-untiled`;
  - `surface-nodes/`: `boolean-math`, `get-vector-component`, `get-vector-component-bump`, `image-missing`, `integer-math`;
  - `world/`: `portal-underflow`, `portal-zero`, `portal-zero-u`.
- The new scenes are the 67 that the macOS closing run of the same commit adds to its base run
  (`native/evidence/cycles-oracles-macos-20261115/`), the same list.

The films of the earlier `linux-arm64-cpu-oracle-20261031`, `-20261101` and `-20261105` folders belong to the previous
Cycles revision; they are not compared with these (kit rule 8).

## Environment (`environment/`)

| Item | Value |
|---|---|
| Mac | Mac mini (Mac14,12), Apple M2 Pro, 16 GB; macOS 27.2.1 (26B131) (`environment/step1-3.txt`) |
| Image | `debian:trixie`, `debian@sha256:9c6e381421d0f48d1b8133e573f33c185ee16cbf505eda9c4a8a4791b25ea8a6` (arm64), unchanged since `linux-arm64-cpu-oracle-20261113` (`environment/step1-3.txt`) |
| Container | `cbc-arm64-oracle-20261113b`, the container of the 20261113 run, still running; every step ran with `docker exec` |
| Toolchain | as in 20261113: GCC 14.2.0-19, Debian clang 19.1.7 (3+b1), glibc 2.41-12+deb13u4, CMake 3.31.6, Python 3.13.5, `uname -m` `aarch64` (`environment/step1-3.txt`) |
| ISPC | 1.30.0 for Linux aarch64, archive `ispc-v1.30.0-linux.aarch64.tar.gz` SHA-256 `509399c399ec162d746889458a10cc13797a1aed1c0164b2bd3faddf7d023f13`, `bin/ispc` SHA-256 `1079bf9e0734777e108fd55e195365df0ebb200c65f532078bba751ad9c4a3de`, version line `Intel(r) Implicit SPMD Program Compiler (Intel(r) ISPC), 1.30.0 (build commit 3fc6d50cf24dc8b4 @ 20260204, LLVM 21.1.8)`. Build-time only |
| WebGPU runtime | CodeBrix.WebGpu.Native.MitLicenseForever 1.0.300.210 from the NuGet cache (mounted read-only), `runtimes/linux-arm64/native/libcodebrix_wgpu.so` (5,462,960 bytes); the host library links it, the CPU oracle does not render with it |
| Device model | `meta.json` `deviceModel` says there is no device tree in a container; the machine is the Mac above |

## Build (inside the container)

`environment/build.sh` (driver log `environment/build-driver.output.txt`):

```sh
export CODEBRIX_ISPC=/work/CodeBrix.Graphics3D.Cycles/native/work/ispc/extracted/ispc-v1.30.0-linux.aarch64/bin/ispc
python3 tools/dependencies/build_linux_arm64.py --jobs 8                  # exit 0, 54 s wall
python3 native/tools/build_cycles_linux_arm64.py --jobs 8 --device-set   # exit 0, 2,564 s wall
```

- Started 2026-11-15 20:36:17 UTC, from an empty `native/work/linux/linux-arm64/host/` (the 20261113 tree was moved aside
  to `host-0b19d875`, `environment/step0.txt`). The dependency recipe reused the prefix: its identity
  (`environment/build/dependencies-status.json`) equals that of the 20261113 run. The Cycles build's wall is inflated:
  the macOS closing build, host gate, managed tests and three-backend corpus render ran on the same Mac at the same time.
- `environment/build/host-status.json`: `passed: true`, `complete: true`. **0 compiler warnings** in the host build log
  (`environment/build/host-build.output.txt`, no `warning:` line). The host's configure log (not copied) has the CMake
  developer warning of 20261113 (`Policy CMP0167 is not set`, pinned `src/cmake/external_libs.cmake:443`).
- The CPU kernel compile line (`environment/cpu-kernel-compile.txt`) is the line of 20261113, unchanged: Cycles'
  `-ffp-contract=on -freciprocal-math`, then `-O3 -DNDEBUG -std=c++20 -fPIC -ffp-contract=off -fno-reciprocal-math`, and
  no `-march`/`-mcpu`; `run.json` `cpuKernelCompile` records `fpContract` `off` and `reciprocalMath` `false`.
- The overlay's translation is unchanged (`environment/hashes.txt`): `translated.wgsl`
  `b047c969cf28bec4d7c07a34e1334282da69484770490e64cbb8ad5cdf921105`, `translation.json`
  `da63a194c70cf93a11eec5f5bd52b5f983ee5321a796ce2b16f1667874e49c94`, cache fingerprint `51fc0628...`, as in 20261113.

| Binary (not copied here; `environment/binaries.sha256`) | SHA-256 |
|---|---|
| `native/work/linux/linux-arm64/host/build/bin/codebrix_cycles_oracle` (runner) | `426c1c9eeb9df5f102ca7c00f47017ba8d782967b045da8b20bd372ce76038db` |
| `native/work/linux/linux-arm64/host/build/lib/libcodebrix_cycles_host.so` | `e744ce15338739e8e54edd37492aff0e571ed4fcfb94f54093d9a8f26104f67b` |

Both equal `runnerSha256` and `hostLibrarySha256` in `run.json` and every `meta.json`.

## Render (inside the container)

`environment/render.sh`: the scene list of section 5 (every `*.json` under `native/oracle/scenes` except `manifest.json`
files and `assets/`, plus the bench bundle's two documents from `native/work/oracle-assets/bench_01/`), with `RUN`,
`RUNNER` and `HOSTLIB` exported for the inner `bash -c`:

```sh
python3 native/oracle/render_oracles.py --output native/work/oracles/linux-arm64-20261115 \
  --runner "$RUNNER" --host-library "$HOSTLIB" --backend cpu-arm64-linux "${scenes[@]}"
```

Started 2026-11-15 21:25:06 UTC, wall 290 s for 793 scenes (default 4 threads), exit 0 (`environment/render.output.txt`,
driver log `environment/render-driver.output.txt`). `run.json` has 793 scenes, one backend and no `failures`. With one
backend `render_oracles.py` compares nothing inside the run: `comparisonsPassed` is `null` for every scene. Every film
and pass file matches the SHA-256 its `meta.json` records.

Scenes per family: ao 2, assets 2, attributes 6, bake 60, bsdf 12, cameras 26, combinations 84, curves 28, curves-b 49,
denoise 6, displacement 5, gsplat 11, hair 63, images 6, lamps 22, light-motion 18, light-tree 2, linux-gates 45,
mnee 20, passes 61, points 59, render-settings 4, sampling 21, shadow-catcher 3, shadow-linking 14, sky 10, starter 4,
subdivision 2, subsurface 30, surface-nodes 22, volumes 76, world 20.

**Two failed invocation attempts before that render** (invocation errors, no product failure; records kept):

1. 2026-11-15 21:20:27 UTC, exit 1 after 163 s (`environment/render-attempt1-unexported-vars.output.txt`,
   `render-driver-attempt1.output.txt`). The variables of the documented command were set in the outer shell but not
   exported into the inner `bash -c`, so the runner received empty `--runner` and `--host-library` paths and stopped
   before rendering anything with
   `IsADirectoryError: [Errno 21] Is a directory: '.'` while hashing its `--host-library`. It had already created the
   run folder.
2. 21:24:45 UTC, exit 1 after 1 s (`environment/render-attempt2-notempty.output.txt`,
   `render-driver-attempt2.output.txt`): `.../native/work/oracles/linux-arm64-20261115 is not empty; use a new run
   directory`. The folder held only the empty `oracle/` directory of attempt 1. It was removed, and the third
   invocation, with the variables exported (`environment/render.sh`), is the run of this folder.

The gap in the documented command (section 5 of docs/TESTING-LINUX-ARM64-ORACLE.md set the variables without exporting
them) is corrected in that document.

## Section 10 and the probe

Not repeated. The section 10 probe (`(int)roundf` saturation, the NaN bits of a point hit at its own centre) was not
compiled and run again: the pinned CPU kernels, their compile line and the toolchain are those of
`linux-arm64-cpu-oracle-20261113`, whose `probe/` and README hold the results. Section 10's scene observations hold here
too: `bsdf/principled-dispersion`, `volumes/phase-mie`, `volumes/mixed-phase-world` and
`volumes/heterogeneous-world-sun-tail` rendered, are repeat-identical, and are bitwise identical to their 20261113
films. In `passes/data-passes` only pass 8, `RenderTime`, differs between the two renders (`pass-8-0.f32` `af0264a3...`,
`pass-8-1.f32` `c17d5b17...`).

## Contents

- `oracle/<scene>/cpu-arm64-linux/`: `film-0.f32`, `film-1.f32` (scene-linear RGBA float32, little-endian, lower-left
  origin), `pass-<index>-<repeat>.f32` where the scene writes render passes, and `meta.json`;
  `oracle/<scene>/cpu-arm64-linux.output.txt` holds the runner's output. Copied unchanged from
  `native/work/oracles/linux-arm64-20261115/`.
- `run.json`: the render record, copied unchanged.
- `environment/`: the run's step notes (`steps.md`), the commit and working-tree records (`step0.txt`,
  `step5-status.txt`), the image, Mac, tool and ISPC records (`step1-3.txt`), the build and render scripts, the build
  and render logs (the third render and the two failed attempts), the overlay hashes, the runner and host paths, the CPU
  kernel compile line, the SHA-256 of the binaries, the comparison with the 20261113 folder, and `build/` (the host and
  dependency build records and the host's build log).
- `SHA256SUMS.txt`: every other file of this folder (`shasum -a 256 -c SHA256SUMS.txt`).

Renames: the repository ignores `*.log`, so every `*.log` of the run's records is copied as `*.output.txt`, and the
host's `build.log` as `environment/build/host-build.output.txt`. The copyright and SPDX lines were added at the top of
the copied scripts (`*.sh`) and step notes (`steps.md`); nothing else in a copied file was changed. Built executables and
libraries are not evidence and are not copied here.
