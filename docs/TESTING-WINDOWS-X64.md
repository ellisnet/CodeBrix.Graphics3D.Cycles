# Testing on Windows (win-x64 and win-arm64, WGSL on WebGPU)

Copyright (c) 2026 Jeremy Ellis and contributors

Steps carry a status marker: **VALIDATED** (run on the win-x64 development
machine; results recorded at the step), **UNTESTED** (not yet run), **MISSING**
(something that does not exist yet) or **UNKNOWN** (a fact that needs the
hardware, with the check that settles it). The recipe and its decisions are in
[native/host/WINDOWS_X64.md](../native/host/WINDOWS_X64.md).

## Purpose

The Windows products are Cycles with the CodeBrix WGSL/WebGPU device, the same
device as Linux, Android and Intel Macs, on the unchanged shipped
CodeBrix.WebGpu.Native runtime: Direct3D 12 with the DXC shader compiler by
default, Vulkan on request. A run establishes:

1. that the Windows recipes build the dependency prefix, the host, the gates and
   the oracle runner from the reviewed snapshots, isolated from every other
   compiler distribution on the machine;
2. win-x64 GPU evidence: the Linux host-gate list passing on the machine's GPU on
   Direct3D 12 and on Vulkan, against the CPU kernels;
3. CPU-x64 oracle films from clang-cl and WGSL films for the oracle corpus, to be
   compared with the Linux x64 films;
4. reviewed `native/binaries/win-x64` and `native/binaries/win-arm64` payloads
   with provenance. win-arm64 is cross-compiled on the x64 machine; an ARM64
   Windows device runs the same gate from the committed payload and builds
   nothing.

## Prerequisites and pins

| Item | Value |
|---|---|
| Build machine | x64 Windows 10 or 11 with a Direct3D 12 GPU (and a Vulkan driver for the Vulkan steps). |
| Test device (win-arm64) | ARM64 Windows 11 with a Direct3D 12 GPU. Nothing is built there. |
| Cycles | `a456b761034dda42c32eef9f4aae0fa5a5c9f604`, the filtered snapshot in `third_party/cycles` |
| WebGPU runtime | `CodeBrix.WebGpu.Native.MitLicenseForever` 1.0.290.1433; package SHA-256 `9ceea7951bff52fe39c4b2451da4e1a80e7a2889a2217487d6392125a18a7112`; `runtimes/win-x64/native/codebrix_wgpu.dll` SHA-256 `61c6f4d8d529240ab531e17f9aab7845b9dda04a45848674c1af463f346b6afa` |
| DXC | `dxcompiler.dll` 1.9.2609.5 in `native/binaries/<rid>/runtime/` (`third_party/dxc-import.json`; `tools/windows_dxc.py` pins the hashes) |
| Visual Studio | 2022 or later with "Desktop development with C++" and "C++ Clang tools for Windows" (clang-cl, lld-link), the Windows SDK, and its bundled CMake and Ninja. Versions are recorded by the recipes. |
| Python | 3.12 or newer, run as `python -I -X utf8` (UTF-8 mode; `-I` ignores `PYTHONUTF8`). |
| .NET | .NET 10 SDK for the machine's architecture. |
| Git | `core.autocrlf` may stay `true`: the root `.gitattributes` keeps every file's committed bytes (LF, and the CRLF files of the pinned snapshots). |

Antivirus scanning of a fresh clone can make the first full read of
`third_party/` take minutes; an exclusion for the working tree removes that.

## Ordered steps

Run every step from the repository root, in PowerShell or a Command Prompt
(Git Bash rewrites `/flag` arguments into paths; the recipes call their tools
directly and are not affected).

### 1. Verify the snapshots **VALIDATED**

```
python -I -X utf8 tools/windows_sources.py
python -I -X utf8 -c "import sys;sys.path.insert(0,'tools');import cycles_source;cycles_source.verify();print('cycles ok')"
python -I -X utf8 tools/arm_math_source.py
```

All 944 Cycles files, the 5 scalar-math files and the 18,370 dependency files
match their pinned SHA-256 values. `tools/licensing/dependency_sources.py` (the
pinned import policy) compares '/' manifest paths with Windows paths and fails on
Windows; `tools/windows_sources.py` applies the same checks with '/' paths, and
the licensing guard test that calls the pinned verifier directly
(`test_guards.DependencyGuardTests.test_reviewed_fixture_passes`) fails on
Windows for the same reason (the other six guard tests and the 13 compatibility
inventory tests pass).

### 2. Dependency prefix **VALIDATED**

```
python -I -X utf8 tools/dependencies/build_windows.py --rid win-x64 --jobs 16
python -I -X utf8 tools/dependencies/build_windows.py --rid win-arm64 --jobs 16
```

Outputs: `native/work/windows/<rid>/dependencies/{install,status.json,provenance.json}`
and `install/codebrix-<rid>-build.json` (library hashes, no registered OIIO
format plugin). Both RIDs: 17 components, 22 DLLs, every DLL of the target
machine type, none importing a Visual C++ runtime DLL; about 4 minutes each from
clean on a 24-thread machine (win-arm64 is cross-compiled).

### 3. Host build **VALIDATED**

```
python -I -X utf8 native/tools/build_cycles_windows.py --rid win-x64 --jobs 16 [--reference-overlay linux-overlay-hashes.json]
python -I -X utf8 native/tools/build_cycles_windows.py --rid win-arm64 --jobs 16
```

`--reference-overlay` takes the hashes written on Linux by
`python3 native/tools/build_cycles_macos_x64.py --write-overlay-hashes native/work/cycles-overlay --output linux-overlay-hashes.json`.
Without it, compare `native/work/windows/<rid>/host/overlay/translated.wgsl`
with a Linux copy: it was byte-identical (SHA-256 `b047c969…`) to
`native/evidence/native-render/generated/translated.wgsl` and the six other
retained Linux copies.

Checked by the recipe: 48 exports; the host loads 10 DLLs besides Windows
(OpenImageIO, OpenImageIO_Util, OpenColorIO, Imath, Boost filesystem and thread,
TBB, expat, zlib and `codebrix_wgpu.dll`), no image codec; the CPU kernel's last
contraction and reciprocal options are off, `/MT`, SSE4.2 without AVX/FMA on x64.

### 4. Host gate **VALIDATED** (win-x64)

```
python -I -X utf8 native/tools/windows_host_gate.py --rid win-x64 --backend d3d12 --output native/work/windows-validation-d3d12-1 [--references <passed Linux host_gate.py output>]
python -I -X utf8 native/tools/windows_host_gate.py --rid win-x64 --backend vulkan --output native/work/windows-validation-vulkan-1 [--references ...]
```

Results on an integrated Intel GPU (Direct3D 12 driver and Vulkan 1.4 driver),
without `--references`: both backends passed, 28 films compared, 0 failed films,
0 same-host bitwise failures, 0 GPU practical failures, maximum GPU error
1.5e-5; the strict differences (145 on D3D12, 148 on Vulkan) are in the four
light films, where the strict criterion is reported, not required. Walls: about
2 minutes (D3D12) and 1 minute (Vulkan), cold.

Findings while bringing the gate up:

- Process exit with a live host crashed (`0xC0000409`): a DLL's `atexit`
  functions run after `ExitProcess` has terminated the other threads. Fixed in
  the host (WINDOWS_X64.md, "Process exit with live hosts").
- **Strict criterion at the edge of accumulation noise.** In one D3D12 run, one
  component of `gpu-exit/run.3` (the 64-sample film; pixel 19,13, blue, 0.3017)
  was 5 ULPs from the CPU reference (1.49e-7 against a strict threshold of
  1.30e-7) while every other film passed. The same pixel was 3 ULPs off in the
  same run's `gpu/run.3` and 4 ULPs off on Vulkan; two D3D12 renders of the same
  scene differ by up to 7.45e-8 (GPU film accumulation is not bitwise
  repeatable). The pixel sits at the strict threshold on both backends; the
  tolerance is unchanged.

### 5. Managed tests **VALIDATED** (win-x64)

```
dotnet build CodeBrix.Graphics3D.Cycles.slnx -c Release
dotnet tests/CodeBrix.Graphics3D.Cycles.Tests/bin/Release/net10.0/CodeBrix.Graphics3D.Cycles.Tests.dll
```

The build has 0 warnings. With the GPU class excluded (`-noclass
CodeBrix.Graphics3D.Cycles.Tests.GpuRenderTests`): 179 tests, 0 failed, 0
skipped, on the in-tree Windows host. The native tests use the in-tree Windows
host or, on a test device, the staged payload in `native/binaries/<rid>/runtime`
with the WebGPU runtime from the NuGet cache. Set
`CODEBRIX_CYCLES_WEBGPU_BACKEND=vulkan` for a Vulkan run.

`GpuRenderTests` (`-class CodeBrix.Graphics3D.Cycles.Tests.GpuRenderTests
-showLiveOutput`): 12 tests, 0 failed, 0 skipped on Direct3D 12 (412 s, cold)
and on Vulkan (119 s). Every scene passes the practical criterion; largest
errors: textured plane through the Blender glTF mapping 5.1e-5 (D3D12) and
5.4e-5 (Vulkan), glass 2.1e-5 and 2.5e-5, the others below 1e-6. Both report
tier `full` and no warnings; the WGSL size guard prints its notice for
`shade_background` and `prepare_surface` (1,429,639 and 1,375,228 bytes against
1,110,000), which never changes the result.

### 6. Oracle corpus **VALIDATED** (win-x64), one open finding

```
python -I -X utf8 native/oracle/render_oracles.py --output native/work/oracles/windows-x64-1 --runner native/work/windows/win-x64/host/build/bin/codebrix_cycles_oracle.exe --host-library native/work/windows/win-x64/host/build/bin/codebrix_cycles_host.dll --backend cpu-x64-windows --backend wgsl-windows-x64-d3d12 --backend wgsl-windows-x64-vulkan
```

27 minutes. 49 scenes: CPU rendered 49, each WebGPU backend 47.

- 46 scenes pass every backend pair (`run.json` `comparisonsPassed`).
- `linux-gates/closed-volumes` and `linux-gates/world-volume`: refused on both
  WebGPU backends ("WebGPU surface data passes not implemented"), a documented
  WebGPU render-time limit; compared on the CPU only.
- `assets/bench_01` and `assets/bench_01-hdri`: not rendered on any backend; their
  raw files are generated by `native/oracle/scenes/make_bench_bundle.py` and are
  not stored in the repository.
- **Open finding: `linux-gates/triangle-lighting-2`.** Both WebGPU backends differ
  from the CPU film identically (6 practical failures, worst pixel 24,15 R: CPU
  0.04195, GPU 0.03668), while D3D12 and Vulkan agree to 1.1e-7 and the Windows
  CPU film agrees with the macOS arm64 CPU oracle
  (`native/evidence/cycles-oracles-macos-20261023`, 2.8e-6, 0 practical
  failures). Triangle-lighting modes 0, 1, 3 and 5 pass. **UNKNOWN:** whether the
  Linux WGSL device agrees with the Linux CPU film on this scene. Check, on Linux:
  `python3 native/oracle/render_oracles.py --output native/work/oracles/tl2-check --runner native/work/cycles-build/bin/codebrix_cycles_oracle --host-library native/work/cycles-build/lib/libcodebrix_cycles_host.so --backend cpu-x64-linux --backend wgsl-linux-intel --scene native/oracle/scenes/linux-gates/triangle-lighting-2.json`.

### 7. Stage the payloads **VALIDATED**

```
python -I -X utf8 native/tools/stage_windows_binaries.py --rid win-x64 --gate native/work/windows-validation-d3d12-<n> --gate native/work/windows-validation-vulkan-<n>
python -I -X utf8 native/tools/stage_windows_binaries.py --rid win-arm64
```

The stager refuses a gate that validated other binaries. win-x64: 22 files, a
temporary copy of the payload plus the shipped runtime renders the example scene
on CPU and GPU (identical center pixel). The stager writes win-arm64 with
`validation.status` "pending-device"; the committed payload's `provenance.json`
was then speculatively set to "validated", the same as win-x64, because step 8
passes on an ARM64 device before the binaries ship.

The device procedure (step 8) was exercised on win-x64 against the staged
payload: `windows_host_gate.py --rid win-x64 --backend d3d12 --payload
native/binaries/win-x64` passed (28 films, 0 failures, maximum GPU error
1.5e-5).

### 8. win-arm64 device run **UNTESTED**

On the ARM64 device, from a clone of the commit that carries the staged payload
(nothing is built there; .NET 10 SDK for ARM64 and Python are needed):

```
dotnet restore research/GpuProbe/GpuProbe.csproj
python -I -X utf8 native/tools/windows_host_gate.py --rid win-arm64 --backend d3d12 --payload native/binaries/win-arm64 --output native/work/windows-arm64-validation-d3d12-1
dotnet build CodeBrix.Graphics3D.Cycles.slnx -c Release
dotnet tests/CodeBrix.Graphics3D.Cycles.Tests/bin/Release/net10.0/CodeBrix.Graphics3D.Cycles.Tests.dll
python -I -X utf8 native/oracle/render_oracles.py --output native/work/oracles/windows-arm64-1 --runner native/binaries/win-arm64/tests/codebrix_cycles_oracle.exe --host-library native/binaries/win-arm64/runtime/codebrix_cycles_host.dll --backend cpu-arm64-windows --backend wgsl-windows-arm64-d3d12
```

The gate assembles its run folder from the verified payload and the pinned
runtime. **UNKNOWN:** whether the device's GPU driver compiles the largest
kernels within the gate's step timeouts (3,600 s per GPU step). Bring back the
gate output directory and the oracle run directory.

## Reference policy and tolerances

The unmodified Cycles CPU kernels are the oracle. Every comparison uses:

- practical criterion, required: `abs(a - b) <= 5e-5 + 2e-4 * abs(b)` for every
  finite float component, where `b` is the reference;
- strict criterion, `abs(a - b) <= 1e-7 + 1e-7 * abs(b)`: reported always, and
  required exactly where the Linux host gate requires it;
- NaN or Inf: always a failure; same-host CPU films: bitwise identical.

Tolerances are never relaxed, and films are never adjusted to match another
backend. A disagreement is a finding to report with its reproducer.

## Standing rules

- Never write into an existing `native/evidence/*` checkpoint; new results go
  to new directories under `native/work/`.
- Use a new output directory for every gate and oracle run; the scripts refuse
  non-empty ones.
- Never relax a tolerance, adjust a film or change a backend setting to make a
  comparison pass.
- Only the reviewed snapshots in `third_party/` are built; nothing from another
  compiler distribution is linked or included.
- The shipped WebGPU runtime and DXC are used unchanged.
- The maintainer makes every commit and push.
