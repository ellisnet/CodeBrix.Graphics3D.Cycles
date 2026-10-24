
### samsung

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| materials-m6 | principled_svm | 118.6 | PASS | glibc, device-scene | strictFloatFailures=634, maximumAbsoluteError=3.61e-06 |
| materials-m6 | principled_bounce | 83.8 | PASS | device's own CPU / gate | strictFloatFailures=3466, maximumAbsoluteError=0.125 |
| materials-m6 | principled_render | 73.4 | PASS | glibc, device-scene | strictImageFailures=68, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=68 max=6.44e-06 |
| materials-m6 | nodes_svm | 1486.7 | PASS | glibc, device-scene | strictFloatFailures=18450, maximumAbsoluteError=0.00195 |
| materials-m6 | nodes_world | 492.8 | PASS | device's own CPU / gate | strictFloatFailures=16242, maximumAbsoluteError=6.1e-05 |
| materials-m6 | nodes_render | 113.5 | PASS | glibc, device-scene | strictImageFailures=1728, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1728 max=3.72e-05 |
| materials-m6 | principled-replay | 42.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.44e-06, strictImageFailures=68 |
| materials-m6 | nodes-replay | 90.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.72e-05, strictImageFailures=1728 |
| triangle-m6 | triangle_sample | 20.6 | PASS | glibc, device-scene | maximumAbsoluteError=0.000488; host re-judged vs glibc PASS: practical=0 strict=865 max=0.000488 |
| triangle-m6 | surface | 23.9 | PASS | device's own CPU / gate | strictFloatFailures=1246, maximumAbsoluteError=3.81e-06 |
| triangle-m6 | triangle_lighting | 77.5 | PASS | glibc, device-scene | maximumAbsoluteError=1.79e-06; host re-judged vs glibc PASS: practical=0 strict=516 max=1.79e-06 |
| triangle-m6 | replay-0 | 15.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=12 |
| triangle-m6 | replay-1 | 20.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=5 |
| triangle-m6 | replay-2 | 20.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.41e-07, strictImageFailures=8 |
| triangle-m6 | replay-3 | 21.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-06, strictImageFailures=235 |
| triangle-m6 | replay-4 | 21.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.73e-07, strictImageFailures=11 |
| triangle-m6 | replay-5 | 34.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-06, strictImageFailures=245 |
| emitter-m6 | emitter_sample | 7.8 | PASS | glibc, device-scene | maximumAbsoluteError=9.06e-06; host re-judged vs glibc PASS: practical=0 strict=135 max=9.06e-06 |
| regression-m6 | glass_render | 48.4 | PASS | glibc, device-scene | strictImageFailures=63, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=63 max=1.91e-06 |
| regression-m6 | lighting_mis | 29.7 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1070 max=6.08e-06 |
| regression-m6 | transparent_lighting | 89.5 | PASS | glibc, device-scene | maximumAbsoluteError=6.02e-06; host re-judged vs glibc PASS: practical=0 strict=693 max=6.02e-06 |
| lighting-m6 | lighting_mis | 29.0 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1070 max=6.08e-06 |
| lighting-m6 | broad_lighting | 57.0 | PASS | glibc, device-scene | maximumAbsoluteError=3.81e-05; host re-judged vs glibc PASS: practical=0 strict=993 max=3.81e-05 |
| lighting-m6 | lighting_mis-replay-0 | 16.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m6 | lighting_mis-replay-1 | 16.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m6 | broad_lighting-replay-0 | 16.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m6 | broad_lighting-replay-1 | 17.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.17e-07, strictImageFailures=15 |
| transparent-m6 | transparent_shadow | 17.9 | PASS | device's own CPU / gate | maximumAbsoluteError=1.19e-07 |
| transparent-m6 | transparent_lighting | 90.5 | PASS | glibc, device-scene | maximumAbsoluteError=6.02e-06; host re-judged vs glibc PASS: practical=0 strict=693 max=6.02e-06 |
| transparent-m6 | transparent_lighting-replay-0 | 19.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m6 | transparent_lighting-replay-1 | 20.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m6 | transparent_lighting-replay-2 | 25.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.81e-06, strictImageFailures=77 |
| transparent-m6 | transparent_lighting-replay-3 | 25.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.29e-06, strictImageFailures=134 |
| transparent-m6 | transparent_lighting-replay-4 | 26.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=168 |
| transparent-m6 | transparent_lighting-replay-5 | 26.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.9e-07, strictImageFailures=3 |
| transparent-m6 | transparent_lighting-replay-6 | 25.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=103 |
| transparent-m6 | transparent_lighting-replay-7 | 25.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=208 |
| transparent-m6 | transparent-shadow-replay-direct | 6.0 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m6 | transparent-shadow-replay-indexed | 6.3 | PASS | source capture CPU film | maximumAbsoluteImageError=0, strictImageFailures=0 |
| terminator-m6 | shadow_terminator | 98.0 | PASS | glibc, device-scene | maximumAbsoluteError=6.2e-05; host re-judged vs glibc PASS: practical=0 strict=124 max=6.2e-05 |
| terminator-m6 | terminator-replay-0 | 12.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m6 | terminator-replay-1 | 14.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m6 | terminator-replay-2 | 14.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m6 | terminator-replay-3 | 14.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m6 | terminator-replay-4 | 24.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| terminator-m6 | terminator-replay-5 | 24.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| terminator-m6 | terminator-replay-6 | 24.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| terminator-m6 | terminator-replay-7 | 25.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| terminator-m6 | terminator-replay-8 | 25.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.93e-05, strictImageFailures=8 |
| terminator-m6 | terminator-replay-9 | 33.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.91e-05, strictImageFailures=24 |
| spec-refusal-m6 | glass_render-spec | 46.3 | PASS | glibc, device-scene | strictImageFailures=63, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=63 max=1.91e-06 |
| spec-refusal-m6 | glass_render-nospec-refused | 0.2 | PASS | expected refusal |  |
| spec-refusal-m6 | glass_render-superset-refused | 0.2 | PASS | expected refusal |  |
| hostcap-linux-triangle | tri0 | 15.4 | PASS | source capture CPU film | maximumAbsoluteImageError=4.77e-07, strictImageFailures=12 |
| hostcap-linux-triangle | tri1 | 20.7 | PASS | source capture CPU film | maximumAbsoluteImageError=1.49e-07, strictImageFailures=1 |
| hostcap-linux-triangle | tri2 | 20.9 | PASS | source capture CPU film | maximumAbsoluteImageError=6.41e-07, strictImageFailures=14 |
| hostcap-linux-triangle | tri3 | 21.3 | PASS | source capture CPU film | maximumAbsoluteImageError=1.76e-06, strictImageFailures=236 |
| hostcap-linux-triangle | tri4 | 21.1 | PASS | source capture CPU film | maximumAbsoluteImageError=3.73e-07, strictImageFailures=8 |
| hostcap-linux-triangle | tri5 | 34.6 | PASS | source capture CPU film | maximumAbsoluteImageError=1.79e-06, strictImageFailures=257 |
| hostcap-linux-terminator | term0 | 12.2 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term1 | 14.3 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term2 | 14.5 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term3 | 14.7 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term4 | 24.9 | PASS | source capture CPU film | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| hostcap-linux-terminator | term5 | 25.5 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| hostcap-linux-terminator | term6 | 25.3 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| hostcap-linux-terminator | term7 | 25.1 | PASS | source capture CPU film | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| hostcap-linux-terminator | term8 | 25.5 | PASS | source capture CPU film | maximumAbsoluteImageError=2.93e-05, strictImageFailures=8 |
| hostcap-linux-terminator | term9 | 33.8 | PASS | source capture CPU film | maximumAbsoluteImageError=2.93e-05, strictImageFailures=22 |
| arm-m6 | microfacet_render | 44.7 | PASS | glibc, device-scene | strictImageFailures=100, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=100 max=2.38e-06 |
| motion-m6 | render-static | 45.3 | PASS | glibc, device-scene | strictImageFailures=327, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=327 max=7.63e-06 |
| motion-m6 | render-motion-0 | 43.7 | PASS | glibc, device-scene | strictImageFailures=345, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=345 max=2.53e-05 |
| motion-m6 | render-motion-1 | 43.7 | PASS | glibc, device-scene | strictImageFailures=306, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=306 max=7.63e-06 |
| motion-m6 | render-motion-2 | 43.8 | PASS | glibc, device-scene | strictImageFailures=446, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=446 max=0.000188 |
| motion-m6 | render-motion-3 | 42.1 | PASS | glibc, device-scene | strictImageFailures=655, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=655 max=4.01e-05 |
| motion-m6 | render-motion-4 | 42.2 | PASS | glibc, device-scene | strictImageFailures=445, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=445 max=1.53e-05 |
| motion-m6 | render-motion-5 | 42.9 | PASS | glibc, device-scene | strictImageFailures=465, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=465 max=3.05e-05 |
| motion-m6 | render-motion-6 | 41.3 | PASS | glibc, device-scene | strictImageFailures=586, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=586 max=4.58e-05 |
| motion-m6 | render-motion-7 | 41.8 | PASS | glibc, device-scene | strictImageFailures=443, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=443 max=1.53e-05 |
| pixel-motion-own | object-moving-images | 45.7 | PASS | device's own CPU / gate | maximumAbsoluteError=6.08e-06 |
| pixel-motion-own | object-stationary-images | 35.3 | PASS | device's own CPU / gate | maximumAbsoluteError=6.08e-06 |
| pixel-motion-own | object-replay-motion-0 | 29.6 | PASS | source capture CPU film | maximumAbsoluteImageError=3.58e-07, strictImageFailures=10 |
| pixel-motion-own | object-replay-motion-1 | 30.5 | PASS | source capture CPU film | maximumAbsoluteImageError=2.86e-06, strictImageFailures=26 |
| pixel-motion-own | object-replay-motion-2 | 30.9 | PASS | source capture CPU film | maximumAbsoluteImageError=2.4e-06, strictImageFailures=290 |
| pixel-motion-own | object-replay-motion-3 | 30.9 | PASS | source capture CPU film | maximumAbsoluteImageError=1.02e-07, strictImageFailures=1 |
| pixel-motion-own | object-replay-motion-4 | 31.2 | PASS | source capture CPU film | maximumAbsoluteImageError=6.08e-06, strictImageFailures=295 |
| pixel-motion-own | object-replay-motion-5 | 31.2 | PASS | source capture CPU film | maximumAbsoluteImageError=4.53e-06, strictImageFailures=59 |
| pixel-motion-own | object-replay-motion-6 | 31.5 | PASS | source capture CPU film | maximumAbsoluteImageError=6.08e-06, strictImageFailures=342 |
| pixel-motion-own | object-replay-motion-7 | 31.4 | PASS | source capture CPU film | maximumAbsoluteImageError=6.02e-06, strictImageFailures=246 |
| pixel-motion-x64 | object-moving-images | 45.3 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1306 max=6.08e-06 |
| pixel-motion-x64 | object-stationary-images | 36.3 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1157 max=6.08e-06 |
| pixel-motion-x64 | object-replay-motion-0 | 30.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.68e-06, strictImageFailures=18 |
| pixel-motion-x64 | object-replay-motion-1 | 32.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=16 |
| pixel-motion-x64 | object-replay-motion-2 | 32.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.52e-06, strictImageFailures=303 |
| pixel-motion-x64 | object-replay-motion-3 | 32.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.08e-09, strictImageFailures=0 |
| pixel-motion-x64 | object-replay-motion-4 | 32.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=302 |
| pixel-motion-x64 | object-replay-motion-5 | 33.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.29e-06, strictImageFailures=72 |
| pixel-motion-x64 | object-replay-motion-6 | 32.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=344 |
| pixel-motion-x64 | object-replay-motion-7 | 32.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=251 |
| volume-m6 | volume-state | 10.6 | PASS | device's own CPU / gate |  |
| volume-m6 | volume-absorption | 64.0 | PASS | glibc, device-scene | strictImageFailures=840, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=840 max=2.29e-05 |
| volume-m6 | volume-isotropic | 87.9 | PASS | glibc, device-scene | strictImageFailures=1437, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1437 max=3.05e-05 |
| volume-m6 | volume-forward | 82.4 | PASS | glibc, device-scene | strictImageFailures=1479, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1479 max=3.05e-05 |
| volume-m6 | volume-backward | 83.5 | PASS | glibc, device-scene | strictImageFailures=1369, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1369 max=3.05e-05 |
| volume-m6 | boundary-state | 23.2 | PASS | device's own CPU / gate |  |
| volume-m6 | boundary-compaction-guard | 4.4 | PASS | device's own CPU / gate |  |
| volume-m6 | closed-single | 97.0 | PASS | glibc, device-scene | strictImageFailures=819, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=819 max=3.05e-05 |
| volume-m6 | closed-overlap | 156.9 | PASS | glibc, device-scene | strictImageFailures=1013, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1013 max=3.05e-05 |
| volume-m6 | closed-inside | 147.0 | PASS | glibc, device-scene | strictImageFailures=2061, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=2061 max=3.05e-05 |
| volume-m6 | volume-replay-absorption | 15.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.29e-05, strictImageFailures=840 |
| volume-m6 | volume-replay-isotropic | 17.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=1437 |
| volume-m6 | volume-replay-forward | 17.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=1479 |
| volume-m6 | volume-replay-backward | 17.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=1369 |
| volume-m6 | closed-replay-single | 18.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=819 |
| volume-m6 | closed-replay-overlap | 28.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=1013 |
| volume-m6 | closed-replay-inside | 28.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=2061 |

### hp

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| materials-m6 | principled_svm | 163.8 | PASS | glibc, device-scene | strictFloatFailures=946, maximumAbsoluteError=9.54e-07 |
| materials-m6 | principled_bounce | 236.7 | PASS | device's own CPU / gate | strictFloatFailures=5237, maximumAbsoluteError=0.125 |
| materials-m6 | principled_render | 140.6 | PASS | glibc, device-scene | strictImageFailures=92, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=92 max=6.44e-06 |
| materials-m6 | nodes_svm | 806.4 | PASS | glibc, device-scene | strictFloatFailures=10978, maximumAbsoluteError=0.0107 |
| materials-m6 | nodes_world | 256.4 | PASS | device's own CPU / gate | strictFloatFailures=9125, maximumAbsoluteError=6.1e-05 |
| materials-m6 | nodes_render | 94.5 | PASS | glibc, device-scene | strictImageFailures=1843, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1843 max=0.000103 |
| materials-m6 | principled-replay | 117.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.44e-06, strictImageFailures=92 |
| materials-m6 | nodes-replay | 72.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=0.000103, strictImageFailures=1843 |
| triangle-m6 | triangle_sample | 22.7 | PASS | glibc, device-scene | maximumAbsoluteError=0.000488; host re-judged vs glibc PASS: practical=0 strict=865 max=0.000488 |
| triangle-m6 | surface | 38.2 | PASS | device's own CPU / gate | strictFloatFailures=1813, maximumAbsoluteError=3.81e-06 |
| triangle-m6 | triangle_lighting | 87.5 | PASS | glibc, device-scene | maximumAbsoluteError=2.32e-06; host re-judged vs glibc PASS: practical=0 strict=887 max=2.32e-06 |
| triangle-m6 | replay-0 | 36.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=9 |
| triangle-m6 | replay-1 | 44.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| triangle-m6 | replay-2 | 45.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.41e-07, strictImageFailures=14 |
| triangle-m6 | replay-3 | 45.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-06, strictImageFailures=427 |
| triangle-m6 | replay-4 | 45.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.73e-07, strictImageFailures=10 |
| triangle-m6 | replay-5 | 63.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.26e-06, strictImageFailures=427 |
| emitter-m6 | emitter_sample | 19.8 | PASS | glibc, device-scene | maximumAbsoluteError=0.00513; host re-judged vs glibc PASS: practical=0 strict=235 max=0.00513 |
| regression-m6 | glass_render | 59.4 | PASS | glibc, device-scene | strictImageFailures=104, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=104 max=9.54e-07 |
| regression-m6 | lighting_mis | 49.5 | PASS | glibc, device-scene | maximumAbsoluteError=8.34e-06; host re-judged vs glibc PASS: practical=0 strict=2681 max=8.34e-06 |
| regression-m6 | transparent_lighting | 76.5 | PASS | glibc, device-scene | maximumAbsoluteError=7.75e-06; host re-judged vs glibc PASS: practical=0 strict=1682 max=7.75e-06 |
| lighting-m6 | lighting_mis | 48.1 | PASS | glibc, device-scene | maximumAbsoluteError=8.34e-06; host re-judged vs glibc PASS: practical=0 strict=2681 max=8.34e-06 |
| lighting-m6 | broad_lighting | 68.6 | PASS | glibc, device-scene | maximumAbsoluteError=5.34e-05; host re-judged vs glibc PASS: practical=0 strict=1742 max=5.34e-05 |
| lighting-m6 | lighting_mis-replay-0 | 38.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=3 |
| lighting-m6 | lighting_mis-replay-1 | 38.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=11 |
| lighting-m6 | broad_lighting-replay-0 | 38.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=1 |
| lighting-m6 | broad_lighting-replay-1 | 38.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.07e-07, strictImageFailures=26 |
| transparent-m6 | transparent_shadow | 12.8 | PASS | device's own CPU / gate | maximumAbsoluteError=1.19e-07 |
| transparent-m6 | transparent_lighting | 76.8 | PASS | glibc, device-scene | maximumAbsoluteError=7.75e-06; host re-judged vs glibc PASS: practical=0 strict=1682 max=7.75e-06 |
| transparent-m6 | transparent_lighting-replay-0 | 41.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.49e-07, strictImageFailures=1 |
| transparent-m6 | transparent_lighting-replay-1 | 42.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=8 |
| transparent-m6 | transparent_lighting-replay-2 | 47.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=93 |
| transparent-m6 | transparent_lighting-replay-3 | 48.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.01e-06, strictImageFailures=323 |
| transparent-m6 | transparent_lighting-replay-4 | 48.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.75e-06, strictImageFailures=435 |
| transparent-m6 | transparent_lighting-replay-5 | 49.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.75e-07, strictImageFailures=4 |
| transparent-m6 | transparent_lighting-replay-6 | 48.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.56e-06, strictImageFailures=299 |
| transparent-m6 | transparent_lighting-replay-7 | 48.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.79e-06, strictImageFailures=519 |
| transparent-m6 | transparent-shadow-replay-direct | 5.8 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m6 | transparent-shadow-replay-indexed | 5.8 | PASS | source capture CPU film | maximumAbsoluteImageError=7.45e-09, strictImageFailures=0 |
| terminator-m6 | shadow_terminator | 148.9 | PASS | glibc, device-scene | maximumAbsoluteError=1.53e-05; host re-judged vs glibc PASS: practical=0 strict=97 max=1.53e-05 |
| terminator-m6 | terminator-replay-0 | 31.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m6 | terminator-replay-1 | 39.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m6 | terminator-replay-2 | 39.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m6 | terminator-replay-3 | 39.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m6 | terminator-replay-4 | 66.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=22 |
| terminator-m6 | terminator-replay-5 | 66.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=18 |
| terminator-m6 | terminator-replay-6 | 67.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=18 |
| terminator-m6 | terminator-replay-7 | 66.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=22 |
| terminator-m6 | terminator-replay-8 | 67.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.38e-07, strictImageFailures=5 |
| terminator-m6 | terminator-replay-9 | 79.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.1e-06, strictImageFailures=12 |
| spec-refusal-m6 | glass_render-spec | 59.0 | PASS | glibc, device-scene | strictImageFailures=104, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=104 max=9.54e-07 |
| spec-refusal-m6 | glass_render-nospec-refused | 0.3 | PASS | expected refusal |  |
| spec-refusal-m6 | glass_render-superset-refused | 0.3 | PASS | expected refusal |  |
| hostcap-linux-triangle | tri0 | 39.6 | PASS | source capture CPU film | maximumAbsoluteImageError=4.77e-07, strictImageFailures=9 |
| hostcap-linux-triangle | tri1 | 47.2 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| hostcap-linux-triangle | tri2 | 47.6 | PASS | source capture CPU film | maximumAbsoluteImageError=6.41e-07, strictImageFailures=14 |
| hostcap-linux-triangle | tri3 | 47.8 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-06, strictImageFailures=430 |
| hostcap-linux-triangle | tri4 | 47.4 | PASS | source capture CPU film | maximumAbsoluteImageError=3.73e-07, strictImageFailures=10 |
| hostcap-linux-triangle | tri5 | 66.5 | PASS | source capture CPU film | maximumAbsoluteImageError=2.26e-06, strictImageFailures=425 |
| hostcap-linux-terminator | term0 | 33.3 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term1 | 40.8 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term2 | 40.7 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term3 | 41.0 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term4 | 68.2 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=22 |
| hostcap-linux-terminator | term5 | 68.0 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=18 |
| hostcap-linux-terminator | term6 | 68.5 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=18 |
| hostcap-linux-terminator | term7 | 68.4 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=22 |
| hostcap-linux-terminator | term8 | 69.5 | PASS | source capture CPU film | maximumAbsoluteImageError=2.38e-07, strictImageFailures=5 |
| hostcap-linux-terminator | term9 | 82.9 | PASS | source capture CPU film | maximumAbsoluteImageError=1.07e-06, strictImageFailures=17 |
| arm-m6 | microfacet_render | 58.8 | PASS | glibc, device-scene | strictImageFailures=119, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=119 max=1.91e-06 |
| motion-m6 | render-static | 44.3 | PASS | glibc, device-scene | strictImageFailures=406, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=406 max=7.63e-06 |
| motion-m6 | render-motion-0 | 47.7 | PASS | glibc, device-scene | strictImageFailures=422, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=422 max=2.53e-05 |
| motion-m6 | render-motion-1 | 47.9 | PASS | glibc, device-scene | strictImageFailures=386, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=386 max=1.14e-05 |
| motion-m6 | render-motion-2 | 47.5 | PASS | glibc, device-scene | strictImageFailures=601, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=601 max=0.000188 |
| motion-m6 | render-motion-3 | 48.1 | PASS | glibc, device-scene | strictImageFailures=792, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=792 max=3.71e-05 |
| motion-m6 | render-motion-4 | 46.7 | PASS | glibc, device-scene | strictImageFailures=660, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=660 max=4.48e-05 |
| motion-m6 | render-motion-5 | 46.9 | PASS | glibc, device-scene | strictImageFailures=647, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=647 max=0.000135 |
| motion-m6 | render-motion-6 | 46.0 | PASS | glibc, device-scene | strictImageFailures=761, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=761 max=2.86e-05 |
| motion-m6 | render-motion-7 | 46.6 | PASS | glibc, device-scene | strictImageFailures=661, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=661 max=4.48e-05 |
| volume-m6 | volume-state | 11.8 | PASS | device's own CPU / gate |  |
| volume-m6 | volume-absorption | 59.6 | PASS | glibc, device-scene | strictImageFailures=1143, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1143 max=1.53e-05 |
| volume-m6 | volume-isotropic | 73.7 | PASS | glibc, device-scene | strictImageFailures=1611, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1611 max=3.24e-05 |
| volume-m6 | volume-forward | 75.1 | PASS | glibc, device-scene | strictImageFailures=1669, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1669 max=4.67e-05 |
| volume-m6 | volume-backward | 75.7 | PASS | glibc, device-scene | strictImageFailures=1617, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1617 max=4.01e-05 |
| volume-m6 | boundary-state | 45.9 | PASS | device's own CPU / gate |  |
| volume-m6 | boundary-compaction-guard | 6.9 | PASS | device's own CPU / gate |  |
| volume-m6 | closed-single | 89.2 | PASS | glibc, device-scene | strictImageFailures=976, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=976 max=3.05e-05 |
| volume-m6 | closed-overlap | 143.0 | PASS | glibc, device-scene | strictImageFailures=1260, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1260 max=4.58e-05 |
| volume-m6 | closed-inside | 141.1 | PASS | glibc, device-scene | strictImageFailures=2284, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=2284 max=2.29e-05 |
| volume-m6 | volume-replay-absorption | 25.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=1143 |
| volume-m6 | volume-replay-isotropic | 26.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.24e-05, strictImageFailures=1611 |
| volume-m6 | volume-replay-forward | 26.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.67e-05, strictImageFailures=1669 |
| volume-m6 | volume-replay-backward | 26.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.01e-05, strictImageFailures=1617 |
| volume-m6 | closed-replay-single | 27.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=976 |
| volume-m6 | closed-replay-overlap | 52.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.58e-05, strictImageFailures=1260 |
| volume-m6 | closed-replay-inside | 53.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.29e-05, strictImageFailures=2284 |

### s20

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| materials-s20-principled_svm | principled_svm | 418.3 | PASS | glibc, device-scene | strictFloatFailures=557, maximumAbsoluteError=3.59e-06 |
| a650-refusals-m6 | nodes_world-refused | 0.5 | PASS | expected refusal |  |
| a650-refusals-m6 | nodes_svm-refused | 0.4 | PASS | expected refusal |  |
| a650-refusals-m6 | nodes_render-refused | 0.5 | PASS | expected refusal |  |
| triangle-s20-triangle_sample | triangle_sample | 79.4 | PASS | glibc, device-scene | maximumAbsoluteError=0.000488; host re-judged vs glibc PASS: practical=0 strict=865 max=0.000488 |
| regression-s20-glass_render | glass_render | 84.6 | PASS | glibc, device-scene | strictImageFailures=64, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=64 max=1.91e-06 |
| regression-s20-lighting_mis | lighting_mis | 63.7 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1057 max=6.08e-06 |
| triangle-s20-surface | surface | 45.4 | PASS | device's own CPU / gate | strictFloatFailures=1244, maximumAbsoluteError=3.81e-06 |
| transparent-s20-transparent_lighting | transparent_lighting | 193.5 | PASS | glibc, device-scene | maximumAbsoluteError=6.02e-06; host re-judged vs glibc PASS: practical=0 strict=683 max=6.02e-06 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-0 | 50.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-1 | 51.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-2 | 61.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.81e-06, strictImageFailures=72 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-3 | 61.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.55e-06, strictImageFailures=133 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-4 | 62.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=167 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-5 | 63.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.9e-07, strictImageFailures=3 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-6 | 61.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=103 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-7 | 62.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=205 |
| transparent-s20-transparent_shadow | transparent_shadow | 15.5 | PASS | device's own CPU / gate | maximumAbsoluteError=1.19e-07 |
| transparent-s20-transparent_shadow | transparent-shadow-replay-direct | 11.1 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-s20-transparent_shadow | transparent-shadow-replay-indexed | 11.2 | PASS | source capture CPU film | maximumAbsoluteImageError=0, strictImageFailures=0 |
| materials-s20-principled_render | principled_render | 207.5 | PASS | glibc, device-scene | strictImageFailures=64, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=64 max=6.44e-06 |
| materials-s20-principled_render | principled-replay | 168.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.44e-06, strictImageFailures=64 |
| pixel-motion-s20-object-moving-images | object-moving-images | 140.4 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1276 max=6.08e-06 |
| pixel-motion-s20-object-moving-images | object-replay-motion-0 | 120.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.56e-06, strictImageFailures=12 |
| pixel-motion-s20-object-moving-images | object-replay-motion-1 | 120.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=12 |
| pixel-motion-s20-object-moving-images | object-replay-motion-2 | 121.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.52e-06, strictImageFailures=301 |
| pixel-motion-s20-object-moving-images | object-replay-motion-3 | 121.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.31e-09, strictImageFailures=0 |
| pixel-motion-s20-object-moving-images | object-replay-motion-4 | 131.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=294 |
| pixel-motion-s20-object-moving-images | object-replay-motion-5 | 177.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.05e-06, strictImageFailures=70 |
| pixel-motion-s20-object-moving-images | object-replay-motion-6 | 173.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=338 |
| pixel-motion-s20-object-moving-images | object-replay-motion-7 | 180.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=249 |
| pixel-motion-s20-deforming-moving-images | deforming-moving-images | 322.8 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1213 max=6.08e-06 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-0 | 120.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.38e-07, strictImageFailures=2 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-1 | 176.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-07, strictImageFailures=19 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-2 | 176.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.4e-06, strictImageFailures=304 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-3 | 176.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.31e-09, strictImageFailures=0 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-4 | 153.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=280 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-5 | 125.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.17e-06, strictImageFailures=49 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-6 | 123.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=300 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-7 | 123.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=259 |
| materials-s20-principled_bounce | principled_bounce | 336.0 | PASS | device's own CPU / gate | strictFloatFailures=2796, maximumAbsoluteError=0.125 |
