Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

# Linux ARM64 CPU oracle corpus at commit 0b19d8750, Debian arm64 container on an Apple Silicon Mac (2026-11-13)

**This folder is the ARM64 gold standard for commit `0b19d87507ac24f1e55ba6a50d1ac6b05c8d417c`**
(docs/TESTING-LINUX-ARM64-ORACLE.md section 1), the first Linux ARM64 CPU oracle at the pinned Cycles v5.3.0
(revision `b3f6262640f1a2f2d10b55b6ffccbf310ce0f981`). Android ARM64 WGSL films of that commit in the product default
arithmetic (`wgslArithmetic` `arm64-scalar`) are judged against these films (native/DEVICE.md, "Validation reference
policy", Option A). It is Part B of docs/KIT-MACOS-ARM64.md: backend `cpu-arm64-linux` of `native/oracle/render_oracles.py`,
the unmodified pinned Cycles CPU kernels at the fixed ARM64 oracle baseline (ARMv8-A base level, `-ffp-contract=off`,
`-fno-reciprocal-math`), GCC 14 and glibc 2.41, in a `linux/arm64` Debian 13 container running natively (no emulation)
under Docker Desktop on a Mac mini M2 Pro. Facts first; lines marked **INFERENCE** are conclusions that were not measured
directly. Absolute host paths are written as `<repo>` and `<home>`; copied tool output keeps the container paths
(`/work/CodeBrix.Graphics3D.Cycles` is `<repo>` mounted in the container).

| Checklist item (section 9) | Result |
|---|---|
| 1. Commit, clean tree | `0b19d87507ac24f1e55ba6a50d1ac6b05c8d417c` (branch `cycles-with-metal-v5.3.0`); `git status` empty before the build (`environment/step0.txt`), before the render (`environment/gitstatus-before-render.txt`) and after it (`environment/gitstatus-final.txt`); all 726 `meta.json` record `repoCommit` `0b19d875...` and `repoWorkingTreeClean: true`; `run.json` `common.cyclesRevision` is `b3f62626...` |
| 2. Whole committed corpus plus the bench bundle | **726 scenes rendered** = 724 committed scene documents (`find native/oracle/scenes -name "*.json" ! -name manifest.json ! -path "*/assets/*" \| wc -l`; every one of them is in `run.json`) + 2 bench bundle documents. **Refused: none. Failed: none** (`run.json` has no `failures`) |
| 3. Repeat identity | `repeatBitwiseIdentical` **true for all 726** scenes. **Non-identical: none.** One render pass (not the film) differs between the two renders by design: section 10, item 5 |
| 4. Compared with | **SKIPPED.** No `native/evidence/linux-x64-reference-<date>/` folder of Cycles v5.3.0 is committed (the newest, `linux-x64-reference-20261107`, is of the previous pin), and a comparison with another Cycles revision is not made (section 6). The comparison is made later on the Linux x64 machine. There is no comparison JSON and no `vs-x64/` folder |
| 5. Layout | section 7: `oracle/`, `run.json`, this README; plus the section 10 probe (`probe/`), the run records (`environment/`), the failed first attempt (`first-attempt/`) and `SHA256SUMS.txt`. Earlier `linux-arm64-cpu-oracle-*` folders are untouched |

Warnings recorded by the runner (`meta.json` `warnings`), verbatim, in 3 scenes; the films rendered and are
repeat-identical. The same three warnings, with the same numbers, are in the macOS CPU-arm64 films of
`native/evidence/cycles-oracles-macos-20261113/` (information):

| Scene | Warning |
|---|---|
| `linux-gates/closed-volumes` | `00:00.00 \| WARNING: Maximum number of closures exceeded: 66 > 64` |
| `volumes/bsdf-in-volume` | `00:00.00 \| WARNING: Maximum number of closures exceeded: 77 > 64` |
| `volumes/mixed-phase-world` | `00:00.00 \| WARNING: Maximum number of closures exceeded: 96 > 64` |

The films of the earlier `linux-arm64-cpu-oracle-*` folders belong to the previous Cycles revision; they are not compared
with these (kit rule 8).

## Why this commit

The kit runs Part B at its base commit `828f6c2f9ee2c57dd379d472a0252edaf3caa87a`. At that commit the Linux ARM64 build
stops in its overlay step (first attempt, below). The maintainer approved the translator fix and ruled that Part B builds
and renders at the commit that contains it. `0b19d8750` is the base commit plus, and only plus
(`git diff --stat 828f6c2f9 0b19d8750`, 22,047 files):

- the Part A evidence: `native/evidence/device-macos-arm64-20261113/` (3,976 files) and
  `native/evidence/cycles-oracles-macos-20261113/` (18,064 files);
- Part A's re-recorded Metal reference films of the managed tests,
  `tests/CodeBrix.Graphics3D.Cycles.Tests/References/metal-osx-arm64/` (5 films and `manifest.json`);
- the approved translator fix, `research/translate.py` (3 lines added, 1 removed): `-U__ARM_NEON` in the clang AST
  command.

These changes cannot affect a CPU film. Evidence folders and the managed test references are read by no build and not by
the oracle runner. `research/translate.py` only produces the WGSL translation of 23 pinned functions
(`translated.wgsl`, `translation.json`), which `native/tools/build_cycles.py` turns into the WebGPU device's shader text.
The CPU kernels do not use it. Its output here, `b047c969...` and `da63a194...` (`environment/hashes.txt`), is
byte-identical to the committed Linux x64 records of the translation (made at the previous pin, for example
`native/evidence/linux-x64-reference-20261101/reference/linux-overlay-hashes.json`; the 23 translated functions read the
same at both pins). The pinned source and the build recipes are the same at both commits. That this commit's CPU films
equal the base commit's follows from the list above. It was not measured, because the base commit does not build on this
host.

## Environment (`environment/`)

| Item | Value |
|---|---|
| Mac | Mac mini (Mac14,12), Apple M2 Pro, 16 GB; macOS 27.2.1 (26B131) (`environment/step1-3.txt`, `first-attempt/01-mac.txt`) |
| Docker | Docker Desktop, engine 29.9.3 (read after the render), LinuxKit kernel 7.0.14 (`linux/arm64`; `run.json` `common.machine.os`), VM with 10 CPUs and 8,319,438,848 bytes; `uname -m` `aarch64` (`environment/step2-3-versions.txt`, `environment/docker.txt`) |
| Image | `debian:trixie`, `debian@sha256:9c6e381421d0f48d1b8133e573f33c185ee16cbf505eda9c4a8a4791b25ea8a6` (created 2026-11-04T21:23:39Z, arm64; Debian 13.7), the image of `linux-arm64-cpu-oracle-20261105` |
| Container | `cbc-arm64-oracle-20261113b`, created 2026-11-14T06:58:24Z, `sleep infinity`, `<repo>` bind-mounted read-write at `/work/CodeBrix.Graphics3D.Cycles`, `<home>/.nuget/packages` read-only at `/root/.nuget/packages`; every step ran with `docker exec` (`environment/docker.txt`) |
| Packages | section 2: `gcc-14 g++-14 gcc g++ clang cmake make time python3 git ca-certificates file` (114 newly installed; `environment/step2-apt.txt`) |
| Toolchain | GCC 14.2.0-19 (`gcc-14 (Debian 14.2.0-19) 14.2.0`; `c++` is GCC, as every `meta.json` records); Debian clang 19.1.7 (3+b1) (the translator's AST dump); glibc 2.41-12+deb13u4; CMake 3.31.6; Python 3.13.5 (`environment/step2-3-versions.txt`); git 2.47.3 and GNU Make 4.4.1 (read after the render, `environment/docker.txt`) |
| ISPC | 1.30.0 for Linux aarch64, the official release archive `ispc-v1.30.0-linux.aarch64.tar.gz` from github.com/ispc/ispc/releases, SHA-256 `509399c399ec162d746889458a10cc13797a1aed1c0164b2bd3faddf7d023f13` (as published), unpacked in the ignored `native/work/ispc/extracted/` and named with `CODEBRIX_ISPC`; `bin/ispc` SHA-256 `1079bf9e0734777e108fd55e195365df0ebb200c65f532078bba751ad9c4a3de`; version line `Intel(r) Implicit SPMD Program Compiler (Intel(r) ISPC), 1.30.0 (build commit 3fc6d50cf24dc8b4 @ 20260204, LLVM 21.1.8)`. Build-time only |
| WebGPU runtime | CodeBrix.WebGpu.Native.MitLicenseForever 1.0.300.210 from the NuGet cache (mounted read-only), `runtimes/linux-arm64/native/libcodebrix_wgpu.so` (5,462,960 bytes); the host library links it, the CPU oracle does not render with it |
| Device model | `meta.json` `deviceModel` is `unavailable: no /proc/device-tree/model (no device tree, for example in a container)`; the machine is the Mac above |

## Build (inside the container)

```sh
git config --global --add safe.directory /work/CodeBrix.Graphics3D.Cycles
export CODEBRIX_ISPC=/work/CodeBrix.Graphics3D.Cycles/native/work/ispc/extracted/ispc-v1.30.0-linux.aarch64/bin/ispc
python3 tools/dependencies/build_linux_arm64.py --jobs 8                  # exit 0, 40 s wall
python3 native/tools/build_cycles_linux_arm64.py --jobs 8 --device-set   # exit 0, 1,764 s wall
```

(`environment/build1.sh`, `build2.sh`, logs `build-deps.output.txt`, `build-cycles.output.txt`; build records in
`environment/build/`: `host-status.json` with every artifact's SHA-256 and the CPU kernel flags, the dependency build's
`dependencies-status.json` and `dependencies-provenance.json`, and the host's build log.)

- Dependencies, 2026-11-14 06:59:08 UTC: the recipe reuses only a complete prefix whose source and tool inputs match.
  It reused the prefix that the first attempt had built in the same image in 424 s (`first-attempt/04a-deps.txt`). No
  dependency input differs between the two commits.
- Cycles, 06:59:53 UTC: from an empty `native/work/linux/linux-arm64/host/` (the first attempt's tree was moved aside to
  `host-failed-20261113`, `environment/step0.txt`). `host-status.json`: `passed: true`, `complete: true`.
- 0 compiler warnings in the host build log (`environment/build/host-build.output.txt`, no `warning:` line). The host's
  configure log (`native/work/linux/linux-arm64/host/configure.log`, not copied) has one CMake developer warning
  (`Policy CMP0167 is not set: The FindBoost module is removed.`, at the pinned `src/cmake/external_libs.cmake:443`).
- The CPU kernel compile line (`environment/cpu-kernel-compile.txt`, the `compile_commands.json` entry of
  `src/kernel/device/cpu/kernel.cpp`) carries Cycles' `-ffp-contract=on -freciprocal-math`, then
  `-O3 -DNDEBUG -std=c++20 -fPIC -ffp-contract=off -fno-reciprocal-math`, and no `-march`/`-mcpu`. The last options
  decide: `run.json` `cpuKernelCompile` records `fpContract` `off` and `reciprocalMath` `false`.
- The overlay's translation (`environment/hashes.txt`): `translated.wgsl` `b047c969cf28bec4d7c07a34e1334282da69484770490e64cbb8ad5cdf921105`,
  `translation.json` `da63a194c70cf93a11eec5f5bd52b5f983ee5321a796ce2b16f1667874e49c94`, cache fingerprint
  `51fc06286e5b5c1b68704ca2350b2e5066c2d72e3fd97d6c86d6425251218c8d`.

| Binary (not copied here; `environment/binaries.sha256`) | SHA-256 |
|---|---|
| `native/work/linux/linux-arm64/host/build/bin/codebrix_cycles_oracle` (runner) | `17e2ceb7942f5f4ef686a93f1c85a894417c8443a2fe02c69c06ad403db5c2ce` |
| `native/work/linux/linux-arm64/host/build/lib/libcodebrix_cycles_host.so` | `b9bbcacdb3fe4144fdbd3f03dfc00a98f72bffae5aca95c987efc007fc923681` |

Both equal `runnerSha256` and `hostLibrarySha256` in `run.json` and every `meta.json`.

## Render (inside the container)

The scene list of section 5 (`environment/render.sh`; the log's first line is the count check `find count 724`): every
`*.json` under `native/oracle/scenes` except `manifest.json` files and `assets/`, plus the bench bundle's two documents
from `native/work/oracle-assets/bench_01/`. On 2026-11-13 the 13 generated files of that bundle matched
`native/oracle/scenes/assets/manifest.json`, and both documents were byte-identical to
`native/oracle/scenes/assets/bench_01.json` and `bench_01-hdri.json`.

```sh
python3 native/oracle/render_oracles.py --output native/work/oracles/linux-arm64-20261113 \
  --runner "$RUNNER" --host-library "$HOSTLIB" --backend cpu-arm64-linux "${scenes[@]}"
```

Started 2026-11-14 07:29:41 UTC, wall 173 s for 726 scenes (default 4 threads), exit 0 (`environment/render.output.txt`,
summary `environment/render-summary.txt`). `run.json` has 726 scenes, one backend and no `failures`. With one backend
`render_oracles.py` compares nothing inside the run: `comparisonsPassed` is `null` for every scene and no `comparisons/`
directory is written. Every film and pass file matches the SHA-256 its `meta.json` records.

Scenes per family: ao 2, assets 2, bake 36, bsdf 11, cameras 26, combinations 84, curves 28, curves-b 49, denoise 6,
displacement 5, hair 63, lamps 17, light-motion 18, light-tree 2, linux-gates 45, mnee 20, passes 61, points 57,
sampling 21, shadow-catcher 3, shadow-linking 14, sky 10, starter 4, subdivision 2, subsurface 30, surface-nodes 17,
volumes 76, world 17.

## Section 6: comparison with the Linux x64 CPU oracle

**Skipped.** Section 6 compares only against a Linux x64 folder of the same pinned Cycles revision, and none of
Cycles v5.3.0 is committed. The comparison is made later on the Linux x64 machine, against the
`linux-x64-reference-<date>` folder of v5.3.0 that it records. There is no comparison JSON.

## Section 10: what the first run at this pin shows

1. **The scene count.** 726 = 724 committed scene documents + 2 bench documents (item 2 of the checklist).
   `bsdf/principled-dispersion` rendered (32x24, 8 samples), repeat-identical (`repeatMaxAbsDifference` 0), no warnings:
   dispersion renders on the CPU device at this pin.
2. **The mixed-phase volume helpers.** `volumes/phase-mie` and `volumes/mixed-phase-world` (32x24, 16 samples) rendered
   and are repeat-identical (`repeatMaxAbsDifference` 0). `volumes/mixed-phase-world` carries the closure warning above.
3. **The step count of an unbounded volume segment.** `volumes/heterogeneous-world-sun-tail` (32x24, 16 samples)
   rendered and is repeat-identical. The mean of its first film (`film-0.f32`; R, G, B, A):
   `[9.909120599986092e-07, 9.187378108288513e-07, 8.057808932500817e-07, 1.0]` (`environment/section10-abc.txt`).
   For information, the macOS CPU-arm64 film of the same scene, of the base commit
   (`native/evidence/cycles-oracles-macos-20261113/`), has the means
   `[9.909120647355607e-07, 9.187378143075501e-07, 8.057808965437433e-07, 1.0]`.
   The probe (`probe/`, described below) prints `(int)roundf(x)` for inputs read from `argv`, and the clamp of
   `kernel/integrator/shade_volume.h`:

   | Input | Bits | `(int)roundf(x)` | Clamped to 1..1024 |
   |---|---|---|---|
   | `inf` | `0x7f800000` | 2147483647 (`0x7fffffff`) | 1024 |
   | `FLT_MAX` (`3.40282347e38`) | `0x7f7fffff` | 2147483647 (`0x7fffffff`) | 1024 |
   | `1e37` | `0x7cf0bdc2` | 2147483647 (`0x7fffffff`) | 1024 |
   | `2147483648` | `0x4f000000` | 2147483647 (`0x7fffffff`) | 1024 |
   | `4294967296` | `0x4f800000` | 2147483647 (`0x7fffffff`) | 1024 |
   | `nan` | `0x7fc00000` | 0 (`0x00000000`) | 1 |

   With the kernel's compiler and options the conversion **saturates**: an infinite or out-of-range segment takes the
   largest step count, 1024 (`VOLUME_MAX_STEPS`). **The WGSL assumption holds** for every infinite and out-of-range value tested.
   **INFERENCE:** RGB means of order 1e-6 fit a sun fully shadowed by the far medium. No x86-64 film of this pin is
   committed to show the one-step case beside it.
   A NaN converts to 0, which the clamp makes ONE step, not 1024. The comment above `vh_expected_steps` in
   `native/shaders/wavefront_volume_octree.wgsl` names NaN together with the values whose conversion saturates. This run
   checks neither what the WGSL conversion gives for a NaN nor whether a NaN step count occurs in any scene; it is
   recorded for the maintainer.
4. **The NaN of a point hit at its own centre.** Computed at run time from `argv`: `0.0f / 0.0f` = `0x7fc00000`,
   `sqrtf(-1.0f)` = `0x7fc00000`. **The WGSL assumption (`0x7fc00000` on ARM64) holds.**
5. **Repeat identity.** True for every scene (checklist item 3). In `passes/data-passes` the film and passes 0 to 7 are
   identical in the two renders. Only pass 8, `RenderTime` (a wall-clock measurement), differs: `pass-8-0.f32`
   `0d863bba...`, `pass-8-1.f32` `ab1383f6...` (`environment/section10-abc.txt`). It is the same pass that
   `linux-arm64-cpu-oracle-20261105` names as the one that differs between any two renders.

The probe was compiled and run inside the container (`probe/build-probe.sh`, log `probe/probe-run.output.txt`) twice:

- With the CPU kernel's code-generation options, taken from its `compile_commands.json` entry without the defines,
  include paths and warning options:
  `/usr/bin/g++-14 -fno-strict-aliasing -std=c++17 -fPIC -fno-trapping-math -fno-math-errno -fno-signed-zeros -ffp-contract=on -freciprocal-math -fno-signaling-nans -fno-rounding-math -O3 -std=c++20 -fPIC -ffp-contract=off -fno-reciprocal-math -x c++ probe.c`
  (`probe/out-kernelflags.txt`).
- Plain, `gcc-14 -O2 probe.c` (`probe/out-plain.txt`).

The two outputs are identical; the inputs were `inf 3.40282347e38 1e37 2147483648 4294967296 nan -- 0 0 -1`. The probe
is scratch and is not part of the build; its two executables are not copied (SHA-256 in `environment/binaries.sha256`).

## The first attempt (failed, at the base commit)

`first-attempt/` holds its records (times are the Mac's clock, PST, 2026-11-13). It ran at the base commit
`828f6c2f9`, clean tree, in container `cbc-arm64-oracle-20261113`, with the same image and toolchain. The dependency
build passed (exit 0, 7 min 4 s, `04a-deps.txt`). `python3 native/tools/build_cycles_linux_arm64.py --jobs 8 --device-set`
failed after 2 min 6 s with exit 1, in the overlay step, before the CMake configure (`04b-cycles.txt`, verbatim). The failing
command was `research/translate.py`'s AST dump (`<repo>` is `/work/CodeBrix.Graphics3D.Cycles` in the log):

```
clang++ -std=c++20 -fsyntax-only -Xclang -ast-dump=json -I <repo>/third_party/deps/tbb/include \
  -Xclang -ast-dump-filter=ccl::ray_triangle_reciprocal -I <repo>/third_party/cycles/src <repo>/research/native/reference.cpp
```

The include chain was `research/native/reference.cpp:18` -> `util/math.h:7` -> `util/types.h:42` ->
`util/types_gpu_compressed.h:7` -> `util/half.h`. It produced 9 errors, all in the pinned
`third_party/cycles/src/util/half.h`:

| Line:column | Error |
|---|---|
| 208:3 | unknown type name `uint16x4_t` |
| 208:18 | use of undeclared identifier `vdup_n_u16` |
| 225:63 | use of undeclared identifier `vdupq_n_f32` |
| 244:46 | use of undeclared identifier `float32x4_t` |
| 268:47 | use of undeclared identifier `vld1_u16` |
| 284:9 | unknown type name `uint16x4_t` |
| 284:58 | use of undeclared identifier `float32x4_t` |
| 303:9 | unknown type name `float32x4_t` |
| 304:28 | use of undeclared identifier `uint16x4_t` |

Cause (established on scratch copies, nothing tracked edited):

- Clang predefines `__ARM_NEON` for its aarch64 target, and `util/half.h` then selects its NEON conversions.
- Nothing in that include closure includes `arm_neon.h`. Upstream gets it only through sse2neon, which this repository
  does not use for Cycles. The recipes add a guarded include only to their work copy of `half.h`; the translator reads
  the pristine `third_party/cycles`.
- What is new at v5.3.0 is `util/types_gpu_compressed.h`, which `util/types.h` includes. At the previous pin `half.h` was
  not in `reference.cpp`'s include closure, so the earlier ARM64 runs translated without error.

No pinned file was edited. The fix, approved by the maintainer and part of `0b19d8750`, adds `-U__ARM_NEON` to the
clang command in `research/translate.py` `ast()`. An ARM64 build host then parses the same generic branches of the
pinned headers as an x86-64 host. Verification:

- On scratch copies, with the flag, the translation passed in the same image and on the Mac (Apple clang 21). Both gave
  `translated.wgsl` `b047c969...` and `translation.json` `da63a194...`, the Linux x64 outputs.
- On x86-64 targets the flag changes nothing: the preprocessed translation unit is byte-identical.
- In this run the build passed with the same two hashes. Its cache fingerprint was `51fc0628...`, the value predicted
  for the fixed translator.

The failed tree is `native/work/linux/linux-arm64/host-failed-20261113/` (ignored, not evidence). The run's scripts are
`first-attempt/run-deps.sh` and `run-cycles.sh`; `<repo>` replaces the checkout path.

## Contents

- `oracle/<scene>/cpu-arm64-linux/`: `film-0.f32`, `film-1.f32` (scene-linear RGBA float32, little-endian, lower-left
  origin), `pass-<index>-<repeat>.f32` where the scene writes render passes, and `meta.json`;
  `oracle/<scene>/cpu-arm64-linux.output.txt` holds the runner's output. These are copied unchanged from
  `native/work/oracles/linux-arm64-20261113/`.
- `run.json`: the render record (`scenes`, `backends`, `common`, `kernelCompileSeconds`, `comparisonsPassed`), copied
  unchanged.
- `probe/`: the section 10 program `probe.c`, its build-and-run script, its log and its two outputs.
- `environment/`: the run's step notes (`steps.md`), the commit and working-tree records, the image, versions and apt
  records, the build and render scripts, the build and render logs, the render summary, the overlay hashes, the
  runner and host paths, the section 10 extracts, the CPU kernel compile line, the Docker facts, the SHA-256 of the
  binaries, and `build/` (the build records named above).
- `first-attempt/`: the records of the failed first attempt.
- `SHA256SUMS.txt`: every other file of this folder (`shasum -a 256 -c SHA256SUMS.txt`).

Renames: the repository ignores `*.log`, so `build-deps.log`, `build-cycles.log`, `render.log` and `probe-run.log` are
copied as `*.output.txt`, and the host's `build.log` as `environment/build/host-build.output.txt`. The copyright and
SPDX lines were added at the top of the copied scripts (`*.sh`) and step notes (`steps.md`). Nothing else in a copied
file was changed, except the checkout path in the two `first-attempt/*.sh` files. Built executables and libraries are
not evidence and are not copied here.
