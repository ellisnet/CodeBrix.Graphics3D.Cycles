| Corpus | Job | S20 wall s | Tablet wall s | Tablet | S20 | Class | practical / strict / max (vs glibc unless "local") | Compile > 20 s | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| canary-q | canary-glass_render | 97.9 | 79.9 | PASS (glass_render, no canary run) | PASS | - | local 33 / 9.54e-07 | - | q/runs/s20/canary-q |
| emitter-q | emitter_sample | 22.4 | 11.3 | PASS | PASS | - | 0 / 125 / 9.06e-06 | - | q/runs/s20/emitter-q |
| hostcap-linux-terminator-q2 | term replays (10) | 42-368 | 17-73 | PASS | PASS 10/10 | - | readbacks 428/418/418/428/455/446/446/455/407/443; strict 0/0/0/0/27/19/19/27/8/21; max 6.2e-05 | - | q/runs/s20/hostcap-linux-terminator-q2 |
| hostcap-linux-triangle-q2 | tri replays (6) | 44-367 | 22-88 | PASS | PASS 6/6 | - | readbacks 289/417/415/473/415/464; strict 12/0/12/233/5/237; max 1.79e-06 | - | q/runs/s20/hostcap-linux-triangle-q2 |
| lighting-fc | feature_combination | 91.3 | 42.8 | PASS | PASS | - | 0 / 1179 / 6.02e-06 | init_camera 22 | q/runs/s20/lighting-fc |
| lighting-fc | feature_combination replays (2) | 72-72 | 28-28 | PASS | PASS 2/2 | - | readbacks 408/461; strict 12/9; max 2.41e-06 | - | q/runs/s20/lighting-fc |
| lighting-q | lighting_mis | 87.4 | 33.4 | PASS | PASS | - | 0 / 1057 / 6.08e-06 | shade_surface 27 | q/runs/s20/lighting-q |
| lighting-q | broad_lighting | 174.9 | 68.3 | PASS | PASS | - | 0 / 951 / 3.81e-05 | shade_surface 27, shade_surface 27 | q/runs/s20/lighting-q |
| lighting-q | lighting_mis replays (2) | 46-49 | - | not run on tablet | PASS 2/2 | - | readbacks 414/450; strict 0/0; max 1.19e-07 | - | q/runs/s20/lighting-q |
| lighting-q | broad_lighting replays (2) | 45-46 | 20-20 | PASS | PASS 2/2 | - | readbacks 414/450; strict 0/13; max 4.17e-07 | - | q/runs/s20/lighting-q |
| materials-nr | nodes_render | 14.0 | 153.2 | FAIL pixel 6720 (item 9) | FAIL | device lost | - | - | q/runs/s20/materials-nr |
| materials-nr | nodes-replay | - | - | not run (source failed) | FAIL | not run: source job failed | - | - | q/runs/s20/materials-nr |
| materials-nsvm | nodes_svm | 19.2 | 879.2 | FAIL typed 2D noise (item 9) | FAIL | device lost | - | - | q/runs/s20/materials-nsvm |
| materials-nw | nodes_world | 6.1 | 509.8 | PASS | FAIL | numerical practical (GPU kernel reported invalid data) | - | - | q/runs/s20/materials-nw |
| materials-nwiso | nodes_world | 6.1 | 509.8 | PASS | FAIL | numerical practical (GPU kernel reported invalid data) | - | - | q/runs/s20/materials-nwiso |
| materials-pr | principled_svm | 152.0 | 71.2 | FAIL device lost (item 8) | FAIL | numerical practical | - | - | q/runs/s20/materials-pr |
| materials-pr | principled_bounce | 251.6 | 26.3 | FAIL device lost (item 8) | FAIL | numerical practical (GPU kernel reported invalid data) | - | prepare_surface 152, shade_surface 68 | q/runs/s20/materials-pr |
| materials-pr | principled_render | 255.9 | 28.7 | FAIL device lost (item 8) | FAIL | numerical practical (GPU kernel reported invalid data) | - | prepare_surface 151, shade_surface 68 | q/runs/s20/materials-pr |
| materials-pr | principled-replay | - | - | not run (source failed) | FAIL | not run: source job failed | - | - | q/runs/s20/materials-pr |
| motion-q | object-moving-images | 194.1 | 84.9 | PASS | FAIL | numerical practical (GPU kernel reported invalid data) | - | intersect_closest 74, prepare_shader_data 43 | q/runs/s20/motion-q |
| motion-q | deforming-moving-images | 84.0 | 110.8 | PASS | FAIL | numerical practical (GPU kernel reported invalid data) | - | prepare_surface 43 | q/runs/s20/motion-q |
| motion-q | camera-static | 9.1 | 7.9 | PASS | PASS | - | local 0 / 0 | - | q/runs/s20/motion-q |
| motion-q | camera-motion | 29.3 | 14.2 | PASS | PASS | - | local 2818 / 0 | init_camera 20 | q/runs/s20/motion-q |
| motion-q | object replays (1) | - | 67-67 | PASS | not run 1/1 (source failed) | not run: source job failed | - | - | q/runs/s20/motion-q |
| motion-q | deforming replays (1) | - | 30-30 | PASS | not run 1/1 (source failed) | not run: source job failed | - | - | q/runs/s20/motion-q |
| precision-q | closest | 33.1 | 21.4 | PASS | PASS | - | local 0 / 0 | - | q/runs/s20/precision-q |
| precision-q | shadow | 12.8 | 16.2 | PASS | PASS | - | local 0 / 5.96e-08 | - | q/runs/s20/precision-q |
| precision-q | shadow replays (2) | 2-2 | 2-2 | PASS | PASS 2/2 | - | readbacks 4/4; strict 0/0; max 5.96e-08 | - | q/runs/s20/precision-q |
| regression-q | glass_render | 95.7 | 79.9 | PASS | PASS | - | 0 / 100 / 1.91e-06 | - | q/runs/s20/regression-q |
| regression-q | lighting_mis | 63.4 | 33.4 | PASS | PASS | - | 0 / 1057 / 6.08e-06 | - | q/runs/s20/regression-q |
| regression-q | transparent_lighting | 46.9 | 106.9 | PASS | FAIL | device lost | - | - | q/runs/s20/regression-q |
| spec-refusal-q | glass_render-spec | 101.0 | 76.8 | PASS | PASS | - | local 33 / 9.54e-07 | - | q/runs/s20/spec-refusal-q |
| spec-refusal-q | glass_render-nospec-refused | 0.3 | 0.3 | PASS (refused) | PASS | - | - | - | q/runs/s20/spec-refusal-q |
| terminator-q | shadow_terminator | 421.2 | 147.4 | PASS | PASS | - | 0 / 120 / 6.2e-05 | intersect_closest 75, intersect_shadow 56 | q/runs/s20/terminator-q |
| terminator-q | terminator replays (10) | 28-376 | 15-68 | PASS | PASS 10/10 | - | readbacks 428/418/418/428/455/446/446/455/407/443; strict 0/0/0/0/27/19/19/27/8/20; max 6.2e-05 | - | q/runs/s20/terminator-q |
| transparent-iso | transparent_shadow | 10.2 | 16.1 | PASS | FAIL | numerical practical (GPU kernel reported invalid data) | - | - | q/runs/s20/transparent-iso |
| transparent-iso | transparent-shadow replays (2) | - | 4-4 | PASS | not run 2/2 (source failed) | not run: source job failed | - | - | q/runs/s20/transparent-iso |
| transparent-q | transparent_shadow | 10.4 | 16.1 | PASS | FAIL | numerical practical (GPU kernel reported invalid data) | - | - | q/runs/s20/transparent-q |
| transparent-q | transparent_lighting | 46.8 | 106.9 | PASS | FAIL | device lost | - | - | q/runs/s20/transparent-q |
| transparent-q | transparent_lighting replays (8) | - | 24-32 | PASS | not run 8/8 (source failed) | not run: source job failed | - | - | q/runs/s20/transparent-q |
| transparent-q | transparent-shadow replays (2) | - | 4-4 | PASS | not run 2/2 (source failed) | not run: source job failed | - | - | q/runs/s20/transparent-q |
| triangle-q | triangle_sample | 80.1 | 25.8 | FAIL 2 practical (sample 3133) | PASS | - | 0 / 865 / 0.000488 | - | q/runs/s20/triangle-q |
| triangle-q | surface | 141.3 | 97.4 | PASS | PASS | - | local 1244 / 3.81e-06 | - | q/runs/s20/triangle-q |
| triangle-q | triangle_lighting | 472.9 | 159.0 | PASS | PASS | - | 0 / 490 / 1.79e-06 | shade_surface_light 107, intersect_closest 75 | q/runs/s20/triangle-q |
| triangle-q | triangle_lighting replays (6) | 43-356 | 21-87 | PASS | PASS 6/6 | - | readbacks 289/417/415/473/415/464; strict 12/0/6/233/8/231; max 1.79e-06 | - | q/runs/s20/triangle-q |
| volume-ci | closed-inside | 281.5 | 302.9 | PASS | PASS | - | local 1788 / 1.53e-05 | shade_volume 21 | q/runs/s20/volume-ci |
| volume-ci | closed replays (1) | 72-72 | 34-34 | PASS | PASS 1/1 | - | readbacks 1116; strict 1788; max 1.53e-05 | - | q/runs/s20/volume-ci |
| volume-co | closed-overlap | 293.9 | 319.9 | PASS | PASS | - | local 857 / 3.05e-05 | shade_volume 20 | q/runs/s20/volume-co |
| volume-co | closed replays (1) | 71-71 | 34-34 | PASS | PASS 1/1 | - | readbacks 1192; strict 857; max 3.05e-05 | - | q/runs/s20/volume-co |
| volume-cs | closed-single | 196.5 | 214.4 | PASS | PASS | - | local 608 / 3.05e-05 | - | q/runs/s20/volume-cs |
| volume-cs | closed replays (1) | 40-40 | 22-22 | PASS | PASS 1/1 | - | readbacks 838; strict 608; max 3.05e-05 | - | q/runs/s20/volume-cs |
| volume-q | volume-state | 19.4 | 13.2 | PASS | PASS | - | local 8551 / 0.00013 | - | q/runs/s20/volume-q |
| volume-q | volume-absorption | 124.4 | 120.8 | PASS | PASS | - | local 552 / 2.29e-05 | - | q/runs/s20/volume-q |
| volume-q | volume-isotropic | 179.0 | 172.0 | PASS | PASS | - | local 1439 / 2.29e-05 | - | q/runs/s20/volume-q |
| volume-q | volume-forward | 220.6 | 170.8 | PASS | PASS | - | local 1472 / 3.62e-05 | - | q/runs/s20/volume-q |
| volume-q | volume-backward | 219.1 | 171.7 | PASS | PASS | - | local 1382 / 2.29e-05 | - | q/runs/s20/volume-q |
| volume-q | boundary-state | 83.2 | 32.4 | PASS | PASS | - | local 6492 / 0.000244 | shade_surface 24 | q/runs/s20/volume-q |
| volume-q | boundary-compaction-guard | 21.6 | 6.4 | PASS | PASS | - | local 0 / 0 | - | q/runs/s20/volume-q |
| volume-q | volume replays (4) | 55-56 | 20-21 | PASS | PASS 4/4 | - | readbacks 464/672/664/664; strict 552/1439/1472/1382; max 3.62e-05 | - | q/runs/s20/volume-q |
| host-gate | android-host-gate | 136 | 62.0 | PASS 48 exports (wall from file times) | PASS | - | 48 exports, 1579 films, 0 practical, 0 required strict, max 1.43e-05 (cpu/cpu-exit/reload/stress/gpu/gpu-exit all rc 0) | - | q/device/codebrix-q-host-gate (log device/android-host-gate.log) |
| app | app-webgpu | 379 | 173.0 | PASS 0 practical | PASS | - | 3 scenes vs Linux glibc films: 0 practical, 921 strict, max 1.38e-05; tier full; cancellation ok=True; capability warning: pipeline 'shade_surface_light' took 53882 ms, over the 20000 ms compile budget (full tier); first-use latency is degraded on this driver | shade_surface_light 54 | q/android-app/webgpu-s20 (log device/app-s20.log) |
