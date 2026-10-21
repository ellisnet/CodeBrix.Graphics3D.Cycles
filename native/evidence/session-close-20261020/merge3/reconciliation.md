# Merge wave 3: E5, E4 and M4 into main

Main before the merge = `baseline-merge2.json` (58,498 files, verified 0 missing/extra/changed; `pre-merge.json`).
Work folders: `patches/` (the three sealed patches, copied), `e4/` and `m4/` (`base/`, `ours/`, `theirs/`,
`merged/` for every three-way file), `docs/` (the sequential DEVICE.md / PARITY.md merges and the hand-written
resolutions), `tools/` (runner set, `classify.py`, `resolve.py`).

## Patch verification

| Lane | changes.patch sha256 (= handoff.json) | Seed | Paths |
| --- | --- | --- | --- |
| E5 Pixel sampler | `bd0574592574c84af7b3a11ab6441c14dab25a81731f8911a7f98245f05f98e9` | `baseline-merge1.json` | 3 |
| E4 mode 4 on Adreno | `07f0ac15edb6f93a21b13fae3459dfefae517ba19fe81055beff3ffb24ee4ac2` | `baseline-merge1.json` | 10 |
| M4 bump + derivatives | `9d2fa055077e0ea26b05ed4ebc4f6a8a032c03eed09a6c9248e2fea149300608` | `baseline-m4.json` (01eb27b6 + M3) | 14 (1 new) |

Every `source/` copy equals its `changes.json` after-hash, and every E4/E5 `before/` copy equals its before-hash.
M4's handoff has no `before/`: its base copies were rebuilt from `native/work/merge2/theirs/` (M3's source copies),
`git show 01eb27b6:<path>` or main's unchanged file, and each matches M4's `baselineSha256`
(`tools/classify.py` and the build step of this file's history in `timeline.log`).

## Order and method
1. **E5.** `emitter_sample_gate.cpp` was at E5's before-hash, so it went in by plain `git apply --include` and ends at
   E5's after-hash `a240bcfb`.
2. **E4.** Its 7 single-owner code files (`camera.cpp`, `lighting.cpp`, `lighting_gate.cpp`, `reference_policy.h`,
   `surface.cpp`, `wavefront.h`, `wavefront_shader_data.wgsl`) were at E4's before-hashes (waves 1-2 had not touched them
   after E4's seed), so plain `git apply --exclude` applied them; all 7 end at E4's after-hashes. The fuse-proof
   ShaderData forms are therefore E4's bytes exactly. `emitter_sample_gate.cpp` was merged with
   `git merge-file` (base = E4 `before/`, ours = main after E5, theirs = E4 `source/`): clean; the result is E5's
   slice loop plus E4's `reference_policy::use_reference_oracle_arithmetic();` first line of `main`, and the wave-1
   `require_webgpu_device` line (sha256 `5ca72c77`).
3. **M4.** 6 single-owner paths by plain `git apply --include`: `compatibility/opcodes.json`,
   `native/cycles/{nodes_fixture.h,svm_layout.cpp}`, `native/shaders/{wavefront_svm_derivatives.wgsl (new),
   wavefront_svm_nodes.wgsl}`, `native/tools/nodes_gate.py`; all 6 end at M4's after-hashes (then `nodes_gate.py`
   was edited, below; M4's after copy is kept as `m4/nodes_gate.lane-after.py`). 6 shared code files merged three-way
   (base = 01eb27b6 + M3, ours = main, theirs = M4 `source/`).
4. **Docs** (`native/DEVICE.md`, `docs/PARITY.md`): merged in sequence E5, E4, M4 (`docs/*.after-<lane>.md`).

## Shared code files (M4)

| File | Automatic | Resolution / review |
| --- | --- | --- |
| `native/cycles/pipelines.h` | clean | E3's bits 30/31 and M3's 32-54 unchanged; M4's 55-61 appended; one `SF_ALL=(1ull<<62)-1`. |
| `native/cycles/pipelines.cpp` | clean | Pure additions: M4's seven `SVM_NODE_*` names plus `SVM_DERIVATIVE_ANY`, `SF_MAPPING_DERIVATIVE` in `SVM_NODES_ANY/DOT/TRANSFORM`, the 13 census `OP` rows, and the object-with-transform tex-coord check for the derivative opcode. E3's terminator names and M3's tables are unchanged. |
| `native/cycles/svm_world_gate.cpp` | clean | World 22, the opcode-size rows and `WORLD_COUNT=23`; the hardening line is kept. |
| `native/cycles/svm_surface_gate.cpp` | 1 conflict | Wave 2's probe line kept: `std::ofstream(prefix+".wgsl")<<gate_probe_wgsl(d,source);auto shader=pipeline(d,source);`. M4's `CODEBRIX_M4_PROBE_FEATURES` hook was dropped. M4's 110 typed programs and the set-bump programs all dispatch through `evaluate()`, which calls `refresh_probe` before its dispatch, so programs uploaded after probe creation get a module rebuilt for their features. M4 added no direct `d.dispatch`. The hardening line is kept. |
| `native/shaders/wavefront_svm.wgsl` | clean | E2's module-scope `svm_stack` (accessors, mask clear) and M3's cases kept. M4's size-switch cases, dispatch cases and the shared tex-coord call site (`//#if SVM_NODE_TEX_COORD_DERIVATIVE else ...`, a form that `specialize_wgsl` supports) are added. Neither M4 file touches the stack directly: every M4 helper goes through `svm_load*/svm_store*` on `ptr<function,SvmState>`, so E2's stack move applies to them unchanged. |
| `native/tools/build_cycles.py` | clean | `wavefront_svm_derivatives.wgsl` appended to the SVM module list; X2's AST rename and E3's terminator module are kept. No other build list names the SVM shaders. |

## Wave 2's probe specialization replaces M4's probe hook
- `svm_surface_gate.cpp`: the hook is removed (above).
- `nodes_gate.py`: `PROBE_FEATURES` and the `CODEBRIX_M4_PROBE_FEATURES` environment are removed. The nodes_svm probe
  now uses the scene-specialized module (`gate_probe_wgsl`, `refresh_probe`).
- **Spec-off runs (Jeremy, 17:52, option 1).** With M4's regions the unspecialized modules exceed what Intel Mesa
  25.0.7 handles reliably. `nodes_gate.py` therefore takes `--unspecialized KINDS`, with default `nodes_world`: only
  the listed kinds also run with `CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0` and are compared on/off. A comment in the
  script, plus the status scope string and `unspecializedKinds` in the report, record which kinds ran. No check is
  relaxed; a kind that runs unspecialized must pass and match bit for bit.
- `unspecialized_compile_smoke.py`: the usage line now names `--unspecialized nodes_render` as the way to obtain the
  module. The tool is otherwise unchanged and stays warning-only.

## E4 and the materials code
E4's single-owner files were applied byte-exact, so `wavefront_shader_data.wgsl` (rounded `P` from u/v, oracle-grouped
`surface_transform_point/direction`), `surface.cpp`, `lighting.cpp` and `wavefront.h` carry E4's forms. M3's and M4's
SVM code reaches ShaderData only through those functions (`surface_object_point` and friends). Neither M lane edits
these files.

## Docs
- **DEVICE.md.**
  - E5 conflict: two independent insertions before the open items (wave 2's Principled and materials sections, and
    E5's sampler section); both kept, wave 2's first.
  - E4 conflict 1: the same, with E4's section after E5's.
  - E4 conflict 2: the head of the open-items list, written by hand (`docs/dev-e4-c2.txt`): the resolved list gains
    E5's and E4's bullets; item 1 combines E4's Samsung sampler result with E5's Pixel one; item 2 is E4's
    "resolved"; item 3 is E5's GPU-reset item without the stale "mode 5 not reached" sentence.
  - M4 merged cleanly (its bump/derivative section).
  - Consolidated after validation.
- **PARITY.md.**
  - E5: the Material row kept from main, the Lighting row taken from E5.
  - E4: E4's Lighting row with E5's Pixel sampler sentence.
  - M4 conflict 1 (Surface SVM row): M4's text up to "bitwise repeat", then main's specialization/device sentences.
  - M4 conflict 2 (Material row): main's row plus M4's bump sentence (without the probe-mask clause); "derivative
    variants, bump/" removed from the deferred list. Lighting row kept from the E4 merge.
  - Consolidated after validation.

## Final feature-bit table (`native/cycles/pipelines.h`)

| Bits | Names | Lane |
| --- | --- | --- |
| 0-29 | base table (`SF_GEOMETRY` ... `SF_LIGHT_TRIANGLE`) | base |
| 30, 31 | `SF_TERMINATOR_GEOMETRY`, `SF_TERMINATOR_SHADING` | E3 |
| 32-34 | `SF_BSDF_PRINCIPLED`, `SF_BSDF_THIN_FILM`, `SF_BSDF_THIN_WALL` | M1 |
| 35-52 | `SF_TEX_NOISE` ... `SF_TEX_VORONOI` (18 bits) | M2 |
| 53, 54 | `SF_WAVELENGTH`, `SF_WIREFRAME` | M3 |
| 55, 56 | `SF_BUMP`, `SF_BUMP_STATE` | M4 |
| 57-61 | `SF_GEOMETRY_DERIVATIVE`, `SF_TEX_COORD_DERIVATIVE`, `SF_VALUE_DERIVATIVE`, `SF_VECTOR_MATH_DERIVATIVE`, `SF_MAPPING_DERIVATIVE` | M4 |
| mask | `SF_ALL=(1ull<<62)-1` (contiguous 0-61); composites `SF_PRINCIPLED_ALL`=32-34, `SVM_DERIVATIVE_ANY`=57-61 | |
| free | 62, 63 only. The next lane that needs more than two bits must widen the 64-bit mask. | |

The tier table is untouched.

## Hardening
Every gate source that this wave touched keeps `require_webgpu_device`: `emitter_sample_gate.cpp`,
`lighting_gate.cpp`, `svm_surface_gate.cpp` and `svm_world_gate.cpp`.

## Final hashes of the merged shared files (sha256, first 16)
`pipelines.h` 3bf7a02af9cfb491, `pipelines.cpp` 122cce180b77b1a7, `svm_surface_gate.cpp` 43842921e59c96f3,
`svm_world_gate.cpp` 2ea105d55cef052d, `wavefront_svm.wgsl` 2a37a987614be345, `build_cycles.py` e15d815e0b53d09d,
`nodes_gate.py` 16fdd6bb48c814bd, `emitter_sample_gate.cpp` 5ca72c7764ed3fa6,
`unspecialized_compile_smoke.py` 4f05ad7c881d1224.
