# Merge wave 2: M3 (combined M1+M2+M3 materials) into main

Main before the merge = `baseline-merge1.json` (47,971 files, verified 0 missing/extra/changed; `pre-merge.json`).
M3 handoff `changes.patch` sha256 `00895dfae46f32f4f9a406fb02b6997739dbcabdd18426d3e44c39f0fd6d53a7` (verified),
28 paths against 01eb27b6. Every `source/` copy in the handoff equals its `changes.json` after-hash.

## Method
- 14 paths were unchanged in main since 01eb27b6 (single owner = M3). They were applied with plain
  `git apply --exclude=<the 14 shared paths> changes.patch` (`--check` first). All 14 end at M3's after-hash:
  `compatibility/opcodes.json`, `native/cycles/{nodes_fixture.h,principled_fixture.h,surface_closure.h,
  surface_fixture.h,surface_reference.cpp,svm_layout.cpp,svm_surface_reference.cpp}`,
  `native/shaders/{wavefront_microfacet.wgsl,wavefront_svm_closures.wgsl,wavefront_svm_microfacet.wgsl,
  wavefront_svm_nodes.wgsl}`, `native/tools/{nodes_gate.py,principled_gate.py}`.
  (The listed `svm_layout.cpp` was predicted shared; main had not changed it, so it is single-owner.)
- 14 paths were changed by both sides. Each was merged with `git merge-file -p ours base theirs`
  (base = `git show 01eb27b6:<path>` in `base/`, ours = main in `ours/`, theirs = M3 `source/` in `theirs/`,
  result in `merged/`), reviewed, hand-resolved where needed and copied over main's file.
  8 merged cleanly, 6 had conflicts.

## Shared files

| File | Automatic | Resolution |
| --- | --- | --- |
| `native/cycles/pipelines.h` | conflict | E3's `SF_TERMINATOR_GEOMETRY=1<<30`, `SF_TERMINATOR_SHADING=1<<31` and their comment kept exactly; M3's "bits 30/31 reserved" comment dropped (they are now used); M3's table 32-54 and `SF_PRINCIPLED_ALL` kept; one `SF_ALL=(1ull<<55)-1` (E3's `(1<<32)-1` removed). |
| `native/cycles/pipelines.cpp` | conflict | Name table: E3's `SHADOW_TERMINATOR_GEOMETRY/SHADING` entries, then M3's node names and `SVM_NODES_*` composites, then M3's `SVM_BSDF_MICROFACET` (now `GLOSSY|GLASS|PRINCIPLED`) and the three Principled names. The rest (Principled census, unsupported-feature refusal, terminator feature detection) merged cleanly and was reviewed: E3's terminator bits are added after M3's census line in `shader_features()`. |
| `native/cycles/svm_surface_gate.cpp` | conflict | M3's `main` line (`host_rejections`) with wave 1's `require_webgpu_device(owner.get(),"svm_surface_gate device creation")` hardening in place of the unchecked cast. |
| `native/cycles/svm_world_gate.cpp` | conflict | M3's `main` line plus its `CBC_NODES_FIXTURE` world loop, with the wave-1 hardening call in place of the unchecked cast. |
| `docs/PARITY.md` | conflict | Material coverage row: M3's text up to the Principled sentence, then main's merged device sentences (Pixel 4 XL passes after E2's fix; volumes/motion on all three). Lighting row: main's (M3's was the stale base text). Materials device results added during docs consolidation. |
| `native/DEVICE.md` | conflict | Main's text kept whole; M3's "Principled BSDF closures" section inserted before main's "Open device items (after the Phase E merge)"; M3's copy of the old pre-merge open-items list dropped (main's list supersedes it). M3's node section (mid-file) applied cleanly. Consolidated afterwards (materials device section, open items 8-14). |
| `native/cycles/device.cpp` | clean | E3's `tri_shader` smooth-triangle scan and M3's `tri_shader_words` copy are separate statements on the same upload; both kept. E1's Android spec=0 refusal and E3's `recover` untouched. |
| `native/cycles/device.h` | clean | E3's `smooth_triangles`, `recover`, wave 1's `require_webgpu_device`, M3's Principled refusal state and `tri_shader_words`. |
| `native/cycles/render_gate.cpp` | clean | X1 reference policy + hardening kept; M3's Principled scene shaders and diagnostic `rayBitsIdentical` field. |
| `native/cycles/surface_gate.cpp` | clean | E3 test-12 rejection + hardening kept; M3's `CBC_PRINCIPLED_FIXTURE` conditions. |
| `native/shaders/wavefront_svm.wgsl` | clean | E2's module-scope `svm_stack` and constant-index mask clears kept; M3's size and dispatch cases added. M3's handlers use the accessors only (no `SvmState` stack field). |
| `native/shaders/wavefront_surface.wgsl` | clean + 1 edit | E1 prefill, E3 `surf_terminator` split and M1 sheen/thin-wall kept. **Semantic edit:** M1's sheen sample branch now applies E3's shading terminator before the bump term (`value=surf_terminator(...); value*=surf_bump(...)`), as Cycles `bsdf_sample` does for every non-transmission label. Bit-identical when the frequency multiplier is <= 1 (every gated scene). |
| `native/tools/build_cycles.py` | clean | X2 AST rename, E3 terminator module, M3 `wavefront_svm_nodes.wgsl` and `svm_nodes_enums.inc`. |
| `native/tools/inject_cycles.cmake` | clean | E3 terminator target; M1 principled and M2 nodes target loops. |

## Gate device-creation hardening
Every gate source that M1/M2/M3 build from (`svm_surface_gate.cpp`, `svm_world_gate.cpp`, `render_gate.cpp`,
`surface_gate.cpp`) calls `require_webgpu_device`; no unchecked `dynamic_cast<WebGPUDevice*>(owner.get())`
remains. M1/M2/M3 added no new gate source file.

## Final feature-bit table (`native/cycles/pipelines.h`)

| Bits | Names | Lane |
| --- | --- | --- |
| 0-29 | base table (`SF_GEOMETRY` ... `SF_LIGHT_TRIANGLE`) | base |
| 30, 31 | `SF_TERMINATOR_GEOMETRY`, `SF_TERMINATOR_SHADING` | E3 |
| 32-34 | `SF_BSDF_PRINCIPLED`, `SF_BSDF_THIN_FILM`, `SF_BSDF_THIN_WALL` | M1 |
| 35-52 | `SF_TEX_NOISE` ... `SF_TEX_VORONOI` (18 bits) | M2 |
| 53, 54 | `SF_WAVELENGTH`, `SF_WIREFRAME` | M3 |
| mask | `SF_ALL=(1ull<<55)-1` (contiguous 0-54); `SF_PRINCIPLED_ALL`=32-34 | |

No feature bit was added; the tier table was not touched.

## Notices
`notices.py` regeneration was byte-identical (NOTICE, both THIRD-PARTY-NOTICES, `dependencies.json`);
`--check` 34 components; `audit.py` passed.

## Probe gates compile scene-specialized modules (coordinator decision, option 1)

**Why.** After the merge, `normal_gate` failed in all four variants on Linux (Intel UHD ADL-S, Mesa 25.0.7)
with SVM error 8 at path 16, shader 7. Its probe module is the whole unspecialized SVM source: 288 KB merged,
against 169 KB in wave 1. Two scratch A/B builds both passed with wave 1's exact numbers:
- M3's function-local stack instead of E2's module-scope `svm_stack`;
- E2's stack with a scene-specialized probe.

The M3 handlers are unreachable for that fixture, so the merged code behaves exactly like wave 1's: this is a
driver miscompile of E2's module-scope `svm_stack` inside the large unspecialized probe module. It is the same
class as M3's Gabor finding. Records: `validation/diag-*`, `validation/diag-B.patch`.

**Change** (files and patch: `option1/before`, `option1/after`, `option1/probe-specialization.patch`):

| File | Change |
| --- | --- |
| `native/cycles/device.h` | `gate_probe_wgsl(device, source)`: the scene-specialized module (`module_wgsl`), with the reason in a comment. |
| `native/cycles/surface_svm_test.h` | `pipeline()` builds the probe for the uploaded scene. `refresh_probe()` rebuilds it before a dispatch whose programs reach a region the module lacks (typed programs and rejection cases upload programs after creation; without this, run 3 failed with error 2). The probe keeps the Principled region whenever glass is reachable: canonical glass records carry the Fresnel model and f0 only there, and the CPU reference always encodes them (without this, `glass_svm` failed on `closures[0].roughness` 0 vs 4). Native refusals (unsupported Principled features) keep the current module. Captures write the compiled module. |
| `native/cycles/normal_gate.cpp`, `attribute_gate.cpp`, `svm_surface_gate.cpp` | `refresh_probe` before each rejection dispatch; the dumped `.wgsl` is the compiled module. |
| `native/cycles/image_gate.cpp` | Graph probe dump as above; sampler probe specialized once (no programs change). |
| `native/cycles/image_file_gate.cpp` | File-image probe specialized once at creation; capture writes the compiled module. |
| `native/tools/unspecialized_compile_smoke.py`, `native/tools/compile_probe.cpp` (new) | Compile-only smoke step for the unspecialized `prepare_surface`/`shade_surface_light`: module and inlined sizes, cold pipeline time and peak memory, each in a fresh process with a fresh Mesa cache. Warning only, never a gate failure. `compile_probe.cpp` is the Phase E1 measurement probe, promoted from the E1 evidence. |

**Not changed.**
- `shader_data_gate.cpp` is back at its pre-merge bytes. Its probe has no SVM interpreter, and its negative test
  needs the unspecialized regions (run 2 failed "Invalid surface data did not report failure 3").
- No feature bit, no production code path, no WGSL.
- Gabor is not shipped.

**Effect.**
- Every probe gate reports exactly the numbers it reported with the unspecialized module: svm_surface, normal,
  attribute, image, principled_svm, nodes_svm, glass_svm and microfacet_svm all have identical reports.
- `CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0` still selects the unspecialized probe; nodes_gate's spec-off runs
  use it.
- The unspecialized module stays checked by the spec-off render gates (glass, principled, nodes on/off film
  identity) and measured by the smoke step.

## Opcode census (computed from the merged WGSL)
- Case labels in every `native/shaders/*.wgsl` dispatch, plus `NODE_END` (the loop terminator), against the
  111 entries of `compatibility/opcodes.json`.
- **76 of the 108 real opcodes are implemented.** `NODE_NONE`, `NODE_PAD1` and `NODE_NUM` are not opcodes.
- 32 are not implemented: derivative variants, bump/displacement, volume, particle/hair/point info, sky, IES,
  light falloff, AO/bevel/raycast, AOV, radial tiling and the withheld Gabor.
- This agrees with M3's census.
