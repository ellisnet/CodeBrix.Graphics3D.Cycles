# Testing on an Intel Mac (osx-x64, WGSL on WebGPU)

Copyright (c) 2026 Jeremy Ellis and contributors

> **MANDATORY TEST RULES (from 2026-10-29; they apply to every run of this
> procedure and override anything below, including the historical records).**
>
> 1. **WebGPU binaries: ONLY from the `CodeBrix.WebGpu.Native.MitLicenseForever`
>    1.0.300.210 NuGet package.** Load the `codebrix_wgpu` binary from that
>    package's `runtimes/<rid>/native/` folder (restored into the NuGet cache)
>    and from nowhere else. **Do NOT** test with WebGPU binaries from any other
>    source (a local build, a committed payload, another package, Dawn, an
>    upstream wgpu-native release, a system library) or from any other version
>    of this package, earlier or later. Runs with other WebGPU binaries are not
>    evidence and must not be recorded as results.
> 2. **.NET SDK: ONLY version 10.0.403.** Every build, test and tool run of the
>    .NET libraries and tools in this repository uses the .NET 10 SDK
>    10.0.403 - **absolutely no earlier version**. Check with
>    `dotnet --version` (it must print `10.0.403`) before starting and record
>    the output with the run.
> 3. **Linux x64 reference: ONLY from the Linux x64 machine.** Linux x64 and
>    Android testing and validation are done on the dedicated Linux x64 machine
>    only. Every comparison against Linux x64 uses the newest
>    `native/evidence/linux-x64-reference-<date>/` set (now
>    `linux-x64-reference-20261030`); device-gate exchanges go through
>    `native/tools/reference_exchange_handoff.py` with that machine as the Linux
>    side. **Do NOT** produce Linux x64 references in Docker, WSL or any other
>    machine; `linux-x64-docker-reference-20261027` is history only.

Steps carry a status marker: **VALIDATED** (run on the Intel Mac; results
recorded at the step), **UNTESTED** (not yet run), **MISSING** (something that
does not exist yet) or **UNKNOWN** (a fact that needs hardware or input not at
hand, with the check that settles it). The recipe and its decisions are in
[native/host/MACOS_X64.md](../native/host/MACOS_X64.md).

The first Intel Mac run was made on 2026-10-26, at the level of the win-x64
validation of the same day ([TESTING-WINDOWS-X64.md](TESTING-WINDOWS-X64.md),
"Revalidation with the W1 fixes"): host gate with driver coverage and the
device gates, managed tests including `GpuRenderTests`, the oracle corpus, the
bench scenes, the glTF client, staging and the licence audit. No Linux x64
input (overlay hashes, host-gate output, CPU films) was available on the Mac;
the comparisons that need it are listed under "Not covered".

**Reference device (2026-10-27).** This Intel Mac mini is a reference device
(native/DEVICE.md, "Reference devices and the publish rule"): everything ported
so far must pass on it, judged against the correctly rounded Linux x64 reference,
before a release is published. Status on 2026-10-27: **every item passes** -
the host gate (with the Linux references), every device gate (the three that
fail against Apple's libm pass against glibc, step 7), all 56 WGSL oracle and
bench films against the Linux CPU films (step 8), the managed tests and the
glTF client. The Linux x64 reference data of those runs was
`native/evidence/linux-x64-docker-reference-20261027/` (produced in Docker on
this Mac with the dedicated Linux machine's toolchain). **From 2026-10-29 it is
replaced by `native/evidence/linux-x64-reference-20261029/`**, made on the Linux
x64 machine; its CPU films are bitwise identical to the Docker set's. From
2026-10-30 the newest set is `native/evidence/linux-x64-reference-20261030/`
(the same CPU films for every scene both sets have, plus the feature-combination
scenes). The next run uses the newest set, and the device-gate exchanges go through
`native/tools/reference_exchange_handoff.py` instead of Docker (step 7).

The final runs (2026-10-27: gate `macos-x64-validation-7`, corpus
`macos-x64-7`, bench `macos-x64-bench-3`, the staged payload and the bundle)
are on commit 0e95e5ce3 - the runtime pin moved to
`CodeBrix.WebGpu.Native.MitLicenseForever` 1.0.300.210, which carries the M1
switch - with the Windows W2 fix (f2a841ce4) and the host calling
`wgpuCodebrixSetMetalPreciseMath(true)` on Apple (M1, resolved). Earlier runs
(`-3` to `-6`, on 1.0.290.1433 with Metal's fast math) are kept under
`native/work/` for comparison; where their results differ, both are given.

## Purpose

The osx-x64 product is Cycles with the CodeBrix WGSL/WebGPU device, the same
device as Linux, Android and Windows, on the unchanged shipped
CodeBrix.WebGpu.Native runtime, which reaches the GPU through Metal on macOS.
(Cycles' own Metal device is the osx-arm64 product only.) A run establishes:

1. that the osx-x64 recipes build the dependency prefix, the host, the gates
   and the oracle runner from the reviewed snapshots, isolated from Homebrew;
2. osx-x64 GPU evidence: the Linux host-gate list passing on the Intel Mac's
   GPU through the runtime's Metal route, against the CPU kernels;
3. CPU-x64 oracle films from Apple Clang and WGSL films for the oracle corpus;
4. a reviewed `native/binaries/osx-x64` payload with provenance.

There is no Metal oracle on the Intel Mac: Cycles' Metal device at the pinned
revision accepts only Apple GPUs and skips Intel and AMD GPUs by name, so the
Metal films come from Apple Silicon only.

## Test machine (2026-10-26)

| Item | Value |
|---|---|
| Machine | Mac mini (2018), Intel Core i7-8700B (6 cores, 12 threads; AVX2 present), 32 GB |
| GPU | Intel UHD Graphics 630, 1536 MB, "Metal Support: Metal 3"; the only adapter |
| macOS | 15.8.2 (24H232); native x86_64 (no Rosetta: `sysctl.proc_translated` absent) |
| Toolchain | Xcode Command Line Tools 26.3 only (no Xcode app): SDK 26.2, Apple clang 17.0.0 (clang-1700.6.4.2) |
| Tools | CMake 4.4.3, Python 3.15.2 (python.org framework build), zstd 1.5.7 (Homebrew, `/usr/local`), .NET SDK 10.0.403 (`RID: osx-x64`) |
| Homebrew | `/usr/local`, 197 entries in `/usr/local/include` (boost among them), `nasm` in `/usr/local/bin` |

The machine record is `native/work/macos-x64-identity/` (step 1).

## WebGPU backend on Apple platforms: Metal **VALIDATED**

`native/src/webgpu_device.cpp` requests `WGPUInstanceBackend_Metal` under
`__APPLE__` (the shipped runtime's macOS builds contain wgpu's Metal backend
and no Vulkan backend). Confirmed on the Intel Mac: the adapter report names
backend type 5 (Metal), "Intel(R) UHD Graphics 630", the only enumerated
adapter. Metal reports no PCI vendor or device ID (both 0) and empty vendor and
architecture strings; driver workarounds for Intel GPUs on Metal therefore
match the device name (M2).

## Prerequisites and pins

| Item | Value |
|---|---|
| Machine | An Intel Mac that runs macOS 15 or later (the deployment target). |
| Cycles | `a456b761034dda42c32eef9f4aae0fa5a5c9f604`, the filtered snapshot in `third_party/cycles` |
| WebGPU runtime | `CodeBrix.WebGpu.Native.MitLicenseForever` 1.0.300.210; package SHA-256 `ddfd92f8e9ce0f4348b0b079b856158608552184d3255c26a3e35a87b73b8b7b`; `runtimes/osx-x64/native/libcodebrix_wgpu.dylib` SHA-256 `840a106d54fe479c255ecb98d51cee192a9044c3d0411fc1535f9ff0b8abbb82` (x86_64, minimum macOS 11.0, ad-hoc signed; 257 exports, codebrix contract 4, `wgpuCodebrixSetMetalPreciseMath` included). The first runs used 1.0.290.1433 (`9ceea795...`, dylib `3a3abcf9...`). |
| Xcode | The Xcode Command Line Tools are enough (validated); Xcode works too. Versions are recorded by the recipes. |
| Homebrew (x64) | In `/usr/local`. Used only for tools; every recipe excludes `/usr/local` and `/opt/homebrew` from searches and rejects any reference to them. |
| CMake | 3.24 or newer (4.4.3 validated). |
| Python | 3.12 or newer (3.15.2 validated). Xcode's Python is too old. |
| .NET | .NET 10 SDK **10.0.403 only** (no earlier version) for x64 (`dotnet --version` must print `10.0.403`; `dotnet --info` must report `RID: osx-x64`). |
| zstd | For the oracle bundle. |
| Deployment target | macOS 15.0 for every built Mach-O (the runtime declares 11.0). |

## Ordered steps

Run every step from the repository root. Walls are from the 2018 Mac mini above.

### 1. Record the machine **VALIDATED**

```sh
mkdir -p native/work/macos-x64-identity
cd native/work/macos-x64-identity
sw_vers > sw_vers.txt
system_profiler SPHardwareDataType SPDisplaysDataType > system_profiler.txt
sysctl -n machdep.cpu.brand_string > cpu.txt
sysctl -n machdep.cpu.features machdep.cpu.leaf7_features > cpu-features.txt
sysctl -n sysctl.proc_translated > rosetta.txt 2>&1   # must be 0 or absent
xcodebuild -version > xcode.txt 2>&1; xcode-select -p >> xcode.txt
xcrun --show-sdk-version >> xcode.txt; xcrun clang --version >> xcode.txt
cmake --version > tools.txt; python3 --version >> tools.txt; zstd --version >> tools.txt
dotnet --info > dotnet.txt
git -C ../../.. rev-parse HEAD > commit.txt; git -C ../../.. status --porcelain > status.txt
cd ../../..
```

With the Command Line Tools only, `xcodebuild -version` prints "tool
'xcodebuild' requires Xcode"; that is expected (the recipes record the CLT
package version instead, B2). `sysctl.proc_translated` is absent on Intel
Macs ("unknown oid"), which means native.

### 2. Restore and verify the WebGPU runtime **VALIDATED**

```sh
dotnet restore research/GpuProbe/GpuProbe.csproj
P=~/.nuget/packages/codebrix.webgpu.native.mitlicenseforever/1.0.300.210
shasum -a 256 $P/*.nupkg $P/runtimes/osx-x64/native/libcodebrix_wgpu.dylib
otool -L $P/runtimes/osx-x64/native/libcodebrix_wgpu.dylib
lipo -info $P/runtimes/osx-x64/native/libcodebrix_wgpu.dylib
codesign -dv $P/runtimes/osx-x64/native/libcodebrix_wgpu.dylib
```

Both SHA-256 values match the pins; `otool -L` lists only `/usr/lib` and
`/System/Library` entries (QuartzCore, Metal, Foundation, CoreFoundation,
libobjc, libiconv, libSystem); `lipo` reports x86_64 alone; `codesign` reports
`Signature=adhoc`.

### 3. Bring the Linux x64 reference **VALIDATED** (Docker, 2026-10-27; superseded)

From 2026-10-29 the reference comes from the Linux x64 machine only: use
`native/evidence/linux-x64-reference-20261030/` (overlay hashes in
`reference/linux-overlay-hashes.json`, the full host gate in `host-gate/`, the
oracle and bench films in `oracles/`). No Docker run. The 2026-10-27 record,
when it was made on this Mac in a container (see
`native/evidence/linux-x64-docker-reference-20261027/README.md`), follows. On
the Linux x64 machine, after a maintained Linux build of the same commit:

```sh
python3 native/tools/build_cycles_macos_x64.py --write-overlay-hashes native/work/cycles-overlay \
  --output native/work/linux-overlay-hashes.json
```

plus `python3 native/tools/host_gate.py --output <dir> [--cpu-only]` and
`render_oracles.py --backend cpu-x64-linux` for the corpus and the bench bundle,
and the Linux side of the device-gate scene exchange (step 7).

**Result (2026-10-27, Docker on this Mac):** Debian 13, GCC 14.2.0-19, clang
19.1.7, CMake 3.31.6, glibc 2.41 - the dedicated Linux machine's toolchain.
The Linux build passed; 19 overlay hashes; the Linux host gate's CPU steps
passed (`--cpu-only`: no GPU or .NET in the container; 1,568 comparisons, 0
practical failures); `cpu-x64-linux` films for the whole corpus and the bench.
The Mac build with `--reference-overlay` then reported **19 of 19 overlay files
identical**: Apple Clang and Linux clang 19 generate the same WGSL kernels.

### 4. Build **VALIDATED** (five recipe fixes, B1-B5)

```sh
python3 tools/build_macos_x64.py --jobs 12 [--reference-overlay native/work/linux-overlay-hashes.json]
```

Dependency prefix: 17 components (png skipped by design), 690 compiler
dependency files scanned, no Homebrew header or library referenced; 71 dylibs.
First complete build about 10 minutes (dependencies about 8.5); a host-only
rebuild (`--skip-dependencies`) about 1.5 minutes. Host: 48 exports, x86_64
alone, `minos 15.0`, no `Metal.framework` on the host, 263 compiler dependency
files scanned, one compiler warning (an unused variable in a gate source). The
host build now also builds the six device gates win-x64 added (`ao_render`,
`ao_state`, `light_tree`, `mnee`, `mnee_render`, `shadow_catcher`).

The build would not complete without these changes, all made in the osx-x64
recipes only (the osx-arm64 recipe's hash is part of its prefix identity):

- **B1. Homebrew headers in Apple Clang's default search (CMake 4).** CMake 4
  no longer picks a sysroot on macOS. Without `-isysroot`, Apple Clang searches
  `/usr/local/include` before the SDK - 197 Homebrew entries here - and the
  recipe's own check (`check_compiler_search`) stopped the build, as designed.
  Fix (`tools/dependencies/build_macos_x64.py`, `tools/build_macos_x64.py`):
  `SDKROOT` in the build environment and `CMAKE_OSX_SYSROOT` for every CMake
  configure (dependencies, host, oracle runner). With either, the default list
  holds only SDK paths (the SDK's own `usr/local/include` does not exist).
- **B2. Command Line Tools without Xcode.** The recipe's identity recorded
  `xcodebuild -version`, which fails without Xcode. It now records the CLT
  package version (`pkgutil --pkg-info=com.apple.pkg.CLTools_Executables`) when
  Xcode is absent.
- **B3. libjpeg-turbo found Homebrew's nasm.** CMake picked
  `/usr/local/bin/nasm` for libjpeg-turbo's SIMD code and the isolation check
  rejected the configured cache. The osx-x64 profile now sets `WITH_SIMD=OFF`
  for jpeg, as the Windows recipe does ("the scalar JPEG fallback on every
  RID"); libjpeg is not in the macOS payload's load closure either.
- **B4. `ccl::fmaxf` exists only on Windows.** `native/cycles/mnee_reference.cpp`
  called `ccl::fmaxf` (the win-x64 fix for an ambiguity against the UCRT's
  `fmaxf`), but Cycles declares `ccl::fmaxf` only under `_WIN32`
  (`util/math_base.h`), so the MNEE gate failed to compile on macOS - and would
  on Linux and Android. Fix: a helper inside `namespace ccl`
  (`mnee_reference_fmaxf`) whose unqualified `fmaxf` resolves exactly as the
  upstream MNEE kernel's does on each platform. **UNTESTED** on Windows and
  Linux since (it must still build there).
- **B5. x86_64 output is not signed by the linker.** Apple's linker ad-hoc
  signs arm64 output by itself but leaves x86_64 output unsigned; the host
  gate's `codesign --verify` failed on the host library. Fix:
  `add_link_options("LINKER:-adhoc_codesign")` in
  `native/tools/inject_cycles_macos_x64.cmake`. The stager re-signs its copies
  ad hoc in any case, so the shipped payload is unchanged by this.

`FMT_CONSTEVAL=` is still needed: the injection keeps it (Apple clang 17 builds
with it; not retried without).

### 5. Binary review (inside the host gate) **VALIDATED**

Step 6 starts with it: x86_64 alone; `minos 15.0`; linked libraries only from
`/usr/lib`, `/System/Library`, the dependency prefix by `@rpath` and
`@rpath/libcodebrix_wgpu.dylib`; no `Metal.framework` on the host; the 48
exports equal `native/host/cycles_host.h`; no image codec library in the load
closure; signatures verify (after B5).

### 6. Host gate **VALIDATED**

```sh
python3 native/tools/macos_x64_host_gate.py --output native/work/macos-x64-validation-<n> \
  [--references <the passed Linux host_gate.py output>] [--device-gates]
```

`native/tools/macos_x64_host_gate.py` was first brought to the level of
`windows_host_gate.py`: the six new device gates, the driver-coverage step (the
same four oracle scenes, CPU against GPU through the oracle runner, practical
criterion required), the Linux rule for `run.3` (practical required, strict
recorded; native/DEVICE.md, "Merge wave 7"), a check that the adapter is on
Metal, and the diagnostic-run rule (a run with any opt-in diagnostic set never
passes).

**Result** (`native/work/macos-x64-validation-7`, the payload's gate:
runtime 1.0.300.210 with precise math, `--device-gates`, without
`--references`): **passed**. 28 films compared, 0 failed, 0 same-host bitwise
failures, 0 GPU practical failures, maximum GPU error 1.5e-5; 51 strict
differences, all in the four ABI light films, where the strict criterion is
reported, not required (144 with fast math). Driver coverage: all four pass
(smooth shading 2.4e-7, motion with a normal map 1.2e-7, glass and area light
1.1e-5, light tree 3.5e-6). The device report shows `metalPreciseMath: true`
(the gate requires it), the driver workaround `intel-metal-surface-workgroup`
(M2) and no diagnostic. **With `--references`** on the Linux CPU-only gate
(`native/work/macos-x64-validation-8`, the payload's gate; same binaries):
**passed**, 36 films (the 28 plus 8 cross-platform), 0 failed. The Mac CPU
lifecycle films agree with Linux glibc within the strict criterion (maximum
1.5e-8, one bitwise identical), the 64-sample and concurrency films within
1.5e-8. The runs on 1.0.290.1433 (`-3`, then `-5` after the AMD
precaution and the Intel adapter preference, `-6` after the W2 fix) passed with
the same film results; the `image` device gate passes in every run. First full
run 8.6 minutes. GPU step walls: under
10 s each (the GPU lifecycle 8 s); no GPU timeout came close.

The first gate runs found the two Mac findings: `-1` stopped at the binary
review (B5); `-2` (before M2's workaround) passed the 28 films but failed
driver coverage on motion with a normal map (126 practical failures, maximum
error 3.05, two GPU renders in one process 17.5 apart).

**Resource path:** the host points Cycles' resource path at a `codebrix-cycles`
directory beside the library on every Apple RID; the osx-x64 payload has none,
and no CPU or GPU step reported a missing path. Settled.

### 7. WGSL device gates **VALIDATED** (against Linux x64 glibc)

Run by step 6 with `--device-gates`, each against the CPU reference computed in
the same process (Apple's libm). Where that fails, the authoritative verdict is
the device-scene flow against Linux x64 glibc (below).

**From 2026-10-29 the Linux side is the Linux x64 machine, not Docker.** Run the
exchange as the four-step hand-off of TESTING-WINDOWS-ARM64.md, "Device-gate
exchange with the Linux x64 machine" (on this Mac with `python3`; the gates are
found in `native/work/macos/osx-x64/host/build/bin`). The Docker route below is
the 2026-10-27 record; do not use it for new evidence. Then, **one command** ran
all three of those judgements on this Mac, with the Linux side in Docker:

```sh
python3 native/tools/macos_x64_reference_exchange.py --output native/work/macos-x64-exchange-<n>
```

It builds or starts the Linux reference container
(`native/tools/linux_reference/Dockerfile`: the dedicated Linux machine's
toolchain, plus Mesa lavapipe for device creation and the RDTSCP shim), copies
this working tree's source in (git-visible files, uncommitted work included),
rebuilds the three Linux gates, runs the `light_tree` and `microfacet_render`
scene exchanges (with the render stage judge) and the `mnee` stage-state
exchange, and writes `summary.json` (verdicts, both sides' gate binary hashes,
image identity, toolchain); exit status 0 only when all three pass. Needs Docker
Desktop running and the Mac host built. **Measured 2026-10-27:** all three pass;
971 s from nothing (image, container, Linux dependency prefix and gates built
first), 325 s warm (`native/work/macos-x64-exchange-1` and `-2`). The Linux gate
binaries the fresh container built were byte-identical to the earlier hand-made
container's (`light_tree` `89c47f58...`, `microfacet_render` `a0ffc783...`, `mnee`
`dac82d8e...`).

| Result (`validation-7`, precise math) | Gates |
|---|---|
| Passed (19) | device, camera, closest, background, svm_world, shader_data, svm_surface, attribute, normal, image, surface, microfacet_svm, microfacet_bounce, render, ao_render, ao_state, mnee_render, shadow_catcher; `atomic_film` (contract and memory accounting pass; scored as failed only because its row has no top-level `passed`, as on Windows and Apple Silicon) |
| Failed (3) | `microfacet_render` (a stage-6 `mis_ray_pdf`, as on win-x64), `light_tree` (one probe sample: 1.455756 against 1.454949), `mnee` (a walk outcome: 4/40 against 4/38) |

**Metal's fast math caused four of the earlier failures.** With fast math
(runtime 1.0.290.1433, `validation-3` to `-6`) seven failed: the three above
and `closest` (180 decision and 4,319 bit mismatches), `mnee_render`, and - Mac
only - `svm_surface` and `microfacet_svm` (the multiplexed `multiscatter`
closure field, 0.008793 against 0.008947). With precise math those four pass.
(An earlier note here said the seven were unchanged with fast math off. That
interposer run was never active in the gates: they were started through
`caffeinate`, an Apple system binary, and System Integrity Protection strips
`DYLD_INSERT_LIBRARIES` from protected binaries' environments, so the
interposer never loaded. The oracle-runner experiments, started directly, were
valid.)

These gates judge the GPU against a CPU reference computed in the same process,
here with Apple's libm, which is not glibc's correctly rounded libm the GPU
reproduces. By the validation reference policy (native/DEVICE.md) such an
in-process reference is a diagnostic, as with the UCRT on Windows and bionic on
Android; the judgement of the three remaining failures needs the scene exchange
with a Linux x64 reference (`CODEBRIX_CYCLES_SCENE_DUMP` here, the reference
computed on Linux, `CODEBRIX_CYCLES_REFERENCE_IMPORT` here). Done on
2026-10-27 (`native/evidence/linux-x64-docker-reference-20261027/exchange/`):

| Gate | Against Apple libm (in process) | Against Linux x64 glibc (authoritative) |
|---|---|---|
| `light_tree` | fails (one probe sample) | **passes**, all 7 cases (scene exchange: the Mac's dumped tables, the glibc reference computed from them, the Mac GPU run with `CODEBRIX_CYCLES_REFERENCE_IMPORT` and `CODEBRIX_CYCLES_SCENE_VERIFY`) |
| `microfacet_render` | fails (a stage-6 `mis_ray_pdf`) | **passes**: the image against the glibc film, and the stage judge of all 149 recorded GPU stages (140,006 paths, 11.6 million integer and 16.5 million float checks; worst float at 5.8% of the practical tolerance) |
| `mnee` | fails (a walk outcome: 4/40 against 4/38) | **passes**: the new MNEE stage-state exchange (`mnee_gate.cpp`, 2026-10-27; below). The Linux judge ran all 1,131 recorded checks (23,757 paths, 52 with sampled caustics; 2.36 million exact integer checks including every walk outcome and iteration count, 5.48 million float checks within the practical criterion) on the Mac's scene and inputs. The Mac pass counted 3 local differences against Apple's libm (the walk outcome above among them), all agreeing with glibc on the GPU's side. |

**The MNEE stage-state exchange** (`native/cycles/mnee_gate.cpp`, added
2026-10-27; DEVICE.md open item 5 for this gate). The gate is CPU-driven
lockstep, so its record carries each check's inputs (the device CPU's path
rows) and every value read back from the GPU, in order:

```sh
# device pass (Mac): runs the gate on the GPU, records, writes the scene tables it used beside the record
CODEBRIX_CYCLES_STAGE_RECORD=$PWD/mnee.stages.bin native/work/macos/osx-x64/host/build/bin/codebrix_cycles_mnee_gate run/run
# Linux judge (glibc): the same checks with this host's CPU and the recorded GPU reads, no GPU work
python3 native/tools/android_reference.py stage-judge --bin native/work/cycles-build/bin \
  --scenes mnee.stages.bin.scenes --stages mnee.stages.bin --gate mnee --output mnee.stage-judge.json
```

In the device pass the local comparisons are a counted diagnostic
(`localDifferencesDiagnostic`); the judge applies them unchanged and fails if
this host's CPU makes a decision the device CPU did not (it would ask for a GPU
read the record lacks). Rows travel as their non-zero 64-byte blocks (155 MB for
the whole gate). The record pass writes its own scene tables because Cycles
leaves some `KernelObject` words uninitialized (`velocity_scale` and alignment
padding of objects without volume motion), so two preparations of one scene are
not byte-identical. Without the new variables the gate is unchanged (its default
run still reports the Apple-libm walk outcome).

The provenance per reference records how the Mac-prepared scene tables differ
from the ones Linux prepares itself (for example 18 words of `light_tree_nodes`
in light-tree case 4): Apple's libm reaching the GPU's inputs through scene
preparation, which is why the reference is computed from the Mac's own tables.

Before M2's workaround, `ao_render` and `ao_state` failed as well; with it they
pass.

### 8. Oracle corpus **VALIDATED** (against Linux x64 glibc)

```sh
python3 native/oracle/render_oracles.py --output native/work/oracles/macos-x64-<n> \
  --runner native/work/macos/osx-x64/oracle-build/codebrix_cycles_oracle \
  --host-library native/work/macos/osx-x64/host/build/lib/libcodebrix_cycles_host.dylib \
  --backend cpu-x64-macos --backend cpu-x64-macos-sse42 --backend wgsl-macos-x64
```

4 minutes. **Result** (`native/work/oracles/macos-x64-7`, the payload's run
with precise math, bundled in step 12; the same result in `macos-x64-3`, `-5`
and `-6` on the earlier runtime):
56 scenes, **55 pass every rendered backend pair**.

- `cpu-x64-macos` and `cpu-x64-macos-sse42` agree bitwise on every scene: the
  fixed-baseline CPU oracle (SSE4.2 kernels only, no FMA contraction, no
  reciprocal math) compiles and runs as designed on the Intel Mac.
- `linux-gates/closed-volumes` and `linux-gates/world-volume`: refused on WebGPU
  ("WebGPU surface data passes not implemented"), the documented render-time
  limit; compared on the CPU only.
- **Against the Linux x64 glibc CPU films (authoritative), all 56 WGSL films
  pass** - the 54 rendered corpus scenes and the two bench scenes; maximum
  error 9.2e-5 (`bench_01-hdri`), 0 practical failures
  (`native/evidence/linux-x64-docker-reference-20261027/comparisons/SUMMARY.json`).
- **`linux-gates/triangle-lighting-2` is resolved: it was the reference.** Against
  the Mac's own CPU film the WGSL film has 3 practical failures (worst pixel
  24,15 R: CPU 0.04195, GPU 0.03668 - the same values win-x64's D3D12 and Vulkan
  films have); against the Linux glibc film it passes, and the Mac CPU film
  itself fails against glibc there (3 practical failures, maximum 5.0e-3). It is
  the only scene where the Mac CPU film and glibc disagree. **For Windows:** its
  WGSL films match the Mac's at that pixel, so they very likely pass against
  glibc too; the UCRT CPU film is then the outlier.
- Before M2's workaround (`macos-x64-1`), all 16 motion-blur scenes
  (`deforming-motion-0`..`7`, `object-motion-0`..`7`) and `ao/ao-node` failed
  (maximum errors 0.1 to 29.6; GPU repeats up to 43 apart). With it all pass.

Against the Apple Silicon evidence (`native/evidence/cycles-oracles-macos-20261023`)
for the four starter scenes, the Intel Mac's CPU and WGSL films have 0
practical failures against the arm64 CPU oracle (largest 7.3e-5,
glossy-sphere-point-light); Cycles' Metal device shows its known 4 practical
differences on `textured-plane`.

**Bench scenes** (from a bundle `make_bench_bundle.py` generated and verified
on this Mac - the scene documents byte-identical to the committed ones, all 13
raw files matching the manifest). **With precise math both pass**
(`native/work/oracles/macos-x64-bench-3`): 0 practical failures against this
Mac's CPU, maximum error 7.0e-5 (`bench_01`) and 9.2e-5 (`bench_01-hdri`).
With fast math (`macos-x64-bench-1` and `-2`) both failed (33 practical
failures, maximum 1.0e-3; 77, maximum 1.2e-3) - nearly the counts win-x64
records (33 and 74/77). So on the Mac that "bench finding" was Metal's fast
math, not path divergence. **For Windows:** it suggests looking at the D3D12
and Vulkan drivers' floating-point behaviour (the same probe shader as M1)
before blaming the textures.

### 9. Application run: the glTF example client **VALIDATED**

```sh
H=native/work/macos/osx-x64/host/build/lib/libcodebrix_cycles_host.dylib
dotnet run --project tools/gltf/CycleGltfRender -c Release -- \
  --write-sample native/work/macos-x64-gltf/sample.glb --sample-sun
dotnet run --project tools/gltf/CycleGltfRender -c Release -- native/work/macos-x64-gltf/sample.glb \
  --output native/work/macos-x64-gltf/film.bin --compare --native-library $H
```

GPU against CPU: 12,288 floats, maximum error 4.8e-7, 0 practical failures, 88
strict differences: practical PASS. GPU render 4.3 s (pipeline compilation
included), CPU 0.07 s; 7 s in all.

### 10. Managed tests **VALIDATED**

```sh
dotnet build CodeBrix.Graphics3D.Cycles.slnx -c Release
dotnet tests/CodeBrix.Graphics3D.Cycles.Tests/bin/Release/net10.0/CodeBrix.Graphics3D.Cycles.Tests.dll \
  -noclass CodeBrix.Graphics3D.Cycles.Tests.GpuRenderTests
dotnet tests/CodeBrix.Graphics3D.Cycles.Tests/bin/Release/net10.0/CodeBrix.Graphics3D.Cycles.Tests.dll \
  -class CodeBrix.Graphics3D.Cycles.Tests.GpuRenderTests -showLiveOutput
```

The native-host tests had skipped on Intel Macs: `RepositoryPaths` had no
osx-x64 host. It now resolves
`native/work/macos/osx-x64/host/build/lib/libcodebrix_cycles_host.dylib` (the
runtime beside it). The tests themselves already expected WebGPU (not Metal) on
osx-x64. Release build: 0 warnings. **180 non-GPU tests, 0 failed, 0 skipped**
(10.7 s); **`GpuRenderTests` 14 of 14** (117 s), on the in-tree osx-x64 host -
the same counts as win-x64.

### 11. Stage and audit **VALIDATED**

```sh
python3 native/tools/stage_macos_x64_binaries.py --gate native/work/macos-x64-validation-7 \
  --oracles native/work/oracles/macos-x64-7
python3 tools/licensing/audit.py
python3 tools/licensing/notices.py --check
```

`native/binaries/osx-x64/`: 35 files (16 binaries; runtime 25 MB, unstripped
34 MB, symbols 13 MB) and `provenance.json` with `validation.hostGate` from
`macos-x64-validation-7` and the runtime 1.0.300.210 (`840a106d...`) in
`webGpuRuntime` (restaged after each host change - the AMD precaution, the
Intel adapter preference, the W2 fix, M1 and the new runtime; the stager
refuses to overwrite a payload, so each earlier, uncommitted one was removed
first). The temporary-folder render from the payload plus the
pinned runtime succeeded on CPU and WebGPU. `audit.py`: `"passed": true` (36
reviewed components); `notices.py --check`: current.

### 12. Bundle **VALIDATED**

```sh
python3 native/oracle/render_oracles.py --bundle native/work/oracles/macos-x64-7
```

`cycles-oracles-macos-x64-20261027.tar.zst` (729 files, SHA-256
`6456c7c8c8f50060086cf9876cb283347e5938078f7fa00b118bb3c932bf4952`,
verified; it replaces uncommitted bundles of earlier runs); its extraction is
the new evidence directory `native/evidence/cycles-oracles-macos-x64-20261027/`.
For the Linux machine: that bundle, `native/work/macos-x64-identity/`,
`native/work/macos-x64-validation-7/`
and `native/binaries/osx-x64/provenance.json`.

## Findings

### M1. The WebGPU runtime compiled WGSL with Metal's fast math (resolved)

The shipped runtime's Metal backend (wgpu-hal 29.0.3) sets only the MSL
language version and `preserveInvariance` on its `MTLCompileOptions`; Metal's
default is fast math (`mathMode` 2). A probe shader through the shipped
runtime on the UHD 630 (inputs from a buffer, so nothing folds):

| Check | IEEE | Fast math (shipped) | `mathMode` Safe + precise functions |
|---|---|---|---|
| `nan != nan` | true | **false** | true |
| two-sum error term `(1-(1+1e-8))+1e-8` | 1e-8 | **0** | 1e-8 |
| `a*b+c`, no contraction | 0 | -1.49e-8 (FMA) | -1.49e-8 (FMA) |
| `sin(1000)` / `cos` / `exp` / `pow` | correctly rounded | 80 / 29 / 4 / 3 ulps | 1 / 0 / 0 / 1 ulps |

The WGSL device relies on IEEE behaviour (its NaN and validity checks, the
exact-rounding helpers, the precise trigonometry), so fast math is a latent
correctness hazard: the kernels' own `fail()` checks can vanish silently. It
made M2's errors far larger (maximum 29.6 instead of about 0.09), and it caused
the bench scenes' failures and four device-gate failures (step 7, step 8). The source
pragma `#pragma METAL fp math_mode(safe)` restores NaN and reassociation but not
precise functions; nothing tried (options or `#pragma METAL fp contract(off)`)
stops the Intel compiler from contracting `a*b+c` into an FMA, which the
practical criterion tolerates. The proof used a diagnostic interposer
(swizzling the device's `newLibraryWithSource:options:error:`), never shipped.

**Fix.** CodeBrix.WebGpu patch 0010 (a vendored copy of wgpu-hal 29.0.3) adds a
process-wide switch, off by default, and the export
`wgpuCodebrixSetMetalPreciseMath(WGPUBool)`: `MTLMathModeSafe` + precise
functions on macOS 15+, fast math off before. Released in
CodeBrix.WebGpu.Native 1.0.300.210 (osx-x64 built on Apple Silicon). Checked
on this Intel Mac against that published binary: the smoke test 88 of 88 on the
UHD 630, including "a NaN compares unequal to itself" with the switch on, and
the Native tests 166 (138 passed, 28 Vulkan-only skips, 0 failed). Cycles now
pins that runtime and the host calls the export with true on Apple before any
pipeline exists (`native/src/webgpu_device.cpp`); the device report carries
`metalPreciseMath` and `macos_x64_host_gate.py` requires it. Revalidated: step 6
onward. The switch is process-wide: in an application that also renders its own
WGSL through the same runtime, those pipelines get IEEE semantics too once a
Cycles device exists (which WGSL allows).

**Loading the natives from a checkout on this Mac.** Files written by
Sourcetree carry `com.apple.quarantine`; `dlopen` of a quarantined, ad-hoc
signed dylib then waits for a Gatekeeper assessment that never completes in a
background process (the smoke test hung in the kernel's signature check).
The NuGet cache's copy has no such attribute. Remove it from a checkout with
`xattr -dr com.apple.quarantine <repo>` before running a gate or the Native
tests against the checked-in natives.

### M2. Intel Metal compiler: `prepare_surface` wrong with more than one invocation per workgroup (worked around)

**Symptom.** All 16 motion-blur scenes and `ao/ao-node` failed on the GPU, and
were not repeatable: two GPU renders of the same scene differ by up to 43,
while every other scene repeats within accumulation noise (at most 5.7e-6).

**Narrowing** (every step on `linux-gates/object-motion-3` unless named;
CODEBRIX_CYCLES_WGSL_LOAD overrides on the dumped modules):

| Variant | Result |
|---|---|
| shipped (fast math) | 76-84 practical failures, repeats differ by about 1 |
| Metal safe math (interposer) | maximum error 3.05 -> 0.02 on `deforming-motion-1`, still failing and not repeatable |
| conservative tier (workgroup size 1 everywhere), fast or safe math | **exact**: 0 practical, 0 strict, maximum 2.6e-8, repeat 4.7e-10 |
| one module at size 1: background, camera, closest, shadow | each still failing |
| surface module at size 1 | exact |
| `prepare_surface` alone at size 1 | **exact** |
| `prepare_surface` at size 2, 8, 16 or 32 | failing (size 2: 16 practical failures) |
| `ao/ao-node`: also `prepare_surface_raytrace` at size 1 | exact (1.9e-6) |
| motion rotation without acos/sincos; motion transform not interpolated at all | still not repeatable |
| local coefficient tables as `let` instead of `var` | no change |
| cold compile forced by a salt in `prepare_surface` (as W1's workaround does), three runs | every first run fails (78, 88, 96 practical failures; about 15 s instead of about 1 s, so the compile was cold) |
| Metal shader cache emptied (`$(getconf DARWIN_USER_CACHE_DIR)com.apple.metal` moved aside, restored after) | first run fully cold (64.5 s) fails (82); the second, cached, fails alike (85) |

The kernels use no workgroup memory, barriers or subgroup operations, so
invocations of a workgroup do not interact, and workgroups of size 1 still run
in parallel - a true memory race would persist. What remains is the Intel Metal
compiler mis-compiling `prepare_surface` (shader-graph evaluation; not the
motion transform itself) whenever two lanes of a SIMD group are live; the paths
in a group change from run to run (queues are compacted with atomics), hence
the non-repeatable films. Win-x64's Intel Direct3D 12 and Vulkan drivers render
the same WGSL correctly.

**Not a shader-cache problem (unlike W1).** TESTING-WINDOWS-X64.md, W1, is also
an Intel GPU, but there the first compile of new DXIL is correct, only the
driver's cached copy is wrong, and two renders in one process agree. Here a
cold compile is already wrong (both rows above), and two renders in one process
disagree, so a per-compile salt like `intel-d3d12-shader-cache` cannot help;
one invocation per workgroup does. Not yet found: the WGSL construct that triggers it
(the next bisection is inside `svm_evaluate` for the normal-map / texture-
coordinate graph of these fixtures; compare Adreno's F3, the same fixtures,
TESTING-WINDOWS-ARM64.md).

**Workaround: `intel-metal-surface-workgroup`** (`native/cycles/pipelines.cpp`;
native/host/WINDOWS_X64.md, "Driver workarounds"): on an adapter on Metal whose
name contains "Intel", `prepare_surface` and `prepare_surface_raytrace` are
compiled with `@workgroup_size(1)`; dispatch reads the declared size, so the
grid adapts. Listed in the device report; off with
`CODEBRIX_CYCLES_DRIVER_WORKAROUNDS=none`. **Cost** on the UHD 630 (warm GPU
renders, workaround on against off): the glTF sample at 320x240, 32 spp 34.3 to
35.1 s against 34.1 s (1-3%); the textured bench asset at 320x240, 32 spp
20.6 s against 19.1 to 19.2 s (about 7%). The first render after a new
`prepare_surface` variant pays a one-time Metal compile (65 s once for
`bench_01`). Proposed next: find the construct and replace it with equivalent
WGSL every GPU runs (the workaround policy), which would let the workaround go.

### Recipe findings

B1-B5 under step 4. One more: **the osx-x64 host gate and managed tests had
fallen behind win-x64** (no new device gates, no driver coverage, the old
`run.3` rule, no osx-x64 host path in `RepositoryPaths`); brought up to the
Windows level for this run.

## Not covered on this run

- The Linux x64 GPU side of the reference set: the container has no GPU, so the
  Linux host gate ran `--cpu-only` and no Linux WGSL films were made. Nothing
  on the Mac depends on them; the dedicated Linux machine provides them.
- The volume scenes (refused on the WebGPU device, every platform).
- An AMD GPU (Intel Macs with a discrete Radeon, AMD eGPUs). **Policy
  (2026-10-26): not supported, may work.** As a precaution the known-adapter
  table (native/DEVICE.md, "Device tiers"; Apple builds only) puts any adapter
  whose name contains "AMD" in the conservative tier (one invocation per
  workgroup everywhere); the device report gives reason `table`. Exercised with
  `CODEBRIX_CYCLES_TEST_ADAPTER="AMD|Apple|AMD Radeon Pro 5500 XT||0|0"` on the
  UHD 630: conservative tier selected, `object-motion-3` exact against the CPU.
  A Mac with two adapters (Intel + AMD MacBook Pros, 15-inch 2016-2019 and
  16-inch 2019; last sold in October 2021): the default adapter rule now
  prefers an Intel adapter on Apple builds (`native/src/webgpu_device.cpp`),
  whatever order the runtime lists them in, so such a laptop renders on the
  supported Intel GPU with the M2 workaround.
  `CODEBRIX_CYCLES_WEBGPU_ADAPTER` (a diagnostic) still picks any adapter
  explicitly. **UNTESTED** on a two-adapter Mac (none at hand); the
  single-adapter path is unchanged (gates `macos-x64-validation-5` and `-6`).
- Windows and Linux with the W2 and M1 era host changes of this run (the Mac
  changes touch shared files - `pipelines.cpp`, `device_tier.cpp`,
  `webgpu_device.cpp` - behind Apple-only conditions; a rebuild there confirms
  they still build).

## Reference policy and tolerances

The unmodified Cycles CPU kernels are the oracle. Every comparison uses:

- practical criterion, required: `abs(a - b) <= 5e-5 + 2e-4 * abs(b)` for every
  finite float component, where `b` is the reference;
- strict criterion, `abs(a - b) <= 1e-7 + 1e-7 * abs(b)`: reported always, and
  required exactly where the Linux host gate requires it;
- NaN or Inf: always a failure; same-host CPU films: bitwise identical.

Tolerances are never relaxed, and films are never adjusted to match another
backend. A disagreement is a finding to report with its reproducer. Since
2026-10-27 the Intel Mac mini is a reference device (native/DEVICE.md,
"Reference devices and the publish rule"): the Linux x64 glibc reference
decides, and the Mac's own CPU reference (Apple libm) is a diagnostic.

## Oracle bundle layout

As for the Apple Silicon oracles:

```
oracle/<scene id>/<backend>/
  film-0.f32, film-1.f32 ...   scene-linear RGBA float32, little-endian, lower-left origin
  meta.json                    sceneId, sceneSha256, backend, cyclesRevision, repoCommit,
                               hostLibrarySha256, runnerSha256, os, cpu, gpu, compiler, xcode,
                               cmake, threads, width, height, samples, repeats,
                               repeatBitwiseIdentical, repeatMaxAbsDifference, renderSeconds,
                               warnings, filmSha256, environment (CYCLES_CPU_NO_AVX2 for sse42)
oracle/<scene id>/<backend>.output.txt  runner output
comparisons/<scene>.json         pairwise compare.py reports
run.json                         backends, common machine record, failures, comparisonsPassed
manifest.json                    SHA-256 of every file
```

Backend names on the Intel Mac: `cpu-x64-macos`, `cpu-x64-macos-sse42`,
`wgsl-macos-x64`. Scenes use the `cbcOracleScene` version 1 format
(`native/oracle/README.md`).

## Standing rules

- Never write into an existing `native/evidence/*` checkpoint; new results go
  to new directories under `native/work/` and to new evidence directories
  created by the bundle step.
- Use a new output directory for every gate and oracle run; the scripts refuse
  non-empty ones.
- Never relax a tolerance, adjust a film or change a backend setting to make a
  comparison pass.
- Only the reviewed snapshots in `third_party/` are built; nothing from
  Homebrew is linked or included. No new NuGet packages.
- The shipped WebGPU runtime is used unchanged.
- The maintainer makes every commit and push.
