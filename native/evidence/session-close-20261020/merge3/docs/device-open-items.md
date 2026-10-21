## Open device items (session close, 2026-10-20)

These items are open. None is masked, and no tolerance was relaxed. Resolved items are covered in the sections
above:
- the Samsung triangle mode-2 decision (exact traversal);
- the HP compile memory (emissive-triangle compile load);
- the specialization-off crashes on Android (refused by design);
- the Pixel feature-combination, motion and volume-stack failures (function-scope array fix);
- the Android oracle libm differences (validation reference policy);
- the Pixel triangle sampler's error mask (a GPU reset during one long dispatch; Phase E5);
- static instanced triangle emitters (mode 4) on every device (exact traversal; Phase E4 on the Adreno);
- the deforming-emitter sampler on the Pixel 4 XL (reference oracle's arithmetic, Phase E4).

1. **Samsung triangle sampler.** 2 values of 1 sample differ from the glibc reference (sample 3133, object 4,
   prim 5: `P.y` 0.382186 against 0.379992, and `t`). The device CPU and glibc agree on that sample, so the
   Adreno 730 differs from both; not yet localised. The Pixel 4 XL and the HP pass.
2. **Pixel 4 XL object motion with the reference oracle's arithmetic.** The object-moving motion images report
   SVM error 8 when the x64 oracle's adapter constants are selected (imported reference, Phase E4); with the
   device's own oracle they pass ("Session close: the E4/E5/M4 merge on the devices"). Either the adapters'
   WGSL meets an Adreno 640 compiler defect, or the gate should keep the device oracle on this GPU; undecided.
3. **Adreno 640 GPU reset on long submissions.** The driver resets the GPU when one submission runs for about
   2 s. The sampler gate dispatches in slices of 256 invocations. The renderer does not slice its dispatches,
   and one dispatch covers up to 4,096 paths (`num_concurrent_states`); the sampler probe needs about 1 ms per
   invocation on this GPU, so a larger emissive-triangle render could reach the reset (an estimate from the
   probe; the gate images pass). A production fix is dispatch slicing in the scheduler or a per-tier path cap.
4. **Intel Mesa 25.0.7 cold-compile crash** (`brw_opt_combine_constants`, intermittent; Linux): a recompile
   after an area light (object motion) and the exact-traversal emitter probe module. Not hit in the
   session-close runs. A driver fix or a smaller lamp stage remains open.
5. **Checks not yet on the glibc reference.** `render_gate.cpp` lockstep stage checks and the state gates
   (surface, closest, shadow, shader_data, glass/microfacet SVM and bounce, camera, volume, volume_boundary,
   light_state, transparent_shadow, svm_surface and the SVM node gates) compare against the device's own CPU.
   `render_gate.cpp` images use a Linux-prepared reference without scene exchange, and the managed-API scenes
   have no scene dump.
6. **Remaining function-scope arrays** (Adreno 640 candidates): `array<i32,192>` BVH stacks; the
   `array<vec3<f32>,2|3>` locals in shader data, motion triangles, `wavefront_svm_nodes.wgsl` and the
   smooth-normal derivative in `wavefront_svm_derivatives.wgsl` (plus its `array<vec2<f32>,3>`);
   `array<u32,13>` in light-sample packing.
7. **Pixel 4 XL compile time.** Over the 20 s budget, as warnings: motion `intersect_closest` and
   `intersect_shadow` (74-87 s), emissive-triangle `shade_surface_light` (up to about 120 s),
   node-render `prepare_surface` (about 120 s) and `prepare_surface`/`prepare_shader_data` with motion
   (about 50-60 s). Every run stayed on the full tier and inside its wall budget.
8. **Principled BSDF on the Adreno 730 (Samsung).** Creating the Principled probe or `prepare_surface`
   pipeline loses the device.
9. **Texture nodes on the Adreno 730 (Samsung).** The typed 2D fBM noise program (1.792859 against 1.791512)
   and node-render image component 6720 exceed the practical criterion. Contraction or precision in the noise
   chain is suspected. Node worlds pass.
10. **Materials on the Adreno 640 (Pixel 4 XL).** Every Principled and node gate fails: SVM error 8 (24 with
    16 in node renders) in worlds and renders, and the node SVM probe loses the device when its pipeline is
    created. Lamps, glass, feature combination and the triangle gates pass. Candidates are the Adreno 640
    array class (item 6) and the module-scope `svm_stack` with the larger interpreter.
11. **Unspecialized modules exceed the Intel driver's reliable size** (as of the bump/derivative opcodes;
    "Session close: specialization-off status"). On Linux, glass and node renders still give bit-identical
    on/off films after a 10-minute `prepare_surface` compile; the Principled render mis-evaluates; the
    unspecialized SVM probe mis-evaluates node graphs. On the HP the unspecialized `prepare_surface` compile
    terminates the driver. Specialization is required on Android, so this affects the desktop diagnostic
    path and the on/off check. The next step is a "superset" mode for the check (every handler except unused
    bump/derivative regions), then the full on/off identity again.
12. **Gabor texture withheld.** Ported; with its code in a large unspecialized module Intel Mesa 25.0.7
    mis-evaluates Principled paths. Scene-specialized modules would compile it only for scenes that use it;
    the choice is between shipping it on that mechanism, a much smaller body, or a Mesa issue.
13. **Node render scene limit.** A 68-triangle node render scene failed one pixel at 1.25 times the
    practical bound (not isolated), so the gate keeps 60 triangles.
14. **Not yet ported:** true displacement (`NODE_SET_DISPLACEMENT`, displacement and vector displacement) and
    the bump-state branch that needs the undisplaced position; tangent and motion-triangle smooth-normal
    derivatives (rejected, error 16); `NODE_RADIAL_TILING`; AOV output; sky, IES, light falloff, AO, bevel,
    raycast, volume coefficient and particle/hair/point info nodes.
15. **Feature mask nearly full.** Bits 0-61 are used; only 62 and 63 are free. The next lane that needs more
    than two specialization bits must widen the 64-bit mask.
16. **Contractible ShaderData differentials.** `du`, `dv` and `dP` still differ by ULPs between Intel and
    the Adreno; no current film depends on them. Make them contraction-proof if a texture-filtering gate
    needs exact differentials.
