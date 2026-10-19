# CodeBrix.Graphics3D.Cycles

Copyright (c) 2026 Jeremy Ellis and contributors

Native Cycles integration with a new WebGPU backend, targeting full Cycles
parity while using the unchanged binaries shipped by CodeBrix.WebGpu.Native.
Work is in progress; this is not yet a production renderer or NuGet package.

Source provenance and licenses are recorded in
[THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt). The active Cycles build now
uses a filtered snapshot in `third_party/cycles` and independent Apache-2.0
host atomics and dependency-target adapters. The excluded upstream atomic
headers are no longer build inputs. The repository publication checks, fresh
offline Linux build and bounded rendering revalidation have passed; see
[the review evidence](native/evidence/license-compliance-20261018/README.md).
Future RID and package audits remain tracked in [the license work](RELEASE-LICENSE-PLAN.md).
Historical evidence retains the earlier findings; it is not rewritten.

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

The current Linux x64 build starts with `python3 tools/build.py --jobs 6`.
It builds the 18 pinned dependency snapshots in `third_party/deps`, then the
native Cycles host, CPU references, WGSL translation and WebGPU backend. Build
outputs stay in ignored work directories. Python 3, CMake, Make, a C/C++20
toolchain and Clang for AST translation are external build tools. NASM is
optional for JPEG SIMD acceleration; the verified build uses the scalar fallback. The
unchanged `CodeBrix.WebGpu.Native.MitLicenseForever` 1.0.290.1433 package must be
available in the normal NuGet cache or at `CODEBRIX_WEBGPU_PACKAGE`, including
its original archive for integrity verification. Source fetches and sibling
implementation checkouts are not normal build inputs.

Run `python3 tools/licensing/audit.py` to check the proposed Git contents and
notice consistency. Offline build, deep license scanning, binary dependency
inspection and rendering regression checks are additional publication gates;
see `RELEASE-LICENSE-PLAN.md` for their current status.

Intentional future RID binaries, applicable unstripped builds and symbols will
be versioned under `native/binaries/<rid>/` after their artifact reviews. The
Visual Studio/.NET build-output ignore rules remain active elsewhere. Jeremy
performs all commits and pushes.
