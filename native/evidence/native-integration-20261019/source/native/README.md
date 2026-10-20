# Batched native WebGPU backend

The following checkpoint covers the working snapshot renderer. The subsequent
[registered device gate](DEVICE.md) now connects Cycles' original scene upload
and GPU scheduler control to WebGPU. Registered camera/intersection, supported
surface/volume shading and film output pass the bounded native image corpora.
See [current integrated support and limits](#integrated-handoffs-and-current-limits-2026-10-19)
for the merged features and remaining gaps.

This is the next step from the parity harness toward a reusable Cycles device.
`libcodebrix_cycles_webgpu.so` owns the WebGPU device, pipelines, scene buffers,
bounded path batches, shadow work and film. A C ABI exposes creation, scene
replacement, rendering and destruction. The renderer and backend are native
C++; the Python scripts only build, deploy and compare results. No CLR or
Slang runs in the rendering path.

The limited library gate passes on Linux x64 and all three Android devices,
including ARM64 and x64 at API 33. Every image is byte-identical to that
device's earlier finite-light GPU image and passes comparison against the
original Cycles CPU megakernel. Full Cycles parity remains open; the known
strict kernel discrepancies in [the parity ledger](../PARITY.md) remain.

![Native library render](evidence/batched-backend/linux-x64/render.png)

## What moved into the library

Each batch executes surface integration, shadow traversal and film
accumulation as three ordered compute passes. Batches stay within a sample,
so each invocation owns one pixel and successive passes accumulate samples
without floating-point atomics. GPU scratch buffers are reused across batches.
No per-path data is read back, and no CPU shadow traversal or film summation
is used. The final readback contains the image and 16 bytes of status.

Pipelines are created once per context. Repeated renders reuse scene buffers
and pipelines. Scene replacement validates and copies input before installing
new resources; rejected input preserves the existing scene. Caller-owned
scene memory can be released immediately after a successful upload.

The current 96×72, 16-sample, four-bounce fixture uses these GPU allocations:

| Allocation | 4,096 paths per batch | 1,021 paths per batch |
| --- | ---: | ---: |
| Scene tables | 1,058,288 bytes | 1,058,288 bytes |
| Scratch, batch uniforms and status | 1,581,072 bytes | 420,752 bytes |
| Film | 110,592 bytes | 110,592 bytes |
| Readback buffer | 110,608 bytes | 110,608 bytes |
| Batches / compute dispatches | 32 / 96 | 112 / 336 |

These are requested buffer sizes, not total driver or process memory. Scratch
path storage scales with batch capacity; uniforms scale with batch count and
film storage with resolution. This small fixture does not establish production
throughput or large-scene viability.

## Measured results

The unchanged tolerance is `5e-5 + 2e-4 * abs(reference)` per linear RGBA
component. No components fail on any tested device.

| Device | API | Maximum image component error | Library lifecycle tests |
| --- | --- | ---: | --- |
| Linux x64, Intel UHD | — | 0.0000130534 | Pass |
| HP Android x64, Intel Xe | 33 | 0.0000130534 | Pass |
| Pixel Android ARM64, Adreno 640 | 33 | 0.00000435114 | Pass |
| Samsung Android ARM64, Adreno 730 | 36 | 0.00000435114 | Pass |

Lifecycle tests clear all caller scene buffers after upload, render repeatedly,
reject nine malformed or unsupported scene/render cases, replace and restore
lighting, and render using a second context with a non-multiple-of-64 batch
size. Repeated/restored images and both batch sizes match exactly. Repeated
renders allocate no new GPU buffers or pipelines; scene replacement reuses
all three pipelines.

The Pixel's earlier silent all-zero monolithic shader result motivated
explicit completion checks. Path output is cleared before reuse; a completed
surface path writes an alpha marker, each active shadow must have a resolved
visibility value, and accumulation counts batches and paths. Nonfinite output
or incomplete work returns an error before touching the caller's image.
Three Linux fault builds independently replace surface integration, shadow
resolution or accumulation with an empty shader. All three are detected and
preserve every caller output value. This detects those missing-stage failures;
it does not prove detection of arbitrary GPU miscomputation.

After the shared camera indexing and shadow completion changes, the standalone
finite-light gate was regenerated and rerun on Linux and Pixel. All four GPU
output buffers and all six validation reports match the frozen previous
checkpoint exactly. The strict gate still exits 1 for its existing numerical
failures; no tolerances or failing cases were changed.

All runs use the exact shipped `CodeBrix.WebGpu.Native.MitLicenseForever`
1.0.290.1433 bytes. Builds audit the NuGet archive, cache and sibling repository;
runs identify the loaded library and verify its bytes, including on Android.
No optional GPU features, WebGPU patches or alternate implementations are used.
The backend selects hardware Vulkan and has no software-renderer fallback.

## Reproduce

Build the pinned native Cycles host and dependencies as described in
[the prototype instructions](../prototype/README.md#reproduce-on-this-machine).
Then run:

```sh
python3 parity/tools/light_gate.py prepare
python3 native/tools/run.py linux
python3 native/tools/run.py faults
python3 native/tools/run.py android --serial 98271FFBA0048D
python3 native/tools/run.py android --serial 192.168.86.225:5555
python3 native/tools/run.py android --serial R52W207XQ1V
```

The existing original CPU host supplies build-time kernel enum metadata;
it is not a runtime dependency of the GPU shared library. Clang 19/C++20 and
the pinned NDK compile the three tested RIDs with warnings treated as errors.
Android builds target API 33. Android execution uses an `adb shell` native
driver and the Linux-prepared snapshot; a full Android Cycles host and app
sandbox have not been tested.

`tools/build.py --rid RID` builds the library and driver independently.
`tools/archive.py` creates the frozen `evidence/batched-backend` checkpoint
and refuses to overwrite it. Evidence contains shader/source snapshots,
input tables, reference film, result reports, previews, build commands and
hashes. Native dependency binaries are not redistributed in that checkpoint.

## API scope and remaining integration

[include/cycles_backend.h](include/cycles_backend.h) defines experimental ABI 1,
with distinct `cbc_backend_*` symbols from the earlier prototype. Set struct
sizes and versions explicitly. Errors return a status and caller-owned message;
C++ exceptions are caught at the API boundary. Rendering is synchronous and
returns linear RGBA with the Cycles lower-left film origin. Calls on one context
are serialized; destruction must not race a call. Output is copied only after
successful readback and validation. Cancellation and shared external-device
handoff are not implemented.

The descriptor accepts three little-endian snapshot buffers prepared by
`parity/native/path_host.cpp`, not an arbitrary Cycles scene. They contain
static triangle BVH2/SVM data, a static perspective camera with tabulated Sobol
tables, and constant finite spherical lights. Validation rejects unsupported
SVM opcodes, closure mixtures, unsupported projection/sampling layouts and
invalid offsets or cycles. This format cannot represent textures, motion,
volumes or the other unimplemented features; the upstream exporter must reject
features it cannot represent rather than silently flattening their behavior.

Current limits are four surface bounces, up to 64 lights, 64–65,536 configured
paths per batch, 65,536 batches, dimensions up to 4,096 and 1–256 samples.
Individual input tables are limited to 64 MiB; actual device buffer limits
can impose tighter limits. Supported shading is single Lambert, Oren–Nayar
or translucent closures, constant world emission and finite-light NEE/MIS.

This remains a snapshot bridge: it is **not yet registered as a `ccl::Device`
or driven by the existing Cycles `DeviceQueue`/session scheduler**. The separate
[registered device implementation](DEVICE.md) now runs native scene preparation,
scheduling, shading and film output for the documented bounded subset. New
point/sun MIS, glass/refraction and camera motion pass integrated Linux state,
image and combined-scene tests. Constant spot/area lights also pass their merged
Linux sampling, state, image and replay corpus. Emissive triangle lights,
scene-driven shader specialization with compile-budget reporting, split
direct-lighting and transparent-shadow stages, and ARM64-oracle normal rounding
pass the integrated Linux corpus with cold shader caches. Wider
material/light/geometry coverage, Android revalidation of the integrated tree and
numerical discrepancies remain tracked there.
Complete cross-platform parity, remaining RID builds, release packaging and
final redistribution review remain unfinished. The final package must depend on the existing
WebGPU package and honor its single-library/load-order contract.

See [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt) for shader derivations.


## Integrated handoffs and current limits (2026-10-19)

The working tree combines the sealed transparent-shadow, deforming-triangle and
finite homogeneous-volume slices. The combined Linux regression is PASS in
`native/work/parallel-integration/all-lanes-validation-v2`: 29 targets built,
78 independent CPU/GPU scene comparisons and 45 exact command replays with
22,789 scheduling readbacks. It includes the new feature corpora and shared
world-volume, glass/GGX, camera-motion and broader-lighting regressions. All
production source, executable and package-runtime hashes stayed unchanged during
testing. Strict differences and repeated-film differences remain recorded.

The first combined run crashed in Intel's Vulkan compiler while creating the
transparent-shadow pipeline. Reducing that shading workgroup from 64 to 1,
with matching dispatch sizing, passes the unchanged corpus. Opaque traversal
retains 64; the original crash/backtraces are preserved in `all-lanes-crash-v1`.
Large-scene performance and wider driver coverage remain unmeasured.

- Transparent/colored shadows support static triangles and bounded hit storage
  with resumed traversal. Moving transparent shadows, volume shadow attenuation,
  light/shadow linking, AO, MNEE and shadow catchers remain guarded. The stable
  GPU queue gather scans path capacity serially; large-scene cost is unmeasured.
- Deforming triangles use native motion frames, interpolated vertex/corner
  normals, instance transforms and opaque-shadow traversal. Curves, points,
  subdivision, animated emitters and transparent-shadow/volume combinations
  remain unsupported or unvalidated; their guards are retained.
- Finite homogeneous volumes include native GPU density preparation, camera-
  inside stack initialization, entry/exit and overlapping HG media. Volume
  NEE/MIS, emission/light passes, shadow attenuation, heterogeneous media and
  moving scenes remain guarded. Mixed-anisotropy duplicate compaction is
  rejected because the independent native CPU closure assignment loses HG data
  in padding; the original failure is preserved and the oracle is unchanged.
- These handoffs now have combined Linux evidence. Their new Android paths
  remain unvalidated. Full Cycles lighting/MIS is not complete: mesh/environment
  sampling and MIS, light trees, textured emission and remaining interactions
  still require implementation and proof.

All tests continue to use the unchanged CodeBrix.WebGpu.Native.MitLicenseForever
1.0.290.1433 binaries, independent original CPU references and unchanged practical
and strict diagnostic criteria. Recorder v5 uses slots 9 (ShaderData), 10 (volume
stack), 11 (volume transport), 12 (transparent shadows), 13 (surface lighting)
and 14 (homogeneous density); earlier capture formats remain supported.


## Wrap-up: triangle-light candidate deferred (2026-10-19)

All completed sub-agent handoffs above remain integrated and uncommitted.
A subsequent primary-agent triangle-light/MIS candidate passes focused sampler,
surface and four static-image/replay checks, but its final transparent-lighting
regression crashes. The candidate and failure evidence are preserved under
`native/work/mesh-light-v1/deferred-candidate`; it is excluded from production.
The validated pre-extension source was restored by exact hash, retaining the
original triangle-light guards and recorder v5. Restoration and follow-up results
are recorded under `native/work/mesh-light-v1` and in section 44 of Jeremy's plan.
Before reintegration, resolve the transparent-lighting regression and the retained
instanced-emitter image mismatch, then validate moving emitters and broader
interactions. Environment/textured emission, light trees and remaining full MIS
work are unfinished. No percentage increase or new Android coverage is claimed.
Superseded: the transparent-lighting crash was diagnosed (an intermittent Intel
Mesa compiler fault) and the candidate was reintegrated; see below.

## Integrated lighting, specialization and ARM adapters (2026-10-19)

The registered device now includes scene-driven WGSL specialization (only the SVM
handlers, closures, area/triangle lights and motion code the uploaded scene can
reach are compiled; a reached removed handler reports an error), per-pipeline
compile-budget warnings with a conservative fallback tier after creation
failures, flat-CDF constant-emission triangle lights with forward-hit MIS
(including moving and deforming emitters), separate light-sampling and
BSDF/shadow-connection stages, a separate ShaderData stage for transparent
shadows, and oracle-host-selected `normalize(float3)` rounding plus rounded
cosine-hemisphere and valid-normal adapters for ARM64 parity. Static instanced
triangle emitters, light trees, environment/textured emission and the other
listed lighting interactions remain guarded or unimplemented. Command captures use
recorder v7 (entry 15 `connect_surface_light`, entry 16
`prepare_shadow_shader_data`); v1-v6 captures still replay. Details and the
validation summary are in [DEVICE.md](DEVICE.md).
