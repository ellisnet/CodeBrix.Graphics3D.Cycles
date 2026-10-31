# Metal comparison on Apple Silicon: Velvet subnormal paths and Radial Tiling with two sides (2026-10-31)

Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

The Apple Silicon side of the comparison prepared in
`native/evidence/metal-comparison-linux-x64-20261031/` (frozen; never written to), made by
docs/TESTING-MACOS-ARM64.md section 9 on a Mac mini M2 Pro: the comparison scenes and
`bsdf/velvet` rendered with pinned Cycles' own Metal device (`metal-m2pro`, and
`metal-m2pro-metalrt` with MetalRT forced on) and its CPU device (`cpu-arm64-macos`), every
film compared with the frozen Linux x64 films, and a kernel-level Metal probe of the velvet
value chain.

## Answers

**1. Velvet subnormal paths: pinned Cycles-with-Metal flushes subnormal floats too.** On the
M2 Pro GPU every binary32 arithmetic operation reads a subnormal operand as zero and writes a
result below FLT_MIN as zero, with fast math on (the option pinned Cycles compiles with) and
off; loads and stores keep the bits (probe, below). So Metal ends the same velvet paths that
the WebGPU device and the FTZ+DAZ x64 CPU end. The `velvet_chain.c` values at sigma 0.3
themselves stay normal on Metal (`expf` 1.37307603e-38, BSDF value 9.97140401e-37 against the
CPU's 9.97129997e-37). At film level nothing of this is visible: `velvet-subnormal` and
`bsdf/velvet` on both Metal backends are **within the practical criterion** of every CPU film
(x64, x64 with FTZ+DAZ, macOS arm64, Linux arm64), with no practical failures; the x64 normal
and FTZ+DAZ films are bitwise identical to each other.

**2. Radial Tiling with two sides: the Metal build renders every scene and refuses none.**
At roundness 0 and 1, and for the three-sided control, both Metal backends are **within the
practical criterion** of the CPU films. At roundness 0.3, 0.5 and 0.75, the range the WebGPU
device refuses, both Metal backends are **far outside it**: about 700 of 3,072 film
components fail (about 410 of 768 pixels, spread over both panels), the largest difference is
1.77 (CPU 1.765 at pixel 18, 6, blue; Metal 0), and 163 pixels that are non-zero on the CPU
are black on Metal. Neither device produces NaN or Inf. The two CPU films (macOS arm64 and
Linux x64) agree within the practical criterion in that range too (31 or 32 strict differences,
at most 2.3e-6), and Metal and MetalRT agree with each other (maximum 1.2e-7). Stock Metal
therefore does not reproduce the CPU in that range; it outputs a different image rather than
NaN or Inf.

These are observations for the maintainer; nothing was adjusted, rerendered or retuned.

## Source and working tree

| Item | Value |
|---|---|
| Commit | `af0a254dd26966f8223e5c55f70f1866fd25ec47` |
| Working tree | **not clean** (`repoWorkingTreeClean: false` in every `meta.json`). At the start of the render eight files differed from the commit, all made on 2026-10-31 and committed with this folder (`environment/render-status-start.txt`, SHA-256 in `environment/modified-sha256.txt`): the macOS recipes `tools/dependencies/build_macos.py`, `tools/build_macos.py`, `tools/dependencies/build_macos_x64.py`, `tools/build_macos_x64.py` (SDK named with `xcrun --sdk macosx`, see Build), `native/oracle/render_oracles.py` (device-tree model optional in containers) and the documents `docs/TESTING-MACOS-ARM64.md`, `docs/TESTING-LINUX-ARM64-ORACLE.md`, `native/host/MACOS_X64.md`. None of them is kernel, host or runner source. |

## Machine and toolchain

| Item | Value |
|---|---|
| Mac | Mac mini (Mac14,12), Apple M2 Pro (10 CPU cores, 16 GPU cores, Metal 4 family), 16 GB; macOS 27.2 |
| Xcode | Xcode 27.1 (27A9275), developer directory `/Volumes/VideoWork/Applications/Xcode.app/Contents/Developer`, macOS SDK 27.0; Apple clang 21.0.0 (clang-2100.3.34.2); CMake 4.4.5; Python 3.15.2. Command Line Tools 27.2 are also installed (see Build) |
| Metal settings | as `render_oracles.py` sets them: `CYCLES_METAL_SPECIALIZATION_LEVEL=0`; `CYCLES_METALRT=0` (`metal-m2pro`) and `1` (`metal-m2pro-metalrt`, a validation path: the shipped rule is MetalRT on M3 and later only) |
| CPU kernels | fixed baseline: the kernel compile line ends `-ffp-contract=off -fno-reciprocal-math`, no `-mcpu` (`cpuKernelCompile` in `run.json`) |

## Build

```sh
python3 tools/build_macos.py --jobs 8     # 203.3 s wall, exit 0
```

The first attempt at the checked-out commit stopped after 10 s in the dependency recipe's
identity record: `xcrun --show-sdk-version` failed (`xcodebuild: error: SDK
"/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk" cannot be located`). Without `--sdk`,
xcrun takes the newest SDK it finds (`man xcrun`); Command Line Tools 27.2, installed that
morning, had made their macOS 27.2 SDK the newest, and Xcode 27.1 (macOS 27.0 SDK) cannot read
it (`environment/first-attempt-build.output.txt`). The recipes now name the SDK (`xcrun --sdk macosx`) and pass it as `SDKROOT` and
`CMAKE_OSX_SYSROOT` (docs/TESTING-MACOS-ARM64.md, Prerequisites). The rebuild recorded
`sdkPath` `.../Xcode.app/.../SDKs/MacOSX27.0.sdk` and `sdkVersion` `27.0`
(`environment/build/invocation.json`), and all 174 compile commands of the host build carry
that SDK. Build records: `environment/build/` (`status.json`, `invocation.json`, the
dependency marker), log `environment/build.output.txt`.

| Binary (not copied here) | SHA-256 |
|---|---|
| `native/work/macos/osx-arm64/host/build/lib/libcodebrix_cycles_host.dylib` | `69d8173f13b91fd55fcd5448b9099a956f63ba740e72965b03f91d4a609f2215` |
| `native/work/macos/osx-arm64/oracle-build/codebrix_cycles_oracle` | `15573ad6088c0cd54f733cc00d1127a55aa00fd604739a89f17b89a542a528d3` |

## Render (section 9 step 1)

```sh
bash -c 'E=native/evidence/metal-comparison-linux-x64-20261031; scenes=(); for f in $E/scenes/*.json native/oracle/scenes/bsdf/velvet.json; do scenes+=(--scene "$f"); done
python3 native/oracle/render_oracles.py --output native/work/oracles/metal-comparison-20261031 \
  --runner native/work/macos/osx-arm64/oracle-build/codebrix_cycles_oracle \
  --host-library native/work/macos/osx-arm64/host/build/lib/libcodebrix_cycles_host.dylib \
  --backend cpu-arm64-macos --backend metal-m2pro --backend metal-m2pro-metalrt "${scenes[@]}"'
```

Wall 185.1 s (2026-10-31 19:18:49 to 19:21:54 UTC), exit 0, no failures (`run.json`
`failures` is empty): all 8 scenes rendered on all 3 backends. Each Metal backend compiled
its kernels in a fresh cache on its first render (`warmup/`, scene
`metal-comparison/radial-sides2-r0.3`): `kernelCompileSeconds` **85.3 s** (`metal-m2pro`)
and **85.7 s** (`metal-m2pro-metalrt`), against about 7.5 s recorded earlier on this Mac
(docs/TESTING-MACOS-ARM64.md section 4); recorded as a finding, not investigated. No
`meta.json` has Metal compile `warnings`. CPU repeats are bitwise identical; Metal repeats
differ by at most 2.4e-7 (film accumulation uses atomics), see the table at the end.

## Comparisons (section 9 steps 2 and 3)

`compare.py <reference> <candidate> --json compare/<scene>--<candidate>--vs--<reference>.json`
for every Metal film and the macOS CPU film against the frozen `cpu-x64-linux/`,
`cpu-x64-linux-ftz/` (labelled "FTZ+DAZ" below; its backend directory is also named
`cpu-x64-linux`) and `wgsl-linux-intel/` films, the Metal films against `cpu-arm64-macos`, and
for `bsdf/velvet`, which has no x64 film in the repository (the scene was added in
`af0a254dd`), against the Linux ARM64 CPU film of
`native/evidence/linux-arm64-cpu-oracle-20261031/`. `compare.py` needs a `cpu-*` backend as
gold standard: where a WGSL film is given as reference it takes the CPU film as reference
itself (the tables show the roles it used), and Metal-versus-WGSL and Metal-versus-MetalRT
pairs are refused ("no gold standard"; `environment/compare.output.txt`). Metal against MetalRT
is in `render_oracles.py`'s own three-way reports (`comparisons/`). The WGSL films of roundness
0.3, 0.5 and 0.75 do not exist (refused by design). Criteria: practical
`abs(a-b) <= 5e-5 + 2e-4*abs(b)` (b the reference), strict `abs(a-b) <= 1e-7 + 1e-7*abs(b)`.

**velvet-subnormal**

| Candidate | Reference | Bitwise | Practical failures | Strict differences | Max abs | Max rel | Worst pixel (x, y, channel: reference / candidate) | NaN/Inf (ref, cand) |
|---|---|---|---|---|---|---|---|---|
| metal-m2pro | cpu-arm64-macos | no | 0 | 111 | 1.91e-06 | 0.000163 | 23, 7, B: 0.121905625 / 0.121904999 | 0, 0 |
| metal-m2pro | cpu-x64-linux | no | 0 | 124 | 1.91e-06 | 0.000167 | 23, 7, B: 0.121905684 / 0.121904999 | 0, 0 |
| metal-m2pro | cpu-x64-linux (FTZ+DAZ) | no | 0 | 124 | 1.91e-06 | 0.000167 | 23, 7, B: 0.121905684 / 0.121904999 | 0, 0 |
| metal-m2pro-metalrt | cpu-arm64-macos | no | 0 | 117 | 1.91e-06 | 0.000163 | 16, 4, R: 0.12866962 / 0.128669232 | 0, 0 |
| metal-m2pro-metalrt | cpu-x64-linux | no | 0 | 120 | 1.91e-06 | 0.000167 | 16, 4, R: 0.128669694 / 0.128669232 | 0, 0 |
| metal-m2pro-metalrt | cpu-x64-linux (FTZ+DAZ) | no | 0 | 120 | 1.91e-06 | 0.000167 | 16, 4, R: 0.128669694 / 0.128669232 | 0, 0 |
| wgsl-linux-intel | cpu-arm64-macos | no | 0 | 51 | 1.91e-06 | 3.67e-05 | 23, 6, R: 0.0465131365 / 0.0465134047 | 0, 0 |
| cpu-arm64-macos | cpu-x64-linux | no | 0 | 1 | 1.19e-07 | 2.01e-05 | 22, 4, B: 0.106834263 / 0.106834143 | 0, 0 |
| cpu-arm64-macos | cpu-x64-linux (FTZ+DAZ) | no | 0 | 1 | 1.19e-07 | 2.01e-05 | 22, 4, B: 0.106834263 / 0.106834143 | 0, 0 |

**bsdf/velvet**

| Candidate | Reference | Bitwise | Practical failures | Strict differences | Max abs | Max rel | Worst pixel (x, y, channel: reference / candidate) | NaN/Inf (ref, cand) |
|---|---|---|---|---|---|---|---|---|
| metal-m2pro | cpu-arm64-linux | no | 0 | 346 | 1.91e-06 | 1.17e-05 | 11, 15, R: 0.155773848 / 0.155773357 | 0, 0 |
| metal-m2pro | cpu-arm64-macos | no | 0 | 346 | 1.91e-06 | 1.17e-05 | 11, 15, R: 0.155773848 / 0.155773357 | 0, 0 |
| metal-m2pro-metalrt | cpu-arm64-linux | no | 0 | 358 | 1.91e-06 | 1.19e-05 | 5, 4, R: 0.112528875 / 0.112528063 | 0, 0 |
| metal-m2pro-metalrt | cpu-arm64-macos | no | 0 | 358 | 1.91e-06 | 1.19e-05 | 5, 4, R: 0.112528875 / 0.112528063 | 0, 0 |
| cpu-arm64-macos | cpu-arm64-linux | no | 0 | 0 | 7.45e-09 | 2.02e-07 | 17, 7, G: 0.0714361519 / 0.0714361444 | 0, 0 |

**radial-sides2-r0**

| Candidate | Reference | Bitwise | Practical failures | Strict differences | Max abs | Max rel | Worst pixel (x, y, channel: reference / candidate) | NaN/Inf (ref, cand) |
|---|---|---|---|---|---|---|---|---|
| metal-m2pro | cpu-arm64-macos | no | 0 | 462 | 5.25e-06 | 0.000128 | 21, 14, B: 1.06477344 / 1.0647682 | 0, 0 |
| metal-m2pro | cpu-x64-linux | no | 0 | 454 | 6.56e-06 | 0.000134 | 14, 6, B: 1.3423115 / 1.34230494 | 0, 0 |
| metal-m2pro | cpu-x64-linux (FTZ+DAZ) | no | 0 | 454 | 6.56e-06 | 0.000134 | 14, 6, B: 1.3423115 / 1.34230494 | 0, 0 |
| metal-m2pro-metalrt | cpu-arm64-macos | no | 0 | 462 | 4.53e-06 | 7.57e-06 | 14, 6, B: 1.34230936 / 1.34230483 | 0, 0 |
| metal-m2pro-metalrt | cpu-x64-linux | no | 0 | 453 | 6.68e-06 | 7.87e-06 | 14, 6, B: 1.3423115 / 1.34230483 | 0, 0 |
| metal-m2pro-metalrt | cpu-x64-linux (FTZ+DAZ) | no | 0 | 453 | 6.68e-06 | 7.87e-06 | 14, 6, B: 1.3423115 / 1.34230483 | 0, 0 |
| wgsl-linux-intel | cpu-arm64-macos | no | 0 | 254 | 3.34e-06 | 1.51e-05 | 19, 5, B: 0.924330235 / 0.924326897 | 0, 0 |
| cpu-arm64-macos | cpu-x64-linux | no | 0 | 51 | 2.15e-06 | 6.38e-06 | 25, 3, B: 0.24164322 / 0.241642386 | 0, 0 |
| cpu-arm64-macos | cpu-x64-linux (FTZ+DAZ) | no | 0 | 51 | 2.15e-06 | 6.38e-06 | 25, 3, B: 0.24164322 / 0.241642386 | 0, 0 |

**radial-sides2-r0.3**

| Candidate | Reference | Bitwise | Practical failures | Strict differences | Max abs | Max rel | Worst pixel (x, y, channel: reference / candidate) | NaN/Inf (ref, cand) |
|---|---|---|---|---|---|---|---|---|
| metal-m2pro | cpu-arm64-macos | no | 695 | 805 | 1.77 | 1 | 18, 6, B: 1.76512706 / 0 | 0, 0 |
| metal-m2pro | cpu-x64-linux | no | 695 | 802 | 1.77 | 1 | 18, 6, B: 1.76512706 / 0 | 0, 0 |
| metal-m2pro | cpu-x64-linux (FTZ+DAZ) | no | 695 | 802 | 1.77 | 1 | 18, 6, B: 1.76512706 / 0 | 0, 0 |
| metal-m2pro-metalrt | cpu-arm64-macos | no | 695 | 809 | 1.77 | 1 | 18, 6, B: 1.76512706 / 0 | 0, 0 |
| metal-m2pro-metalrt | cpu-x64-linux | no | 695 | 806 | 1.77 | 1 | 18, 6, B: 1.76512706 / 0 | 0, 0 |
| metal-m2pro-metalrt | cpu-x64-linux (FTZ+DAZ) | no | 695 | 806 | 1.77 | 1 | 18, 6, B: 1.76512706 / 0 | 0, 0 |
| cpu-arm64-macos | cpu-x64-linux | no | 0 | 32 | 2.26e-06 | 3.89e-06 | 25, 3, B: 0.295107126 / 0.295106292 | 0, 0 |
| cpu-arm64-macos | cpu-x64-linux (FTZ+DAZ) | no | 0 | 32 | 2.26e-06 | 3.89e-06 | 25, 3, B: 0.295107126 / 0.295106292 | 0, 0 |

**radial-sides2-r0.5**

| Candidate | Reference | Bitwise | Practical failures | Strict differences | Max abs | Max rel | Worst pixel (x, y, channel: reference / candidate) | NaN/Inf (ref, cand) |
|---|---|---|---|---|---|---|---|---|
| metal-m2pro | cpu-arm64-macos | no | 704 | 814 | 1.76 | 1 | 18, 6, B: 1.7617774 / 0 | 0, 0 |
| metal-m2pro | cpu-x64-linux | no | 704 | 810 | 1.76 | 1 | 18, 6, B: 1.7617774 / 0 | 0, 0 |
| metal-m2pro | cpu-x64-linux (FTZ+DAZ) | no | 704 | 810 | 1.76 | 1 | 18, 6, B: 1.7617774 / 0 | 0, 0 |
| metal-m2pro-metalrt | cpu-arm64-macos | no | 704 | 817 | 1.76 | 1 | 18, 6, B: 1.7617774 / 0 | 0, 0 |
| metal-m2pro-metalrt | cpu-x64-linux | no | 704 | 813 | 1.76 | 1 | 18, 6, B: 1.7617774 / 0 | 0, 0 |
| metal-m2pro-metalrt | cpu-x64-linux (FTZ+DAZ) | no | 704 | 813 | 1.76 | 1 | 18, 6, B: 1.7617774 / 0 | 0, 0 |
| cpu-arm64-macos | cpu-x64-linux | no | 0 | 32 | 2.03e-06 | 3.77e-06 | 25, 3, B: 0.295107126 / 0.295106292 | 0, 0 |
| cpu-arm64-macos | cpu-x64-linux (FTZ+DAZ) | no | 0 | 32 | 2.03e-06 | 3.77e-06 | 25, 3, B: 0.295107126 / 0.295106292 | 0, 0 |

**radial-sides2-r0.75**

| Candidate | Reference | Bitwise | Practical failures | Strict differences | Max abs | Max rel | Worst pixel (x, y, channel: reference / candidate) | NaN/Inf (ref, cand) |
|---|---|---|---|---|---|---|---|---|
| metal-m2pro | cpu-arm64-macos | no | 716 | 821 | 1.76 | 1 | 18, 6, B: 1.75751078 / 0 | 0, 0 |
| metal-m2pro | cpu-x64-linux | no | 716 | 817 | 1.76 | 1 | 18, 6, B: 1.75751078 / 0 | 0, 0 |
| metal-m2pro | cpu-x64-linux (FTZ+DAZ) | no | 716 | 817 | 1.76 | 1 | 18, 6, B: 1.75751078 / 0 | 0, 0 |
| metal-m2pro-metalrt | cpu-arm64-macos | no | 716 | 824 | 1.76 | 1 | 18, 6, B: 1.75751078 / 0 | 0, 0 |
| metal-m2pro-metalrt | cpu-x64-linux | no | 716 | 821 | 1.76 | 1 | 18, 6, B: 1.75751078 / 0 | 0, 0 |
| metal-m2pro-metalrt | cpu-x64-linux (FTZ+DAZ) | no | 716 | 821 | 1.76 | 1 | 18, 6, B: 1.75751078 / 0 | 0, 0 |
| cpu-arm64-macos | cpu-x64-linux | no | 0 | 31 | 2.15e-06 | 4.69e-06 | 25, 3, B: 0.295107126 / 0.295106292 | 0, 0 |
| cpu-arm64-macos | cpu-x64-linux (FTZ+DAZ) | no | 0 | 31 | 2.15e-06 | 4.69e-06 | 25, 3, B: 0.295107126 / 0.295106292 | 0, 0 |

**radial-sides2-r1**

| Candidate | Reference | Bitwise | Practical failures | Strict differences | Max abs | Max rel | Worst pixel (x, y, channel: reference / candidate) | NaN/Inf (ref, cand) |
|---|---|---|---|---|---|---|---|---|
| metal-m2pro | cpu-arm64-macos | no | 0 | 543 | 7.39e-06 | 6.16e-06 | 21, 14, G: 1.42977083 / 1.42976344 | 0, 0 |
| metal-m2pro | cpu-x64-linux | no | 0 | 540 | 7.27e-06 | 6.16e-06 | 21, 14, G: 1.42977071 / 1.42976344 | 0, 0 |
| metal-m2pro | cpu-x64-linux (FTZ+DAZ) | no | 0 | 540 | 7.27e-06 | 6.16e-06 | 21, 14, G: 1.42977071 / 1.42976344 | 0, 0 |
| metal-m2pro-metalrt | cpu-arm64-macos | no | 0 | 550 | 4.65e-06 | 6.16e-06 | 9, 9, G: 0.47508508 / 0.475087225 | 0, 0 |
| metal-m2pro-metalrt | cpu-x64-linux | no | 0 | 544 | 6.68e-06 | 6.16e-06 | 14, 6, B: 1.34018314 / 1.34017646 | 0, 0 |
| metal-m2pro-metalrt | cpu-x64-linux (FTZ+DAZ) | no | 0 | 544 | 6.68e-06 | 6.16e-06 | 14, 6, B: 1.34018314 / 1.34017646 | 0, 0 |
| wgsl-linux-intel | cpu-arm64-macos | no | 0 | 307 | 3.81e-06 | 4.05e-06 | 19, 5, B: 0.899839461 / 0.899836361 | 0, 0 |
| cpu-arm64-macos | cpu-x64-linux | no | 0 | 82 | 3.1e-06 | 3.46e-06 | 25, 3, B: 0.241125688 / 0.241124853 | 0, 0 |
| cpu-arm64-macos | cpu-x64-linux (FTZ+DAZ) | no | 0 | 82 | 3.1e-06 | 3.46e-06 | 25, 3, B: 0.241125688 / 0.241124853 | 0, 0 |

**radial-sides3-r0.5**

| Candidate | Reference | Bitwise | Practical failures | Strict differences | Max abs | Max rel | Worst pixel (x, y, channel: reference / candidate) | NaN/Inf (ref, cand) |
|---|---|---|---|---|---|---|---|---|
| metal-m2pro | cpu-arm64-macos | no | 0 | 532 | 5.84e-06 | 2.12e-05 | 21, 14, G: 1.10631824 / 1.10631239 | 0, 0 |
| metal-m2pro | cpu-x64-linux | no | 0 | 532 | 8.11e-06 | 1.82e-05 | 14, 6, B: 1.54873657 / 1.54872847 | 0, 0 |
| metal-m2pro | cpu-x64-linux (FTZ+DAZ) | no | 0 | 532 | 8.11e-06 | 1.82e-05 | 14, 6, B: 1.54873657 / 1.54872847 | 0, 0 |
| metal-m2pro-metalrt | cpu-arm64-macos | no | 0 | 526 | 5.6e-06 | 2.01e-05 | 9, 4, R: 0.272800893 / 0.272799045 | 0, 0 |
| metal-m2pro-metalrt | cpu-x64-linux | no | 0 | 525 | 8.23e-06 | 1.71e-05 | 14, 6, B: 1.54873657 / 1.54872835 | 0, 0 |
| metal-m2pro-metalrt | cpu-x64-linux (FTZ+DAZ) | no | 0 | 525 | 8.23e-06 | 1.71e-05 | 14, 6, B: 1.54873657 / 1.54872835 | 0, 0 |
| wgsl-linux-intel | cpu-arm64-macos | no | 0 | 297 | 3.93e-06 | 8.48e-06 | 19, 5, B: 1.04600012 / 1.04599619 | 0, 0 |
| cpu-arm64-macos | cpu-x64-linux | no | 0 | 71 | 2.62e-06 | 5.4e-06 | 25, 3, B: 0.309178144 / 0.309177279 | 0, 0 |
| cpu-arm64-macos | cpu-x64-linux (FTZ+DAZ) | no | 0 | 71 | 2.62e-06 | 5.4e-06 | 25, 3, B: 0.309178144 / 0.309177279 | 0, 0 |


### Repeats and render seconds

Render seconds as recorded in `meta.json` (the first render includes scene setup; the Metal kernels were already compiled by the warm-up).

| Scene | Backend | Repeat bitwise | repeatMaxAbsDifference | Render s (1st / 2nd) | Warnings |
|---|---|---|---|---|---|
| bsdf/velvet | cpu-arm64-macos | True | 0 | 0.00246 / 0.00112 | 0 |
| bsdf/velvet | metal-m2pro | False | 5.96e-08 | 0.473 / 0.0278 | 0 |
| bsdf/velvet | metal-m2pro-metalrt | False | 5.96e-08 | 0.286 / 0.0289 | 0 |
| metal-comparison/radial-sides2-r0 | cpu-arm64-macos | True | 0 | 0.00193 / 0.00138 | 0 |
| metal-comparison/radial-sides2-r0 | metal-m2pro | False | 1.19e-07 | 0.392 / 0.0238 | 0 |
| metal-comparison/radial-sides2-r0 | metal-m2pro-metalrt | False | 2.38e-07 | 0.216 / 0.0183 | 0 |
| metal-comparison/radial-sides2-r0.3 | cpu-arm64-macos | True | 0 | 0.00261 / 0.000938 | 0 |
| metal-comparison/radial-sides2-r0.3 | metal-m2pro | False | 1.19e-07 | 0.379 / 0.0257 | 0 |
| metal-comparison/radial-sides2-r0.3 | metal-m2pro-metalrt | False | 1.19e-07 | 0.47 / 0.0511 | 0 |
| metal-comparison/radial-sides2-r0.5 | cpu-arm64-macos | True | 0 | 0.00188 / 0.000832 | 0 |
| metal-comparison/radial-sides2-r0.5 | metal-m2pro | False | 1.19e-07 | 0.419 / 0.0266 | 0 |
| metal-comparison/radial-sides2-r0.5 | metal-m2pro-metalrt | False | 1.19e-07 | 0.41 / 0.0275 | 0 |
| metal-comparison/radial-sides2-r0.75 | cpu-arm64-macos | True | 0 | 0.0025 / 0.00114 | 0 |
| metal-comparison/radial-sides2-r0.75 | metal-m2pro | False | 1.19e-07 | 0.37 / 0.0248 | 0 |
| metal-comparison/radial-sides2-r0.75 | metal-m2pro-metalrt | False | 1.19e-07 | 0.214 / 0.0231 | 0 |
| metal-comparison/radial-sides2-r1 | cpu-arm64-macos | True | 0 | 0.00194 / 0.00108 | 0 |
| metal-comparison/radial-sides2-r1 | metal-m2pro | False | 1.19e-07 | 0.379 / 0.0233 | 0 |
| metal-comparison/radial-sides2-r1 | metal-m2pro-metalrt | False | 1.19e-07 | 0.213 / 0.0193 | 0 |
| metal-comparison/radial-sides3-r0.5 | cpu-arm64-macos | True | 0 | 0.00205 / 0.00127 | 0 |
| metal-comparison/radial-sides3-r0.5 | metal-m2pro | False | 1.19e-07 | 0.412 / 0.0252 | 0 |
| metal-comparison/radial-sides3-r0.5 | metal-m2pro-metalrt | False | 1.19e-07 | 0.213 / 0.015 | 0 |
| metal-comparison/velvet-subnormal | cpu-arm64-macos | True | 0 | 0.00252 / 0.00127 | 0 |
| metal-comparison/velvet-subnormal | metal-m2pro | False | 2.98e-08 | 0.379 / 0.0236 | 0 |
| metal-comparison/velvet-subnormal | metal-m2pro-metalrt | False | 2.98e-08 | 0.208 / 0.0162 | 0 |

## Velvet: does pinned Cycles-with-Metal flush subnormal floats? (kernel-level probe)

**Answer: yes.** On the Apple M2 Pro GPU (macOS 27.2, Metal Shading Language 3.2) every binary32 arithmetic
operation flushes subnormal values: a subnormal operand is read as zero (DAZ) and a result below FLT_MIN
(1.17549435e-38) is written as zero (FTZ). This holds both with `fastMathEnabled = YES`, the option pinned Cycles
compiles its Metal kernels with, and with fast math off. A velvet path whose value, or whose value times the closure
weight and throughput, falls below FLT_MIN therefore ends on Metal exactly as it does on the WebGPU device and on the
Linux x64 CPU with FTZ+DAZ set, and not as on the normal CPU oracle, which keeps subnormals.

The values the frozen input folder asks about are **not** subnormal: for the `probes/velvet_chain.c` inputs at sigma
0.3, Metal's `expf` result (about 1.3731e-38) and BSDF value (about 9.971e-37) are normal and CPU-like (within about
1e-5 relative), not 0.

### What pinned Cycles passes and uses (checked in `third_party/cycles`)

- `src/device/metal/device_impl.mm` (MSL to AIR compile): `MTLCompileOptions` with `fastMathEnabled = YES` and
  `languageVersion = MTLLanguageVersion3_2` on macOS 15 and later (2.4 / 3.0 / 3.1 on macOS 12 / 13 / 14). With the
  macOS 27 Metal framework, `fastMathEnabled = YES` reads back as `mathMode = MTLMathModeFast` and
  `mathFloatingPointFunctions = MTLMathFloatingPointFunctionsFast` (`fastMathEnabled` is deprecated since macOS 15 in
  favour of `mathMode`; pinned Cycles still sets `fastMathEnabled`).
- `src/kernel/device/metal/compat.h`: `#define trigmode fast`, so `expf` is `metal::fast::exp` and `sqrtf` is
  `metal::fast::sqrt` (also `sinf`, `cosf`, `tanf`, `logf`).
- `src/util/math_float3.h`: Cycles' own `dot` and `normalize` are compiled only when `__KERNEL_METAL__` is not
  defined, so on Metal `dot(float3, float3)` and `normalize(float3)` are the Metal built-ins.
- `src/kernel/closure/bsdf_ashikhmin_velvet.h` (`bsdf_ashikhmin_velvet_sample`):
  `D = expf(-cotangent2 * m_invsigma2) * m_invsigma2 * M_1_PI_F / sinNH4`, `power = 0.25f * (D * G) / cosNI`.

### The probe (`probes/metal_velvet_probe.m`)

A standalone Objective-C program with one Metal compute kernel, compiled at run time with
`newLibraryWithSource:options:` exactly as Cycles does: once with `fastMathEnabled = YES` and `languageVersion 3.2`
(the pinned Cycles options), once with `fastMathEnabled = NO` (`MTLMathModeSafe`, precise functions). It is not
Cycles' own compiled kernel: it repeats the velvet expressions of the pinned sample function with the same Metal
functions. Every input is read from a buffer, so nothing can be folded at compile time. One thread computes:

- `cycles` chain: `bsdf_ashikhmin_velvet_setup` and `_sample` for the `velvet_chain.c` vectors N, wi, wo and the
  nine fixture sigmas, with `metal::dot`, `metal::normalize`, `fabs`, `fmax`, `fmin` and `fast::exp`;
- `cprobe` chain: `velvet_chain.c` line for line (scalar dot products, `fast::sqrt`, `fast::exp`);
- direct checks: `exp(-87.1811905)` in the `fast::`, `precise::` and default namespaces; a subnormal input (1e-39)
  times 1, plus 0, compared with 0, and copied without arithmetic; `FLT_MIN * 0.5`; `exp(-88.1811905)` (exact result
  5.05e-39, subnormal);
- the gate's own values: the four distinct normal-CPU velvet values of `velvet-gate-paths.txt` for paths 534, 549,
  1566 and 2055, times 1 and times their closure weight.

Outputs are printed with `%.9g` and as binary32 bit patterns (`probes/metal_velvet_probe.output.txt`).

CPU references made on the same Mac:

- `probes/velvet_chain_cpu_arm64.output.txt`: the frozen `velvet_chain.c`, unchanged, built with Apple clang 21
  (`-O2 -ffp-contract=off -fno-reciprocal-math`, the fixed oracle baseline), run on the M2 Pro CPU.
- `probes/velvet_chain_cpu_arm64_fz.output.txt`: the same source linked with `probes/fz_arm64.c`, the AArch64
  counterpart of `ftz_daz.c`: it sets FPCR.FZ (bit 24) before `main` and prints `FLT_MIN * 0.5 = 0` to show that
  the bit is set.

### Results

**1. Subnormal handling on the GPU (identical in both modes).**

| Check | IEEE 754 (CPU without flushing) | Metal, fast math on | Metal, fast math off |
|---|---|---|---|
| 1e-39 (subnormal, bits 0x000ae398) * 1 | 1e-39 | 0 | 0 |
| 1e-39 + 0 | 1e-39 | 0 | 0 |
| 1e-39 != 0 | true | false | false |
| 1e-39 copied, no arithmetic | 0x000ae398 | 0x000ae398 (bits kept) | 0x000ae398 (bits kept) |
| FLT_MIN * 0.5 | 5.87747175e-39 | 0 | 0 |
| exp(-88.1811905), exact 5.05e-39 | subnormal | 0 (`fast::` and `precise::`) | 0 (`fast::` and `precise::`) |

Arithmetic and comparisons flush subnormal operands and results to zero; loads and stores do not change bits.

**2. The decisive values (`velvet_chain.c` inputs, sigma 0.3).**

| | arg = -cot2 * invsigma2 | expf | expf bits | BSDF value (`power`) | value bits | value vs CPU |
|---|---|---|---|---|---|---|
| Linux x64 CPU (gate, per the input README) and Mac arm64 CPU | -87.1811905 | 1.37306174e-38 | 0x00958360 | 9.97129997e-37 | 0x03a9a723 | - |
| Mac arm64 CPU with FPCR.FZ | -87.1811905 | 1.37306174e-38 | 0x00958360 | 9.97129997e-37 | 0x03a9a723 | identical |
| Metal, fast math on (pinned Cycles options) | -87.1811829 | 1.37307603e-38 | 0x009583c6 | 9.97140401e-37 | 0x03a9a797 | +1.04e-5 relative |
| Metal, fast math off | -87.1811981 | 1.37305417e-38 | 0x0095832a | 9.97124796e-37 | 0x03a9a6e9 | -5.2e-6 relative |

- Both values stay normal on Metal (0x0095... is above FLT_MIN, 0x00800000), so this path is **not** flushed. Metal is
  CPU-like here, not zero.
- The `cycles` and `cprobe` chains give bit-identical results on Metal in each mode, so the difference from the CPU
  is not caused by the built-in `dot` or `normalize`.
- The Metal argument already differs from the CPU in the last bits (-87.1811829 and -87.1811981 against
  -87.1811905); `expf` amplifies that to 102 ulp (fast) and -54 ulp (safe) of the result.
- For the exact argument -87.1811905, the correctly rounded `expf` is 0x00958360 (the CPU value). Metal gives
  `fast::exp` 0x0095835e (2 ulp low) and `precise::exp` 0x0095835f (1 ulp low); the default-namespace `exp` follows
  the mode (fast with fast math on, precise with it off).
- With FPCR.FZ set, the Mac CPU's output for all nine sigmas is identical to the normal run: on these inputs no
  intermediate of the chain is subnormal, so flushing has no effect until a later product falls below FLT_MIN.
- Every other sigma is normal and CPU-like on Metal too; sigma 0.01 and 0.2 give 0 on every device (the exponential
  underflows far below the subnormal range).

**3. The gate's own velvet values (`velvet-gate-paths.txt`, closure kind 16).**

| Path | Normal-CPU value | Kind | Metal value * 1 | Weight | CPU value * weight | Metal value * weight |
|---|---|---|---|---|---|---|
| 534 | 1.54958639e-38 (0x00a8bc2a) | normal | same bits | 0.5 | 7.74793195e-39 (subnormal) | 0 |
| 549 | 8.23442718e-38 (0x01e02995) | normal | same bits | 0.5 | 4.11721359e-38 (normal) | 4.11721359e-38 (same bits) |
| 1566 | 9.84552301e-42 (0x00001b72) | subnormal | 0 | 0.5 | 4.92276151e-42 (subnormal) | 0 |
| 2055 | 1.13863109e-38 (0x007bfc67) | subnormal | 0 | 0.8 | 9.10904818e-39 (subnormal) | 0 |

Identical in both Metal modes. Every other velvet value in that file (paths 81, 1650, 1659, 2655, 2922 and the
repeated rows) is subnormal and is therefore 0 in any Metal arithmetic. The FTZ+DAZ CPU column of
`velvet-gate-paths.txt` is 0 for all of them, including the two normal values (534 and 549), so on those paths an
earlier intermediate of the chain must be subnormal. Metal flushes such intermediates the same way, but the path
inputs that would show it directly are not in the frozen folder, so the probe cannot evaluate those two chains.

### Limits of this probe

- One GPU (Apple M2 Pro, `Metal 4` family) and one OS (macOS 27.2, whose run-time Metal compiler compiles the
  kernel). Other Apple GPUs or later macOS releases are not covered.
- It is a standalone kernel, not Cycles' compiled path-tracing kernel; the film-level behaviour of the real Metal
  build is in the oracle renders of `velvet-subnormal` and `bsdf/velvet` in this folder.
- The value `9.97129997e-37` that the input README names as the gate's normal-CPU value for the `velvet_chain.c`
  path does not appear in `velvet-gate-paths.txt`, so the probe's chain cannot be tied to one row of that file.

### Reproducing

```sh
cd native/evidence/cycles-oracles-macos-metal-comparison-20261031/probes
SDK=$(xcrun --sdk macosx --show-sdk-path)
xcrun --sdk macosx clang -isysroot "$SDK" -fobjc-arc -O2 -Wall -Wno-deprecated-declarations metal_velvet_probe.m \
  -framework Metal -framework Foundation -o /tmp/metal_velvet_probe && /tmp/metal_velvet_probe
I=../../metal-comparison-linux-x64-20261031/probes/velvet_chain.c
xcrun --sdk macosx clang -isysroot "$SDK" -O2 -ffp-contract=off -fno-reciprocal-math $I -o /tmp/velvet_chain && /tmp/velvet_chain
xcrun --sdk macosx clang -isysroot "$SDK" -O2 -ffp-contract=off -fno-reciprocal-math $I fz_arm64.c -o /tmp/velvet_chain_fz && /tmp/velvet_chain_fz
```

`probes/environment.txt` records the Mac, Xcode, SDK and compiler, and the SHA-256 of the sources and of the three
executables (the executables themselves are not evidence and are not committed).

## Contents

- `oracle/<scene>/<backend>/`: `film-0.f32`, `film-1.f32` (scene-linear RGBA float32, little-endian, lower-left
  origin, 32 x 24, 8 samples) and `meta.json`; `oracle/<scene>/<backend>.output.txt`, the runner's output.
- `warmup/<backend>/`: each Metal backend's first, kernel-compiling render.
- `run.json`: the render record (backends, scenes, render seconds, `kernelCompileSeconds`, `comparisonsPassed`).
- `comparisons/`: `render_oracles.py`'s three-way report per scene (CPU, Metal, MetalRT).
- `compare/`: the pairwise reports against the frozen Linux x64 films and the ARM64 CPU films (tables above).
- `probes/`: `metal_velvet_probe.m` and `fz_arm64.c` (sources), their outputs and `environment.txt` (machine,
  Xcode, SDK, compiler and SHA-256 of the sources and executables).
- `environment/`: working-tree status and SHA-256 of the modified files at the start of the render, start and end
  times, walls and exit codes, the build, render and compare logs, the build records (`build/`), and the scripts that
  ran the renders and comparisons (`run.sh`) and wrote the tables (`tables.py`).

Executables and libraries are not evidence and are not copied here; their SHA-256 are above and in
`probes/environment.txt`.
