
### S20 rerun on the merged tree (merge wave 4, 2026-10-22)

Lane Q's failing jobs, rerun once each on the merge-wave-4 tree (lanes L, S, P), one job per run, timeout max(600 s,
3 x the merge-wave-4 Samsung wall), same reference flow as the tablet (device-scene glibc). Adapter "Adreno (TM) 650",
unknown to the tier table (full tier by default, 20 s unknown-mobile compile budget); the table was not changed.
Record only; nothing was fixed. Evidence: `native/work/merge4/runs/s20/`.

| Job | Timeout s | S20 wall s | Result (first error line) |
| --- | --- | --- | --- |
| transparent_lighting | 600 | 47.5 | FAIL: `Parent device is lost` (8 replays not run) |
| transparent_shadow | 600 | 11.0 | FAIL: invalid data (mask 1): invalid state/data |
| object-moving-images | 600 | 110.9 | FAIL: lighting stage, mask 8 (invalid/uninitialized SVM stack) |
| deforming-moving-images | 600 | 82.7 | FAIL: lighting stage, mask 8 (invalid/uninitialized SVM stack) |
| nodes_world | 1452 | 6.2 | FAIL: compiled world 0, mask 8 (invalid/uninitialized SVM stack) |
| principled_svm | 600 | 401.1 | PASS |
| principled_bounce | 915 | 915.3 | TIMEOUT at the cutoff |
| principled_render | 600 | 233.1 | FAIL: stage 40 `path.mis_ray_pdf` 1751.469 against 1747.274 (as the Samsung) |
| nodes_svm | 600 | - | FAIL: the scene-dump pass lost the device (`wgpuDeviceCreateComputePipeline: Parent device is lost`) |
| nodes_render | 600 | 17.2 | FAIL: `wgpuDeviceCreateComputePipeline: Parent device is lost` |

principled_svm now passes on the S20 (it failed in lane Q's sweep on 3392df3d; the merged tree adds lane S's arena
stack). The SVM-stack (mask 8) failures of motion and node worlds remain on the merged tree.
