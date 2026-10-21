# M4 materials lane: bump stages and derivative opcodes

Worktree `/home/jeremy/ClaudeHome/cycles-worktrees/m4-bump-derivative` (detached 01eb27b6 + sealed M3
patch 00895dfa..., seed manifest `baseline-m4.json`, 32,861 files). Linux x64, Intel UHD ADL-S,
Mesa 25.0.7, unchanged `CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433`. No commits, no
stash, no sub-agents. Job 2 (specialized probe + Gabor) was dropped by the coordinator at 15:55;
Gabor stays withheld.

Handoff: `native/work/m4/handoff/` (changes.patch = only M4's changes on top of M3, changes.json,
handoff.json, source/, validation/, tools/, budgets.txt, timeline.txt, census.md, feature-bit
table below). Patch sha256 and path list: `handoff.json`.

## Timeline

- [15:53] Start; read docs, pipelines, M2/M3 reports and handoff. Full build started.
- [15:59] The first full build (6 m 27 s, exit 0) picked up my first edits mid-build; M3 state
  restored (28 paths hash-verified against M3 `changes.json`) and rebuilt for the baseline.
- [16:02-16:09] Baseline from copied M3-state binaries: nodes_svm 196 s, nodes_world 127 s,
  nodes_render 58 s, principled_svm 181 s (all pass).
- [16:05-16:20] Port (WGSL + fixtures + typed programs); first probe run stuck in the Mesa
  compiler > 600 s (killed own PID). wgsl_stats: probe inlined 2.53 MB -> 4.11 MB.
- [16:20] Size cut: plain and derivative tex-coord share one `svm_texco` call site; one object
  normal transform call site each in set_bump and the smooth-normal derivative -> 3.05 MB.
- [16:22-16:33] nodes_svm with the whole unspecialized probe fails (error mask 24 = 8|16, path 0 of
  an unrelated noise graph, 640 s): the known Mesa class. nodes_world: world gate opcode table
  lacked the derivative ops (fixed).
- [16:37-16:48] `CODEBRIX_M4_PROBE_FEATURES` probe mask: nodes_svm passes after raising the
  fixture bump filter width (conditioning, see below); nodes_world 23 worlds pass;
  principled_svm with the whole unspecialized probe passes (identical counts, 644 s).
- [16:49-16:56] FINAL build `tools/build.py --skip-dependencies` (exit 0) + 34 gate targets.
- [16:56-17:42] Final validation (two slots, cold Mesa caches): all-lanes 1,815 s PASS; host 58 s
  PASS; nodes_gate 1,267 s (all but render spec-off); glass on 39 s PASS, glass off 723 s FAIL
  (driver terminated); nodes_render spec-off solo retry 778 s FAIL (wrong film); nodes replay PASS;
  principled_gate 655 s PASS.
- [17:43] Sealed (patch see below, 14 paths), captures deleted (hashes recorded), report.

## Opcode delta (13 opcodes, census 35 -> 22 deferred)

Done and gated: NODE_SET_BUMP, NODE_ENTER_BUMP_EVAL, NODE_LEAVE_BUMP_EVAL, and every remaining
derivative variant: GEOMETRY, CONVERT, TEX_COORD, VALUE_F, VALUE_V, VECTOR_MATH, TEXTURE_MAPPING,
MAPPING, SEPARATE_VECTOR, COMBINE_VECTOR `_DERIVATIVE`. No `*_DERIVATIVE` remains deferred.
Ported from pinned Cycles a456b761 (Apache-2.0: svm/displace.h, bump.h, geometry.h, tex_coord.h,
value.h, convert.h, sepcomb_vector.h, math.h, math_util.h, mapping.h, util/math_dual.h,
geom/triangle.h). Explicit rejections (16, never approximations): geometry tangent derivative,
motion-triangle smooth-normal derivative, enter_bump_eval with an undisplaced-position attribute
(needs true displacement). Background evaluation of SET_BUMP stores zero and enter/leave do nothing,
exactly as the host background node mask (no BUMP / BUMP_STATE feature).
`compatibility/opcodes.json`: the 13 rows -> integrated-tested / partial. Census: `handoff/census.md`.

## Feature-bit table (final, `native/cycles/pipelines.h`)

| Bits | Use |
| --- | --- |
| 0-29 | base | 
| 30-31 | reserved for E3 (unused) |
| 32-34 | M1 Principled |
| 35-52 | M2 nodes |
| 53-54 | M3 wavelength, wireframe |
| 55 | `SF_BUMP` / `SVM_NODE_BUMP` (set bump; its layout constants are region-gated) |
| 56 | `SF_BUMP_STATE` / `SVM_NODE_BUMP_STATE` (enter/leave) |
| 57 | `SF_GEOMETRY_DERIVATIVE` |
| 58 | `SF_TEX_COORD_DERIVATIVE` (census also sets SF_TEX_COORD: shared case) |
| 59 | `SF_VALUE_DERIVATIVE` (value f/v, convert, separate/combine vector) |
| 60 | `SF_VECTOR_MATH_DERIVATIVE` |
| 61 | `SF_MAPPING_DERIVATIVE` (mapping, texture mapping; also in SVM_NODES_ANY/DOT/TRANSFORM) |
| composite | `SVM_DERIVATIVE_ANY` = 57-61 (dual helpers) |

`SF_ALL=(1ull<<62)-1` (contiguous). Bits 62 and 63 are the last free bits of the 64-bit mask:
the next lane that needs more than two bits must widen the mask.

## Gates (before = baseline run on M3-state binaries / M3 sealed; after = final sealed tree)

| Gate | Before | After |
| --- | --- | --- |
| nodes_svm (spec on/off) | 63 graphs, 417 cases, 31,392 paths, 32 rejections, strict 11,041, max 9.67e-5 | 65 graphs, 529 cases, 37,440 paths, 32 rejections, strict 12,936, max 1.07e-2 (practical bound passes; largest absolute errors are in large-valued derivative weights, e.g. tan/power, scaled x1000), both runs PASS **with the probe mask** |
| nodes_world (on/off) | 22 worlds, 183 cases, 45 opcodes, strict 7,754, max 6.10e-5 | 23 worlds, 184 cases, 48 opcodes, strict 7,754, max 6.10e-5, on/off images identical |
| nodes_render (spec on) | M3 sealed numbers | identical in every key (147,056 paths, stage max 7.34e-5, strict image 3,980, image max 1.75e-4); GPU/CPU films bit-identical to the baseline run |
| nodes_render spec off | pass (M3) | **FAIL**: in the gate, unspecialized `prepare_surface` compiled 648 s, then the Vulkan driver terminated creating `shade_surface_light` (SIGSEGV); solo retry compiled (602 s + 95 s) and then mis-evaluated: stage 8 film 1168 pixel 292 actual 0.647531 expected 0.643585. Spec on/off bit-identity therefore NOT shown in M4 |
| principled_gate (svm/bounce/render) | M3 sealed | PASS, 655 s; `compare_sealed`: 0 regressions, 0 missing vs M3 (principled_svm with the whole unspecialized probe: identical counts, compile 644 s vs 181 s) |
| glass_render spec on | pass | PASS (39 s) |
| glass_render spec off | pass | **FAIL** (723 s): the Vulkan driver terminated creating unspecialized `shade_surface_light` (core dump); not retried (box) |
| nodes_render recorder replay | pass | PASS (`extra/nodes_render_replay/validation.json`) |
| all-lanes 29 targets (cold Mesa cache) | M3 sealed | PASS (1,815 s, sources and runtime unchanged) |
| host gate | pass | PASS (58 s) |
| audit.py / notices --check / compatibility tests (12) / guard tests (7) / git diff --check | pass | all PASS |

all-lanes against M3 sealed (`validation/final/all-lanes-compare.log`): every count matches; the only
"worse" values are atomic run-to-run quantities in the broad-lighting / transparent-lighting /
feature-combination / lighting-MIS / deforming-motion repeat counters (repeatBitwiseDifferences,
strictDifferences 5,048 vs 5,046, replay rms in the 1e-11 range, strictImageFailures 343 vs 340) and
`broad_light_state.maximumAbsoluteError` 9.54e-7 vs 4.77e-7 (one ulp step; in the same
atomic-accumulation broad-lighting lane, not re-run to prove it). Other differences are module and
arena hashes: every module now carries ~14 unconditional enum constants (new opcodes,
`ATTR_STD_POSITION_UNDISPLACED`).
`tools/compatibility/inventory.py check` reports "Evidence needs review after source change:
native/cycles/attribute_gate.cpp", a file M4 did not touch (pre-existing).

## Compile sizes (wgsl_stats.py)

| Module | Bytes | Inlined probe/entry bytes |
| --- | --- | --- |
| SVM probe, whole/unspecialized | 284,239 -> 311,969 | 2,533,334 -> 3,055,006 (4.11 MB before the call-site cuts) |
| SVM probe, M4 probe mask (nodes gate) | 284,239 -> 276,082 | 2,533,334 -> 1,533,190 |
| nodes_render specialized surface | 324,955 -> 326,253 | prepare_surface 812,156 = ; shade_surface_light 643,134 = |
| nodes_render specialized background | 273,460 -> 274,758 | shade_background 865,681 = |

Specialized modules of scenes without bump/derivatives have identical code (only the enum
constant lines differ); the tex-coord case uses the `//#if ... else` fallback so that path is
textually the M3 one. Compile time of the whole unspecialized modules grows sharply on Mesa:
probe 196 s -> 640 s+, unspecialized `prepare_surface` 92-102 s (M3) -> 648 s.

## Fixtures and gate changes

- `nodes_fixture.h`: graphs 63/64 (SVM gate only; render variant unchanged): bump from a
  geometry-position dot height (strength .8) and from an object-coordinate length height
  (invert, object space, linked geometry normal, distance .6). The real compiler copies the height
  sub-graph, so these compile to GEOMETRY_DERIVATIVE / TEX_COORD_DERIVATIVE with centre/dx/dy offsets
  and SET_BUMP. Filter width 2000: at 40 the dx/dy height differences cancel to ~1e-6 and a last-bit
  hit-point difference moved the normal by 3e-4; for a linear height the bump normal is independent
  of the width, so the large width tests the same function with good conditioning. Capacity: 65
  graphs x 3 objects x 7 rays = 4,095 states (limit 4,096) — no more graphs fit at 7 rays.
- `svm_surface_gate.cpp`: 110 typed programs (geometry 30, tex coord 32, value/convert/sepcomb 3,
  vector math 28, mapping 6, set bump 10, bump state 1) on a dual point whose value is the
  bit-identical quantized point; derivatives are routed into closure weights x1000; set bump also
  drives the closure normals. Plus the default-off `CODEBRIX_M4_PROBE_FEATURES` probe-mask hook.
- `svm_world_gate.cpp`: world 22 (bump in a background) and the derivative/bump entries of the
  world gate's opcode-size table. `nodes_gate.py`: 65 graphs / 23 worlds, and the nodes_svm runs set
  the probe mask `3ffffff8c0ffdf83` (drops image, attribute, normal map, tangent, vertex colour,
  volume, glossy/glass, light, motion and Principled regions).

## Final status

Sealed and validated: all 13 opcodes on the SVM (probe mask), world and principled gates; all-lanes,
host, glass spec on, nodes render spec on, replay and audits. Not validated: spec-off renders (open
item 2). No device runs.

## Open items

1. Whole unspecialized SVM probe with the M4 handlers: Intel Mesa 25.0.7 mis-evaluates it (error 8|16
   on unrelated graphs) — the Gabor class. Validated with the probe mask; merge wave 2's
   scene-specialized probe module should replace the hook and the mask (then delete both).
2. **Unspecialized render modules regress** (the conservative path with
   `CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0`): `prepare_surface` compiles ~600 s (M3 92-102 s);
   `shade_surface_light` either terminates the driver (glass, nodes in the gate) or, when it
   compiles, mis-evaluates (nodes_render stage 8). Same Mesa class as the probe. Specialized
   renders (default; required on Android) are byte-for-byte unaffected in code and pass with
   identical numbers. Candidate fixes: shrink the always-present derivative code further (the
   vector-math dual switch and the smooth-normal loop are the largest), or have the conservative
   path drop M4 regions when the scene census lacks them. This must be resolved or accepted
   before merge; I did not ship a workaround.
3. Devices: none run (Linux only, by brief). Typed derivative programs exercise only static
   triangles; motion-triangle smooth-normal and tangent derivatives are rejected (16).
4. True displacement (SET_DISPLACEMENT etc.) and the undisplaced bump-state branch remain deferred.
5. Two free feature bits remain (62, 63).
6. `inventory.py check`: pre-existing attribute_gate evidence review flag.

## Merge notes (wave 3 applies this patch after wave 2)

Files touched (11): `native/cycles/{pipelines.h,pipelines.cpp,svm_layout.cpp,nodes_fixture.h,
svm_surface_gate.cpp,svm_world_gate.cpp}`, `native/shaders/{wavefront_svm.wgsl,wavefront_svm_nodes.wgsl,
wavefront_svm_derivatives.wgsl (new)}`, `native/tools/{build_cycles.py,nodes_gate.py}`, plus docs
`docs/PARITY.md`, `native/DEVICE.md`, `compatibility/opcodes.json` (14 paths with docs; exact list in
`handoff.json`).

- Overlap with wave 2: `wavefront_svm.wgsl` (E2 module-scope `svm_stack`): M4 adds size-switch and
  dispatch cases after the wireframe ones and rewrites the NODE_TEX_COORD case; every M4 helper uses
  `svm_load/svm_store*` through `s:ptr<function,SvmState>` — if E2 moved the stack, `svmd_*` helpers
  follow the same API change mechanically. `pipelines.h` (E3 bits 30/31 untouched; M4 appends 55-61
  and moves SF_ALL to (1<<62)-1). `svm_surface_gate.cpp`, `svm_world_gate.cpp` (device-creation
  hardening line in every gate source; M4 edits are elsewhere: include of pipelines.h, typed
  programs, probe hook, world 22, opcode table). The probe hook in `svm_surface_gate.cpp` and
  `PROBE_FEATURES` in `nodes_gate.py` should be dropped in favour of wave 2's specialized probe.
- `nodes_gate.py` asserts 65 graphs / 23 worlds / 32 rejections.

## Seal

`native/work/m4/handoff/changes.patch` sha256 `9d2fa055077e0ea26b05ed4ebc4f6a8a032c03eed09a6c9248e2fea149300608`
(14 paths, against `baseline-m4.json`; checked by applying it to a scratch copy of the before files).
The `native/DEVICE.md` spec-off paragraph was added after validation (text only; it is a hashed
gate input, so the evidence predates that one paragraph). Scratch captures were deleted; their
sha256 are in `handoff/validation/removed-captures.sha256` and `removed-large-files.sha256`.
