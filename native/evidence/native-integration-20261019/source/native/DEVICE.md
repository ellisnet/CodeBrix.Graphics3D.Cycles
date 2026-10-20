# Registered Cycles device and wavefront queue

The new `native/cycles` implementation registers `DEVICE_WEBGPU` with the
original Cycles device factory and implements `DeviceQueue`. The Linux gate
passes against the unchanged CodeBrix.WebGpu.Native package. This connects
scene storage, scheduler control, limited camera initialization, closest
intersection, selected SVM surface bounces and background/film output to WebGPU.
A complete bounded scene-to-image loop now runs through the original native GPU
scheduler. The maintained [experimental C host and .NET wrapper](host/README.md)
now cover a bounded `Session` lifecycle; general feature support and complete
platform/application validation remain pending. The earlier
[snapshot renderer](README.md) remains
the separate, working image reference on Linux and Android.

The latest support and validation status is in
[Integrated handoffs and current limits](#integrated-handoffs-and-current-limits-2026-10-19).
Earlier milestone sections preserve their original test scope and limitations.

## Current session milestone

Prove that Cycles’ native scene preparation and render scheduler can drive a
complete, repeatable GPU render through the shipped CodeBrix.WebGpu.Native
runtime. Full Cycles functionality remains the longer-term objective, with
all previously requested RIDs retained, including Windows ARM64 and Linux
RISC-V64. This session does not include Metal development.

Acceptance criteria:

- Native scene preparation and native shader compilation feed a complete WGSL
  render loop with multiple bounces and selected rough diffuse, translucent,
  transparent, emissive and mixed materials.
- Images and intermediate path states are compared against the original CPU
  kernels, with recorded tolerances and failures.
- At least one ForestCommunity model renders using the supported material subset.
- The relevant GPU workload runs on Linux and the three Android test devices,
  including API 33, through unchanged shipped WebGPU binaries. Native scene
  replay establishes GPU portability; native Cycles Android host builds are
  a separate milestone.
- Unsupported features fail explicitly; tests, runtime hashes and provenance
  make the result reproducible.

Status: the bounded rendering proof passes on Linux, with complete command replay
on all three Android devices. The [native-render checkpoint](evidence/native-render/validation.json)
records the acceptance results. This does not establish full Cycles parity.
The current host uses filtered source and independent Apache-2.0 atomics.
See the root release-license plan for dependency and package audit status.

## Complete native scheduler render

`render_gate.cpp` prepares a real `Scene` on `WebGPUDevice`, compiles its shader
graphs through Cycles, and drives the original scheduler's camera, closest,
surface and background entries until all paths terminate. The selected surface
kernel includes closure sampling/mixtures, emission, holdout, roulette throughput,
ray offsets and native queue/film updates. It uses buffer-backed closure scratch.
It does not use the earlier snapshot renderer as a scheduler substitute.

Two 64 x 48, 8-sample fixtures pass independent original-CPU image comparisons:

| Fixture | Kernel stages | Path invocations | Surface shaders | Max bounce / transparent counter | Max CPU image error |
| --- | ---: | ---: | ---: | ---: | ---: |
| Mixed-material enclosure | 157 | 136,344 | 21 | 5 / 3 | 5.43594e-5 |
| ForestCommunity bench, rough clay | 120 | 165,304 | 1 | 5 / 0 | 1.19209e-6 |

Each fixture renders twice on the Linux native host with bitwise-identical GPU
film. The first run also compares intermediate state and film to the original
CPU kernels from identical incoming state. A separate CPU path loop supplies the
independent final image. Integer state and counters are checked exactly; floats
use `5e-5 + 2e-4 * abs(reference)`. Stricter `1e-7 + 1e-7 * abs(reference)`
diagnostics still report differences, so this is not strict numerical parity.

The bench has one explicitly recorded shared-edge exception: CPU primitive 178
and GPU primitive 179 at stage 47/path 752. Both are flat, constant-clay triangles
of the same object/material, share an edge, and have matching hit position and
geometric normal. Only their primitive ID and two barycentrics use this semantic
check; independent film comparison remains unchanged. `run.shared-edges.json`
retains both intersections. This exception does not apply to smooth/textured
geometry or material/attribute seams. The triangle dot adapter preserves the
original SSE CPU reference's y/z-first addition grouping.

That frozen isolated surface integration gate additionally passes 19 cases / 18,360
paths, 1,478,656 integer and 2,510,736 float checks, plus 20 explicit rejection
cases. Maximum absolute float error is 7.53403e-5 within the relative/absolute
tolerance; stricter differences, including film accumulation order, remain.

`RenderCapture` records the second native render's actual uploads, clears,
dispatch parameters and scheduling readbacks. The small native replay driver
checks every scheduling readback before proceeding and never injects expected
results. The mixed scene has 901 commands / 346 exact readbacks; the bench has
688 commands / 272 exact readbacks. Both pass on Linux and all Android devices:

| GPU target | API | Mixed-scene maximum CPU film error | Bench maximum CPU film error |
| --- | --- | ---: | ---: |
| Linux x64 Intel UHD | Vulkan | 5.43594e-5 | 1.19209e-6 |
| Pixel 4 XL ARM64 Adreno 640 | Android 33 | 3.05176e-5 | 2.14577e-6 |
| Samsung SM-X700 ARM64 Adreno 730 | Android 36 | 3.05176e-5 | 2.14577e-6 |
| HP Android x64 Intel Xe | Android 33 | 5.43594e-5 | 9.53674e-7 |

All replay films compare against the independent CPU image at the same tolerance.
Every terminal path slot is zero, device error words are zero and native scene
tables remain unchanged. Package runtime hashes and loaded paths are checked.
Android executes complete GPU command sequences captured from the Linux native
host; a complete native Cycles host on Android remains a separate gate.

The preview at `work/render/bench-preview/ForestCommunity-bench-clay-1024x768-32spp.png`
is 1024 x 768 at 32 samples. It uses a blue constant world (RGB 0.25/0.4/0.6,
strength 0.8), no lamps, and rough diffuse clay in an open-front enclosure.
Original textures/materials are not represented. Raw linear film is retained;
the display preview uses Reinhard mapping and gamma 2.2, not a finished OCIO
display pipeline. Preview mode is not the independent-CPU proof mode.

Reproduce the full gate with `python3 native/tools/render_gate.py run` after
preparing the bench geometry as documented in `parity/ASSETS.md`. It rebuilds,
runs earlier regressions, native proof fixtures and four-device replays. `collect`
validates existing working outputs and tests that a changed expected scheduling
readback is rejected. `archive` creates `evidence/native-render` once and refuses
to overwrite it. No executable or native dependency library is archived.

General lighting/MIS, tiled/sparse image paths, most closures, volumes, motion,
curves/points, guiding, MNEE and general film/session modes still require work.
The broad roadmap is `/home/jeremy/ClaudeHome/PLAN_Cycles_Targetting_WGSL_WebGPU.md`;
all nine requested RIDs and full functionality remain its target.

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
Sun forward/MIS, background MIS, volumes, shadow catchers, denoising, path guiding,
light/shadow linking and the background motion pass are explicitly unsupported
in this entry. Later surface and opaque-shadow progress is recorded below; full
direct lighting and transparent shadows remain open. The earlier standalone snapshot renderer's material
support is not yet connected to this registered shading entry.

The root [license inventory](../THIRD-PARTY-NOTICES.txt) records the filtered
source, independent atomic replacement and current dependency audit scope.
These development results do not certify future native packages.

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

## Native surface shader-data preparation

`wavefront_shader_data.wgsl` implements the static-triangle portion of Cycles'
`shader_setup_from_ray`. It reads the original intersection state, vertex
indices/positions, object transforms, flags, shader IDs and packed normal table.
It reconstructs the hit position, geometric and smooth normals, incoming
direction, backface flags, parameter tangents, compact ray differentials and
UV differentials. Vertex and corner normals use the pinned revision's
octahedral 32-bit encoding. Signed position/normal offsets are preserved.
Motion/other primitive types and invalid references are rejected explicitly.

The Linux gate compares every defined output field with the original CPU
function, including actual camera → closest-intersection hits and controlled
rays across nine objects. It covers flat/smooth/corner normals, shared instances,
nonuniform and mirrored transforms, transforms baked into vertices, all three
differential projection axes, backfaces, triangle vertices/edges, dense and
sparse inputs, zero work and signed offsets. Across 11 cases and 12,066 hits,
84,462 integer checks, 337,848 float comparisons and 1,576,960 unchanged path-state
checks pass. All 5,324 output guard bytes remain unchanged. Twelve malformed or
unsupported inputs are rejected before writing a result. Cycles allocation
accounting returns to zero. There are 451 differences at the stricter float
tolerance; maximum absolute error is 0.000000476837.

A portable replay harness executes the same compiled WGSL and a captured
1,314-hit native buffer set using the unchanged package runtime on four devices:

| Device | System | Result at existing tolerance | Stricter float differences |
| --- | --- | --- | --- |
| Linux Intel UHD | Linux x64 / Vulkan | Pass | 49 |
| Pixel 4 XL, Adreno 640 | Android ARM64 / API 33 | Pass | 3 |
| Samsung SM-X700, Adreno 730 | Android ARM64 / API 36 | Pass | 3 |
| HP laptop, Intel Xe | Android x64 / API 33 | Pass | 49 |

Each replay checks 36,792 float values, 9,198 integers and all 4,661,736 bytes
outside the output range. The maximum float error on each device is
0.000000476837. The driver compiles for Android API 33, and deployed/loaded
WebGPU binary hashes match the package. This is GPU kernel portability
evidence: native scene preparation still runs on Linux. The full Cycles
Android host, complete surface SVM coverage and surface-bounce scheduler
entry remain unfinished. The probe calls reusable shader-data functions;
it does not register a substitute for `integrator_shade_surface`.

## Native surface SVM evaluation

`wavefront_svm.wgsl` now evaluates selected surface programs using the same
native bytecode as the world interpreter. The GPU first constructs shader data
from original scene and intersection tables. It then sets up Lambert, Oren-Nayar,
translucent and transparent closures, holdout, emission, mixed/additive graphs,
selected surface texture coordinates and explicit normal updates. CPU-prepared
closure inputs are not substituted for this evaluation.

The Linux gate executes the original CPU `shader_setup_from_ray` and
`surface_shader_eval<KERNEL_FEATURE_NODE_MASK_SURFACE>` as its oracle. Twenty
Cycles-compiled graphs and fourteen typed test programs cover 8,874 evaluations
in 23 cases. All 603,432 integer checks, 7,463,034 float comparisons, 3,297,280
unchanged state checks and 2,944 guard bytes pass at the existing
`5e-5 + 2e-4 * abs(reference)` tolerance. There are 1,513 differences at the
stricter `1e-7 + 1e-7 * abs(reference)` tolerance; the maximum absolute error is
0.00000762939. These are tolerance comparisons, not bitwise float parity.

The cases include closure capacities 0, 1, 2 and 64; more than 64 requested
lobes; holdout flags after exhaustion; repeated transparent lobes with signed
weights; negative/tiny BSDF weights; termination, emission and shadow modes;
zero/explicit normals; dense/sparse indices and zero work. Roughness tests
straddle the Lambert/Oren-Nayar threshold and include values outside [0, 1].
RNG initialization reads the original packed 16-bit RNG offset. Undefined
holdout normal/sample-weight fields are omitted from the semantic comparison
and represented as zeros in the test record. All eleven unsupported/malformed
program or closure-storage checks report the expected error and leave output untouched. Cycles'
allocation accounting returns to zero.

The threshold tests exposed cancellation amplification in Oren-Nayar's
energy-compensation term. An original CodeBrix binary32 addition adapter
preserves separate rounding for its product sums using integer significands,
guard bits and ties-to-even rounding. Constants are explicitly binary32.
No CPU reference or comparison tolerance was changed, and no additional math
library or runtime modification was introduced.

The portable capture contains 1,020 evaluations of the twenty compiled graphs,
including near-threshold roughness. It passes on Linux x64, Pixel 4 XL ARM64
API 33, Samsung SM-X700 ARM64 API 36 and HP Android x64 API 33 using the unchanged
shipped runtime. Each replay compares 857,820 floats and 69,360 integers, verifies
all 3,655,680 closure scratch bytes against the output records, and checks that
12,018,384 bytes outside those two ranges remain unchanged. Maximum absolute
error is 0.00000762939 on every device. Strict float differences are 219 on Linux
and HP, 598 on Pixel and 627 on Samsung.

The initial function-local 64-closure array caused pipeline creation failures
on both ARM64 devices; Pixel's compiler reported register spilling. Closures
now occupy bounded per-path storage in the same GPU arena, with explicit bounds
and alignment checks, while retaining the 64-closure capacity. This change fixes
both drivers without changing WebGPU, weakening comparisons or reducing the
supported closure count. It also removes the large private result copy.
CPU tests verify the 64-lobe exhaustion case; this replay has mixed materials
with fewer live lobes, and is not a mobile 64-lobe stress benchmark.
The capture validates GPU evaluation from a Linux-prepared scene; it does not
establish Android native host support. This probe is not a registered surface
queue entry, BSDF sampling, a surface bounce, or a complete render. Image textures,
derivatives/bump, generated-attribute tangents, volume coordinates and other
closure families still require implementation; unsupported programs fail
explicitly. Production performance and shader compilation cost remain open.

## Build and reproduce

Use the pinned source/dependency setup from the prototype instructions, then:

```sh
python3 native/tools/device_gate.py run
python3 native/tools/camera_queue_gate.py run
python3 native/tools/closest_queue_gate.py run
python3 native/tools/background_queue_gate.py run
python3 native/tools/svm_world_gate.py run
python3 native/tools/shader_data_gate.py run --android
python3 native/tools/svm_surface_gate.py run --android
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
`shader_data_gate.py run` includes those five gates, checks surface shader data
and runs a Linux buffer replay. Add `--android` to replay on the three connected
devices listed above. Its `archive` command creates `evidence/shader-data` once.
That checkpoint includes the captured arena, parameters and original CPU
results, plus each device's result records, source/runtime hashes and all six
native gate reports. The large input arena and replay records are compressed;
the manifest explains how to reconstruct and hash each complete output arena.
No executable or native dependency library is copied into the checkpoint.

The package remains `CodeBrix.WebGpu.Native.MitLicenseForever` 1.0.290.1433.
During this gate the sibling repository received an external documentation
change in `include/codebrix_wgpu_procs.rs`: an updated Rust inclusion example.
All nine native binaries and every C header still match the NuGet archive and
cache. The new builder explicitly permits only whole-line comment differences
in that unused Rust helper; it records both hashes and the exception. Package
cache bytes must still match the archive exactly. Rust code, C-header and
native-binary differences still reject the build; six audit tests verify these
boundaries. Earlier builders retain their strict all-files identity check.

## Native static-triangle attributes

`attribute_gate.py` extends native scene-table transport and the surface SVM
interpreter with named/standard attribute lookup, 64-bit identity, object-map
chains, signed offsets and checked table access. Object/mesh/face/vertex/corner
values include float/float2/float3/float4, packed normals and byte colors. Native
Rec709 conversion, UV seams, generated-coordinate fallback, interpolation,
attribute bump offsets and stored derivatives are compared with unmodified CPU
`shader_setup_from_ray` and `surface_shader_eval`.

The Linux gate has 48 Cycles-compiled graphs and 328 typed programs, 329 cases /
5,088 evaluations, seven rejected malformed/unsupported inputs, unchanged state
and output guards, and zero Cycles allocations after cleanup. Maximum absolute
error is 1.78814e-7; 17 component differences exceed the strict diagnostic bound.
The captured 48-graph workload has 1,152 evaluations; the derivative/rejection
programs currently execute only on Linux.

A complete native 64x48/8-spp UV-checker scene exercises 156 stages / 135,912
paths, five bounces and three transparent bounces. Its independent CPU film
passes the existing `5e-5 + 2e-4 * abs(reference)` criterion and the GPU repeat
is bitwise identical, with no shared-edge equivalence. Maximum absolute film
error is 0.000137329; 840 components exceed the strict diagnostic bound.
This is procedural shading through native UV attributes, not image sampling or
original glTF material fidelity. Existing synthetic/clay regressions also pass.

```bash
python3 native/tools/attribute_gate.py run
python3 native/tools/attribute_gate.py collect
python3 native/tools/attribute_gate.py archive
```

`run` includes the existing regression chain and replays both the native graph
capture and complete UV-render commands on Linux, Pixel, Samsung and HP Android.
Each replay checks runtime identity, original CPU output and immutable scene
bytes; the complete render also verifies every scheduler readback. `collect`
checks existing results without rerunning passing workloads. `archive` creates
`native/evidence/native-attributes` once and refuses to overwrite it. The first
capture invocations were recorded after direct execution while the collection
driver was being implemented; their invocation metadata states this explicitly.

Other geometry domains, matrix/volume consumers, production image coverage, general bump
state and full native Android hosts remain unfinished. Strict discrepancies are
retained, and no all-feature or all-RID completion claim is made.

## Resident native images

The resident image backend now stores native `device_image` pixels in the same
bounded GPU arena and publishes Cycles `KernelImageInfo` plus checked allocation
bounds. Logical texture/UDIM descriptors remain native Cycles tables. ImageManager
continues to prepare pixels; the WebGPU backend does not replace host decoding
or conformance. Queue initialization and image updates publish current descriptors,
and image frees remove their ownership records. Large scenes remain bounded by
the single storage-binding arena; sparse and tiled cache paths remain unfinished.

`image_gate.py` validates eight resident formats (float/half/byte/ushort, scalar
and RGBA), nearest/linear/cubic/Smart filters and repeat/extend/clip/mirror modes.
The native ImageManager fixture has 128 images/compiled graphs. Original CPU
sampling covers 115,650 cases, including boundaries, negative coordinates,
UDIM hits/misses, HDR, alpha unassociation and compressed-sRGB decoding. Fourteen
malformed-input cases check descriptor/range/format/coordinate errors, explicit
tiled-cache rejection and untouched output. Another 176 typed programs exercise
flat/sphere/tube/box image projections, both environment projections, color/alpha
outputs, and derivative opcodes. Resident images follow native non-mip sampling;
this is not proof of tiled footprints, stochastic mip selection or cache retries.

The initial Linux sampling comparison passes the unchanged absolute/relative
criterion. Its largest absolute difference is 0.328125 at CPU value 225665.84375
(after HDR/near-transparent unassociation and sRGB conversion); the largest
ratio to the allowed error across the corpus is 0.00727692016. Strict failures
remain recorded. The graph/typed corpus has 1,824 evaluations and retains its
strict discrepancies. The complete `--images` native render compares independent
CPU film and exact GPU repetition. `image_gate.py run` includes the complete
attribute/prior-render regression chain and four-device sampler, graph and full
render replay; `collect` rechecks saved evidence; `archive` refuses to overwrite
`native/evidence/native-images`.

All four devices pass the 115,650-case sampler and 768-evaluation native graph
captures. The typed projection/derivative and malformed-input programs currently
execute on Linux only. The complete native image render has 155 stages and
136,029 path invocations, with bitwise-identical Linux GPU repetition and no
shared-edge equivalence. Its recorded loop passes on Linux x64, Pixel API 33,
Samsung API 36 and HP Android x64 API 33: 890 commands, 377 dispatches and 342
exact scheduling readbacks per replay. The maximum independent CPU-film error is
3.67164612e-5 on Linux/HP and 3.05175781e-5 on Pixel/Samsung. Strict image failures
remain (2,202 / 2,205 / 1,844 / 1,850, respectively). All scene bytes remain
unchanged and every path terminates. Earlier attribute, selected-material and
clay-bench regressions also pass. The [image checkpoint](evidence/native-images/validation.json)
contains these results and fresh earlier regressions; historical checkpoints are
preserved. Android results establish GPU command replay, with native Android
Cycles hosts still pending.

```bash
python3 native/tools/image_gate.py run
python3 native/tools/image_gate.py collect
python3 native/tools/image_gate.py archive
```

These initial fixtures use generated input pixels through native ImageManager.
The next section extends validation to encoded file inputs and replacements.
Production OCIO configurations, additional file/animation/UDIM coverage, broader
edit and resize stress, sparse 3D images, tiled cache residency, original
ForestCommunity materials, general closures and native Android hosts remain open.

## File-image and replacement validation

`native/tools/image_file_gate.py` exercises the existing native ImageManager and
reviewed OIIO/OCIO build with eighteen encoded PNG, JPEG and OpenEXR fixtures.
One through four channels, byte/ushort/half/float precision, five alpha modes and
five color selections produce 450 parameter variants. Its original OCIO matrix
configuration is validated, serialized and loaded from disk. Known source pixels
also verify lossless decoding, channel expansion, image orientation and the
matrix conversion independently of the CPU sampler; JPEG remains a lossy-codec
transport comparison, with no exact pre-encoding pixel claim.

Sixteen native replacement cycles change dimensions and precision while an old
image read is queued. Each verifies the old result, the newly loaded pixels,
native handle deduplication, logical descriptor reuse, animation-frame update
signals and unchanged unrelated image storage. This tests replacement by native
handle release/re-add, not a completed host application edit API. Both `<UDIM>`
and `<UVTILE>` filename patterns build native three-tile mappings. Missing and
corrupt inputs retain the original missing-image behavior. Cleanup leaves zero
Cycles allocation bytes and no image owners or image-info/bounds tables.

The Linux gate checks 154,931 samples, including 34,483 known lossless texels
(maximum error 5.96046448e-8). The final 116,352-case snapshot is the four-device
GPU replay corpus, now passed on Linux, Pixel API 33, Samsung API 36 and HP Android
x64 API 33. Actual file decoding, OCIO setup, replacement/lifetime and
known-texel assertions currently execute on Linux only. Recorded diagnostics
separate very large HDR values after near-zero-alpha unassociation/sRGB conversion
from ordinary-color errors; the same absolute/relative tolerances apply.

The TIFF plugin remains deliberately excluded by the reviewed dependency profile;
this gate records that gap rather than enabling excluded implementation code.
A compatible TIFF/LZW path and other unavailable format paths remain required.
No dependency revision or imported implementation was added for this work.
Production OCIO configurations, cache/mip residency, broader animation/lifetime
stress, original material renders and all native platform hosts remain open.

```bash
python3 native/tools/image_file_gate.py run
python3 native/tools/image_file_gate.py collect
python3 native/tools/image_file_gate.py archive
```

`archive` creates `native/evidence/native-image-files` once, preserving file
fixtures, the OCIO configuration, captures, replay arenas and test sources. Prior
native-image rendering evidence remains separately frozen; the collector checks
identity of its native implementation and four earlier native executables for
that historical checkpoint. After renderer edits, the normal gate supplies a
fresh image/attribute/render regression chain with matching source hashes.
For separate collection after that fresh regression, use
`python3 native/tools/image_file_gate.py collect --regression native/work/images/validation.json`.
Without that option, the standalone file gate retains its original strict
renderer-identity requirement and rejects changed renderer binaries.
The [file-image checkpoint](evidence/native-image-files/validation.json) records
the results and explicit proof limits.

## Native normal maps and tangents

`python3 native/tools/normal_gate.py run` checks native static-triangle
normal-map spaces (tangent, object, world and Blender object/world), both green
channel conventions, original/displaced base normals, strengths below zero and
above one, missing attributes, zero colors and out-of-range finite colors.
Native Mesh tangent generation supplies standard/named UV frames. Shared mesh
instances, nonuniform/mirrored transforms, front/backfaces, flat/smooth/corner
normals and separate undisplaced normals feed the original CPU SVM comparison.
Radial/UV tangents include analytic dx/dy and FLOAT2/FLOAT3 attribute consumers.

The Linux corpus has 48 compiled graphs, 398 typed programs and 10,704 surface
SVM evaluations. Ten malformed/degenerate cases must set an error without writing
an output record. A degenerate plain tangent produces nonfinite values in the
original implementation; this backend explicitly rejects it. The derivative
zero-frame semantics are separate. Full bump state, motion and other geometry
remain open. No original asset material-fidelity claim is made.

The gate also runs a complete 64x48, 8-sample native image-normal-mapped render,
checks independent CPU film, repeats the GPU film bitwise, and replays the graphs,
selected nonzero tangent derivatives and full recorded render on four GPUs.
It reruns previous image/attribute/render gates and file-image checks following
renderer edits. `collect` rechecks existing results; `archive` freezes
`native/evidence/native-normals` once. Android replay validates the recorded GPU
work; native Android Cycles hosts remain pending. Strict diagnostics are retained
alongside the unchanged 5e-5 + 2e-4*abs(reference) acceptance criterion.

## GGX reflection work in progress

The current GGX reflection changes are not a frozen rendering checkpoint. The
native setup corpus passes 167 cases and 16 malformed-input rejections. The
earlier full native render stopped on a sharp-GGX PDF comparison. At the retained
stage-6 input, a one-step difference between host `cosf` and correctly rounded
binary32 cosine changes the sampled half-vector enough to exceed the unchanged
PDF tolerance. The original CPU reference and acceptance limits remain intact.
The corrected bounded adapter now uses the MIT option of Arm Optimized Routines
v25.01, pinned at 3752b981a6f3b89b5468476a87568a29fd9e0822. The four original
source/data files and selected MIT grant are retained under
`third_party/arm-optimized-routines`, with hashes/license basis in
`third_party/arm-math-import.json`. Original CodeBrix two-component arithmetic
evaluates the polynomial in WGSL without requiring binary64. This adds no linked
math library. `tools/arm_math_source.py` verifies the subset before native builds
and source auditing; root and native notices retain Arm attribution.

Its development backend passes the unchanged full native gate (149 stages /
140,006 paths, independent CPU film and bitwise repeat) and all 34,838 scalar
checks on four GPUs. The integrated build still requires fresh native/device and
broader regression validation. `native/tools/native_trig_gate.py` supplies a
larger corpus with independent platform `sinf`/`cosf` calls. The original Taylor
adapter and its rounded-binary64 gate remain separate.

The composed surface shader also exposed execution-layout sensitivity on the
tested Linux Intel GPU: size 64 reproduces incorrect packed state and ray
directions. Its size-1 variant passes Linux and Samsung, but HP aborts in the
vendor compiler and Pixel exceeds the 600-second test timeout. The current path
therefore separates preparation/closure evaluation (size 64) from sampling and
state updates (size 1), with a 208-byte GPU-private record per invocation. The
ready word is cleared before validation and set only after the complete record
is written. The native scheduler retains one surface operation and its existing
queue transitions; no CPU readback connects these two GPU dispatches.

The captured split, including the actual native-generated module, passes all
77,824 integer checks, 110,592 float comparisons, film, queue and guard checks on
Linux, Pixel, Samsung and HP with unchanged package binaries. Strict differences
remain reported. The current native GGX bounce gate passes 22 valid cases /
21,420 paths and 21 invalid-input checks. The generic bounce gate passes 18 valid
cases / 17,340 paths and the same 21 rejections. These results predate the newly
integrated native trig adapter. Broader regression
and performance assessment remain open. A retained branchless arithmetic variant
fails Linux, so it is not the production default. Working diagnostics are under
`native/work/microfacet`; older immutable evidence describes its own source pins.

Global closure capacity now requires 1..MAX_CLOSURE, matching native Scene's
minimum of one. The previously injected global-zero positive case can leave the
CPU with a BSDF flag but no allocated closure, making sampling undefined. All
1,020 original paths are retained as a rejection case, with the same flags,
weights, film allocation and queue counts; every path field, film value and
counter must remain unchanged. Minimum-capacity-one and local zero-storage SVM
evaluation remain covered. This reclassification leaves historical evidence
intact and does not change numerical acceptance.

A complete image diagnostic records exactly one practical state-float mismatch
(the PDF above), zero integer-state mismatches and zero CPU/GPU queue-count
differences over 149 stages / 140,006 path invocations. Its independent CPU film
passes the existing tolerance, with maximum error 1.90735e-6 and 173 strict
differences; repeated GPU film is bitwise identical. It explicitly reports
`diagnosticOnly=true` and `stageGatePassed=false`. Four-device replay of this fixed
capture checks scheduling and film; it cannot establish the failed stage gate.
Single-kernel replay now allows 600 seconds by default, matching full-render
replay, after Pixel exceeded the previous 180-second compilation/execution limit.

Dispatch sizes are explicit in native recording and replay. Command-stream v2
adds kind 5: the existing three-word header contains kind, entry and invocation
count, followed by a workgroup-size word and the 128-byte parameter block. Kind 3
retains its original implicit size 64, so v1 captures remain replayable. Entry 5
selects `prepare_surface` from `surface.wgsl`; entry 4 selects `shade_surface`.
Tests
cover an actual size-1 surface dispatch, old and converted full-render captures,
invalid sizes/grid counts, truncated records and unknown command kinds. Expected
scheduling readbacks remain assertions and are never injected into GPU state.

## Next integration

The earlier bounded surface-bounce and complete scheduler-loop checkpoints pass
for their recorded sources and fixtures; GGX reflection has since passed bounded
native-render regressions. Point/sun NEE and shadow-path creation now have bounded
native-render coverage below. Next are transparent shadows, remaining emitters,
forward/MIS and broader rendering features.
Use the existing Linux and Android hosts for relevant feature regressions.
Jeremy's 2026-10-19 priority defers additional platform ports and release packaging
until rendering functionality is complete, unless a concrete feature needs them.
The existing WGSL rendering functions provide tested behavior to adapt, but
the existing complete-path shader must not be disguised as those individual
kernels. Queue transitions, counters, shadow-path creation, film writes and
compaction need end-to-end comparison through the original scheduler.

Full session rendering, texture bindings, other GPU platforms, large scenes,
shared-device handoff and final NuGet/RID packaging remain unfinished. All
unimplemented kernel IDs currently produce a named error rather than a
fallback render. Full rendering parity remains the objective.

`svm_surface_gate.py run --android` includes the prior six native gates and the
surface evaluation comparisons, then replays the compiled material capture on
Linux and the three connected Android devices. Its `archive` command creates
`evidence/svm-surface` once, preserving capture/replay data, input hashes,
reference sources and prior-gate reports. It never overwrites checkpoints or
archives a complete upstream checkout or native dependency binaries. All such
evidence remains subject to the release review in `../RELEASE-LICENSE-PLAN.md`.

## Registered opaque shadow stages (2026-10-19)

The registered queue now implements `INTEGRATOR_INTERSECT_SHADOW` and
`INTEGRATOR_SHADE_SHADOW` for opaque static triangles and native instances.
Originating and light primitives are excluded by their native object/primitive
identities. The stages preserve native shadow-state and queue-counter semantics
and write combined, adaptive auxiliary, light-group and surface direct/indirect
film passes. Transparent shadows, volumes, AO, shadow linking and shadow catchers
remain explicit errors. This initial shadow-stage checkpoint preceded the
point/sun light-sampling and shadow-state integration recorded below; stage tests
alone do not prove a complete direct-light render.

A boundary test exposed ordinary WGSL division rounding an exact depth of 4 to
3.999999761581421. The existing binary32 rounding helper now computes BVH inverse
directions and generated triangle depth division. This preserves ray bounds;
the original failed decision and independent scalar probes are retained.

Run the shadow stage and affected traversal/GGX regressions with a new output
folder:

```bash
python3 native/tools/shadow_gate.py --output native/work/shadow-check-local
```

The initial successful run is `native/work/shadow-stages/linux-v4/validation`.
It passes 60 cases/3,846 paths against unmodified original CPU shadow kernels,
with 1,204,320 exact state checks, 1,111,680 film-component checks and 18 invalid
case rejections. Shadow film comparisons have zero strict differences, maximum
absolute error 5.960464477539063e-8. Existing closest-intersection and GGX full
render regressions also pass; closest-stage strict floating diagnostics remain
reported separately.

`native/work/shadow-stages/android-v1/validation` records the same native test
on Pixel ARM64, Samsung ARM64 and HP Android x64: all pass with the same coverage
and film-error results through unchanged shipped WebGPU binaries. These isolated
feature builds reuse the existing reviewed Android dependency prefixes. Payload,
compiler/header/archive and ELF reviews pass; no new platform port or release
package is implied. Full direct-light and transparent-shadow integration remains
required.

The recorder/replayer now includes shadow intersection and shading as pipeline
IDs 6 and 7. Captures using these entries have format `native-command-stream-v3`
and include `shadow.wgsl`; existing v1/v2 captures retain their original pipeline
IDs and shader requirements. Capture metadata records film stride, workload and
shadow queue bounds. Validation checks all shadow paths terminate and compares
every film pass against the original CPU reference. A shadow-stage replay is
reported separately from a full render. `shadow_gate.py` now records and replays
one direct and one indexed shadow batch on Linux as part of its acceptance run;
both pass in `native/work/shadow-capture/linux-v1`, alongside the original shadow,
closest-intersection and GGX render gates. Each replay verifies four scheduling
readbacks, all 4,096 shadow slots terminated and 9,264 film components, with zero
strict film differences.

`native/work/shadow-capture/compatibility-v2` passes nine additional replays:
historical v1/v2 and the current render capture on Linux, plus both shadow batches
on Pixel, Samsung and HP Android. The shadow batches have zero strict film
differences on all devices, maximum absolute error 5.960464477539063e-8.
Historical full-render strict diagnostics remain reported (1,230 v1 and 173 v2);
their practical image criteria pass unchanged. Altering an expected scheduling
readback is rejected, as is a nonterminal shadow slot. Source, binary, runtime and
capture input hashes remained unchanged throughout validation. These Android
runs are command replay; the separate native-host shadow proof above remains the
native execution evidence. An initial work-runner import-path error is retained
with its original source/log; the corrected compatibility run passes.

## Native point/sun next-event estimation (2026-10-19)

The native surface stage now samples the original light CDF and point/sun records,
evaluates supported diffuse/translucent/GGX mixtures, applies light termination,
and allocates shadow paths through the original native scheduler. Point lights
support zero radius, disk and sphere geometry; sun lights support finite cones.
Constant emission, cast-shadow switches, bounce limits and existing shadow film
passes use native semantics. CPU table copies only validate capabilities; GPU
kernels perform the rendering work.

Full forward/MIS, light trees, triangle/area/spot emitters, nonconstant light
shaders, transparent shadows and smooth shadow-terminator geometry offsets remain
explicitly unsupported. These remain required work. The direct-light contribution
is generated by native scene preparation and WGSL kernels without changing the
shipped WebGPU library.

```bash
python3 native/tools/lighting_gate.py --output native/work/lighting-check-local
```

`native/work/direct-light/linux-v4` passes eight 32x24, 8-sample scenes against
unmodified original CPU integrator/shadow kernels. The test exercises 29,527 CPU
reference shadow paths (2,707 blocked, 26,820 visible) and 49,152 film comparisons,
with maximum absolute error 8.344650268554688e-6. All practical comparisons pass;
5,616 strict differences remain reported. Seven scenes repeat bitwise; the
light-termination scene has six differing repeated GPU components, maximum
1.1920928955078125e-7. This is not a determinism fix or a strict-parity claim.

All eight complete native command streams replay successfully on Linux, including
shadow allocator readbacks and terminal main/shadow queues. Existing shadow,
closest, surface, background and GGX render regressions pass; their strict
numerical diagnostics remain intact. Earlier failed fixture/reference-wrapper
runs and the test scheduler overload mistake are retained in linux-v1/v2/v3.
The corrected test uses the full native scheduling method, including shadow
compaction and allocation bookkeeping.

Actual native tests on Samsung ARM64 API36 and HP Android x64 API33 pass all eight
scenes and 49,152 film comparisons per device, with byte-identical generated WGSL.
Samsung reports maximum image error 6.020069122314453e-6 and 2,342 strict
differences; HP reports 8.344650268554688e-6 and 5,584 strict differences. Both
exercise the same 29,527 reference shadow paths as Linux and leak no native
allocations. The light-termination scene repeats with five differing components
on Samsung (maximum 2.384185791015625e-7) and eight on HP (1.1920928955078125e-7).
Other scenes repeat bitwise.

Pixel ARM64 API33 timed out after 900 seconds in its first GPU scene, after the
CPU reference film completed. The Android coordinator is retained as failed in
`native/work/direct-light/android-v1`; this feature is not yet validated on Pixel.
Separate post-timeout review confirms unchanged source/binary/runtime pins.
The process is absent and its CPU film is retained. Pipeline timing diagnosis is
underway; no numerical acceptance criteria were loosened. The first diagnostic
attempt rejected a stale existing replay binary before device execution; the
next diagnostic builds an isolated current harness. These are functionality
tests on existing RIDs, with no new platform or release-package claim.

## Native perspective camera motion

The camera entry now reads Cycles' native decomposed camera-motion array and
shutter inverse-CDF table. WGSL performs translation/rotation/scale interpolation,
perspective FOV motion and top-to-bottom rolling shutter, including durations
zero and one. Native scene preparation still computes the decomposed transforms
and shutter table. Existing object/deformation-motion and curve guards remain;
this is camera motion against static triangle geometry, not complete motion blur.

`python3 native/tools/camera_motion_gate.py --output NEW_DIRECTORY` builds and
runs the original CPU camera/path-state oracle, malformed-configuration checks,
a stationary image control and eight independent CPU/GPU moving-camera images.
The images exercise multiple bounces and transparent/diffuse/GGX materials. The
runner requires both CPU and GPU moving images to differ measurably from the
stationary control, records unchanged source/binary/runtime hashes, and retains
strict diagnostics alongside the established practical tolerances. Camera state
cases include two/three/five transform steps, small/large rotations, scale/shear,
nonuniform shutter curves, FOV changes, rolling shutter and indexed retries.

The first rotating-image attempt was correctly rejected by the independent film
comparison even though camera-state and per-stage tolerances passed. The camera
port now uses paired binary32 quaternion dot/normalization and an original
bounded Taylor acos adapter with the existing precise sin/cos helper. The failed
attempt remains in `native/work/camera-motion-v1`; subsequent raw films are saved
before image comparisons so a failed comparison cannot discard its evidence.

Linux development evidence is recorded under `native/work/camera-motion-v2`.
No Android camera-motion execution or complete platform coverage is claimed by
this lane. Depth of field, other camera models, object/deformation motion, hair,
subdivision and motion-vector passes require their own implementations and tests.

## Parallel functionality integration (2026-10-19)

The main source now includes three separately validated Linux slices: registered
point/sun forward lighting and MIS; single-scatter GGX glass/refraction; and native
perspective camera motion. Their isolated gate results and sealed source deltas
are recorded in the sibling `.parallel/{lighting,materials,geometry}` worktrees.
The primary agent has merged their shared scene layouts, canonical closure records,
shader assembly and recorder entries. Main-source regression and combined-scene
validation passes in `native/work/parallel-integration/validation-v1`: the full
point/sun MIS and no-MIS corpus, glass/GGX state and image checks, camera-motion
corpus and 24 complete command replays. The eight combined scenes check 49,152
film components, with maximum absolute error 3.337860107421875e-5; 5,769 strict
differences and five differing repeated-film components remain recorded.

`native/tools/feature_combination_gate.py` exercises all three slices in eight
native scenes with independent original CPU films and full command replay.
`lighting_mis_gate.py`, `glass_gate.py` and `camera_motion_gate.py` retain their
independent feature corpora and existing regression checks. Strict diagnostics
and repeated-film differences are reported under unchanged comparison criteria.

Forward sphere/disk lamp hits, finite suns and their NEE/forward MIS weights now
have integrated Linux evidence. Broader emitters, light trees, environment MIS,
nonconstant emitter shaders and transparent shadows remain required. Glass covers
single-scatter GGX reflection/transmission, backfacing IOR, TIR, mixtures and pass
classification; thin film, multi-GGX glass and full Principled remain required.
Camera motion is limited to perspective pinhole cameras and static triangles.
Focused native Android builds pass for ARM64/x64 using the existing toolchains.
Static and moving-camera tests pass on Pixel, Samsung and HP. Forward-light state
checks pass on Samsung and HP. Glass remains unresolved: HP aborts during shader
compilation with Scudo allocation exhaustion; Samsung fails a near-singular
refraction PDF comparison. Neither reaches the combined-image corpus. The failed
coordinator, per-device outputs and unchanged post-failure source/binary checks are
retained in `native/work/parallel-integration/android-v1/validation`.

Every WebGPU test uses the unchanged runtime from
`CodeBrix.WebGpu.Native.MitLicenseForever` 1.0.290.1433. Staged hashes are checked
against the package, and successful native runs verify the loaded library path.
Copied-shader SVM-storage diagnostics also timed out on Pixel and HP; they have
not been promoted to production or counted as passed features.

## Spot and area lighting extension (2026-10-19, Linux main validation passed)

Registered NEE, lamp intersections and forward MIS now include constant spot
lights and rectangle/ellipse/disk area lights with native shape/spread sampling.
The sealed lighting lane passed 4,096 independent native sampler probes, six
invalid-input checks, 53 broad-light state cases, 12 independent CPU/GPU images,
point/sun and combined glass/camera regressions, and 20 complete command replays.
Its first rectangle-image mismatch was fixed by preserving native float operation
order and bounded trigonometric evaluation; failed attempts remain retained.

The source delta is merged into main with reviewed in-repository derivations and
attribution. `python3 native/tools/broad_lighting_gate.py --output NEW_DIRECTORY`
builds and executes the maintained corpus. Main validation passes all 26 reports
in `native/work/parallel-integration/lighting-round2-v1`, with source, executable
and runtime pins unchanged. The 12 new scenes exercise 2,141 forward-light paths
and 27,285 shadow paths; all 73,728 film comparisons pass the practical criterion.
Maximum absolute image error is 1.010894775390625e-4; 6,253 strict differences
and 13 repeated-film component differences remain recorded. All native
allocations are released. Build-input review covers 263 compilation entries,
1,296 compiler-observed headers, 11 static archives and six gate executables;
the source-license audit passes 25,610 files with zero errors.

No Android spot/area claim is made yet. Mesh/environment emitters,
textured light shaders, light trees and degenerate area dimensions remain
guarded. Transparent shadows now pass the integrated corpus below. No WebGPU
runtime or numerical tolerance was changed.


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
Superseded: the transparent-lighting crash was diagnosed and the candidate was
reintegrated; see the integration section below.

## Compiler pressure: scene specialization, light-stage split and pipeline tiers (2026-10-19)

Vulkan drivers inline every WGSL function into each entry point. The generic SVM
interpreter therefore gave the driver one function of about 2.3 MB of inlined
WGSL for `prepare_surface`, `shade_background`, `shade_volume` and the
transparent-shadow stage. The HP Intel compiler (Mesa 24.0.8) crashed or exhausted
Scudo's 256 MiB size-class regions on these entries, and the Pixel 4 XL Adreno
compiler needed minutes (509 s for `prepare_surface`).

- **Scene-driven specialization** (`native/cycles/pipelines.{h,cpp}`). Optional
  handlers are comment regions (`//#if FEATURE else FALLBACK` ... `//#endif`); the
  unspecialized module keeps every handler. Before creating a pipeline the device
  decodes the uploaded `svm_nodes` exactly as the WGSL interpreter does (shader
  headers, shader and conditional jumps, node, BSDF-data and texture-transform
  record sizes) and keeps only reachable SVM node handlers and closure setups,
  plus area-light and object/camera-motion code when the uploaded lights, object
  flags or camera use them. A removed handler's fallback reports error bit 2; it
  never produces substitute radiance. Enabled features only grow during a device
  lifetime, so a scene update reaching a new handler recompiles the affected
  pipelines. `CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0` compiles every handler.
  Captures record the module the device actually compiles.
- **Direct-lighting split.** Light sampling and BSDF/MIS/shadow connection run
  in separate pipelines (`shade_surface_light`, `connect_surface_light`); the
  sample is stored bit-exactly in the GPU-private stage record. Recorder v6 adds
  pipeline entry 15; earlier formats replay unchanged. The integrated partition
  is described in the next section.
- **Call-site consolidation** (unchanged arithmetic and order): one image call for
  all image node layouts, one `attribute_fetch` call in `attribute_read`, one
  `svm_image_sample` in box projection.
- **Tiers and reporting.** Each pipeline creation is timed against a declared
  budget (60 s default, `CODEBRIX_CYCLES_PIPELINE_BUDGET_MS`); an overrun marks the
  session degraded with a warning but never changes the variant. A non-fatal
  creation failure retries once in the conservative tier (workgroup size 1, which
  can change last bits on Linux Intel but must meet the practical criterion);
  device loss and driver crashes are reported with the stage name (a scoped signal
  breadcrumb names the stage if the driver terminates the process). Gate and
  replay JSON carry `pipelineReport` (tier, degraded, warnings, compile times).
  The shipped runtime exposes no pipeline-cache API; Linux Mesa's disk cache is
  the only warm path observed (41 s cold, 8 ms warm); HP and Pixel shell
  processes recompile every time.
- Measurements and the original HP/Pixel validations are in the B3 handoff; the
  integrated tree's Linux validation is described in the next section. Android
  devices have not run the integrated tree yet.

## Integrated lighting, specialization and ARM adapters (2026-10-19)

The working tree now combines three further slices on Linux x64 with the
unchanged CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433 runtime. Linux
validation ran every corpus with a fresh, empty Mesa shader cache, so driver
compile failures cannot be hidden by the disk cache; outputs are in
`native/work/phaseC/` and the frozen checkpoint
`native/evidence/native-integration-20261019/`.

- **Scene-driven specialization and pipeline tiers** (`native/cycles/pipelines.{h,cpp}`)
  as described in the previous section, now also covering emissive triangles
  (`LIGHT_TRIANGLE`: any triangle in the light distribution or any shader flagged
  as a MIS emitter) and the triangle-light private object-motion code
  (`MOTION_OBJECT`). Uploading the light distribution counts as a scene change.
  `CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0` still compiles every handler. Gate
  probes that compile their own copy of the surface module specialize it the
  same way.
- **Emissive triangle lights.** Flat-CDF constant emission with native
  solid-angle/area sampling, centre-area selection, sidedness and forward-hit
  MIS (`wavefront_triangle_light.wgsl`), including moving and deforming emitters.
  The triangle sampler keeps private copies of the motion arithmetic it needs; the
  shared motion shader is unchanged. Static instanced emitters remain rejected:
  the GPU matches the CPU at identical inputs, but closest-hit barycentrics differ
  by 1-3 ULP (driver FMA contraction and the oracle's SSE reciprocal), which flips
  edge-grazing samples. Light trees, environment and textured/nonconstant
  emission remain unimplemented.
- **Direct-lighting split.** `shade_surface_light` samples the lamp or emissive
  triangle and the forward-hit triangle MIS weight; `connect_surface_light`
  applies holdout, emission, roulette and the sampled light's BSDF/MIS/shadow
  setup. The GPU-private surface record carries the weight and the 13-word light
  sample. (An alternative partition that sampled lights after emission and
  roulette inside one entry produced wrong moving-emitter images on Linux Intel
  only when the area-light handler was specialized away, and compiled for 149 s;
  it was not adopted.)
- **Transparent-shadow split.** A GPU-only `prepare_shadow_shader_data` stage
  (workgroup 64) caches per-hit ShaderData for `shade_shadow_transparent`
  (workgroup 1), which avoids an intermittent Intel Mesa 25.0.7 compiler crash
  (`brw_opt_combine_constants`) on the combined entry.
- **ARM64 oracle rounding.** `NATIVE_FLOAT3_RECIPROCAL_NORMALIZE` (from
  `__KERNEL_SSE__` on the oracle host) selects per-component division or
  reciprocal-length multiplication for ShaderData normals, matching each CPU
  oracle's `normalize(float3)`. Cosine-hemisphere sampling and the specular
  valid-normal adjustment use the existing rounding adapters, which are bit-exact
  on Intel and Adreno; on Linux these only move results closer to the CPU oracle.

Command recorder formats (replay reads every format):

| Format | Adds pipeline entries |
| --- | --- |
| v1 | 0 control, 1 init_camera, 2 intersect_closest, 3 shade_background, 4 shade_surface, 5 prepare_surface (implicit size 64) |
| v2 | explicit dispatch sizes (kind 5 commands) |
| v3 | 6 intersect_shadow, 7 shade_shadow |
| v4 | 8 shade_light_forward |
| v5 | 9 prepare_shader_data, 10 intersect_volume_stack, 11 shade_volume, 12 shade_shadow_transparent, 13 shade_surface_light, 14 shader_eval_volume_density |
| v6 | 15 connect_surface_light |
| v7 | 16 prepare_shadow_shader_data |

Remaining guards and gaps: static instanced triangle emitters; moving
transparent shadows; volume NEE/MIS, shadow attenuation and heterogeneous media;
light trees, environment MIS and textured emission; light/shadow linking, AO,
shadow catchers and MNEE. The emitter-sampling entry with object motion still
exceeds the 60 s compile budget cold on Linux (warning only). Old captures replay
with their own unspecialized modules and can still crash the driver with a cold
cache. Android has not run this combination yet.
