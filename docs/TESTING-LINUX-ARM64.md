# Testing on Linux ARM64 (Raspberry Pi 5, WebGPU)

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

Instructions for building, running and recording the WGSL/WebGPU renderer on a
Raspberry Pi 5: Linux ARM64, Vulkan through the shipped runtime's
`linux-arm64` binary, Broadcom V3D with Mesa's V3DV driver. The cross-build
recipe is described in [LINUX_ARM64.md](../native/host/LINUX_ARM64.md); this
document is the ordered test procedure built on it.

Lines marked **MISSING** name a step for which the repository has no recipe or
tool yet. Do not work round them on the Pi; record what was done instead and
bring the output back. Since 2026-10-29 the Pi builds HEAD natively ("Native
build on the device" below), and several MISSING items are resolved; each says
so where it stands.

## Status (2026-10-29): first device run, WebGPU runtime 1.0.300.210

**Verdict.** The shipped `CodeBrix.WebGpu.Native.MitLicenseForever` 1.0.300.210
linux-arm64 runtime renders correctly on the Raspberry Pi 5's V3D 7.1 (Mesa
V3DV 26.2.2) wherever it was run: 16 GPU films against the same-host CPU
references, all within the practical criterion. It needed one host fix to run at
all (L1). Coverage stops there: the V3DV compiler takes minutes to hours per
Cycles pipeline when cold (L2), so the device gates, the driver-coverage scenes
and the oracle corpus were not reached in 10 hours. On this board the GPU is
practical only with a warm driver cache and a fixed scene; the CPU reference
backend rendered the smooth-shading coverage scene in 0.03 to 0.045 s per
render while the GPU's compile of the same scene was still running after 9.5
hours.

Evidence: `native/evidence/device-linux-arm64-20261029/` (13 MB, records only, no
binaries; README there). Device: Raspberry Pi 5 Model B Rev 1.1, 16 GB, Debian 13
(trixie), glibc 2.41, GCC 14.2.0, clang 19.1.7, CMake 3.31.6, .NET SDK 10.0.403.

| Check | Result |
|---|---|
| Package | nupkg SHA-256 `ddfd92f8...` equals `third_party/dependencies.json`; `libcodebrix_wgpu.so` `3fb3a382...` loaded as shipped |
| Native build (dependencies, host, all device gates, oracle runner) | passed: 18 components in 806 s; 65 artifacts in 2,572 s; CPU kernels at the fixed baseline (`-ffp-contract=off -fno-reciprocal-math`, no `-march`/`-mcpu`) |
| Generated WGSL | byte-identical to the Linux x64 reference overlay (`linux-x64-docker-reference-20261027`) once the F1 fix is accounted for: at HEAD only the 3 modules embedding it differ; with the pre-fix `wavefront_shader_data.wgsl` all 19 files match |
| P2, libm probe | glibc 2.41 on ARM64 bit-identical to Linux x64 glibc for all 11 functions (same result digests); 99 s |
| Native host gate: binary review | passed: 48 exports, loader closure within the build, the prefix, the pinned runtime and the C/C++ runtime only; 547 system headers, all from `libc6-dev`, `libstdc++-14-dev`, `libgcc-14-dev`, `linux-libc-dev` |
| CPU references, consumers, colour reloads, concurrency | passed: 1,568 same-target films bitwise identical |
| `gpu`, `gpu-exit` (first WebGPU lifecycle on the V3D) | **passed**: 11 GPU films against the CPU Session references, 0 practical failures, 0 strict differences, max 1.19e-7; repeats bitwise identical |
| ABI test, GPU | stopped while compiling (L2); its `base` film (max 3.0e-8, 0 strict) and four light films (max 1.5e-5; 159/22/1/0 strict differences, reported, as on Linux x64 Intel) pass the practical criterion; the motion films were not reached |
| Example, driver coverage, cross-platform CPU comparison, device gates, oracle corpus, managed tests, glTF client | **not reached** (L2) |
| P4, Samsung triangle sampler | not run: needs the Samsung device-run inputs of the same commit, which were not on the Pi. **Since 2026-10-29 they are in the repository** (`native/evidence/android-reference-20261029/samsung-triangle-sample/`, commit `686f79282`; section 5) |

### L1. The host refused the V3D: downlevel adapter limits (fixed 2026-10-28)

**Symptom.** `codebrix_host_gpu_test` exited with `cbc_host_create(&config,
&host) returned 2: Requested device is unavailable`, nothing else.

**Cause.** wgpu treats the V3D 7.1 as a downlevel adapter (not fully WebGPU
compliant) and gives a device its downlevel defaults for every limit the
request leaves undefined. The host requested only the two buffer-size limits,
so the device got `maxStorageBuffersPerShaderStage` 4 (WebGPU default 8), and
`cbc::Device` then refused it ("Seven storage bindings required"), although the
adapter supports 8. Also lower than the WebGPU defaults: `maxUniformBufferBindingSize`
16,384 (65,536) and `maxComputeWorkgroupStorageSize` 16,352 (16,384). Found with a
scratch probe against the shipped runtime (`probe/` in the evidence). No other
platform's GPU is downlevel, so none met it.

**Fix** (`native/src/webgpu_device.cpp`): those three limits are requested at
the WebGPU default, capped at what the adapter supports. A compliant adapter
gets exactly its defaults, so Intel, Adreno, Apple and NVIDIA devices are
unchanged (not rerun on those platforms; a host gate there after the change
confirms it). With the fix the V3D device has 8 storage buffers per stage and
the lifecycle passes.

**Error reporting** (`native/cycles/device.cpp`, `native/host/cycles_host.cpp`):
the WebGPU device's own reason was discarded during device enumeration, which
is why L1 (and Windows F8, an ABI mismatch) showed only "Requested device is
unavailable". The message now carries it, for example "Requested device is
unavailable: Seven storage bindings required". Compiled only into WebGPU builds.

### L2. V3DV compiles the Cycles kernels very slowly (open)

Mesa's V3D compiler takes minutes to hours per pipeline, cold. The GPU
lifecycle test (8 pipelines) took 4,571 s cold and 17.6 s warm:

| Pipeline (lifecycle test, alone) | Cold compile | Linux x64 Intel (median of genuine cold compiles in the evidence) |
|---|---|---|
| `intersect_closest` | 1,269 s | - |
| `prepare_surface` | 1,208 s | 16.2 s |
| `shade_surface_light` | 723 s | 3.0 s |
| `connect_surface_light` | 670 s | 5.8 s |
| `shade_surface` | 242 s | 7.0 s |
| `prepare_shader_data` | 236 s | 1.6 s |
| `shade_background` | 158 s | 4.1 s |
| `init_camera` | 40 s | - |

That is 35 to 240 times Intel, about 100 times overall. Other scene
specializations were slower still in the 10-hour run, with three compiles sharing the
four cores (so contended): `prepare_surface` for `linux-gates/glass-render`
11,015 s, `shade_surface_light_triangle` (ABI test) 10,303 s,
`connect_surface_light` (glass-render) 7,289 s. The kernel log of the window has
no V3D errors or resets, and nothing swapped. Each Vulkan pipeline reaches the
driver with every function inlined (130 to 500 KB of WGSL per entry point); why
the V3D backend scales so badly with that size was not profiled.

Mesa's on-disk shader cache works on V3DV: warm, every lifecycle pipeline loads
in 2 to 10 ms and the film is byte-identical to the cold run's. A consumer pays
the cold compile once per scene specialization and Mesa version, not per frame
or per render job. Validation that completes on this device therefore needs
either days of unattended cold compiling (estimated 2 to 5 days serial for the
gate set, from the Intel compile totals of a validation round) or smaller
kernels. Proposed, not done: create pipelines concurrently (Mesa compiles each on
one core); split the largest entry points further, as was done for the Android
compilers; use the runtime's new Vulkan pipeline-cache exports (the maintainer
decision in MAINTAINER-README.txt) so applications can ship or pre-warm caches.

### Notes from the run

- **N1. Native system headers.** On Debian the native `/usr/include/<triple>/asm`
  resolves into `/usr/lib/linux/uapi` (`linux-libc-dev`). The native binary review
  accepts that directory and additionally requires every system header to belong
  to one of the four toolchain packages (`dpkg -S`) and every system library in
  the loader closure to be part of the C/C++ runtime. The Pi has
  `zlib1g-dev`, `libpng-dev` and `libexpat1-dev` installed; no compile used them.
- **N2. The non-GPU managed test filter is not GPU-free.** `-noclass
  CodeBrix.Graphics3D.Cycles.Tests.GpuRenderTests` (TESTING-WINDOWS-ARM64.md, step 2)
  still runs `WebGpuAmbientOcclusionTests`, which renders on the GPU; on the Pi it
  was cold-compiling after 10 minutes and the run was stopped. Exclude that class
  too where GPU compiles are slow.
- **N3. Build records include the gate scripts.** `render_gate.inputs()` hashes the
  whole tree outside `work/` and `evidence/`, so editing any tool (a gate script
  included) makes the next gate refuse the build record; an incremental
  `build_cycles_linux_arm64.py` rerun (about 65 s) re-records it.
- **N4.** The host gates write `comparisons.json` only when they end; a gate stopped
  from outside leaves its comparisons unwritten (the lanes run's manual-stop
  record compares the films that exist).

## Native build on the device

Since 2026-10-28 the Linux ARM64 recipes also run natively on an ARM64 Linux
machine and choose that mode by themselves (`platform.machine()` aarch64): the
system GCC 14 (`/usr/bin/gcc-14`, `/usr/bin/g++-14`) and glibc, no emulator, a
toolchain file that sets only the compilers. The cross-build on Linux x64 is
unchanged (its identity record is the same). The WebGPU runtime is never built:
the host takes the package's `libcodebrix_wgpu.so` from the NuGet cache and
records its SHA-256.

Requirements on the Pi, beyond section 2: `gcc-14`/`g++-14`, `clang` (the WGSL
translation's AST dump), `cmake`, `make`, `time`, the .NET 10 SDK **10.0.403 only**
(no earlier version) for the managed steps, and the package
(`dotnet restore research/GpuProbe/GpuProbe.csproj`).

```sh
python3 tools/dependencies/build_linux_arm64.py --jobs 4
python3 native/tools/build_cycles_linux_arm64.py --jobs 4 --device-set
export MESA_SHADER_CACHE_DIR=$PWD/native/work/<run>/mesa-cache MESA_SHADER_CACHE_MAX_SIZE=8G
python3 native/tools/linux_arm64_host_gate.py --output native/work/linux-arm64-validation-<n> \
  --references native/evidence/linux-x64-reference-20261030/host-gate \
  --device-gates [--step-timeout <seconds>]
python3 native/tools/linux_arm64_lanes.py --output native/work/linux-arm64-lanes-<n> --cap-seconds 36000
```

- `--device-set` builds the default set, every device gate of section 4, the
  extra host tests and (natively) the oracle runner; it replaces the explicit
  `--target` list of section 3 and can be rerun.
- The host gate's native mode adds the GPU steps of `host_gate.py` and
  `windows_host_gate.py` (lifecycle, ABI, example on `CBC_HOST_GPU`, against the
  same-host CPU references), checks that the adapter is hardware Vulkan, renders
  the four driver-coverage scenes and, with `--device-gates`, runs every built
  gate and counts its JSON verdict (on the Pi the gate's in-process glibc
  reference is the judge, section 5). `--step-timeout` (default 3,600 s) bounds
  each GPU step; it never changes a result.
- `linux_arm64_lanes.py` runs jobs in parallel lanes under one cap, in the job form
  of section 4, sharing the Mesa cache (default lanes: driver coverage; the host
  gate and one gate per family; Principled and nodes). Concurrent walls are
  contended.
- `render_oracles.py` has the backends `cpu-arm64-linux` and
  `wgsl-linux-arm64-vulkan` and records the board model and `vulkaninfo`; the
  corpus has not been rendered on the Pi yet.
- The managed tests load `native/work/linux/linux-arm64/host/build/lib/libcodebrix_cycles_host.so`
  on Linux ARM64 (its RUNPATH names the prefix and the package's runtime).

## Device run 2026-10-28 (run record)

On HEAD `9388e9878` with the uncommitted Linux ARM64 work (the native recipes,
the gate and lane runner, L1). Walls on the Pi; `native/work/` names given for
reference.

| Step | Wall | Result | Evidence subdirectory | `native/work/` |
|---|---|---|---|---|
| Package restore and hash check | 39 s | passed | `README.md` | - |
| P2 libm probe | 99 s | passed (diagnostic) | `device-runs/libm-*` | `linux-arm64-device-1` |
| Dependency prefix | 806 s | passed | `build/dependencies-*` | `linux/linux-arm64/dependencies` |
| Host, gates, oracle runner (`--device-set`) | 2,572 s | passed | `build/host-*`, `build/invocation.json` | `linux/linux-arm64/host` |
| Gate run 1 | 17 s | stopped: binary review, kernel UAPI header (N1) | `stopped-attempts/gate-run-1` | `linux-arm64-validation-1` |
| Gate run 2 | 16 s | stopped: build record stale (N3) | `stopped-attempts/gate-run-2` | `linux-arm64-validation-2` |
| Gate run 3 | 73 s | failed: `gpu`, L1 | `stopped-attempts/gate-run-3` | `linux-arm64-validation-3` |
| L1 fix, rebuild; `gpu` alone | 1,800 s | stopped at 3 x the P3 budget while compiling (L2) | `stopped-attempts/gpu-first-try-after-L1` | - |
| `gpu` cold, empty Mesa cache | 4,571 s | passed (its own checks) | `device-runs/gpu-cold` | `linux-arm64-device-1/gpu-cold` |
| `gpu` warm | 17.6 s | passed; film byte-identical to the cold run | `device-runs/gpu-warm` | `linux-arm64-device-1/gpu-warm` |
| Managed build (SDK 10.0.403) | 63 s | 0 warnings, 0 errors | `device-runs/managed-build.output.txt` | - |
| Managed tests without `GpuRenderTests` | 631 s | stopped (N2) | `device-runs/tests-nongpu-stopped-1.output.txt` | - |
| Gate run 4 (`--device-gates`, warm cache) | 3,693 s | stopped at `abi-gpu` (3,600 s limit, L2); every earlier step passed | `gate-run-4` | `linux-arm64-validation-4` |
| Lanes run (10-hour cap) | 34,215 s | stopped at the maintainer's request after 9.5 hours: every lane still on its first job (L2) | `lanes-1` (`manual-stop.json`) | `linux-arm64-lanes-1` |

Next on this device, when wanted: the jobs of the lanes run continue from the
populated Mesa cache (`native/work/linux-arm64-device-1/mesa-cache`), so a run
without a cap, or a later run after an L2 change, does not repeat the compiles
already made; then the managed tests (excluding `WebGpuAmbientOcclusionTests`
first, N2), the glTF client and the oracle corpus.

## 1. Purpose and evidence produced

The Linux ARM64 recipe cross-compiles on the Linux x64 machine and its host
gate runs under QEMU, which proves emulated CPU paths and nothing about an
ARM64 GPU. A run of this procedure produces:

- the first real Linux ARM64 GPU evidence: the WebGPU host lifecycle and the
  native device gates running on the Pi's V3D GPU, each judged against the
  Pi's own CPU reference;
- a glibc ARM64 CPU reference computed on the Pi from the Samsung SM-X700's
  own dumped triangle-sampler scene, which decides the open Samsung triangle
  sampler item (sample 3133) and the Samsung ARM64 float3 question (section 5);
- a libm record of the Pi's glibc against correctly rounded results;
- the QEMU host gate of the same build, on the Linux x64 machine;
- device and driver identity, walls per step, and every failure or driver
  refusal with its logs.

## 2. Prerequisites and pins

Pins (the build scripts check them; do not change them):

- Cycles revision `a456b761034dda42c32eef9f4aae0fa5a5c9f604`, the filtered
  snapshot in `third_party/cycles` (`third_party/cycles-import.json`).
- CodeBrix.WebGpu.Native.MitLicenseForever 1.0.300.210, unchanged. The Pi
  loads its `runtimes/linux-arm64/native/libcodebrix_wgpu.so`. The build reads
  the package from the NuGet cache,
  `~/.nuget/packages/codebrix.webgpu.native.mitlicenseforever/1.0.300.210`.
- Dependency sources only from the verified snapshots in `third_party/deps`;
  nothing is fetched.

Two machines take part. The Pi runs the binaries; it does not build the
renderer.

**Resolved 2026-10-28** ("Native build on the device"): the Linux ARM64 recipes
build natively on the Pi. The original note: **MISSING:** a native build on the Pi. `tools/build.py` refuses any machine
other than Linux x86_64, and the Linux ARM64 recipes are cross-builds that
expect the x64 machine's cross compiler, target SDK and QEMU
(`tools/dependencies/build_linux_arm64.py` records `buildHost` linux-x64). A
native ARM64 recipe would need its own toolchain identity, dependency prefix
and host build without the cross toolchain file.

### Linux x64 build machine (Debian)

Already required for the Linux x64 build (MAINTAINER-README.txt, BUILDING):
Python 3, CMake 3.24 or newer, Make, a C/C++20 toolchain, Clang, the pinned
runtime package in the NuGet cache. Additionally, for the ARM64 cross-build
(the scripts check these exact paths):

- `/usr/bin/aarch64-linux-gnu-gcc-14` and `/usr/bin/aarch64-linux-gnu-g++-14`
  (Debian packages `gcc-14-aarch64-linux-gnu`, `g++-14-aarch64-linux-gnu`);
- the target SDK under `/usr/aarch64-linux-gnu` with `ld-linux-aarch64.so.1`,
  `libc.so.6`, `libstdc++.so.6` and `libgcc_s.so.1` (`libc6-dev-arm64-cross`,
  `libstdc++-14-dev-arm64-cross`);
- `aarch64-linux-gnu-readelf` and `aarch64-linux-gnu-nm`
  (`binutils-aarch64-linux-gnu`);
- `/usr/bin/qemu-aarch64` (`qemu-user`).

### Raspberry Pi 5 (Debian-based OS, apt only)

The maintainer installs these with apt; nothing else in this procedure uses
`sudo` on the Pi:

- `python3`
- `gcc` (builds the libm probe)
- `mesa-vulkan-drivers` (V3DV) and `libvulkan1`
- `vulkan-tools` (`vulkaninfo`, identity only)
- `time` (`/usr/bin/time`, walls)
- `rsync` (copying the build from the x64 machine)
- `git`

No .NET SDK is needed on the Pi for the cross-built procedure (section 4, step
P9); the native build on the device needs SDK 10.0.403, and only that version.
No other GPU stack is needed; the host chooses only a hardware Vulkan adapter
and ignores `llvmpipe`.

Checkout on both machines, at the same commit:

```sh
git clone https://github.com/ellisnet/CodeBrix.Graphics3D.Cycles
cd CodeBrix.Graphics3D.Cycles
git rev-parse HEAD
git status --porcelain
```

## 3. Build commands (Linux x64 machine)

All commands run from the repository root.

```sh
python3 tools/build.py --jobs 6
python3 native/tools/host_gate.py --output native/work/host-validation-<n>
python3 tools/build_linux_arm64.py --jobs 4
```

The first builds the Linux x64 host, gates and generated WGSL; the second is
the Linux x64 host gate whose passed output the ARM64 host gate requires as
references; the third builds the ARM64 dependency prefix
(`native/work/linux/linux-arm64/dependencies/install`) and the ARM64 host,
reference executables and the default gate set
(`native/work/linux/linux-arm64/host/build/{bin,lib}`).

The default ARM64 set (shared with the Android recipe: 8 host outputs and 26
gates, 34 artifacts, including principled_svm, transparent_shadow, the
environment, light-tree and shadow-catcher gates and the two ambient-occlusion
and two MNEE gates) holds only part of the device gates. Build the rest with
explicit targets (these targets have not been built for this RID before;
record any failure):

```sh
python3 native/tools/build_cycles_linux_arm64.py --jobs 4 \
  --target codebrix_host_abi_test \
  --target codebrix_cycles_triangle_sample_gate --target codebrix_cycles_triangle_lighting_gate \
  --target codebrix_cycles_emitter_sample_gate --target codebrix_cycles_lighting_gate \
  --target codebrix_cycles_light_state_gate --target codebrix_cycles_lighting_mis_gate \
  --target codebrix_cycles_broad_light_state_gate --target codebrix_cycles_broad_lighting_gate \
  --target codebrix_cycles_feature_combination_gate \
  --target codebrix_cycles_transparent_lighting_gate --target codebrix_cycles_shadow_terminator_gate \
  --target codebrix_cycles_shadow_gate --target codebrix_cycles_object_motion_render_gate \
  --target codebrix_cycles_deforming_motion_render_gate --target codebrix_cycles_glass_svm_gate \
  --target codebrix_cycles_glass_bounce_gate --target codebrix_cycles_glass_render_gate \
  --target codebrix_cycles_volume_gate --target codebrix_cycles_volume_render_gate \
  --target codebrix_cycles_volume_boundary_gate --target codebrix_cycles_closed_volume_render_gate \
  --target codebrix_cycles_principled_bounce_gate \
  --target codebrix_cycles_principled_render_gate --target codebrix_cycles_nodes_svm_gate \
  --target codebrix_cycles_nodes_world_gate --target codebrix_cycles_nodes_render_gate
```

After this, do not rerun `tools/build_linux_arm64.py` in the same tree: its
default run asserts the default artifact count and fails once the extra
targets are present. To rebuild, use `build_cycles_linux_arm64.py` with
explicit `--target` arguments.

The oracle runner is not part of the cross-build (`native/host/targets.cmake`
adds it only when not cross-compiling). Build it standalone against the ARM64
host with the cross toolchain file the dependency build wrote (not yet run for
this RID):

```sh
cmake -S native/oracle -B native/work/linux/linux-arm64/oracle-build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=native/work/linux/linux-arm64/dependencies/toolchain.cmake \
  -DCODEBRIX_HOST_LIBRARY=$PWD/native/work/linux/linux-arm64/host/build/lib/libcodebrix_cycles_host.so
cmake --build native/work/linux/linux-arm64/oracle-build
```

Copy the build to the Pi, keeping the same paths relative to the repository
root and the same NuGet cache path (`<pi>` is the Pi's checkout, for example
`pi@raspberrypi:CodeBrix.Graphics3D.Cycles`):

```sh
rsync -a native/work/linux/linux-arm64/host/build/bin native/work/linux/linux-arm64/host/build/lib \
  <pi>/native/work/linux/linux-arm64/host/build/
rsync -a native/work/linux/linux-arm64/host/status.json native/work/linux/linux-arm64/host/invocation.json \
  <pi>/native/work/linux/linux-arm64/host/
rsync -a native/work/linux/linux-arm64/dependencies/install/lib \
  <pi>/native/work/linux/linux-arm64/dependencies/install/
rsync -a native/work/linux/linux-arm64/oracle-build/codebrix_cycles_oracle \
  <pi>/native/work/linux/linux-arm64/oracle-build/
rsync -a ~/.nuget/packages/codebrix.webgpu.native.mitlicenseforever/1.0.300.210 \
  <pi-home>/.nuget/packages/codebrix.webgpu.native.mitlicenseforever/
```

On the Pi, check that the copies match `status.json` (`artifacts` lists the
SHA-256 of every binary) with `sha256sum`, and set the library path once per
shell:

```sh
export LD_LIBRARY_PATH=$PWD/native/work/linux/linux-arm64/host/build/lib:$PWD/native/work/linux/linux-arm64/dependencies/install/lib:$HOME/.nuget/packages/codebrix.webgpu.native.mitlicenseforever/1.0.300.210/runtimes/linux-arm64/native
```

## 4. Ordered steps and budgets

On the Linux x64 machine the budget is the measured wall itself. On the Pi,
budgets are derived from the Linux x64 reference walls of a full validation
round on the Dell Precision 7770 laptop (Intel UHD ADL-S):
**Pi budget = 5 x the Linux x64 wall, at least 60 s per job.** The multiplier
is an estimate for the Pi's four Cortex-A76 cores and the V3D GPU against the
laptop, not a measurement. Rule: record the wall of every step; stop a step at
3 x its budget and bring back its log. A stopped step is a recorded result,
not something to retry with a longer budget.

Run every Pi gate in a new directory, one job at a time, with the pipeline log
on and `OCIO` unset. The form of every Pi job (`<out>` is the run's new output
directory, `<job>` the label from the tables, `<stop>` 3 x the budget in
seconds):

```sh
mkdir -p <out>/<job>
env -u OCIO CODEBRIX_CYCLES_PIPELINE_LOG=<out>/<job>/pipelines.jsonl [ENV...] \
  /usr/bin/time -f '%e' -o <out>/<job>.wall.txt \
  timeout <stop> native/work/linux/linux-arm64/host/build/bin/codebrix_cycles_<gate>_gate <out>/<job>/run [ARGS...] \
  > <out>/<job>.stdout.txt 2> <out>/<job>.stderr.txt
echo $? > <out>/<job>.exit.txt
```

A job passes when it exits 0, its stdout is one JSON report with
`"passed": true` and `cyclesAllocationBytesAfterCleanup` 0 (when present),
and its `run.library.txt` (`run/library.txt` for the image gates) names the
`libcodebrix_wgpu.so` of the pinned package. Never set
`CODEBRIX_CYCLES_PIPELINE_TIER`, `CODEBRIX_CYCLES_PIPELINE_BUDGET_MS`,
`CODEBRIX_CYCLES_PIPELINE_BUDGET_ACTION` or `CODEBRIX_CYCLES_SHADER_SPECIALIZATION`
to make a job pass.

**Partly resolved 2026-10-28:** `linux_arm64_host_gate.py --device-gates` runs every
built gate natively and counts its JSON verdict, and `linux_arm64_lanes.py` runs
jobs in this form with walls, exits and kernel logs; the per-gate count
assertions of the x64 scripts and the job table's arguments for every row are not
applied yet. The original note: **MISSING:** a Linux ARM64 device runner. The Linux x64 gate scripts in
`native/tools/` (for example `triangle_lighting_gate.py`, `principled_gate.py`)
run only the x64 build under `native/work/cycles-build`, and the Android
corpus runner that mirrors their assertions, enforces budgets and checks
staged hashes is a work tool, not in the repository. On the Pi the gate's own
JSON verdict (GPU against the in-process CPU reference) is the result; the
scripts' additional count assertions are not applied. A maintained runner
would take the job list below, stage and hash-check the binaries, apply the
per-gate assertions and write one results file.

### Linux x64 machine

| Step | Command | Linux x64 wall | Budget |
| --- | --- | --- | --- |
| X1 | `python3 tools/build.py --jobs 6` | 242 s (clean host build, dependencies reused) | 242 s; record the full-dependency wall |
| X2 | `python3 native/tools/host_gate.py --output native/work/host-validation-<n>` | 119 s (native targets already built) | 119 s; MAINTAINER-README.txt allows about 20 minutes when the gate has to rebuild |
| X3 | `python3 tools/build_linux_arm64.py --jobs 4` | no measurement yet | record the wall |
| X4 | extra ARM64 targets and the oracle runner (section 3) | no measurement yet | record the wall |
| X5 | QEMU host gate (below) | no measurement yet | record the wall; script timeouts up to 2,400 s per call |

```sh
python3 native/tools/linux_arm64_host_gate.py \
  --output native/work/linux-arm64-validation-<n> \
  --references native/work/host-validation-<n>
```

It reviews compiler sources and headers, archives, ARM64 ELF segments, every
export in `native/host/cycles_host.h` and the emulated loader closure, then
runs the native Session references, the C lifecycle and process-exit
consumers, three colour configurations and six reload rounds, and the
concurrent-lifetime test under QEMU. Same-target CPU comparisons require
bitwise identity; cross-platform images use the practical criterion with
strict diagnostics; cross-platform lifecycle films also require strict
agreement. It proves emulated CPU paths only (`actualArm64GpuProven` false).

### Raspberry Pi 5

| Step | What | Linux x64 wall | Pi budget |
| --- | --- | --- | --- |
| P1 | identity record (section 7) | - | 60 s |
| P2 | libm probe | 23 s | 115 s |
| P3 | native host executables | covered by the host gate, 119 s | 600 s for the set |
| P4 | Samsung scene reference (section 5) | 257 s for the whole Linux reference export | 600 s |
| P5 | triangle group | 126 s (triangle lighting gate) | 630 s for the group |
| P6 | device gate corpus (table below) | per row | per row |
| P7 | materials | 775 s (Principled gate), 1,863 s (node gate) | 3,875 s / 9,315 s per group |
| P8 | oracle runner (optional) | no measurement yet | record the wall |
| P9 | managed tests | not run on the Pi | - |

**P2, libm probe.** Records the Pi glibc's deviation from correctly rounded
results for the functions Cycles calls (diagnostic; it decides no gate):

```sh
python3 native/tools/libm_probe.py linux <out>/libm-linux-arm64-glibc.json
```

**P3, native host executables.** Run each in its own directory with the job
form above, replacing the gate path with the host executable:

| Job | Executable and arguments |
| --- | --- |
| session | `codebrix_host_session_reference CPU <out>/session/run` |
| session64 | `codebrix_host_session64_reference CPU <out>/session64/run` |
| cpu | `codebrix_host_cpu_test <out>/cpu/run` |
| gpu | `codebrix_host_gpu_test <out>/gpu/run` |
| gpu-exit | `codebrix_host_gpu_test <out>/gpu-exit/run --exit-live` |
| abi-gpu | `codebrix_host_abi_test <out>/abi-gpu/run gpu` |

`gpu` is the first real WebGPU lifecycle on the Pi's GPU: its report must show
`explicitWebGpu` and `repeatStrictCriterionPassed`.

**Resolved 2026-10-28:** `linux_arm64_host_gate.py` has a native mode with the
GPU-versus-CPU comparisons ("Native build on the device"). The original note:
**MISSING:** a native (non-QEMU) Linux ARM64 host gate.
`native/tools/linux_arm64_host_gate.py` runs every executable through
`/usr/bin/qemu-aarch64` with the cross SDK and reviews ELF files with the
cross binutils, so it cannot run on the Pi; the GPU films of `gpu` are
therefore not compared with the `session` CPU films by any tool. A native mode
would drop the emulator prefix, take the system loader and binutils, and add
the GPU-versus-CPU comparisons that `native/tools/host_gate.py` makes on
Linux x64.

**P5 and P6, device gate corpus.** Order and arguments follow the Android
device corpora. Linux x64 walls are the reference walls of the gate or gate
script that runs the same binary; where no per-job wall exists, the row says
so and the budget is a stated 600 s per job.

| Order | Job | Gate | Arguments / environment | Linux x64 wall | Pi budget |
| --- | --- | --- | --- | --- | --- |
| 1 | triangle_sample | triangle_sample | - | in the triangle group, 126 s | 630 s for the group |
| 2 | surface | surface | - | 13 s | 65 s |
| 3 | triangle_lighting | triangle_lighting | - | in the triangle group | (group) |
| 4 | emitter_sample | emitter_sample | - | 13 s | 65 s |
| 5 | lighting | lighting | - | no per-job wall | 600 s |
| 6 | light_state | light_state | - | no per-job wall | 600 s |
| 7 | lighting_mis | lighting_mis | - | no per-job wall | 600 s |
| 8 | broad_light_state | broad_light_state | - | no per-job wall | 600 s |
| 9 | broad_lighting | broad_lighting | - | no per-job wall | 600 s |
| 10 | feature_combination | feature_combination | - | no per-job wall | 600 s |
| 11 | glass_render | glass_render | - | 266 s (four specialization groups) | 1,330 s |
| 12 | transparent_shadow | transparent_shadow | - | no per-job wall | 600 s |
| 13 | transparent_lighting | transparent_lighting | - | no per-job wall | 600 s |
| 14 | shadow_terminator | shadow_terminator | - | 103 s | 515 s |
| 15 | object-moving-images | object_motion_render | - | no per-job wall | 600 s |
| 16 | object-stationary-images | object_motion_render | `--stationary` | no per-job wall | 600 s |
| 17 | deforming-moving-images | deforming_motion_render | - | no per-job wall | 600 s |
| 18 | deforming-stationary-images | deforming_motion_render | `--stationary` | no per-job wall | 600 s |
| 19 | camera-static | camera | - | no per-job wall | 600 s |
| 20 | camera-motion | camera | `--motion` | no per-job wall | 600 s |
| 21 | render-static | render | - | no per-job wall | 600 s |
| 22 | render-motion-0 ... render-motion-7 | render | `--motion=0` ... `--motion=7` | no per-job wall | 600 s each |
| 23 | glass_svm | glass_svm | - | no per-job wall | 600 s |
| 24 | glass_bounce | glass_bounce | - | no per-job wall | 600 s |
| 25 | microfacet_svm | microfacet_svm | - | no per-job wall | 600 s |
| 26 | microfacet_bounce | microfacet_bounce | - | no per-job wall | 600 s |
| 27 | microfacet_render | microfacet_render | - | no per-job wall | 600 s |
| 28 | shader_data | shader_data | - | 6 s | 60 s |
| 29 | closest | closest | - | 5 s | 60 s |
| 30 | closest-motion | closest | `--motion` | 9 s | 60 s |
| 31 | closest-deform | closest | `--deform` | 6 s | 60 s |
| 32 | closest-deform-motion | closest | `--deform-motion` | 9 s | 60 s |
| 33 | shadow | shadow | - | 75 s | 375 s |
| 34 | svm_surface | svm_surface | - | 10 s | 60 s |
| 35 | svm_world | svm_world | - | 25 s | 125 s |
| 36 | normal | normal | - | 31 s | 155 s |
| 37 | attribute | attribute | - | 20 s | 100 s |
| 38 | image | image | - | 24 s | 120 s |
| 39 | image_file | image_file | - | 1 s | 60 s |
| 40 | volume-state | volume | - | no per-job wall | 600 s |
| 41 | volume-absorption | volume_render | `CODEBRIX_VOLUME_ABSORPTION_ONLY=1` | no per-job wall | 600 s |
| 42 | volume-isotropic | volume_render | `CODEBRIX_VOLUME_G=0` | no per-job wall | 600 s |
| 43 | volume-forward | volume_render | `CODEBRIX_VOLUME_G=.45` | no per-job wall | 600 s |
| 44 | volume-backward | volume_render | `CODEBRIX_VOLUME_G=-.65` | no per-job wall | 600 s |
| 45 | boundary-state | volume_boundary | - | no per-job wall | 600 s |
| 46 | boundary-compaction-guard | volume_boundary | `CODEBRIX_VOLUME_COMPACTION_CASE=1` | no per-job wall | 600 s |
| 47 | closed-single | closed_volume_render | `CODEBRIX_VOLUME_CLOSED_COUNT=1` | no per-job wall | 600 s |
| 48 | closed-overlap | closed_volume_render | `CODEBRIX_VOLUME_CLOSED_COUNT=4` | no per-job wall | 600 s |
| 49 | closed-inside | closed_volume_render | `CODEBRIX_VOLUME_CLOSED_COUNT=4 CODEBRIX_VOLUME_CAMERA_INSIDE=1` | no per-job wall | 600 s |
| 50 | atomic_film | atomic_film | takes no output prefix: run it without `<out>/<job>/run` (CPU atomic contract test) | no per-job wall | 600 s |
| 51 | device | device | - | no per-job wall | 600 s |
| 52 | background | background | - | no per-job wall | 600 s |

"No per-job wall": the Linux x64 reference round ran these inside one
combined validation run (1,526 s for the whole set) without separate walls.

**P7, materials** (last, because they compile the largest modules):

| Job | Gate | Linux x64 wall | Pi budget |
| --- | --- | --- | --- |
| principled_svm, principled_bounce, principled_render | principled_svm, principled_bounce, principled_render | 775 s for the Principled gate script | 3,875 s for the group |
| nodes_svm, nodes_world, nodes_render | nodes_svm, nodes_world, nodes_render | 1,863 s for the node gate script | 9,315 s for the group |

**MISSING:** command replays and the specialization identity check on the Pi.
`native/tools/render_replay.py` builds the replay executable only for
linux-x64, android-arm64 and android-x64, so neither the Pi's own v8
captures nor the Linux x64 captures can be replayed there; and the superset
bit-identity check (`CODEBRIX_CYCLES_SHADER_SPECIALIZATION=superset`) is made
by `glass_gate.py`, `principled_gate.py` and `nodes_gate.py`, which run only
the x64 build. A linux-arm64 entry in `render_replay.py` (compiler
`aarch64-linux-gnu-g++-14`, the package's `runtimes/linux-arm64/native`) and
an ARM64 mode of those scripts would close both.

**P8, oracle runner (optional).** The runner renders `cbcOracleScene` files
on the Pi's CPU and GPU:

```sh
native/work/linux/linux-arm64/oracle-build/codebrix_cycles_oracle \
  --scene native/oracle/scenes/starter/diffuse-triangle.json --backend cpu \
  --label cpu-arm64-linux --output <out>/oracle/starter/diffuse-triangle/cpu-arm64-linux --threads 4
```

and the same with `--backend webgpu`.

**Resolved 2026-10-28, not yet run:** `render_oracles.py` has `cpu-arm64-linux` and
`wgsl-linux-arm64-vulkan` and records the board model. The original note:
**MISSING:** Linux ARM64 backends in `native/oracle/render_oracles.py`. Its
backend list holds only the macOS and Linux x64 names, so the corpus loop,
`meta.json` enrichment, per-scene comparisons and bundling are not available
for Pi films; the runner above writes bare `meta.json` and films only. Adding
`cpu-arm64-linux` and a `wgsl-linux-arm64-*` backend (with a non-Darwin
identity that reads the Pi's model) would make the Pi a third oracle
platform.

**P9, managed tests.** Not run on the Pi: the test project's
`NativeHostLibrary` loads only the Linux x64 host and skips every native test
on ARM64. **Resolved 2026-10-28:** `RepositoryPaths.cs` now has the branch
(the build only; the tests were not run to completion, N2). The original note:
**MISSING:** a linux-arm64 branch in
`tests/CodeBrix.Graphics3D.Cycles.Tests/NativeHostLibrary.cs` loading
`native/work/linux/linux-arm64/host/build/lib/libcodebrix_cycles_host.so`;
until it exists, a managed run on the Pi proves only that the tests skip.

### If the Pi's GPU or driver refuses a module

A pipeline-creation error, a lost device ("Parent device is lost"), a driver
crash (signal exit, no JSON report), a V3D job timeout or reset, or a tier
fallback to conservative (`failure` reason in the pipeline log) is a recorded
result:

1. Do not retry with a different tier, budget, specialization mode or
   workgroup setting, and do not edit WGSL or the runtime.
2. Keep the job's whole directory: `pipelines.jsonl`, `.stdout.txt`,
   `.stderr.txt`, `.exit.txt`, `.wall.txt` and any `run.*` files.
3. Save the kernel log for the window of the job (the maintainer, or a user in the
   `adm` group): `journalctl -k --since "<job start>" > <out>/<job>.kernel.txt`.
4. Write the reproducer: the exact command line and environment from the job
   form.
5. Continue with the next job; a failure in one job does not stop the corpus.

## 5. Reference policy and tolerances

- The CPU kernels are the oracle, built at the fixed baseline on both machines
  (native/DEVICE.md, "Fixed-baseline CPU oracle"): no FMA contraction and no
  reciprocal math (`-ffp-contract=off -fno-reciprocal-math` from the shared
  CMake injection), Cycles' SSE4.2 kernels on Linux x64 and the ARMv8-A base
  level on ARM64 (no `-march`/`-mcpu`, also when the recipe runs on the Pi
  itself). The recipes refuse a CPU kernel compile line that is not at the
  baseline. The ARM64 cross build at this rule compiles on the Linux x64
  machine; it has not yet been run on the Pi.
- Practical criterion, must pass for every component:
  `abs(a - b) <= 5e-5 + 2e-4 * abs(b)`, with `b` the reference.
- Strict diagnostic, reported, never required:
  `abs(a - b) <= 1e-7 + 1e-7 * abs(b)`.
- NaN or Inf always fails. Tolerances are never relaxed.
- The compile envelope checked by `native/tools/intel_compile_envelope.py`
  (every entry point below 60,000 backend instructions) is a rule for the
  Intel Mesa backend of the Linux x64 machine and does not apply to V3D. On the
  Pi a module the driver cannot compile is recorded as described under "If the
  Pi's GPU or driver refuses a module"; the WGSL is never changed on the Pi.

**Which reference judges the Pi.** On an ARM64 device the judge is a glibc CPU
reference computed from the device's own scene, and the device CPU is a
diagnostic. On Android the device CPU uses bionic, so the glibc reference is
computed on Linux x64 from the device's dumped scene (native/DEVICE.md,
"Validation reference policy"). On the Pi the device CPU itself runs glibc
with the device's own scene, so the in-process CPU reference of every gate
(the default run, no `CODEBRIX_CYCLES_SCENE_*` or
`CODEBRIX_CYCLES_REFERENCE_*` variables) is that judge. The WGSL arithmetic
adapters follow it: `reference_oracle_sse()` in `native/cycles/camera.cpp`
returns false on a non-SSE, non-Android build, so the kernels reproduce the
ARM64 scalar oracle.

**MISSING:** judging the Pi's GPU against the Linux x64 glibc reference of the
Pi's own scene. The switch to the x64 oracle arithmetic
(`CODEBRIX_CYCLES_REFERENCE_ORACLE=linux-x64-sse4.2`) is compiled only for
Android builds, so a device-scene run against an x64 reference on the Pi would
compare scalar-adapter GPU output with SSE arithmetic. It would need that
branch extended to Linux ARM64.

### The Samsung triangle sampler (sample 3133) and the ARM64 float3 question

Open item 1 in native/DEVICE.md: on the Samsung SM-X700, sample 3133 of the
triangle sampler (deforming emitter, object 5, primitive 4) differs from the
Linux x64 glibc reference of the Samsung's own scene: `P.y` GPU 0.382186,
x64 glibc 0.379992, Samsung CPU (bionic, ARM64) 0.380744. Separately, the
Samsung CPU and the x64 glibc reference disagree on the deforming emitter in a
way the Linux reference attributes to ARM64 float3 arithmetic
(`NATIVE_FLOAT3_RECIPROCAL_NORMALIZE`, Cycles' scalar path without SSE), not
libm. The Pi computes the missing fourth value: glibc ARM64 arithmetic on the
same scene.

Inputs: **committed since 2026-10-29** in
`native/evidence/android-reference-20261029/samsung-triangle-sample/` (`<samsung>`
below; `SHA256SUMS` lists every file). They come from the tablet's device-scene run
of the `triangle` corpus on the Linux x64 machine at commit `686f79282`. The Pi
build must prepare scenes exactly as that commit does: `686f79282` itself, or a
later commit that changed no scene preparation (the scene loader refuses tables
of another layout, below). If it refuses, ask for a newer Android evidence
directory from the Linux x64 machine; never make these files anywhere else. The
files:

- `scenes/triangle_sample/samples.scene` and `samples.local.f32` (the
  Samsung's dumped scene and its bionic CPU reference);
- `device-scene-reference/references/triangle_sample/samples.f32` and
  `device-scene-reference/triangle_sample.provenance.json` (the x64 glibc
  reference of that scene);
- `output/triangle_sample/run.actual.bin` (the Samsung GPU samples) and
  `run.reference.bin`, `run.reference-diagnostic.jsonl`.

If the scene file was written by a different commit, scene loading fails with
"Scene exchange element size differs" or "order differs"; the Samsung run must
then be refreshed on the Linux x64 machine at the Pi's commit before this step.

**P4: the glibc ARM64 reference of the Samsung's scene** (on the Pi, with the
library path set):

```sh
python3 native/tools/android_reference.py scene-reference \
  --bin native/work/linux/linux-arm64/host/build/bin \
  --scenes <samsung>/scenes/triangle_sample \
  --output <out>/samsung-scene-arm64 --label triangle_sample --gate triangle_sample
```

It runs the Pi's triangle-sampler gate on CPU only with
`CODEBRIX_CYCLES_SCENE_LOAD` and `CODEBRIX_CYCLES_REFERENCE_EXPORT`, writes
`references/triangle_sample/samples.f32` and records the platform (`machine`
aarch64, glibc version), the gate binary hash and how the Pi's own scene
preparation differs from the Samsung's.

**Comparisons that settle it** (on the Pi or on the Linux x64 machine):

```sh
python3 native/tools/android_reference.py compare --kind samples \
  --run <samsung>/output/triangle_sample/run \
  --reference <out>/samsung-scene-arm64/references/triangle_sample \
  --output <out>/samsung-gpu-vs-arm64-glibc.json
```

gives the Samsung GPU against glibc ARM64 (row `authoritative`) and the
Samsung's bionic CPU against glibc ARM64 (`diagnosticLocalVersusGlibc`). The
two references against each other, and sample 3133 from all four sources:

```sh
python3 -c "import sys,json; sys.path.insert(0,'native/tools'); import android_reference as r; print(json.dumps(r.compare_arrays(r.floats(sys.argv[1]), r.floats(sys.argv[2])), indent=1))" \
  <out>/samsung-scene-arm64/references/triangle_sample/samples.f32 \
  <samsung>/device-scene-reference/references/triangle_sample/samples.f32
python3 -c "import sys; sys.path.insert(0,'native/tools'); import android_reference as r; [print(p, list(r.floats(p)[3133*12:3133*12+12])) for p in sys.argv[1:]]" \
  <samsung>/output/triangle_sample/run.actual.bin \
  <samsung>/device-scene-reference/references/triangle_sample/samples.f32 \
  <samsung>/scenes/triangle_sample/samples.local.f32 \
  <out>/samsung-scene-arm64/references/triangle_sample/samples.f32
```

Each sample is 12 floats; field 0 is validity and field 2 is `P.y`. The first
one-liner compares every float, validity words included.

**MISSING:** a maintained command that compares two reference sets; the
one-liners above call `compare_arrays` and `floats` from
`native/tools/android_reference.py` directly.

What each outcome means (bring all four values back whatever the outcome; the
decision is the maintainer's):

| Pi glibc ARM64 at sample 3133 | Meaning |
| --- | --- |
| equals (or practically matches) x64 glibc 0.379992, and the reference sets agree on the deforming emitter | The ARM64 float3 explanation is refuted: glibc on ARM64 reproduces the x64 reference, so the Samsung CPU's deviation comes from bionic or the Android build, not ARM64 arithmetic. The x64 glibc reference stands as judge; sample 3133 stays a Samsung GPU fix with no exception. |
| matches the Samsung CPU 0.380744, and the Samsung CPU agrees with glibc ARM64 on the deforming emitter where it disagreed with x64 | The ARM64 float3 explanation is confirmed: the deviation is ARM64 scalar arithmetic, which no x64 reference reproduces. The Samsung GPU (0.382186) still differs from both CPU values, so the GPU deviation remains; whether ARM64 devices keep the x64-arithmetic reference for this path is a policy decision. |
| matches the Samsung GPU 0.382186 | The GPU computes what ARM64 scalar arithmetic computes although it runs with the x64-oracle adapters; the adapter for this path does not take effect on the Adreno, which localises the fix. |
| matches none of them | A fourth value; record it as a finding, with the reference-set comparison, for the maintainer. |

The libm record from P2 shows whether the Pi's glibc is correctly rounded for
the same functions as x64 glibc (`acosf`, `asinf`, `atanf`, `atan2f`,
`tanf`, `sqrtf`); if it is not, the comparison mixes libm and arithmetic
differences and must say so.

## 6. Bundle to bring back

There is no oracle bundle for this platform yet (P8 MISSING). Bring back ONE
new directory, for example `native/evidence/device-linux-arm64-<date>/`,
never inside an existing `native/evidence/*` checkpoint, containing:

1. the identity record and the walls table (section 7);
2. from the x64 machine: `native/work/linux-arm64-validation-<n>/` (QEMU host
   gate), the ARM64 build records `native/work/linux/linux-arm64/host/status.json`,
   `invocation.json`, `platform.patch`, `webgpu-compatibility.json`, and
   `native/work/linux/linux-arm64/dependencies/provenance.json`; configure
   and build logs renamed from `.log` to `.output.txt` (the repository
   ignores `*.log`), with the renames listed;
3. from the Pi: the whole `<out>` directory (P2 to P8), including every
   failed or stopped job's directory and kernel log;
4. the P4 reference directory and the comparison JSON files;
5. if P8 ran, the oracle films with their `meta.json`, in the layout
   `oracle/<scene id>/<label>/film-N.f32`.

Never copy executables or libraries into evidence; record their SHA-256
(`status.json` lists them).

## 7. What to record

Identity, on the Pi:

```sh
cat /proc/device-tree/model; echo
uname -a
cat /etc/os-release
ldd --version
lscpu
free -h
vulkaninfo --summary
dpkg -s mesa-vulkan-drivers libvulkan1 | grep -E '^(Package|Version)'
gcc --version
python3 --version
git rev-parse HEAD
git status --porcelain
```

and on the x64 machine `aarch64-linux-gnu-g++-14 --version`,
`qemu-aarch64 --version`, `cmake --version`, `git rev-parse HEAD`.

The WebGPU adapter as the host sees it (vendor, device, driver description,
tier, compile budget) is in each job's `pipelines.jsonl` and in the gate
reports; record it once from the `gpu` job.

Per step: the command, start time, wall (`.wall.txt`), exit status, and
whether it passed, failed or was stopped at 3 x budget. Per failure: the
first error line, the reproducer, the files listed in section 4 ("If the Pi's
GPU or driver refuses a module"). For compile-heavy jobs, the largest
`pipelineMs` in `pipelines.jsonl`.

## 8. Standing rules

- Never commit evidence into a frozen checkpoint: every existing
  `native/evidence/*` directory is immutable. New output goes to new
  directories only.
- Never relax a tolerance, change a tier, budget or specialization mode to
  make a job pass, or rerun until it passes.
- Never modify the shipped CodeBrix.WebGpu.Native runtime or the pinned
  snapshots under `third_party/`; the Pi loads the package's binary as
  shipped.
- Nothing run on the Pi uses `sudo`; packages are the maintainer's to install, with
  apt only.
- Never commit from the test machine without reviewing the complete list of
  new and changed paths (`git status --porcelain`) first; build output under
  `native/work/` stays uncommitted (it is ignored).
- Record every MISSING step that blocked or limited the run.
