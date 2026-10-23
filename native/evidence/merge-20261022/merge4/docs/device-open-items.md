## Open device items (merge wave 4, 2026-10-22)

One list for the reference devices (Samsung SM-X700, Linux x64, HP). None is masked, and no tolerance was relaxed.
Old numbers (session close, 2026-10-20) are given in brackets. The Adreno 650 (S20) results are not open items of the
reference devices; see "Samsung Galaxy S20 FE 5G (Adreno 650) sweep" at the end (a separate Adreno 650 lane follows).

1. **Samsung triangle sampler** [1]. 2 values of 1 sample differ from the glibc reference (sample 3133 on the
   deforming emitter, object 5, prim 4: `P.y` 0.382186 against 0.379992, and `t`; the device CPU gives 0.380744). The
   pdf differs by one ulp, so the spherical-triangle sampling amplifies an upstream one-ulp difference; not localised.
   Reproducer (lane S): `run_corpus.py samsung triangle X 2400 900 --reference ../reference/linux-x64 --device-scene
   --jobs triangle_sample`. Unchanged in merge wave 4.
2. **Samsung principled_render stage 40** [8, new part]. The Principled pipelines now compile (lane S); the render
   fails a local lockstep check: stage 40, path 2441, `path.mis_ray_pdf` 1751.469 against 1747.274. Linux gives the
   Samsung value when the GGX/sheen/rotation arithmetic is made contraction-proof (lane S `p4`), so the Adreno computes
   the unfused form. Reproducers: Samsung `run_corpus.py samsung materials X 3600 2400 --reference ../reference/linux-x64
   --device-scene --jobs principled_render`; Linux with lane S's `native/work/s/patches/p4_principled_contraction.py`.
   The same failure appears on the S20.
3. **Samsung node SVM probe** [9]. (a) New in merge wave 4: compiled graph 44 (Fresnel + Layer Weight) mis-evaluates
   on the Adreno 730 when the Gabor handler is in the module ("Merge wave 4" above; reproducer: the captured dispatch
   and WGSL variants in `native/work/merge4/diag/svm309`, `samsung_variants.sh`). (b) Lane S: after 512 passing cases the
   Vulkan driver terminates (SIGSEGV) creating the cumulative probe module (after "vector math derivative 29");
   reproducer `run_corpus.py samsung materials X 3600 2400 --reference ../reference/linux-x64 --device-scene --jobs
   nodes_svm`. (b) was not reached in merge wave 4 because the gate now stops at (a).
4. **Samsung node render component 6720** [9]. GPU 0.928481 against glibc 0.928078; the GPU equals the device CPU
   (0.928485): the pixel follows `render_gate.cpp`'s device-own arithmetic adapters, kept for the lockstep checks.
   Needs a design decision (lane S decision 1). Reproducer: `--jobs nodes_render` as above.
5. **Checks not yet on the glibc reference** [5]. The `render_gate.cpp` lockstep stage checks and the state gates
   (surface, closest, shadow, shader_data, glass/microfacet SVM and bounce, camera, volume, volume_boundary,
   light_state, transparent_shadow, svm_world) compare against the device's own CPU. Lane S moved the
   `render_gate.cpp` images and the SVM probe gates to the device-scene glibc flow (environment opt-in); the
   managed-API scenes have no scene dump.
6. **Not yet ported** [14]: true displacement (`NODE_SET_DISPLACEMENT`, displacement and vector displacement) and the
   bump-state branch that needs the undisplaced position; tangent and motion-triangle smooth-normal derivatives
   (rejected, error 16); `NODE_RADIAL_TILING`; AOV output; sky, IES, light falloff, AO, bevel, raycast, volume
   coefficient and particle/hair/point info nodes.
7. **Feature mask** [15]. Bits 0-61 and 63 (Gabor) are used; only bit 62 is free. The mask must be widened before
   any lane that needs a new specialization region (Step zero).
8. **Contractible ShaderData differentials** [16]. `du`, `dv` and `dP` still differ by ULPs between Intel and the
   Adreno; no current film depends on them.

Resolved, or closed under the device reference policy:
- [4] Intel Mesa 25.0.7 cold-compile crash: FIXED (lane L; 16-bit IP/VGRF fields in `brw_opt_combine_constants`,
  light stage split, Horner loops; every compiled entry below 60,000 backend instructions, re-measured in merge 4:
  worst 52,509).
- [11] Specialization on/off identity on Linux: FIXED (lane L; superset groups, every group bit-identical).
- [12] Gabor: shipped on the scene-specialized path (lane L); see open item 3(a) for the Adreno 730.
- [13] Node render scene limit: FIXED (68 triangles; root cause recorded as an inference).
- [8] Principled device loss on the Adreno 730: FIXED (lane S; SVM stack in a per-invocation arena slot);
  principled_svm and principled_bounce pass. The remaining render failure is open item 2.
- [9] Typed 2D noise program on the Adreno 730: FIXED (lane S; fade product ordering barrier).
- [3] Long submissions: measured on the Adreno 730, longest 29 ms, no reset (lane S); the Adreno 640 part is closed
  under the policy.
- [2], [6], [7], [10] Adreno 640 items: closed under the device reference policy (not supported on the Adreno 640;
  "Pixel 4 XL triage under the device reference policy"). The materials verdicts were settled in merge wave 4:
  Principled not supported (3x), surface nodes not supported (fails).
- Earlier: the Samsung triangle mode-2 decision (exact traversal); the HP compile memory (emissive-triangle compile
  load); the specialization-off crashes on Android (refused by design); the Pixel feature-combination, motion and
  volume-stack failures (function-scope array fix); the Android oracle libm differences (validation reference policy);
  the Pixel triangle sampler's error mask (Phase E5); static instanced triangle emitters (mode 4, Phase E4); the
  deforming-emitter sampler on the Pixel 4 XL (Phase E4).
