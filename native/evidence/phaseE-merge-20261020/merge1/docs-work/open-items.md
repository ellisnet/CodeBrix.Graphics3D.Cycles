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
