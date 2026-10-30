# r29-linux: Linux x64 reference set at HEAD 686f79282 (2026-10-29, 10:53-12:32)

All outputs: /home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/refs-20261029/r29-linux (timeline.log, budgets.txt, validation/, tools/, orig-tools/, cmp-vs-w300.txt, cmp_docker.txt, oracle-wgsl-vs-cpu.txt).

## Start state (verified)
HEAD 686f79282, branch main; 5 modified docs/TESTING-*.md; `git diff --binary | sha256sum` = 2220dcea... (matches, unchanged at end). dotnet 10.0.403. Disk 228 GB at start, 208 GB at end. Host ldd -> ~/.nuget/packages/codebrix.webgpu.native.mitlicenseforever/1.0.300.210/runtimes/linux-x64/native/libcodebrix_wgpu.so sha256 5a61096ef092ae3fb6638189f1ea9fe7a99350f21376f5ea1ce9b22afce35b2b.

## Job table (10-27 / w300 value beside)
| Job | Verdict | Wall | Numbers (now / 10-27) |
|---|---|---|---|
| 2 all-targets build (66 targets, --parallel 6) | exit 0 (mnee gates now build) | 470 s (10:53:18-11:01:08) | w300: exit 1 at fmaxf; ALL-TARGETS BUILD DONE exit=0 written 11:01:08 |
| 2 bin/lib audit | all 64 executables/libs that link wgpu -> 1.0.300.210; 0 bad; 1 executable has no wgpu dependency | | 76 files sha256: linux-bin-sha256.txt, ldd.txt (w300: 65 entries, 63/65) |
| 3 dotnet build Release / Debug | 0 warnings, 0 errors each (incremental, 4 s / 2 s) | | |
| 3 dotnet test Release / Debug | PASS 185/185 each, 0 skipped | 145 s / 148 s | 185/185. GpuRenderTests (tests/CodeBrix.Graphics3D.Cycles.Tests/GpuRenderTests.cs) is in the assembly, 0 skipped; I did not list per-test results, so "ran" is INFERENCE from skipped=0 and the 2.4 min duration |
| 4 A2 host_gate full (GPU steps) | PASS, complete, exit 0 | 141 s | 3,146 comparisons, 0 practical, 152 strict, max 1.5259e-05 / 3,146, 0, 149, 1.53e-05. exportCount 48, independentReferenceFilms 786. Output: host-gate/ |
| 5 A1 overlay hashes | exit 0 | <1 s | reference/linux-overlay-hashes.json; vs Docker 10-27 file: 19 entries each, 3 differ: background_shader.inc, shader_data_shader.inc, volume_stack_shader.inc (as expected) |
| 6 A5 libm | exit 0 | 24 s | reference/libm-linux-x64-glibc.json; vs w300 record: all 163 values equal except /build/command[6] (temp dir name) |
| 7 A3 oracle corpus (cpu-x64-linux + wgsl-linux-intel) | exit 0, 54 scenes | 277 s | oracles/corpus. WGSL vs CPU: 54 pairs, 0 not passed, 0 practical, strict 7,367, max 4.77e-05 |
| 7 A4 bench bundle + bench_01, bench_01-hdri | bundle verified, exit 0; oracle exit 0 | 2 s / 148 s | oracles/bundle (17 files), sha256 list oracles/bundle-sha256.txt; oracles/bench: 2 pairs, 0 not passed, strict 564, max 9.16e-05 |
| 7 CPU films vs Docker set | corpus: 56 scenes in both, 0 not bitwise identical (2 scenes, assets/bench_01 and bench_01-hdri, exist only in my corpus run: the Docker corpus lacks them); bench: 2/2 bitwise identical to linux-docker-bench-2 | | cmp_docker.txt |
| 8 A6 managed reference films | exit 0 | 149 s | managed-linux-x64/{diffuse-sun,glass,textured-plane}.cpu.rgba.bin (12,288 B each) |
| 9 regression set, 4 lanes | 132 job rows, 129 exit 0, 3 non-zero (all known, below); NEW FAIL 0 | 11:19-12:32 | |

Known failures reproduced verbatim-class: p8-light-tree exit 1 (290 s); p8-light-tree-replay-2-alone exit 1 (49 s, "Native scheduler decision readback differs at command 51 address=4403888"); p8-sim650-nodes-world exit 1 (0 s, "WebGPU: texture, colour and utility shader nodes are not supported on Adreno (TM) 650 GPUs (this scene uses SVM_NODE_TEX_NOISE)...").

### Job 9 details vs w300 (cmp-vs-w300.txt, 93 of 117 labels have 0 diffs; the rest diff only as below)
- p8-mnee (new, mnee_gate now builds): PASS exit 0, 177 s; paths 23,757, strict 458, max 0.125 (equal to wave 8: 23,757 / 458 / 0.125); mnee_render cases strict 236/165/162/2,833/0 (max 6.1e-05). inputsUnchanged true.
- p8-envelope-m2: passed, 289 entries (w300 239 without mnee; wave 8 289), worst 54,153. abi 64/40,441; materials 63/59,416; environment 82/40,341; bench HDRI 24/51,291: all equal to w300.
- p8-all-lanes: PASS exit 0 (1,607 s; w300 1,597 s). Strict counts shift by 1-3 in the atomic family (broad_lighting replay 7 227->226, replay 8 228->230, feature_combination replay 5 27->28, broad_lighting strict 3512->3511, repeatBitwiseDifferences 8->9).
- WGSL byte sizes all +407 B (e.g. glass superset 331,970 -> 332,377; nodes 424,255 -> 424,662; tier gate wgslBytes +407): expected from the wavefront_shader_data.wgsl change. Size guard passed.
- Other value diffs vs w300: p8v-background strictFloatFailures 202 -> 218 (varies run to run, 195/198/219/240 seen on 10-27); ao_render repeatBitwiseDifferences 8 -> 7; transparent-dump 11 -> 7; transparent-shadow broad_lighting strict 3510 -> 3513, repeat diffs 10->7. Other diffs are pipeline timings (maxPipelineMs).
- All p8-s flows, gltf, p8v-*, probes, supersets (supersetIdentical), comparators (m2, starters, bench, HDRI, Mac) exit 0 / no problems.
- Light_tree replays (re-run after tool setup fix, below): replays 4,5,6 PASS (93 s): readbacks 462/391/445, max 4.888e-06 / 2.849e-05 / 1.895e-05, strict 370/174/612 (= w300). Replay 2 alone FAIL (known, same text). Replay 3 alone PASS (48 s): readbacks 459, max 9.537e-06, strict 310 (= w300). First attempt (before fix) failed: results.json FileNotFoundError, 0 s each, kept as validation/p8-light-tree-replays.failed-1, p8-light-tree-replay-2-alone.failed-1, p8-light-tree-replay-3-alone.failed-1.
- p8-principled-replay / p8-nodes-replay ran after the replay-build existed (replay-build built 12:18; slot D waits on supersets): both exit 0.

## Tool adaptations (originals in orig-tools/, adapted in tools/)
1. All tools: path `webgpu-300-210/w300-linux` -> `refs-20261029/r29-linux` (sed, from the already adapted w300 copies; superset_dump.py parents[5] kept: same depth). No remote prefix/app id (no Android).
2. slot8p_N.sh: p8-mnee line restored (from w300 orig/slot8p_N.sh, path-adapted). slot8p_A.sh: dotnet Debug test line removed (job 3 covers it). slot.sh: `cd $R` -> `cd $R; mkdir -p $V`.
3. New: ball.sh (all-targets build; targets from `cmake --target help`, 66), seq.sh (jobs 3-8), lane.sh (copy of w300 lane.sh path-adapted), redo_L.sh, cmp2.py (w300 cmp.py with F=w300 validation, W=mine), cmp_docker.py.
4. Setup step on coordinator instruction (12:18): linux-x64 render_replay driver built into my folder (render_replay.BASE=r29-linux/replay-build; render_replay.build('linux-x64')); libcodebrix_wgpu.so in it sha256 5a61096e... (ldd resolves to it). It was missing at 11:54 when slot L ran; reason: my tool copy (like w300's) points BASE at my folder, which I had not built.
5. Job 7 bench: bundle generated with make_bench_bundle.py --model bench_01.gltf --tool <Release GltfRender>; oracle run with both scene JSONs in one invocation.

## Deliverable paths
- A1 /home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/refs-20261029/r29-linux/reference/linux-overlay-hashes.json
- A2 /home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/refs-20261029/r29-linux/host-gate/ (status.json, comparisons.json)
- A3 /home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/refs-20261029/r29-linux/oracles/corpus/ (oracle/, comparisons/, run.json)
- A4 /home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/refs-20261029/r29-linux/oracles/bench/ and bundle /home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/refs-20261029/r29-linux/oracles/bundle/ (+ bundle-sha256.txt)
- A5 /home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/refs-20261029/r29-linux/reference/libm-linux-x64-glibc.json
- A6 /home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/refs-20261029/r29-linux/managed-linux-x64/
- Also (slot C, w300-style) /home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/refs-20261029/r29-linux/reference-p2/{libm-linux-x64-glibc.json,linux-x64/}; wave-style oracles in /home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/refs-20261029/r29-linux/oracles-p2/, bundles in /home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/refs-20261029/r29-linux/bundles-p2/.

## Not run
Nothing from the brief omitted. No devices, no locks.

## Cleanup / state
No processes of mine left (4 lanes + redo_L reported DONE; PIDs gone). Mesa caches removed by slot.sh (mesa/ empty). No tracked file changed; no git writes; nothing written under native/evidence/. No device locks held.
