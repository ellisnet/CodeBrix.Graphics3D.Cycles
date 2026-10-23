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
