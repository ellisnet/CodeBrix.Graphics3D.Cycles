# Phase E4: triangle image mode 4 (static instanced emitters) on the Adreno 730/640

Agent: E4 (Opus 5.5). Worktree `/home/jeremy/ClaudeHome/cycles-worktrees/e4-mode4-adreno` (detached at 01eb27b6,
seeded from `baseline-merge1.json`). Devices: Samsung SM-X700 `R52W207XQ1V` for the lane, Pixel 4 XL
`98271FFBA0048D` for the final confirmation. Runtime unchanged (CodeBrix.WebGpu.Native.MitLicenseForever
1.0.290.1433; android-arm64 `libcodebrix_wgpu.so` sha256 `8113cbfa...`, verified on the device before every
corpus). Nothing was committed; main and the other worktrees were not modified. Work folder:
`native/work/e4/` (tools, probe, captures, variants, runs, linux, handoff).

## Result in brief

**Mode 4 now passes on the Samsung**: 0 practical differences against the glibc reference (11 strict, max
3.73e-7; it was 9 practical, max 0.0127), all six images and all six on-device replays pass, and the Linux
mode-4 capture replays on the Adreno (415 readbacks, max 3.73e-7; it diverged at command 438). Two causes, both
fixed:

1. **Contractible ShaderData arithmetic** (cause 3 of the brief: an error in the instanced path that Linux/Intel
   rounded over). `prepare_shader_data` computed `P=(a+u*dPdu)+v*dPdv` and the static object transform with plain
   WGSL arithmetic. Intel Mesa fuses those products into the sums; the Adreno did not (its result equals the
   unfused CPU oracle). The transform also grouped the translation differently from the oracle. One ULP of `P.z`
   decided whether a light sample exactly on a vertex of the instanced emitter hit it.
2. **Android ARM64 emitted its own scalar oracle's arithmetic adapters** (cause 1). The Android ARM64 build is a
   scalar build (layout: `CAMERA_NATIVE_VECTOR_DIVIDE` 0, `NATIVE_FLOAT3_RECIPROCAL_NORMALIZE` 1,
   `NATIVE_TRIANGLE_ARITHMETIC` **0**, not 2 as recorded in the merge report), so the Adreno reproduced the
   device's own ARM64 CPU film (component 312: GPU 0.0646473 = device CPU 0.0646473; glibc 0.0658437) while it is
   judged against the Linux x64 glibc reference.

Cause 2 (the Adreno 640 array defect class) was tested and excluded: rewriting the triangle-light cache and key
arrays did not change the replay.

## Timeline
- [14:40] Samsung lock taken; read the merge-wave, X2, E1, X1 and E2 reports; copied main's android-arm64
  dependency install prefix (read-only) and E2's probe tools.
- [14:41-14:58] Seed builds: Linux (`tools/build.py --jobs 6`), Linux gate targets, Android arm64 triangle gates;
  probe built for both (same runtime hash).
- [15:00] Linux seed triangle gate: PASS; captures/films bit-identical to merge wave 1's.
- [15:01] Samsung replay of the Linux mode-4 capture: diverges at command 438 (343 vs 342), as in merge wave 1.
- [15:02-15:05] Arena dumps after 432-437 and injection of the Linux arena after 432: the lost shadow ray is
  path 127; `prepare_shader_data` already differs by 1-2 ULP with identical inputs.
- [15:06-15:09] Array variants (cache, keys, both): pass on Linux, still diverge on the Adreno: not the E2 class.
- [15:10-15:13] Debug variants of the light sampler: vertex-exact sample on the instanced emitter, hit/miss decided
  by `sd.P.z`.
- [15:14-15:18] Exact-`P` / exact-transform variants: on Linux the exact `P` itself diverges at 438 (Intel fuses);
  after injection the Adreno's `P` equals Linux's bit for bit. Root cause 1 localised.
- [15:18-15:26] Candidate 1 (ShaderData) built; Linux triangle gate: mode 4 77 -> 10 strict; all 6 Linux captures
  replay on the Samsung (tri4 included).
- [15:26-15:34] Samsung native: mode 4 unchanged and bit-identical to merge wave 1 (candidate 1 and candidate 2,
  `NATIVE_TRIANGLE_ARITHMETIC` on the SSE2NEON branch).
- [15:35-15:43] A device capture (diagnostic self-reference run) shows the ARM64 layout is the scalar oracle's
  (mode 0, reciprocal normalize). Root cause 2.
- [15:44-15:57] Candidate 3 (x64 oracle adapters when importing the reference): Samsung mode 4 PASS natively;
  sampler 41 -> 2 practical; replay-4 failed only against the capture's bionic CPU film -> runner judges replays
  against the glibc reference.
- [16:00-16:22] Clean Linux build and full Android build of the final candidate; Linux slots and the Samsung chain
  started.
- [16:39-16:45] Samsung `glass_render` lockstep stage check failed against the local CPU (process-wide oracle
  switch) -> chain stopped (exact PIDs), selection scoped to reference-only gates (candidate 4, final).
- [16:46-17:46] Android rebuild + Samsung chain on the final build; Linux rebuild (slow under the shared load).
- [17:46] Pixel lock attempt failed (held by the materials merge wave) but my compound command still pushed my
  own staging folder to the Pixel and ran one read-only `ps`; recorded as a protocol incident; waited for the lock.
- [17:50-17:55] Doc edits during validation tripped the source pins (host gate, all-lanes deforming, Samsung
  chain 2); docs restored to the seed, edits re-applied after validation.
- [17:56-18:12] Samsung remaining corpora PASS (triangle images + 6 replays, regression, emitter, precision,
  spec-refusal); Samsung cleaned, lock released.
- [17:56] Pixel lock taken; Pixel triangle confirmation started.
- [18:21] Pixel: all six triangle images PASS natively (mode 4 and mode 5); replays 0-1 PASS.
- [18:23] Linux all-lanes PASS on the final tree (1,060 s); host gate PASS (rerun); docs re-applied; audits pass.
- [18:25] Scratch arenas deleted (hashes recorded), which broke the local validation of Pixel replays 2-4.
- [18:33] Pixel run stopped (exact PID), Pixel cleaned, lock released; handoff sealed.

## Root cause, with evidence

Method: E2's command-replay probe (`native/work/e4/probe/probe_replay.cpp`, render_replay plus arena dumps,
arena injection and a continue-after-mismatch option), the Linux x64 mode-4 capture regenerated bit-identically to
merge wave 1's (commands, WGSL and films), and per-path/per-record arena tools (`tools/state_fields.py`,
`tools/stage_records.py`, `tools/shadow_keys.py`).

1. **First difference.** Command 438 is the readback of the shading-queue count after the second camera sample's
   first surface stage (`prepare_shader_data` 433, `prepare_surface` 434, `shade_surface_light` 435,
   `connect_surface_light` 436, `shade_surface` 437): 343 on Linux, 342 on the Adreno. The shadow-ray count after
   436 is 343/342; the missing shadow ray is pixel 127, sample 3 (queue id 219, path 127).
2. **Injection.** With the Linux arena injected after command 432, the Adreno's `prepare_shader_data` output still
   differs in 770 words, all 1-2 ULP, among them `sd.P.z` of path 127 (-0.985982597 vs -0.985982656). Vertices,
   `P.x`, `P.y` and all scene inputs are identical.
3. **Amplification.** Debug variants of `shade_surface_light` (`variants/make_debug.py`, staged into the light-sample
   slot of the stage record) show path 127 sampling the instanced emitter (vertices (-2.56,0.9,-1), (-1.44,0.9,-1),
   (-1.44,2.1,-1)) with `dp`=0.9999999 and `z`=1: the sampled direction is vertex B, and `c_ray_triangle_intersect`
   hits on Intel and misses on the Adreno (marker -7 only on the Adreno). Other paths show 1e-4 light-sample
   differences from the same 1-ULP inputs through the catastrophic cancellation `u=sin-ca` of the spherical-triangle
   sampler (CPU behaviour, not a GPU fault).
4. **Variants** (each first replayed on Linux):

   | Variant | Linux replay of the capture | Adreno |
   | --- | --- | --- |
   | `vcache`: triangle-light cache as one private `array<u32,52>` | passes, film = base | diverges at 438 |
   | `vkeys`: key array as four scalars | passes, film = base | diverges at 438 |
   | `vboth` | passes, film = base | diverges at 438 |
   | `p-tfm`: oracle-grouped, contraction-proof object transform | passes (78 strict vs 77) | (not run alone) |
   | `p-point`: contraction-proof `P` from u/v | **diverges at 438 (342)**: Intel was fusing | after injection, `P` equal to Linux |
   | `p-both` | diverges at 438 | after injection, `P` (and every field except the differentials) **bit-identical to Linux** |

   So the capture's expected 343 came from Intel's fused arithmetic, not from the oracle. With the change,
   Linux's own mode-4 film moves toward the CPU (77 -> 10 strict, max 3.31e-5 -> 3.73e-7) and the regenerated
   capture replays on the Adreno.
5. **Native gate after fix 1 only** (candidate 1): the Samsung mode-4 film was bit-identical to merge wave 1's
   (and so were modes 0-3): the Adreno had never fused. Setting `NATIVE_TRIANGLE_ARITHMETIC` alone (candidate 2)
   changed nothing because the ARM64 build never reached that branch. A diagnostic native run that kept its own
   capture (`probe/devcap`, self-reference, diagnostic only) showed the scalar-oracle layout constants. With the x64
   constants (candidate 3) mode 4 passed natively; process-wide selection then broke the Samsung `glass_render`
   lockstep stage check against the local CPU (`path.mis_ray_pdf` 2035.839 vs 2035.207), so the final form
   selects the x64 constants only in gates whose every check uses the imported reference.

A Linux-only diagnostic (`probe/diag_triangle_lighting_gate`, built from a temporary edit of
`reference_policy.h` that was reverted byte-identically at 15:36) tried to render the Samsung-prepared scene on
Intel; it is not usable (the scene is uploaded before the exchange) and is not evidence.

## The fix

- `native/shaders/wavefront_shader_data.wgsl`: `sd.P=rounded_add3(rounded_add3(a,u*dPdu),v*dPdv)` (Cycles'
  `triangle_point_from_uv` order), and `surface_transform_point`/`surface_transform_direction` via
  `surface_transform_row` with the closest-stage instance-push rule (SSE `((w+z)+y)+x`, scalar `(x+y)+(z+w)`,
  every product rounded before its sum). Every module that includes ShaderData gets it (surface, shadow, volume,
  volume-stack, background).
- `native/cycles/wavefront.h`, `camera.cpp`, `surface.cpp`, `lighting.cpp`: the five arithmetic-adapter constants
  come from `reference_oracle_sse()`. SSE builds: true (unchanged). Android scalar builds: true only when
  `CODEBRIX_CYCLES_REFERENCE_ORACLE=linux-x64-sse4.2`, then `NATIVE_TRIANGLE_ARITHMETIC` is 1 (the `RCPSS` model
  verified on the Linux x64 host that computes the reference; integer WGSL). Other builds: false (unchanged).
- `native/cycles/reference_policy.h`: `use_reference_oracle_arithmetic()` sets that variable when
  `CODEBRIX_CYCLES_REFERENCE_IMPORT` is set; called first in `lighting_gate.cpp` and `emitter_sample_gate.cpp`
  `main`. `render_gate.cpp` (lockstep stage checks against the local CPU) and the product are unaffected.
- No feature bit, no tolerance, fixture, CPU kernel, reference or runtime change. No new files in the source tree.
- Docs: `native/DEVICE.md` new section "Static instanced emitters on the Adreno: ShaderData arithmetic and the
  reference oracle (Phase E4, 2026-10-20)", open items 1 and 2 rewritten; `docs/PARITY.md` Lighting row.

## Results

### Linux x64 (cold scratch Mesa cache per gate, `--jobs 6`, at most 2 gates at once)
Before = merge wave 1's validation of the same seed (`main/native/work/merge1/validation`) and this lane's own
seed run (`linux/before-triangle`, bit-identical to merge wave 1's triangle captures and films).
Final tree = `g*`/`h1` runs (after the clean build and the last source change); `f*` runs were on the previous
candidate (same WGSL; the triangle captures and films of `f2` and `g2` are bit-identical). Practical failures: 0
everywhere, no NaN/Inf, no tolerance changed.

| Gate | Before (merge wave 1 / seed) | After (E4 final) | Films |
| --- | --- | --- | --- |
| Clean build `tools/build.py --jobs 6` + all gate targets + host tests | - | rc 0 | |
| triangle gate: images (6 incl. mode 4) | 2,076 strict, max 3.31e-5 | **1,776 strict, max 2.32e-6** | mode 0 identical; modes 1-5 changed, all toward the CPU: per mode strict 9->9, 41->0, 37->14, 446->430, **77->10**, 428->425; mode-4 max 3.31e-5 -> 3.73e-7 |
| triangle gate: sampler, surface, 6 replays | 865 strict / 4.88e-4; replays 289/417/415/473/415/464 | identical | |
| emitter probe (broad lighting gate) | 165 strict, 1.9226e-3 | see all-lanes row | |
| closest static / motion / deform / deform-motion + precision fixture | 0 bit mismatches each; bench ray exact (static) | identical | |
| shadow gate + precision fixture | 0 of 4,096 decision mismatches | identical; microfacet film strict 127->124 | |
| shadow_terminator (10 cases, 10 replays) | 276 strict, 1.53e-5 | 2 identical, 6 better, 2 equal-count | strict 30->22, 25->18, 25->18, 30->22, 9->5, 19->17 |
| object_motion images | 5,520 strict, max 8.34e-6 | 5,469, max 8.34e-6 | 16 changed: 8 better, 4 equal, 4 one strict value more |
| deforming_motion images (+ object regression) | 8,295 strict, max 8.94e-6 | 8,229, max 9.06e-6 | 24 changed: 12 better, 5 equal, 7 worse by 1-4 strict values; one maximum 8.94e-6 -> 9.06e-6 |
| feature_combination | 2,799 strict, max 1.19e-5 | 2,790, max 1.19e-5 | 7 changed: 3 better, 2 equal, case-2 +1, case-5 +2 (its own repeat differs in 5 values) |
| transparent_shadow (+ broad/transparent lighting films) | PASS | PASS | 19 changed: 12 better, 3 equal, 4 worse; broad-lighting cases 7 and 8 (atomic, own repeat differs) maximum 2.1e-5 -> 1.6e-4 and 9.5e-6 -> 2.0e-4 at component 1994 (value 32.04, relative 5e-6; practical bound 6.4e-3) |
| surface gate | PASS | PASS | |
| scene_edit gate | PASS | PASS | |
| glass_render spec on / off | both `a072683c...` | both **`db8e84f9990299d2b2ce1c53a390f56e1d4beb73ec7326aa55da4c1b074802f6`** (bit-identical on/off); strict vs CPU 112 -> 109, max 9.54e-7 unchanged | |
| host gate (48 exports) | 3,142 comparisons, max 1.19e-7 | PASS (rerun after the doc-pin incident): 3,142 comparisons, max 1.19e-7 | |
| `dotnet test` | 162/162 | 162/162 passed (Release, cold Mesa cache) | |
| all-lanes (29 targets + binary audit) | PASS | **PASS** (1,060 s / 2,700 s; 8 gate groups + binary audit, exit codes all 0). Against merge wave 1: 78 films, 2 identical, 76 changed (50 better, 12 equal, 14 worse by a few strict values); total strict 36,201 -> 33,460; practical 0 -> 0; overall maximum 5.24e-4 unchanged (`linux/h1-vs-merge1.txt`) | |
| audits (`audit.py`, `notices.py --check`, guard tests, `git diff --check`) | pass | `audit.py` rc 0, `notices.py --check` rc 0 (34 reviewed components), guard tests OK, `git diff --check` clean (after the docs were re-applied) | |

"Worse" rows are single strict values crossing 1e-7 relative in multi-sample films; every maximum except the two
broad-lighting cases and one deforming case is unchanged or lower. Like X2's traversal change, this is a CPU-order
fix: the GPU now evaluates ShaderData `P` exactly as the unfused CPU oracle, which moved the triangle images and the
terminator films substantially toward the CPU; the remaining small increases are downstream reshuffles of
nondeterministic or near-tolerance values, not new divergences.

### Samsung SM-X700 (full tier by table, device-scene glibc reference, bionic CPU a diagnostic)
Before = merge wave 1's Samsung runs (`main/native/work/merge1/runs/samsung/*-mw1`). Every run: full tier by
table, no tier warning, `libcodebrix_wgpu.so` hash verified, no codebrix process left afterwards.

| Corpus | Before | After (E4 final build) | Wall / budget |
| --- | --- | --- | --- |
| triangle: `triangle_lighting` 6 images | **mode 4 FAIL** (9 practical, max 0.0127); modes 0-3 540 strict | **PASS all 6**: per mode strict 12/5/8/235/**11**/248, max 1.79e-6; mode 4 max 3.73e-7 | 569 / 2400 s |
| triangle: 6 on-device replays | not run (source job failed) | **6/6 PASS** (289/417/415/473/415/464 readbacks; judged against the glibc reference) | |
| triangle: `triangle_sample` | FAIL 41 practical (24 samples) | FAIL **2 practical (1 sample)**, 1,069 strict | |
| triangle: surface | PASS | PASS | |
| Linux triangle captures (6) | 5/6 (tri4 diverged at 438) | **6/6 PASS** (tri4 415 readbacks, max 3.73e-7) | 271 / 3600 s |
| Linux terminator captures (10) | 10/10 | 10/10 PASS | 321 / 3600 s |
| transparent (lighting 8 + shadow, 10 replays) | 978 strict, max 4.39e-5 | PASS, 693 strict, max 6.02e-6 | 426 / 2400 s |
| lighting: broad_lighting | 2,184 strict, max 7.44e-5 | PASS, 993 strict, max 3.81e-5 | 257 / 2400 s |
| lighting: feature_combination | 1,304 strict, max 6.26e-6 | PASS, 1,194 strict, max 1.54e-5 | |
| motion: object / deforming moving images | 1,461 / 1,484 strict | PASS, 1,306 / 1,258 strict, max 6.08e-6; camera static/motion PASS; 2 replays PASS | 365 / 2400 s |
| terminator (10 images, 10 replays) | 195 strict, max 8.49e-5 | PASS, 124 strict, max 6.2e-5 | 497 / 1800 s |
| volume (10 gates, 7 replays) | PASS | PASS | 1743 / 1800 s (tight, as in merge wave 1) |
| regression: glass_render / lighting_mis / transparent_lighting | 97 / 1,188 / 978 strict | PASS, 97 / 1,070 / 693 strict (glass max 1.91e-6 unchanged; render_gate keeps the device oracle and its lockstep stage checks pass) | 266 / 1500 s |
| emitter probe / precision fixtures (closest, shadow, 2 replays) / spec-refusal | PASS (135 strict, 9.06e-6) / PASS / PASS | PASS (135 strict, 9.06e-6, unchanged) / PASS / PASS (refusal exits 1) | 23 / 49 / 90 s |

The bionic CPU films (diagnostic) are unchanged; for mode 4 the device CPU itself is 6 practical away from glibc,
which is why replay-4 is judged against the glibc reference (runner change, merge notes).

### Pixel 4 XL (confirmation, single run, 20 s compile budget as warnings, 2400 s per gate)
Pixel lock taken at 17:56 (after the materials merge wave released it); staged binaries re-pushed and
hash-verified under the lock.

| Job | Merge wave 1 | E4 final build |
| --- | --- | --- |
| `triangle_lighting` (6 images) | FAIL at mode 4 (9 practical, same values as the Samsung); mode 5 never reached natively | **PASS all 6** against glibc: per mode strict 12/0/6/233/**8**/231, mode 4 max 3.58e-7, 0 practical; mode 5 reached and passes for the first time natively. 622 s / 2400 s; longest compile 124 s (warning, 20 s budget; full tier) |
| `surface` | PASS | PASS (198 s) |
| `triangle_sample` | FAIL, GPU error mask 1059595178 (out-of-bounds store) | FAIL, identical error mask (open item 3, E5's lane; unchanged) |
| on-device replays | not run | replay-0 PASS (289 readbacks), replay-1 PASS (417); replays 2-4 ran on the device but their local validation failed with FileNotFoundError because I deleted the local arena copies of the device captures during scratch cleanup while the run was still going (my error, not a device result); run stopped before replay-5 at the stop rule |

## Open items
1. **Samsung triangle sampler**: 2 practical in 1 sample (sample 3133, object 4, prim 5: `P.y` 0.382186 vs
   0.379992, and `t`), down from 41 in 24 samples. Device CPU and glibc agree there; the Adreno differs from both.
   Not localised (time). Same probe method applies (emitter probe module, dumps of the sampled vertices/angles).
2. **Pixel triangle sampler** (DEVICE.md item 3, E5's lane): unchanged error mask 1059595178.
3. **HP not run** (not this lane's device): only fix 1 (ShaderData) applies to its SSE build; rerun its corpora at
   merge. Expect strict counts to move as on Linux.
4. **Remaining contractible ShaderData arithmetic**: the differentials (`du`, `dv`, `dP` in
   `surface_differentials`, `sd.dP=ray_dP+t*dI`) still differ by ULPs between Intel and the Adreno after
   injection. They do not affect any current film; make them contraction-proof if a texture-filtering gate
   needs exact differentials.
5. Two Linux broad-lighting films (atomic-accumulation cases 7 and 8, own repeat differs) have a higher maximum at
   component 1994 (value 32.04; 1.6e-4 / 2.0e-4 absolute, relative 5e-6, far inside the practical bound), and the
   deforming case 6 maximum rose 8.94e-6 -> 9.06e-6. Strict counts fell overall. Not investigated further.
6. **Protocol incident** (17:46): a compound command pushed my own staging folder to the Pixel and ran one
   read-only `ps`/`ls` although the Pixel lock `mkdir` had failed (held by the materials merge wave, whose running
   process was not touched); a false "lock taken" line from the same command is corrected in the timeline.
7. **Process notes**: DEVICE.md/PARITY.md are part of the gate source pins; editing them while gates ran made the
   host gate, one all-lanes deforming gate and the Samsung chain refuse (17:50-17:55); all were rerun on the
   restored seed docs and the docs re-applied at the end (docs-only diff, audits rerun afterwards). An earlier
   all-lanes run (16:34) stopped on an intermittent Intel crash while creating `init_camera` in a replay; the same
   gate passed standalone twice.
8. The stale scope string in `triangle_lighting_gate.py` (merge wave 1 note) is still present (not mine).

## Merge notes
- Files: `native/shaders/wavefront_shader_data.wgsl`, `native/cycles/{camera.cpp,surface.cpp,lighting.cpp,wavefront.h,
  reference_policy.h,lighting_gate.cpp,emitter_sample_gate.cpp}`, `native/DEVICE.md`, `docs/PARITY.md`.
- Merge wave 1 hardening lines: `lighting_gate.cpp` and `emitter_sample_gate.cpp` carry the `require_webgpu_device`
  line; E4 only inserts one line at the top of `main` (no overlap). `render_gate.cpp` untouched.
- M3/M4 (materials): `wavefront_shader_data.wgsl` is shared with the SVM/texture code (`surface_object_point` is
  used by `NODE_TEXCO_OBJECT` and attributes); M-lanes that touch ShaderData must keep the rounded forms.
  `surface.cpp` (layout) and `lighting.cpp` are shared with the materials/lighting lanes: one constant line each.
  `camera.cpp`'s layout block is X2's; E4 replaces three `#ifdef __KERNEL_SSE__` blocks.
- E5 (Pixel sampler): `emitter_sample_gate.cpp` main gets one line; the triangle-light cache is untouched here.
- Every Linux film with a triangle shading point changes (toward the CPU oracle): the expected glass spec on/off
  film becomes `db8e84f9990299d2b2ce1c53a390f56e1d4beb73ec7326aa55da4c1b074802f6`. Any later lane comparing against `a072683c...` must use the new value.
- HP (Android x64) was not run (not this lane's device): its build is an SSE build, so only fix 1 applies there;
  the HP corpora should be rerun at merge.
- Runner change (work tool only): `native/work/e4/tools/run_corpus.py` judges on-device replays of image captures
  against the job's glibc device-scene reference (the capture's own bionic CPU film is validated as a diagnostic);
  merge-wave runners should adopt it, otherwise replay-4 fails on the bionic film (6 practical there).
