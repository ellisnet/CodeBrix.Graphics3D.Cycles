
## Samsung Galaxy S20 FE 5G (Adreno 650) sweep (lane Q, 2026-10-22)

A sweep only: every completed device functionality was run once on a new device, with no product, gate,
tolerance or tier-table change and no diagnosis. Tree 3392df3d; runtime unchanged
(CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433; staged `libcodebrix_wgpu.so` arm64 `8113cbfa...`
hash-verified before every corpus). The runner is the merge-wave-3 tool set retargeted to this device
(`native/work/q/tools`, remote prefix `codebrix-q-`). Image and sampler gates were judged against the glibc reference
computed on Linux x64 from the S20's own dumped scene. Their replays were judged against the same reference. The
S20's bionic CPU result was recorded as the diagnostic column. The state, SVM and world gates compare against the
S20's own CPU. `principled_render` and `nodes_render` used the Linux-prepared export. All of this matches the
tablet runs.

**Device identity.**
- Device: SM-G781U1, serial RFCRB0MPHYH, Android 13 / API 33, board `kona` (Snapdragon 865), arm64-v8a,
  5.7 GB RAM.
- Build: `samsung/r8quex/r8q:13/TP1A.220624.014/G781U1UESNGYI1`.
- WebGPU adapter:
  - device `Adreno (TM) 650`, vendor `Qualcomm Technologies Inc. Adreno Vulkan Driver`, vendor id 20803,
    device id 100990978;
  - driver description `Driver Build: db7e7066a2, I1fad33e957, 1746950372 Date: 05/11/25 Compiler Version:
    EV031.32.02.10`.
- Tier: the adapter is not in the known-adapter table (`knownAdapter` null).
  - Every pipeline of every run used the full tier, reason `default` ("first run on this device: full fidelity"),
    with the unknown-mobile 20 s compile budget (`budgetSource` `unknown-mobile`).
  - Slow compiles only warned. No run changed tier or fell back to conservative.
- Init canary (opt-in, `glass_render`): passed in 1,586 ms.
- Device record: written in the documented format (status `valid`; 9 full-tier stages; no failure or breadcrumb).
  The persisted copy is under `native/work/q/runs/s20/canary-q/output/canary-glass_render/record/`.

**Sweep table.** Walls are the gate run itself, excluding the scene-dump and reference steps. Tablet walls and
results are the SM-X700's latest runs (merge waves 1-3). The host-gate and app rows count the whole tool. The
Evidence column is relative to `native/work/q/`. No job reached its timeout, which was max(600 s, 3 x the tablet
wall).

| Corpus | Job | S20 wall s | Tablet wall s | Tablet | S20 | Class | practical / strict / max (vs glibc unless "local") | Compile > 20 s | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| canary-q | canary-glass_render | 97.9 | 79.9 | PASS (glass_render, no canary run) | PASS | - | local 33 / 9.54e-07 | - | runs/s20/canary-q |
| emitter-q | emitter_sample | 22.4 | 11.3 | PASS | PASS | - | 0 / 125 / 9.06e-06 | - | runs/s20/emitter-q |
| hostcap-linux-terminator-q2 | term replays (10) | 42-368 | 17-73 | PASS | PASS 10/10 | - | readbacks 428/418/418/428/455/446/446/455/407/443; strict 0/0/0/0/27/19/19/27/8/21; max 6.2e-05 | - | runs/s20/hostcap-linux-terminator-q2 |
| hostcap-linux-triangle-q2 | tri replays (6) | 44-367 | 22-88 | PASS | PASS 6/6 | - | readbacks 289/417/415/473/415/464; strict 12/0/12/233/5/237; max 1.79e-06 | - | runs/s20/hostcap-linux-triangle-q2 |
| lighting-fc | feature_combination | 91.3 | 42.8 | PASS | PASS | - | 0 / 1179 / 6.02e-06 | init_camera 22 | runs/s20/lighting-fc |
| lighting-fc | feature_combination replays (2) | 72-72 | 28-28 | PASS | PASS 2/2 | - | readbacks 408/461; strict 12/9; max 2.41e-06 | - | runs/s20/lighting-fc |
| lighting-q | lighting_mis | 87.4 | 33.4 | PASS | PASS | - | 0 / 1057 / 6.08e-06 | shade_surface 27 | runs/s20/lighting-q |
| lighting-q | broad_lighting | 174.9 | 68.3 | PASS | PASS | - | 0 / 951 / 3.81e-05 | shade_surface 27, shade_surface 27 | runs/s20/lighting-q |
| lighting-q | lighting_mis replays (2) | 46-49 | - | not run on tablet | PASS 2/2 | - | readbacks 414/450; strict 0/0; max 1.19e-07 | - | runs/s20/lighting-q |
| lighting-q | broad_lighting replays (2) | 45-46 | 20-20 | PASS | PASS 2/2 | - | readbacks 414/450; strict 0/13; max 4.17e-07 | - | runs/s20/lighting-q |
| materials-nr | nodes_render | 14.0 | 153.2 | FAIL pixel 6720 (item 9) | FAIL | device lost | - | - | runs/s20/materials-nr |
| materials-nr | nodes-replay | - | - | not run (source failed) | FAIL | not run: source job failed | - | - | runs/s20/materials-nr |
| materials-nsvm | nodes_svm | 19.2 | 879.2 | FAIL typed 2D noise (item 9) | FAIL | device lost | - | - | runs/s20/materials-nsvm |
| materials-nw | nodes_world | 6.1 | 509.8 | PASS | FAIL | numerical practical (GPU kernel reported invalid data) | - | - | runs/s20/materials-nw |
| materials-nwiso | nodes_world | 6.1 | 509.8 | PASS | FAIL | numerical practical (GPU kernel reported invalid data) | - | - | runs/s20/materials-nwiso |
| materials-pr | principled_svm | 152.0 | 71.2 | FAIL device lost (item 8) | FAIL | numerical practical | - | - | runs/s20/materials-pr |
| materials-pr | principled_bounce | 251.6 | 26.3 | FAIL device lost (item 8) | FAIL | numerical practical (GPU kernel reported invalid data) | - | prepare_surface 152, shade_surface 68 | runs/s20/materials-pr |
| materials-pr | principled_render | 255.9 | 28.7 | FAIL device lost (item 8) | FAIL | numerical practical (GPU kernel reported invalid data) | - | prepare_surface 151, shade_surface 68 | runs/s20/materials-pr |
| materials-pr | principled-replay | - | - | not run (source failed) | FAIL | not run: source job failed | - | - | runs/s20/materials-pr |
| motion-q | object-moving-images | 194.1 | 84.9 | PASS | FAIL | numerical practical (GPU kernel reported invalid data) | - | intersect_closest 74, prepare_shader_data 43 | runs/s20/motion-q |
| motion-q | deforming-moving-images | 84.0 | 110.8 | PASS | FAIL | numerical practical (GPU kernel reported invalid data) | - | prepare_surface 43 | runs/s20/motion-q |
| motion-q | camera-static | 9.1 | 7.9 | PASS | PASS | - | local 0 / 0 | - | runs/s20/motion-q |
| motion-q | camera-motion | 29.3 | 14.2 | PASS | PASS | - | local 2818 / 0 | init_camera 20 | runs/s20/motion-q |
| motion-q | object replays (1) | - | 67-67 | PASS | not run 1/1 (source failed) | not run: source job failed | - | - | runs/s20/motion-q |
| motion-q | deforming replays (1) | - | 30-30 | PASS | not run 1/1 (source failed) | not run: source job failed | - | - | runs/s20/motion-q |
| precision-q | closest | 33.1 | 21.4 | PASS | PASS | - | local 0 / 0 | - | runs/s20/precision-q |
| precision-q | shadow | 12.8 | 16.2 | PASS | PASS | - | local 0 / 5.96e-08 | - | runs/s20/precision-q |
| precision-q | shadow replays (2) | 2-2 | 2-2 | PASS | PASS 2/2 | - | readbacks 4/4; strict 0/0; max 5.96e-08 | - | runs/s20/precision-q |
| regression-q | glass_render | 95.7 | 79.9 | PASS | PASS | - | 0 / 100 / 1.91e-06 | - | runs/s20/regression-q |
| regression-q | lighting_mis | 63.4 | 33.4 | PASS | PASS | - | 0 / 1057 / 6.08e-06 | - | runs/s20/regression-q |
| regression-q | transparent_lighting | 46.9 | 106.9 | PASS | FAIL | device lost | - | - | runs/s20/regression-q |
| spec-refusal-q | glass_render-spec | 101.0 | 76.8 | PASS | PASS | - | local 33 / 9.54e-07 | - | runs/s20/spec-refusal-q |
| spec-refusal-q | glass_render-nospec-refused | 0.3 | 0.3 | PASS (refused) | PASS | - | - | - | runs/s20/spec-refusal-q |
| terminator-q | shadow_terminator | 421.2 | 147.4 | PASS | PASS | - | 0 / 120 / 6.2e-05 | intersect_closest 75, intersect_shadow 56 | runs/s20/terminator-q |
| terminator-q | terminator replays (10) | 28-376 | 15-68 | PASS | PASS 10/10 | - | readbacks 428/418/418/428/455/446/446/455/407/443; strict 0/0/0/0/27/19/19/27/8/20; max 6.2e-05 | - | runs/s20/terminator-q |
| transparent-iso | transparent_shadow | 10.2 | 16.1 | PASS | FAIL | numerical practical (GPU kernel reported invalid data) | - | - | runs/s20/transparent-iso |
| transparent-iso | transparent-shadow replays (2) | - | 4-4 | PASS | not run 2/2 (source failed) | not run: source job failed | - | - | runs/s20/transparent-iso |
| transparent-q | transparent_shadow | 10.4 | 16.1 | PASS | FAIL | numerical practical (GPU kernel reported invalid data) | - | - | runs/s20/transparent-q |
| transparent-q | transparent_lighting | 46.8 | 106.9 | PASS | FAIL | device lost | - | - | runs/s20/transparent-q |
| transparent-q | transparent_lighting replays (8) | - | 24-32 | PASS | not run 8/8 (source failed) | not run: source job failed | - | - | runs/s20/transparent-q |
| transparent-q | transparent-shadow replays (2) | - | 4-4 | PASS | not run 2/2 (source failed) | not run: source job failed | - | - | runs/s20/transparent-q |
| triangle-q | triangle_sample | 80.1 | 25.8 | FAIL 2 practical (sample 3133) | PASS | - | 0 / 865 / 0.000488 | - | runs/s20/triangle-q |
| triangle-q | surface | 141.3 | 97.4 | PASS | PASS | - | local 1244 / 3.81e-06 | - | runs/s20/triangle-q |
| triangle-q | triangle_lighting | 472.9 | 159.0 | PASS | PASS | - | 0 / 490 / 1.79e-06 | shade_surface_light 107, intersect_closest 75 | runs/s20/triangle-q |
| triangle-q | triangle_lighting replays (6) | 43-356 | 21-87 | PASS | PASS 6/6 | - | readbacks 289/417/415/473/415/464; strict 12/0/6/233/8/231; max 1.79e-06 | - | runs/s20/triangle-q |
| volume-ci | closed-inside | 281.5 | 302.9 | PASS | PASS | - | local 1788 / 1.53e-05 | shade_volume 21 | runs/s20/volume-ci |
| volume-ci | closed replays (1) | 72-72 | 34-34 | PASS | PASS 1/1 | - | readbacks 1116; strict 1788; max 1.53e-05 | - | runs/s20/volume-ci |
| volume-co | closed-overlap | 293.9 | 319.9 | PASS | PASS | - | local 857 / 3.05e-05 | shade_volume 20 | runs/s20/volume-co |
| volume-co | closed replays (1) | 71-71 | 34-34 | PASS | PASS 1/1 | - | readbacks 1192; strict 857; max 3.05e-05 | - | runs/s20/volume-co |
| volume-cs | closed-single | 196.5 | 214.4 | PASS | PASS | - | local 608 / 3.05e-05 | - | runs/s20/volume-cs |
| volume-cs | closed replays (1) | 40-40 | 22-22 | PASS | PASS 1/1 | - | readbacks 838; strict 608; max 3.05e-05 | - | runs/s20/volume-cs |
| volume-q | volume-state | 19.4 | 13.2 | PASS | PASS | - | local 8551 / 0.00013 | - | runs/s20/volume-q |
| volume-q | volume-absorption | 124.4 | 120.8 | PASS | PASS | - | local 552 / 2.29e-05 | - | runs/s20/volume-q |
| volume-q | volume-isotropic | 179.0 | 172.0 | PASS | PASS | - | local 1439 / 2.29e-05 | - | runs/s20/volume-q |
| volume-q | volume-forward | 220.6 | 170.8 | PASS | PASS | - | local 1472 / 3.62e-05 | - | runs/s20/volume-q |
| volume-q | volume-backward | 219.1 | 171.7 | PASS | PASS | - | local 1382 / 2.29e-05 | - | runs/s20/volume-q |
| volume-q | boundary-state | 83.2 | 32.4 | PASS | PASS | - | local 6492 / 0.000244 | shade_surface 24 | runs/s20/volume-q |
| volume-q | boundary-compaction-guard | 21.6 | 6.4 | PASS | PASS | - | local 0 / 0 | - | runs/s20/volume-q |
| volume-q | volume replays (4) | 55-56 | 20-21 | PASS | PASS 4/4 | - | readbacks 464/672/664/664; strict 552/1439/1472/1382; max 3.62e-05 | - | runs/s20/volume-q |
| host-gate | android-host-gate | 136 | 62.0 | PASS 48 exports (wall from file times) | PASS | - | 48 exports, 1579 films, 0 practical, 0 required strict, max 1.43e-05 (cpu/cpu-exit/reload/stress/gpu/gpu-exit all rc 0) | - | device/codebrix-q-host-gate (log device/android-host-gate.log) |
| app | app-webgpu | 379 | 173.0 | PASS 0 practical | PASS | - | 3 scenes vs Linux glibc films: 0 practical, 921 strict, max 1.38e-05; tier full; cancellation ok=True; capability warning: pipeline 'shade_surface_light' took 53882 ms, over the 20000 ms compile budget (full tier); first-use latency is degraded on this driver | shade_surface_light 54 | android-app/webgpu-s20 (log device/app-s20.log) |

The merge-wave-3 Linux captures could not be replayed as they were. merge3's scratch cleanup had deleted their
`run.arena.bin` inputs, so the first hostcap attempt stopped at the first push. That was a harness input problem, not
a device result. The captures were re-recorded on Linux in this worktree (`triangle_lighting_gate.py` and
`shadow_terminator_gate.py`, both pass) and replayed as `hostcap-*-q2`. `transparent_shadow` and `nodes_world` each ran
directly after a device-lost process, so each was repeated once in isolation (`transparent-iso`, `materials-nwiso`;
fresh stage, 60 s idle). Both reproduce.

**Differs from the tablet (SM-X700, Adreno 730).**
- These pass on the tablet and fail on the S20:
  - `transparent_lighting` (regression and transparent corpora): the device is lost in mode 0
    (`wgpuQueueSubmit: Parent device is lost`, exit 134). Its 8 replays did not run.
  - `transparent_shadow`: the kernel reports invalid data, mask 1. Its 2 replays did not run.
  - The object- and deforming-motion images: SVM error 8 (`invalid/uninitialized SVM stack`). Their replays did not
    run. Camera static and motion pass.
  - `nodes_world`: SVM error 8 at world 0.
- `triangle_sample` passes on the S20 (0 practical, 865 strict) and fails on the tablet (open item 1). The S20's own
  bionic CPU differs from glibc there by 72 practical values.
- These fail on both devices, in different ways:
  - Principled SVM, bounce and render: SVM error 8 on the S20; on the tablet the device is lost (open item 8).
  - `nodes_svm`: the S20 loses the device creating `surface_svm_probe`; on the tablet the typed 2D noise program
    fails (item 9).
  - `nodes_render`: the S20 loses the device creating `prepare_surface`; on the tablet image component 6720 fails
    (item 9).
- Speed: the volume renders take 0.9-1.3 x the tablet's wall. Triangle, lighting, terminator, boundary-state and
  replay jobs take 2-6 x.
- Compile times: compiles over the 20 s unknown-mobile budget (warnings only) reached:
  - Principled `prepare_surface` 152 s;
  - emissive-triangle `shade_surface_light` 107 s;
  - motion and terminator `intersect_closest` 75 s;
  - `shade_surface_light` 54 s in the managed app (reported as its capability warning).
- The SVM error 8 failures (motion images, node world, Principled) have the same signature as the Adreno 640 items 2
  and 10. This is an observation, not a diagnosis.

The S20 was left with no `codebrix-q-*` folders, no lane Q app and no codebrix process. Folders and packages that were
on the device before this sweep (`codebrix-ncdbg`, four `com.codebrix.*` test apps) were not touched.
