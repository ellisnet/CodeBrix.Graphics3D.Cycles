# r29-android report (refs at HEAD 686f79282, runtime 1.0.300.210, .NET SDK 10.0.403)
Folder: `native/work/refs-20261029/r29-android/` (timeline.log, budgets.txt, runs/, runs-table-{samsung,hp,s20}.txt, counts.txt, app-summary.txt, c1-sha256.txt, device/, validation/, app/, app2/, reference/, tools/, orig/). Started 10:53, finished about 16:20. No tracked file changed by me; read-only git only; nothing written under native/evidence/.

## Start state
HEAD 686f79282, branch main, `git diff --binary | sha256sum` = 2220dcea... at start (matches docs-only.patch), `dotnet --version` 10.0.403, devices tablet R52W207XQ1V, S20 RFCRB0MPHYH, HP 192.168.86.225:5555 attached, 228 GB free. At the end `git status` shows additionally ` M MAINTAINER-README.txt` and untracked `native/evidence/linux-x64-reference-20261029/` (not mine; coordinator work). Pixel 4 XL never touched.
Runtime: `stage.py review` asserts lib/libcodebrix_wgpu.so = arm64 52dc9667c0e58002..., x64 005633eaad3d4442... (staged lists: staged-binaries-android-{arm64,x64}.json, 79 files each); the nupkg files hash to exactly those.

## Timeline of work (HH:MM)
- 10:53-11:19 builds (all kinds incl. `device`, mnee, mnee_render; `build_android_m6.py` then `tools/build_android.py --skip-dependencies --jobs 6`): gate arm64 497 s, x64 465 s; host x64 269 s, arm64 268 s; all exit 0 (this time mnee builds; the fmaxf fix is in HEAD).
- 11:20-11:22 libm record + Linux reference export (8 jobs, 108 s, exit 0). Oracle sha256 28e2f9e821d420468c4509d81da718f558e1d8a376bdfa05edaca500abbb59a5 = the same value as 10-27 (w300 export). Started only after r29-linux `ALL-TARGETS BUILD DONE exit=0` (11:01:08).
- 11:22-12:05 STALL on my side (coordinator noticed; I did not start device runs until 12:05). 12:05-15:18 tier 1 + tier 2 chains on tablet/HP/S20; 12:05-12:09 Linux triangle and shadow-terminator captures (my own, `validation/r29-triangle`, `r29-shadow-terminator`, exit 0 each). Another gap 15:18-16:01 (second stall). 16:01-16:19 managed app gates. 16:03-16:09 S20 MNEE.

## Results (job rows / replay rows), r29 vs 10-27 (w300-android)
| Device | r29 jobs | r29 replays | 10-27 jobs | 10-27 replays |
|---|---|---|---|---|
| Tablet (Adreno 730) | 28 PASS, 2 FAIL | 80 PASS, 6 FAIL | 28 / 2 | 80 / 6 |
| HP (Intel Xe) | 30 PASS, 0 FAIL | 85 PASS, 1 FAIL | 30 / 0 | 69 / 1 |
| S20 (Adreno 650) | 16 PASS, 8 FAIL | 16 PASS, 29 FAIL | 16 / 8 | 16 / 29 |
HP replays 85 vs 69: the HP host-capture replays (6 triangle + 10 terminator, all PASS) ran this time. Per-job tables: runs-table-*.txt; diff vs 10-27 in counts.txt.
Strict-count differences vs 10-27 (all practical 0, max unchanged): tablet ao_render 1743 (1737), broad_lighting 954 (952), light_tree 15,005 (15,004), ao_render-replay-4 238 (239), -replay-7 247 (246); HP ao_render 4,361 (4,360), broad_lighting 1,733 (1,732), ao replay-4 479 (478). Everything else equal. INFERENCE: +-1..6 strict differences from nondeterministic atomic accumulation order or the code changes since 10-27; not diagnosed.

### New failures
None. Every FAIL equals the known/expected list, verbatim texts below.
- Tablet light_tree-replay-2: `Native scheduler decision readback differs at command 51 address=4403824` (HP same text, address=4403888; known case-2 replay). light_tree gate itself PASS on both (tablet practical 0, strict 15,005, max 0.0145).
- Tablet mnee_render and mnee: `WebGPU scheduler kernel reported invalid data (mask 1): invalid state/data`; 5 mnee replays "source job failed" (not run); device and closest PASS. Known.
- HP: mnee_render PASS (strict 1,072, max 1.23e-05), mnee, device, closest, 5 replays PASS.
- S20 known failures reproduced: principled_render, principled_bounce, object-moving-images, deforming-moving-images (all `mask 8: invalid/uninitialized SVM stack`; replays "source job failed"), light_tree (179 practical, strict 2,026, max 1.61e-03; 7 replays not run), mnee_render and mnee (`mask 1`), closest (Adreno 650 AO-node refusal text), environment refused at case 0. S20 PASS: principled_svm, transparent_shadow, triangle_sample, surface, glass_render, lighting_mis, transparent_lighting (8 replays), shadow_catcher (6 replays), AO refusals + replace, a650 refusals.

### Android host gate (android_host_gate.py, references = r29-linux/host-gate at HEAD)
Tablet PASS (114 s): films 1579, practical 0, required strict 0, image strict diagnostics 50,798, max 9.54e-06. HP PASS (147 s): films 1579, practical 0, strict required 0, diagnostics 92,670, max 1.34e-05. S20 not run (not asked).

### Managed app gates (APK built 224e145e97ff8ce6c5048136aa4addb6d0b66bac919e6570f22510fea32fee45, id com.codebrix.cycles.hostvalidation.r29, `build_android_app.py` OK; run with `android_app_gate.py --uninstall`, result timeout 2400)
Linux references used (r29-linux, not my own): webgpu mode `r29-linux/managed-linux-x64/`; control bundle `r29-linux/bundles-p2/control` + film `r29-linux/oracles-p2/control/oracle/gltf/textured-plane/cpu-x64-linux/film-0.f32`; bench/HDRI bundle `r29-linux/oracles/bundle` + films `r29-linux/oracles/bench/oracle/assets/{bench_01,bench_01-hdri}/cpu-x64-linux/film-0.f32`.
| Run | Tablet | HP |
|---|---|---|
| webgpu (3 scenes) | PASS, app 62.1 s, practical 0, strict 913, max 9.36e-06 | PASS, 148.5 s, practical 0, strict 866, max 4.17e-07 |
| bundle control | PASS, 119.9 s, practical 0, strict 508, max 1.66e-04 | PASS, 191.3 s, practical 0, strict 777, max 1.62e-04 |
| bundle bench | PASS, 101.3 s, practical 0, strict 111, max 4.29e-05 | PASS, 191.3 s, practical 0, strict 211, max 3.77e-05 |
| bundle HDRI | PASS, 208.8 s, practical 0, strict 322, max 7.71e-05 | PASS, 275.9 s, practical 0, strict 436, max 9.17e-05 |
All tier full. 10-27: webgpu 897/901 strict, control 505/772, bench 116/210, HDRI 321/432 (tablet/HP). Compile-budget capability warnings present in some bundle runs (tablet 2/1/2, HP 2/1/1; webgpu 0); the app's own diagnostic in-app-CPU comparison (e.g. HP HDRI 48 practical) is not the gate verdict.

## DELIVERABLE C1 (tablet triangle_sample device-scene run, left in place)
Run folder `/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/refs-20261029/r29-android/runs/samsung/triangle-r29/` (file names exactly as expected; paths relative to it). Hashes also in c1-sha256.txt.
| File | sha256 |
|---|---|
| scenes/triangle_sample/samples.scene | 16b2f997a28f16421ad928462da21d2d101c7d0438583cff91e3c0aa8380ee33 |
| scenes/triangle_sample/samples.local.f32 | 9f75625cce13129c24244c23398eeac05eb10b4b7b58152787ec06bbe1dbca05 |
| device-scene-reference/references/triangle_sample/samples.f32 | 042c3d1c08be2fc31bdacd367220972be7bb427309797468c15c8871277d90b4 |
| device-scene-reference/triangle_sample.provenance.json | 860e123b25634d48a4e5617e704efb7f2292fefa9834df0270fc75e647fb1ebb |
| output/triangle_sample/run.actual.bin | d3b31384b8ad2f42191c09265c5ac03b6b6746d15ae995b4078202df41c459c1 |
| output/triangle_sample/run.reference.bin | 9f75625cce13129c24244c23398eeac05eb10b4b7b58152787ec06bbe1dbca05 |
| output/triangle_sample/run.reference-diagnostic.jsonl | 8c55c3fb888464d2e6683efa4f7887f6756ba163ff0f2dce0ec694d76196c933 |
Run verdict: triangle_sample PASS, glibc practical 0, strict 865, max 4.88e-04 (same as 10-27). Note samples.local.f32 and run.reference.bin hash equal (same bytes). The folder is 12 GB in total under runs/; only the above are C1.

## DELIVERABLE C3 (glibc-judged practical/strict/max; verdict)
| Job | Tablet (Adreno 730) | S20 (Adreno 650) | HP (Intel Xe) |
|---|---|---|---|
| glass_render | PASS 0 / 63 / 1.91e-06 | PASS 0 / 64 / 1.91e-06 | PASS 0 / 104 / 9.54e-07 |
| deforming_motion_render, object motion | not in tablet/HP job set | FAIL both (deforming-moving-images, object-moving-images): `mask 8: invalid/uninitialized SVM stack`, no numbers | not in job set |
| broad_lighting (area lights) | PASS 0 / 954 / 3.81e-05 | not in S20 job set | PASS 0 / 1,733 / 5.34e-05 |
| lighting_mis | PASS 0 / 1,057 / 6.08e-06 | PASS 0 / 1,057 / 6.08e-06 | PASS 0 / 2,682 / 8.34e-06 |
| microfacet_render | not in any device job set (gate built, not run) | not run | not run |
| principled_render | PASS 0 / 67 / 6.44e-06 | FAIL `mask 8` (no numbers) | PASS 0 / 91 / 6.44e-06 |
| nodes_render | PASS 0 / 1,689 / 3.72e-05 | refused by design (Adreno 650 nodes refusals PASS) | PASS 0 / 1,794 / 1.03e-04 |
| mnee_render | FAIL `mask 1` (known) | FAIL `mask 1` | PASS 0 / 1,072 / 1.23e-05 |
| mnee | FAIL `mask 1` | FAIL `mask 1` | PASS |
So on this HEAD the tablet (Adreno/Vulkan) renders smooth-shaded BSDF jobs (glass, lighting, principled, nodes) within the practical criterion; the Adreno 650 S20 fails principled/motion with the SVM-stack error. FACT only; no inference on Windows ARM64.

## Not run / incidents (none is a gate result)
- Managed-app and pipeline times of tier 2 on S20 MNEE: first two S20 mnee attempts failed in 18 s: (1) staged binaries had been removed by the S20 chain's end cleanup (`CalledProcessError ... sha256sum /data/local/tmp/codebrix-r29-stage/bin/...`), (2) `FileExistsError: runs/s20/mnee-r29` (leftover folder; moved to `mnee-r29-aborted-nostage`), plus a mistyped device name `s20x` (staging failed, no device touched). Third attempt (restage + mnee) completed: results above. More than one retry of my own script errors, none of a flaky step.
- The APK build's END line was lost from timeline.log when my shell exited; build verified from app/build/status.json (complete, passed).
- Not run: Android host gate on S20; film_compare; microfacet_render/deforming/object motion on tablet and HP (not in the w300 job set).
- Stalls at 11:22-12:05 and 15:18-16:01 were mine (turn ended without a pending monitor).

## Tool adaptations (orig/ vs tools/)
Paths and prefixes only: REPO via parents[4]; `webgpu-300-210/w300-android` -> `refs-20261029/r29-android`; remote prefix `codebrix-w300-` -> `codebrix-r29-`; labels `w300` -> `r29`; app id `.w300` -> `.r29`; host-gate references -> `refs-20261029/r29-linux/host-gate`; Linux hostcap captures -> my `validation/r29-*`; app2.sh uses r29-linux references and `app/build`; lock owner text. New: tools/builds.sh, ref.sh, drive.sh, linuxcap.sh, s20_mnee.sh. build_android_m6.py still builds in `native/work/merge5/build-<rid>` (as before). Diff counts: stage.py 1 changed line, run_corpus.py 5, replay_host_captures.py 4.

## Cleanup and locks
Each device cleaned under its lock (cleanup8.sh; `cleaned (left: '')` for samsung 16:13, hp 16:19, s20 16:09 and 16:04). Final check: no `codebrix-r29*` in /data/local/tmp, no `.r29` package, no codebrix/render_replay process on any device; codebrix-ncdbg and other apps untouched. All locks released (`device-locks/` empty). No processes of mine running.
