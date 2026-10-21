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
evidence remains subject to the release review in `../docs/RELEASE-LICENSE-PLAN.md`.

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
shaders and transparent shadows remain explicitly unsupported (smooth
shadow-terminator offsets were added in Phase E3; see below). These remain required work. The direct-light contribution
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
  integrated tree's Linux validation is described in the next section and its
  Android device results in the two sections after it.

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
cache. ARM64 and Android x64 device results are in the next two sections.

## Integrated tree on ARM64 Android devices (2026-10-20)

The integrated tree ran natively on the Samsung SM-X700 (API 36, Adreno 730; the full-fidelity device) and the
Pixel 4 XL (API 33, Adreno 640; the old-hardware gauge). It used the unchanged runtime, Android ARM64 binaries
built from the same sources with NDK 29.0.14206865 at API 33, and the practical criterion unchanged.
Outputs are in `native/work/phaseC2/arm64/`.

- ARM64 oracle rounding works on the device. On the Samsung, glass_bounce (path 126), glass_render and
  microfacet_render pass with exactly the numbers recorded when the adapters were introduced. On the Pixel,
  glass_bounce and glass_render now also pass.
- Samsung passes these, with 71 on-device replays of its own v3/v6/v7 captures:
  - transparent shadows (v7) and transparent lighting;
  - point/sun/spot/area lighting, MIS and broad lighting;
  - feature combination;
  - object and deforming motion;
  - camera motion;
  - glass/GGX SVM, bounce and render;
  - shader data, closest and opaque shadows;
  - finite and closed homogeneous volumes.
  Linux Phase C v6/v7 captures replay within tolerance, except the triangle mode-2 capture (below).
- Specialized pipelines compile in at most 32.6 s on the Samsung and 70 s on the Pixel. The emissive-triangle
  `shade_surface_light` with object motion took 204.8 s on the Samsung. It still renders correctly, with a
  degraded-latency warning.
- Specialization is mandatory on Adreno. The every-handler `shade_surface_light`
  (`CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0`) crashes the Adreno 730 driver and exhausts memory on the Adreno 640.
  Bit-identity between specialized and unspecialized modules is therefore established on Linux only.
- Open on ARM64 in this run, each revisited in Phase E (sections below):
  - Emissive-triangle sampling and the emitter-sample probe failed against the on-device (bionic) oracle. Under the
    validation reference policy the probe passes and only the deforming emitter still fails the sampler gate.
  - Triangle image mode 2 diverged by one path decision on Adreno 730, even when replaying the x64 capture; it passes
    since the exact traversal.
  - On the Pixel 4 XL, object/deforming motion images, feature combination, the volume-boundary state test and the
    camera-inside closed volume failed in this run; they pass after the Adreno 640 fix described in
    "Pixel 4 XL (Adreno 640) function-scope array fix" below.

## Android x64 (HP, Intel Mesa 24.0.8) on the integrated tree (2026-10-20)

The integrated tree was built for android-x64 (NDK 29.0.14206865, API 33) and run on the HP with cold compiles
(`MESA_SHADER_CACHE_DISABLE=true`; the device has no Mesa disk cache) and the unchanged 1.0.290.1433 runtime. The
build uses SSE normalization (`NATIVE_FLOAT3_RECIPROCAL_NORMALIZE=0`). Glass/GGX (svm, bounce incl. path 126,
render), microfacet, the transparent-shadow split (v7, `prepare_shadow_shader_data`), spot/area and point/sun
lighting with MIS and forward light, feature combination, the emitter-sample probe for lamps, finite homogeneous
volumes and volume boundaries, camera motion, object motion and deforming-triangle images pass the practical
criterion at full tier with no compile-budget warnings; 65 v6/v7 command replays pass with 31,277 exact
scheduling readbacks. Evidence: `native/work/phaseC2/x64/`.

Three pipelines still exhaust Scudo's 256 MiB per-size-class regions inside the HP's Intel compiler and abort the
process, on every cold compile: the emissive-triangle `shade_surface_light` with object motion (triangle gate
mode 5), the triangle sampler gate's `emitter_sample_probe`, and the unspecialized `shade_surface_light`
(`CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0`). Emissive triangle lights are therefore not validated on the HP beyond
static triangle images 0-3 (which pass, with replays), and the specialization on/off bit-identity check cannot
run there. Phase E resolved this: the first two compile since the emissive-triangle compile-load change, and
the unspecialized configuration is refused on Android (sections below).

## Device tiers: persisted record, known adapters and init canary

WebGPU reports adapter identity and limits but not shader-compiler capacity, and
a vendor compiler crash ends the process. The WebGPU device therefore chooses
its pipeline tier once, when it is created and before any renderer pipeline
compiles (`native/cycles/device_tier.{h,cpp}`):

1. an explicit request (host API or `CODEBRIX_CYCLES_PIPELINE_TIER`), reason
   `override` (full) or `opt-in` (conservative);
2. the persisted device-identity record, reason `record`: a full-tier crash
   breadcrumb or pipeline-creation failure selects conservative; full-tier
   compiles without any failure select full (so the record overrides the table);
3. the known-adapter table, reason `table`;
4. full fidelity, reason `default`.

The optional init canary then compiles one representative module (the surface
module with no optional handlers, entry `prepare_surface`) in the full tier. A
canary failure selects conservative before the scene compiles (reason `canary`);
a canary crash leaves a breadcrumb, so the next run starts conservative from the
record; a canary over its budget (10 s, `CODEBRIX_CYCLES_PIPELINE_CANARY_BUDGET_MS`)
only warns. During rendering a non-fatal creation failure still retries in the
conservative tier (reason `failure`) and is recorded. A slow compile never
changes the tier: it warns and marks the session degraded, because the
conservative tier (workgroup size 1) is not bit-identical to full fidelity. The
only exception is the explicit old-hardware opt-in
`CODEBRIX_CYCLES_PIPELINE_BUDGET_ACTION=conservative` (reason `budget-opt-in`,
never recorded). Full-tier WGSL is unchanged by this mechanism.

Known-adapter table (`known_adapters()`; device and driver are substrings of the
WebGPU adapter device name and driver description):

| Entry | Device | Driver | Tier |
| --- | --- | --- | --- |
| Pixel 4 XL Adreno 640 | `Adreno (TM) 640` | any | full, 20 s compile budget |
| Samsung SM-X700 Adreno 730 | `Adreno (TM) 730` | any | full |
| HP Intel Xe (Android x64) | `Intel(R) Xe Graphics` | `Mesa 24.0.8` | full |
| Linux Intel UHD ADL-S | `Intel(R) UHD Graphics (ADL-S GT1)` | `Mesa 25.0.7` | full |

An unknown mobile adapter (Android build, or a Qualcomm, ARM, Imagination,
Samsung or Apple vendor id or Adreno/Mali/PowerVR/Xclipse device name) starts
full with a 20 s compile budget instead of 60 s (warnings only). A table entry
may carry its own budget: the Pixel 4 XL entry is full with 20 s, because full
tier meets the practical criterion there and the conservative tier was not
faster (see below). No table entry selects the conservative tier; it remains a
reactive fallback after a crash breadcrumb or a creation failure, or an explicit
opt-in. An explicit `CODEBRIX_CYCLES_PIPELINE_BUDGET_MS` replaces any budget.

**Persisted record.** Enabled only when the application supplies a writable
directory (`cbc_host_tier_config`, or `CODEBRIX_CYCLES_DEVICE_RECORD_DIR`); missing
directories are created; problems become warnings and never fail rendering. The
file is `codebrix-cycles-device-<fnv1a64 of vendor, architecture, device,
driver description, vendor id, device id>.txt`, UTF-8 text, one `key=value` per
line, fields free of newlines and `|`:

```
codebrix-cycles-device-record 1
vendor=<WGPUAdapterInfo.vendor>
architecture=<WGPUAdapterInfo.architecture>
device=<WGPUAdapterInfo.device>
description=<WGPUAdapterInfo.description (driver)>
runtime=codebrix-api=<n>;wgpu-native=<v>;wgpu-core=<v>;target=<triple>;patches=<list>
shaderHash=fnv1a64:<16 hex digits over every production WGSL module>
stage=<tier>|<module>|<entry>|<successes>|<failures>|<last ms>|<max ms>
breadcrumb=<tier>|<module>|<entry>|signal or interrupted|<signal>|<unix time>
failure=<tier>|<module>|<entry>|<message>
```

Unknown keys are ignored; at most 16 breadcrumbs and 16 failures are kept. A
record whose identity, runtime or shader hash differs is reported
(`identity-mismatch`, `stale-runtime`, `stale-shader-hash`), never applied, and
replaced by the next write. Writes take an advisory `flock` on `<record>.lock`,
re-read the file and rename a temporary copy, so concurrent processes merge.
Before each pipeline creation the device writes `<record>.pending-<pid>-<device>-<serial>`
(runtime, shader hash, tier, module, entry, time); the crash handler appends
`signal=<n>`; the file is removed when creation returns. A pending file whose
process no longer runs becomes a breadcrumb on the next start (`interrupted` if
no signal was written, which includes out-of-memory kills and forced stops while
compiling). Delete the record to start again at full fidelity.

**Host C API (additive; ABI 1).** `cbc_host_tier_config(const CBCTierConfig *,
const char *record_directory)` sets the process-wide request (`CBC_TIER_AUTO`,
`CBC_TIER_FULL`, `CBC_TIER_CONSERVATIVE`) and flags (`CBC_TIER_RECORD_OFF`,
`CBC_TIER_RECORD_READ_ONLY`, `CBC_TIER_CANARY`) for hosts created afterwards;
`CBCTierConfig` is size/ABI/reserved checked. `cbc_host_device_report_json`
returns the capability report: backend, adapter identity, tier, reason and
detail, known-adapter entry, compile budget and its source, record mode, path,
status and breadcrumbs, canary result, environment overrides, warnings and
compiled pipelines. The .NET wrapper exposes `CyclesHost.ConfigureTiers` and
`DeviceReportJson`. Gate and replay JSON carry the same decision under
`pipelineReport.decision`; the pipeline log adds `tierReason`.

**Environment overrides (gates).** `CODEBRIX_CYCLES_PIPELINE_TIER=full|conservative|auto`,
`CODEBRIX_CYCLES_DEVICE_RECORD_DIR=<dir>`,
`CODEBRIX_CYCLES_DEVICE_RECORD=on|off|ignore|readonly` (`ignore` reads and reports
without applying or writing), `CODEBRIX_CYCLES_PIPELINE_CANARY=1|0`, and the
existing `CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0`. Test hooks used only by
`native/tools/tier_gate.py`: `CODEBRIX_CYCLES_TEST_ADAPTER` (simulated identity for
the decision; the real adapter still compiles), `CODEBRIX_CYCLES_TEST_CRASH_PIPELINE`
(raise `SIGABRT` inside the breadcrumb scope of the named stage, or
`canary:prepare_surface`) and `CODEBRIX_CYCLES_TEST_CANARY=fail` (a canary
module the WebGPU front end rejects).

`native/tools/tier_gate.py` runs each scenario as a separate process of
`tests/host/tier_consumer.c` on Linux: fresh device, record reuse, a real crash
breadcrumb followed by a conservative start, stale shader hash and runtime,
table lookups (including the real adapter and an unknown mobile adapter),
record-over-table, overrides, canary pass/slow/failure/crash and a slow compile
that keeps the full tier. Every film meets the practical criterion against the
original CPU session.

**Device confirmation (2026-10-20).** Android builds of the merged tree (NDK
29.0.14206865, API 33, unchanged runtime) report these adapters and decisions,
with glass_render, transparent_lighting and lighting_mis passing the practical
criterion (outputs in `native/work/phaseC4/`):

| Device | Adapter device / driver description | Decision | Tier warnings |
| --- | --- | --- | --- |
| Samsung SM-X700 | `Adreno (TM) 730` / Adreno driver EV031.36.08.33 | full, `table` | none |
| HP Android x64 | `Intel(R) Xe Graphics (TGL GT2)` / `Mesa 24.0.8` | full, `table` | none |
| Pixel 4 XL | `Adreno (TM) 640` / Adreno driver EV031.31.04.01 | full, `table` (20 s budget) | compile-budget warnings only |

- On the Pixel 4 XL, the conservative tier (the former table entry) and the
  full tier (`CODEBRIX_CYCLES_PIPELINE_TIER=full`) give the same image errors and
  strict counts (glass_render film bit-identical); the conservative tier was not
  faster to compile (`shade_surface_light` up to 79 s against up to 70 s in
  transparent_lighting, 444 s against 410 s wall). The opt-in canary compiled in 2.0 s
  against its 10 s budget.
- The Samsung and HP glass_render films are bit-identical across runs with and
  without a device record. Specialized against unspecialized bit-identity cannot
  be checked on these devices, because the unspecialized surface module does not
  compile there (see "Open device items").
- A device record in a shell-writable folder under `/data/local/tmp` is created
  with the documented format, reused (`full`, reason `record`), and its
  `<record>.lock` serializes writers: with another process holding
  `flock(LOCK_EX)` for 60 s, a gate's record writes waited for it (about 56 s
  longer wall time) and the stage counts merged.
- On the HP, `CODEBRIX_CYCLES_TEST_CRASH_PIPELINE=prepare_surface` aborted the
  process; Android's signal chaining still let the handler append `signal=6` to
  the pending file. The next run consumed it as a breadcrumb and started
  conservative (reason `record`, workgroup size 1, practical criterion met, film
  bit-identical to full tier); after deleting the record the device returned to
  full (reason `table`).
- The Android x64 host gate passes on the HP with 38 exports.

## Managed WebGPU renders on Android and the 48-export host (2026-10-20)

Android builds of the ABI 1 tree (D2 typed API, D3 48 exports; NDK 29.0.14206865,
API 33, unchanged WebGPU runtime) pass `android_host_gate.py` on the Samsung
SM-X700 and the HP (48 exports in both binary reviews; 1,579 films each, 0
practical failures, 0 required strict differences; GPU lifecycle repeats within
the strict criterion). The validation app gained a WebGPU mode
(`android_app_gate.py --mode webgpu`, see MAINTAINER-README.txt): inside the
installed app the typed API renders three scenes (glass sphere, diffuse with a
sun, textured plane through image upload; 32x24, 16 samples;
`tools/hosttests/WebGpuScenes/WebGpuValidationScenes.cs`) on `CyclesBackend.Gpu`
and on `CyclesBackend.CpuReference`, and the runner recomputes every comparison
from the extracted films (outputs in `native/work/phaseD5/android/`):

| Device | Glass max / strict | Diffuse+sun max / strict | Textured plane max / strict | Practical | Tier decision | Compile-budget warnings | App time |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Samsung SM-X700 (Adreno 730) | 2.32e-6 / 301 | 4.17e-7 / 549 | 2.38e-7 / 30 | 0 | full, `table` (60 s budget) | none | 178 s |
| HP Android x64 (Intel Xe, Mesa 24.0.8) | 3.71e-5 / 319 | 1.43e-6 / 567 | 2.38e-7 / 32 | 0 | full, `table` (60 s budget) | none | 195 s |
| Pixel 4 XL (Adreno 640), optional gauge | 2.26e-6 / 315 | 4.17e-7 / 537 | 2.38e-7 / 35 | 0 | full, `table` (20 s budget) | `shade_surface_light` 121-125 s; `shade_surface` 24 s, `prepare_surface` 25 s, `shade_background` 21 s | 641 s |

Strict counts are out of 3,072 floats per film. On every device the GPU render
reported native progress events, a render cancelled from its first progress
event threw `OperationCanceledException` without faulting the device, and a
re-render on the same scene met the practical criterion. GPU render times are
dominated by first-use pipeline compiles (Samsung 40-48 s, HP 37-64 s, Pixel
135-181 s per scene). The ARM64 CPU reference films are bit-identical between
the Samsung and the Pixel. Smooth-shaded objects in these scenes used a zero
shadow-terminator geometry offset at the time; Phase E3 implemented the offsets
and the scenes now keep the default (confirmed on all three devices in the Phase E merge).

## Emissive-triangle compile load and the Android oracles' C library (2026-10-20)

**Compile load.** Drivers inline every WGSL call, and `shade_surface_light`
with `MOTION_OBJECT` + `LIGHT_TRIANGLE` held four copies of the emissive-triangle
vertex computation (the forward-hit MIS weight and the emitter sample each needed
the path-time and centre-time vertices, each with the object-motion transform,
its 30-step double-word arccosine and the motion sine/cosine). That entry inlined
to 3.35 MB of WGSL and ran the HP's Intel Mesa 24.0.8 compiler out of memory
(Scudo); the Samsung took 205 s and Linux 65-75 s cold. The sampler now reads
vertices from `triangle_light_prefill`, which computes every set an invocation can
use, in the original order, from one call site; the arccosine, the motion
sine/cosine and its disk sine/cosine are triangle-light-private copies whose
Horner series run as loops (the same operations in the same order). The shared
motion and trigonometry helpers are unchanged. Inlined size: mode-5
`shade_surface_light` 3.35 -> 1.32 MB, triangle without motion 1.60 -> 1.16 MB,
the triangle sampler gate's probe 2.15 -> 1.20 MB. Every Linux result is
byte-identical (triangle sampler and emitter-sample bins, all five triangle
images' GPU/CPU/repeat films and command streams). Cold single-pipeline compiles:

| Entry (cold, one pipeline per process) | Linux Intel Mesa 25.0.7 | HP Intel Xe, Mesa 24.0.8 | Samsung Adreno 730 |
| --- | --- | --- | --- |
| `shade_surface_light`, triangle + object motion (mode 5) | 75.3 -> 24.3 s | Scudo OOM at 2.25 GB after 21 s -> 27.4-27.6 s / 2.08 GB (2 of 2) | 203.9 s / 3.28 GB -> 29.4-30.0 s / 0.49 GB |
| `shade_surface_light`, triangle, no motion (mode 1) | 14.9 -> 14.7 s | 24.9 s / 2.61 GB -> 20.5 s / 1.84 GB | 34.1 s / 0.54 GB -> 20.1 s / 0.39 GB |
| `emitter_sample_probe` (triangle sampler gate) | compiled cold in both | Scudo OOM at 1.85 GB after 13 s -> 21.9-22.0 s / 1.99 GB (2 of 2) | 80.5 s / 1.47 GB -> 24.4-24.7 s / 0.41 GB |
| `connect_surface_light` (mode 5) | unchanged | 10.8 -> 10.9 s / 0.32 GB | 1.7 -> 1.7 s |
| `prepare_surface` (mode 5) | unchanged | 5.9 -> 5.9 s / 0.34 GB | 2.6 -> 2.6 s |

**Android oracles use bionic's arccosine and arcsine.** The Samsung triangle
sampler failures (24 in 22 of 4,096 samples) and the emitter-probe area-light
pdf (1.10 times the tolerance) are not SSE-versus-scalar float3 arithmetic. At
all 18 failing samples on static emitters the ARM64 oracle's
`acosf(cos_alpha)` (`kernel/light/triangle.h`, `safe_acosf`) differs from the
correctly rounded value, which glibc returns and the WGSL reproduces; the
probe's pdf difference is one `asinf` in the rectangle solid angle
(`kernel/light/area.h`), 2 ulp in one term reproducing both oracle pdfs exactly.
Over every binary32 input in [-1, 1], bionic arm64 `acosf` differs from the
correctly rounded result at 5,387,136 inputs and `asinf` at 682,540; bionic
x86_64 (the HP) differs at 5,717,766 and 4,220,748 with different results again;
glibc is correctly rounded. Evidence for this section: `native/work/e1/` in the E1 worktree (`libm/`, `analysis/`, `logs/probe-table-*.jsonl`, `runs/`); the sealed handoff is in `native/evidence/phaseE-merge-20261020/laneE1/`. Matching these
oracles on the GPU would mean reproducing bionic's libm (a separate, licensed
implementation, different per Android ABI). The decision taken instead is the
validation reference policy below: the correctly rounded glibc reference decides,
bionic is a diagnostic.

**Specialization off on Android.** `CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0` is now
refused on Android when the WebGPU device is created, with a message naming the
variable, before any pipeline compiles. It remains a desktop A/B diagnostic.

**Device results with these changes** (unchanged runtime, cold compiles, practical
criterion unchanged). HP: the triangle sampler gate now compiles (22 s); the
surface gate and all five triangle images pass, including the moving/deforming
emitter (mode 5) and the static-instanced rejection, with five on-device replays
and the five Linux triangle captures replayed exactly. Samsung: the surface gate,
triangle images 0 and 1 and the Linux captures of modes 0, 1, 3 and 5 pass
(the mode-5 capture's `shade_surface_light` now compiles in 29 s). On both,
glass_render, lighting_mis and transparent_lighting match the previous device runs
(glass bit for bit; the others within each run's own repeat variation).

## Pixel 4 XL (Adreno 640) function-scope array fix (2026-10-20)

The three Pixel-only failure families from the integrated-tree run were one driver defect class: the Adreno 640
compiler (driver EV031.31.04.01) miscompiles dynamically indexed arrays held inside function-scope aggregates in
large entries. Each family was reproduced by replaying the unchanged Linux x64 Phase C captures on the Pixel
(`native/work/e2/probe/`): the first scheduling readback that differs isolates the stage, and arena dumps after
each dispatch, compared with the same replay on Linux after injecting the Linux state, isolate the field.

- **feature_combination (error 512).** `prepare_surface` raised the microfacet lookup-table bounds error in
  `microfacet_lut3` (generalized Schlick for glass) because `table_count` read 0: the table-descriptor address
  `p(14)` arrived with its low 16 bits cleared (0x270000 instead of 0x27E7F0). Coordinates, offsets and the table
  were correct; adding an atomic probe hid the fault. The B3-era capture of the same scene replays cleanly; the
  larger `prepare_surface` after the ARM rounding adapters exposed the latent fault.
- **Object and deforming motion images (zero film).** `prepare_surface` produced no closures: its
  stage record held `count=0, remaining=0` where Linux has `count=1, remaining=11`, so shading created no shadow rays
  and the film stayed zero.
  The B3-era deforming capture fails the same way; the fault predates B3 (motion images had not run on the Pixel).
- **Volume-boundary state and camera-inside closed volume (integer stack mismatches).** In
  `intersect_volume_stack` every entry of the function-local `array<NativeHit,64>` read back as the last hit
  appended, so the sort and the enclosure test picked the wrong object for the initial volume stack.

The fix keeps every computation and comparison unchanged:
- `wavefront_svm.wgsl`: the SVM value stack and its initialization mask live in one module-scope
  `var<private>` array (`svm_stack`, words 0..254 and mask words 255..262) instead of two arrays inside the
  function-local `SvmState`; the mask is cleared with constant-index stores at the start of every evaluation
  (evaluations never nest). Two separate private arrays lost mask bits on this driver (error 8), and an
  in-struct vector mask still failed, so the single array is deliberate.
- `wavefront_volume_stack.wgsl`: hits are parallel scalar arrays (t, object, prim) filled through a pointer; a
  pointer-passed array of structs still failed.

Results with the fix (Pixel, full tier by table, 20 s budget, warnings only):

| Corpus (Pixel, single confirmation run) | Before | After |
| --- | --- | --- |
| feature_combination (+1 replay) | error 512 | PASS; replay 408 readbacks, max 3.87e-7 |
| object/deforming motion images, moving and stationary (+2 replays) | zero film, all four | PASS, all four; replays 414/420 readbacks |
| volume_boundary state test | `volume_stack[0].object` mismatch | PASS |
| camera-inside closed volume (+1 replay) | `volume_stack[1].object` mismatch | PASS; replay 1,116 readbacks, max 1.53e-5 |
| regression: lighting_mis, glass_bounce, glass_render, transparent shadow/lighting, camera static/motion, GGX set | passed in C2 | PASS; lighting_mis (8), glass_render and transparent (8) films bit-identical to the C2 Pixel films |

Linux x64 results are unchanged: every deterministic film of the affected gates is bit-identical before and after,
the only differing films are atomic-accumulation cases whose own repeat differs within the run, the glass film is
unchanged with specialization on and off, and all-lanes passes. The conservative tier does not change these outcomes
(emulated on the pre-fix WGSL with workgroup-1 modules and dispatches, the same failures occur), so it is still never
selected for this adapter. The fix does not change compile times: on the Pixel the motion entries still take
50-73 s (`intersect_closest`, `intersect_shadow`, `prepare_surface`, `prepare_shader_data`) and the surface/camera
entries 21-31 s against the 20 s budget, as warnings. Evidence: `native/work/e2/` in the E2 worktree.

## Validation reference policy (2026-10-20)

Decision (2026-10-20): Android device validation judges the device GPU against a **correctly
rounded CPU reference**. Today that is the Linux x64 glibc CPU oracle; Linux ARM64 glibc (a
Raspberry Pi 5) and macOS are planned to confirm it in Phase F. The CPU reference a gate computes on
the Android device itself, with Android's bionic C library, is kept as a **diagnostic** column: its
deviation from the glibc reference is reported per gate and it never decides pass or fail. No libm
code is imported or emulated.

**Why bionic is a diagnostic.** Cycles calls the C library's binary32 functions (`safe_acosf` and
`safe_asinf` in light and emitter sampling, `atan2f`/`sinf`/`cosf`/`tanf`/`expf`/`logf`/`powf` in
closures, camera, SVM nodes and scene preparation). glibc 2.41 returns the correctly rounded
`acosf`, `asinf`, `atanf`, `atan2f`, `tanf` and `sqrtf`, and the WGSL kernels reproduce those values.
bionic does not, and arm64 and x86_64 bionic differ from each other. Measured with
`native/tools/libm_probe.py` (records in the X1 worktree's `native/work/x1/libm/`; the Linux record of the merge is `native/work/merge1/reference/libm-linux-x64-glibc.json`):

| Function (domain) | Linux x64 glibc 2.41 | Samsung SM-X700 bionic arm64 | HP 87FE bionic x86_64 |
| --- | --- | --- | --- |
| `acosf` (all 2,130,706,434 binary32 in [-1,1]) | 0 | 5,387,132 | 5,717,762 |
| `asinf` (same) | 0 | 682,532 | 4,220,746 |
| `atanf` (every 16th binary32, 267,386,880) | 0 | 394,660 | 394,660 |
| `tanf` (same) | 0 | 18,985,998 | 18,985,998 |
| `atan2f` (2^24 pairs in [-8,8]^2) | 0 | 2,708,169 | 2,708,169 |
| `sinf` / `cosf` (every 16th binary32) | 1,836,020 / 1,764,666 | same values as glibc | 1,836,018 / 1,764,664 (other results) |
| `expf` / `logf` / `powf` | 10,698 / 26,322 / 10,435 | same values as glibc | same values as glibc |
| `sqrtf` | 0 | 0 | 0 |

Counts are inputs whose result differs from the correctly rounded one (at most 1 ulp everywhere);
inputs within 2^-50 of a rounding midpoint are counted separately as undecidable (at most 10 per
function). glibc's own `sinf`/`cosf`/`expf`/`logf`/`powf` are not correctly rounded either; for those
the Linux reference is glibc's result, which the Samsung's bionic matches bit for bit. The decisive
functions for the earlier Android failures are `acosf`/`asinf`: all 18 static-emitter triangle-sampler
failures and the emitter-probe area-light pdf are 1-ulp bionic errors (Phase E1).

**Two routes by which the device C library reaches a result.** The device's CPU *kernels* (the local
reference) call bionic, and the device's *scene preparation* (light records, transforms and motion
decompositions, BVH, lookup tables, the gates' own fixed inputs) runs on the device too and is what the
GPU consumes. A reference prepared on Linux therefore differs from the GPU's inputs: on the HP the
emitter probe's light records differ in 8 words and the GPU result follows them (24 practical
differences against a Linux-prepared reference, 0 against the reference computed from the HP's own
scene). The authoritative comparison is therefore the **device-scene flow**:

1. the device runs the gate once with `CODEBRIX_CYCLES_SCENE_DUMP` (no GPU work): it writes the scene
   tables it prepared, `KernelData`, the gate's fixed inputs and its local CPU reference;
2. Linux x64 runs the same gate binary with `CODEBRIX_CYCLES_SCENE_LOAD` (CPU only) and exports the
   glibc reference computed from exactly that scene (`native/tools/android_reference.py
   scene-reference`, which also records per table how the device-prepared inputs differ from Linux's);
3. the reference is pushed beside the staged binaries and the device runs the gate with
   `CODEBRIX_CYCLES_REFERENCE_IMPORT` and `CODEBRIX_CYCLES_SCENE_VERIFY` (its local reference must equal
   the dump pass's bit for bit, so the imported reference belongs to these inputs); the gate compares
   its GPU output with the imported reference and appends the local-versus-glibc deviation to
   `reference-diagnostic.jsonl`;
4. the host recomputes every comparison from the pulled files (`android_reference.py compare`):
   GPU versus glibc (authoritative, must agree with the device's verdict), local versus glibc and GPU
   versus local (diagnostics, the latter being the comparison used before this policy).

Provenance per reference: Linux gate binary SHA-256, glibc version, source pins, device and Linux scene
SHA-256, every reference file's SHA-256 and the set digest (`oracleSha256`). Without any of these
environment variables every gate behaves exactly as before (Linux results unchanged).

Reference data flow per Android gate:

| Gate (Android) | Before | After |
| --- | --- | --- |
| `triangle_sample`, `emitter_sample` (sampler arrays) | on-device bionic CPU | device-scene glibc reference; bionic diagnostic |
| Image gates from `lighting_gate.cpp` (`lighting`, `lighting_mis`, `broad_lighting`, `transparent_lighting`, `triangle_lighting`, `feature_combination`, `object_motion_render`, `deforming_motion_render`) | on-device bionic CPU film | device-scene glibc film; bionic film diagnostic |
| `render_gate.cpp` independent image (`glass_render`, `microfacet_render`, `render`, volume renders) | on-device bionic CPU film | Linux-prepared glibc film (`android_reference.py export`); no scene exchange yet |
| `render_gate.cpp` lockstep stage checks; state gates (`surface`, `closest`, `shadow`, `shader_data`, glass/microfacet `svm`/`bounce`, `camera`, `volume`, `volume_boundary`, `light_state`, `transparent_shadow`, `svm_surface`) | on-device bionic CPU | **unchanged (local)**: the stage checks' CPU input is the device's own GPU state; the state gates are not yet routed through the policy |
| Command replays of the device's own captures and of Linux captures (`render_replay`) | the capture's recorded readbacks and film | unchanged: no CPU reference on the device; the Linux captures carry the Linux x64 film |
| `android_host_gate.py` (host API films) | Linux reference run (`host_gate.py`) | unchanged: already a Linux reference |
| `android_app_gate.py` CPU suite | Linux references packaged in the APK | unchanged |
| `android_app_gate.py --mode webgpu` (managed API) | in-app CPU film (device scene, bionic) | `--linux-reference`: Linux glibc CPU films rendered by `WebGpuRenderTests` with `CODEBRIX_CYCLES_REFERENCE_EXPORT` (Linux-prepared scene; the managed API has no scene exchange); in-app CPU film diagnostic |

Results under the policy in the X1 worktree, before E1 and X2 were merged (unchanged 1.0.290.1433 runtime;
strict counts are of the reached cases; the merged-tree results are in "Merged tree on the devices"):

| Device / gate | GPU vs glibc reference (authoritative) | Local bionic CPU vs glibc (diagnostic) | Before (GPU vs local bionic) |
| --- | --- | --- | --- |
| Samsung `emitter_sample` | **PASS**, 135 strict, max 9.06e-6 | 1,192 floats differ, 1 practical (the area-light pdf) | FAIL (sample 4093 pdf) |
| Samsung `triangle_sample` | **FAIL**: 0 failures on the static emitters; 41 practical in 24 samples, all on the deforming emitter | 4,620 floats differ, 65 practical | FAIL, 24 practical in 22 samples (18 static, libm) |
| Samsung `triangle_lighting` | modes 0-1 PASS; **mode 2 FAIL** (component 1484, 0.351310 vs 0.311020; passes since the exact traversal); modes 3-5 not reached | 1,973 floats differ, 0 practical | same mode-2 failure |
| Samsung `glass_render` / `lighting_mis` / `transparent_lighting` | PASS: 98 image strict / 1,162 / 974 | 2,280 / 4,509 / 5,108 floats differ, 0 practical | PASS; glass film `8199ad80...` bit for bit as C2/E1 |
| HP `emitter_sample` | **PASS**, 235 strict, max 5.1e-3 | 617 floats differ, 0 practical | PASS |
| HP `triangle_sample` | not reached: compiler memory (compiles since the E1 change; passes in the merged tree) | - | same |
| HP `triangle_lighting` | modes 0-3 PASS (699 strict); mode 5 not reached (passes in the merged tree) | 195 floats differ, 0 practical | same |
| HP `glass_render` / `lighting_mis` / `transparent_lighting` | PASS: 152 image strict / 2,769 / 1,933 | 168 / 10 / 317 floats differ, 0 practical | PASS; glass film `bafd466d...` bit for bit as C2/E1 |
| Linux v6 triangle captures replayed | Samsung tri0/1/3/4 PASS, tri2 FAIL (passes in the merged tree); HP tri0-3 PASS, tri4 compiler memory (passes in the merged tree) | - | same |
| Managed WebGPU mode (glass, diffuse+sun, textured plane) | Samsung and HP PASS against Linux films, 0 practical (max 2.8e-5 / 2.3e-6) | in-app CPU films differ from Linux's (up to 41 strict differences per film), 0 practical | PASS against the in-app film |

The Samsung's remaining sampler failures are on the deforming emitter only. There the Samsung's own CPU
reference and the glibc reference computed from the Samsung's scene disagree in 44 fields, and against
the Linux-prepared reference the HP's x86_64 bionic CPU agrees bit for bit on 561 of the emitter's 731
samples where the Samsung's agrees on 108: it is ARM64
float3 arithmetic (the `NATIVE_FLOAT3_RECIPROCAL_NORMALIZE` class from B2), not libm, which an x64
reference cannot reproduce. Re-judged against glibc, Phase E1's HP sampler run (with E1's compile
fix) has no static-emitter failure either; it keeps 5 practical differences on the
deforming emitter. The Linux ARM64 glibc reference (Pi 5, Phase F) is the lane that confirms or
refutes this; macOS confirms the libm part on a second correctly rounded library.

Reproduce: `python3 native/tools/libm_probe.py android-arm64 OUT.json --serial SERIAL` (or
`android-x64`, `linux`); `python3 native/tools/android_reference.py export|scene-reference|compare
--help`. The device runner used for these results is `native/work/x1/tools/run_corpus.py
--device-scene --reference <export>` (copied for the merge as `native/work/merge1/tools/run_corpus.py`).

## Exact traversal and intersection arithmetic (2026-10-20)

The closest, shadow (opaque and transparent) and volume-stack traversals, and
the emissive-triangle planar hit, now reproduce the CPU oracle's ray/triangle
and instance-push arithmetic bit for bit. Before, the WGSL used plain products
and sums that drivers may fuse into FMA, and an exact divide where the x86
oracle takes an approximate reciprocal.

- **Contraction-proof sums.** `c_ray_triangle_intersect` (in
  `shaders/wavefront_triangle.wgsl`) rounds every product before it reaches a
  sum, using the integer `rounded_add`. No driver can fuse it. The generated AST
  version stays in the module as `c_ray_triangle_intersect_ast` for review.
- **Oracle grouping.** A new layout constant, `NATIVE_TRIANGLE_ARITHMETIC`, is
  emitted from the source host, the same way `NATIVE_FLOAT3_RECIPROCAL_NORMALIZE`
  is:
  - **0 (scalar build, ARM64 without SSE2NEON).** `ray_triangle_dot` is
    `(x+y)+z` and the reciprocal is a correctly rounded `1/x`.
  - **1 (x86 SSE4.2 build).** `ray_triangle_dot` is `x+(y+z)`. `madd`/`msub` are
    unfused. The reciprocal is `RCPSS` followed by one Newton step,
    `r*(2-r*x)`, with each operation rounded.
  - **2 (any other SSE build, i.e. SSE2NEON or an unrecognised `RCPSS`).** SSE
    grouping with `1/x`; this mode is documented as not oracle-exact.
- **`RCPSS` model.** On the Intel i7-12850HX that produces the Linux oracle, the
  result depends only on the sign, the exponent and the top 11 mantissa bits.
  - Its 12-bit mantissa is the value nearest to `2^25/(4097+2i)`, with exponent
    `253-e`. Denormal inputs give infinity, infinities give zero, and underflow
    flushes to zero.
  - An exhaustive check of all 2^32 inputs against the instruction found no
    difference.
  - `wavefront_layout()` re-checks every mantissa class on the host before it
    emits mode 1. A host whose `RCPSS` differs, such as another x86 vendor, gets
    mode 2.
- **Instance push.** `transform_vector` and the object-motion push compute
  `transform_point`/`transform_direction` exactly as the oracle does:
  - SSE builds: transpose and accumulate `((w+z)+y)+x`.
  - Scalar builds: the float4 dot `(x+y)+(z+w)`, using a signed zero for
    directions.
  - The shared `wavefront_motion.wgsl` is unchanged.
- **Closest-stage lamp hits.** The area-light quad test (`area_light_intersect`
  plus `ray_quad_intersect`) and the spot back-face test use rounded dots, sums
  and quotients.
- **min/max.** Builtin `min`/`max` equal Cycles' `(a<b)?a:b` for every non-NaN
  input. A `select()` form also mirrors NaN inputs, but it made an intermittent
  Intel Mesa 25.0.7 backend crash (`brw_opt_combine_constants`) frequent in the
  triangle-light module.

Gates:

- **`closest_gate`.** Adds a precision fixture (`cycles/precision_fixture.h`):
  - Geometry: the Principled bench sliver (bench triangle 328, 0.62 m x 5.7 mm),
    seven synthetic slivers shared by three instances (mirrored, rotated,
    scaled), and an instanced emissive quad.
  - Rays: 8,192, aimed at edges, vertices, the quad diagonal and interiors, with
    0-2 ULP perturbations. They include the exact ray that stopped the
    Principled bench at stage 30.
  - Every hit's t, u, v, prim, object and type must equal the CPU bit for bit.
- **`shadow_gate`.** Adds 4,096 near-tangent shadow rays on the same fixture:
  - tmax is within 1e-6 of the target.
  - The aimed emitter triangle is excluded as the light.
  - The blocked decision and every state field must match exactly.
- **Triangle gate.** Now renders static instanced triangle emitters (mode 4) and
  checks them against an independent CPU image plus a replay. The renderer guard
  is lifted.

Command 574 of the Samsung triangle mode-2 replay:

- Re-dispatched on Intel with the Adreno's own arena, the shadow ray is blocked
  with the old traversal and with the exact one. With Intel's arena it is
  unblocked with both. The decision follows its inputs.
- The inputs already differ at bounce 0. The camera ray and `isect.t` are
  bit-identical, but `isect.u` is `0x3e39c8be` on the Adreno against
  `0x3e39c8c0` on Intel. That barycentric comes from the closest stage, which is
  now integer-exact on every GPU.
- Confirmed on the devices in the Phase E merge: the Samsung and the Pixel replay
  the re-recorded Linux mode-2 capture within tolerance and pass mode 2 natively
  ("Merged tree on the devices").

Cost on Linux (Intel Mesa 25.0.7, cold cache, median of three runs):

- `intersect_closest`: 0.55 s before, 1.15 s after (71.6 KB to 76.5 KB of WGSL).
- `intersect_shadow`: 0.69 s before, 1.09 s after.
- `init_camera` and `shade_shadow` are unchanged (0.74 to 0.76 s, 0.04 s).
- Every module stays on the full tier.
- Textual inlined size: `intersect_closest` +76-92 KB, `intersect_shadow`
  +76 KB, `intersect_volume_stack` +38 KB, `shade_surface_light` +21 KB.

## Shadow terminator offsets (Phase E3)

`native/shaders/wavefront_terminator.wgsl` implements the object
`shadow_terminator_geometry_offset` exactly as `light/sample.h`
`shadow_ray_offset`/`shadow_ray_smooth_surface_offset` (the shadow-ray origin of
a smooth-shaded triangle is lifted along the interpolated normal by the local
parabolic envelope, the ray then skips its own primitive only when the light is
on the geometric normal's side, and the self-intersection offset applies to
the lifted origin) and the `shadow_terminator_shading_offset` as
`closure/bsdf.h` `shift_cos_in` (after the bump term in BSDF evaluation for
`cos >= 0` and in sampling for every reflection). The cosine uses the existing
`native_disk_sincos` adapter, so the device rejects shading offsets above 1
(multiplier above 2) with "WebGPU shadow terminator shading offset outside [0,
1] not implemented". Both are scene-specialized: `SHADOW_TERMINATOR_GEOMETRY`
when direct lighting, a smooth triangle and an object with a positive geometry
offset are uploaded, `SHADOW_TERMINATOR_SHADING` when an object's multiplier
exceeds 1. Gate: `native/tools/shadow_terminator_gate.py`
(`codebrix_cycles_shadow_terminator_gate`, a `lighting_gate.cpp` variant).

Results (Linux; the merged-tree numbers): `shadow_terminator_gate.py` 10/10 cases against the original
CPU kernels, 0 practical, max 1.53e-5, with 10 native command replays; the offsets change the CPU image
(sun geometry 69 components, sun shading 390, point geometry 21, point shading 384), and zero offsets
leave every deterministic image film unchanged.

**Intel Mesa recompile crash (Linux).** Creating `shade_surface_light` again when `LIGHT_AREA` or
`MOTION_OBJECT` appears later in one device lifetime intermittently crashes the Intel Mesa 25.0.7
backend (`brw_opt_combine_constants` reading a garbage register number; not a WGSL error). Area lamp
sampling now runs its arcsine series as loops (same operations and order; films unchanged), which
took the `LIGHT_AREA` recompile from 3 of 4 cold crashes to 0 of 7; a later recompile after an area
light still crashed 2 of 4, so the GPU ABI test keeps its area light on the CPU (open item 4).

## Merged tree on the devices (Phase E merge, 2026-10-20)

The Phase E changes above (emissive-triangle compile load, Pixel function-scope arrays, host edits and
shadow terminator, validation reference policy, exact traversal) were merged and validated together. The
runtime stayed the unchanged 1.0.290.1433 package; the shipped `libcodebrix_wgpu.so` was verified by hash
on each device before every corpus. Every sampler and image gate was judged with the device-scene flow of
the validation reference policy (glibc reference computed on Linux x64 from the device's own prepared
scene; the device's bionic CPU result is a diagnostic). Lockstep stage checks and state gates (surface,
closest, shadow, camera, volume, volume_boundary, transparent_shadow) still compare against the device's
own CPU. Every device ran the full tier, decided by the known-adapter table, with no tier warning.

**Linux x64.** Clean build; the maintained all-lanes corpus, the host gate, `dotnet test`, the
triangle gate with six images (static instanced emitter included), the closest/shadow
precision fixtures, the shadow-terminator and scene-edit gates and the motion, volume and
feature-combination gates all pass. Every number equals the lanes' own final runs, apart from 1-2
strict differences in atomic-accumulation cases whose own repeat films differ within the run. The
glass_render film is bit-identical with specialization on and off
(`a072683ce5d76309bdd93f81cc18a8c924c7a71260dd44c51c43871f7b0f851a`; the exact traversal changed it
from the earlier `a8e69a34...`).

**Samsung SM-X700 (Adreno 730).**
- Triangle image mode 2 now passes. The Linux mode-2 capture that failed at command 574 replays
  within tolerance (415 readbacks, max 6.41e-7), and the native mode-2 image passes against the glibc
  reference (0 practical, max 1.69e-5). The shadow-ray decision now follows the CPU reference: its
  bounce-0 barycentric is integer-exact (see "Exact traversal and intersection arithmetic").
- Static instanced triangle emitters (image mode 4) fail: 9 practical differences against the glibc
  reference (max 0.0127). The device's own ARM64 CPU result also differs from glibc there (6
  practical). The Linux mode-4 capture diverges at command 438, the queue count after surface
  shading (`shade_surface_light`/`connect_surface_light`/`shade_surface`). Modes 0-3 pass. Mode 5
  was not reached natively (the gate stops at the first failure); its Linux capture replays.
- The triangle sampler still fails on the deforming emitter only (41 practical; ARM64 float3
  arithmetic), as before.
- These pass against the glibc reference:
  - the emitter probe;
  - broad lighting, feature combination, lighting MIS and transparent lighting (0 practical in each);
  - the shadow-terminator gate (10 cases, 195 strict) and its 10 device replays;
  - E3's 10 Linux shadow-terminator captures replayed on the device;
  - glass_render (97 image strict);
  - object and deforming motion images.
- Finite and closed volumes, the volume-boundary state test, camera static/motion and the
  transparent-shadow gate pass against the device CPU.
- The closest/shadow precision fixtures reproduce the device CPU bit for bit (0 bit mismatches, 0
  shadow decision mismatches).
- The managed app (glass, diffuse+sun with the default shadow-terminator offset, textured plane)
  passes against the Linux films: 0 practical, max 1.38e-5.

**HP Android x64 (Intel Xe, Mesa 24.0.8).**
- The emissive-triangle corpus passes completely against the glibc reference. The sampler passes
  (0 practical, 865 strict; the bionic CPU differs from glibc by 32 practical, the earlier failure).
  All six images pass, including the static instanced emitter and the moving/deforming emitter (0
  practical), with six device replays.
- Mode 5 and the sampler probe now compile within the Scudo limit.
- The emitter probe, the shadow-terminator gate and its replays, glass_render, lighting MIS,
  transparent lighting, broad lighting, feature combination, volumes and motion pass.
- Precision fixtures: 0 bit mismatches, 0 shadow decision mismatches.
- The six re-recorded Linux triangle captures replay with the same strict counts as Linux's own replays,
  and the ten shadow-terminator captures replay.
- The managed app passes against the Linux films: 0 practical, max 2.55e-5, no capability warning.

**Pixel 4 XL (Adreno 640, old-hardware gauge; 20 s compile budget, warnings only).** One confirmation
run per corpus.
- The formerly failing E2 corpora pass against the glibc reference with their replays: feature
  combination (0 practical), lighting MIS, the four object/deforming motion images, the
  volume-boundary state test, the camera-inside closed volume, and camera static/motion.
- E3's 10 Linux shadow-terminator captures replay within tolerance.
- Triangle images 0-3 pass, and Linux captures 0-3 and 5 replay. Mode 4 fails exactly as on the
  Samsung (same component, same values), and the mode-4 Linux capture diverges at the same command
  438.
- The triangle sampler gate fails with a garbage GPU error mask: a float value is written into the
  error word, which is an out-of-bounds store. The sampler module holds E1's per-invocation
  `triangle_light_cache`, a module-scope array of structs. Function- and private-scope arrays of
  structs are the Adreno 640 defect class described in "Pixel 4 XL (Adreno 640) function-scope array
  fix". The sampler had not run on the Pixel before.
- Compile-budget warnings (over 20 s; warnings only, no tier change): `intersect_closest` 86-87 s and
  `intersect_shadow` 74-83 s in the motion images, `prepare_surface` 48-50 s, emissive-triangle
  `shade_surface_light` 65 s, `init_camera` 27 s, `shade_volume` 27 s, and `shade_surface` 21-23 s.
- The closest/shadow precision fixtures reproduce the device CPU bit for bit.
- The managed app passes against the Linux films (0 practical, max 1.38e-5) and reports the over-budget
  compiles as capability warnings. App time was 421 s (128 s on the Samsung, 197 s on the HP).

**Gate binaries and failed device creation.** A gate whose WebGPU device cannot be created now exits
with status 1. It prints `<gate> device creation: WebGPU device creation failed: <reason>` instead of
crashing on the failed cast (`require_webgpu_device`, `native/cycles/device.h`). Verified on both
Android devices: the specialization-off refusal, which previously ended the gate with exit 139, now
exits 1 with that message.

## Open device items (2026-10-20, after the Phase E merge)

These items are open. None is masked, and no tolerance was relaxed. Items resolved in Phase E, now
covered in the sections above:
- the Samsung triangle mode-2 decision (exact traversal);
- the HP compile memory (emissive-triangle compile load);
- the specialization-off crashes (refused on Android);
- the Pixel feature-combination, motion and volume-stack failures (function-scope array fix);
- the Android oracle libm differences (validation reference policy);
- static instanced triangle emitters on Linux and the HP (exact traversal).

1. **Deforming triangle emitter on ARM64 (Samsung).** The triangle sampler fails against the glibc
   reference in 41 values (24 samples), all on the deforming emitter. The cause is ARM64 float3
   arithmetic, which the x64 reference does not share. It is to be confirmed against a Linux ARM64
   glibc reference (Raspberry Pi 5, Phase F).
2. **Static instanced triangle emitters on Adreno (Samsung SM-X700, Pixel 4 XL).**
   - Image mode 4 fails with identical values on both GPUs: 9 practical differences against the
     glibc reference. The device's own ARM64 CPU result also differs from glibc there (6
     practical).
   - The Linux x64 mode-4 capture, whose layout selects the oracle-exact triangle arithmetic,
     diverges on both at command 438, the queue count after surface shading.
   - The HP and Linux pass.
   - Related decision: Android ARM64 builds emit `NATIVE_TRIANGLE_ARITHMETIC` 2 (SSE2NEON: SSE
     grouping with `1/x`). The authoritative reference is the x64 oracle (mode 1, `RCPSS` model),
     so the native device images use arithmetic that the reference does not. Whether ARM64 device
     builds should emit mode 1 under the validation reference policy is undecided. The capture
     result shows that the arithmetic mode alone does not explain mode 4.
3. **Pixel 4 XL: triangle sampler gate.** The `emitter_sample_probe` module writes a float into
   the GPU error word, an out-of-bounds store. The emissive-triangle prefill keeps a module-scope
   array of structs (`triangle_light_cache`), the form that the Adreno 640 miscompiles. The fix
   pattern is in "Pixel 4 XL (Adreno 640) function-scope array fix" (one private scalar array or
   parallel scalar arrays). Triangle image mode 5 was not reached natively on the Pixel; its Linux
   capture replays.
4. **Intel Mesa 25.0.7 cold-compile crash** (`brw_opt_combine_constants`, intermittent; Linux).
   - Phase E3's area-light mitigation leaves a later recompile after an area light (object motion)
     crashing 2 of 4 cold runs. The GPU ABI test keeps its area light on the CPU.
   - The exact-traversal emissive-triangle module (`emitter_sample_probe`) crashes about 1 in 10
     cold compiles.
   - Neither occurred in the merged-tree runs. A driver fix or a smaller lamp stage remains open.
5. **Checks not yet on the glibc reference.** These still compare against the device's own CPU
   (bionic):
   - `render_gate.cpp` lockstep stage checks (their CPU input is the device's GPU state);
   - the state gates: surface, closest, shadow, shader_data, glass/microfacet svm and bounce,
     camera, volume, volume_boundary, light_state, transparent_shadow and svm_surface.
   `render_gate.cpp` independent images use a Linux-prepared reference without scene exchange.
   The managed-API scenes have no scene dump, so their Linux films are Linux-prepared.
6. **Remaining function-scope arrays** (Adreno 640 candidates if a Pixel failure shows the same
   signature of cleared low bits or collapsed elements):
   - `array<i32,192>` BVH stacks;
   - three `array<vec3<f32>,3>` locals (normals, shader data, motion triangles);
   - `array<u32,13>` in light-sample packing;
   - the triangle-light cache of item 3.
7. **Pixel 4 XL compile time.** Over the 20 s budget, as warnings: `intersect_closest` and
   `intersect_shadow` with motion take 74-87 s, emissive-triangle and managed-scene
   `shade_surface_light` 64-71 s, and `prepare_surface` with motion 48-50 s. Every run stayed on
   the full tier and inside its wall budget.
