# Registered Cycles device and wavefront queue

The new `native/cycles` implementation registers `DEVICE_WEBGPU` with the
original Cycles device factory and implements `DeviceQueue`. The Linux gate
passes against the unchanged CodeBrix.WebGpu.Native package. This connects
scene storage, scheduler control, limited camera initialization, closest
intersection and constant/SVM-world background/film output to WebGPU. It does not
yet render surfaces through a complete Cycles session. The earlier
[snapshot renderer](README.md) remains
the separate, working image reference on Linux and Android.

## What is implemented

`Scene::device_update` runs directly against `WebGPUDevice`. Native Cycles
builds the BVH and SVM programs and uploads its original device tables. The
backend keeps their raw layouts, including `KernelData`, and assigns virtual
byte addresses to `device_ptr` values. Scene data is no longer prepared on a
CPU device for this test. Scene preparation itself remains native host work,
as it does with other Cycles GPU devices.

The first allocator uses one WebGPU storage-buffer arena with 16-byte aligned
allocations, coalescing free ranges, stable virtual addresses and GPU-to-GPU
growth copies. Zero/copy/readback operations respect queue order. It is bounded
by the device's single storage-binding limit; paging or multiple arenas will
be needed before claiming large-scene support. This is not a performance gate.

The original `PathTraceWork::create` selects `PathTraceWorkGPU` for this device.
The original GPU scheduler allocates its native structure-of-arrays state,
uploads `IntegratorStateGPU`, queues reset and reads its queue counters through
the new backend. No CPU rendering or alternate scheduler stands in for these
operations.

Twelve scheduler kernels have WGSL implementations:

- Reset both main and shadow path queues.
- Select queued main/shadow paths, active main paths, terminated main/shadow
  paths, and main/shadow paths needing compaction.
- Compute shader-count prefix sums and select sorted paths with deferred work
  when a dispatch limit is reached.
- Compact main and shadow states.

WGSL's 32-bit atomic storage also carries Cycles' 8-bit and 16-bit fields.
Masked atomic updates prevent adjacent state writes from overwriting each
other. State-copy descriptors come from the original state templates and native
`sizeof` values; all allocated fields are copied, including vector payloads
and array members. MNEE's cross-path ownership repair is explicitly rejected
until implemented.

## Control checkpoint

The [Linux report](evidence/device-queue/linux-x64/validation.json) records:

| Check | Result |
| --- | --- |
| Device discovery, type names and factory creation | Pass |
| Original GPU path-work selection, allocation and reset | Pass |
| Original scene tables uploaded and read back unchanged | 11 |
| Reset sizes, including zero and partial workgroups | 7 |
| Path-selection modes, membership/counts and guards | 7 |
| Sorting limits, including zero and partial buckets | 6 |
| Main and shadow compaction versus original helpers | 2 |
| Native state fields / state slots exercised | 123 / 4,099 |
| Invalid inputs, unsupported MNEE and invalid GPU sort key | 7 rejected |
| Camera dispatch at this earlier checkpoint | Explicit error; implemented below in the next checkpoint |
| Cycles allocation accounting after cleanup | Zero bytes |

The prefix-sum and sorted-index references execute the original upstream
helpers. State compaction executes the untouched original state-copy/move
function bodies using host arrays, with MNEE disabled in that oracle. Every
byte of every tested state field is compared, including untouched guards.
Selection membership is compared exactly; atomic reservations may produce a
different valid order within a shader bucket, so sorting verifies bucket
membership, unique indices, counts, deferred counters and output guards.

Additional tests force arena growth after a queued GPU write and verify its
result at the same virtual address. They check range reuse, a host upload
following a pending kernel, and an unaligned partial-byte readback. A bad sort
key reaches the GPU check and is reported through the queue's status word.

The main gate made 30 compute dispatches. Its numerous host transfers inspect
individual fields for correctness; their counts are not a production workload
or a throughput measurement. Only Linux x64/Intel Vulkan has executed this
new registered-device gate. The previous four-device image evidence remains
valid for the snapshot renderer, not for these new scheduler kernels.

## Camera entry through the original scheduler

`DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA` now runs WGSL against the original
native `KernelData`, sampler/filter tables and `IntegratorStateGPU` arrays.
The original scheduler's `enqueue_work_tiles` uploads the actual work tiles,
dispatches the camera entry, reads the queue counters, and selects
`INTERSECT_CLOSEST` next. No CPU-prepared per-path rays or replacement scheduler
are involved. Native `offsetof`/`sizeof` values generate the WGSL layout
constants for the current ABI.

This entry implements static perspective pinhole rays, tabulated Sobol, pixel
filter or explicit jitter, clipping, compact ray differentials, path state
initialization and the queue transition. It also implements atomic film sample
reservation, the existing adaptive-pixel skip flag, and indexed camera retries.
Other camera modes, motion/depth of field and optional initialization features
(volumes, guiding, denoising, light linking and portals) are explicitly rejected.
Reading adaptive skip flags is not a complete adaptive-sampling implementation.

The [camera report](evidence/camera-queue/linux-x64/validation.json) records:

| Check | Result |
| --- | --- |
| Configurations, covering four scrambling distances and both work orderings | 24 |
| Initialized paths across multi-tile dispatches | 22,972 |
| Indexed retry paths and queue-counter transitions | 65 |
| Integer/state/guard comparisons | 3,422,188 |
| Floating-point comparisons | 254,276 |
| Untouched state bytes checked | 11,734,032 |
| Unsupported configurations, invalid film/pointers/tiles/indices | 12 rejected |
| Original scheduler selects closest intersection next | Pass |
| Cycles allocation accounting after cleanup | Zero bytes |

The reference executes the unchanged original CPU camera, path initialization
and film functions on the same prepared scene data. All non-ray state and film
bytes must match exactly. Atomic sample assignment can reorder samples among
lanes: the test first proves one-to-one membership in the independently computed
CPU `(pixel, sample)` set, then compares every resulting state. Untouched slots
and skipped pixels retain their expected bytes.

Ray checks retain the earlier camera tolerance, `5e-5 + 2e-4 * abs(reference)`.
Maximum absolute errors were `9.53674e-7` for position/direction/time/minimum
distance, `6.70552e-8` for compact differentials, and `0.000244141` for maximum
distance (the fixture uses a 1,000-unit clip length). The separate strict
`1e-7 + 1e-7 * abs(reference)` comparison reports **7,668 failing components**;
strict numerical parity has not passed. These are retained in the evidence,
not excluded or hidden by a changed tolerance.

The twelve scheduler-control kernels and six package-audit tests also pass with
this expanded backend. Their rerun is saved alongside the camera evidence. The
control gate now uses the still-unimplemented intersection entry to verify
unsupported dispatch rejection at that checkpoint. This integrated camera gate has run only on
Linux x64/Intel Vulkan; it does not establish Android or Metal camera parity.

## Closest intersection and shader queues

`DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST` now reads the original native BVH2,
triangle, object, shader and path-state arrays. It traverses static triangles
and static native instances, including nonuniform and negative scales. Object
and primitive identities remain separate for self-hit rejection. Node and
primitive visibility checks preserve Cycles' transmission and reflection rules.
The triangle-intersection arithmetic is generated from the pinned upstream AST;
the BVH traversal and stage scheduling are WGSL adaptations of the original
functions. Camera and intersection share native state access and Sobol helpers.

The stage writes intersection records, makes Russian-roulette and AO bounce
termination decisions, and transitions paths to background, surface or raytrace
surface queues. Emissive hits preserve the original deferred-termination flags.
It writes the original GPU shader-sort keys and counters; the original scheduler
then consumes those counters using its prefix and sorted-path entries. Selecting
a raytrace shader queue does not implement AO/bevel material evaluation itself.
Likewise, the complete roulette path still needs shading's throughput handling.

The [closest-intersection report](evidence/closest-queue/linux-x64/validation.json)
records:

| Check | Result |
| --- | --- |
| Original scheduler camera → closest dispatch | Pass |
| Configurations / paths | 22 / 36,226 |
| Native instance leaves / distinct hit objects | 3 / 5 |
| Surface hits / misses | 4,107 / 32,119 |
| Terminated paths | 8,496 |
| Fractional continuation probabilities / emissive deferred termination | 1,700 / 390 |
| Raytrace surface queue selections | 568 |
| Original scheduler consumes surface and raytrace sort counters | 2 cases |
| Direct dispatch sizes, without an index array | 1, 63, 64, 65, 193 |
| Exact state/guard comparisons / float comparisons | 3,009,016 / 144,904 |
| Unsupported or invalid inputs | 16 rejected |
| Cycles allocation accounting after cleanup | Zero bytes |

The original CPU `integrator_intersect_closest` runs on the same scene and ray
inputs. For this stage comparison, both implementations receive the GPU camera
rays, which isolates intersection from camera rounding; the separate camera
regression still compares against the original CPU camera. The oracle enables
Cycles' optional local-struct initialization hook to make otherwise undefined
CPU miss fields deterministic. Original function bodies are unchanged. GPU-only
sorting keys/counters are checked against the original sorting formula, and
the original scheduler's sort routines verify their consumption.

The maximum intersection/continuation absolute error is `9.53674e-7`. The
existing `5e-5 + 2e-4 * abs(reference)` gate passes; the separately retained
`1e-7 + 1e-7 * abs(reference)` check has **370 failing components**. This does
not resolve the earlier strict numerical failures. All nonnumeric state,
queue counters, shader-sort counters and untouched guards must match exactly.
The camera and control regression gates retain their previous results.

Motion geometry, curves, points, volumes, shadow catchers/linking, MNEE,
non-tabulated samplers and forward light intersection remain explicitly
unsupported in this entry. It is not a complete Cycles render yet. Traversal
uses the upstream 192-entry stack and a 1,048,576-iteration failure guard; large
scene limits and performance remain work items. Invalid indices, roots,
geometry references and leaf types report errors. Only Linux x64/Intel Vulkan
has executed this integrated gate, using the unchanged package binary.

## Background entry and native film

The registered backend now runs `integrator_shade_background` for Cycles'
constant-emission world fast path. A native Cycles scene goes through the
original GPU scheduler's camera, closest-intersection and background entries
to the original film layout: 64 x 48 pixels at eight samples per pixel. This
world-only scene exercises the complete implemented sequence; surface shading
and full `Session` rendering remain unfinished.

The film port implements combined RGB/transparency, the background pass,
lightgroups, diffuse/glossy/transmission direct and indirect routing, light
clamping, AO background scaling, adaptive auxiliary accumulation and background
depth/position/object/material passes. Shader visibility exclusions follow
Cycles' path flags. Float accumulation uses integer compare-and-swap on the
existing storage arena, with no optional GPU features or runtime changes.
As in Cycles, combined alpha storage accumulates transparency; it is not final
display alpha. Original CPU `integrator_shade_background` supplies the oracle.

The Linux gate covers 26 cases and 55,681 paths, including noncontiguous queues,
partial workgroups and 4,096 paths accumulating to one pixel. All 3,940,352
state comparisons and 3,833,856 film comparisons pass the existing tolerance
`5e-5 + 2e-4 * abs(reference)`. An initial run had 195 stricter float failures
at `1e-7 + 1e-7 * abs(reference)`, with maximum absolute error 0.000244141;
atomic accumulation order can change those counts. This is not bitwise parity.
Twelve rejection cases leave the film and queue counters unchanged, and Cycles
allocations return to zero after cleanup. Controls, camera and closest-stage
regressions run alongside it.

The SVM extension below handles selected nonconstant world shaders.
Sun lights, background MIS, volumes, shadow catchers, denoising, path guiding,
light/shadow linking and the background motion pass are explicitly unsupported
in this entry. Surface and shadow integration remain next. The earlier standalone snapshot renderer's material
support is not yet connected to this registered shading entry.

The root [license inventory](../THIRD-PARTY-NOTICES.txt) records the upstream
atomic-header GPL issue, which Jeremy is resolving. These are development
results; the native host is not cleared for the intended Apache release.

## Native SVM world shaders

The background entry interprets Cycles' original typed SVM bytecode in WGSL.
Native Cycles compiles each graph and uploads its `svm_nodes` table unchanged;
the backend derives node sizes, field offsets and enum values from the same
compiled C++ headers. The per-path stack stores 32-bit words, preserving integer
values whose bit patterns would be NaNs when viewed as floats. It checks stack
initialization, bytecode bounds and a 65,536-instruction limit. Unsupported
opcodes report an error with a diagnostic mask.

The current subset covers 25 opcodes: shader/conditional jumps and termination,
scalar/vector constants, geometry and selected world texture coordinates,
scalar/vector math, conversions, clamp, vector components, checker and gradient
textures, gamma/invert/brightness-contrast, closure weights, mixed closures,
and background/emission accumulation. Derivative variants, image textures and
the rest of SVM are unfinished. This interpreter currently sets up background
shader data only; it does not establish surface or volume shader support.

The Linux x64 gate renders 20 nonconstant graphs compiled by Cycles, each at
64 x 48 pixels and one sample, through the original camera → closest → background
scheduler. A further 161 typed bytecode programs cover all 41 scalar operations,
30 vector operations and eight conversion kinds, conditional jump outcomes,
zero divisors, signed/zero/unit directions and inverse-trigonometric endpoints.
The original CPU background/SVM code receives identical state and bytecode.
Across 102,817 paths, all 25,948,160 state checks and 2,224,128 film comparisons
pass the existing `5e-5 + 2e-4 * abs(reference)` tolerance. The initial successful
run recorded 7,483 stricter float differences and maximum absolute error
0.0000610352; this remains approximate numerical parity. Ten invalid programs
are rejected with the expected diagnostic bits, unchanged single-path film and
queue counters. That check does not guarantee rollback of a multi-path batch.
Cycles allocation accounting returns to zero after cleanup.

This gate exposed an arcsine difference beyond the accepted tolerance on the
Intel GPU. Original CodeBrix adapters now use a Taylor series on a reduced
interval and half-angle identities for arcsine/arccosine. The comparison
tolerance was retained, and endpoint/threshold cases were added. WGSL permits
implementation-dependent accuracy within its specified limits; see the
[WGSL floating-point accuracy rules](https://www.w3.org/TR/2026/CRD-WGSL-20260921/#floating-point-accuracy).
The change is entirely in our generated shader; the shipped WebGPU binary
remains unchanged. This integrated SVM gate has run on Linux x64/Intel Vulkan
only. Android, other GPU vendors, shader-stack cost and performance still need
validation. The previews are clipped linear PPM images, not color-managed output.

## Build and reproduce

Use the pinned source/dependency setup from the prototype instructions, then:

```sh
python3 native/tools/device_gate.py run
python3 native/tools/camera_queue_gate.py run
python3 native/tools/closest_queue_gate.py run
python3 native/tools/background_queue_gate.py run
python3 native/tools/svm_world_gate.py run
python3 native/tests/test_webgpu_audit.py
```

The build uses a separate CMake directory, `native/work/cycles-build`. A
generated overlay changes three upstream files: the device enumeration,
factory/discovery functions and report name. The reference checkout remains
clean. The exact patch, generated files, compiler commands, source snapshots,
reference helpers, package audit and results are saved in
`evidence/device-queue`; `device_gate.py archive` creates this checkpoint once
and refuses to overwrite it. No native dependency binaries are archived.
The camera runner also rebuilds and reruns the control gate. Its separate
`camera_queue_gate.py archive` checkpoint is `evidence/camera-queue`, with source
hashes, original reference headers, compiler commands, runtime identity and
both test reports. It also refuses to overwrite an existing checkpoint.
`closest_queue_gate.py run` includes both previous gates, then the closest-stage
checks. Its `archive` command creates `evidence/closest-queue` once, including
the generated shader/translation evidence, original reference headers and all
three reports. The reference checkout remains unmodified.
`background_queue_gate.py run` includes those three gates before checking
background shading and native film. Its `archive` command creates
`evidence/background-queue` once, including the root provenance/license records,
generated shaders, reference headers and four sets of results. It saves hashes
of the native dependencies, not their binaries.
`svm_world_gate.py run` includes all four preceding gates and the six runtime
audit tests, then checks the SVM extension. Its `archive` command creates
`evidence/svm-world` once, with the original SVM headers, generated shaders,
native bytecode/previews, license records and all five test reports. Earlier
checkpoints remain unchanged.

The package remains `CodeBrix.WebGpu.Native.MitLicenseForever` 1.0.290.1433.
During this gate the sibling repository received an external documentation
change in `include/codebrix_wgpu_procs.rs`: an updated Rust inclusion example.
All nine native binaries and every C header still match the NuGet archive and
cache. The new builder explicitly permits only whole-line comment differences
in that unused Rust helper; it records both hashes and the exception. Package
cache bytes must still match the archive exactly. Rust code, C-header and
native-binary differences still reject the build; six audit tests verify these
boundaries. Earlier builders retain their strict all-files identity check.

## Next integration

The wavefront queue now needs surface/light and shadow entry points operating
on Cycles' native state and scene tables, alongside broader SVM coverage.
Camera-to-intersection-to-background now reaches native film through the
original scheduler for constant and selected SVM worlds; remaining modes still need work.
The existing WGSL rendering functions provide tested behavior to adapt, but
the existing complete-path shader must not be disguised as those individual
kernels. Queue transitions, counters, shadow-path creation, film writes and
compaction need end-to-end comparison through the original scheduler.

Full session rendering, texture bindings, other GPU platforms, large scenes,
shared-device handoff and final NuGet/RID packaging remain unfinished. All
unimplemented kernel IDs currently produce a named error rather than a
fallback render. Full rendering parity remains the objective.
