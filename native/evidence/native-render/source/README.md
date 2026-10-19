# CodeBrix.Graphics3D.Cycles

Native Cycles integration with a new WebGPU backend, targeting full Cycles
parity while using the unchanged binaries shipped by CodeBrix.WebGpu.Native.
Work is in progress; this is not yet a production renderer or NuGet package.

Source provenance and licenses are recorded in
[THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt). The current Cycles host
includes upstream atomic headers carrying GPL-2.0-or-later notices; this is
an unresolved Apache release blocker that Jeremy is handling. Rendering
development continues, and the current native host artifacts are not cleared
for distribution under the intended license.

The [WGSL release license work](RELEASE-LICENSE-PLAN.md) tracks the requirements
from Jeremy's October 18 license plan, including source, dependencies, generated
shaders, history review, and package contents.

- [Parity ledger and remaining work](PARITY.md)
- [Successful native scene-to-image prototype](prototype/README.md)
- [Original material tests and real-model renders](parity/README.md)
- [Multi-bounce WGSL render versus the original Cycles integrator](parity/README.md#multi-bounce-integration-against-the-original-integrator)
- [GPU camera and sampling checkpoint](parity/README.md#gpu-camera-and-sampling)
- [Finite lights, shadows, MIS and the Pixel dispatch fix](parity/README.md#finite-lights-shadows-and-mis)
- [Reusable native backend, bounded batches and GPU film accumulation](native/README.md)
- [Registered Cycles device and wavefront scheduler kernels](native/DEVICE.md)
- [Camera initialization through Cycles' original GPU scheduler](native/DEVICE.md#camera-entry-through-the-original-scheduler)
- [Native BVH intersection, instances and shader-queue transitions](native/DEVICE.md#closest-intersection-and-shader-queues)
- [Native background shading and Cycles film passes](native/DEVICE.md#background-entry-and-native-film)
- [Cycles-compiled SVM world shaders executed in WGSL](native/DEVICE.md#native-svm-world-shaders)
- [Native surface shader data and Android GPU replay](native/DEVICE.md#native-surface-shader-data-preparation)
- [Native surface SVM closure evaluation](native/DEVICE.md#native-surface-svm-evaluation)
- [Complete native scheduler render and four-GPU replay](native/DEVICE.md#complete-native-scheduler-render)
- [Kernel research and feasibility findings](research/FEASIBILITY.md)

The prototype renders an actual Cycles-prepared scene on Linux x64 and Android
ARM64/x64, including API 33, with no CLR or Slang in the rendering path. Its
current shading is deliberately restricted; full materials and integration
remain under development. Changes are left uncommitted for Jeremy's review.

The bounded native-scheduler proof now includes a repeated mixed-material render
and a ForestCommunity clay bench, independently compared against original CPU
kernels. Complete captured GPU command sequences pass on Linux and all three
Android devices through the unchanged shipped runtime. Android native Cycles
hosts and full rendering parity remain pending. The comprehensive continuation
plan is `/home/jeremy/ClaudeHome/PLAN_Cycles_Targetting_WGSL_WebGPU.md`.
