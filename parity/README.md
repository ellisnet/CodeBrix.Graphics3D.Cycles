# Material and real-asset parity work

The target remains full Cycles parity. These are measured intermediate gates,
not a complete renderer. All GPU tests use the unchanged binaries from
`CodeBrix.WebGpu.Native.MitLicenseForever` 1.0.290.1433. No Slang is involved.
The [parity ledger](../docs/PARITY.md) lists the remaining areas.
The [native library gate](../native/README.md) now reuses these kernels with
bounded batches, GPU film accumulation and persistent native resources.

## Original Cycles materials

`native/material_host.cpp` creates seven actual Cycles shader graphs, runs
`Scene::device_update`, and exports the resulting SVM word stream unchanged.
The pinned Cycles revision uses typed, variable-length instructions. It is
not the old fixed `uint4` instruction format. Native static assertions check
the layouts consumed by the WGSL decoder.

`native/material_reference.cpp` executes the original Cycles SVM interpreter
and original BSDF evaluation/sampling functions using the scene's CPU kernel
globals. This is a stronger material reference than the initial Lambert image
fixture, which did not execute SVM.

The WGSL subset implements shader jumps, normal geometry output, constant
closure weight and a single diffuse or translucent closure. Diffuse roughness
selects Lambert or improved Oren–Nayar, including its multiscattering terms.
It compares 7,168 records: 1,024 per material, with varied normals, incident
angles, independent outgoing directions, random samples and endpoint samples.
Fields include closure allocation, weight, sample weight, normal, type,
sampled direction, evaluation, probability density and reflection/transmission
labels. Tests include black closure suppression and the roughness cutoff.

Current results, after a shader-side bounded-angle sin/cos implementation:

| Device | Interior scalar failures | Horizon scalar failures | Type/count/label failures | Decoder rejection cases |
| --- | ---: | ---: | ---: | --- |
| Linux x64, Intel UHD | 0 | 522 | 0 | 12/12 pass |
| HP Android x64, API 33, Intel Xe | 0 | 522 | 0 | 12/12 pass |
| Pixel Android ARM64, API 33, Adreno 640 | 0 | 443 | 0 | 12/12 pass |
| Samsung Android ARM64, API 36, Adreno 730 | 0 | 443 | 0 | 12/12 pass |

**The strict material gate fails on every tested device.** The tolerance stays
`5e-5 + 2e-4 * abs(reference)`, with exact counts/types/labels. All remaining
failures are among 420 records whose sampling coordinates lie exactly on a
unit-square boundary. Concentric disk sampling turns last-bit radius error
into a small, nonzero square root near the hemisphere horizon; CPU and GPU
results disagree in sampled direction and near-zero PDF/evaluation. Maximum
component error is about 0.000237 on Intel and 0.000335 on Adreno. Endpoint
samples have not been removed, nor has the tolerance been widened.

The earlier builtin-trig results are retained in
`evidence/material-builtins`. With the expanded input set, they had 21 interior
scalar failures on Intel as well as horizon failures. The bounded-angle
polynomials remove those interior failures without changing WebGPU. Current
results and source snapshots are in `evidence/material-closures`.

The rejection fixture tests unknown nodes/closures, out-of-range and cyclic
jumps, invalid stack offsets, a second closure, overflowing root IDs and
truncated payloads. Malformed input is tested only on our bounded decoder;
it is not passed into the upstream CPU interpreter. Passing these cases does
not establish complete validation of arbitrary SVM streams. Closure mixtures,
texture nodes, Principled/microfacet models and most other SVM features remain
unimplemented and must not silently fall back to a default material.

After building the native host as described in `prototype/README.md`:

```sh
python3 parity/tools/material_gate.py prepare
python3 research/run.py linux --materials
python3 research/run.py android --materials --serial 98271FFBA0048D
```

The last two commands currently exit 1 because of the documented numerical
failures. Detailed reports are saved despite that exit status.

## Models supplied by Jeremy

[ASSETS.md](ASSETS.md) records the 706-model corpus and test progression.
`GltfImport` uses the actual `CodeBrix.Graphics3D.Gltf2.MitLicenseForever`
1.0.290.996 package in strict validation mode. It is a test input adapter.
Scene preparation, BVH construction, GPU orchestration and rendering remain
native C++. The image driver executes without a CLR.

The adapter flattens rigid transforms/instances and preserves winding. This
gate rejects skins, animations, morph targets and non-triangle primitives.
It writes world-space triangle positions plus a metadata/provenance report.
The native host rotates Y-up to Z-up, uniformly fits the model to four units,
grounds it, and adds a two-triangle floor. The current interchange accepts up
to 500,000 triangles and is a test format, not a public asset API.

All appearance is explicitly replaced with neutral clay for this geometry
gate: no imported textures, alpha, vertex normals or material graphs are
evaluated. The house's blended materials are therefore opaque in this test.
The full glTF-to-Cycles material conversion remains future work.

At 256×192 with eight samples, comparing 196,608 linear RGBA components:

| Model | Imported triangles | Linux Intel | Pixel API 33 |
| --- | ---: | --- | --- |
| ForestVillage bench | 784 | Pass | Pass |
| PolyHaven lime | 8,100 | Fail: one RGB pixel | Pass |
| ForestVillage ceramic home | 40,553 | Fail: one RGB pixel | Pass |

Every run also passes the native ABI checks, repeatability, rejected scene
updates, scene replacement and upload ownership checks. The largest packed
scene is 3,888,672 bytes. The actually loaded WebGPU library is identified and
hashed against the package on Linux and on the Android device. The Pixel
runs an API-33 native executable under `adb shell`; an Android application
sandbox and Android-native Cycles scene preparation are not yet tested.

The two Intel image failures are isolated in `pixel-*/diagnostic.json`:

* Lime pixel (105,62), sample 7: CPU primitive 2749 versus GPU 2833.
* House pixel (100,101), sample 5: CPU floor primitive 1 versus GPU 8305.

The camera ray directions differ at floating-point rounding precision. In a
second diagnostic dispatch using the CPU's exact directions, the GPU selects
the CPU primitive in every sample of both pixels and closely matches shading.
This isolates the observed discrepancies to camera-ray sensitivity near
triangle boundaries. The separate dispatch does not turn the original image
failures into passes, and does not establish general intersection parity.
CPU instrumentation preserves all three original reference images byte for
byte. Adding imported input also preserves the original 38-triangle fixture
scene byte for byte.

Evidence, preview images, exact package audit, shader and source snapshots are
saved under `evidence/real-assets`. Original model resources remain in Oasys;
they have not been copied into this repository. Native wall-clock timings in
the reports include allocation, dispatch and readback and are not production
performance benchmarks.

```sh
dotnet restore parity/GltfImport
python3 parity/tools/asset_gate.py prepare --name bench --asset \
  /home/jeremy/GitHome/Oasys/Assets/ForestCommunity/Domes/ForestVillage/CommunityProps/bench_01.gltf
python3 research/run.py linux --asset-name bench
python3 research/run.py android --asset-name bench --serial 98271FFBA0048D
# After preparing lime in the same way:
python3 research/run.py linux --asset-name lime --trace-pixel 15977
```

## Multi-bounce integration against the original integrator

The next integration gate now connects Cycles-compiled materials to WGSL path
tracing. `native/path_host.cpp` prepares a 44-triangle scene with Lambert,
Oren–Nayar and diffuse-transmission materials and a constant world. The reference
in `native/path_reference.cpp` calls the original Cycles
`integrator_init_from_camera` and `integrator_megakernel`, including the original
SVM interpreter, BSDFs, path state and combined-film accumulation. A second
execution instruments the original kernels and must produce an exactly equal
raw film record for every path. The reference fails if the fixture does not
exercise primary hits, indirect hits and the maximum bounce limit.

The experiment runs 110,592 paths: 96×72 pixels, 16 samples and up to four
surface bounces. There are 21,709 second-surface hits and 6,052 paths that reach
four bounces. WGSL uses the actual compiled shader words and material IDs,
original triangle barycentric position reconstruction, self-intersection
correction, closure sampling and throughput updates. All intersections,
material evaluations and bounce decisions run on the GPU.

In this frozen baseline, camera rays and per-bounce random samples are prepared by the
original native Cycles camera and sampler and uploaded. Sharing those inputs
isolates path/material differences from camera or sampler differences. This is
an intentional intermediate stage. The subsequent gate below moves both to WGSL.
The scene has no light objects or background importance map. It uses forward
path sampling of a constant world, with direct contributions enabled, no
next-event estimation or MIS, and Russian roulette disabled by the configured
minimum bounce count. Imported glTF appearance is not part of this gate.

| Device | Image comparison | Path color / triangle sequence / bounce count | Strict intersection float failures |
| --- | --- | --- | ---: |
| Linux x64, Intel UHD | Pass, max error 8.89e-6 | All pass | 3 |
| HP Android x64, API 33 | Pass, max error 8.89e-6 | All pass | 3 |
| Pixel Android ARM64, API 33 | Pass, max error 2.39e-6 | All pass | 3 |
| Samsung Android ARM64, API 36 | Pass, max error 2.39e-6 | All pass | 3 |

Tolerance is unchanged at `5e-5 + 2e-4 * abs(reference)`; path triangle IDs and
bounce counts must match exactly. Every pixel and every per-path color passes.
No SVM decoding errors occurred. **The strict overall gate still fails** because
three later-bounce intersection records exceed the floating-point tolerance.
Their exact distances/barycentrics are retained in each `validation.json`; they
must be investigated rather than hidden by the passing image. The earlier
material horizon failures and real-asset camera-ray failures remain open too.

This uses the native C++ dispatch harness and baseline WebGPU storage buffers,
with no optional features. The harness now records `dladdr`'s actual loaded
library path and checks its bytes/hash against the pinned package, including
the deployed Android library. There are still no modifications to WebGPU.
Evidence and source snapshots are in `evidence/multi-bounce`; PNG previews flip
Cycles' lower-left film origin for display, while binary comparisons use raw
linear film values. Android remains an `adb shell` test of a Linux-prepared
snapshot. A production native backend API, a registered Cycles device/queue,
and original session scheduling are still required.

```sh
python3 prototype/tools/build_host.py
python3 parity/tools/path_gate.py prepare
python3 research/run.py linux --paths
python3 research/run.py android --paths --serial 98271FFBA0048D
python3 parity/tools/archive.py paths
```

The GPU commands currently exit 1 for the three strict intersection failures,
even though `imagePassed` and `pathStatePassed` are true. Archiving expects the
Linux run and all three Android device runs to have completed.

## GPU camera and sampling

The next checkpoint removes the native per-path camera/sampler input from the
render dispatch. WGSL computes pixel hashes, tabulated Sobol sample indices,
pixel filtering, static perspective pinhole rays, clipping and per-bounce
random samples. It consumes the camera matrices, configuration, pixel-filter
table and sample-pattern table exported from the prepared Cycles scene.
Uploading these scene tables is normal host preparation; no CPU-generated
per-ray directions or per-bounce random streams are uploaded by this gate.
The renderer still uses the original compiled SVM words and static BVH2 data.

Three independent dispatches check the sampler, camera and combined renderer:

- 16,384 sampler queries vary pixel coordinates, seeds, dimensions, sample
  indices (including table wraps and large integer boundaries), and scrambling
  distance values 0, 0.125, 0.75 and 1. The original Cycles 1D/2D/3D/4D tabulated
  functions are checked for their shared-component relationship; GPU output
  compares against the original 4D function. The table has 256 patterns of
  256 samples. Other sequence sizes and other sampling patterns remain untested.
- All 110,592 fixture paths compare WGSL origin/direction/clipping values with
  `integrator_init_from_camera`. The native values are reference files only.
- The four-bounce render compares with the same original Cycles megakernel and
  film as the frozen baseline. Reference image and per-path results are unchanged
  byte-for-byte. The combined dispatch computes camera and sampler values itself;
  it does not read back and re-upload the camera probe's output.

| Device | Camera rays / fixture random samples | Sampler strict float failures | Image / path colors / triangle sequences / bounce counts | Strict intersection records |
| --- | --- | ---: | --- | ---: |
| Linux x64, Intel UHD | Pass / bit-exact | 312 | All pass; image max error 8.89e-6 | 9 |
| HP Android x64, API 33 | Pass / bit-exact | 312 | All pass; image max error 8.89e-6 | 9 |
| Pixel Android ARM64, API 33 | Pass / bit-exact | 0 | All pass; image max error 2.39e-6 | 9 |
| Samsung Android ARM64, API 36 | Pass / bit-exact | 388 | All pass; image max error 2.39e-6 | 9 |

Every tested sampler hash and table index matches exactly on every device.
The Pixel's entire sampler output is bit-exact. The broader sampler stress
test uses `1e-7 + 1e-7 * abs(reference)`; the maximum float error elsewhere is
1.1921e-7. Intel has 2,470 differing float bit patterns, all consistent with
fusing the rotation multiply and add. Samsung has 3,438 differing bit patterns;
that same rounding prediction explains 876 of them. The remaining Samsung
differences are not yet diagnosed. These diagnostics do not replace the original
native oracle or affect pass/fail decisions.

Camera, image and intersection tolerances remain `5e-5 + 2e-4 * abs(reference)`.
The additional strict intersection discrepancies appear after introducing
GPU-generated camera rays; their full cause still needs investigation.
**The overall strict gate fails on all four devices.** No tolerance was widened,
and the earlier material/real-asset failures remain open. The matching image
does not establish full parity or guarantee identical behavior on larger scenes.

Evidence, exact deployed WebGPU hashes, shader/source snapshots and previews
are in `evidence/gpu-camera-sampling`. The original `evidence/multi-bounce`
checkpoint is preserved. A Linux regression of the shared-input path after
refactoring reproduces its previous GPU results byte-for-byte. The CPU reference
now allocates two path-state slots as the original CPU worker does, while
explicitly rejecting shadow catchers; this removed compiler bounds warnings
without changing any reference outputs.

```sh
python3 parity/tools/camera_gate.py prepare
python3 research/run.py linux --camera
python3 research/run.py android --camera --serial 98271FFBA0048D
python3 research/run.py android --camera --serial 192.168.86.225:5555
python3 research/run.py android --camera --serial R52W207XQ1V
python3 parity/tools/archive.py camera
```

GPU commands currently exit 1 for the recorded strict numerical failures.
Android remains a native `adb shell` test of Linux-prepared scene data, with
API 33 binaries. No WebGPU changes or optional features are required.
This camera port does not yet implement ray differentials, depth of field,
motion blur, stereo, orthographic/panoramic/custom cameras, or pixel-jitter
overrides. Unsupported configurations are rejected by the native export guard.
The tabulated Sobol implementation is still a manual port, not a general
C++-to-WGSL compiler.

## Finite lights, shadows and MIS

The next checkpoint adds two actual Cycles spherical point-light objects,
with radii 0.45 and 0.6, distinct emission colors and constant emission graphs.
It exports Cycles' prepared light parameters and CDF. WGSL selects a light,
samples its sphere, evaluates the current BSDF and the next-event-estimation
(NEE) MIS weight, creates shadow rays, and adds unoccluded contributions.
Paths that intersect lights also evaluate emission and the forward MIS weight.
The original Cycles megakernel remains the reference, including its original
shadow and forward-light kernels. Instrumented execution must match its film
exactly for every path.

The 110,592-path fixture exercises 88,707 shadow rays, including 14,244 blocked
rays, and 3,437 forward light hits, of which 1,926 occur after a surface bounce.
The native reference refuses a fixture that lacks nonzero direct-light energy,
blocked and visible shadows, or indirect forward light hits. Per-bounce records
compare shadow-light selection, visibility, throughput and distance; per-path
records also compare forward-light contribution and hit count.

| Device | Image / path colors / geometry sequence / bounce count | Lighting-path checks | Strict isolated light sampler failures |
| --- | --- | --- | --- |
| Linux x64, Intel UHD | All pass; image max error 1.31e-5 | All pass | 31 float components, 1 sphere hit/miss |
| HP Android x64, API 33 | All pass; image max error 1.31e-5 | All pass | 31 float components, 1 sphere hit/miss |
| Pixel Android ARM64, API 33 | All pass; image max error 4.36e-6 | All pass | 7 float components, 0 hit/miss |
| Samsung Android ARM64, API 36 | All pass; image max error 4.36e-6 | All pass | 7 float components, 0 hit/miss |

The independent 4,096-case light probe includes origins inside and outside
spheres, reflective/transmissive sampling, both emitters, exact sampling
endpoints and varying directions. Every light selection agrees. Four failing
float components on each device are interior samples with differing sphere
normals; other failures occur at sampling boundaries. On Intel, case 76 has
an exact-boundary sample whose ray intersects the sphere in the CPU reference
and misses in WGSL. These failures remain in the reports. The same nine path
intersection discrepancies and prior sampler/material failures remain open.
**The overall strict gate still fails on every device.** Lighting uses the
unchanged `5e-5 + 2e-4 * abs(reference)` tolerance, with exact discrete decisions.

This gate exposed a real implementation constraint on the Pixel's older Adreno
640 driver: putting both surface and shadow traversal in one shader produced an
all-zero output without a reported WebGPU validation/runtime error. Reducing
the workgroup from 64 to 32 did not resolve it. Splitting shadow traversal into
a second compute pass did resolve it, and the same implementation now passes
the image and lighting-path comparisons on all four devices. The precise driver
cause is not established; resource pressure or compilation are hypotheses.

The native C++ harness creates explicit buffer layouts and two compute pipelines.
The first pass writes path colors and shadow rays; the second traverses those
shadow rays and accumulates their contributions. There is no CPU shadow work or
per-path readback between passes. Both use baseline storage buffers with no
optional features. The exact shipped WebGPU binaries remain unchanged and their
loaded/deployed identity is audited. The failed Pixel shader, native harness,
validation reports and reduced-workgroup evidence are preserved alongside the
working implementation in `evidence/finite-light-mis`.

```sh
python3 parity/tools/light_gate.py prepare
python3 research/run.py linux --lighting
python3 research/run.py android --lighting --serial 98271FFBA0048D
python3 research/run.py android --lighting --serial 192.168.86.225:5555
python3 research/run.py android --lighting --serial R52W207XQ1V
python3 parity/tools/archive.py lighting
```

Commands exit 1 for the retained strict numerical failures. Android is still
an `adb shell` native test of a Linux-prepared scene. This is not yet a complete
Cycles device implementation or a shipping library API. Only constant finite
spherical point lights, flat opaque shadow geometry and the previously tested
diffuse/translucent closures are covered. Area/sun/spot lights, emissive mesh
sampling, light trees, texture-driven emission, transparent shadows, linking,
Russian roulette and general film/output behavior remain to be implemented.
The source refactor preserves the earlier camera-only image and path results
on Linux and Pixel; frozen evidence remains separate.

Next: connect the growing kernels to native backend queues; expand light types,
textures, closure mixtures and Principled/microfacet materials; then connect
the queues to Cycles' device and session machinery. Track exact
functional coverage and numerical failures separately, expand the real-model
corpus as appearance support lands, and compare against Metal when that test
hardware is available. Full Cycles functionality remains the active goal.
