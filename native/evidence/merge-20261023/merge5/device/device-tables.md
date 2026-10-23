
### samsung

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| materials-m5 | principled_svm | 119.0 | PASS | glibc, device-scene | strictFloatFailures=634, maximumAbsoluteError=3.61e-06 |
| materials-m5 | principled_bounce | 85.0 | PASS | device's own CPU / gate | strictFloatFailures=3466, maximumAbsoluteError=0.125 |
| materials-m5 | principled_render | 81.4 | FAIL: Rendered repeat=0 sample=6 stages=127 Rendered repeat=0 sample=7 stages=145 Independent CPU image 9114 actual=1.546950 expected=1.545995 | glibc, device-scene | ; host re-judged vs glibc FAIL: practical=2 strict=267 max=0.000954 |
| materials-m5 | nodes_svm | 1500.5 | PASS | glibc, device-scene | strictFloatFailures=18450, maximumAbsoluteError=0.00195 |
| materials-m5 | nodes_world | 495.3 | PASS | device's own CPU / gate | strictFloatFailures=16242, maximumAbsoluteError=6.1e-05 |
| materials-m5 | nodes_render | 133.2 | FAIL: Rendered repeat=0 sample=6 stages=127 Rendered repeat=0 sample=7 stages=144 Independent CPU image 6720 actual=0.928481 expected=0.928078 | glibc, device-scene | ; host re-judged vs glibc FAIL: practical=1 strict=3998 max=0.000403 |
| materials-m5 | principled-replay | - | not run (source job failed) | device's own CPU / gate |  |
| materials-m5 | nodes-replay | - | not run (source job failed) | device's own CPU / gate |  |
| triangle-m5 | triangle_sample | 20.9 | FAIL: lorIO Info]: Color management disabled. (Specify the $OCIO environment variable to enable.) Emitter sample=3133 field=2 got=0.382186 expected=0.379992 | glibc, device-scene | ; host re-judged vs glibc FAIL: practical=2 strict=1069 max=0.00219 |
| triangle-m5 | surface | 24.1 | PASS | device's own CPU / gate | strictFloatFailures=1246, maximumAbsoluteError=3.81e-06 |
| triangle-m5 | triangle_lighting | 78.6 | PASS | glibc, device-scene | maximumAbsoluteError=1.79e-06; host re-judged vs glibc PASS: practical=0 strict=519 max=1.79e-06 |
| triangle-m5 | replay-0 | 15.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=12 |
| triangle-m5 | replay-1 | 20.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=5 |
| triangle-m5 | replay-2 | 21.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.41e-07, strictImageFailures=8 |
| triangle-m5 | replay-3 | 22.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-06, strictImageFailures=235 |
| triangle-m5 | replay-4 | 21.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.73e-07, strictImageFailures=11 |
| triangle-m5 | replay-5 | 35.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-06, strictImageFailures=248 |
| emitter-m5 | emitter_sample | 7.7 | PASS | glibc, device-scene | maximumAbsoluteError=9.06e-06; host re-judged vs glibc PASS: practical=0 strict=135 max=9.06e-06 |
| lighting-m5 | lighting_mis | 29.4 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1070 max=6.08e-06 |
| lighting-m5 | broad_lighting | 56.5 | PASS | glibc, device-scene | maximumAbsoluteError=3.81e-05; host re-judged vs glibc PASS: practical=0 strict=993 max=3.81e-05 |
| lighting-m5 | lighting_mis-replay-0 | 16.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m5 | lighting_mis-replay-1 | 17.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m5 | broad_lighting-replay-0 | 16.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m5 | broad_lighting-replay-1 | 17.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.17e-07, strictImageFailures=15 |
| regression-m5 | glass_render | 63.1 | PASS | glibc, device-scene | strictImageFailures=97, strictStageFailures=1087; host re-judged vs glibc PASS: practical=0 strict=97 max=1.91e-06 |
| regression-m5 | lighting_mis | 29.9 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1070 max=6.08e-06 |
| regression-m5 | transparent_lighting | 90.8 | PASS | glibc, device-scene | maximumAbsoluteError=6.02e-06; host re-judged vs glibc PASS: practical=0 strict=693 max=6.02e-06 |
| transparent-m5 | transparent_shadow | 18.1 | PASS | device's own CPU / gate | maximumAbsoluteError=1.19e-07 |
| transparent-m5 | transparent_lighting | 89.6 | PASS | glibc, device-scene | maximumAbsoluteError=6.02e-06; host re-judged vs glibc PASS: practical=0 strict=693 max=6.02e-06 |
| transparent-m5 | transparent_lighting-replay-0 | 19.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m5 | transparent_lighting-replay-1 | 19.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m5 | transparent_lighting-replay-2 | 25.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.81e-06, strictImageFailures=77 |
| transparent-m5 | transparent_lighting-replay-3 | 25.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.29e-06, strictImageFailures=134 |
| transparent-m5 | transparent_lighting-replay-4 | 25.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=168 |
| transparent-m5 | transparent_lighting-replay-5 | 26.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.9e-07, strictImageFailures=3 |
| transparent-m5 | transparent_lighting-replay-6 | 24.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=103 |
| transparent-m5 | transparent_lighting-replay-7 | 25.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=208 |
| transparent-m5 | transparent-shadow-replay-direct | 6.3 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m5 | transparent-shadow-replay-indexed | 6.4 | PASS | source capture CPU film | maximumAbsoluteImageError=0, strictImageFailures=0 |
| terminator-m5 | shadow_terminator | 96.0 | PASS | glibc, device-scene | maximumAbsoluteError=6.2e-05; host re-judged vs glibc PASS: practical=0 strict=124 max=6.2e-05 |
| terminator-m5 | terminator-replay-0 | 12.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m5 | terminator-replay-1 | 14.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m5 | terminator-replay-2 | 14.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m5 | terminator-replay-3 | 14.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m5 | terminator-replay-4 | 24.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| terminator-m5 | terminator-replay-5 | 24.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| terminator-m5 | terminator-replay-6 | 24.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| terminator-m5 | terminator-replay-7 | 24.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| terminator-m5 | terminator-replay-8 | 24.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.93e-05, strictImageFailures=8 |
| terminator-m5 | terminator-replay-9 | 33.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.91e-05, strictImageFailures=24 |
| spec-refusal-m5 | glass_render-spec | 62.8 | PASS | glibc, device-scene | strictImageFailures=97, strictStageFailures=1087; host re-judged vs glibc PASS: practical=0 strict=97 max=1.91e-06 |
| spec-refusal-m5 | glass_render-nospec-refused | 0.4 | PASS | expected refusal |  |
| spec-refusal-m5 | glass_render-superset-refused | 0.2 | PASS | expected refusal |  |
| hostcap-linux-triangle | tri0 | 15.4 | PASS | source capture CPU film | maximumAbsoluteImageError=4.77e-07, strictImageFailures=12 |
| hostcap-linux-triangle | tri1 | 20.6 | PASS | source capture CPU film | maximumAbsoluteImageError=1.49e-07, strictImageFailures=1 |
| hostcap-linux-triangle | tri2 | 21.1 | PASS | source capture CPU film | maximumAbsoluteImageError=6.41e-07, strictImageFailures=14 |
| hostcap-linux-triangle | tri3 | 21.7 | PASS | source capture CPU film | maximumAbsoluteImageError=1.76e-06, strictImageFailures=236 |
| hostcap-linux-triangle | tri4 | 21.4 | PASS | source capture CPU film | maximumAbsoluteImageError=3.73e-07, strictImageFailures=8 |
| hostcap-linux-triangle | tri5 | 34.7 | PASS | source capture CPU film | maximumAbsoluteImageError=2.38e-06, strictImageFailures=258 |
| hostcap-linux-terminator | term0 | 12.2 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term1 | 14.4 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term2 | 14.7 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term3 | 14.8 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term4 | 24.7 | PASS | source capture CPU film | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| hostcap-linux-terminator | term5 | 24.9 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| hostcap-linux-terminator | term6 | 25.0 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| hostcap-linux-terminator | term7 | 25.1 | PASS | source capture CPU film | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| hostcap-linux-terminator | term8 | 25.5 | PASS | source capture CPU film | maximumAbsoluteImageError=2.93e-05, strictImageFailures=8 |
| hostcap-linux-terminator | term9 | 33.6 | PASS | source capture CPU film | maximumAbsoluteImageError=2.93e-05, strictImageFailures=22 |
| pixel-motion-own | object-moving-images | 45.2 | PASS | device's own CPU / gate | maximumAbsoluteError=6.08e-06 |
| pixel-motion-own | object-stationary-images | 36.0 | PASS | device's own CPU / gate | maximumAbsoluteError=6.08e-06 |
| pixel-motion-own | object-replay-motion-0 | 30.0 | PASS | source capture CPU film | maximumAbsoluteImageError=3.58e-07, strictImageFailures=10 |
| pixel-motion-own | object-replay-motion-1 | 31.4 | PASS | source capture CPU film | maximumAbsoluteImageError=2.86e-06, strictImageFailures=26 |
| pixel-motion-own | object-replay-motion-2 | 31.4 | PASS | source capture CPU film | maximumAbsoluteImageError=2.4e-06, strictImageFailures=290 |
| pixel-motion-own | object-replay-motion-3 | 32.7 | PASS | source capture CPU film | maximumAbsoluteImageError=1.02e-07, strictImageFailures=1 |
| pixel-motion-own | object-replay-motion-4 | 31.9 | PASS | source capture CPU film | maximumAbsoluteImageError=6.08e-06, strictImageFailures=295 |
| pixel-motion-own | object-replay-motion-5 | 31.5 | PASS | source capture CPU film | maximumAbsoluteImageError=4.53e-06, strictImageFailures=59 |
| pixel-motion-own | object-replay-motion-6 | 31.9 | PASS | source capture CPU film | maximumAbsoluteImageError=6.08e-06, strictImageFailures=342 |
| pixel-motion-own | object-replay-motion-7 | 31.6 | PASS | source capture CPU film | maximumAbsoluteImageError=6.02e-06, strictImageFailures=246 |
| pixel-motion-x64 | object-moving-images | 45.8 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1306 max=6.08e-06 |
| pixel-motion-x64 | object-stationary-images | 36.9 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1157 max=6.08e-06 |
| pixel-motion-x64 | object-replay-motion-0 | 30.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.68e-06, strictImageFailures=18 |
| pixel-motion-x64 | object-replay-motion-1 | 32.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=16 |
| pixel-motion-x64 | object-replay-motion-2 | 32.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.52e-06, strictImageFailures=303 |
| pixel-motion-x64 | object-replay-motion-3 | 32.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.08e-09, strictImageFailures=0 |
| pixel-motion-x64 | object-replay-motion-4 | 32.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=302 |
| pixel-motion-x64 | object-replay-motion-5 | 32.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.29e-06, strictImageFailures=72 |
| pixel-motion-x64 | object-replay-motion-6 | 32.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=344 |
| pixel-motion-x64 | object-replay-motion-7 | 32.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=251 |

### hp

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| materials-m5 | principled_svm | 168.7 | PASS | glibc, device-scene | strictFloatFailures=946, maximumAbsoluteError=9.54e-07 |
| materials-m5 | principled_bounce | 236.3 | PASS | device's own CPU / gate | strictFloatFailures=5234, maximumAbsoluteError=0.125 |
| materials-m5 | principled_render | 152.1 | PASS | glibc, device-scene | strictImageFailures=92, strictStageFailures=926; host re-judged vs glibc PASS: practical=0 strict=92 max=6.44e-06 |
| materials-m5 | nodes_svm | 812.7 | PASS | glibc, device-scene | strictFloatFailures=10978, maximumAbsoluteError=0.0107 |
| materials-m5 | nodes_world | 258.0 | PASS | device's own CPU / gate | strictFloatFailures=9125, maximumAbsoluteError=6.1e-05 |
| materials-m5 | nodes_render | 105.9 | PASS | glibc, device-scene | strictImageFailures=1843, strictStageFailures=3987; host re-judged vs glibc PASS: practical=0 strict=1843 max=0.000103 |
| materials-m5 | principled-replay | 120.7 | PASS | source capture CPU film | maximumAbsoluteImageError=6.44e-06, strictImageFailures=94 |
| materials-m5 | nodes-replay | 73.6 | PASS | source capture CPU film | maximumAbsoluteImageError=0.000103, strictImageFailures=1849 |
| triangle-m5 | triangle_sample | 22.9 | PASS | glibc, device-scene | maximumAbsoluteError=0.000488; host re-judged vs glibc PASS: practical=0 strict=865 max=0.000488 |
| triangle-m5 | surface | 39.3 | PASS | device's own CPU / gate | strictFloatFailures=1810, maximumAbsoluteError=3.81e-06 |
| triangle-m5 | triangle_lighting | 88.3 | PASS | glibc, device-scene | maximumAbsoluteError=2.32e-06; host re-judged vs glibc PASS: practical=0 strict=887 max=2.32e-06 |
| triangle-m5 | replay-0 | 37.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=9 |
| triangle-m5 | replay-1 | 45.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| triangle-m5 | replay-2 | 45.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.41e-07, strictImageFailures=14 |
| triangle-m5 | replay-3 | 46.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-06, strictImageFailures=427 |
| triangle-m5 | replay-4 | 45.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.73e-07, strictImageFailures=10 |
| triangle-m5 | replay-5 | 64.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.26e-06, strictImageFailures=427 |
| emitter-m5 | emitter_sample | 19.8 | PASS | glibc, device-scene | maximumAbsoluteError=0.00513; host re-judged vs glibc PASS: practical=0 strict=235 max=0.00513 |
| lighting-m5 | lighting_mis | 47.7 | PASS | glibc, device-scene | maximumAbsoluteError=8.34e-06; host re-judged vs glibc PASS: practical=0 strict=2681 max=8.34e-06 |
| lighting-m5 | broad_lighting | 69.4 | PASS | glibc, device-scene | maximumAbsoluteError=5.34e-05; host re-judged vs glibc PASS: practical=0 strict=1740 max=5.34e-05 |
| lighting-m5 | lighting_mis-replay-0 | 37.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=3 |
| lighting-m5 | lighting_mis-replay-1 | 38.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=11 |
| lighting-m5 | broad_lighting-replay-0 | 39.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=1 |
| lighting-m5 | broad_lighting-replay-1 | 38.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.07e-07, strictImageFailures=26 |
| regression-m5 | glass_render | 70.1 | PASS | glibc, device-scene | strictImageFailures=104, strictStageFailures=1155; host re-judged vs glibc PASS: practical=0 strict=104 max=9.54e-07 |
| regression-m5 | lighting_mis | 49.9 | PASS | glibc, device-scene | maximumAbsoluteError=8.34e-06; host re-judged vs glibc PASS: practical=0 strict=2681 max=8.34e-06 |
| regression-m5 | transparent_lighting | 77.6 | PASS | glibc, device-scene | maximumAbsoluteError=7.75e-06; host re-judged vs glibc PASS: practical=0 strict=1682 max=7.75e-06 |
| transparent-m5 | transparent_shadow | 12.7 | PASS | device's own CPU / gate | maximumAbsoluteError=1.19e-07 |
| transparent-m5 | transparent_lighting | 78.8 | PASS | glibc, device-scene | maximumAbsoluteError=7.75e-06; host re-judged vs glibc PASS: practical=0 strict=1682 max=7.75e-06 |
| transparent-m5 | transparent_lighting-replay-0 | 42.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.49e-07, strictImageFailures=1 |
| transparent-m5 | transparent_lighting-replay-1 | 42.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=8 |
| transparent-m5 | transparent_lighting-replay-2 | 48.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=93 |
| transparent-m5 | transparent_lighting-replay-3 | 48.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.01e-06, strictImageFailures=323 |
| transparent-m5 | transparent_lighting-replay-4 | 49.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.75e-06, strictImageFailures=435 |
| transparent-m5 | transparent_lighting-replay-5 | 49.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.75e-07, strictImageFailures=4 |
| transparent-m5 | transparent_lighting-replay-6 | 48.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.56e-06, strictImageFailures=299 |
| transparent-m5 | transparent_lighting-replay-7 | 49.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.79e-06, strictImageFailures=519 |
| transparent-m5 | transparent-shadow-replay-direct | 5.9 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m5 | transparent-shadow-replay-indexed | 5.9 | PASS | source capture CPU film | maximumAbsoluteImageError=7.45e-09, strictImageFailures=0 |
| terminator-m5 | shadow_terminator | 151.5 | PASS | glibc, device-scene | maximumAbsoluteError=1.53e-05; host re-judged vs glibc PASS: practical=0 strict=97 max=1.53e-05 |
| terminator-m5 | terminator-replay-0 | 31.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m5 | terminator-replay-1 | 40.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m5 | terminator-replay-2 | 40.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m5 | terminator-replay-3 | 40.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m5 | terminator-replay-4 | 67.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=22 |
| terminator-m5 | terminator-replay-5 | 67.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=18 |
| terminator-m5 | terminator-replay-6 | 67.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=18 |
| terminator-m5 | terminator-replay-7 | 67.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=22 |
| terminator-m5 | terminator-replay-8 | 67.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.38e-07, strictImageFailures=5 |
| terminator-m5 | terminator-replay-9 | 80.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.1e-06, strictImageFailures=12 |
| spec-refusal-m5 | glass_render-spec | 70.2 | PASS | glibc, device-scene | strictImageFailures=104, strictStageFailures=1155; host re-judged vs glibc PASS: practical=0 strict=104 max=9.54e-07 |
| spec-refusal-m5 | glass_render-nospec-refused | 0.3 | PASS | expected refusal |  |
| spec-refusal-m5 | glass_render-superset-refused | 0.2 | PASS | expected refusal |  |
| hostcap-linux-triangle | tri0 | 38.6 | PASS | source capture CPU film | maximumAbsoluteImageError=4.77e-07, strictImageFailures=9 |
| hostcap-linux-triangle | tri1 | 47.7 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| hostcap-linux-triangle | tri2 | 48.0 | PASS | source capture CPU film | maximumAbsoluteImageError=6.41e-07, strictImageFailures=14 |
| hostcap-linux-triangle | tri3 | 47.8 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-06, strictImageFailures=430 |
| hostcap-linux-triangle | tri4 | 48.3 | PASS | source capture CPU film | maximumAbsoluteImageError=3.73e-07, strictImageFailures=10 |
| hostcap-linux-triangle | tri5 | 66.5 | PASS | source capture CPU film | maximumAbsoluteImageError=2.26e-06, strictImageFailures=425 |
| hostcap-linux-terminator | term0 | 33.1 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term1 | 41.3 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term2 | 40.8 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term3 | 41.0 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term4 | 68.7 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=22 |
| hostcap-linux-terminator | term5 | 68.9 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=18 |
| hostcap-linux-terminator | term6 | 68.5 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=18 |
| hostcap-linux-terminator | term7 | 69.5 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=22 |
| hostcap-linux-terminator | term8 | 70.0 | PASS | source capture CPU film | maximumAbsoluteImageError=2.38e-07, strictImageFailures=5 |
| hostcap-linux-terminator | term9 | 83.1 | PASS | source capture CPU film | maximumAbsoluteImageError=1.07e-06, strictImageFailures=17 |

### s20

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| a650-refusals-m5 | nodes_world-refused | 0.7 | PASS | expected refusal |  |
| a650-refusals-m5 | nodes_svm-refused | 0.4 | PASS | expected refusal |  |
| a650-refusals-m5 | nodes_render-refused | 0.6 | PASS | expected refusal |  |
| triangle-s20-triangle_sample | triangle_sample | 78.5 | PASS | glibc, device-scene | maximumAbsoluteError=0.000488; host re-judged vs glibc PASS: practical=0 strict=865 max=0.000488 |
| regression-s20-glass_render | glass_render | 95.8 | PASS | glibc, device-scene | strictImageFailures=100, strictStageFailures=1079; host re-judged vs glibc PASS: practical=0 strict=100 max=1.91e-06 |
| regression-s20-lighting_mis | lighting_mis | 63.2 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1057 max=6.08e-06 |
| triangle-s20-surface | surface | 45.2 | PASS | device's own CPU / gate | strictFloatFailures=1244, maximumAbsoluteError=3.81e-06 |
| transparent-s20-transparent_lighting | transparent_lighting | 193.7 | PASS | glibc, device-scene | maximumAbsoluteError=6.02e-06; host re-judged vs glibc PASS: practical=0 strict=683 max=6.02e-06 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-0 | 50.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-1 | 50.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-2 | 60.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.81e-06, strictImageFailures=72 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-3 | 61.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.55e-06, strictImageFailures=133 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-4 | 62.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=167 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-5 | 62.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.9e-07, strictImageFailures=3 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-6 | 61.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=103 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-7 | 61.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=205 |
| transparent-s20-transparent_shadow | transparent_shadow | 16.4 | PASS | device's own CPU / gate | maximumAbsoluteError=1.19e-07 |
| transparent-s20-transparent_shadow | transparent-shadow-replay-direct | 11.1 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-s20-transparent_shadow | transparent-shadow-replay-indexed | 11.0 | PASS | source capture CPU film | maximumAbsoluteImageError=0, strictImageFailures=0 |
| materials-s20-principled_render | principled_render | 213.6 | FAIL: Rendered repeat=0 sample=6 stages=127 Rendered repeat=0 sample=7 stages=145 Independent CPU image 9114 actual=1.546950 expected=1.545995 | glibc, device-scene | ; host re-judged vs glibc FAIL: practical=2 strict=262 max=0.000954 |
| materials-s20-principled_render | principled-replay | - | not run (source job failed) | device's own CPU / gate |  |
| materials-s20-principled_svm | principled_svm | 85.2 | FAIL: r management disabled. (Specify the $OCIO environment variable to enable.) Native compiled graphs SVM error mask=8 first incomplete path=119 shader=12 | glibc, device-scene |  |
| pixel-motion-s20-object-moving-images | object-moving-images | 139.9 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1276 max=6.08e-06 |
| pixel-motion-s20-object-moving-images | object-replay-motion-0 | 117.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.56e-06, strictImageFailures=12 |
| pixel-motion-s20-object-moving-images | object-replay-motion-1 | 120.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=12 |
| pixel-motion-s20-object-moving-images | object-replay-motion-2 | 119.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.52e-06, strictImageFailures=301 |
| pixel-motion-s20-object-moving-images | object-replay-motion-3 | 118.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.31e-09, strictImageFailures=0 |
| pixel-motion-s20-object-moving-images | object-replay-motion-4 | 120.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=294 |
| pixel-motion-s20-object-moving-images | object-replay-motion-5 | 120.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.05e-06, strictImageFailures=70 |
| pixel-motion-s20-object-moving-images | object-replay-motion-6 | 122.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=338 |
| pixel-motion-s20-object-moving-images | object-replay-motion-7 | 160.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=249 |
| pixel-motion-s20-deforming-moving-images | deforming-moving-images | 315.3 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1213 max=6.08e-06 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-0 | 85.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.38e-07, strictImageFailures=2 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-1 | 119.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-07, strictImageFailures=19 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-2 | 119.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.4e-06, strictImageFailures=304 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-3 | 119.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.31e-09, strictImageFailures=0 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-4 | 120.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=280 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-5 | 119.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.17e-06, strictImageFailures=49 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-6 | 129.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=300 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-7 | 170.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=259 |
| materials-s20-principled_bounce | principled_bounce | 455.2 | PASS | device's own CPU / gate | strictFloatFailures=2796, maximumAbsoluteError=0.125 |
