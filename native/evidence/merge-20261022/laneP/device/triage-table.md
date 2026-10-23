| Corpus | Job | Samsung wall (s) | Pixel wall (s) | Cutoff (s) | Pixel result | Verdict | Max pipeline ms | Log |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| triangle | triangle_sample | 25.8 (Samsung FAIL) | 77.3 | 77 | FAIL (3x cutoff at 77 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/triangle-p-triangle_sample-1792695459 |
| triangle | surface | 97.4 | 199.7 | 292 | PASS | SUPPORTED | 10057.9 | native/work/p/runs/pixel/triangle-p-surface-1792695459 |
| triangle | triangle_lighting | 159.0 | 477.2 | 477 | FAIL (3x cutoff at 477 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/triangle-p-triangle_lighting-1792695459 |
| triangle | replay-0 | 21.4 | None | 64 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/triangle-p-triangle_lighting-1792695459 |
| triangle | replay-1 | 39.1 | None | 117 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/triangle-p-triangle_lighting-1792695459 |
| triangle | replay-2 | 39.2 | None | 118 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/triangle-p-triangle_lighting-1792695459 |
| triangle | replay-3 | 39.4 | None | 118 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/triangle-p-triangle_lighting-1792695459 |
| triangle | replay-4 | 39.0 | None | 117 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/triangle-p-triangle_lighting-1792695459 |
| triangle | replay-5 | 87.0 | None | 261 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/triangle-p-triangle_lighting-1792695459 |
| emitter | emitter_sample | 11.3 | 21.4 | 34 | PASS | SUPPORTED | None | native/work/p/runs/pixel/emitter-p-emitter_sample-1792695459 |
| hostcap-linux-triangle | tri0 | 21.7 | 61.4 | 65 | PASS | SUPPORTED | None | native/work/p/runs/pixel/hostcap-linux-triangle-1792701173 |
| hostcap-linux-triangle | tri1 | 40.0 | 120.0 | 120 | FAIL (3x cutoff at 120 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/hostcap-linux-triangle-1792701173 |
| hostcap-linux-triangle | tri2 | 40.0 | 120.1 | 120 | FAIL (3x cutoff at 120 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/hostcap-linux-triangle-1792701173 |
| hostcap-linux-triangle | tri3 | 40.0 | 120.0 | 120 | FAIL (3x cutoff at 120 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/hostcap-linux-triangle-1792701173 |
| hostcap-linux-triangle | tri4 | 39.1 | 117.1 | 117 | FAIL (3x cutoff at 117 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/hostcap-linux-triangle-1792701173 |
| hostcap-linux-triangle | tri5 | 87.9 | 264.1 | 264 | FAIL (3x cutoff at 264 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/hostcap-linux-triangle-1792701173 |
| hostcap-linux-terminator | term0 | 17.2 | 39.2 | 52 | PASS | SUPPORTED | None | native/work/p/runs/pixel/hostcap-linux-terminator-1792701173 |
| hostcap-linux-terminator | term1 | 19.6 | 51.5 | 59 | PASS | SUPPORTED | None | native/work/p/runs/pixel/hostcap-linux-terminator-1792701173 |
| hostcap-linux-terminator | term2 | 19.5 | 51.2 | 58 | PASS | SUPPORTED | None | native/work/p/runs/pixel/hostcap-linux-terminator-1792701173 |
| hostcap-linux-terminator | term3 | 19.4 | 50.6 | 58 | PASS | SUPPORTED | None | native/work/p/runs/pixel/hostcap-linux-terminator-1792701173 |
| hostcap-linux-terminator | term4 | 33.7 | 101.1 | 101 | FAIL (3x cutoff at 101 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/hostcap-linux-terminator-1792701173 |
| hostcap-linux-terminator | term5 | 33.7 | 101.1 | 101 | FAIL (3x cutoff at 101 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/hostcap-linux-terminator-1792701173 |
| hostcap-linux-terminator | term6 | 33.5 | 100.1 | 100 | FAIL (3x cutoff at 100 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/hostcap-linux-terminator-1792701173 |
| hostcap-linux-terminator | term7 | 33.7 | 101.1 | 101 | FAIL (3x cutoff at 101 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/hostcap-linux-terminator-1792701173 |
| hostcap-linux-terminator | term8 | 33.7 | 101.1 | 101 | FAIL (3x cutoff at 101 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/hostcap-linux-terminator-1792701173 |
| hostcap-linux-terminator | term9 | 72.8 | 218.0 | 218 | FAIL (3x cutoff at 218 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/hostcap-linux-terminator-1792701173 |
| materials | nodes_svm | 879.2 (Samsung FAIL) | 158.3 | 2638 | FAIL: Parent device is lost | PROVISIONAL (NOT SUPPORTED (fails)) | None | native/work/p/runs/pixel/materials-p-nodes_svm-1792693942 |
| materials | nodes_world | 509.8 | 6.8 | 1529 | FAIL: 00:06.58 / ERROR: WebGPU scheduler kernel reported invalid data (mask 8): invalid/uninitialized SVM stack | NOT SUPPORTED (fails) | 3092.33 | native/work/p/runs/pixel/materials-p-nodes_world-1792693942 |
| materials | nodes_render | 153.2 (Samsung FAIL) | 143.1 | 460 | FAIL: 02:22.76 / ERROR: WebGPU scheduler kernel reported invalid data (mask 24): invalid/uninitialized SVM stack unsupported SVM parameter / Render iteration failed: WebGPU scheduler kernel reported invalid data (mask 24): inv | PROVISIONAL (NOT SUPPORTED (fails)) | 119672 | native/work/p/runs/pixel/materials-p-nodes_render-1792693942 |
| materials | nodes-replay | None (Samsung FAIL) | None | None | not run (no Samsung baseline, deferred to merge) | NO BASELINE | None | - |
| materials | principled_svm | 71.2 (Samsung FAIL) | 115.0 | 214 | FAIL: Native compiled graphs SVM error mask=8 first incomplete path=119 shader=12 | PROVISIONAL (NOT SUPPORTED (fails)) | None | native/work/p/runs/pixel/materials-p-principled_svm-1792693942 |
| materials | principled_bounce | 26.3 (Samsung FAIL) | 79.2 | 79 | FAIL (3x cutoff at 79 s) | PROVISIONAL (NOT SUPPORTED (3x)) | None | native/work/p/runs/pixel/materials-p-principled_bounce-1792693942 |
| materials | principled_render | 28.7 (Samsung FAIL) | 86.2 | 86 | FAIL (3x cutoff at 86 s) | PROVISIONAL (NOT SUPPORTED (3x)) | None | native/work/p/runs/pixel/materials-p-principled_render-1792693942 |
| materials | principled-replay | None (Samsung FAIL) | None | None | not run (no Samsung baseline, deferred to merge) | NO BASELINE | None | - |
| lighting | broad_lighting | 68.3 | 203.7 | 205 | PASS | SUPPORTED | 32173.9 | native/work/p/runs/pixel/lighting-p-broad_lighting-1792695459 |
| lighting | broad_lighting-replay-0 | 20.3 | 61.1 | 61 | FAIL (3x cutoff at 61 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/lighting-p-broad_lighting-1792695459 |
| lighting | broad_lighting-replay-1 | 20.4 | 61.1 | 61 | FAIL (3x cutoff at 61 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/lighting-p-broad_lighting-1792695459 |
| lighting | feature_combination | 42.8 | 128.2 | 128 | FAIL (3x cutoff at 128 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/lighting-p-feature_combination-1792695459 |
| lighting | feature_combination-replay-0 | 27.6 | None | 83 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/lighting-p-feature_combination-1792695459 |
| lighting | feature_combination-replay-1 | 27.9 | None | 84 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/lighting-p-feature_combination-1792695459 |
| lighting | lighting_mis | 33.4 | 96.4 | 100 | PASS | SUPPORTED | 26921.1 | native/work/p/runs/pixel/lighting-p-lighting_mis-1792695459 |
| lighting | lighting_mis-replay-0 | 15.6 | 47.2 | 47 | FAIL (3x cutoff at 47 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/lighting-p-lighting_mis-1792695459 |
| lighting | lighting_mis-replay-1 | 16.0 | 48.2 | 48 | FAIL (3x cutoff at 48 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/lighting-p-lighting_mis-1792695459 |
| regression | glass_render | 79.9 | 121.4 | 240 | PASS | SUPPORTED | 24025.5 | native/work/p/runs/pixel/spec-refusal-p-spec-refusal-1792695459-glass_render-spec |
| regression | lighting_mis | 33.4 | 96.4 | 100 | PASS | SUPPORTED | 26921.1 | native/work/p/runs/pixel/lighting-p-lighting_mis-1792695459 |
| regression | transparent_lighting | 107.5 | 64.7 | 322 | FAIL: Parent device is lost | NOT SUPPORTED (fails) | 26860.6 | native/work/p/runs/pixel/transparent-p-transparent_lighting-1792695459 |
| transparent | transparent_shadow | 16.1 | 13.9 | 48 | FAIL: 00:13.62 / ERROR: WebGPU scheduler kernel reported invalid data (mask 1): invalid state/data / Transparent intersect failed: WebGPU scheduler kernel reported invalid data (mask 1): invalid state/data | NOT SUPPORTED (fails) | 6721.03 | native/work/p/runs/pixel/transparent-p-transparent_shadow-1792695459 |
| transparent | transparent_lighting | 106.9 | 64.7 | 321 | FAIL: Parent device is lost | NOT SUPPORTED (fails) | 26860.6 | native/work/p/runs/pixel/transparent-p-transparent_lighting-1792695459 |
| transparent | transparent_lighting-replay-0 | 23.7 | None | 71 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/transparent-p-transparent_lighting-1792695459 |
| transparent | transparent_lighting-replay-1 | 23.7 | None | 71 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/transparent-p-transparent_lighting-1792695459 |
| transparent | transparent_lighting-replay-2 | 30.5 | None | 92 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/transparent-p-transparent_lighting-1792695459 |
| transparent | transparent_lighting-replay-3 | 30.9 | None | 93 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/transparent-p-transparent_lighting-1792695459 |
| transparent | transparent_lighting-replay-4 | 31.4 | None | 94 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/transparent-p-transparent_lighting-1792695459 |
| transparent | transparent_lighting-replay-5 | 31.9 | None | 96 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/transparent-p-transparent_lighting-1792695459 |
| transparent | transparent_lighting-replay-6 | 30.2 | None | 91 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/transparent-p-transparent_lighting-1792695459 |
| transparent | transparent_lighting-replay-7 | 30.8 | None | 92 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/transparent-p-transparent_lighting-1792695459 |
| transparent | transparent-shadow-replay-direct | 3.9 | None | 12 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/transparent-p-transparent_shadow-1792695459 |
| transparent | transparent-shadow-replay-indexed | 4.0 | None | 12 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/transparent-p-transparent_shadow-1792695459 |
| terminator | shadow_terminator | 147.4 | 442.3 | 442 | FAIL (3x cutoff at 442 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/terminator-p-shadow_terminator-1792695459 |
| terminator | terminator-replay-0 | 15.3 | None | 46 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/terminator-p-shadow_terminator-1792695459 |
| terminator | terminator-replay-1 | 17.5 | None | 52 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/terminator-p-shadow_terminator-1792695459 |
| terminator | terminator-replay-2 | 17.6 | None | 53 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/terminator-p-shadow_terminator-1792695459 |
| terminator | terminator-replay-3 | 17.6 | None | 53 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/terminator-p-shadow_terminator-1792695459 |
| terminator | terminator-replay-4 | 29.8 | None | 89 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/terminator-p-shadow_terminator-1792695459 |
| terminator | terminator-replay-5 | 29.8 | None | 89 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/terminator-p-shadow_terminator-1792695459 |
| terminator | terminator-replay-6 | 29.8 | None | 89 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/terminator-p-shadow_terminator-1792695459 |
| terminator | terminator-replay-7 | 29.6 | None | 89 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/terminator-p-shadow_terminator-1792695459 |
| terminator | terminator-replay-8 | 29.9 | None | 90 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/terminator-p-shadow_terminator-1792695459 |
| terminator | terminator-replay-9 | 67.7 | None | 203 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/terminator-p-shadow_terminator-1792695459 |
| spec-refusal | glass_render-spec | 76.8 | 121.4 | 230 | PASS | SUPPORTED | 24025.5 | native/work/p/runs/pixel/spec-refusal-p-spec-refusal-1792695459-glass_render-spec |
| spec-refusal | glass_render-nospec-refused | 0.3 | 0.9 | 5 | PASS | SUPPORTED | None | native/work/p/runs/pixel/spec-refusal-p-spec-refusal-1792695459-glass_render-nospec-refused |
| motion | object-moving-images | 84.9 | 252.9 | 255 | FAIL: 04:12.60 / ERROR: WebGPU scheduler kernel reported invalid data (mask 8): invalid/uninitialized SVM stack / Lighting stage failed: WebGPU scheduler kernel reported invalid data (mask 8): invalid/uninitialized SVM stack | NOT SUPPORTED (fails) | 85006.9 | native/work/p/runs/pixel/motion-p-item2-chain-1792693942 |
| motion | object-replay-motion-0 | 66.8 | None | 200 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/motion-p-item2-chain-1792693942 |
| motion | object-stationary-images | 36.2 | 109.2 | 109 | FAIL (3x cutoff at 109 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/motion-p-object-stationary-images-1792695459 |
| motion | deforming-moving-images | 110.8 | 116.0 | 332 | FAIL: 01:55.72 / ERROR: WebGPU scheduler kernel reported invalid data (mask 8): invalid/uninitialized SVM stack / Lighting stage failed: WebGPU scheduler kernel reported invalid data (mask 8): invalid/uninitialized SVM stack | NOT SUPPORTED (fails) | 54190.9 | native/work/p/runs/pixel/motion-p-deforming-moving-images-1792695459 |
| motion | deforming-replay-motion-0 | 29.6 | None | 89 | not run (source job failed) | NOT RUN (source job not supported) | None | native/work/p/runs/pixel/motion-p-deforming-moving-images-1792695459 |
| motion | deforming-stationary-images | 36.3 | 109.2 | 109 | FAIL (3x cutoff at 109 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/motion-p-deforming-stationary-images-1792695459 |
| motion | camera-static | 7.9 | 10.0 | 24 | PASS | SUPPORTED | 754.782 | native/work/p/runs/pixel/motion-p-camera-static-1792695459 |
| motion | camera-motion | 14.2 | 37.3 | 43 | PASS | SUPPORTED | 26996.6 | native/work/p/runs/pixel/motion-p-camera-motion-1792695459 |
| precision | closest | 21.4 | 40.4 | 64 | PASS | SUPPORTED | 4041.28 | native/work/p/runs/pixel/precision-p-closest-1792695459 |
| precision | shadow | 16.2 | 16.7 | 49 | PASS | SUPPORTED | 1900.82 | native/work/p/runs/pixel/precision-p-shadow-1792695459 |
| precision | shadow-replay-direct | 1.5 | 2.0 | 5 | PASS | SUPPORTED | None | native/work/p/runs/pixel/precision-p-shadow-1792695459 |
| precision | shadow-replay-indexed | 1.5 | 2.0 | 5 | PASS | SUPPORTED | None | native/work/p/runs/pixel/precision-p-shadow-1792695459 |
| volume | volume-state | 13.2 | 24.9 | 40 | PASS | SUPPORTED | 15539.9 | native/work/p/runs/pixel/volume-p-volume-state-1792698799 |
| volume | volume-absorption | 120.8 | 140.1 | 362 | PASS | SUPPORTED | 15770.2 | native/work/p/runs/pixel/volume-p-volume-absorption-1792698799 |
| volume | volume-isotropic | 172.0 | 179.2 | 516 | PASS | SUPPORTED | 15950.6 | native/work/p/runs/pixel/volume-p-volume-isotropic-1792698799 |
| volume | volume-forward | 170.8 | 180.4 | 512 | PASS | SUPPORTED | 15754.5 | native/work/p/runs/pixel/volume-p-volume-forward-1792698799 |
| volume | volume-backward | 171.7 | 182.8 | 515 | PASS | SUPPORTED | 16468.7 | native/work/p/runs/pixel/volume-p-volume-backward-1792698799 |
| volume | boundary-state | 32.4 | 81.0 | 97 | PASS | SUPPORTED | 23886.5 | native/work/p/runs/pixel/volume-p-boundary-state-1792698799 |
| volume | boundary-compaction-guard | 6.4 | 19.2 | 19 | FAIL (3x cutoff at 19 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/volume-p-boundary-compaction-guard-1792698799 |
| volume | closed-single | 214.4 | 219.4 | 643 | PASS | SUPPORTED | 16388.3 | native/work/p/runs/pixel/volume-p-closed-single-1792698799 |
| volume | closed-overlap | 319.9 | 345.9 | 960 | PASS | SUPPORTED | 30007.2 | native/work/p/runs/pixel/volume-p-closed-overlap-1792698799 |
| volume | closed-inside | 302.9 | 337.5 | 909 | PASS | SUPPORTED | 29359.2 | native/work/p/runs/pixel/volume-p-closed-inside-1792698799 |
| volume | volume-replay-absorption | 19.5 | 50.1 | 58 | PASS | SUPPORTED | None | native/work/p/runs/pixel/volume-p-volume-absorption-1792698799 |
| volume | volume-replay-isotropic | 20.6 | 50.5 | 62 | PASS | SUPPORTED | None | native/work/p/runs/pixel/volume-p-volume-isotropic-1792698799 |
| volume | volume-replay-forward | 20.5 | 50.3 | 62 | PASS | SUPPORTED | None | native/work/p/runs/pixel/volume-p-volume-forward-1792698799 |
| volume | volume-replay-backward | 20.7 | 54.8 | 62 | PASS | SUPPORTED | None | native/work/p/runs/pixel/volume-p-volume-backward-1792698799 |
| volume | closed-replay-single | 21.5 | 54.1 | 64 | PASS | SUPPORTED | None | native/work/p/runs/pixel/volume-p-closed-single-1792698799 |
| volume | closed-replay-overlap | 33.9 | 102.1 | 102 | FAIL (3x cutoff at 102 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/volume-p-closed-overlap-1792698799 |
| volume | closed-replay-inside | 34.1 | 102.2 | 102 | FAIL (3x cutoff at 102 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/volume-p-closed-inside-1792698799 |
| app | managed-app-3-scenes | 134.4 | >403 (result timeout) | 403 | FAIL (3x cutoff at 403 s) | NOT SUPPORTED (3x) | None | native/work/p/android-app/webgpu-pixel-1792695459 |
| motion (item 2) | object-moving-images, device oracle, no reference import | 84.9 | 255.3 | 255 | FAIL (3x cutoff at 255 s) | NOT SUPPORTED (3x) | None | native/work/p/runs/pixel/motion-p-item2-device-oracle-1792693942 |
| motion (item 2) | object-moving-images, CODEBRIX_CYCLES_REFERENCE_ORACLE=linux-x64-sse4.2, no reference import | 84.9 | 251.8 | 255 | FAIL: 04:11.48 / ERROR: WebGPU scheduler kernel reported invalid data (mask 8): invalid/uninitialized SVM stack / Lighting stage failed: WebGPU scheduler kernel reported invalid data (mask 8): invalid/uninitialized SVM stack | NOT SUPPORTED (fails) | 85383.7 | native/work/p/runs/pixel/motion-p-item2-x64-oracle-1792693942 |
