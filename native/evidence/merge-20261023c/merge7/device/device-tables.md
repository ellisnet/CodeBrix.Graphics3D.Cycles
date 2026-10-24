
### samsung

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| materials-m7 | principled_svm | 118.6 | PASS | glibc, device-scene | strictFloatFailures=634, maximumAbsoluteError=3.61e-06 |
| materials-m7 | principled_bounce | 85.3 | PASS | device's own CPU / gate | strictFloatFailures=3466, maximumAbsoluteError=0.125 |
| materials-m7 | principled_render | 68.4 | PASS | glibc, device-scene | strictImageFailures=67, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=67 max=6.44e-06 |
| materials-m7 | nodes_svm | 1506.7 | PASS | glibc, device-scene | strictFloatFailures=18450, maximumAbsoluteError=0.00195 |
| materials-m7 | nodes_world | 505.8 | PASS | device's own CPU / gate | strictFloatFailures=16230, maximumAbsoluteError=6.1e-05 |
| materials-m7 | nodes_render | 115.5 | PASS | glibc, device-scene | strictImageFailures=1689, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1689 max=3.72e-05 |
| materials-m7 | principled-replay | 43.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.44e-06, strictImageFailures=67 |
| materials-m7 | nodes-replay | 91.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.72e-05, strictImageFailures=1689 |
| triangle-m7 | triangle_sample | 21.3 | PASS | glibc, device-scene | maximumAbsoluteError=0.000488; host re-judged vs glibc PASS: practical=0 strict=865 max=0.000488 |
| triangle-m7 | surface | 24.1 | PASS | device's own CPU / gate | strictFloatFailures=1246, maximumAbsoluteError=3.81e-06 |
| triangle-m7 | triangle_lighting | 80.6 | PASS | glibc, device-scene | maximumAbsoluteError=1.79e-06; host re-judged vs glibc PASS: practical=0 strict=490 max=1.79e-06 |
| triangle-m7 | replay-0 | 16.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=12 |
| triangle-m7 | replay-1 | 21.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=0 |
| triangle-m7 | replay-2 | 21.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.41e-07, strictImageFailures=6 |
| triangle-m7 | replay-3 | 22.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-06, strictImageFailures=233 |
| triangle-m7 | replay-4 | 21.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.58e-07, strictImageFailures=8 |
| triangle-m7 | replay-5 | 35.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-06, strictImageFailures=231 |
| emitter-m7 | emitter_sample | 8.0 | PASS | glibc, device-scene | maximumAbsoluteError=9.06e-06; host re-judged vs glibc PASS: practical=0 strict=135 max=9.06e-06 |
| regression-m7 | glass_render | 50.3 | PASS | glibc, device-scene | strictImageFailures=63, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=63 max=1.91e-06 |
| regression-m7 | lighting_mis | 29.5 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1057 max=6.08e-06 |
| regression-m7 | transparent_lighting | 91.4 | PASS | glibc, device-scene | maximumAbsoluteError=6.02e-06; host re-judged vs glibc PASS: practical=0 strict=685 max=6.02e-06 |
| lighting-m7 | lighting_mis | 29.4 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1057 max=6.08e-06 |
| lighting-m7 | broad_lighting | 59.6 | PASS | glibc, device-scene | maximumAbsoluteError=3.81e-05; host re-judged vs glibc PASS: practical=0 strict=953 max=3.81e-05 |
| lighting-m7 | lighting_mis-replay-0 | 16.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m7 | lighting_mis-replay-1 | 17.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m7 | broad_lighting-replay-0 | 17.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m7 | broad_lighting-replay-1 | 17.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.17e-07, strictImageFailures=13 |
| transparent-m7 | transparent_shadow | 17.6 | PASS | device's own CPU / gate | maximumAbsoluteError=1.19e-07 |
| transparent-m7 | transparent_lighting | 92.8 | PASS | glibc, device-scene | maximumAbsoluteError=6.02e-06; host re-judged vs glibc PASS: practical=0 strict=685 max=6.02e-06 |
| transparent-m7 | transparent_lighting-replay-0 | 20.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m7 | transparent_lighting-replay-1 | 20.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m7 | transparent_lighting-replay-2 | 25.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.81e-06, strictImageFailures=73 |
| transparent-m7 | transparent_lighting-replay-3 | 26.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.55e-06, strictImageFailures=133 |
| transparent-m7 | transparent_lighting-replay-4 | 27.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=167 |
| transparent-m7 | transparent_lighting-replay-5 | 27.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.9e-07, strictImageFailures=3 |
| transparent-m7 | transparent_lighting-replay-6 | 26.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=103 |
| transparent-m7 | transparent_lighting-replay-7 | 26.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=206 |
| transparent-m7 | transparent-shadow-replay-direct | 6.0 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m7 | transparent-shadow-replay-indexed | 6.1 | PASS | source capture CPU film | maximumAbsoluteImageError=0, strictImageFailures=0 |
| terminator-m7 | shadow_terminator | 100.7 | PASS | glibc, device-scene | maximumAbsoluteError=6.2e-05; host re-judged vs glibc PASS: practical=0 strict=114 max=6.2e-05 |
| terminator-m7 | terminator-replay-0 | 13.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m7 | terminator-replay-1 | 15.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m7 | terminator-replay-2 | 15.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m7 | terminator-replay-3 | 15.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m7 | terminator-replay-4 | 25.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| terminator-m7 | terminator-replay-5 | 25.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| terminator-m7 | terminator-replay-6 | 25.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| terminator-m7 | terminator-replay-7 | 25.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| terminator-m7 | terminator-replay-8 | 26.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.93e-05, strictImageFailures=8 |
| terminator-m7 | terminator-replay-9 | 37.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.91e-05, strictImageFailures=14 |
| spec-refusal-m7 | glass_render-spec | 47.5 | PASS | glibc, device-scene | strictImageFailures=63, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=63 max=1.91e-06 |
| spec-refusal-m7 | glass_render-nospec-refused | 0.2 | PASS | expected refusal |  |
| spec-refusal-m7 | glass_render-superset-refused | 0.2 | PASS | expected refusal |  |
| environment-m7 | environment | 455.8 | PASS | glibc, device-scene | maximumAbsoluteError=3.81e-06; host re-judged vs glibc PASS: practical=0 strict=349 max=3.81e-06 |
| environment-m7 | environment-replay-0 | 25.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.86e-06, strictImageFailures=48 |
| environment-m7 | environment-replay-1 | 39.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-06, strictImageFailures=36 |
| environment-m7 | environment-replay-2 | 33.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.86e-06, strictImageFailures=40 |
| environment-m7 | environment-replay-3 | 40.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-06, strictImageFailures=30 |
| environment-m7 | environment-replay-4 | 42.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=62 |
| environment-m7 | environment-replay-5 | 36.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.81e-06, strictImageFailures=28 |
| environment-m7 | environment-replay-6 | 64.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=5 |
| environment-m7 | environment-replay-7 | 61.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| environment-m7 | environment-replay-8 | 61.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.54e-07, strictImageFailures=41 |
| environment-m7 | environment-replay-9 | 63.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.53e-07, strictImageFailures=4 |
| environment-m7 | environment-replay-10 | 64.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.43e-06, strictImageFailures=39 |
| environment-m7 | environment-replay-11 | 61.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.43e-06, strictImageFailures=16 |
| arm-m7 | microfacet_render | 46.0 | PASS | glibc, device-scene | strictImageFailures=100, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=100 max=2.38e-06 |
| motion-m7 | render-static | 45.7 | PASS | glibc, device-scene | strictImageFailures=326, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=326 max=7.63e-06 |
| motion-m7 | render-motion-0 | 42.9 | PASS | glibc, device-scene | strictImageFailures=322, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=322 max=2.53e-05 |
| motion-m7 | render-motion-1 | 43.8 | PASS | glibc, device-scene | strictImageFailures=287, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=287 max=7.63e-06 |
| motion-m7 | render-motion-2 | 44.4 | PASS | glibc, device-scene | strictImageFailures=351, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=351 max=1.67e-05 |
| motion-m7 | render-motion-3 | 44.4 | PASS | glibc, device-scene | strictImageFailures=602, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=602 max=3.36e-05 |
| motion-m7 | render-motion-4 | 43.8 | PASS | glibc, device-scene | strictImageFailures=328, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=328 max=1e-05 |
| motion-m7 | render-motion-5 | 44.5 | PASS | glibc, device-scene | strictImageFailures=312, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=312 max=1.57e-05 |
| motion-m7 | render-motion-6 | 42.7 | PASS | glibc, device-scene | strictImageFailures=515, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=515 max=4.58e-05 |
| motion-m7 | render-motion-7 | 43.1 | PASS | glibc, device-scene | strictImageFailures=327, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=327 max=1e-05 |
| pixel-motion-own | object-moving-images | 45.2 | PASS | device's own CPU / gate | maximumAbsoluteError=6.08e-06 |
| pixel-motion-own | object-stationary-images | 36.3 | PASS | device's own CPU / gate | maximumAbsoluteError=6.08e-06 |
| pixel-motion-own | object-replay-motion-0 | 31.2 | PASS | source capture CPU film | maximumAbsoluteImageError=2.38e-07, strictImageFailures=6 |
| pixel-motion-own | object-replay-motion-1 | 32.2 | PASS | source capture CPU film | maximumAbsoluteImageError=2.92e-06, strictImageFailures=17 |
| pixel-motion-own | object-replay-motion-2 | 32.2 | PASS | source capture CPU film | maximumAbsoluteImageError=2.43e-06, strictImageFailures=290 |
| pixel-motion-own | object-replay-motion-3 | 32.5 | PASS | source capture CPU film | maximumAbsoluteImageError=1.02e-07, strictImageFailures=1 |
| pixel-motion-own | object-replay-motion-4 | 32.9 | PASS | source capture CPU film | maximumAbsoluteImageError=6.08e-06, strictImageFailures=288 |
| pixel-motion-own | object-replay-motion-5 | 33.9 | PASS | source capture CPU film | maximumAbsoluteImageError=4.53e-06, strictImageFailures=59 |
| pixel-motion-own | object-replay-motion-6 | 33.0 | PASS | source capture CPU film | maximumAbsoluteImageError=6.08e-06, strictImageFailures=335 |
| pixel-motion-own | object-replay-motion-7 | 32.9 | PASS | source capture CPU film | maximumAbsoluteImageError=6.02e-06, strictImageFailures=241 |
| pixel-motion-x64 | object-moving-images | 46.7 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1180 max=6.08e-06 |
| pixel-motion-x64 | object-stationary-images | 36.8 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1135 max=6.08e-06 |
| pixel-motion-x64 | object-replay-motion-0 | 33.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.62e-06, strictImageFailures=8 |
| pixel-motion-x64 | object-replay-motion-1 | 34.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.36e-07, strictImageFailures=10 |
| pixel-motion-x64 | object-replay-motion-2 | 34.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.4e-06, strictImageFailures=291 |
| pixel-motion-x64 | object-replay-motion-3 | 34.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.31e-09, strictImageFailures=0 |
| pixel-motion-x64 | object-replay-motion-4 | 34.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=271 |
| pixel-motion-x64 | object-replay-motion-5 | 34.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.29e-06, strictImageFailures=45 |
| pixel-motion-x64 | object-replay-motion-6 | 34.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=329 |
| pixel-motion-x64 | object-replay-motion-7 | 35.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=226 |
| volume-m7 | volume-state | 10.3 | PASS | device's own CPU / gate |  |
| volume-m7 | volume-absorption | 62.0 | PASS | glibc, device-scene | strictImageFailures=834, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=834 max=2.29e-05 |
| volume-m7 | volume-isotropic | 84.0 | PASS | glibc, device-scene | strictImageFailures=1429, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1429 max=3.05e-05 |
| volume-m7 | volume-forward | 79.0 | PASS | glibc, device-scene | strictImageFailures=1472, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1472 max=3.05e-05 |
| volume-m7 | volume-backward | 79.3 | PASS | glibc, device-scene | strictImageFailures=1366, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1366 max=3.05e-05 |
| volume-m7 | boundary-state | 23.2 | PASS | device's own CPU / gate |  |
| volume-m7 | boundary-compaction-guard | 4.4 | PASS | device's own CPU / gate |  |
| volume-m7 | closed-single | 98.9 | PASS | glibc, device-scene | strictImageFailures=813, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=813 max=3.05e-05 |
| volume-m7 | closed-overlap | 153.2 | PASS | glibc, device-scene | strictImageFailures=1007, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1007 max=3.05e-05 |
| volume-m7 | closed-inside | 145.5 | PASS | glibc, device-scene | strictImageFailures=2054, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=2054 max=3.05e-05 |
| volume-m7 | volume-replay-absorption | 16.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.29e-05, strictImageFailures=834 |
| volume-m7 | volume-replay-isotropic | 17.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=1429 |
| volume-m7 | volume-replay-forward | 17.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=1472 |
| volume-m7 | volume-replay-backward | 17.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=1366 |
| volume-m7 | closed-replay-single | 18.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=813 |
| volume-m7 | closed-replay-overlap | 28.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=1007 |
| volume-m7 | closed-replay-inside | 28.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=2054 |
| hostcap-linux-triangle | tri0 | 16.8 | PASS | source capture CPU film | maximumAbsoluteImageError=4.77e-07, strictImageFailures=12 |
| hostcap-linux-triangle | tri1 | 21.2 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| hostcap-linux-triangle | tri2 | 21.3 | PASS | source capture CPU film | maximumAbsoluteImageError=6.41e-07, strictImageFailures=12 |
| hostcap-linux-triangle | tri3 | 22.1 | PASS | source capture CPU film | maximumAbsoluteImageError=1.76e-06, strictImageFailures=233 |
| hostcap-linux-triangle | tri4 | 21.8 | PASS | source capture CPU film | maximumAbsoluteImageError=2.53e-07, strictImageFailures=5 |
| hostcap-linux-triangle | tri5 | 35.9 | PASS | source capture CPU film | maximumAbsoluteImageError=1.79e-06, strictImageFailures=237 |
| hostcap-linux-terminator | term0 | 12.7 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term1 | 14.7 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term2 | 14.9 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term3 | 15.0 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term4 | 25.5 | PASS | source capture CPU film | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| hostcap-linux-terminator | term5 | 26.0 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| hostcap-linux-terminator | term6 | 25.6 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| hostcap-linux-terminator | term7 | 26.6 | PASS | source capture CPU film | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| hostcap-linux-terminator | term8 | 26.2 | PASS | source capture CPU film | maximumAbsoluteImageError=2.93e-05, strictImageFailures=9 |
| hostcap-linux-terminator | term9 | 35.2 | PASS | source capture CPU film | maximumAbsoluteImageError=2.91e-05, strictImageFailures=15 |

### hp

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| materials-m7 | principled_svm | 162.3 | PASS | glibc, device-scene | strictFloatFailures=946, maximumAbsoluteError=9.54e-07 |
| materials-m7 | principled_bounce | 234.2 | PASS | device's own CPU / gate | strictFloatFailures=5230, maximumAbsoluteError=0.125 |
| materials-m7 | principled_render | 140.2 | PASS | glibc, device-scene | strictImageFailures=91, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=91 max=6.44e-06 |
| materials-m7 | nodes_svm | 802.8 | PASS | glibc, device-scene | strictFloatFailures=10978, maximumAbsoluteError=0.0107 |
| materials-m7 | nodes_world | 257.2 | PASS | device's own CPU / gate | strictFloatFailures=9052, maximumAbsoluteError=6.1e-05 |
| materials-m7 | nodes_render | 96.4 | PASS | glibc, device-scene | strictImageFailures=1794, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1794 max=0.000103 |
| materials-m7 | principled-replay | 118.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.44e-06, strictImageFailures=91 |
| materials-m7 | nodes-replay | 73.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=0.000103, strictImageFailures=1794 |
| triangle-m7 | triangle_sample | 23.1 | PASS | glibc, device-scene | maximumAbsoluteError=0.000488; host re-judged vs glibc PASS: practical=0 strict=865 max=0.000488 |
| triangle-m7 | surface | 38.8 | PASS | device's own CPU / gate | strictFloatFailures=1801, maximumAbsoluteError=3.81e-06 |
| triangle-m7 | triangle_lighting | 102.6 | PASS | glibc, device-scene | maximumAbsoluteError=2.32e-06; host re-judged vs glibc PASS: practical=0 strict=867 max=2.32e-06 |
| triangle-m7 | replay-0 | 37.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=9 |
| triangle-m7 | replay-1 | 46.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| triangle-m7 | replay-2 | 47.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.41e-07, strictImageFailures=12 |
| triangle-m7 | replay-3 | 46.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-06, strictImageFailures=426 |
| triangle-m7 | replay-4 | 46.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.38e-07, strictImageFailures=7 |
| triangle-m7 | replay-5 | 66.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-06, strictImageFailures=413 |
| emitter-m7 | emitter_sample | 20.0 | PASS | glibc, device-scene | maximumAbsoluteError=0.00513; host re-judged vs glibc PASS: practical=0 strict=235 max=0.00513 |
| regression-m7 | glass_render | 60.9 | PASS | glibc, device-scene | strictImageFailures=104, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=104 max=9.54e-07 |
| regression-m7 | lighting_mis | 52.2 | PASS | glibc, device-scene | maximumAbsoluteError=8.34e-06; host re-judged vs glibc PASS: practical=0 strict=2682 max=8.34e-06 |
| regression-m7 | transparent_lighting | 79.7 | PASS | glibc, device-scene | maximumAbsoluteError=7.75e-06; host re-judged vs glibc PASS: practical=0 strict=1679 max=7.75e-06 |
| lighting-m7 | lighting_mis | 52.1 | PASS | glibc, device-scene | maximumAbsoluteError=8.34e-06; host re-judged vs glibc PASS: practical=0 strict=2682 max=8.34e-06 |
| lighting-m7 | broad_lighting | 68.0 | PASS | glibc, device-scene | maximumAbsoluteError=5.34e-05; host re-judged vs glibc PASS: practical=0 strict=1732 max=5.34e-05 |
| lighting-m7 | lighting_mis-replay-0 | 39.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=3 |
| lighting-m7 | lighting_mis-replay-1 | 39.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=11 |
| lighting-m7 | broad_lighting-replay-0 | 39.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=1 |
| lighting-m7 | broad_lighting-replay-1 | 39.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.07e-07, strictImageFailures=26 |
| transparent-m7 | transparent_shadow | 12.4 | PASS | device's own CPU / gate | maximumAbsoluteError=1.19e-07 |
| transparent-m7 | transparent_lighting | 78.9 | PASS | glibc, device-scene | maximumAbsoluteError=7.75e-06; host re-judged vs glibc PASS: practical=0 strict=1679 max=7.75e-06 |
| transparent-m7 | transparent_lighting-replay-0 | 43.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.49e-07, strictImageFailures=1 |
| transparent-m7 | transparent_lighting-replay-1 | 43.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=8 |
| transparent-m7 | transparent_lighting-replay-2 | 49.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=89 |
| transparent-m7 | transparent_lighting-replay-3 | 49.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.01e-06, strictImageFailures=323 |
| transparent-m7 | transparent_lighting-replay-4 | 49.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.75e-06, strictImageFailures=435 |
| transparent-m7 | transparent_lighting-replay-5 | 50.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.75e-07, strictImageFailures=4 |
| transparent-m7 | transparent_lighting-replay-6 | 48.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.56e-06, strictImageFailures=299 |
| transparent-m7 | transparent_lighting-replay-7 | 50.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.79e-06, strictImageFailures=520 |
| transparent-m7 | transparent-shadow-replay-direct | 5.8 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m7 | transparent-shadow-replay-indexed | 5.8 | PASS | source capture CPU film | maximumAbsoluteImageError=7.45e-09, strictImageFailures=0 |
| terminator-m7 | shadow_terminator | 155.1 | PASS | glibc, device-scene | maximumAbsoluteError=1.53e-05; host re-judged vs glibc PASS: practical=0 strict=107 max=1.53e-05 |
| terminator-m7 | terminator-replay-0 | 32.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m7 | terminator-replay-1 | 40.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m7 | terminator-replay-2 | 41.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m7 | terminator-replay-3 | 40.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m7 | terminator-replay-4 | 68.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=25 |
| terminator-m7 | terminator-replay-5 | 68.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=21 |
| terminator-m7 | terminator-replay-6 | 68.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=21 |
| terminator-m7 | terminator-replay-7 | 68.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=25 |
| terminator-m7 | terminator-replay-8 | 67.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.38e-07, strictImageFailures=5 |
| terminator-m7 | terminator-replay-9 | 82.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.54e-07, strictImageFailures=10 |
| spec-refusal-m7 | glass_render-spec | 59.7 | PASS | glibc, device-scene | strictImageFailures=104, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=104 max=9.54e-07 |
| spec-refusal-m7 | glass_render-nospec-refused | 0.3 | PASS | expected refusal |  |
| spec-refusal-m7 | glass_render-superset-refused | 0.3 | PASS | expected refusal |  |
| environment-m7 | environment | 348.2 | PASS | glibc, device-scene | maximumAbsoluteError=2.86e-05; host re-judged vs glibc PASS: practical=0 strict=1343 max=2.86e-05 |
| environment-m7 | environment-replay-0 | 46.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.25e-06, strictImageFailures=122 |
| environment-m7 | environment-replay-1 | 71.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-06, strictImageFailures=135 |
| environment-m7 | environment-replay-2 | 62.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-06, strictImageFailures=146 |
| environment-m7 | environment-replay-3 | 71.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=8.82e-06, strictImageFailures=123 |
| environment-m7 | environment-replay-4 | 71.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.81e-06, strictImageFailures=216 |
| environment-m7 | environment-replay-5 | 62.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.86e-05, strictImageFailures=130 |
| environment-m7 | environment-replay-6 | 108.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=8.52e-06, strictImageFailures=65 |
| environment-m7 | environment-replay-7 | 110.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=7 |
| environment-m7 | environment-replay-8 | 110.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.69e-05, strictImageFailures=163 |
| environment-m7 | environment-replay-9 | 110.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.66e-06, strictImageFailures=47 |
| environment-m7 | environment-replay-10 | 110.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.91e-06, strictImageFailures=136 |
| environment-m7 | environment-replay-11 | 110.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.43e-06, strictImageFailures=53 |
| hostcap-linux-triangle-run1 | tri0 | 39.9 | PASS | source capture CPU film | maximumAbsoluteImageError=4.77e-07, strictImageFailures=9 |
| hostcap-linux-triangle-run1 | tri1 | 49.2 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| hostcap-linux-triangle-run1 | tri2 | 49.7 | PASS | source capture CPU film | maximumAbsoluteImageError=6.41e-07, strictImageFailures=12 |
| hostcap-linux-triangle-run1 | tri3 | 49.1 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-06, strictImageFailures=426 |
| hostcap-linux-triangle-run1 | tri4 | 49.4 | PASS | source capture CPU film | maximumAbsoluteImageError=2.38e-07, strictImageFailures=7 |
| hostcap-linux-triangle-run1 | tri5 | 69.3 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-06, strictImageFailures=412 |
| hostcap-linux-terminator-run1 | term0 | 34.4 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator-run1 | term1 | 41.5 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator-run1 | term2 | 42.2 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator-run1 | term3 | 41.3 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator-run1 | term4 | 69.2 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=25 |
| hostcap-linux-terminator-run1 | term5 | 68.4 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=21 |
| hostcap-linux-terminator-run1 | term6 | 69.5 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=21 |
| hostcap-linux-terminator-run1 | term7 | 69.5 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=25 |
| hostcap-linux-terminator-run1 | term8 | 70.2 | PASS | source capture CPU film | maximumAbsoluteImageError=4.77e-07, strictImageFailures=8 |
| hostcap-linux-terminator-run1 | term9 | 83.8 | PASS | source capture CPU film | maximumAbsoluteImageError=1.07e-06, strictImageFailures=12 |
| arm-m7 | microfacet_render | 60.3 | PASS | glibc, device-scene | strictImageFailures=119, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=119 max=1.91e-06 |
| motion-m7 | render-static | 45.1 | PASS | glibc, device-scene | strictImageFailures=400, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=400 max=7.63e-06 |
| motion-m7 | render-motion-0 | 49.3 | PASS | glibc, device-scene | strictImageFailures=407, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=407 max=2.53e-05 |
| motion-m7 | render-motion-1 | 48.9 | PASS | glibc, device-scene | strictImageFailures=361, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=361 max=1.14e-05 |
| motion-m7 | render-motion-2 | 49.7 | PASS | glibc, device-scene | strictImageFailures=442, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=442 max=1.67e-05 |
| motion-m7 | render-motion-3 | 49.9 | PASS | glibc, device-scene | strictImageFailures=657, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=657 max=3.36e-05 |
| motion-m7 | render-motion-4 | 49.1 | PASS | glibc, device-scene | strictImageFailures=443, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=443 max=4.48e-05 |
| motion-m7 | render-motion-5 | 49.4 | PASS | glibc, device-scene | strictImageFailures=382, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=382 max=1.57e-05 |
| motion-m7 | render-motion-6 | 48.3 | PASS | glibc, device-scene | strictImageFailures=592, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=592 max=4.58e-05 |
| motion-m7 | render-motion-7 | 48.4 | PASS | glibc, device-scene | strictImageFailures=444, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=444 max=4.48e-05 |
| volume-m7 | volume-state | 11.8 | PASS | device's own CPU / gate |  |
| volume-m7 | volume-absorption | 60.2 | PASS | glibc, device-scene | strictImageFailures=1133, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1133 max=1.53e-05 |
| volume-m7 | volume-isotropic | 76.5 | PASS | glibc, device-scene | strictImageFailures=1607, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1607 max=3.24e-05 |
| volume-m7 | volume-forward | 76.6 | PASS | glibc, device-scene | strictImageFailures=1665, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1665 max=4.67e-05 |
| volume-m7 | volume-backward | 75.8 | PASS | glibc, device-scene | strictImageFailures=1609, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1609 max=4.01e-05 |
| volume-m7 | boundary-state | 46.7 | PASS | device's own CPU / gate |  |
| volume-m7 | boundary-compaction-guard | 7.1 | PASS | device's own CPU / gate |  |
| volume-m7 | closed-single | 91.6 | PASS | glibc, device-scene | strictImageFailures=974, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=974 max=3.05e-05 |
| volume-m7 | closed-overlap | 148.6 | PASS | glibc, device-scene | strictImageFailures=1251, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1251 max=4.58e-05 |
| volume-m7 | closed-inside | 144.5 | PASS | glibc, device-scene | strictImageFailures=2283, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=2283 max=2.29e-05 |
| volume-m7 | volume-replay-absorption | 27.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=1133 |
| volume-m7 | volume-replay-isotropic | 27.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.24e-05, strictImageFailures=1607 |
| volume-m7 | volume-replay-forward | 27.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.67e-05, strictImageFailures=1665 |
| volume-m7 | volume-replay-backward | 27.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.01e-05, strictImageFailures=1609 |
| volume-m7 | closed-replay-single | 28.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=974 |
| volume-m7 | closed-replay-overlap | 54.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.58e-05, strictImageFailures=1251 |
| volume-m7 | closed-replay-inside | 55.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.29e-05, strictImageFailures=2283 |
| hostcap-linux-triangle | tri0 | 40.4 | PASS | source capture CPU film | maximumAbsoluteImageError=4.77e-07, strictImageFailures=9 |
| hostcap-linux-triangle | tri1 | 49.1 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| hostcap-linux-triangle | tri2 | 49.0 | PASS | source capture CPU film | maximumAbsoluteImageError=6.41e-07, strictImageFailures=12 |
| hostcap-linux-triangle | tri3 | 48.9 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-06, strictImageFailures=426 |
| hostcap-linux-triangle | tri4 | 49.5 | PASS | source capture CPU film | maximumAbsoluteImageError=2.38e-07, strictImageFailures=7 |
| hostcap-linux-triangle | tri5 | 68.9 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-06, strictImageFailures=412 |
| hostcap-linux-terminator | term0 | 34.4 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term1 | 41.3 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term2 | 41.6 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term3 | 41.6 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term4 | 68.9 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=25 |
| hostcap-linux-terminator | term5 | 68.9 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=21 |
| hostcap-linux-terminator | term6 | 68.9 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=21 |
| hostcap-linux-terminator | term7 | 69.1 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=25 |
| hostcap-linux-terminator | term8 | 70.1 | PASS | source capture CPU film | maximumAbsoluteImageError=4.77e-07, strictImageFailures=8 |
| hostcap-linux-terminator | term9 | 84.9 | PASS | source capture CPU film | maximumAbsoluteImageError=1.07e-06, strictImageFailures=12 |

### s20

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| materials-s20-principled_svm | principled_svm | 413.4 | PASS | glibc, device-scene | strictFloatFailures=557, maximumAbsoluteError=3.59e-06 |
| a650-refusals-m7 | nodes_world-refused | 0.5 | PASS | expected refusal |  |
| a650-refusals-m7 | nodes_svm-refused | 0.4 | PASS | expected refusal |  |
| a650-refusals-m7 | nodes_render-refused | 0.5 | PASS | expected refusal |  |
| triangle-s20-triangle_sample | triangle_sample | 82.6 | PASS | glibc, device-scene | maximumAbsoluteError=0.000488; host re-judged vs glibc PASS: practical=0 strict=865 max=0.000488 |
| regression-s20-glass_render | glass_render | 87.6 | PASS | glibc, device-scene | strictImageFailures=64, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=64 max=1.91e-06 |
| regression-s20-lighting_mis | lighting_mis | 66.9 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1057 max=6.08e-06 |
| triangle-s20-surface | surface | 46.5 | PASS | device's own CPU / gate | strictFloatFailures=1244, maximumAbsoluteError=3.81e-06 |
| transparent-s20-transparent_lighting | transparent_lighting | 203.8 | PASS | glibc, device-scene | maximumAbsoluteError=6.02e-06; host re-judged vs glibc PASS: practical=0 strict=683 max=6.02e-06 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-0 | 52.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-1 | 54.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-2 | 64.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.81e-06, strictImageFailures=72 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-3 | 64.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.55e-06, strictImageFailures=133 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-4 | 65.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=167 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-5 | 65.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.9e-07, strictImageFailures=3 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-6 | 64.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=103 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-7 | 64.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=205 |
| transparent-s20-transparent_shadow | transparent_shadow | 16.2 | PASS | device's own CPU / gate | maximumAbsoluteError=1.19e-07 |
| transparent-s20-transparent_shadow | transparent-shadow-replay-direct | 11.2 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-s20-transparent_shadow | transparent-shadow-replay-indexed | 11.0 | PASS | source capture CPU film | maximumAbsoluteImageError=0, strictImageFailures=0 |
| materials-s20-principled_render | principled_render | 212.2 | PASS | glibc, device-scene | strictImageFailures=64, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=64 max=6.44e-06 |
| materials-s20-principled_render | principled-replay | 173.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.44e-06, strictImageFailures=64 |
| pixel-motion-s20-object-moving-images | object-moving-images | 149.7 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1181 max=6.08e-06 |
| pixel-motion-s20-object-moving-images | object-replay-motion-0 | 128.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.62e-06, strictImageFailures=7 |
| pixel-motion-s20-object-moving-images | object-replay-motion-1 | 133.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.36e-07, strictImageFailures=10 |
| pixel-motion-s20-object-moving-images | object-replay-motion-2 | 182.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.4e-06, strictImageFailures=291 |
| pixel-motion-s20-object-moving-images | object-replay-motion-3 | 189.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.31e-09, strictImageFailures=0 |
| pixel-motion-s20-object-moving-images | object-replay-motion-4 | 190.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=272 |
| pixel-motion-s20-object-moving-images | object-replay-motion-5 | 189.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.29e-06, strictImageFailures=45 |
| pixel-motion-s20-object-moving-images | object-replay-motion-6 | 191.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=329 |
| pixel-motion-s20-object-moving-images | object-replay-motion-7 | 191.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=227 |
| pixel-motion-s20-deforming-moving-images | deforming-moving-images | 234.5 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1151 max=6.08e-06 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-0 | 85.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.38e-07, strictImageFailures=2 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-1 | 131.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.17e-07, strictImageFailures=7 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-2 | 143.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.4e-06, strictImageFailures=304 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-3 | 188.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.31e-09, strictImageFailures=0 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-4 | 188.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=280 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-5 | 188.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.98e-06, strictImageFailures=34 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-6 | 187.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=300 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-7 | 186.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=224 |
| materials-s20-principled_bounce | principled_bounce | 337.3 | PASS | device's own CPU / gate | strictFloatFailures=2796, maximumAbsoluteError=0.125 |
