# Testing on an Intel Mac (osx-x64, WGSL on WebGPU)

Copyright (c) 2026 Jeremy Ellis and contributors

This document was written on a Linux machine without access to an Intel Mac.
None of the macOS steps below has been executed; each one carries an
**UNTESTED** marker at the step itself. The maintainer runs them on the Intel Mac,
corrects what is wrong in this document and in the recipes, and commits the
validated version. Lines marked **MISSING** name something that does not exist
yet; lines marked **UNKNOWN** name a fact that cannot be determined without the
hardware, each with the exact check that settles it.

## Purpose

The osx-x64 product is Cycles with the CodeBrix WGSL/WebGPU device, the same
device as Linux and Android, on the unchanged shipped CodeBrix.WebGpu.Native
runtime, which reaches the GPU through Metal on macOS. (Cycles' own Metal device
is the osx-arm64 product only.) A run on the Intel Mac establishes:

1. that the osx-x64 recipes build the host, the gates and the oracle runner
   from the reviewed snapshots, isolated from Homebrew;
2. the first osx-x64 GPU evidence: the Linux host-gate list passing on the
   Intel Mac's GPU through the runtime's Metal route, against the CPU kernels;
3. CPU-x64 oracle films from Apple Clang and WGSL films for the oracle corpus,
   to be compared with the Linux x64, Apple Silicon CPU and Metal films;
4. a reviewed `native/binaries/osx-x64` payload with provenance.

There is no Metal oracle on the Intel Mac: Cycles' Metal device at the pinned
revision accepts only Apple GPUs and skips Intel and AMD GPUs by name, so the
Metal films come from Apple Silicon only.

The recipe itself is described in [native/host/MACOS_X64.md](../native/host/MACOS_X64.md).

## WebGPU backend on Apple platforms: Metal

`native/src/webgpu_device.cpp` selects the WebGPU instance backend by
platform: under `#ifdef __APPLE__` it requests `WGPUInstanceBackend_Metal`
(and names Metal in its two error messages, "Cannot create Metal WebGPU
instance" and "No hardware Metal adapter"); on every other platform it keeps
`WGPUInstanceBackend_Vulkan`, so Linux and Android are unchanged. This is
needed because the shipped runtime's osx-x64 (and osx-arm64) dylib contains
wgpu's Metal backend and no Vulkan backend: it links `Metal.framework` and
`QuartzCore.framework`, references `MTLCopyAllDevices`, and contains none of
the Vulkan loader strings that the Linux and Windows builds contain. Before
this change every GPU step (steps 6 to 9 below) failed on a Mac with
`No hardware Vulkan adapter`.

**UNTESTED.** The Apple branch has been compiled and run nowhere yet (no Mac
is attached to the build machine); the first Intel Mac run confirms it.
Check the source before building:

```sh
grep -n 'WGPUInstanceBackend_' native/src/webgpu_device.cpp
```

The expected output names `WGPUInstanceBackend_Metal` inside the `__APPLE__`
branch and `WGPUInstanceBackend_Vulkan` in the other branch. If a GPU step
still reports "No hardware Metal adapter", record the adapter list and the
full stderr and bring them back.

The CPU steps (1 to 5, the CPU parts of 6 and 8, and staging prerequisites) do
not depend on it.

## Prerequisites and pins

| Item | Value |
|---|---|
| Machine | An Intel Mac that runs macOS 15 or later (the deployment target). |
| macOS | 15.0 or later. The host and the .NET wrapper refuse older systems. |
| Cycles | `a456b761034dda42c32eef9f4aae0fa5a5c9f604`, the filtered snapshot in `third_party/cycles` |
| WebGPU runtime | `CodeBrix.WebGpu.Native.MitLicenseForever` 1.0.300.210; package SHA-256 `ddfd92f8e9ce0f4348b0b079b856158608552184d3255c26a3e35a87b73b8b7b`; `runtimes/osx-x64/native/libcodebrix_wgpu.dylib` SHA-256 `840a106d54fe479c255ecb98d51cee192a9044c3d0411fc1535f9ff0b8abbb82` (x86_64, minimum macOS 11.0, ad-hoc signed) |
| Xcode | Xcode or the Xcode command-line tools: Apple Clang, the macOS SDK, libc++. Versions are recorded by the recipes. |
| Homebrew (x64) | In `/usr/local`. Used only for tools (CMake, Python, zstd); every recipe excludes `/usr/local` and `/opt/homebrew` from searches and rejects any reference to them. |
| CMake | 3.24 or newer (`brew install cmake`). |
| Python | 3.12 or newer (`brew install python`); the bundle step needs `tarfile`'s extraction filter. Xcode's Python is too old. |
| .NET | .NET 10 SDK for x64 (`dotnet --info` must report `RID: osx-x64`). |
| zstd | For the oracle bundle (`brew install zstd`). |
| Deployment target | macOS 15.0 for every built Mach-O (the runtime declares 11.0). |

**UNKNOWN:** the exact Intel Mac model, CPU, GPU(s) and macOS version.
Check: step 1 records them.

**UNKNOWN:** whether the Mac has AVX2 (every Intel Mac that runs macOS 15 is
expected to; this is an inference). It is recorded only: the fixed-baseline CPU
oracle builds no AVX2 kernel, so the result does not change the films. Check:
`sysctl -n machdep.cpu.leaf7_features | tr ' ' '\n' | grep -x AVX2`.

## Ordered steps

Run every step from the repository root of a clean checkout of the commit
under test. Budgets: none of these steps has been measured on an Intel Mac;
for every step, **no measurement yet; record the wall** (`/usr/bin/time -p`
in front of the command, or the timestamps in each output directory).

### 1. Record the machine **UNTESTED**

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

`system_profiler SPDisplaysDataType` names every GPU and its Metal support
line. **UNKNOWN:** which Metal feature set the Intel GPU(s) expose (for example
"Metal Support: Metal 3" or a Metal family). Check: the "Metal Support" line in
`system_profiler.txt`.

### 2. Restore and verify the WebGPU runtime **UNTESTED**

The recipes read the runtime from the NuGet cache
(`~/.nuget/packages/codebrix.webgpu.native.mitlicenseforever/1.0.300.210`, or the
directory named by `CODEBRIX_WEBGPU_PACKAGE`). Restore it once, then verify:

```sh
dotnet restore research/GpuProbe/GpuProbe.csproj
P=~/.nuget/packages/codebrix.webgpu.native.mitlicenseforever/1.0.300.210
shasum -a 256 $P/*.nupkg $P/runtimes/osx-x64/native/libcodebrix_wgpu.dylib
otool -L $P/runtimes/osx-x64/native/libcodebrix_wgpu.dylib
lipo -info $P/runtimes/osx-x64/native/libcodebrix_wgpu.dylib
codesign -dv $P/runtimes/osx-x64/native/libcodebrix_wgpu.dylib
```

Expected: the two SHA-256 values in the pins table; `otool -L` lists only
`/usr/lib` and `/System/Library` entries (Metal and QuartzCore among them);
`lipo` reports x86_64 alone; `codesign` reports `Signature=adhoc`.

**UNKNOWN:** whether the runtime's osx-x64 build routes WebGPU to Metal on
this Mac and which adapter it chooses when the Mac has both an integrated Intel
GPU and a discrete AMD GPU (the device takes the first hardware adapter the
runtime enumerates). Check: after step 6, `adapterReport` in the host gate's
`status.json` and `abi-gpu/run.capabilities-host.json`
(`host.deviceReport`), which name the adapter and its backend type
(5 is Metal in `webgpu.h`). There is no rule yet for Macs with more than one
GPU: record which adapter the runtime listed first and its identity (vendor,
architecture, device, description, backend type); the choice is decided on the
actual machine.

### 3. Bring the Linux overlay hashes **UNTESTED** (Linux part verified)

On the Linux x64 machine, after a maintained Linux build of the same commit:

```sh
python3 native/tools/build_cycles_macos_x64.py --write-overlay-hashes native/work/cycles-overlay \
  --output native/work/linux-overlay-hashes.json
```

Copy `linux-overlay-hashes.json` to the Mac (for example to
`native/work/linux-overlay-hashes.json`). Also bring a passed Linux
`host_gate.py` output directory (`native/work/host-validation` or the one the
Linux run named) for the cross-platform comparisons in step 6.

### 4. Build **UNTESTED**

```sh
python3 tools/build_macos_x64.py --jobs 8 --reference-overlay native/work/linux-overlay-hashes.json
```

This builds the dependency prefix (`native/work/macos/osx-x64/dependencies`),
the host, the consumers, the Session references and the WGSL device gates
(`native/work/macos/osx-x64/host/build`), and the oracle runner
(`native/work/macos/osx-x64/oracle-build`). Status and logs:
`native/work/macos/osx-x64/{dependencies,host}/status.json` and the
`configure.log`/`build.log` files beside them. Rebuild only the host after a
source change with `python3 tools/build_macos_x64.py --skip-dependencies ...`.

Points that may need correction on the Mac:

- **UNKNOWN:** whether Apple Clang's default header search on this Mac reaches
  `/usr/local/include`. The dependency recipe checks it before building and
  fails if so. Check by hand:
  `echo | xcrun clang++ -x c++ -v -E - 2>&1 | sed -n '/search starts/,/End of search/p'`.
- **UNKNOWN:** whether Makefile builds emit compiler dependency files
  (`*.o.d`) for every component; the recipes scan them for Homebrew headers
  and record how many they found (`compilerDependencyFiles` in the status
  files). A count of 0 means that check covered nothing.
- **UNKNOWN:** whether the Apple Clang AST dump translates the WGSL kernels
  identically. `--reference-overlay` fails the build and names the files if it
  does not; that is a finding to report, not something to adjust.
- **UNKNOWN:** whether Cycles' build on x86_64 macOS needs an adaptation that
  the Linux and Apple Silicon builds do not (the recipe applies none). Check:
  `native/work/macos/osx-x64/host/build.log`.
- **UNKNOWN:** whether `FMT_CONSTEVAL=` is still needed with the installed
  Apple Clang (it is on Apple Clang 21).

### 5. Binary review (inside the host gate) **UNTESTED**

Step 6 starts with it; to look by hand:

```sh
B=native/work/macos/osx-x64/host/build
otool -L $B/lib/libcodebrix_cycles_host.dylib
lipo -info $B/lib/libcodebrix_cycles_host.dylib
otool -l $B/lib/libcodebrix_cycles_host.dylib | grep -A3 LC_BUILD_VERSION
codesign -dv $B/lib/libcodebrix_cycles_host.dylib
nm -gU $B/lib/libcodebrix_cycles_host.dylib | grep ' T _cbc_host_' | wc -l
```

What must be true: x86_64 alone; `minos 15.0`; linked libraries only from
`/usr/lib`, `/System/Library`, the dependency prefix by `@rpath` and
`@rpath/libcodebrix_wgpu.dylib`; no `Metal.framework` on the host itself (only
the runtime links Metal); the exports equal `native/host/exports.macos`; no
image codec library in the load closure; signatures verify.

### 6. Host gate **UNTESTED**

```sh
python3 native/tools/macos_x64_host_gate.py --output native/work/macos-x64-validation-1 \
  --references <the passed Linux host_gate.py output>
```

The gate list is the Linux host gate's, with the same comparisons and
tolerances: Session references on CPU (3 films and 3 at 64 samples),
C lifecycle and process exit (bitwise against the references), concurrent
lifetimes, the thin .NET wrapper (HostLifecycle, bitwise), the WebGPU lifecycle
through `CBC_HOST_GPU` (6 films and the exit variant, practical and strict
against the CPU references), the ABI additions on CPU and GPU (base, object
motion and deforming films practical and strict; the four light films practical
with strict reported), and the example consumer on CPU and GPU. The color
reload and image reference tests do not apply and stay skipped: the macOS
payload rule holds for osx-x64 too (no image codec in the load closure;
textures arrive decoded). With `--references`, the CPU films are also compared with the Linux x64
films: lifecycle films practical and strict, the others practical with strict
reported. Results: `status.json` (`passed`, `gpuPracticalFailures`,
`gpuStrictDifferences`, `adapterReport`), `comparisons.json`, `results.json`,
`binary-review.json`, and stdout/stderr per step.

The GPU parts depend on the Metal backend selection described above
(UNTESTED until this run).

**UNKNOWN:** on every Apple RID the host points Cycles' resource path at a
`codebrix-cycles` directory beside the library (the osx-arm64 payload ships its
Metal kernel sources there). The osx-x64 payload has no such directory; the CPU
and WGSL devices are expected not to need it. Check: the host gate's CPU and
GPU steps pass and their stderr files show no missing-path messages.

**UNKNOWN:** the GPU timeouts (1,800 s per GPU step) are guesses; driver shader
compilation through the runtime's WGSL-to-MSL path on an Intel GPU has not been
measured. Record the wall of each GPU step from `commands.json` and the file
times.

### 7. WGSL device gates (diagnostic) **UNTESTED**

```sh
python3 native/tools/macos_x64_host_gate.py --output native/work/macos-x64-validation-2 \
  --references <the passed Linux host_gate.py output> --device-gates
```

Runs each built device gate executable with an output prefix and records its
own `passed` flag and the WebGPU library it loaded (`deviceGates` in
`status.json`). **MISSING:** the Linux gate drivers (`device_gate.py`,
`render_gate.py`, `svm_surface_gate.py` and the others) read
`native/work/cycles-build` and add coverage assertions and checkpoint archiving
that this diagnostic does not reproduce; an osx-x64 driver set does not exist.
A failure here is a finding with its stderr tail recorded, not a gate failure.

### 8. Oracle corpus **UNTESTED**

```sh
python3 native/oracle/render_oracles.py --output native/work/oracles/macos-x64-1 \
  --runner native/work/macos/osx-x64/oracle-build/codebrix_cycles_oracle \
  --host-library native/work/macos/osx-x64/host/build/lib/libcodebrix_cycles_host.dylib \
  --backend cpu-x64-macos --backend cpu-x64-macos-sse42 --backend wgsl-macos-x64
```

This renders every scene under `native/oracle/scenes` (the starter scenes and
the Linux gate corpus) with Cycles' default CPU kernel dispatch, with the
SSE4.2 kernels forced, and with the WGSL device, then compares them pairwise
(`comparisons/`, `run.json` `comparisonsPassed`). A backend that rejects a
scene is recorded under `failures` and the run continues. Then compare with the
existing Apple Silicon evidence for the starter scenes:

```sh
for s in diffuse-triangle glossy-sphere-point-light glass-and-area-light textured-plane; do
  python3 native/oracle/compare.py native/evidence/cycles-oracles-macos-20261019/oracle/starter/$s/cpu-arm64-macos \
    native/work/oracles/macos-x64-1/oracle/starter/$s/cpu-x64-macos \
    native/work/oracles/macos-x64-1/oracle/starter/$s/wgsl-macos-x64 \
    --json native/work/oracles/macos-x64-1/vs-arm64-$s.json
done
```

and, on Linux after the bundle is back, with `cpu-x64-linux` and
`wgsl-linux-intel` films of the same scenes rendered on Linux by the same
script. The build is the fixed-baseline CPU oracle (native/DEVICE.md, "Fixed-baseline
CPU oracle"): SSE4.2 kernels only, no FMA contraction, no reciprocal math, so
`cpu-x64-macos` and `cpu-x64-macos-sse42` run the same kernels and must agree
bitwise, and both must agree with `cpu-x64-linux` rendered by the same commit
within the practical criterion; a difference that does not trace to the C
library (macOS libm against glibc) is a finding. **UNTESTED:** the fixed-baseline
rule has not yet been compiled on an Intel Mac; step 4's recipe refuses a CPU
kernel line that is not at the baseline.

### 9. Application run: the glTF example client **UNTESTED**

```sh
H=native/work/macos/osx-x64/host/build/lib/libcodebrix_cycles_host.dylib
dotnet run --project tools/gltf/CycleGltfRender -c Release -- \
  --write-sample native/work/macos-x64-gltf/sample.glb --sample-sun
dotnet run --project tools/gltf/CycleGltfRender -c Release -- native/work/macos-x64-gltf/sample.glb \
  --output native/work/macos-x64-gltf/film.bin --compare --native-library $H
```

`--compare` renders on CPU and GPU and exits with 3 when the GPU film fails the
practical criterion. Record the printed comparison and the wall.

### 10. Managed tests **UNTESTED**

```sh
dotnet build CodeBrix.Graphics3D.Cycles.slnx -c Release
dotnet test CodeBrix.Graphics3D.Cycles.slnx -c Release
```

The native-host tests skip outside Linux x64 (they load the in-tree Linux
build); on the Mac this step proves that the solution builds and the managed
tests pass, nothing native. The native coverage on the Mac comes from step 6
(HostLifecycle) and step 9.

### 11. Stage and audit **UNTESTED**

Only after steps 6 and 8 passed:

```sh
python3 native/tools/stage_macos_x64_binaries.py --gate native/work/macos-x64-validation-1 \
  --oracles native/work/oracles/macos-x64-1
python3 tools/licensing/audit.py
python3 tools/licensing/notices.py --check
```

The stager writes `native/binaries/osx-x64/{runtime,unstripped,symbols,provenance.json}`
with the Mach-O review per dylib (see MACOS_X64.md for what must be true), then
renders `starter/diffuse-triangle` from a temporary copy of the payload plus
the package runtime on CPU and WebGPU; the CPU film must equal the oracle film.
`tools/licensing/audit.py` approves any `native/binaries/<rid>/provenance.json`
by its `approvedPayload` hashes and `snapshotCopies` (empty for osx-x64, which
ships no kernel sources); it must report `"passed": true`.

### 12. Bundle and bring back **UNTESTED**

```sh
python3 native/oracle/render_oracles.py --bundle native/work/oracles/macos-x64-1
```

On an Intel Mac the bundle is `cycles-oracles-macos-x64-<YYYYMMDD>.tar.zst`
under the run's `bundle/` directory; the verified extraction is published as a
new directory `native/evidence/cycles-oracles-macos-x64-<YYYYMMDD>/` (a numeric
suffix is added rather than overwriting). Bring back to the Linux machine, in
one new directory per run (for example under `~/ClaudeHome/cycles-oracles/`):
the bundle file, `native/work/macos-x64-identity/`, the host gate output
directories, `native/work/macos/osx-x64/{dependencies,host}/{status.json,invocation.json,provenance.json}`
and the staged `native/binaries/osx-x64/provenance.json`.

## Reference policy and tolerances

The unmodified Cycles CPU kernels are the oracle. Every comparison uses:

- practical criterion, required: `abs(a - b) <= 5e-5 + 2e-4 * abs(b)` for every
  finite float component, where `b` is the reference;
- strict criterion, `abs(a - b) <= 1e-7 + 1e-7 * abs(b)`: reported always, and
  required exactly where the Linux host gate requires it;
- NaN or Inf: always a failure; same-host CPU films: bitwise identical.

Tolerances are never relaxed, and films are never adjusted to match another
backend. A disagreement is a finding to report with its reproducer. The device
reference policy decides where things must pass; the Intel Mac is not one of
its reference devices, so its results are evidence, recorded as they are.

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

## What to record

- Hardware and driver identity: step 1's files (`sw_vers`,
  `system_profiler SPHardwareDataType SPDisplaysDataType`, CPU brand and
  features, Xcode, SDK, Clang, CMake, Python, `dotnet --info`, commit and
  working-tree state) and the runtime's adapter report (`adapterReport` in the
  host gate status, `host.deviceReport` in `abi-gpu/run.capabilities-host.json`).
- Walls: every step, and per GPU step in the host gate (`commands.json` with
  file times); first-run versus second-run render times in the oracle
  `meta.json` (`renderSeconds`) to see the driver's shader compile.
- Failures with reproducers: the failing command line from `commands.json`,
  its stdout/stderr files, the scene file or consumer name, the film pair and
  the `comparisons.json` row (maximum error, practical failures, strict
  differences).
- Every correction made to this document or to the recipes on the Mac, so the
  validated version can replace the **UNTESTED** markers.

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
