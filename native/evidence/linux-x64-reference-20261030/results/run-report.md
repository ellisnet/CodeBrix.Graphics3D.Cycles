# f32-linux: final full Linux x64 regression after item27 / item27b (2026-10-30). RUN AND REPORT, nothing diagnosed

Folder: /home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/refs-20261030/f32-linux (timeline.log, budgets.txt, validation/<label>/{exit.json,slot.stdout.txt,out/}, freshness.txt, cmp-vs-r29.txt, o-compare/, o-compare-summary.txt, linux-bin-sha256.txt, ldd.txt, warn-counts.txt).
Run: 11:38-14:05:31 (build 11:38-11:45; sequence to 12:13; five lanes 12:13-14:04; jobs 10-11 to 14:05:31).

## Start/end state
- HEAD 51fc577cc + uncommitted merged tree. tree_fingerprint.sh start = end = b3439b9c8a5553dcfa9ccd3a1eba443c40df9817ca681db218e4f2c3154901af (fingerprint-start.txt / fingerprint-end.txt).
- dotnet 10.0.403; disk 140 GB free at start. Host lib ldd -> ~/.nuget/.../1.0.300.210/runtimes/linux-x64/native/libcodebrix_wgpu.so, sha256 5a61096ef092ae3fb6638189f1ea9fe7a99350f21376f5ea1ce9b22afce35b2b (job1.txt; the job1.txt ldd line was appended after the build because the clean had removed the lib; ldd.txt has the audit of all bin/lib).

## Freshness check (job 2)
Passed: log has `Built target codebrix_cycles_overlay` (1); no overlay *.inc newer than libcycles_device.a; zstd_compress EXEMPT (link.txt has neither libcycles_device.a nor libcodebrix_cycles_host); shadow_catcher_gate marker count 1; miss=0 (freshness.txt).

## Job table (now / f31 or r29)
| Job | Verdict | Wall | Numbers |
|---|---|---|---|
| 1 state | OK | | above |
| 2 clean + build_cycles.py --target all --jobs 6 | exit 0 | ~6.5 min | 49 warnings, all `-Wclass-memaccess`; 0 `-Wmisleading-indentation`, 0 `-Wunused-variable`. 64 bin/lib link 1.0.300.210, 0 bad, 2 with no wgpu dependency (atomic_film_gate, zstd_compress) |
| 3 dotnet build Release / Debug | 0 warnings, 0 errors | 4 s / 2 s | |
| 3 dotnet test Release / Debug | PASS 185/185 both | 160 s / 160 s | f31 185/185. GpuRenderTests: `--list-tests` in validation/j3-dotnet-test-list; that it ran is INFERENCE (0 skipped, ~160 s) |
| 4 A2 host_gate | PASS, complete, exit 0 | 136 s | 3,146 comparisons, 0 practical, 149 strict, max 1.5259e-05 / f31 148, r29 152 |
| 5 A1 overlay hashes | exit 0 | 0 s | 19 entries. Differ from f31: background_shader, shader_data_shader, surface_shader, svm_shader, volume_stack_shader (.inc). Differ from evidence/linux-x64-reference-20261029: background, shader_data, shadow, surface, svm, volume_stack |
| 5 A5 libm | exit 0 | 21 s | identical to f31 except temp-dir name in /build/command (/tmp/tmp_7442uel vs /tmp/tmpdgxinn8p) |
| 6 A3 oracle corpus (cpu-x64-linux + wgsl-linux-intel) | exit 0, 867 s | | 116 scenes listed; 114 compared, all passed: WGSL vs CPU 0 practical failures, 30,813 strict differences (f31 31,369). run.json failures (4, not new): assets/bench_01 and bench_01-hdri both backends (asset image-0.byte4 missing; rendered by the bench job), linux-gates/closed-volumes and world-volume WGSL refused by design ("cbc_host_render returned 3: WebGPU surface data passes not implemented") |
| 6 A4 bench bundle + oracle | bundle exit 0 (2 s); oracle exit 0 (155 s) | | oracles/bench, oracles/bundle |
| 6 CPU films vs earlier | all CPU .f32 byte-identical to f31 (224 same, 0 differ, 8 f31-missing = the 4 new enclosure scenes x 2 files); vs r29 112 common files same, 0 non-bitwise | | |
| 7 A6 managed export | exit 0 | 161 s | managed-linux-x64/ |
| 8/9 regression set, 5 lanes | all slots exit 0 except 2 expected (below) | 12:13-14:04 | |
| 10 nodes_render gate binary | exit 0, passed true | 33 s | strictStageFailures 3,213, max stage err 2.527e-05, strictImageFailures 793, max image err 2.027e-05 / f31 3,925, 2.527e-05, 1,824, 2.985e-04 |
| 11 compare.py per scene | 118 scenes: 114 exit 0 "perfect" (incl. all 4 combinations/enclosure-emissive-panel* ); 4 exit 2, empty verdict | | the 4 = assets/bench_01, assets/bench_01-hdri, linux-gates/closed-volumes, linux-gates/world-volume (same as f31) |

## Non-zero results (verbatim, not diagnosed)
1. p8-sim650-nodes-world exit 1 (0 s), by design.
2. x-inventory-check exit 1, expected; first message (stderr): `[Errno 2] No such file or directory: '/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/evidence/linux-x64-ledger-20261030/native-render/validation.json'`
3. Job 11: compare.py exit 2 for the four scenes above (o-compare/<scene>.txt).
No other non-zero. No retries needed.

## Job 8/9 values (now / f31)
- p8-shadow-catcher PASS 23 cases, 18/18 replays passed; gate strict 14,918, max 4.554e-05 (f31 17,253 / 1.383e-4); 435-522 s.
- p8-light-tree PASS 8 cases + 8 replay dirs; film strict 32,605 (as reported by results.json strictDifferences; f31 reported 5,804 by a different count), max 0.0227.
- p8-ao-state PASS 20 cases, max 1.211e-4 (f31 1.211e-4). p8-ao-render PASS 24 cases, strict 25,247, max 3.242e-05; p8-ao-render-replays PASS (24 entries, 415 s).
- p8-mnee PASS (results.json passed true; case 0 paths 5,966, max 0.125).
- p8-all-lanes PASS 1,553 s (f31 1,489; r29 1,607). cmp-vs-r29.txt: 91 labels diffs=0, remainder timings/count shifts/new cases.
- Size guard PASS. Intel compile envelope (entries / worst maxEndIp, all withinLimit true): abi 64/40,441; materials 63/53,904; m2 579/59,274 (closest, below 60,000); environment 82/40,341; bench-hdri 24/57,122; x-envelope 91/33,674. Worst module names not extracted (INFERENCE from item27b: m2 worst is shade_surface); see validation/p8-envelope-*/out/envelope.json.
- Single-run gates: licensing audit PASS, notices --check PASS, test_inventory PASS, test_guards PASS, atomic_film PASS, scene_edit_gate PASS (118 s), lighting_gate PASS (129 s), microfacet_gate native PASS, object_motion_gate PASS, inventory.py check exit 1 (expected). Gates inside the slot8p set (light_state, broad_light_state, emitter_sample, deforming/camera motion, transparent_shadow, tier, triangle_sample, surface, triangle_lighting, shadow_terminator, gltf tool, principled/nodes replays, p8v-*, probes, s-flows) not run twice; all exit 0.
- Bench scenes in the corpus job need the bench bundle image asset and are rendered by the bench job (not a failure).

## Tool adaptations (originals in orig-tools/)
f31 tools/scripts copied; sed `f31-linux/F31_/f31 -> f32-linux/F32_/f32` only. Plus one addition: fresh.sh now carries the zstd_compress exemption (EXEMPT line when its link.txt has no libcycles_device.a / libcodebrix_cycles_host) so run1.sh continues. cmp2.py compares against r29 validation (run from native/). Replay driver via pysite/sitecustomize + F32_REPLAY_BASE (as f31); no tracked file touched.

## Deliverables
- A1 .../f32-linux/reference/linux-overlay-hashes.json
- A2 .../f32-linux/host-gate/ (status.json, comparisons.json)
- A3 .../f32-linux/oracles/corpus/ (oracle/, comparisons/, run.json)
- A4 .../f32-linux/oracles/bench/ and oracles/bundle/
- A5 .../f32-linux/reference/libm-linux-x64-glibc.json
- A6 .../f32-linux/managed-linux-x64/
- Gate results: validation/<label>/{exit.json,slot.stdout.txt} and out/{status.json,results.json}; validation/v-nodes_render; o-compare/, o-compare-summary.txt.

## Not run
No Android/devices (by brief).

## Cleanup / state
No processes of mine alive (run1.pid, post.pid gone; no codebrix_cycles processes); no tracked file changed; nothing written under native/evidence/; no git writes; no locks.
