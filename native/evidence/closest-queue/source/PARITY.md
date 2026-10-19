# Cycles parity ledger

Full Cycles parity is the intended outcome, explicitly requested by Jeremy.
The reference is the pinned upstream revision
`a456b761034dda42c32eef9f4aae0fa5a5c9f604`; each new behavior needs comparison
with original Cycles code and, ultimately, complete reference renders.

Hard constraints: unchanged shipped CodeBrix.WebGpu.Native binaries, native C++
host/backend, no Slang, Android API 33 minimum, the requested desktop/mobile RIDs,
permissively licensed redistributable dependencies, and no Git commits/pushes.
A need to modify WebGPU itself is a decision point, not an implicit workaround.

| Area | Status / evidence required |
| --- | --- |
| Native WebGPU runtime | Demonstrated on Linux x64 and Android ARM64/x64; exact package/repository binary identity audited |
| Cycles scene preparation | Demonstrated with real `ccl::Scene`, static BVH2 and prepared geometry on Linux |
| Native image output | Passed a 38-triangle image gate on four devices; deliberately restricted shading |
| Kernel math | Narrow AST lowering and original CPU references exist; Intel numerical discrepancies remain open |
| Actual materials | Actual Cycles SVM/BSDF comparison implemented for Lambert, Oren–Nayar and translucent closures on four devices. Interior samples pass; strict horizon tests still fail. See `parity/README.md` |
| Path integration | Limited four-bounce snapshot render with finite spherical lights, NEE and MIS passes on four devices; earlier strict intersection failures remain. The registered closest stage now makes CPU-compared RR/AO termination decisions and schedules background/surface/raytrace queues on Linux. Full wavefront shading, roulette throughput handling and general film behavior pending |
| Camera and sampler | Static perspective pinhole, original pixel filter and tabulated Sobol pass the earlier 110,592-path camera gate on four devices. The actual Cycles device now also initializes compact ray differentials and native path state on Linux: 22,972 initialized paths, 65 retries, sample reservation and adaptive skipping pass the existing camera tolerance. Its stricter float check still has 7,668 component failures. Broader sampler stress failures and other camera/sampler modes remain open |
| Device integration | Registered `ccl::WebGPUDevice`/`DeviceQueue` now executes original scheduler camera → closest intersection → shader queues and sorting on Linux. Twelve control kernels cover 123 state fields; the closest gate checks 36,226 paths and retains 370 strict float failures. Shading/film dispatches and full session rendering remain unfinished. Separate snapshot library image/lifecycle gate still passes on four devices. See `native/DEVICE.md` |
| Geometry | Registered closest stage now traverses native BVH2 static triangles and instances with visibility/self-hit rules; 3 instances and all 5 fixture objects verified. Motion, curves, points, volumes and displacement pending. Earlier three Gltf2 models up to 40,553 triangles use the separate snapshot renderer and flattened rigid transforms |
| Real-asset images | Bench passes on Linux/Pixel API 33; lime and house pass on Pixel with one strict pixel failure each on Linux, isolated to camera-ray rounding near edges. Clay appearance only |
| Material coverage | Full SVM nodes, procedural/image textures, bump, closure mixtures, Principled/microfacet, glass, hair, subsurface and volumes pending |
| Lighting | Two constant finite spherical lights with CDF selection, NEE, opaque shadows and both MIS strategies tested on four devices. Strict isolated sphere-sampling/intersection failures remain. Other light types, emissive mesh sampling, light tree, environment maps, linking and general visibility pending |
| Output/features | Camera-stage atomic sample counts and adaptive skip flags tested on Linux. General film passes, adaptive convergence, denoising integration, color-management behavior, compositing-related outputs and interoperability pending |
| Memory and performance | Bounded path scratch/resource reuse demonstrated in the snapshot renderer. Registered device allocator growth/order and queue compaction now pass correctness gates; its arena is bounded by one storage-binding limit. Large scenes, stack/register pressure, compile time and practical throughput remain pending |
| Platforms/delivery | Full Linux host only; other host builds, remaining GPU backends/RIDs, .NET package loading and Android application sandbox pending |
| Full parity decision | Open. A small matching render cannot establish this. Unsupported features must be reported explicitly, never silently approximated |

Material-driven, multi-bounce image comparisons now exist for a limited scene.
WGSL camera/sampling now feeds those paths without native per-path inputs.
Finite-light NEE/MIS now uses a second dispatch for shadow traversal, with
all per-path work on the GPU. The shared native library now adds bounded batches
and GPU film accumulation without changing the images. Actual Cycles device
registration, scheduler controls, native camera and closest intersection now exist.
Next are shading/film dispatches through that scheduler and broader light/material coverage, alongside
investigation of numerical differences. Each gate keeps
prior evidence reproducible. The full renderer remains the objective; passing
a limited image gate is permission to continue, not a declaration of parity.
