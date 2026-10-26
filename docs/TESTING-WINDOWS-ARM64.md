# Testing on Windows ARM64 (win-arm64 device run, WGSL on WebGPU)

Copyright (c) 2026 Jeremy Ellis and contributors

Steps carry a status marker: **VALIDATED** (run on the win-arm64 test device;
passed; results recorded at the step), **FAILED** (run; findings recorded),
**RUNNING**, **UNTESTED** (not yet run), **NOT AVAILABLE** (the device cannot
run it) or **UNKNOWN** (a fact still to be settled, with the check that settles
it). This document is the device-side counterpart of
[TESTING-WINDOWS-X64.md](TESTING-WINDOWS-X64.md) (step 8 there); the build
recipe and its decisions are in
[native/host/WINDOWS_X64.md](../native/host/WINDOWS_X64.md).

## Summary (2026-10-25)

The committed win-arm64 payload loads, renders and passes the host gate on the
Adreno 680 with Direct3D 12, and passes every non-GPU managed test. It does
**not** reach the win-x64 level: **smooth-shaded surfaces render black on the
GPU** (finding F1), so 3 of 12 `GpuRenderTests` fail, and the oracle corpus has
two further GPU failure classes: **motion blur with a normal map** (F3, 10 scenes)
and **glass lit by an area light** (F4, 1 scene). All three are Qualcomm D3D12
driver problems: the win-x64 host, which passes the same scenes on Intel, fails
them identically on this driver. The CPU path is at full parity (every CPU film
matches the macOS arm64 CPU oracle). Routing around the driver problems needs the
diagnostic additions listed under "Recommended additions for the next Windows
build".

One win-x64 open finding is answered here: `linux-gates/triangle-lighting-2`,
where both win-x64 WGSL backends (Intel) differ from the CPU, **passes** on the
Adreno (WGSL versus CPU max 2.1e-6). The win-x64 discrepancy is therefore
specific to the Intel driver, not to the WGSL device. The payload's
`provenance.json` "validated" status is not supported by this run and should go
back to "pending-device" until F1 is resolved and the device run repeated.

| Step | Result | win-x64 |
|---|---|---|
| 1. Host gate, D3D12 | VALIDATED: 28 films, 0 failures, max GPU error 1.53e-5 | passed, 1.5e-5 |
| 2a. Managed build and non-GPU tests | VALIDATED: 0 warnings; 179 passed | 0 warnings; 179 passed |
| 2b. `GpuRenderTests` | **FAILED: 9 passed, 3 failed (F1)** | 12 passed |
| 3. Oracle corpus | **FAILED: 34 of 49 pass; 13 WGSL failures (F1, F3, F4)**; volumes refused and bench not rendered, as on win-x64 | 46 of 49 pass |
| 4. Cross-platform comparison | VALIDATED: all 47 CPU films match the macOS arm64 CPU oracle (0 practical failures) | |
| 5. Vulkan | NOT AVAILABLE (only Dozen, which wgpu hides unless the host opts in) | passed |

## Purpose

The win-arm64 payload in `native/binaries/win-arm64` is cross-compiled on the x64
build machine and is never built on an ARM64 device. Its `provenance.json` was set
to "validated" before any device run (TESTING-WINDOWS-X64.md, step 7). A run on an
ARM64 device establishes, for the committed payload and nothing newer:

1. the host gate passing on the device's GPU on Direct3D 12 (DXC), against the
   CPU kernels;
2. the managed tests (non-GPU and `GpuRenderTests`) passing against the staged
   payload;
3. CPU-arm64 oracle films and WGSL Direct3D 12 films for the oracle corpus, to be
   compared with the win-x64 and macOS arm64 oracle runs;
4. Vulkan, where the device has a Vulkan driver;
5. a recorded verdict that either confirms the payload's "validated" status or
   sends it back to "pending-device" with findings.

## What is tested: the payload commit, not HEAD

The payload was staged in commit `992615635` ("Windows x64 binary work.",
2026-10-24) and has not changed since. Later commits add WGSL device features
(ambient occlusion, light tree, light linking, MNEE, shadow catcher) with their own
tests, oracle scenes and gates; the arm64 binaries do not contain them, so running
HEAD's tests against the payload would produce meaningless failures. The device
run therefore uses a detached worktree of the payload commit:

```
git -C <repo> worktree add --detach <repo>-arm64-test 992615635
```

On the test device the worktree is `C:\GitHome\Cycles-arm64-test`. All commands
run from the worktree root, and every output directory named below is under its
`native/work/` (git-ignored). The results of the 2026-10-25 run are committed in
`native/evidence/windows-arm64-device-20261025` (see "Run record"); the payload
copies inside the gate directories are not.

## Test device

| Item | Value |
|---|---|
| Device | Microsoft Surface Pro X (Microsoft SQ1), Windows 11 Home 10.0.26300, ARM64 |
| GPU | Qualcomm Adreno 680, driver 27.20.2140.0 (2024-10-30), Direct3D 12; the host reports it as a mobile adapter (`budgetSource` "unknown-mobile", no device record, tier `full`) |
| Vulkan | Loader 1.3.300; the only Vulkan driver is Mesa's Dozen (Vulkan on D3D12, "Microsoft Direct3D12 (Qualcomm(R) Adreno(TM) 680 GPU)", Mesa 26.3.0-devel, Vulkan 1.1.217) from the Microsoft "OpenCL, OpenGL & Vulkan Compatibility Pack" (`Microsoft.D3DMappingLayers` 1.2608.3.0); see step 5 |
| Python | 3.15.1, native ARM64, run as `python -I -X utf8` |
| .NET | SDK 10.0.403 (ARM64) |
| Build tools | none; nothing is compiled except the managed solution |

## Ordered steps

### 0. Prepare **VALIDATED**

```
git -C C:\GitHome\CodeBrix.Graphics3D.Cycles worktree add --detach C:\GitHome\Cycles-arm64-test 992615635
cd C:\GitHome\Cycles-arm64-test
dotnet restore research/GpuProbe/GpuProbe.csproj
```

158,406 files checked out; the restore took 9 s.

### 1. Host gate, Direct3D 12 **VALIDATED**

```
python -I -X utf8 native/tools/windows_host_gate.py --rid win-arm64 --backend d3d12 --payload native/binaries/win-arm64 --output native/work/windows-arm64-validation-d3d12-1
```

Passed: 28 films compared, 0 failed films, 0 same-host bitwise failures, 0 GPU
practical failures, maximum GPU error 1.53e-5 (win-x64: 1.5e-5). 61 strict
differences (win-x64: 145 on D3D12), reported, not required. Payload inputs
unchanged. Adapter: D3D12, "Qualcomm(R) Adreno(TM) 680 GPU", tier `full`, no
warnings, runtime `wgpu-native=29.0.1.1`, `target=aarch64-pc-windows-msvc`, DXC
from the payload. Wall: about 5.5 minutes, cold (win-x64: about 2 minutes).

Skipped, as on win-x64, because the product disables the OIIO image plugins: the
PNG reload test, the image-reference films and the managed image-mode checks.

**UNKNOWN:** why the gate passes despite F1. Presumably its GPU films contain no
smooth-shaded BSDF surfaces; check the scenes `codebrix_host_gpu_test` renders.

**UNKNOWN, settled:** the Adreno driver compiles the largest kernels well within
the 3,600 s step timeout. Single pipelines can take 30 to 50 s (superset mode
reported `prepare_surface` at 49 s, over the 20 s compile budget).

### 2. Managed tests **FAILED** (non-GPU VALIDATED)

```
dotnet build CodeBrix.Graphics3D.Cycles.slnx -c Release
dotnet tests/CodeBrix.Graphics3D.Cycles.Tests/bin/Release/net10.0/CodeBrix.Graphics3D.Cycles.Tests.dll -noclass CodeBrix.Graphics3D.Cycles.Tests.GpuRenderTests
dotnet tests/CodeBrix.Graphics3D.Cycles.Tests/bin/Release/net10.0/CodeBrix.Graphics3D.Cycles.Tests.dll -class CodeBrix.Graphics3D.Cycles.Tests.GpuRenderTests -showLiveOutput
```

Build: 0 warnings, 0 errors, 26 s. Non-GPU: 179 tests, 0 failed, 0 skipped,
2.3 s, against the staged payload. The OIIO "Image file does not exist" lines in
its output come from tests that pass an empty image filename.

`GpuRenderTests` on Direct3D 12: 12 tests, **3 failed**, 0 skipped, 821 s cold
(win-x64: 0 failed, 412 s). Every failure is a scene with smooth-shaded geometry
(F1):

| Test / scene | Result |
|---|---|
| gltf-sample | FAIL: max error 0.807, 1,731 of 6,144 values practical failures |
| gltf-textured-plane-blender | FAIL: max error 4.97, 1,665 practical failures |
| glass (smooth sphere) | FAIL: max error 0.715, 246 of 3,072 practical failures |
| diffuse-sun (flat geometry) | pass, max 4.2e-7 |
| textured-plane (flat geometry) | pass, max 2.4e-7 |
| the other 7 (progress, cancellation, recovery, device report, ...) | pass |

### 3. Oracle corpus **FAILED** (F1, F3, F4)

The command in TESTING-WINDOWS-X64.md step 8 does not start the runner (F2).
The run assembles a run folder the same way the host gate does: the verified
payload DLLs plus the pinned runtime from the gate's `payload-run`, and the
payload's oracle runner, all unchanged (13 files, SHA-256 equal to their sources):

```
mkdir native/work/windows-arm64-oracle-run-1
copy native/work/windows-arm64-validation-d3d12-1/payload-run/*.dll native/work/windows-arm64-oracle-run-1/
copy native/binaries/win-arm64/tests/codebrix_cycles_oracle.exe native/work/windows-arm64-oracle-run-1/
python -I -X utf8 native/oracle/render_oracles.py --output native/work/oracles/windows-arm64-2 --runner native/work/windows-arm64-oracle-run-1/codebrix_cycles_oracle.exe --host-library native/work/windows-arm64-oracle-run-1/codebrix_cycles_host.dll --backend cpu-arm64-windows --backend wgsl-windows-arm64-d3d12
```

The first attempt, with the documented command, is kept in
`native/work/oracles/windows-arm64-1` (every scene "failed" with empty output).

62 minutes. 49 scenes: CPU rendered 47, WGSL 45.

- **34 scenes pass** every backend pair, including all 12 broad-lighting, all 8
  transparent-lighting, triangle-lighting 0, 1, 2, 3 and 5, object-motion 0, 2,
  4, 6, deforming-motion 4 and 6, and starter diffuse-triangle,
  glossy-sphere-point-light and textured-plane. CPU repeats are bitwise
  identical.
- `linux-gates/closed-volumes` and `linux-gates/world-volume`: refused on WebGPU
  ("WebGPU surface data passes not implemented"), as on win-x64.
- `assets/bench_01` and `assets/bench_01-hdri`: not rendered (raw files not in the
  repository), as on win-x64.
- **13 WGSL failures**, in three classes (the classification below uses the
  smooth-face count and the motion settings of each scene file):

| Class | Scenes | Practical failures | Notes |
|---|---|---|---|
| F1 smooth shading | linux-gates/glass-render, linux-gates/textured-plane | 2,631; 1,104 | 20 smooth faces each |
| F3 motion blur with normal map | deforming-motion 0, 1, 2, 3, 5, 7; object-motion 1, 3, 5, 7 | 139 to 334 each | flat geometry; `normal_map` and `texture_coordinate` nodes; GPU both brighter and darker; the other modes of the same fixtures pass |
| F4 glass with area light | starter/glass-and-area-light | 372 | flat geometry; worst pixel CPU 0.076, GPU 0.53 |

win-x64 reference: 46 of 49 pass; the volumes refused and bench not rendered, as
here; open finding `linux-gates/triangle-lighting-2`, which passes here.

### 4. Cross-platform comparison **VALIDATED**

The step 3 films compared with the committed macOS arm64 CPU oracle
(`native/evidence/cycles-oracles-macos-20261023`), film 0, practical criterion:

- **CPU:** all 47 scenes rendered on both platforms match, 0 practical failures,
  maximum difference 1.28e-5 (world-volume).
- **WGSL (Adreno) versus macOS CPU:** the same 34 pass and the same 13 fail as
  against the Windows CPU film.

The win-x64 CPU oracle (`native/work/oracles/windows-x64-1`) is on the build
machine and was not compared.

### 5. Vulkan **NOT AVAILABLE**

```
python -I -X utf8 native/tools/windows_host_gate.py --rid win-arm64 --backend vulkan --payload native/binaries/win-arm64 --output native/work/windows-arm64-validation-vulkan-1
```

The gate's first GPU step stops with `cbc_host_create(&config, &host) returned
2: Requested device is unavailable` (`gpu.stderr.txt`). Qualcomm ships no native
Vulkan driver for the Adreno 680 on Windows. The only Vulkan driver on the device
is Mesa's Dozen (Vulkan layered on D3D12, from the Microsoft compatibility pack),
and wgpu hides it: with the payload's pinned runtime (wgpu-native 29.0.1.1), a
Vulkan instance enumerates 0 adapters by default and 2 (the Adreno and the
Basic Render Driver, both through Dozen) only with
`InstanceFlag.AllowUnderlyingNoncompliantAdapter`, wgpu's opt-in for
non-conformant drivers. The host sets no instance flags
(`native/src/webgpu_device.cpp`, `WGPUInstanceExtras`), so it neither opts in nor
reads wgpu's environment variables. Not a product failure; a backend never falls
back to another.

Covering Vulkan here would take a host change to opt in to non-conformant
adapters, and the result would still be D3D12 underneath (Dozen translates
SPIR-V to DXIL with Mesa's compiler, then runs on the same Qualcomm D3D12
driver), so it would not be equivalent to the win-x64 Vulkan evidence. An ARM64
device with a native Windows Vulkan driver (for example a Snapdragon X device)
is needed for that. Dozen may still be useful for F1: it reaches the same
Qualcomm driver with DXIL from a different compiler.

### FXC is not tested

Direct3D 12 with FXC was abandoned on win-x64 (it failed to compile the real
Cycles kernels and was tens of times slower where it succeeded;
WINDOWS_X64.md, "DXC, not FXC"). It is not part of the device run.

## Findings

### F1. Smooth-shaded surfaces render black on Adreno 680 / Direct3D 12 (open)

**Symptom.** On the GPU, every surface with smooth shading (`smooth` true on the
mesh) gets exactly 0 from every BSDF: no direct light, no indirect light. Emission
on the same surface is correct. Flat surfaces are correct. The GPU film is never
brighter than the CPU film, only darker, and in the gltf-sample scene 1,179 of
the 1,731 failing values are exactly 0.0. The kernel error word stays 0 (no
fault, no warning), so the checked failure paths in `surface_setup_hit`
(`surface_failure`) are not taken. Deterministic: two renders give identical
statistics. win-x64 (Intel, D3D12 and Vulkan) passes the same scenes.

**Narrowing** (each line: CPU versus GPU on the same converted glTF bundle,
48x32, 16 spp; "black" = GPU 0.0 where the CPU film is lit):

| Variant | GPU |
|---|---|
| full gltf-sample scene | black on every lit, non-emissive surface |
| one diffuse quad, constant background, no sun, no other objects, smooth | black (CPU 0.15/0.165/0.20 = albedo x background) |
| same quad, flat (no `smooth`, no N attribute) | correct, max error 3e-8 |
| same quad, flat, with the N attribute | correct, max error 3e-8 |
| same quad, smooth, without the N attribute (Cycles computes vertex normals) | black |
| smooth, shadow-terminator geometry and shading offsets 0 | black (terminator code ruled out) |
| smooth, `CODEBRIX_CYCLES_SHADER_SPECIALIZATION=superset:0` | black (not specialization-dependent) |
| smooth, emission-only material | correct, max error 6e-8 |
| smooth, sharp GGX glossy material | black |
| integrator `sampling_pattern` default instead of tabulated Sobol | no change |

**Diagnosis.** The interpolated smooth shading normal `sd.N` computed in
`surface_setup_hit` (`native/shaders/wavefront_shader_data.wgsl`, the
`SHADER_SMOOTH_NORMAL` branch: `triangle_frame_index`, `surface_decode_normal`,
the weighted sum and `surface_normalize`) comes out invalid on this driver,
either non-finite or on the wrong side of `Ng`. Cycles' BSDFs then reject every
sample as a light leak, which gives exactly 0 without raising a kernel error.
Ray hit, shader evaluation and film accumulation all work (emission is correct).
The same WGSL is correct on Intel D3D12 and Vulkan (win-x64), so the likely cause
is the Qualcomm D3D12 driver's compilation of the DXC (DXIL) output for this code.
(Not checked here: whether the Android Adreno/Vulkan evidence covers smooth BSDF
surfaces against the CPU.) There is a precedent in the same helpers:
`surface_native_normalize` carries a workaround for the Adreno 650 compiler
dropping a store (error 8). Not yet known: which of decode, interpolation or
normalize goes wrong, and whether the value is NaN or a flipped vector. The host
offers no WGSL override, so that needs a rebuilt host.

**Driver, not host: settled.** Two further experiments on the device:

1. *The win-x64 host shows the same failure on this driver.* The win-x64 payload's
   oracle runner (x64 emulation on Windows on ARM; win-x64 payload DLLs plus the
   pinned win-x64 `codebrix_wgpu.dll`, SHA-256 `61c6f4d8…`, in
   `native/work/windows-x64-emulated-run-1`) rendered the smooth
   `ground-only-no-sun` bundle on the Adreno exactly as the arm64 host did: 1,719
   values darker than the CPU film, 1,470 of them exactly 0. Its CPU film equals
   the arm64 CPU film. The same x64 binary passes on Intel D3D12 (win-x64 step 5).
   The arm64 build is not the cause. (`host-compare/` in the F1 directory.)
2. *The smooth-normal code is correct in isolation on the same driver.* A
   standalone compute shader with the real `wavefront_float.wgsl` helpers, a
   verbatim `surface_decode_normal` and the smooth branch's control flow
   (`table_at` with `fail()`, `triangle_frame_index` with signed offsets, the early
   return from the loop filling `var normals:array<vec3<f32>,3>`, struct return),
   compiled by the payload's DXC through the pinned runtime, gives bit-identical
   results on the Adreno and on the Microsoft Basic Render Driver (WARP, D3D12
   reference) for 256 decode/interpolate cases and 128 branch cases.
   (Scratch probe; see "Recommended additions", item 8.)

Also unchanged by the conservative tier (workgroup size 1, forced with
`CODEBRIX_CYCLES_PIPELINE_BUDGET_MS=1` and
`CODEBRIX_CYCLES_PIPELINE_BUDGET_ACTION=conservative`): its GPU film is
byte-identical to the superset-mode film.

Conclusion: the Qualcomm D3D12 driver (27.20.2140.0) mistranslates the
smooth-normal computation only in the context of the full Cycles kernels. The
WGSL and DXC output are correct (WARP and Intel agree with the CPU). Parity on
this device needs either a shader-side workaround found by on-device iteration,
which the current binary cannot do (no WGSL override, no software adapter), or a
fixed driver.

**Reproducer** (`native/evidence/windows-arm64-device-20261025/f1-smooth-normal`: bundles for
every variant above, the sample `.glb`, the films, `bisect.py`, `filmdiff.py`, the
`GpuRenderTests` log):

```
set CODEBRIX_CYCLES_NATIVE=native\work\windows-arm64-validation-d3d12-1\payload-run\codebrix_cycles_host.dll
dotnet tools/gltf/CycleGltfRender/bin/Release/net10.0/CodeBrix.Graphics3D.Cycles.GltfRender.dll --bundle native/evidence/windows-arm64-device-20261025/f1-smooth-normal/ground-only-no-sun/scene.json --output <new dir>/film.bin --compare
```

Exit code 3 with `max abs error 0.2, practical failures 1719` reproduces F1; the
`gns-flat` bundle passes. Each GPU render takes about 90 s on this device, mostly
pipeline compilation.

**Next steps:** build the diagnostic hooks in "Recommended additions" below
(items 1, 2 and 6 matter most for F1), then iterate on the device. Candidate
shader workarounds to try first through the WGSL override: the opaque-loop
treatment `surface_native_normalize` already uses for the Adreno 650, applied to
`surface_decode_normal` and the interpolation; replacing
`var normals:array<vec3<f32>,3>` with three scalar variables; moving the smooth
branch out of `surface_setup_hit` into its own function. Any accepted WGSL
change is then re-staged and re-validated on both RIDs.

The installed driver is 27.20.2140.0 (2024-10-30). No other driver version was
tried.

### F2. The step-8 oracle command cannot start the runner (documentation)

`native/binaries/win-arm64/tests/codebrix_cycles_oracle.exe` exits with
`0xC0000135` (STATUS_DLL_NOT_FOUND) when run from the payload: its DLLs are in
`runtime/`, and `codebrix_wgpu.dll` is by design not part of the payload. It
ships in the `CodeBrix.WebGpu.Native.MitLicenseForever` NuGet package (which
includes win-arm64) and has to be placed next to the host at run time, as the
host gate's run folder does. `render_oracles.py` then records every scene as failed
with empty output. TESTING-WINDOWS-X64.md step 8 should assemble a run folder as
step 3 here does (or `render_oracles.py` could accept a payload directory the way
`windows_host_gate.py --payload` does). The same applies to win-x64 when it is
run from its payload rather than its build directory.

### F3. Motion blur with a normal map renders wrong on Adreno 680 / Direct3D 12 (open)

**Symptom.** 10 oracle scenes from the deforming-motion and object-motion
fixtures (modes 0, 1, 2, 3, 5, 7 and 1, 3, 5, 7) fail on the GPU with 139 to 334
practical failures each (max 0.001 to 0.15); the GPU film is both brighter and
darker, with almost no exact zeros, so this is not F1's signature. The scenes
have no smooth faces; their materials use `normal_map` and `texture_coordinate`
nodes under motion blur. Other modes of the same fixtures pass, as does motion
blur without a normal map (triangle-lighting-5). The CPU films match the macOS
oracle.

**Driver, not host.** The win-x64 host renders `deforming-motion-1` on the
Adreno with the same failure as the arm64 host (host versus host max 3.0e-8, 0
practical differences; `f3-f4-host-compare` in the evidence directory), and
passes it on Intel. Precedent: the Adreno 650 workaround in
`surface_native_normalize` was for "object and deformation motion images".

**Not yet narrowed** (time box): which mode parameters separate the passing modes
from the failing ones (see `native/cycles/lighting_gate.cpp`, the fixture
modes), and whether the normal map node, the motion transform/vertex
interpolation or the texture coordinate under motion is the miscompiled region.
The recommended additions (WGSL override, WARP, debug output) are the tools for
that. Reproducer: `codebrix_cycles_oracle --scene
native/oracle/scenes/linux-gates/deforming-motion-1.json --backend webgpu
--output <new dir>` from `native/work/windows-arm64-oracle-run-1`.

### F4. Glass lit by an area light renders wrong on Adreno 680 / Direct3D 12 (open)

**Symptom.** `starter/glass-and-area-light` fails on the GPU with 372 practical
failures (max 0.81; worst pixel CPU 0.076, GPU 0.53). Flat geometry; a glass BSDF
and an area light. `starter/glossy-sphere-point-light` passes. The CPU film
matches the macOS oracle.

**Driver, not host.** The win-x64 host renders it on the Adreno with the same
failure (host versus host max 2.4e-7, 0 practical differences), and passes it on
Intel.

**Not yet narrowed** (time box): glass BSDF versus area-light sampling. A
bisection with edited copies of the scene bundle (glass to diffuse, area light to
point light), as done for F1, is the next step and needs no rebuild.
Reproducer: as F3, with `native/oracle/scenes/starter/glass-and-area-light.json`.

## Recommended additions for the next Windows build

What would have let this device run diagnose and route around F1 without a
rebuild per experiment, in priority order. All are opt-in through environment
variables, off by default, and recorded in the device report (and in gate
`status.json`) so a diagnostic run can never be mistaken for validation; gates
should refuse to report "passed" when any of 1 to 3 or 7 is active.

1. **WGSL override (`CODEBRIX_CYCLES_WGSL_LOAD=DIR`).** The counterpart of the
   existing `CODEBRIX_CYCLES_WGSL_DUMP` (`native/cycles/pipelines.cpp`): when
   `DIR/<module>.wgsl` exists, compile it instead of the generated (specialized,
   tier-adjusted) source. Report `shaderOverride` with the file's SHA-256 and print
   a warning. Workflow: dump, edit a copy, load, compare with the CPU film. This
   turns every driver bug into on-device iteration measured in minutes.
2. **Adapter choice (`CODEBRIX_CYCLES_WEBGPU_ADAPTER=hardware|software|<name
   substring>`).** Today `webgpu_device.cpp` accepts only integrated or discrete
   adapters. `software` picks the CPU adapter (WARP on D3D12): a full-kernel
   reference on the same machine, same WGSL, same DXC, which separates
   "our code is wrong" from "this driver is wrong" in one render (F1 needed an x64
   emulation detour for that).
3. **Non-conformant adapters
   (`CODEBRIX_CYCLES_WEBGPU_ALLOW_NONCOMPLIANT=1`).** Set
   `WGPUInstanceFlag_AllowUnderlyingNoncompliantAdapter` in `WGPUInstanceExtras`.
   With `CODEBRIX_CYCLES_WEBGPU_BACKEND=vulkan` this reaches Mesa Dozen
   (Vulkan on D3D12) on Windows on ARM devices without a native Vulkan driver.
   Dozen generates DXIL with Mesa's compiler instead of DXC, then uses the same
   Qualcomm driver, so it is both a diagnostic (is it DXC's DXIL shape?) and a
   possible route around. Report the driver string ("Mesa ... "), and never count
   it as Vulkan evidence.
4. **DXC shader model cap (`CODEBRIX_CYCLES_DXC_MAX_SHADER_MODEL=6.0`...`6.7`).**
   The host hard-codes `WGPUDxcMaxShaderModel_V6_7`. A lower model changes the
   DXIL the driver sees (16-bit types, wave intrinsics, newer instructions) and
   is a cheap first thing to try against a driver compiler bug.
5. **wgpu instance debug flags
   (`CODEBRIX_CYCLES_WEBGPU_INSTANCE_FLAGS=debug,validation,gpu-based-validation`,
   or honour wgpu's own `WGPU_*` variables by setting the "with env" flag).**
   D3D12 debug layer and GPU-based validation catch undefined behaviour that one
   driver tolerates and another does not. Needs the Windows "Graphics Tools"
   optional feature on the device.
6. **Debug output (`CODEBRIX_CYCLES_DEBUG_OUTPUT=shading-normal|geometric-normal|
   shader-flags|...`).** Write a first-hit `SurfaceData` field into the film
   instead of radiance (encoded, with non-finite values marked). One render shows
   whether F1's normal is NaN, zero or flipped. Item 1 can do this ad hoc, but a
   built-in mode is quicker to use and to document.
7. **Named workarounds (`CODEBRIX_CYCLES_WORKAROUNDS=name,...`), Dawn-style.**
   WGSL variants selected at module generation, plus entries in the existing
   device-record/tier policy (`device_tier.h`, keyed by vendor, device and driver
   version) that turn a proven workaround on automatically for an affected driver,
   for example Qualcomm vendor `0x4D4F4351`. Include the workaround set in
   `production_shader_hash` and the device report, and validate each set that ships.
8. **A per-driver micro-probe in the repository.** Extend `research/GpuProbe`
   (Vulkan only today) with D3D12 + DXC, WARP and Dozen modes, and keep small
   WGSL extracts of the kernels (the F1 probe: decode, interpolation, smooth branch)
   with a WARP comparison. This is how F1's code was cleared in isolation.
9. **A correctness canary at device creation.** The host already has a
   compile-time canary (`CODEBRIX_CYCLES_PIPELINE_CANARY_*`). A tiny correctness
   check (for example a smooth-shaded diffuse quad, GPU film compared with the CPU
   film) would turn a silent wrong image like F1 into a reported device warning or a
   refusal, before users see black surfaces.
10. **Coverage and tooling.** A smooth-shaded BSDF scene in the host gate (the
    gate passed despite F1); `render_oracles.py --payload <dir>` that assembles the
    run folder like `windows_host_gate.py --payload` (F2).

## Triage policy for the device run

The device run is diagnostic, not a porting effort. Unattended runs follow these
limits:

- **Allowed:** reading logs and code; re-running a step into a new output
  directory; narrowing a failure to one scene, film or kernel (including edited
  copies of scene bundles in new directories); using options and environment
  variables the tools already support (`--scene`, the `CODEBRIX_CYCLES_*`
  variables) without changing tolerances or backends; querying the device
  (drivers, GpuProbe).
- **Time box:** about 30 minutes of diagnosis per distinct problem. Then the
  problem is recorded here as a finding (symptom, reproducer command, output
  directory, best hypothesis) and the run moves on to the next step.
- **Not done on the device:** editing product code, scripts, tests or the
  payload; rebuilding binaries (nothing is built on the device); changing
  `provenance.json`; relaxing tolerances, adjusting films or changing backend
  settings; writing into `native/evidence/*`; installing software; commits and
  pushes.
- **Fixes:** a cause that needs a code change is proposed as a finding with the
  suggested change, for the maintainer to apply on the build machine, re-stage
  and re-validate on both RIDs. ARM64-only workarounds are not developed on the
  device.
- **Stop:** if a GPU step hangs the device or triggers repeated driver resets
  (TDR), GPU steps stop and the remaining CPU-only work continues.

## Run record

Committed evidence: `native/evidence/windows-arm64-device-20261025/` (4.5 MB).
The device's own `native/work/` names are given for reference.

| Step | Status | Evidence subdirectory | On the device (`native/work/`) |
|---|---|---|---|
| 1. Host gate D3D12 | VALIDATED | `gate-d3d12` (status, results, comparisons, logs; no payload copy) | `windows-arm64-validation-d3d12-1` |
| 2. Managed tests | FAILED (3 GPU tests, F1) | `logs` (`build.log`, `tests-nongpu.log`, `tests-gpu.log`) | |
| 3. Oracle corpus | FAILED (13 WGSL scenes, F1/F3/F4) | `oracle-windows-arm64-2` (films, `run.json`), `logs/oracle-2.log` | `oracles/windows-arm64-2`; first attempt `oracles/windows-arm64-1` (F2); run folder `windows-arm64-oracle-run-1` |
| 4. Cross-platform | VALIDATED | `logs/cross_compare.py` (against `native/evidence/cycles-oracles-macos-20261023`) | |
| 5. Vulkan | NOT AVAILABLE | `gate-vulkan` | `windows-arm64-validation-vulkan-1` |
| F1 reproducer, micro-probe, host comparison | | `f1-smooth-normal` (bundles, films, `bisect.py`, `filmdiff.py`, `wgsl-probe/`, `host-compare/`) | `windows-arm64-smooth-normal-finding-1` |
| F3/F4 host comparison | | `f3-f4-host-compare` | `windows-arm64-oracle-host-compare-1`; x64 run folder `windows-x64-emulated-run-1` |

The summary scripts used for steps 3 and 4 (`oracle_summary.py`,
`oracle_fail_table.py`, `scene_features.py`, `cross_compare.py`) are in `logs/`.

## Reference policy and tolerances

As in TESTING-WINDOWS-X64.md: the unmodified Cycles CPU kernels are the oracle;
the practical criterion `abs(a - b) <= 5e-5 + 2e-4 * abs(b)` is required; the
strict criterion `abs(a - b) <= 1e-7 + 1e-7 * abs(b)` is reported and required
only where the Linux host gate requires it; NaN or Inf always fails; same-host
CPU films must be bitwise identical. Tolerances are never relaxed and films are
never adjusted.
