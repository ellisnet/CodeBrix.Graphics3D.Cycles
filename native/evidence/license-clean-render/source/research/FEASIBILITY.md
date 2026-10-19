# Cycles WebGPU feasibility

Research performed 2026-10-18/19. **Conditional GO for a limited native prototype; full Cycles feature parity and useful rendering performance remain unproven.** Jeremy subsequently authorized that prototype and permissively licensed dependency vendoring.

Update: the [native scene-to-image gate](../prototype/README.md) now passes on
Linux and all three Android devices using unchanged shipped WebGPU binaries.
Jeremy has authorized continued work toward **full parity**; see the
[parity ledger](../PARITY.md). The kernel results and their numerical failures
below remain valid; the small image test does not supersede them.

The absence of a general Cycles C++ → WGSL compiler is not a fundamental obstacle to starting. This repository now translates a narrow, real upstream kernel subset through Clang's AST and executes it through the native CodeBrix WebGPU library. It also contains a manual static-triangle BVH traversal port. This is evidence for an engineering path, not evidence that all Cycles kernels translate automatically.

## Native architecture

The intended product is native Cycles host code plus a native C++ WebGPU backend. A small C ABI exposes opaque handles, scene operations, rendering, cancellation, and error reporting. A thin .NET interop layer and RID-specific NuGet native assets deliver that implementation. C# is not required for scene management, rendering orchestration, or the WebGPU backend.

GPU code remains GPU code: a build-time translator/manual port produces WGSL, embedded in or distributed with the native library. CodeBrix.WebGpu.Native compiles it for the selected GPU at runtime. Compiling host C++ into a RID binary does not automatically turn its kernel functions into WebGPU shaders. No Slang is used or proposed.

The proof uses the C headers and native library from `CodeBrix.WebGpu.Native.MitLicenseForever` **1.0.290.1433**, API version 2, wgpu-native 29.0.1.1. The original C# Linux harness is auxiliary; the recorded final runs use [native/gpu_probe.cpp](native/gpu_probe.cpp), with no CLR on Linux or Android. A production integration must share a compatible native WebGPU library/device ownership contract with other CodeBrix components.

## Measured evidence

Pinned upstream: [`a456b761034dda42c32eef9f4aae0fa5a5c9f604`](https://github.com/blender/cycles/tree/a456b761034dda42c32eef9f4aae0fa5a5c9f604). Checkout: `~/ClaudeHome/cycles-webgpu-upstream`, unmodified. Native references use Clang 19, C++20, the generic scalar math branch, and disabled floating-point contraction. Branch prediction hints are removed without changing boolean values.

Each device executes:

- **16,384 triangle tests**, including parallel, degenerate, edge, vertex, clipped and behind-ray cases. Reference: original `util/math_intersect.h`.
- **10,496 scalar material-math tests**, covering all 41 `svm_math` opcodes. This is not the full SVM interpreter or BSDF system.
- **16,384 BVH rays** through 256 triangles, 128 leaves, depth 7, with a 192-entry traversal stack. CPU reference includes original `kernel/bvh/traversal.h` and extracted original aligned-node/direction helpers. GPU traversal is manually adapted; triangle intersection is generated. The fixture builder is not Cycles' production BVH builder.

The translator emits 23 functions, including helper functions; this does not mean each emitted function has independent runtime coverage.

| Native execution target | Hardware | Strict result |
| --- | --- | --- |
| Linux x64 | Intel UHD Graphics ADL-S GT1; Mesa 25.0.7 | 22 scalar discrepancies; no classification or primitive mismatch |
| Android ARM64, **API 33** | Pixel 4 XL, Adreno 640 | All three suites pass |
| Android ARM64, API 36 | Samsung SM-X700, Adreno 730 | All three suites pass |
| Android x64, **API 33** | HP laptop, Intel Xe TGL GT2; Mesa 24.0.8 | Same 22 discrepancies as Linux; no classification or primitive mismatch |

All devices used Vulkan hardware adapters, zero optional features, and baseline enabled limits including 8 storage bindings and 128 MiB per storage binding. Software adapters were rejected. These tiny scenes do not establish practical memory capacity or throughput. Android binaries were compiled against NDK API 33 and executed through `adb shell`; APK loading and application sandbox behavior are not yet established by these runs.

Tolerance remains `abs(error) <= 5e-5 + 2e-4 * abs(reference)`, with exact triangle classification and BVH primitive IDs. Intel failures remain failures in the saved reports. Twenty are inverse-sine results; the other two are barycentric coordinates in nearly grazing intersections. A double-precision diagnostic finds absolute ray/normal cosines about 0.000398 and 0.001685 for those rays. The CPU scalar reference is not an exact mathematical oracle. No tolerance was relaxed to obtain a pass.

[WGSL floating-point rules](https://www.w3.org/TR/WGSL/#floating-point-evaluation) allow accuracy and evaluation behavior that differ from host C++ math. Runtime nonfinite values also need explicit treatment. These observations motivate a numerics audit; they do not establish a driver defect or acceptable final image quality.

Evidence: [native-kernels](evidence/native-kernels/), including device records, validation, source provenance, generated WGSL and fixture/output hashes. Early managed-host observations are retained separately in `evidence/linux-initial`.

## Remaining engineering gates

| Area | Concrete obstacle and next proof |
| --- | --- |
| Host integration | Implement native device memory/queue interfaces and retain Cycles' scene preparation. `src/device/device.h`, `queue.h`, and `scene/devicescene.h` identify the integration surface. Full scene libraries were not built during the initial kernel research. |
| Storage/addressing | GPU path state uses pointer tables and SoA macros in `kernel/integrator/state.h`. Replace device addresses with explicit buffer offsets/indices. Pack tables into a bounded number of storage buffers, with validated sizes and layouts. Avoid assuming C++ `float3` or bool layout matches WGSL. |
| Closures | `kernel/closure/alloc.h` returns typed/void pointers into closure storage and casts between representations. This needs typed records or an explicit arena representation. Current translator does not solve it. |
| Textures | `kernel/device/gpu/image.h` uses runtime handles and caching. Need a bounded texture array/atlas or buffer-backed sampling design; ordinary bindless handles are not portable baseline WebGPU. |
| Traversal | Static software BVH works in small fixtures. Instances, motion, curves, volumes, visibility/self-intersection rules, deep trees and actual Cycles packing need separate proofs. Do not depend on hardware ray queries for baseline support. |
| Scheduling and film | Port a bounded wavefront path and queue transitions. `kernel/film/write.h` uses 64-bit offsets and floating-point atomic accumulation; baseline WGSL needs bounded indexing and a tested accumulation strategy. Neither is proven by the current single-dispatch cases. |
| Numerical behavior | Audit transcendental functions, contraction, zero directions, very large/small geometry, NaN/infinity assumptions and C library adapters. The research `fmod`/`pow` adapters are validated only over the fixtures, not their entire C domains. |
| Product quality | Render a reference scene, compare images, measure shader compilation, memory, GPU time and end-to-end time, then scale scene complexity. No image-quality or performance claim follows from these microtests. |

The translator is intentionally narrow. Unsupported types, AST constructs and casts fail rather than being guessed. It does not support general C++ templates, dynamic allocation, arbitrary pointer aliasing, casts between closure layouts, or arbitrary storage pointers. A realistic route combines build-time AST lowering of suitable math with explicit adaptation of storage, resource access and selected complex kernels.

## Platform and delivery implications

Linux x64 is the first integration target. Android API 33 is the minimum, with both architectures tested at that API for the native kernel probes. Windows x64/ARM64, Linux ARM64/RISC-V64 and macOS builds have not been tested here. RID availability in the WebGPU package does not prove that Cycles, its dependencies, a usable GPU driver, and the consuming .NET runtime are available for every target. RISC-V64 especially needs its own native toolchain and dependency proof.

Existing Metal remains an option for supported Macs. Intel macOS must have an explicit tested backend policy rather than inheriting the Apple Silicon assumption. No Metal investigation was used to substitute for the Linux WebGPU decision.

Vendoring must pin versions and preserve third-party notices. Cycles is predominantly Apache-2.0, but individual files include other permissive notices (for example BSD-3-Clause in `kernel/closure/bsdf_diffuse.h`). Dependency selection must account for optional image/video plugins and their link dependencies. A distribution's general-purpose OpenImageIO binary is not an automatically acceptable shipping artifact.

## Reproduce

Prerequisites: pinned clean upstream checkout, Clang C++20, Python 3, the pinned native NuGet extracted in the normal user cache; Android additionally needs NDK 29.0.14206865 and ADB at the paths in `native_runner.py`.

```sh
python3 research/run.py prepare
python3 research/run.py linux
python3 research/run.py android --serial 98271FFBA0048D
python3 research/run.py android --serial R52W207XQ1V
python3 research/run.py android --serial 192.168.86.225:5555
python3 research/numerics.py
python3 research/archive.py
```

Linux and Android x64 return exit code 1 for the recorded numerical mismatches. Inspect `validation.json` to distinguish numerical validation failures from runtime/compilation failures. Hardware access may require execution outside the agent sandbox. Work artifacts are ignored by Git; source and selected evidence remain reviewable and uncommitted.
