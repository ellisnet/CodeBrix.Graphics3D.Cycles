# Lane P: Pixel 4 XL triage under the device reference policy (2026-10-22)

Worktree `/home/jeremy/ClaudeHome/cycles-worktrees/p-pixel-triage` (branch p-pixel-triage, base 3392df3d).
Device: Pixel 4 XL `98271FFBA0048D`, Adreno 640, API 33. Policy: plan section 46. A job is not supported on the
Adreno 640 if it fails on the Pixel, or if it takes more than 3x its Samsung (Adreno 730) wall time. Nothing in
product code was changed. The only repository edits are `AGENT-README.txt` and `native/DEVICE.md` (patch sha256 `47602c7698976bc21d2f676225c2d8226589f89538da6340fb9129481c72a023`; `git apply --check -R` OK). Handoff: `native/work/p/handoff/`.

## Results

103 rows (101 cutoff-table rows plus the two extra item-2 variants):

| Verdict | Rows |
| --- | --- |
| SUPPORTED | 33 |
| NOT SUPPORTED (3x) | 26 |
| NOT SUPPORTED (fails) | 7 |
| PROVISIONAL (materials, Samsung also fails today) | 5 |
| NOT RUN (replay; its source job is not supported) | 30 |
| NO BASELINE (materials and node replays) | 2 |

All planned jobs ran. There is nothing left over from the plan.

## Triage table

Each row's log is in the handoff: `device/runs-pixel/<corpus>-p-<step>-<n>/` (`results.json`, `status.json`,
per-job stdout/stderr, pulled output). The table with a log path per row is in `device/triage-table.md` and
`device/triage-table.json`. Item 2's device-oracle and x64-oracle runs are the last two rows.

| Corpus | Job | Samsung wall (s) | Pixel wall (s) | Cutoff (s) | Pixel result | Verdict |
| --- | --- | --- | --- | --- | --- | --- |
| triangle | triangle_sample | 25.8 (fails) | 77.3 | 77.0 | FAIL (3x cutoff at 77 s) | NOT SUPPORTED (3x) |
| triangle | surface | 97.4 | 199.7 | 292.0 | PASS | SUPPORTED |
| triangle | triangle_lighting | 159.0 | 477.2 | 477.0 | FAIL (3x cutoff at 477 s) | NOT SUPPORTED (3x) |
| triangle | replay-0 | 21.4 | - | 64.0 | not run (source job failed) | NOT RUN (source job not supported) |
| triangle | replay-1 | 39.1 | - | 117.0 | not run (source job failed) | NOT RUN (source job not supported) |
| triangle | replay-2 | 39.2 | - | 118.0 | not run (source job failed) | NOT RUN (source job not supported) |
| triangle | replay-3 | 39.4 | - | 118.0 | not run (source job failed) | NOT RUN (source job not supported) |
| triangle | replay-4 | 39.0 | - | 117.0 | not run (source job failed) | NOT RUN (source job not supported) |
| triangle | replay-5 | 87.0 | - | 261.0 | not run (source job failed) | NOT RUN (source job not supported) |
| emitter | emitter_sample | 11.3 | 21.4 | 34.0 | PASS | SUPPORTED |
| hostcap-linux-triangle | tri0 | 21.7 | 61.4 | 65.0 | PASS | SUPPORTED |
| hostcap-linux-triangle | tri1 | 40.0 | 120.0 | 120.0 | FAIL (3x cutoff at 120 s) | NOT SUPPORTED (3x) |
| hostcap-linux-triangle | tri2 | 40.0 | 120.1 | 120.0 | FAIL (3x cutoff at 120 s) | NOT SUPPORTED (3x) |
| hostcap-linux-triangle | tri3 | 40.0 | 120.0 | 120.0 | FAIL (3x cutoff at 120 s) | NOT SUPPORTED (3x) |
| hostcap-linux-triangle | tri4 | 39.1 | 117.1 | 117.0 | FAIL (3x cutoff at 117 s) | NOT SUPPORTED (3x) |
| hostcap-linux-triangle | tri5 | 87.9 | 264.1 | 264.0 | FAIL (3x cutoff at 264 s) | NOT SUPPORTED (3x) |
| hostcap-linux-terminator | term0 | 17.2 | 39.2 | 52.0 | PASS | SUPPORTED |
| hostcap-linux-terminator | term1 | 19.6 | 51.5 | 59.0 | PASS | SUPPORTED |
| hostcap-linux-terminator | term2 | 19.5 | 51.2 | 58.0 | PASS | SUPPORTED |
| hostcap-linux-terminator | term3 | 19.4 | 50.6 | 58.0 | PASS | SUPPORTED |
| hostcap-linux-terminator | term4 | 33.7 | 101.1 | 101.0 | FAIL (3x cutoff at 101 s) | NOT SUPPORTED (3x) |
| hostcap-linux-terminator | term5 | 33.7 | 101.1 | 101.0 | FAIL (3x cutoff at 101 s) | NOT SUPPORTED (3x) |
| hostcap-linux-terminator | term6 | 33.5 | 100.1 | 100.0 | FAIL (3x cutoff at 100 s) | NOT SUPPORTED (3x) |
| hostcap-linux-terminator | term7 | 33.7 | 101.1 | 101.0 | FAIL (3x cutoff at 101 s) | NOT SUPPORTED (3x) |
| hostcap-linux-terminator | term8 | 33.7 | 101.1 | 101.0 | FAIL (3x cutoff at 101 s) | NOT SUPPORTED (3x) |
| hostcap-linux-terminator | term9 | 72.8 | 218.0 | 218.0 | FAIL (3x cutoff at 218 s) | NOT SUPPORTED (3x) |
| materials | nodes_svm | 879.2 (fails) | 158.3 | 2638.0 | FAIL: device lost | PROVISIONAL (NOT SUPPORTED (fails)) |
| materials | nodes_world | 509.8 | 6.8 | 1529.0 | FAIL: SVM error 8 | NOT SUPPORTED (fails) |
| materials | nodes_render | 153.2 (fails) | 143.1 | 460.0 | FAIL: SVM error 24 (8+16) | PROVISIONAL (NOT SUPPORTED (fails)) |
| materials | nodes-replay | - (fails) | - | - | not run (no Samsung baseline, deferred to merge) | NO BASELINE |
| materials | principled_svm | 71.2 (fails) | 115.0 | 214.0 | FAIL: SVM error 8 (probe) | PROVISIONAL (NOT SUPPORTED (fails)) |
| materials | principled_bounce | 26.3 (fails) | 79.2 | 79.0 | FAIL (3x cutoff at 79 s) | PROVISIONAL (NOT SUPPORTED (3x)) |
| materials | principled_render | 28.7 (fails) | 86.2 | 86.0 | FAIL (3x cutoff at 86 s) | PROVISIONAL (NOT SUPPORTED (3x)) |
| materials | principled-replay | - (fails) | - | - | not run (no Samsung baseline, deferred to merge) | NO BASELINE |
| lighting | broad_lighting | 68.3 | 203.7 | 205.0 | PASS | SUPPORTED |
| lighting | broad_lighting-replay-0 | 20.3 | 61.1 | 61.0 | FAIL (3x cutoff at 61 s) | NOT SUPPORTED (3x) |
| lighting | broad_lighting-replay-1 | 20.4 | 61.1 | 61.0 | FAIL (3x cutoff at 61 s) | NOT SUPPORTED (3x) |
| lighting | feature_combination | 42.8 | 128.2 | 128.0 | FAIL (3x cutoff at 128 s) | NOT SUPPORTED (3x) |
| lighting | feature_combination-replay-0 | 27.6 | - | 83.0 | not run (source job failed) | NOT RUN (source job not supported) |
| lighting | feature_combination-replay-1 | 27.9 | - | 84.0 | not run (source job failed) | NOT RUN (source job not supported) |
| lighting | lighting_mis | 33.4 | 96.4 | 100.0 | PASS | SUPPORTED |
| lighting | lighting_mis-replay-0 | 15.6 | 47.2 | 47.0 | FAIL (3x cutoff at 47 s) | NOT SUPPORTED (3x) |
| lighting | lighting_mis-replay-1 | 16.0 | 48.2 | 48.0 | FAIL (3x cutoff at 48 s) | NOT SUPPORTED (3x) |
| regression | glass_render | 79.9 | 121.4 | 240.0 | PASS | SUPPORTED |
| regression | lighting_mis | 33.4 | 96.4 | 100.0 | PASS | SUPPORTED |
| regression | transparent_lighting | 107.5 | 64.7 | 322.0 | FAIL: device lost | NOT SUPPORTED (fails) |
| transparent | transparent_shadow | 16.1 | 13.9 | 48.0 | FAIL: invalid state/data (mask 1) | NOT SUPPORTED (fails) |
| transparent | transparent_lighting | 106.9 | 64.7 | 321.0 | FAIL: device lost | NOT SUPPORTED (fails) |
| transparent | transparent_lighting-replay-0 | 23.7 | - | 71.0 | not run (source job failed) | NOT RUN (source job not supported) |
| transparent | transparent_lighting-replay-1 | 23.7 | - | 71.0 | not run (source job failed) | NOT RUN (source job not supported) |
| transparent | transparent_lighting-replay-2 | 30.5 | - | 92.0 | not run (source job failed) | NOT RUN (source job not supported) |
| transparent | transparent_lighting-replay-3 | 30.9 | - | 93.0 | not run (source job failed) | NOT RUN (source job not supported) |
| transparent | transparent_lighting-replay-4 | 31.4 | - | 94.0 | not run (source job failed) | NOT RUN (source job not supported) |
| transparent | transparent_lighting-replay-5 | 31.9 | - | 96.0 | not run (source job failed) | NOT RUN (source job not supported) |
| transparent | transparent_lighting-replay-6 | 30.2 | - | 91.0 | not run (source job failed) | NOT RUN (source job not supported) |
| transparent | transparent_lighting-replay-7 | 30.8 | - | 92.0 | not run (source job failed) | NOT RUN (source job not supported) |
| transparent | transparent-shadow-replay-direct | 3.9 | - | 12.0 | not run (source job failed) | NOT RUN (source job not supported) |
| transparent | transparent-shadow-replay-indexed | 4.0 | - | 12.0 | not run (source job failed) | NOT RUN (source job not supported) |
| terminator | shadow_terminator | 147.4 | 442.3 | 442.0 | FAIL (3x cutoff at 442 s) | NOT SUPPORTED (3x) |
| terminator | terminator-replay-0 | 15.3 | - | 46.0 | not run (source job failed) | NOT RUN (source job not supported) |
| terminator | terminator-replay-1 | 17.5 | - | 52.0 | not run (source job failed) | NOT RUN (source job not supported) |
| terminator | terminator-replay-2 | 17.6 | - | 53.0 | not run (source job failed) | NOT RUN (source job not supported) |
| terminator | terminator-replay-3 | 17.6 | - | 53.0 | not run (source job failed) | NOT RUN (source job not supported) |
| terminator | terminator-replay-4 | 29.8 | - | 89.0 | not run (source job failed) | NOT RUN (source job not supported) |
| terminator | terminator-replay-5 | 29.8 | - | 89.0 | not run (source job failed) | NOT RUN (source job not supported) |
| terminator | terminator-replay-6 | 29.8 | - | 89.0 | not run (source job failed) | NOT RUN (source job not supported) |
| terminator | terminator-replay-7 | 29.6 | - | 89.0 | not run (source job failed) | NOT RUN (source job not supported) |
| terminator | terminator-replay-8 | 29.9 | - | 90.0 | not run (source job failed) | NOT RUN (source job not supported) |
| terminator | terminator-replay-9 | 67.7 | - | 203.0 | not run (source job failed) | NOT RUN (source job not supported) |
| spec-refusal | glass_render-spec | 76.8 | 121.4 | 230.0 | PASS | SUPPORTED |
| spec-refusal | glass_render-nospec-refused | 0.3 | 0.9 | 5.0 | PASS | SUPPORTED |
| motion | object-moving-images | 84.9 | 252.9 | 255.0 | FAIL: SVM error 8 | NOT SUPPORTED (fails) |
| motion | object-replay-motion-0 | 66.8 | - | 200.0 | not run (source job failed) | NOT RUN (source job not supported) |
| motion | object-stationary-images | 36.2 | 109.2 | 109.0 | FAIL (3x cutoff at 109 s) | NOT SUPPORTED (3x) |
| motion | deforming-moving-images | 110.8 | 116.0 | 332.0 | FAIL: SVM error 8 | NOT SUPPORTED (fails) |
| motion | deforming-replay-motion-0 | 29.6 | - | 89.0 | not run (source job failed) | NOT RUN (source job not supported) |
| motion | deforming-stationary-images | 36.3 | 109.2 | 109.0 | FAIL (3x cutoff at 109 s) | NOT SUPPORTED (3x) |
| motion | camera-static | 7.9 | 10.0 | 24.0 | PASS | SUPPORTED |
| motion | camera-motion | 14.2 | 37.3 | 43.0 | PASS | SUPPORTED |
| precision | closest | 21.4 | 40.4 | 64.0 | PASS | SUPPORTED |
| precision | shadow | 16.2 | 16.7 | 49.0 | PASS | SUPPORTED |
| precision | shadow-replay-direct | 1.5 | 2.0 | 5.0 | PASS | SUPPORTED |
| precision | shadow-replay-indexed | 1.5 | 2.0 | 5.0 | PASS | SUPPORTED |
| volume | volume-state | 13.2 | 24.9 | 40.0 | PASS | SUPPORTED |
| volume | volume-absorption | 120.8 | 140.1 | 362.0 | PASS | SUPPORTED |
| volume | volume-isotropic | 172.0 | 179.2 | 516.0 | PASS | SUPPORTED |
| volume | volume-forward | 170.8 | 180.4 | 512.0 | PASS | SUPPORTED |
| volume | volume-backward | 171.7 | 182.8 | 515.0 | PASS | SUPPORTED |
| volume | boundary-state | 32.4 | 81.0 | 97.0 | PASS | SUPPORTED |
| volume | boundary-compaction-guard | 6.4 | 19.2 | 19.0 | FAIL (3x cutoff at 19 s) | NOT SUPPORTED (3x) |
| volume | closed-single | 214.4 | 219.4 | 643.0 | PASS | SUPPORTED |
| volume | closed-overlap | 319.9 | 345.9 | 960.0 | PASS | SUPPORTED |
| volume | closed-inside | 302.9 | 337.5 | 909.0 | PASS | SUPPORTED |
| volume | volume-replay-absorption | 19.5 | 50.1 | 58.0 | PASS | SUPPORTED |
| volume | volume-replay-isotropic | 20.6 | 50.5 | 62.0 | PASS | SUPPORTED |
| volume | volume-replay-forward | 20.5 | 50.3 | 62.0 | PASS | SUPPORTED |
| volume | volume-replay-backward | 20.7 | 54.8 | 62.0 | PASS | SUPPORTED |
| volume | closed-replay-single | 21.5 | 54.1 | 64.0 | PASS | SUPPORTED |
| volume | closed-replay-overlap | 33.9 | 102.1 | 102.0 | FAIL (3x cutoff at 102 s) | NOT SUPPORTED (3x) |
| volume | closed-replay-inside | 34.1 | 102.2 | 102.0 | FAIL (3x cutoff at 102 s) | NOT SUPPORTED (3x) |
| app | managed-app-3-scenes | 134.4 | >403 (result timeout) | 403.0 | FAIL (3x cutoff at 403 s) | NOT SUPPORTED (3x) |
| motion (item 2) | object-moving-images, device oracle, no reference import | 84.9 | 255.3 | 255.0 | FAIL (3x cutoff at 255 s) | NOT SUPPORTED (3x) |
| motion (item 2) | object-moving-images, CODEBRIX_CYCLES_REFERENCE_ORACLE=linux-x64-sse4.2, no reference import | 84.9 | 251.8 | 255.0 | FAIL: SVM error 8 | NOT SUPPORTED (fails) |

## Where the cutoffs come from

Rule: cutoff = max(5 s, round(3 x the Samsung wall)). The 5 s floor covers adb launch latency and affects only:
- glass_render-nospec-refused (Samsung 0.3 s);
- the two precision shadow replays (1.5 s each).

The cutoff table with a source per row is in `device/cutoff-table.json`. Sources:
- merge 3 (`main:native/work/merge3/runs/samsung/`):
  - triangle-mw3: triangle_sample, surface, triangle_lighting and replays 0-5;
  - emitter-mw3;
  - hostcap-linux-triangle and hostcap-linux-terminator: per-capture walls;
  - materials-mw3: nodes_svm, nodes_world and nodes_render. The nodes_svm and nodes_render runs FAIL on the
    Samsung (items 8 and 9), but they ran to completion.
- merge 2 (`main:native/work/merge2/runs/samsung/`):
  - materials-mw2a / mw2r: principled_svm, principled_bounce and principled_render. These are failing Samsung runs:
    their walls measure time to failure. Treat those three cutoffs as provisional;
  - spec-refusal-mw2.
- merge 1 (`main:native/work/merge1/runs/samsung/`): lighting-mw1, regression-mw1, transparent-mw1,
  terminator-mw1, motion-mw1, precision-mw1, volume-mw1.
- Lane P timing session: one Samsung lock session, 11:29-11:33, 3.6 min. Lock `device-locks/samsung` was free;
  logs in `device/runs-samsung/`. It timed object-stationary-images (36.2 s), deforming-stationary-images (36.3 s),
  and lighting_mis replay 0 (15.6 s) and replay 1 (16.0 s). lighting_mis itself took 29 s there, against 33.4 s in
  merge 1.
- No baseline anywhere: nodes-replay and principled-replay. Their source jobs fail on the Samsung, so the replays
  never ran there. Deferred to merge.
- The app's Samsung time is merge 3's `app-samsung.log`, appSeconds 134.4. The cutoff (403 s) was the gate's
  `--result-timeout`.

## AGENT-README text added

This is the revised, consumer-only text after the coordinator's revision. It sits before "WHAT THIS PACKAGE DOES NOT DO". The diagnostic entries (x64 reference-oracle import, on-device command replays, the volume compaction guard) moved to the DEVICE.md lane P section.

```
NOT SUPPORTED ON ADRENO 640 (PIXEL 4 XL CLASS DEVICES)
======================================================
Android devices with a Qualcomm Adreno 640 GPU (for example the Pixel 4 XL) are
not reference devices for this package. The ARM64 Android reference is an
Adreno 730 device. The policy:
- functionality that works on the reference device but fails on an Adreno 640
  is not supported there;
- functionality that takes more than three times as long as on the reference
  device is treated as a failure and is not supported there either.
The package does not block these features on an Adreno 640. They may render
wrongly, stop with a WebGPU error or a lost device, or take too long. Do not
build a product that depends on them on this GPU. Maintainers: the evidence is
in native/DEVICE.md, "Pixel 4 XL triage under the device reference policy".
This list says nothing about other GPU families (Mali, other Adreno
generations).

Not supported (fails on the Adreno 640):
- Transparent shadows and Transparent BSDF lighting. The transparent-shadow
  stage reports invalid data, and renders with transparent surfaces lose the
  device.
- Object motion blur (moving objects). It either fails or takes more than three
  times the reference device's time.
- Deformation motion blur (vertex motion). The render reports an invalid SVM
  stack.
- World (background) shaders built from texture, colour or utility nodes. The
  world evaluation reports an invalid SVM stack.

Not supported (more than three times the reference device's time):
- Emissive mesh (triangle) lights, for rendering and for light sampling.
- Point, sun and mesh lighting mixed with transmission and forward-hit MIS in
  one scene.
- Shadow terminator geometry and shading offsets.
- Scenes with motion-blur-capable objects or deforming meshes, even when
  rendered without motion.
- Managed .NET WebGPU rendering of simple scenes (glass, diffuse sun, textured
  plane) is more than three times slower than on the reference device.
  Pipeline compilation accounts for most of the difference.

Provisional (the reference device also fails these today; they will be
re-judged once it passes, and may move to either list):
- Principled BSDF materials. Their evaluation reports an invalid SVM stack, and
  bounces and renders take more than three times the reference time.
- Texture, colour and utility nodes in surface shaders. Creating their shaders
  can lose the device, and renders report an invalid SVM stack.

Supported on the Adreno 640 within the policy:
- perspective cameras, static and with camera motion;
- closest-hit and shadow ray traversal;
- glass;
- point, sun, spot and area lighting with MIS;
- point-lamp emission;
- homogeneous volumes: absorption, isotropic, forward and backward scattering,
  closed and overlapping volumes, and a camera inside a volume.
Pipeline compilation is still much slower than on the reference device. Expect
compile-time warnings over the 20 s mobile budget.
```

## Changes to DEVICE.md

- New section at the end: "Pixel 4 XL triage under the device reference policy (lane P, 2026-10-22)". It holds the
  method, the full triage table, a summary, a "Diagnostic-only results" list (x64 reference-oracle import,
  on-device replays, compaction guard) and the compile-time list.
- Open items 2, 6 and 7 are now "Closed under the device reference policy". Item 10 is "Partly closed ... the rest is
  PROVISIONAL". Each keeps its original text after the new lead.
- Not changed: the tier section, the known-adapter table, the persisted record, and the E2/E5 fix sections.

## What happened to open items 2, 6, 7 and 10

- **Item 2, closed (not supported on Adreno 640).** Three runs of object-moving-images, cutoff 255 s
  (3 x Samsung 84.9 s):
  - the merge-chain flow (imported glibc reference, so the x64 oracle arithmetic is used): FAIL, SVM error 8 at
    252.9 s (`motion-p-item2-chain-*`);
  - `CODEBRIX_CYCLES_REFERENCE_ORACLE=linux-x64-sse4.2` with no import: FAIL, SVM error 8 at 251.8 s
    (`motion-p-item2-x64-oracle-*`);
  - the default (the device's own oracle): FAIL (3x cutoff at 255 s) (`motion-p-item2-device-oracle-*`).

  The brief expected the default to pass. It did not finish within the cutoff. Earlier passing runs took 338-340 s,
  which is also over 3x. So object motion is not supported with either oracle, and "the gate keeps the device
  oracle" no longer rescues it. No gate or adapter change was made.
- **Item 6, closed, not fixed.** Jobs failing with SVM error 8, the signature of the array class's lost
  `svm_stack` mask bits (signature only, not localised):
  - object motion (x64 oracle);
  - deforming-moving-images;
  - nodes_world;
  - principled_svm;
  - nodes_render (error 24 = 8 + 16).

  Other signatures, not this class:
  - transparent_shadow: mask 1, invalid state;
  - transparent_lighting: `wgpuQueueSubmit: Parent device is lost`, which may be the item-3 long-submission reset
    (unverified);
  - nodes_svm: device lost creating the pipeline.
- **Item 7, closed (3x rule).** Compile time is part of each job's wall time. Over-budget compiles recorded (all on
  the full tier, warnings only):
  - node-render `prepare_surface`: 119.7 s;
  - object motion: `intersect_closest` 85 s, `prepare_shader_data` 58 s, `prepare_surface` 56 s;
  - deforming `prepare_surface`: 54 s;
  - lighting `shade_surface`: 27-32 s;
  - volume `shade_volume`: 29-30 s;
  - glass `shade_surface`: 24 s;
  - camera-motion `init_camera`: 27 s.

  Jobs stopped at their cutoff left no pulled pipeline log.
- **Item 10, partly closed.**
  - Definitive: nodes_world fails (SVM error 8 at 6.8 s) while the Samsung passes it (509.8 s), so it is not
    supported.
  - PROVISIONAL, rerun at merge after lane S: principled_svm (SVM error 8), principled_bounce and principled_render
    (3x), nodes_svm (device lost), nodes_render (SVM error 24).

## Other findings

- **transparent_shadow and transparent_lighting now fail on the Pixel.** Both passed there in Phase C4 and E2.
  This was one run each, and under the policy they were not investigated. They are marked not supported.
  Decision 2 below asks whether to give them one confirmation rerun at merge.
- **Two jobs passed with very little margin**, single runs on both devices: broad_lighting (203.7 s against a
  205 s cutoff) and lighting_mis (96.4 s against 100 s).
- **Several jobs ended exactly at their cutoff.** Pixel times are about 3x the Samsung's, mostly cold pipeline
  compilation. The cutoff decides the verdict for: triangle_sample, triangle_lighting, feature_combination,
  shadow_terminator, the stationary motion images, the app, and replays tri1-5, term4-9, lighting and
  closed-overlap/inside.
- **The merge-3 Linux captures could not be reused.** Merge 3's scratch deletion removed their `run.arena.bin`. I
  regenerated them in this tree with `triangle_lighting_gate.py` (30 s) and `shadow_terminator_gate.py` (54 s)
  on the Linux host. Samsung walls for those replays still come from merge 3.
- **"killed=False" in the cutoff logs.** The remote gate had already exited when its adb session was killed:
  `readlink /proc/PID/exe` was empty. The post-cutoff process lists show no codebrix process.
- **Builds in this tree** (`--jobs 6`): Linux host (452 s), Linux scene-reference gates (189 s), Android arm64 gates
  (406 s), Android host arm64 and x64 (223 s / 222 s; the APK builder needs both), and the APK
  (`com.codebrix.cycles.hostvalidation.p`). The APK build read its references from
  `main:native/work/merge3/validation/l1-host-gate/out`, read-only, because that folder's `comparisons.json`
  holds absolute paths.
- **Read-only inputs copied from main:** the Android dependency prefixes and
  `merge3/reference/{linux-x64,managed-linux-x64}`.
- **Scratch:** after hashing to `deleted-scratch-sha256.txt` (43,306 files), I deleted the Android build and
  dependency trees, the APK build tree and the run arenas.

## Not run, and why

- nodes-replay and principled-replay: no Samsung baseline (their source jobs fail on the Samsung). Deferred to merge.
- 30 replay rows whose source job was not supported (cut at 3x, or failed): triangle replays 0-5,
  feature_combination 0-1, transparent_lighting 0-7, the transparent-shadow replays, terminator 0-9, and object and
  deforming motion replay 0.
- None of the planned jobs was skipped for time.

## Timeline (lane P, 2026-10-22)

| Time | Event |
| --- | --- |
| 11:05 | Start (worktree clean, Pixel lock free) |
| 11:07-11:28 | Linux host build, Android arm64 gate build, Linux gate build, stage review |
| 11:29-11:33 | Samsung timing session, then the Samsung lock was released |
| 11:31-11:57 | Pixel batch 1: item 2 (three variants) and materials (six jobs) |
| 11:35-11:43 | Android host builds and APK |
| 11:57-12:52 | Pixel batch 2: triangle, emitter, lighting, spec-refusal, transparent, terminator, motion, precision, app. The hostcap steps failed at push because the arenas were missing. |
| 12:53-12:55 | Linux captures regenerated |
| 12:52-13:32 | Pixel batch 3: volume (ten jobs and seven replays) |
| 13:32-14:01 | Pixel batch 4: hostcap triangle (6) and terminator (10) |
| 14:02 | Pixel clean, lock released |
| 14:02-14:10 | Docs, scratch deletion, handoff, report |

The full log is `handoff/timeline.log`, with every DECLARED budget line in `handoff/budgets.txt`. The lock was held
only during batches, and the device was cleaned and the lock released between them.

## Device state at the end

- The lane P lock (`device-locks/pixel`) is released. Other lanes' locks `samsung` and `s20` were present and were
  not touched.
- No `/data/local/tmp/codebrix-p-*` folders and no lane P processes remain on the Pixel. The lane P app was
  uninstalled.
- Codebrix packages I did not install (for example `com.codebrix.cycles.hostvalidation`,
  `com.codebrix.cycles.hostexperiment`, `com.codebrix.samples.*`) were left alone.
- The HP was not touched.

## Decisions needed from Jeremy

1. **The 5 s floor** on 3x cutoffs below 5 s (adb latency). It affects three trivially passing jobs.
2. **Transparent shadows and lighting.** They are newly failing on the Pixel, from one run each. Accept the
   NOT SUPPORTED verdict as it stands, or allow one confirmation rerun at merge (cutoffs 48 s and 321 s)?
3. **The app result.** The managed app's three scenes miss their 3x cutoff (403 s; merge 3 took 425.5 s). Together
   with the compile times, this means practical rendering on the Adreno 640 is slower than the policy allows across
   the board. Should AGENT-README say that broadly, beyond the per-feature list?
4. **Principled cutoffs.** They come from failing Samsung runs (time to device loss). Lane S's Samsung walls should
   replace them at merge.
5. **The tier-table row (proposal only; nothing changed).** Change the Pixel 4 XL row's wording from
   "full, 20 s compile budget" to "full, 20 s compile budget; nice-to-have device, not a reference (see
   'NOT SUPPORTED ON ADRENO 640' in AGENT-README)". The decision logic would not change: the full tier stays right,
   since the conservative tier was not faster and the failures are not tier-dependent.

## What the merge runner should rerun on the Pixel after lanes S and L merge

Recompute every cutoff as 3 x that merge's own Samsung wall where the Samsung reran the job, otherwise use the values
below. One job at a time, using `tools/pixel_batch.py` (flags as in this lane). The lists below are about 45 minutes
of device time at worst.

- **Provisional materials (required):**
  - principled_svm: 214 s;
  - principled_bounce: 79 s;
  - principled_render: 86 s, plus principled-replay (needs a Samsung wall from lane S);
  - nodes_svm: 2638 s (a lane S Samsung wall will be much better);
  - nodes_render: 460 s, plus nodes-replay (needs a Samsung wall);
  - nodes_world: 1529 s, recheck only if lane S touched the SVM interpreter or the world path.
- **Sentinels that confirm the supported set did not regress (recommended):**
  - emitter_sample: 34 s;
  - surface: 292 s;
  - lighting_mis: 100 s;
  - broad_lighting: 205 s (marginal);
  - glass_render-spec: 230 s;
  - camera-motion: 43 s;
  - closest: 64 s;
  - shadow: 49 s;
  - volume-absorption: 362 s;
  - closed-single: 643 s.
- **Optional:** transparent_shadow (48 s) and transparent_lighting (321 s), if decision 2 allows a confirmation run.
- **Not needed:** anything already NOT SUPPORTED under the 3x rule, unless a lane changes those kernels
  substantially.
