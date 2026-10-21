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
glibc is correctly rounded. Evidence: `native/work/e1/libm/`. Matching these
oracles on the GPU means reproducing bionic's libm (a separate, licensed
implementation, different per Android ABI); that is a decision for the
maintainer, not a rounding adapter.

**Specialization off on Android.** `CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0` is now
refused on Android when the WebGPU device is created, with a message naming the
variable, before any pipeline compiles. It remains a desktop A/B diagnostic.
