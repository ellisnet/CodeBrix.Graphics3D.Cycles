# M1 materials: Principled BSDF closures in the WGSL backend

Worktree `/home/jeremy/ClaudeHome/cycles-worktrees/m1-principled` (clean checkout of 01eb27b6,
baseline manifest `baseline-materials.json`). Linux only (Intel UHD ADL-S, Mesa 25.0.7); no
Android devices. No commits, refs, stash or index changes. Sealed handoff:
`native/work/m1/handoff/` (changes.patch sha256 `b2072d45e257675c923172af566369179aab9d9f236b6acad20a86c1567dae13`,
21 paths, verified to reproduce the final tree from a pristine HEAD export).

## Result in one paragraph

The Principled BSDF is implemented in WGSL for every lobe except subsurface: alpha, sheen
(LTC), coat (dielectric Fresnel, tint, coat normal), emission attenuated by sheen/coat,
F82-tint metallic, generalized-Schlick transmission and specular (IOR level, tint), thin-walled
transmission (OpenPBR thin glass), thin-film iridescence (dielectric, metallic and
transmissive lobes), Lambert/Oren-Nayar diffuse, anisotropy with tangent rotation, and both
GGX distributions with multiscatter energy preservation (`ggx_E`, `ggx_glass_E`,
`ggx_glass_inv_E` and averages). Subsurface is rejected natively before rendering (clear
message) and by the GPU handler (error 16). New gates `principled_svm`, `principled_bounce`,
`principled_render` pass against the unmodified CPU kernels at the practical criterion; the
Principled render is bitwise identical with specialization on and off and replays in the
current recorder format. The full 29-target all-lanes corpus passes (cold caches). The glass
scene is untouched: every specialized module byte-identical to the baseline, film `a8e69a34...`
with specialization on and off. One exception: the Principled ForestCommunity bench render
stops at stage 30 on a pre-existing closest-intersection difference (bit-identical ray,
sliver triangle), not on shading.

## Timeline

- [07:03:18] Start; read AGENTS/MAINTAINER standing instructions, native README, DEVICE/PARITY rows, closure/microfacet WGSL, pipelines, gates, pinned `svm/closure.h`, `bsdf_microfacet.h`, `bsdf_sheen.h`, `bsdf_util.h`, `alloc.h`.
- [07:08] Baseline build of the unchanged tree (default targets, then the 29 Phase C targets); baseline binaries saved (`native/work/m1/baseline-bin`).
- [07:17] Baseline glass_render cold: PASS, film `a8e69a34...`, pipeline log + captured modules (before-measurements).
- [07:16-07:22] Principled setup WGSL, Fresnel/sheen eval/sample, census bit, host rejection, reference encoding, fixture, gate cases, CMake targets.
- [07:23] principled_svm first run: all compiled graphs and 157 typed programs pass; one rejection-case ordering issue in my new cases (shared truncation test index) fixed.
- [07:25-07:26] principled_bounce PASS; principled_render PASS; bench fails at stage 30 (diagnosed below).
- [07:28-07:35] Diagnosis: the failing bench ray is bit-identical on GPU and CPU (`rayBitsIdentical=1`); the difference is in closest intersection on a 5.7 mm sliver triangle. Recorded as an exception, fixture unchanged.
- [07:35-07:40] Added thin-film iridescence and thin-walled transmission (scope item 1 lists both as Principled lobes); census bits 31/32.
- [07:40-07:51] principled_bounce case 9 (diffuse filter) failed on one path (0.3-0.7% throughput). Two throw-away experiments (anisotropy off; log-all-mismatches; both reverted) and a direction-bit dump showed a transparent sample with the thin-glass transmission lobe evaluated at its own peak: `reflect(wi,N)` was computed with driver `dot`/contractible arithmetic. Using the existing rounded adapters (`rounded_dot`, `rounded_add3`) fixed it. (Also replaced driver `sin/cos` for the Principled tangent rotation with the precise bounded adapter; this did not affect that path.)
- [07:51-08:00] Gates PASS; Linux replay PASS; specialization off render bitwise identical.
- [08:02] Found that every scene (including glass) enabled the Principled region: Cycles' `default_surface` shader is a Principled BSDF. Terminated the first all-lanes run after ~6 min; Principled bits are now counted only from referenced shaders (triangle shader table, lights, world). Glass modules became byte-identical to baseline.
- [08:14-08:37] Candidate A: all-lanes PASS (29 targets).
- [08:24] Compile attribution: prepare_surface inlined 2.63 MB with thin film/wall (above the Intel Android 2.2-2.5 GB danger zone from B3). Restructured the setup into one native-order lobe loop with single call sites for allocation, albedo estimate, energy preservation and layering; unified LUT/Fresnel/Airy call sites: 2.63 MB -> 0.90 MB (bench-style Principled 1.08 MB -> 0.60 MB).
- [08:40-09:06] Final: full rebuild; all-lanes PASS again (29 targets, sources and binaries unchanged during the run); principled svm/bounce/render, specialization off, glass on/off, replay, bench re-checked; clean sequential cold compile measurements.
- [09:08-09:18] Docs (PARITY rows, DEVICE.md section), new `native/tools/principled_gate.py` (run: PASS), plain svm_surface gate PASS, audits PASS, handoff sealed and patch verified.

## Closure inventory (pinned `svm_node_closure_bsdf`, Principled branch) and mapping

| Principled component (native) | Native closure / Fresnel | WGSL before M1 | WGSL now |
| --- | --- | --- | --- |
| Alpha < 1 | `bsdf_transparent_setup` | transparent existed | reused (`svm_transparent`) |
| Sheen | `SheenBsdf`, `bsdf_sheen_setup` (sheen_ltc 3x32x32, invalid LTC -> `CLOSURE_NONE_ID`) | none | new setup + LTC eval/sample (`sheen_evaluate`, `sheen_sample`) |
| Coat | GGX reflection, `DIELECTRIC` Fresnel, `bsdf_microfacet_setup_fresnel_dielectric` (always preserves energy), coat tint (Beer), coat normal | GGX reflection existed, no dielectric Fresnel | new Fresnel model 1 |
| Emission | `emission_setup` with sheen/coat-attenuated weight; emission-only path uses stack closures (`bsdf_alloc_maybe_emission`) | emission existed | reused; emission-only virtual closures implemented |
| Metallic | GGX reflection, `F82_TINT` (`fresnel_f82tint_B`, `fresnel_f82_Fss`) | none | new Fresnel model 5 |
| Transmission | GGX glass, `GENERALIZED_SCHLICK` exponent -ior, f0 = F0*specular tint, transmission tint sqrt(base) | glass node (scalar f0) | spectral f0, model 4 (glass node shares the path in Principled modules, identical arithmetic) |
| Thin wall | `bsdf_thin_glass_setup`: GGX reflection with constant Fresnel + `CLOSURE_BSDF_THIN_GLASS_TRANSMISSION_ID` (or transparent off camera) | none | new (eval/sample on the mirrored direction) |
| Specular | GGX reflection, generalized Schlick, IOR level, tint, anisotropy | GGX reflection | model 4 reflection |
| Thin film | `fresnel_iridescence_channel` (dielectric and conductive), `thin_film_table` | none (glass node rejects) | new for Principled (glass node still rejects, unchanged) |
| Subsurface | `bssrdf_setup` (Burley/random walk/skin) or thin-wall subsurface | none | **rejected** (native message + GPU error 16) |
| Diffuse | Lambert or Oren-Nayar (`bsdf_oren_nayar_param` saturates colour) | existed | reused math |
| Multiscatter GGX | `microfacet_ggx_preserve_energy` with ggx_E/Eavg (2D/1D) and ggx_glass(_inv)_E/Eavg (3D/2D) | ggx_E only (glossy) | both table families |

Pre-existing WGSL closures: GGX reflection/refraction, glass, Lambert, Oren-Nayar, translucent,
transparent, emission, holdout, volume (absorption/HG).

## Design

- **Canonical record unchanged (112 bytes).** Per-path GPU memory is `paths x 64 x record`, so
  fields are reused per kind (documented in `surface_closure.h`): microfacet `roughness` = native
  `MicrofacetFresnel` value as float (0/1/4/5), `multiscatter` = f0, F82 `b` in
  `reflection_tint`, active thin film thickness/IOR in `a`/`b`; sheen keeps transformA/B in
  `a`/`b`, T/B in `T`/`multiscatter`. The CPU reference fills the same encoding from the real
  closures and throws on any Fresnel model the device cannot represent.
- **Specialization.** Appended bits 30 `SVM_BSDF_PRINCIPLED`, 31 `SVM_BSDF_THIN_FILM`, 32
  `SVM_BSDF_THIN_WALL` (SF_ALL now `(1<<33)-1`); `SVM_BSDF_MICROFACET` also follows Principled.
  Principled bits come from a census rooted at referenced shaders only (new `tri_shader` capture
  plus lights and world shaders) because `default_surface` is a Principled BSDF; all other
  features keep the existing all-shader census. Shared lines that needed a Principled-only
  variant use the marker fallback (`//#if X else <original line>`), so modules without the
  feature are byte-identical to the baseline. The Principled layout constants are emitted inside
  the region too.
- **Native rejection.** `svm_census` records `unsupported` for Principled records whose
  subsurface weight is a constant above the cutoff or linked; `WebGPUDevice::shader_features`
  throws `Unsupported feature: Principled BSDF subsurface scattering ...` before any pipeline or
  dispatch, specialized or not. The WGSL handler reports error 16 if a subsurface weight above
  the cutoff is evaluated (defence in depth).
- **Tables.** Every read is bounds checked (error 512): sheen LTC, gen-Schlick s/ior_s, ggx_E,
  ggx_glass(_inv)_E/Eavg via the existing `microfacet_lut*`; thin-film table validated at setup
  (svm error) and per read in eval (atomic error word, no substitute value). The glass node's
  existing reads are unchanged.
- **Adapters.** Rounded binary32 helpers wherever conditioning is sharp: generalized-Schlick
  arithmetic identical to the glass node, `ggx_dielectric`, rounded polarized Fresnel for the
  film, rounded `reflect` for thin glass, native `normalize`/orthonormal adapters for the sheen
  frame and sample, precise bounded sin/cos for tangent rotation, `surface_normalize` for
  `safe_normalize(_fallback)`.
- **Compile pressure.** One lobe loop (0 sheen, 1 coat, 2 metallic, 3 transmission/thin-wall
  reflection, 4 thin-wall transmission, 5 specular, 6 diffuse) with single call sites for the
  heavy helpers; single LUT site in the albedo estimate; per-channel and per-polarization loops in
  the film code; one `svm_valid_normal` site for base and coat normals.

## Gate results (final binaries, Linux, cold Mesa cache, practical `5e-5+2e-4|ref|`; strict reported)

| Gate | Coverage | Practical | Strict / maxima |
| --- | --- | --- | --- |
| principled_svm | 20 compiled graphs, 237 cases, 19,788 paths, 10 closure families, 34,371,756 float + 1,345,584 integer + 33,976,320 state checks; 19 GPU rejection cases (incl. subsurface 0.5/1/1e-4, truncated record, four out-of-bounds tables); 3 native subsurface rejections + 4 accepted (film, thin wall, sub-cutoff) | PASS | 966 strict float differences; max abs 9.54e-7 |
| principled_bounce | 29 integrator cases (roulette, termination, bounce limits, clamps, closure/caustic filters, transparency threshold, max closures 1), 28,560 paths, 21 rejections | PASS | 7,672 strict; max abs 0.125 on mis_ray_pdf 898325.94 vs .81; max practical ratio 0.109 |
| principled_render | 20 Principled graphs on the fixture triangles + 6 smooth Principled spheres, 64x48x8, 145 stages, 121,235 paths, bitwise repeat | PASS | 16,822 strict stage values (max ratio 0.369); image max 8.34e-6, 126 strict image values |
| principled_render, specialization off | same | PASS | film `6fa85969...` identical to specialized |
| principled_render Linux replay | recorder `native-command-stream-v6` (current format for this workload; v7 only with transparent-shadow ShaderData), 1,056 commands | PASS | image max 8.34e-6, 126 strict |
| principled bench (ForestCommunity, clay replaced by coated/sheen bench, F82 floor, dielectric walls) | 784-triangle bench | **FAIL at stage 30** | see Exceptions |
| principled_gate.py (new tree script) | the three gates with assertions | PASS | |
| glass_render specialized / unspecialized | | PASS | film `a8e69a3424036a797160ad074aca1a5435f0d232e7b3298e5509cf8ef7349602` both |
| svm_surface (plain) | 23 cases, 8,874 paths | PASS | 766 strict, max 7.63e-6 |
| all-lanes 29-target corpus (glass/microfacet svm/bounce/render, feature_combination, volume, motion, transparent shadow, ...) | | PASS (twice) | glass_svm 50 strict / 1.79e-7; microfacet_svm counts unchanged (28,170,666 floats); feature_combination 8 cases, max 3.39e-5, 1 repeat bit difference (pre-existing atomic film order, B3) |
| audits | audit.py, notices.py --check (34 components, unchanged), compatibility (12) and guard (7) tests, git diff --check | PASS | |

The existing gates' record comparisons now include the generalized-Schlick encoding for glass
closures (both sides) and keep their expected counts. Rejection case 0 of the SVM gates used
`CLOSURE_BSDF_PRINCIPLED_ID` as "an unsupported closure"; since Principled is now supported it
uses `CLOSURE_BSDF_ASHIKHMIN_VELVET_ID` (still unsupported, same expected error 16).

## Compile measurements (Linux, cold cache, sequential, wgpu module + pipeline time)

| Scene / module | specialized module bytes | prepare_surface inlined | shade_surface inlined | shade_surface_light inlined | connect_surface_light inlined | prepare / shade / light / connect pipeline ms |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| glass scene, baseline | 240,514 | 286,192 | 610,251 | 643,134 | 321,277 | 2,909 / 5,266 / 2,591 / 3,394 |
| glass scene, final | 240,514 (byte-identical) | 286,192 | 610,251 | 643,134 | 321,277 | 2,985 / 5,556 / 2,697 / 3,370 |
| Principled render (all lobes + film + thin wall + glass/diffuse) | 272,256 | 898,933 | 906,089 | 643,134 | 428,398 | 21,094 / 12,696 / 2,483 / 6,018 |
| Principled bench (Principled only) | 263,849 | 600,169 | 788,458 | 643,134 | 369,714 | 9,859 / 8,626 / 2,508 / 4,322 |
| Principled render before loop restructure (candidate A) | 271,594 | 2,626,735 | 1,016,195 | 643,134 | 483,451 | 42,661 / 15,716 / 2,590 / 7,380 (under all-lanes contention) |
| Principled bench before restructure | 262,165 | 1,083,249 | 788,458 | 643,134 | 369,714 | 17,994 / 10,867 / 3,568 / 5,538 (contention) |
| unspecialized, final | 278,803 | 1,913,209 | 906,089 | 3,949,428 | 428,398 | 131,369 prepare (spec-off run) |
| unspecialized without Principled (= pre-M1 unspecialized) | 247,061 | 1,300,468 | 610,251 | 3,949,428 | 321,277 | |

Inlined size = textual call-graph expansion (B3 `wgsl_stats.py`); specialized modules for the
bench were reconstructed with a Python port of `specialize_wgsl` that reproduces the captured
Principled render module byte for byte. Linux compile times grow 3-7x for Principled scenes
(prepare_surface 21 s with every lobe, 10 s without film/wall); shade_surface_light is
unaffected (no closure setup). The unspecialized prepare_surface grows by 0.61 MB.

## Exceptions

1. **Principled bench render (ForestCommunity) stops at stage 30** (`isect.u path=84 actual=0.003322
   expected=0.003467`). The incoming ray is bit-identical on GPU and CPU (`rayBitsIdentical=1`,
   added diagnostic); triangle 328 is a sliver (0.62 m x 5.7 mm). The GPU closest-intersection
   kernel's barycentric differs beyond the practical tolerance (t differs by 3 ulps). This is in
   the closest stage, not in Principled shading, and outside M1 ownership. The fixture was not
   changed to avoid it; the clay bench never generated this ray. Needs an intersection-precision
   follow-up (same-prim barycentric adapter or comparison rule decision by Jeremy).
2. The thin-wall path with `ior < 1` at grazing incidence reads an uninitialized
   `cos_theta_t` in the pinned CPU code (TIR in `fresnel_dielectric`); WGSL uses 0. Fixtures avoid
   thin wall with ior < 1.
3. Specialization-off unspecialized modules grow by the Principled code (prepare_surface +0.61 MB
   inlined); the open "unspecialized module on Android" item becomes harder.

## What remains

- **Subsurface** (Burley, random walk, skin, thin-wall subsurface): BSSRDF closures, the
  subsurface integrator kernels and their gates. Today rejected with a clear message.
- Glass node thin film and multi-GGX glass node remain rejected (unchanged behaviour).
- Bench intersection follow-up (exception 1).
- **Device validation (not run, no devices):**
  - Samsung SM-X700 (Adreno 730, ARM64): run principled_svm/bounce/render with the oracle-host
    normalization mode. Risks: sharp coat/specular lobes and the thin-glass peak are
    ulp-sensitive (as path 126 was); thin-film Airy sums and pow() for coat/thin-glass tints use
    driver transcendental precision; ARM vs x64 normalize adapters already cover the sheen frame.
  - HP (Intel Mesa 24.0.8, Android x64): compile pressure. Principled prepare_surface is 0.60 MB
    inlined (Principled only) to 0.90 MB (all lobes), above the glass scene's 0.29 MB but below
    the B3 crash zone (2.3 MB); unspecialized prepare_surface 1.91 MB is near it. Run first with
    the persisted-tier canary; expect long compiles.
  - Pixel 4 XL (Adreno 640): compile latency (Linux 10-21 s for prepare_surface suggests minutes on
    Adreno) and the open feature_combination LUT bounds failure (error 512 path): Principled adds
    sheen/gen-Schlick/glass-E/thin-film tables, all bounds checked, so a broken table upload will
    surface as error 512 rather than wrong radiance.

## Changed paths (21)

`docs/PARITY.md`, `native/DEVICE.md`, `native/cycles/{device.cpp,device.h,pipelines.cpp,pipelines.h,
principled_fixture.h (new),render_gate.cpp,surface_closure.h,surface_fixture.h,surface_gate.cpp,
surface_reference.cpp,svm_layout.cpp,svm_surface_gate.cpp,svm_surface_reference.cpp}`,
`native/shaders/{wavefront_microfacet.wgsl,wavefront_surface.wgsl,wavefront_svm_closures.wgsl,
wavefront_svm_microfacet.wgsl}`, `native/tools/{inject_cycles.cmake,principled_gate.py (new)}`.
THIRD-PARTY-NOTICES unchanged; pinned Cycles snapshot and `native/evidence` untouched; runtime
`CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433` unchanged.
