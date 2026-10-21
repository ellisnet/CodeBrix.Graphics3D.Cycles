# Phase E1: emissive triangle lights on the Samsung (ARM64) and the HP (Android x64)

Agent: E1 (Opus 5.5). Worktree `/home/jeremy/ClaudeHome/cycles-worktrees/e1-triangle-arm`, seeded from
`baseline-phaseE.json`. Devices: Samsung SM-X700 `R52W207XQ1V` and HP `192.168.86.225:5555` only (the Pixel was not
touched). Runtime: the unchanged CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433 (android-arm64 `8113cbfa…`,
android-x64 `c2b0105f…`, verified on device for every corpus). Nothing was committed; main, the other worktrees,
`third_party/cycles` and `native/evidence` were not touched. Evidence: `native/work/e1/` (`timeline.log`,
`budgets.txt`, `runs/`, `logs/`, `libm/`, `analysis/`, `replay-diag/`). Sealed handoff: `native/work/e1/handoff/`.

## Result in brief

| Item | Outcome |
| --- | --- |
| 1. Samsung sampler / emitter-probe vs ARM64 oracle | **Root cause found, not fixed (needs a decision).** Not SSE-vs-scalar float3 arithmetic: the Android oracles use bionic's `acosf`/`asinf`, which are not correctly rounded; the WGSL reproduces glibc's correctly rounded values. The HP's x86_64 bionic oracle fails the same way (the sampler now compiles there). |
| 2. Samsung mode 2 / Linux capture at command 574 | **Isolated, not fixed.** One bounce-0 shadow ray toward the instanced emitter, whose inputs already differ by 1-2 ulp between Adreno and Intel, is occluded on the Adreno only. Fix would need contraction-proof shared traversal (changes Linux results). |
| 3. Triangle sampler probe "unspecialized" | **Premise stale.** The probe already compiles `d.module_wgsl(surface_source())` (C1 fix); its HP OOM was the same emissive-triangle+motion load as item 4 and is resolved by it. |
| 4. HP OOM / compile load | **Fixed.** HP compiles mode-5 `shade_surface_light` (27.5 s, 2.08 GB, 2/2) and the probe (22 s); Samsung 204 s -> 30 s; Linux 75 s -> 24 s. Linux results byte-identical. |
| 5. Spec-off on Android | **Fixed as a refusal** at device creation with a clear message (host API returns it; no driver crash). |
| Stretch (static instanced emitters) | Not attempted (1 and 2 are not done). |

## Timeline

- [08:13:40] Started; read the reports, DEVICE.md, PARITY Lighting row, the WGSL and pinned Cycles sources.
- [08:19:00] Linux baseline build (default targets, `--jobs 6`).
- [08:19:30] Re-analysis of the C2 bins: the emitter failure (sample 4093, prim 2) is the BROAD fixture's area light 2, not a sun; triangle failures keep the identical pdf (solid angle) but differ in D/P.
- [08:20:10] libm scan over every binary32 in [-1,1] (`libm/libm_probe.c`): glibc acosf/asinf correctly rounded; Samsung bionic differs at 5,387,136 / 682,540 inputs; HP bionic x86_64 at 5,717,766 / 4,220,748 (different digests from the Samsung).
- [08:21:20] `libm/area_s_probe.c`: one bionic `asinf` term 2 ulp off reproduces both oracle pdfs exactly (3682.84839/4 = 920.712097 ARM, 3682.04028/4 = 920.510071 CR/x64/GPU).
- [08:22:10] `libm/tri_alpha_probe.c`: bionic `acosf(cos_alpha)` differs from CR at the failing triangle samples.
- [08:23:19] Item 5 implemented (`device.cpp`).
- [08:24:30] Item 3 premise check (probe module already specialized).
- [08:26:00] Static attribution (`tools/attrib_parent.py`): mode-5 `shade_surface_light` inlines 3.35 MB; four call sites of `triangle_light_vertices` = 2.17 MB.
- [08:27:49] Draft: single vertex call site + looped acos.
- [08:29:44-08:32:00] Item 2: diagnostic replay copy on the Samsung (command 574, word 15: 330 vs 331); films show only the Adreno differs at component 1484 (ARM oracle = x64 oracle = Linux GPU).
- [08:34:30-08:37:57] HP/Samsung probes of draft1-3 (offline-built modules): HP mode 5 34.9 s/2.88 GB -> 30.4 s/2.32 GB -> 27.4 s/2.08 GB.
- [08:35:25] Linux baseline (cold) triangle and emitter gates = Phase C numbers.
- [08:36:40] draft3 applied; candidate build.
- [08:39:00-08:40:30] Linux v1 (cold): sampler/emitter bins and all five triangle images (gpu/cpu/repeat films, command streams) byte-identical to baseline.
- [08:40:44] Linux all-lanes and close set started (cold).
- [08:42:00] `libm/tri_alpha_all.c`: **18 of 18** failing static-emitter samples have bionic acosf != CR; glibc == CR for all 18.
- [08:42:15 / 08:50] Android arm64 / x64 builds into `build-android-<rid>`; review: 42 files each (ELF machine, NEEDED closure, no GLIBC) in `staged-binaries-<rid>.json`.
- [08:47:38] Close set = Phase C numbers (legacy v5 replay input absent in the worktree; see Remaining).
- [08:49:47] Cold compile tables on HP and Samsung (one pipeline per process).
- [08:51:02-09:13:10] Device chains (triangle, emitter, regression, spec-refusal, Linux v1 captures) on both devices.
- [08:51:38] HP triangle sampler compiles and fails exactly like the Samsung (sample 43) -> bionic, not ARM.
- [08:53:53] Linux glass_render spec-off: film a8e69a34… (= spec-on in all-lanes).
- [09:03:53] Linux all-lanes 29 PASS. [09:07:01] host gate PASS (48 exports). [09:14:50] `dotnet test` 157/157.
- [09:05:00 / 09:13:30] Host-API refusal check on Samsung/HP.
- [09:15:30] Item 2 isolated with `tools/shadow_diff.py`.
- [09:16:00] Devices cleaned; docs applied; audits rerun after docs; handoff sealed.

## Root causes with evidence

### Item 1: the Android CPU oracles' C library, not float3 arithmetic
- Cycles `safe_acosf`/`safe_asinf` call libm. Linux x64 (glibc 2.41) returns correctly rounded `acosf`/`asinf`
  (`libm/linux.txt`: 0 real differences); the WGSL `triangle_light_acos`/`emitter_asin` are correctly rounded
  double-word evaluations and match glibc.
- bionic does not round correctly, and arm64 and x86_64 bionic differ from each other (`libm/samsung.txt`, `libm/hp.txt`).
- Triangle sampler: all 18 failing samples on static emitters (objects 0-4) have bionic `acosf(cos_alpha)` 1 ulp from
  CR (`libm/tri-alpha-all.txt`); the spherical-triangle sampling amplifies that ulp into P/D errors of up to 1.5e-3 while
  the pdf (solid angle via `fast_atan2f`, no libm) stays identical. The remaining 4 failures are on moving/deforming
  emitters, whose host-side motion decomposition and in-kernel rotation also use libm.
- Emitter probe sample 4093 (area light 2, rectangle solid angle in `area.h`): `libm/area-probe.txt` reproduces both
  oracle pdfs exactly from one `asinf` term.
- The HP (x86_64, SSE, bionic) now runs the sampler and fails it too: 32 practical failures in 30 samples, worst again
  sample 3880 at 11.6x with the same oracle value as the Samsung (`analysis/analysis-hp-triangle_sample.json`). Its
  emitter probe passes the practical criterion (734 strict, 0.0131 abs, as in C2).
- The SSE/ARM float3 candidates B2 listed exist (`safe_normalize_len` divides on SSE and multiplies by the reciprocal on
  ARM; WGSL `emitter_normalize_len`/`light_normalize_len` always divide) and explain 1-ulp D differences such as D.z
  of sample 4093, but none of the practical failures. I did not change them (they would only move strict counts).
- **What a fix needs:** reproducing bionic's `acosf`/`asinf` on the GPU per oracle ABI (arm64 and x86_64 differ),
  selected from the oracle host the way `NATIVE_FLOAT3_RECIPROCAL_NORMALIZE` is. That is a translation of bionic's
  libm (FreeBSD msun, Sun permissive licence), which must be pinned and licence-reviewed first under the standing
  instructions. I stopped there; this is Jeremy's decision. Alternatives: validate Android GPUs against a glibc-derived
  (correctly rounded) reference, or document the Android oracle as libm-dependent.

### Item 2: an Adreno-only shadow visibility flip
- `replay-diag/render_replay_diag.cpp` (a diagnostic copy of `native/tests/render_replay.cpp`) on the Linux r2 case-2
  capture: command 574 is the readback after `intersect_shadow`; word 15 (the shade-shadow queue count) is 330 on the
  Adreno, 331 on Intel. 39,599 arena words already differ by ulps before it.
- `tools/shadow_diff.py` on arenas dumped after command 573: pixel 504, sample 4, bounce 0, the ray toward light object 3
  prim 7 (the instanced emitter); origin `3.3912112712860107` vs `3.39121150970459`, D and tmax 1-2 ulp apart; Intel
  queues it unoccluded, the Adreno terminates it (`replay-diag/shadow-diff-at573.txt`).
- The device films: at component 1484 the ARM64 oracle (0.311020) equals the x64 oracle and the Linux GPU; only the
  Adreno differs (0.351310). The HP replays the same Linux capture exactly (tri2 415 readbacks).
- Same family as the static-instanced rejection: plain-arithmetic closest-hit/ShaderData ulps flip an edge-grazing
  test. A fix needs contraction-proof traversal/intersection in shared stages, which changes Linux results (B1 showed
  this for the exact hit); not done.

### Item 4: four inlined copies of the emissive-triangle vertex computation
Drivers inline every call. `shade_surface_light` called `triangle_light_vertices` four times (forward-hit weight and
sample, each at the path time and the centre time); each copy carried the object-motion transform with a 30-step
double-word arccosine and the motion sine/cosine (two disk sine/cosine call sites).

## The changes (all Apache-2.0, original code; no notices change)

1. `native/shaders/wavefront_triangle_light.wgsl`
   - `triangle_light_prefill(index, sd, emission, sample)` computes every vertex set an invocation can use (forward
     hit at path time and centre time, sampled emitter at path time and centre time) in the original evaluation
     order, from one call site, into a private per-invocation cache; `triangle_light_vertices` now reads it back by
     (object, prim, time) (a miss raises the error flag). The body moved unchanged to
     `triangle_light_vertices_compute`. The prefill body is inside a `//#if LIGHT_TRIANGLE` region.
   - `triangle_light_acos`: the 29 Horner steps as a loop over the same coefficients (same operations and order).
   - `triangle_light_motion_sincos` (one disk-sine/cosine call site instead of two) and `triangle_light_disk_sincos`
     (looped copy of `precise_disk_sincos`) for the triangle-light rotation only. The shared `wavefront_motion.wgsl` and
     `wavefront_trig.wgsl` are unchanged.
2. `native/shaders/wavefront_lighting.wgsl`: `light_select` (RNG + CDF search) factored out of `light_sample_native` so
   the prefill selects the same emitter; lamp sampling text unchanged.
3. `native/shaders/wavefront_surface.wgsl`: `shade_surface_light` evaluates the emission/sample conditions once, calls
   the prefill, then the forward weight and the sample as before.
4. `native/cycles/emitter_sample_gate.cpp`: the probe calls the prefill before `light_sample_native`.
5. `native/cycles/device.cpp`: on `__ANDROID__`, `CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0` throws a clear message
   before any pipeline compiles; the existing factory returns Cycles' error device, and the host API reports it.
6. Docs: `native/DEVICE.md` (new section "Emissive-triangle compile load and the Android oracles' C library", open
   items 1-4 rewritten, closing sentence) and `docs/PARITY.md` (Lighting and Memory/performance rows).

No recorder change (no new stage; v1-v7 unchanged), no feature-bit change, no CPU reference, fixture, tolerance or
runtime change.

## Compile tables (cold, one pipeline per process, `tools/probe.py`; before -> after, real modules from the gates)

Inlined WGSL (`tools/attrib_parent.py`): mode-5 `shade_surface_light` 3,351,782 -> 1,315,317 bytes; mode-1
1,598,270 -> 1,158,828; triangle probe 2,148,059 -> 1,197,932; `connect_surface_light`/`prepare_surface` unchanged.

| Entry | Linux Intel Mesa 25.0.7 (gate pipeline log) | HP Intel Xe Mesa 24.0.8 | Samsung Adreno 730 |
| --- | --- | --- | --- |
| `shade_surface_light`, triangle + object motion (mode 5) | 75.3 -> 24.3 s | Scudo OOM at 2.25 GB after 21 s -> 27.6 / 27.4 s, 2.08 GB (2 of 2) | 203.9 s / 3.28 GB -> 30.0 / 29.4 s, 0.49 GB |
| `shade_surface_light`, triangle (mode 1) | 14.9 -> 14.7 s | 24.9 s / 2.61 GB -> 20.5 s / 1.84 GB | 34.1 s / 0.54 GB -> 20.1 s / 0.39 GB |
| `emitter_sample_probe` (triangle sampler) | compiled cold before and after (not timed) | OOM at 1.85 GB after 13 s -> 22.0 / 21.9 s, 1.99 GB (2 of 2) | 80.5 s / 1.47 GB -> 24.4 / 24.7 s, 0.41 GB |
| `connect_surface_light` (mode 5) | unchanged | 10.8 -> 10.9 s, 0.32 GB | 1.7 -> 1.7 s |
| `prepare_surface` (mode 5) | unchanged | 5.9 -> 5.9 s, 0.34 GB | 2.6 -> 2.6 s |

In the device gates: HP `triangle_lighting` max pipeline 27.8 s; Samsung Linux mode-5 capture replay 29.3 s (was 204.8 s).

## Results

Strict `1e-7 + 1e-7*abs(ref)` is reported only; the practical criterion was never changed.

### Linux x64 (Intel, cold scratch Mesa caches)
| Run | Result |
| --- | --- |
| triangle gate (baseline vs final) | PASS both; sampler 4,096, 2,079 strict, max 4.8828e-4; surface 18 cases, 7.534e-5; images 5, 2,344 strict, 1.0423e-5, 213 mesh-MIS paths; 5 replays. **Bins, films (gpu/cpu/repeat) and command streams byte-identical.** |
| emitter_sample | PASS, 165 strict, 1.9226e-3, bins byte-identical |
| all-lanes 29 | PASS, inputs unchanged; broad_lighting 5,048, lighting_mis 5,544, feature_combination 5,659 (3.3855e-5), transparent_lighting 3,838, transparent_shadow 0, emitter 165, glass_render 121 image strict - Phase C within atomic run-to-run variation |
| close set | broad_lighting 5,046 (450 readbacks), transparent 3,838 (596), object motion 5,762 (454), deforming 5,804 (454), camera motion PASS; legacy v5 not run (see Remaining) |
| glass_render spec on / off | both film `a8e69a3424036a797160ad074aca1a5435f0d232e7b3298e5509cf8ef7349602` |
| host gate | PASS, 48 exports, 3,142 comparisons, max 1.19e-7, 0 practical, 0 strict |
| `dotnet test --solution` | 157 passed, 0 failed, 0 skipped (cold Mesa cache) |
| audits | `audit.py` PASS 0 errors; `notices.py --check` PASS (34); guard tests OK; `git diff --check` clean (rerun after docs) |

### HP (android-x64, full fidelity, `MESA_SHADER_CACHE_DISABLE=true`)
| Job | Result | Numbers |
| --- | --- | --- |
| triangle_sample | **FAIL** (item 1, bionic oracle) | compiles in 22 s; 32 practical in 30 samples, worst 11.6x, 3,067 strict |
| surface | PASS | 18 cases, 21 rejections, 7.44e-5 |
| triangle_lighting | **PASS** | 5 images incl. mode 5 and the static-instanced rejection; 2,324 strict, 1.0423e-5 |
| on-device replays | 5/5 PASS | 289/417/415/473/464 readbacks; strict 9/82/144/463/464 |
| Linux v1 triangle captures | 5/5 PASS | 289/417/415/473/464; incl. tri2 |
| emitter_sample | PASS | 734 strict, 0.0131 abs |
| regression | PASS | glass_render 115 image / 12,853 stage strict, film = C2 bit for bit; lighting_mis 5,539 strict 8.34e-6; transparent_lighting 3,872, 7.75e-6 (differing cases only where the run's own gpu/repeat films differ, <= 1.2e-7) |
| spec-off | refused | host API: `cbc_host_create` returned 3 with the message, exit 1 |

### Samsung (android-arm64, full fidelity)
| Job | Result | Numbers |
| --- | --- | --- |
| triangle_sample | **FAIL** (item 1) | 24 practical in 22 samples, worst 11.6x, 2,087 strict (unchanged from C2) |
| surface | PASS | 18 cases, 21 rejections, 7.44e-5 |
| triangle_lighting | **FAIL** at mode 2 (item 2) | component 1484 0.351310 vs 0.311020; modes 0-1 pass; modes 3-5 and the rejection not reached (gate stops at first failure); on-device replays therefore not run |
| Linux v1 triangle captures | 4/5 PASS | tri0 289, tri1 417, tri3 473, tri4 464 readbacks (mode 5 incl.); tri2 FAIL (item 2) |
| emitter_sample | **FAIL** (item 1) | sample 4093 pdf 920.510 vs 920.712 |
| regression | PASS | glass_render 44 image / 2,322 stage, film `8199ad80…` = C2 bit for bit; lighting_mis 2,263 strict 6.08e-6; transparent_lighting 1,620, 1.62e-5 (same run-variance note) |
| spec-off | refused | host API message, exit 1 |

The spec-refusal gate jobs (`glass_render` with spec=0) exit 139 on both devices: the factory converts the refusal to
Cycles' error device (correct product path), but every gate binary dereferences `dynamic_cast<WebGPUDevice*>`
unchecked. The refusal was therefore validated through `codebrix_host_gpu_test` (host API).

Devices: all `/data/local/tmp/codebrix-e1-*` folders removed on both, no codebrix/render_replay/compile_probe process
left. Budgets were declared in `budgets.txt` before every run; none overran.

## What remains
1. Item 1 needs Jeremy's decision on the oracle libm (pin/translate bionic msun `e_acosf`/`e_asinf` per Android ABI, or
   change the Android reference policy). Until then the triangle sampler gate and the Samsung emitter probe fail on
   Android against their own oracles.
2. Item 2 is Adreno-only GPU divergence; fixing it changes shared traversal and Linux results.
3. Samsung triangle images 3-5 and on-device triangle replays are not covered on the device itself (the gate stops at
   mode 2); the Linux captures of modes 3 and 5 replay there.
4. The legacy v5 replay of the close set did not run: its capture exists only in main's `native/work` (and was a cold
   driver crash in Phase C anyway).
5. Gate binaries crash (null dereference) when device creation fails; a checked helper in `wavefront_test.h` would
   give a message. Not changed (test-only, 20 call sites).
6. Stretch (static instanced emitters) not attempted.
7. Process notes: two CPU-only libm probes ran briefly on the Samsung while a compile probe was running (no GPU use);
   the DEVICE.md/PARITY.md edits were applied after the validation runs (docs only; audits rerun afterwards).

## Docs text applied in the worktree
- `native/DEVICE.md`: new section "Emissive-triangle compile load and the Android oracles' C library (2026-10-20)"
  before "Open device items", with the compile table above and the device results; open items 1-4 now read: 1) Android
  oracles (Samsung and HP) use bionic acosf/asinf - open, needs a decision; 2) Samsung mode-2 shadow-ray visibility
  flip - open; 3) HP compile memory - resolved; 4) spec-off refused on Android - resolved as a refusal. Closing sentence
  updated (the triangle corpora were rerun).
- `docs/PARITY.md`: Lighting row (Samsung/HP emissive-triangle status and the bionic-oracle note) and Memory/performance
  row (Samsung 29-30 s, HP 27.6 s for the motion triangle entry; spec-off refused on Android).
