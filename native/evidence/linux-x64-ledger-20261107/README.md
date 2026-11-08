# Linux x64 ledger checkpoints, device checkpoint 1 (2026-11-07)

Copyright (c) 2026 Jeremy Ellis and contributors

The compatibility ledger's evidence (`compatibility/evidence.json`) re-recorded on the device checkpoint 1 baseline pass:
commit `c3783adfa91e0f8310f3cc766e0a55f492e21ee0` plus fix 001 (`native/shaders/wavefront_hair_huang.wgsl`), Linux x64,
Intel UHD Graphics (ADL-S GT1), Mesa 25.0.7-2+deb13u1, CodeBrix.WebGpu.Native.MitLicenseForever 1.0.300.210. It supersedes
`native/evidence/linux-x64-ledger-20261101/`, which stays as history. A later re-record writes a new folder; never edit this one.

One folder per checkpoint: `validation.json` (the report the ledger pins by SHA-256 and asserts on) and copies of the gate
reports it cites. 24 checkpoints, every one `passed: true`:

| Checkpoints | Source of the gate reports |
|---|---|
| native-render, device-queue, camera-queue, closest-queue, background-queue, svm-world, svm-surface | the Linux queue's render gate and `*_gate.py run` jobs (the last report of each log) |
| native-nodes, light-tree, shadow-catcher, shadow-linking (new), mnee | the Linux queue's `nodes_gate.py`, `light_tree_gate.py`, `shadow_catcher_gate.py`, `shadow_linking_gate.py`, `mnee_gate.py` (`status.json`, `results.json`) |
| native-attributes, native-images, native-normals, native-file-images | the gate drivers' Linux halves in the same pass, before fix 001 (fix 001 changes none of these checkpoints' sources); each keeps the driver's own `validation.json` beside it |
| ambient-occlusion, environment-light | `codebrix_cycles_ao_state_gate`, `codebrix_cycles_ao_render_gate`, `codebrix_cycles_environment_gate` on the same build after the queue; the two AO oracle scenes' judge reports from the queue |
| replay-principled-linux, replay-nodes-linux | replays of the queue's `principled_render` and `nodes_render` captures by the pass's Linux replay build |
| replay-principled-hp, replay-nodes-hp, replay-triangle-samsung, replay-terminator-samsung | the Android captures and replays of the 2026-10-29 run (tree 686f79282), carried unchanged in content |

Assertions: every earlier pointer is kept where its value holds at this tree. Changed: the MNEE image case count 9 -> 18
(the gate's own expectation since the 18-case MNEE gate); dropped: the shadow catcher's `volumeRefused` (the catcher with a
volume now renders, and its gate asserts the image). Every source pin is the worktree's current file.

`SHA256SUMS.txt` lists every file (`sha256sum -c SHA256SUMS.txt`). Absolute host paths inside copied reports are written as
`<run>`, `<repo>`, `<home>`.

Source pins re-taken at session 3's early close (2026-11-08), on `1c666d599` plus fixes 001, 007 and 009, with no new runs:
`native/cycles/svm_surface_gate.cpp` (fix 007; 12 checkpoints) and `native/cycles/environment_gate.cpp` (fix 009; 1 checkpoint)
are gate-code changes for the Android arm64 dump pass (the Linux gates g01-g08 were rerun with both fixes in the Linux queue and
passed); `native/cycles/pipelines.cpp` (`1c666d599`; 7 checkpoints) adds only Intel-on-Metal workgroup workarounds, which do not
run on Linux. The reports above were produced before these changes; their content is unchanged.
