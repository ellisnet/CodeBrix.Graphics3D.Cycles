# M3 materials: M1+M2 reconciliation and procedural opcodes

Worktree `/home/jeremy/ClaudeHome/cycles-worktrees/m3-procedural` (clean `01eb27b6`, seed
`native/work/materials-seed.json`). Linux x64 / Intel UHD ADL-S, Mesa 25.0.7 only; no Android
device used. No commit, push, stash, ref or index change (the reconcile used `git apply` /
`git apply --reject`; seal diffs new files with `--no-index`). Runtime
`CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433` unchanged. THIRD-PARTY notices unchanged
(no new third-party code; the CIE rows and kernel bodies come from the pinned Apache-2.0 Cycles
snapshot already listed).

Sealed handoff: `native/work/m3/handoff/` — `changes.patch` sha256
`00895dfae46f32f4f9a406fb02b6997739dbcabdd18426d3e44c39f0fd6d53a7` (28 paths, the combined
M1+M2+M3 result against `01eb27b6`; verified to apply to a pristine `git archive HEAD` export and
reproduce every file hash in `changes.json`), `changes.json`, `handoff.json` (patch sha256, M1/M2
input hashes, seed manifest), `source/`, `validation/` (`final/`, `job1-interim/`, `job2-dev/`),
`tools/`, `budgets.txt`, `timeline.txt`, `reconciliation.md`, `census.md`, `unshipped/`.

## Result in one paragraph

Both sealed patches (hashes verified) were combined; five files had rejected M2 hunks, all
resolved by hand (table below). Bits 30/31 are reserved for E3; M1 is now 32-34, M2 35-52,
`SF_ALL=(1<<53)-1` at the interim seal. On the combined tree every M1 and M2 gate reproduces the
sealed numbers exactly (principled gate 108/108 fields, nodes gate all fields plus the M2 film and
world-image hashes, glass `a8e69a34`, Principled `6fa85969` on/off, replay); the 29-target corpus
passes, differing only in atomic-film-order counters that also differ between the two sealed runs.
Job 2 added `NODE_WAVELENGTH` (bit 53) and `NODE_WIREFRAME` (bit 54), gated against the unmodified
CPU kernels. `NODE_TEX_GABOR` was ported, passed the node SVM/world/render gates, and is
**withheld**: with its code present, the unspecialized SVM probe module (which every SVM gate
compiles whole) makes the Intel driver mis-evaluate Principled paths (`principled_svm` fails; a
stubbed Gabor body passes). Radial tiling not reached; AOV deferred (new pass path).

## Timeline

- [11:20] Start; read AGENTS/MAINTAINER, DEVICE/PARITY, pipelines, both reports and handoffs.
- [11:23] Hashes verified; M1 applied clean; M2 `--reject`: 5 files hand-resolved; bits renumbered.
- [11:23-11:30] Full build `tools/build.py --jobs 6 --skip-dependencies` (6 m 57 s), gate targets (5 m 37 s).
- [11:34-12:14] Job-1 runs: principled_gate 253 s, nodes_gate 908 s, all-lanes 1302 s, principled_render 59 s, spec-off 306 s, replay, glass 31 s, glass spec-off (crash 231 s, retry 324 s), svm_surface 146 s, broad/transparent lighting repeats 35-43 s, audits.
- [12:15] Job-1 interim seal (`validation/job1-interim/j1-interim-seal.json`, tracked diff sha256 `593153e4...`).
- [12:15-12:42] Job 2 applied (apply script in `unshipped/apply_job2.py`); world-gate opcode table and fixture capacity fixed; 3D Gabor needed precise acos/sin/cos (driver error up to 2e-3 relative).
- [12:42-13:00] Render-gate diagnosis: a 68-triangle render scene fails one pixel at 1.25x (see open items); render scene kept at 60 triangles.
- [13:00-13:15] Gabor compile cost cut (one sincos, hash and acos call site): prepare_surface 61 s -> 33 s.
- [13:17-13:55] Final run: `principled_svm` fails with Gabor present (stack error 8, then device lost / wrong closure in variants); stub test confirms; Gabor withheld.
- [14:00-14:38] Final build and full final validation on the shipped tree (all pass).
- [14:39-14:50] Audits, seal, report.

## Reconciliation

| File | Conflict | Resolution |
| --- | --- | --- |
| `native/cycles/pipelines.h` | both appended bits at 30 | 30/31 reserved (comment), M1 32-34, M2 35-52, then M3 53-54; `SF_ALL` contiguous |
| `native/cycles/pipelines.cpp` | M2 names/composites and ramp-table census anchored on pre-M1 lines | names before M1's microfacet/Principled entries; table walk after M1's Principled census block |
| `native/cycles/surface_fixture.h` | M2 graph count and `nodes_material` call | placed beside M1's Principled include/call (exclusive defines) |
| `native/cycles/svm_surface_gate.cpp` | shared rejection 0/1 lines, `node_rejections`, its call | both rejection changes kept; function before `main`; call right after `rejections()` |
| `docs/PARITY.md` | both rewrote "Material coverage" | M1 row + M2 sentences; M2 "Surface SVM evaluation" row |
| `native/DEVICE.md` | clean apply | bit numbers corrected to the final table |

Final feature-bit table:

| Bits | Names | Lane |
| --- | --- | --- |
| 0-29 | base table | base |
| 30, 31 | unused, reserved | E3 (shadow terminator) |
| 32, 33, 34 | `SVM_BSDF_PRINCIPLED`, `_THIN_FILM`, `_THIN_WALL` | M1 (was 30-32) |
| 35-52 | `SVM_NODE_TEX_NOISE`, `_TEX_WHITE_NOISE`, `_TEX_WAVE`, `_TEX_MAGIC`, `_TEX_BRICK`, `_MAPPING`, `_MAP_RANGE`, `_MIX`, `_HSV`, `_COLOR_SEPCOMB`, `_RAMP`, `_FRESNEL`, `_VECTOR_ROTATE`, `_BLACKBODY`, `_NORMAL`, `_SHADING_INFO`, `_LIGHT_PATH`, `_TEX_VORONOI` | M2 (was 30-47) |
| 53, 54 | `SVM_NODE_WAVELENGTH`, `SVM_NODE_WIREFRAME` | M3 |
| mask | `SF_ALL=(1ull<<55)-1`, `SF_PRINCIPLED_ALL=bits 32-34` | |

Specialization resolves region names, so specialized WGSL is independent of the numbering.

## Gate results (before = sealed M1/M2; after = final shipped tree; practical `5e-5+2e-4|ref|`)

| Gate | Before | Interim (job 1) | Final | Wall |
| --- | --- | --- | --- | --- |
| principled_svm | 20 graphs, 237 cases, 19,788 paths, max 9.54e-7, strict 966, 19+3 rejections | identical | identical | in gate 204-253 s |
| principled_bounce | 29 cases, 28,560 paths, max ratio 0.109, strict 7,672 | identical | identical | |
| principled_render | 121,235 paths, image max 8.34e-6, strict 126, film `6fa85969` | identical | identical, spec on/off film `6fa85969` | 59-61 s / off 306-312 s |
| principled replay (v6) | 1,056 commands, PASS | PASS | PASS | |
| principled bench (ForestCommunity) | FAIL stage 30 (sliver triangle, X2 item) | not rerun (no gate target; untouched code) | open | |
| nodes_svm | 60 graphs, 394 cases, 29,715 evals, max 9.67e-5, strict 10,985, 31 rejections | identical | 63 graphs, 417 cases, 31,392 evals, max 9.67e-5, strict 11,041, 32 rejections; spec on/off identical | 908 s / 915 s whole gate |
| nodes_world | 20 worlds, max 6.10e-5, strict 7,754 | identical, image hashes = M2 | 22 worlds, max 6.10e-5, strict 7,754; M2's 20 world images byte-identical | |
| nodes_render | 144 stages, 146,933 paths, stage max 7.73e-5 (0.51), image max 1.75e-4, strict image 4,208 | identical, film = M2 | 147,056 paths, stage max 7.34e-5 (0.25), image max 1.75e-4, strict image 3,980; spec on/off identical | |
| nodes_render replay (v6) | not run by M2 | — | PASS, 1,061 commands | |
| glass_render | film `a8e69a34` on/off | on/off `a8e69a34` (off: first try driver crash under load, retry PASS) | on/off `a8e69a34` | 30 s / off 302 s |
| svm_surface | 23 cases, max 7.63e-6 | identical | identical | 146-147 s |
| all-lanes 29 targets | PASS (M1, M2) | PASS, 2,940 fields match | PASS, 2,949 match | 1,302 s / 1,251 s |
| host gate | (baseline) | deferred to final tree | PASS: 786 reference films, 3,139 comparisons | 53 s (cached builds) |
| audits | PASS | PASS | audit, notices --check, compatibility 12, guards 7, diff --check PASS | |

All-lanes variance (not tolerance changes): only gates with nonzero `repeatBitwiseDifferences`
(atomic film accumulation order) differ. Interim broad_lighting strict 5,049 vs sealed 5,046
(standalone reruns 5,048 and 5,049); final 5,046 (match). Two replay `rmsImageError` values differ
from both sealed runs in the 6th-7th significant digit (3.12580546e-8 vs 3.12577599e-8;
7.2781790e-7 vs 7.2781789e-7); the sealed M1 and M2 values differ from each other at the same
digits and every replay capture hash differs run to run. Recorded, not a stop item: specialized
modules for these scenes are byte-identical to the baseline.

## Opcode census delta

M2 census: 42 + 32 done, 37 deferred. M3: +2 done (`NODE_WAVELENGTH`, `NODE_WIREFRAME`),
35 deferred (`census.md` in the handoff). `compatibility/opcodes.json`: the two rows set to
integrated-tested/partial (same caveat as M2: no evidence checkpoint, generator rule).

- `NODE_WAVELENGTH`: CIE rows as switch constants (no private array, no upload), host truncation
  (incl. the extrapolated (-1,0) row range), zero outside, NaN; typed sweep of 11 incl. both ends,
  graph crossing both table ends, world.
- `NODE_WIREFRAME`: existing vertex tables (motion triangles too), object/motion transform, `dP`
  pixel width, bump offsets; 12 typed programs (pixel/object x centre/dx/dy x constant/per-path),
  two graphs, a world (0 without primitive). Excluded from the render variant (step function of
  the hit point; the independent CPU image differs in the last bits).
- `NODE_TEX_GABOR`: withheld (above). Candidate WGSL (precise-trig version that passed all three
  node gates, and a smaller variant) in `handoff/unshipped/`.
- Deferred by rule: derivatives, bump, displacement, volume, light falloff/IES, sky, particle/hair/
  point info, AO/bevel/raycast; AOV (needs render-buffer writes from the SVM in prepare_surface and
  AOV pass allocation/readout); radial tiling (meets the rule, ~900-line piecewise kernel, time).

## Compile sizes (wgsl_stats.py inlined bytes; Linux cold compile)

| Module / entry | Interim (M1+M2) | Final | Gabor attempt (withheld) |
| --- | --- | --- | --- |
| glass specialized (all entries) | 240,514 B module, byte sizes = baseline | identical | — |
| node render, specialized prepare_surface | 795,248 inl., 21.1 s | 812,156 inl., 20.3 s | 1,100,167 inl., 33 s (61 s before call-site work) |
| node render, specialized shade_background | 848,773 inl., 12.3 s | 865,681 inl., 11.9 s | 1,153,692, 21.6 s |
| unspecialized prepare_surface | 2,494,549 inl., 148.9 s | 2,560,597 inl., 149.1 s | — |
| unspecialized shade_background | 2,547,998, 22.0 s | 2,614,046, 24.8 s | — |
| unspecialized shade_surface_light | 3,949,428, 103 s | unchanged, 104 s | — |

The combined M1+M2 unspecialized `prepare_surface` (2.49 MB) is above B3's 2.3 MB Intel Android
danger zone; M1 (1.91 MB) and M2 (1.88 MB) were each below it.

## Open items

1. **Gabor withheld** (driver finding): any of three Gabor bodies makes `principled_svm` fail on
   the unspecialized probe module (error 8 / wrong `closures[2].multiscatter` / device lost); stub
   passes; node gates pass. Needs a decision: specialize the SVM probe modules in the gates, or a
   much smaller Gabor, or a Mesa issue.
2. Unspecialized `shade_surface_light` compile occasionally kills the Intel Vulkan driver under
   concurrent load (glass spec-off and nodes_render spec-off once each; reruns pass).
3. A 68-triangle node render scene failed one pixel at 1.25x practical (2.8e-4 on 1.07,
   independent CPU image; stages pass; persists with all new graphs off-screen); root cause not
   isolated (M2 graph at a deep bounce or new emitter). Render scene kept at 60 triangles.
4. Principled ForestCommunity bench stage-30 sliver-triangle intersection failure remains open
   (X2; intersection code untouched).
5. Radial tiling not ported; AOV needs a pass path; device validation (Android/ARM) of all M1/M2/M3
   work pending at merge.

## Merge notes (hand-reconcile with E1/E3/X1/X2)

- `native/cycles/pipelines.h/.cpp`: bits 30/31 reserved for E3 (just name them; `SF_ALL` already
  covers them); new lanes append at 55+. Census changes in `svm_census` (Principled roots, ramp tables).
- `native/shaders/wavefront_svm.wgsl` (size/dispatch switches), `wavefront_svm_nodes.wgsl`,
  `wavefront_svm_closures.wgsl`, `wavefront_svm_microfacet.wgsl`, `wavefront_surface.wgsl`,
  `wavefront_microfacet.wgsl`; `svm_layout.cpp` regions.
- `native/DEVICE.md` (M2 section mid-file with the M3 subsection, M1 section near the end),
  `docs/PARITY.md` (Surface SVM and Material coverage rows), `compatibility/opcodes.json`.
- `native/tools/inject_cycles.cmake` (principled and nodes target loops), `build_cycles.py`.
- Notices: unchanged by M1/M2/M3.
