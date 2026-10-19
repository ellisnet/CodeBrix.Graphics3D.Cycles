# Cycles parity ledger

Full Cycles parity is the intended outcome, explicitly requested by Jeremy.
The reference is the pinned upstream revision
`a456b761034dda42c32eef9f4aae0fa5a5c9f604`; each new behavior needs comparison
with original Cycles code and, ultimately, complete reference renders.

Hard constraints: unchanged shipped CodeBrix.WebGpu.Native binaries, native C++
host/backend, no Slang, Android API 33 minimum, the requested desktop/mobile RIDs,
permissively licensed redistributable dependencies, and no Git commits/pushes.
A need to modify WebGPU itself is a decision point, not an implicit workaround.

Release licensing: active builds use the filtered pinned Cycles snapshot and
independent Apache-2.0 host atomics. Multithreaded atomic/film tests pass; both
native proof scenes retain bitwise-identical CPU and GPU films after migration.
Dependency/source-containment and shipping-package checks remain distinct gates.
See [the license tracker](RELEASE-LICENSE-PLAN.md) and generated notices.

| Area | Status / evidence required |
| --- | --- |
| Native WebGPU runtime | Demonstrated on Linux x64 and Android ARM64/x64; exact package/repository binary identity audited |
| Cycles scene preparation | Demonstrated with real `ccl::Scene`, static BVH2 and prepared geometry on Linux |
| Native image output | Passed a 38-triangle image gate on four devices; deliberately restricted shading |
| Kernel math | Narrow AST lowering and original CPU references exist. Native SVM gate covers all 41 scalar and 30 vector math operations on Linux; inverse-trigonometric adapters fix a measured arcsine tolerance failure. Strict numerical discrepancies remain open |
| Actual materials | Actual Cycles SVM/BSDF comparison implemented for Lambert, Oren–Nayar and translucent closures on four devices. Interior samples pass; strict horizon tests still fail. See `parity/README.md` |
| Path integration | Limited four-bounce snapshot render with finite spherical lights, NEE and MIS passes on four devices; earlier strict intersection failures remain. The registered closest stage now makes CPU-compared RR/AO termination decisions and schedules background/surface/raytrace queues on Linux. The selected registered surface entry now handles mixture sampling, roulette throughput and emission in a complete native-scheduler loop. General lighting/shadows/film remain pending |
| Camera and sampler | Static perspective pinhole, original pixel filter and tabulated Sobol pass the earlier 110,592-path camera gate on four devices. The actual Cycles device now also initializes compact ray differentials and native path state on Linux: 22,972 initialized paths, 65 retries, sample reservation and adaptive skipping pass the existing camera tolerance. Its stricter float check still has 7,668 component failures. Broader sampler stress failures and other camera/sampler modes remain open |
| Device integration | Registered `ccl::WebGPUDevice`/`DeviceQueue` executes original scheduler camera → closest intersection → constant or selected SVM background → native film on Linux. Twelve control kernels cover 123 state fields; closest checks 36,226 paths, background/film 55,681, and SVM worlds 102,817. Strict float differences remain. Selected surface shading and repeated complete native-scheduler images now pass independent CPU comparisons. Full native command replay passes on four GPUs; direct light/shadow shading and full Session lifecycle remain unfinished. Separate snapshot library image/lifecycle gate still passes on four devices. See `native/DEVICE.md` |
| Geometry | Registered closest stage now traverses native BVH2 static triangles and instances with visibility/self-hit rules; 3 instances and all 5 fixture objects verified. Motion, curves, points, volumes and displacement pending. Earlier three Gltf2 models up to 40,553 triangles use the separate snapshot renderer and flattened rigid transforms |
| Surface shader data | Native static-triangle setup matches original CPU output for 12,066 hits across 9 objects: vertex/corner packed normals, signed offsets, instance/applied/mirrored transforms, backfaces and compact/UV differentials. A 1,314-hit GPU replay passes on Linux, Pixel API 33, Samsung API 36 and HP Android x64 API 33 with unchanged runtimes. Strict float differences remain; selected surface-bounce integration now passes a separate original-CPU and complete-render gate |
| Surface SVM evaluation | Twenty Cycles-compiled surface graphs and fourteen typed programs match original CPU evaluation at existing tolerances on Linux: 8,874 evaluations, five closure families, emission, allocation limits and special path modes. Near-Lambert Oren-Nayar cancellation requires an explicit rounding adapter. A 1,020-evaluation replay passes on Linux and all three Android devices with unchanged binaries, using GPU-buffer closure storage after local-array compilation failed on Adreno. Strict float differences remain. Selected surface-bounce queue entry now passes 19 cases / 18,360 paths and 20 rejection cases, plus complete native renders |
| Real-asset images | The native scheduler renders a 784-triangle ForestCommunity bench with independent CPU film agreement and bitwise-identical Linux repeat; full command replay passes Linux, Pixel API 33, Samsung API 36 and HP API 33. One flat-clay shared-edge equivalence is explicit. A 1024x768/32-spp preview is available. Earlier lime/house images use the separate snapshot renderer. Clay appearance only |
| Material coverage | Native world interpreter covers 25 SVM opcodes, 20 Cycles-compiled graphs, checker/gradient textures, math/conversion and emission/background mixtures on Linux. Remaining SVM nodes, image textures, derivatives/bump, general closure coverage, Principled/microfacet, glass, hair, subsurface and volumes pending |
| Lighting | Two constant finite spherical lights with CDF selection, NEE, opaque shadows and both MIS strategies tested on four devices. Strict isolated sphere-sampling/intersection failures remain. Other light types, emissive mesh sampling, light tree, environment maps, linking and general visibility pending |
| Output/features | Camera-stage atomic sample counts/adaptive skip and background-stage combined/transparency, background/lightgroups, diffuse/glossy/transmission routing, clamping, adaptive auxiliary and static data passes tested against original CPU kernels on Linux. General surface/volume film, adaptive convergence, denoising, color management and interoperability pending |
| Memory and performance | Bounded path scratch/resource reuse demonstrated in the snapshot renderer. Registered device allocator growth/order and queue compaction now pass correctness gates; its arena is bounded by one storage-binding limit. Large scenes, stack/register pressure, compile time and practical throughput remain pending |
| Platforms/delivery | Full Linux host only; other host builds, remaining GPU backends/RIDs, .NET package loading and Android application sandbox pending |
| Full parity decision | Open. A small matching render cannot establish this. Unsupported features must be reported explicitly, never silently approximated |

Material-driven, multi-bounce image comparisons now exist for a limited scene.
WGSL camera/sampling now feeds those paths without native per-path inputs.
Finite-light NEE/MIS now uses a second dispatch for shadow traversal, with
all per-path work on the GPU. The shared native library now adds bounded batches
and GPU film accumulation without changing the images. Actual Cycles device
registration, scheduler controls, native camera, closest intersection and
constant and selected SVM background/film now exist. Native static-triangle
shader-data preparation now has original CPU and four-device GPU checks.
Selected native surface SVM closure evaluation now passes against the CPU on Linux.
The selected surface dispatch now completes a repeated multi-bounce native-scheduler image, with independent original CPU comparisons and complete command replay on four GPUs. Next are texture/attribute and broader closure coverage, then direct-light/shadow/MIS dispatches, alongside
investigation of numerical differences. Each gate keeps
prior evidence reproducible. The full renderer remains the objective; passing
a limited image gate is permission to continue, not a declaration of parity.
