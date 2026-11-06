Linux x64 FULLY VALIDATED at `f1ed54307` + fixes `b2c803f5f670244b4fa10a42e03a8b997bce86fd718aac9cb434dd9d48e994ea`: NO (3 failures in the final run, not fixed under the coordinator's 06:55 stop: Principled superset `shade_surface` 61,059 Intel instructions > 60,000; `passes/cryptomatte` not perfect (2 cryptomatte slot-order practical failures); 5 render-chain gate drivers not validly run in the final run (a lane input moved by me). Evidence + ledger written; `inventory.py check` PASS.)

Copyright (c) 2026 Jeremy Ellis and contributors

Lane val35 (Opus 5.5). Worktree `/home/jeremy/ClaudeHome/cycles-worktrees/val35`, branch `val35`, base `f1ed54307`.
Lane folder `native/work/val35/` (timeline.log, budgets.txt, validation/, run-A/, checkpoint/, handoff/).
FACT = measured here. INFERENCE is marked. Nothing committed; main untouched (read-only reads/copies only).

## Process faults (mine, stated first)
- I was idle twice because I did not re-arm my wait monitor: 18:53-23:19 and 00:06-06:55. The scripted runs kept going; no
  result was affected, but the triage and the evidence step started late.
- Run A (18:52-23:46) is superseded: I stopped my own pre-fix reruns at 23:46 to land FIX-1 and repeated the whole job set
  on the final tree ("FINAL run", 23:56-06:47; `RUN-FINAL-START` line in timeline.log). Run A outputs: `native/work/val35/run-A/`.
- In the FINAL run I moved the seeded `native/work/render/` (with the bench's imported `triangles.bin`) into run-A before
  the render-chain drivers ran, so `render_gate.py`'s bench step failed "Cannot open imported triangles" and with it the
  attribute/image/normal/image_file/render drivers (Run A: the same drivers passed after FIX-T1..T3; pre-fix tree).
  Under the coordinator's no-rerun instruction they are reported as NOT VALIDLY RUN in the final run.

## Start / end state
- Start (18:42): HEAD `f1ed54307656855716087ddaf195169376da6d29`, `git status --short` empty, fingerprint
  `c630c970a79848fabcc12b7c1f397ffb66a7b2dd40080d4bb2a2fd4de66a90e1` (= expected), `dotnet --version` 10.0.403,
  219 GB free.
- Fingerprint of the tested tree (code fixes, before evidence/docs; `fingerprint-end.txt`):
  `2e262a5bbebccc939617e76121e04494ba71a87d5785b952303d453093c0dada`. Final fingerprint (with evidence and docs): in the
  Seal section below.
- Fixes sha256 at the FINAL run's start: `e509d24c57c645c052a4a3f933c55c3d196b6cef1f06fab1cb046256feb8b1a8` (= the code
  part of `fixes.patch`; the evidence step added only tools/compatibility/inventory.py, compatibility/*.json, docs and
  the two evidence folders, none of which is an input of a run job).
- WebGPU: host lib and 77 binaries link `~/.nuget/.../codebrix.webgpu.native.mitlicenseforever/1.0.300.210/runtimes/linux-x64/native/libcodebrix_wgpu.so`
  (sha256 5a61096e...), 2 have none (atomic_film_gate, zstd_compress), 0 bad. RUNPATH: after the first relink 0 of the
  built binaries point into GitHome (`runpath.txt`); ldd resolves 0 libraries from GitHome. The copied
  `native/work/dependencies` libraries keep RUNPATH `$ORIGIN:<main>/...install/lib` ($ORIGIN first; every library
  resolves inside the worktree, LD_DEBUG shows no file tried in main). The 3 prototype host probes keep a main RUNPATH;
  nothing here runs them.
- Size limit: 20 GB (Jeremy 23:20); folder 25 GB at 23:19 -> 3.1 GB after deleting 850 uncited capture `.bin` files
  over 5 MB (validation/, diag/; sha256 + bytes in `deleted-sha256.txt`).

## Coordinator rulings (06:55), recorded as given
1. KEEP FIX-1 (the final run validated the tree with it). It is a change to WebGPU pipeline selection in shared
   native/cycles files, flagged for Jeremy's review at the merge.
2. ARM64 oracle: the re-render rule targets CPU-kernel and host scene-preparation changes; FIX-1 touches neither, so
   the ARM64 oracle at `f1ed54307` stays valid.

## Fixes (findings), all uncommitted in branch val35
1. **FIX-1 (shared native/cycles: pipelines.cpp, device.h, surface.cpp): `prepare_surface` without the SVM node regions
   no triangle shader reaches.** Finding (FACT): `bench_01-hdri`'s device build warned
   `pipeline 'prepare_surface' (surface module) inlines to 1113668 bytes of WGSL, at or above the 1110000-byte size
   guard` (not on the KNOWN list; Intel 53,555 instr). Its features included `SVM_TEX_ENVIRONMENT` (world-only here).
   Hunk: `surface_unused_svm_features()` (census of the shaders in `tri_shader`, cached on svm_generation; same
   census-only mask as `raytrace_unused_svm_features`); `enqueue_surface` builds `prepare_surface` on its own with those
   regions removed when there are any (the shared build then omits it); `surface_prepare_removed` tracks the removed
   set. Scenes with no such regions compile exactly as before. Result (FACT, diag2/bench-hdri + final run):
   bench_01-hdri `prepare_surface` 1,113,668 -> 1,026,252 B, other surface entries unchanged, no size warning in any
   final-run bench render; film perfect vs CPU (max 9.18e-5, 0 practical, 364 strict). Pattern: e3/val33 census splits.
2. **FIX-T1 native/tools/device_gate.py**: expected `compactionCases` 2 -> 4. The gate reports 4 since the MNEE
   ownership-repair compaction cases (gate.cpp); every driver that starts with the device gate failed `Unexpected gate
   coverage`.
3. **FIX-T2 native/tools/svm_surface_gate.py**: expected `untouchedGuardBytes` 2944 -> 4416 (the gate reports 4416
   since the shader-data cache guard count, surface_svm_test.h; f32/val33 binary runs also 4416). Failure was
   `AssertionError: Surface SVM coverage changed`.
4. **FIX-T3 native/tools/arena_replay.py `validate()`**: the per-invocation SVM stack slot region (arena header after
   the error word: address, slot count; `SVM_STACK_WORDS`) is excluded from the unchanged-bytes check when it lies in
   the arena. Failure was `AssertionError: Replay changed scene/state/guard bytes` (all changed bytes inside
   [32, 4309024) = the slot region). First form broke hand-built arenas (packed_state: `Invalid/overlapping capture
   output spans`); amended with the bounds check.
5. **FIX-T4 tools/compatibility/inventory.py + compatibility/dependencies.json**: after the ledger re-point
   `inventory.py check` failed `Compatibility inventory is stale: dependencies.json` (FACT: the committed file lacked the
   imported `oidn` component; the generator also listed `oidn` as unassessed). `oidn` removed from the unassessed list,
   dependencies.json regenerated (`inventory.py generate`; only that file changed). Check now PASS.
- Not fixed (failures, see below): Principled superset envelope; cryptomatte slot order; render-chain drivers.

## Job table (FINAL run; val33 run B / e3 / oidncpu34 beside)
| Job | Verdict | Numbers (val33 B / lane) |
|---|---|---|
| 1 state | OK | above |
| 2 build (clean, --target all) | exit 0, 9 min; no-op rebuild 0 compiles | warnings only -Wclass-memaccess 67 (67); freshness: overlay target 1, no .inc newer than device.a, zstd_compress exempt, SC marker 1, miss=0 |
| 3 dotnet build Rel/Dbg | 0 warnings | |
| 3 dotnet test Rel/Dbg | 203/203 both (400 s / 421 s, cold Mesa cache per slot) | 195 val33; 203 oidncpu34. List: `validation/j3-dotnet-test-list` (GpuRender + Denoising tests listed) |
| 4 A2 host gate | PASS | 3,146 comp, 0 practical, 153 strict, max 1.526e-5 (3,146/0/151/1.526e-5); strict films are the ABI light films (run-to-run class) |
| 5 A1 overlay | 21 entries | differ from val33: background, camera, closest, shader_data, surface, svm, volume_stack .inc |
| 5 A5 libm | = val33 except temp dir and source path | |
| 6 A3 corpus | exit 0, 3,680 s | scene count by the section-9 formula 231 (= 155a0e317). WGSL vs CPU: 229 compared, **228 perfect, 1 not (passes/cryptomatte)**, 0 film-level practical, film-level strict 61,635 (61,745), worst 7.06e-5 motion-glass-principled-lamps (7.08e-5). Incl. pass entries: strict 109,983 (110,158), worst 1.21e-4 passes/light-passes |
| 6 CPU vs val33 B | 230 bitwise, 1 not | `passes/data-passes` pass-8 RenderTime (known); bench 2 no common files |
| 6 meta | every WGSL meta.json: wgslArithmetic x64-sse4.2, rid linux-x64 (460) | |
| 6 A4 bench + HDRI | pass, no device size warning | |
| 7 A6 managed export | 203/203 | `managed-linux-x64/` |
| 8 regression lanes L1-L6 | see gate values | |
| 9 ARM64 cross-check (reported) | 4/6 perfect vs cpu-arm64-linux | tree-glass-render 2 practical (max 1.96e-4), motion-hdri-emitter 5 (max 3.73e-3); enclosure x3 and tree-triangle-lighting-2 perfect (Run A numbers; final-run compare JSONs in `arm64x/compare/`) |
| 10 OIDN | PASS (practical) | enclosure 1.29e-5/0/4,037 (1.17e-5/0/4,000); beckmann 6.01e-5/0/1,417 (5.72e-5/0/1,470); catcher Combined 4.60e-5/0/482 (4.24e-5/0/494), SC 5.01e-6/0/7,508 (5.36e-6/0/7,772), Matte 4.60e-5/0/482. CPU+OIDN repeats identical; noisy requests = undenoised passes bitwise; denoise-off films = corpus CPU films bitwise; capability `{"available":true,"denoiser":"openimagedenoise","denoiseDevice":{"cpu":"cpu","webgpu":"cpu"}}`; light groups + denoise refused `cbc_host_render returned 2: Light group passes are not supported together with denoising yet` |
| 11 nodes_render binary | PASS | strictStage 3,213, max 2.527e-5, strictImage 793, max 2.027e-5 (= val33) |
| 12 compare.py per scene | 233: 228 exit 0 perfect, 1 exit 1 (cryptomatte), 4 exit 2 | the 4 = bench_01, bench_01-hdri, closed-volumes, world-volume (as val33) |
| 13 envelope + size | 1 FAILURE (materials) | table below |
| 14 tool tests | test_inventory OK, test_guards OK, `git diff --check` clean; licence audit/notices SKIPPED (no external code) | |
| inventory.py check | PASS after evidence step + FIX-T4 | before: `Evidence needs review after source change: native-render native/cycles/background.cpp` |

## Non-zero results of the FINAL run, verbatim
1. `p8-sim650-nodes-world` exit 1 (KNOWN 1): `ERROR: WebGPU: texture, colour and utility shader nodes are not supported on
   Adreno (TM) 650 GPUs (this scene uses SVM_NODE_TEX_NOISE): ...render the scene on another GPU or on the CPU device`.
2. `x-inventory-check` exit 1 (KNOWN 3, before the evidence step): `Evidence needs review after source change:
   native-render native/cycles/background.cpp`.
3. **`p8-envelope-materials` exit 1 (FAILURE)**: `{"module": "principled/superset/003-surface.wgsl", "entry":
   "shade_surface", "inlinedBytes": 997567, "maxEndIp": 61059, "maxAlloc": 40219, "withinLimit": false}` (val33: 59,381,
   same module). INFERENCE: e3's pinned `surf_bump` (+~7.8 KB per copy, two copies in shade_surface); e3 did not dump
   this superset group. FIX-1 does not touch superset mode. Open item 43.
4. **`passes/cryptomatte` not perfect (FAILURE, compare.py verdict)**: `pass CryptoMaterial00 cryptomatte maxAbs=5.96e-08
   practicalFail=1 strictDiff=0 slotOrderDiff=1`, same for CryptoMaterial01. Run A (pre-fix): perfect. FACT: FIX-1 did not
   change this scene's surface pipelines (its prepare_surface features = the shared set). INFERENCE: equal-weight ids
   accumulated in a different order (run-to-run class); not proven by a repeat. Open item 44.
5. **v-drv-attribute / image / normal / render exit 1**: `subprocess.CalledProcessError: Command
   '[.../codebrix_cycles_render_gate', '.../native/work/render/bench/run', '.../native/work/render/bench/triangles.bin']'
   returned non-zero exit status 1` (stderr `Cannot open imported triangles`; my move, above). **v-drv-image_file** exit 1:
   `FileNotFoundError: ... native/work/images/validation.json` (needs the image driver's report). Their gate binaries
   all ran directly and passed (p8v-*). Run A: render PASS (v2), attribute PASS (v3), svm_surface/packed_state PASS after
   fixes; image/normal/image_file never passed in Run A either (stopped at 23:46).
6. `w-arm64x-compare-combinations_tree-glass-render` and `...motion-hdri-emitter` exit 1: job 9, REPORTED only.

## Gate values (FINAL run; val33 B or e3 beside)
- bsdf_gate PASS: bsdf_svm 470 / 5,083; bsdf_bounce 30, 4,935, ratio 0.065; bsdf_render 147 stages, image strict 1
  (4.77e-7); bsdf2_svm 71 / 0; bsdf2_bounce 1,020; bsdf2_render image strict 2; bsdf3_svm 166 / 2,233 (all = val33).
- bsdf3_bounce velvet rule: 24 evaluations, 2,804 differences, 104 paths, 5,039,232 FTZ checks, 0 FTZ mismatches;
  bsdf3_render 10 stages, 255, 12 paths, 0 mismatches, image strict 39, max 3.81e-6 (= val33).
- Velvet NEGATIVE CONTROLS (KNOWN 2, `diag/neg/summary.txt`): velvet x1.001 -> bsdf3_bounce exit 1, bsdf3_render exit 1;
  Oren-Nayar x1.001 -> both exit 1; 0 VELVET-RULE lines in all four. Controls FAIL as required.
- raytrace_nodes_state 6 cases, 320, 9.06e-5; raytrace_nodes_render 9, 11,090, repeat diff 6 (11,094 / e3 11,090).
- displacement 7, 4,125, 1.13e-6. camera static 24 / 0, --motion 24 / 1,616, --projections 24 / 0.
- shadow_catcher PASS, 18/18 replays, 14,917, 4.55e-5 (14,917 / e3 14,919). light_tree 8 + 8 replays, 32,605, 0.0227.
- ao_state 20, 1.21e-4. ao_render 27, 27,908 (repeat diff 52) + 27/27 replays (27,921 / e3 27,914).
- mnee 9 replays pass, paths 46,449, max 0.125, strict 870; mnee_render 9 cases, 6,376 (e3 6,372; repeat diff 6,692).
- transparent_shadow + motion PASS; transparent lighting 10 cases 5,282. shadow_terminator 286 (294; e3 did not run it).
- p8-all-lanes PASS (1,595 s). shadow / broad_lighting / lighting_mis / feature_combination PASS. scene_edit, lighting,
  microfacet native, object_motion (moving images 5,446 = e3), atomic_film, tier, glTF (sample, plane, bench) PASS.
- svm_world 7,446, nodes_world 8,981 (= e3). nodes_gate PASS (3,891 s, budget 8,000), principled_gate PASS.
- Superset dumps: glass 3 groups identical, principled 9 groups identical (unfit NORMAL_MAP), nodes_render 6 identical.
- New drivers (FINAL run, PASS): device, background_queue, camera_queue, closest_queue, svm_world, shader_data,
  svm_surface, float, trig, packed_state, native_trig (via `tools/linux_only.py`: Android device lists emptied, adb
  refused). Not validly run: attribute, image, normal, image_file, render (above).
- tab31 Linux checks (FINAL run, `diag/tab31-ab/`): with the ARM64-only text removed (all one-line
  `if(NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u){...}`, F6 var->let, F3c reverted to bc71f6061's loops): mnee state, shader_data,
  camera --motion, closest, emitter_sample identical (reports and files); light_tree, mnee_render, object_motion_render
  differ only in films whose own repeat also varies. Run A localisation (`run-A/diag/tab31-om`): identity override =
  bitwise; each variant changed object_motion case 5 by <=5.96e-8 in 2 values while its own repeat film differed and all
  150 stage comparisons were identical -> film atomics, not arithmetic. mnee_gate cases 0-8 + 9 replays (stride F4b) PASS;
  shader_data 11 cases, 0 strict; `compare.py --self-test` PASS (covers x64 and arm64 ISA families).

## Envelope (Intel Mesa 25.0.7, 60,000) and size guard (1,110,000 B), FINAL run
| Set | Entries | Worst instr (entry, module) | Largest inlined B |
|---|---|---|---|
| abi | 99 | 48,618 init_camera | 915,586 |
| materials | 72 | **61,059 shade_surface (principled/superset/003) FAIL** | 999,382 prepare_surface (nodes-render superset) |
| m2 | 997 | 55,505 mnee_solve | 1,018,534 |
| environment | 117 | 40,509 shade_surface_light | 715,430 |
| bench-hdri | 33 | 53,555 prepare_surface | 1,113,668 (dump of the shared module; the device builds 1,026,252, no warning) |
| x | 91 | 35,273 shade_surface | 634,336 |
| g | 212 | 53,283 prepare_surface_raytrace | 1,100,976 |
| corpus, 60 largest | 434 | 56,732 shade_surface_light | 1,185,296 shader_eval_displace (dump effect) |
| corpus, EVERY distinct entry (3 shards) | 2,449 (418 distinct modules, 1,845 dumped) | 56,732 | as above |
- Size guard: `p8-size-guard` PASS. Corpus device pipeline logs (per scene, `corpus-dump/r.*/pipelines.jsonl`): no entry
  >= 1,110,000. Displace note RESOLVED as a measurement effect: device builds `shader_eval_displace` at 738,268 B in
  `displacement/both` and `passes/with-displacement-both` (`displace-log/`); the dumps measure 1,185,296 (val33 1,196,200).
- KNOWN 4 (nodes_world `shade_background` over the guard): the nodes_world gate was not dumped in this job set (as val33);
  not re-measured.

## Not run / limits
- Reruns of the 3 final-run failures (coordinator: no further runs).
- Velvet negative controls, tab31 A/B, OIDN, ARM64 cross-check: run (FINAL run slot W). S20 jobs: owed (S20 not attached).
- N/A here: android_app_gate, android_host_gate, linux_arm64_host_gate, macos_host_gate, macos_x64_host_gate,
  windows_host_gate. Licence audit/notices: skipped (no external code).
- nodes_world gate dumps for KNOWN 4: not dumped.

## Tool adaptations
- val33 tools seeded and adapted by the seeding task (`tools/adaptations.diff`, originals `seed/tools-orig/`).
  Mine (`tools/adaptations-val35-extra.diff`, pre-edit copies in `seed/tools-orig/`): pysite/sitecustomize.py tools path ->
  worktree; dump_runner.sh also writes CODEBRIX_CYCLES_PIPELINE_LOG per process; superset_dump.py root parents[5]->[4];
  slot budgets raised to 2x the Run A wall + 300 s (all-lanes 3,800, nodes-gate 8,000, abi-dump 900, envelope-m2 8,100,
  corpus 9,000, supersets 2,400/3,600/2,400 with inner 1,800); marker RUN-B-START -> RUN-FINAL-START; new lane L6 =
  slots V (drivers) + W (velvet controls, OIDN, displace log, tab31 A/B, ARM64 cross-check); post.sh adds the
  every-entry corpus envelope. New tools: linux_only.py, denoise_scenes.py, denoise_judge.py, velvet_neg.sh,
  tab31_strip.py, tab31_ab.sh, ab_compare.py, tab31_om.sh, tab31_variant.py, capability.py, shard_modules.py,
  corpus_totals.py, prune_bins.sh. Copied `native/work/merge4/debug` (3 symlinks into ~/.cache/debuginfod_client).
- Read-only pointers to main remaining: cmp2.py F= (val33 validation), cmp_cpu.py bases= (val33 corpus), ledger_f32.py MAIN
  (AND = r29-android runs for the 4 Android replay rows the ledger still cites: replay-principled-hp, replay-nodes-hp,
  replay-triangle-samsung, replay-terminator-samsung).

## Deliverables (read-only references for the device lanes)
- A1 `native/work/val35/reference/linux-overlay-hashes.json`; A5 `reference/libm-linux-x64-glibc.json`
- A2 `native/work/val35/host-gate/`; A3 `oracles/corpus/`; A4 `oracles/bench/`, `oracles/bundle/`; A6 `managed-linux-x64/`
- Gate results `validation/<label>/`; corpus compares `o-compare/`, `o-compare-summary.txt`; `corpus-totals.txt`, `cmp-cpu.txt`
- Frozen: `native/evidence/linux-x64-reference-20261101/`, `native/evidence/linux-x64-ledger-20261101/` (in branch val35).

## Evidence and ledger
- `native/evidence/linux-x64-reference-20261101/` (120 MB: environment, reference A1/A5, host-gate A2, oracles A3/A4
  incl. all pass films, managed A6, results incl. this report as run-report.md, README.md written by me) via
  `tools/build_evidence.py` (root -> worktree, report path -> agent-reports/val35.md). The pass scenes make the corpus
  102 MB (the 20261030 set had 116 scenes, 15 MB).
- `native/evidence/linux-x64-ledger-20261101/` (23 checkpoints) via `tools/ledger_f32.py` (root, run folder, date,
  produced text adapted; AND kept read-only on main for the 4 Android replay rows). All 23 checkpoints passed=True;
  2 case-count assertions dropped by the tool and re-added with the current values (ambient-occlusion
  `/reports/ao_render/result/cases` 24 -> 27; mnee `/linux/images/cases` 5 -> 9). FACT: the 20261030 ledger folder has
  no README.md, so none was written for this one.
- `compatibility/evidence.json` re-pointed (report, sha256, assertions, sourceSha256); `inventory.py check` PASS
  (after FIX-T4); test_inventory OK; test_guards OK. `git diff --check`: clean outside the evidence folders; the
  evidence data files carry the tools' trailing spaces, as the committed 20261030 set does.

## Docs (branch val35)
- `native/DEVICE.md`: open items 42 (resolved, FIX-1), 43 (Principled superset envelope), 44 (cryptomatte slot order);
  section "Linux x64 validation of the merged tree ..."; reference rule re-pointed.
- Re-pointed 20261030 -> 20261101: MAINTAINER-README.txt, compatibility/README.md, compatibility/evidence.json,
  native/DEVICE.md, docs/TESTING-MACOS-ARM64.md, -MACOS-X64.md, -WINDOWS-X64.md, -WINDOWS-ARM64.md, -LINUX-ARM64.md.
- `docs/PARITY.md`: no row changed status (e3's row stays "CLOSED (Linux)").

## Seal
- `native/work/val35/handoff/changes.patch` = `git diff --binary f1ed54307` (incl. the `git add -N` evidence files);
  sha256 and `git apply --check` results in `handoff/changes.patch.sha256` and `SEALED` (re-sealed after this report was
  copied into the evidence folder; the last SEALED line is the record). `git apply --check -R` in the worktree: OK;
  forward check on the f1ed54307 versions of the 17 modified files: OK.
- `native/work/val35/fixes.patch` (= the diff without the two evidence folders): sha256 on line 1 of this report.
- `handoff/changes.json` (per file sha256Before/After), `handoff/handoff.json` (items, rulings, fingerprints).
- Fingerprints: start `c630c970...6a90e1`; tested tree `2e262a5b...c0dada`; final (with evidence and docs) in
  `fingerprint-final.txt` and handoff.json.
- Processes: none of mine left (checked at the end; only the harness monitors, which I stopped).
- Git: read-only plus `git add -N` on the two evidence folders. No lock, no device, no adb.

## Decisions for Jeremy
- FIX-1 changes which WGSL text `prepare_surface` compiles from in scenes whose world or lamps use SVM node types no
  surface uses (shared native/cycles code, all RIDs). Please review it at the merge; the coordinator ruled the ARM64
  oracle stays valid.
- The Principled superset envelope (61,059) and the cryptomatte slot order need a lane: the first is a gate-superset
  entry over the envelope since the pinned surf_bump; the second needs a repeat to prove or disprove run-to-run.
- Whether denoise scenes should enter the committed corpus (the lane-tooling variants pass: section job 10).
- FIX-T4 touches tools/compatibility/inventory.py; nextbatch38's Embree import will touch the same list.
