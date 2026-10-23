
### samsung

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| materials-m4 | principled_svm | 111.7 | PASS | glibc, device-scene | strictFloatFailures=634, maximumAbsoluteError=3.61e-06 |
| materials-m4 | principled_bounce | 305.1 | PASS | device's own CPU / gate | strictFloatFailures=3964, maximumAbsoluteError=0.125 |
| materials-m4 | principled_render | 49.2 | FAIL: .8700200915,-0.3449651897,-0.3522275686 rayBitsIdentical=0 tmin=0 bounce=2 stage 40 path.mis_ray_pdf path=2441 actual=1751.469238 expected=1747.274414 | glibc, device-scene | ; host re-judged vs glibc FAIL: practical=0 strict=0 max=0 |
| materials-m4 | nodes_svm | 153.8 | FAIL: gnostic a=3e910701/3e910700 b=3df97273/3df97271 Native compiled graphs float closures[0].weight path 309 component 0 actual=0.070653 expected=0.079591 | glibc, device-scene |  |
| materials-m4 | nodes_world | 483.9 | PASS | device's own CPU / gate | strictFloatFailures=16242, maximumAbsoluteError=6.1e-05 |
| materials-m4 | nodes_render | 128.7 | FAIL: Rendered repeat=0 sample=6 stages=127 Rendered repeat=0 sample=7 stages=144 Independent CPU image 6720 actual=0.928481 expected=0.928078 | glibc, device-scene | ; host re-judged vs glibc FAIL: practical=1 strict=3998 max=0.000403 |
| materials-m4 | principled-replay | - | not run (source job failed) | device's own CPU / gate |  |
| materials-m4 | nodes-replay | - | not run (source job failed) | device's own CPU / gate |  |
| triangle-m4 | triangle_sample | 21.1 | FAIL: lorIO Info]: Color management disabled. (Specify the $OCIO environment variable to enable.) Emitter sample=3133 field=2 got=0.382186 expected=0.379992 | glibc, device-scene | ; host re-judged vs glibc FAIL: practical=2 strict=1069 max=0.00219 |
| triangle-m4 | surface | 71.8 | PASS | device's own CPU / gate | strictFloatFailures=1246, maximumAbsoluteError=3.81e-06 |
| triangle-m4 | triangle_lighting | 78.3 | PASS | glibc, device-scene | maximumAbsoluteError=1.79e-06; host re-judged vs glibc PASS: practical=0 strict=519 max=1.79e-06 |
| triangle-m4 | replay-0 | 15.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=12 |
| triangle-m4 | replay-1 | 21.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=5 |
| triangle-m4 | replay-2 | 20.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.41e-07, strictImageFailures=8 |
| triangle-m4 | replay-3 | 21.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-06, strictImageFailures=235 |
| triangle-m4 | replay-4 | 21.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.73e-07, strictImageFailures=11 |
| triangle-m4 | replay-5 | 34.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-06, strictImageFailures=248 |
| emitter-m4 | emitter_sample | 7.6 | PASS | glibc, device-scene | maximumAbsoluteError=9.06e-06; host re-judged vs glibc PASS: practical=0 strict=135 max=9.06e-06 |
| lighting-m4 | lighting_mis | 29.3 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1070 max=6.08e-06 |
| lighting-m4 | broad_lighting | 56.7 | PASS | glibc, device-scene | maximumAbsoluteError=3.81e-05; host re-judged vs glibc PASS: practical=0 strict=991 max=3.81e-05 |
| lighting-m4 | lighting_mis-replay-0 | 16.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m4 | lighting_mis-replay-1 | 16.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m4 | broad_lighting-replay-0 | 16.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m4 | broad_lighting-replay-1 | 17.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.17e-07, strictImageFailures=15 |
| regression-m4 | glass_render | 61.7 | PASS | glibc, device-scene | strictImageFailures=97, strictStageFailures=1087; host re-judged vs glibc PASS: practical=0 strict=97 max=1.91e-06 |
| regression-m4 | lighting_mis | 29.1 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1070 max=6.08e-06 |
| regression-m4 | transparent_lighting | 91.4 | PASS | glibc, device-scene | maximumAbsoluteError=6.02e-06; host re-judged vs glibc PASS: practical=0 strict=693 max=6.02e-06 |
| transparent-m4 | transparent_shadow | 17.3 | PASS | device's own CPU / gate | maximumAbsoluteError=1.19e-07 |
| transparent-m4 | transparent_lighting | 89.5 | PASS | glibc, device-scene | maximumAbsoluteError=6.02e-06; host re-judged vs glibc PASS: practical=0 strict=693 max=6.02e-06 |
| transparent-m4 | transparent_lighting-replay-0 | 19.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m4 | transparent_lighting-replay-1 | 20.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m4 | transparent_lighting-replay-2 | 25.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.81e-06, strictImageFailures=77 |
| transparent-m4 | transparent_lighting-replay-3 | 26.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.29e-06, strictImageFailures=134 |
| transparent-m4 | transparent_lighting-replay-4 | 26.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=168 |
| transparent-m4 | transparent_lighting-replay-5 | 27.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.9e-07, strictImageFailures=3 |
| transparent-m4 | transparent_lighting-replay-6 | 25.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=103 |
| transparent-m4 | transparent_lighting-replay-7 | 26.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=208 |
| transparent-m4 | transparent-shadow-replay-direct | 5.8 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m4 | transparent-shadow-replay-indexed | 5.8 | PASS | source capture CPU film | maximumAbsoluteImageError=0, strictImageFailures=0 |
| terminator-m4 | shadow_terminator | 97.6 | PASS | glibc, device-scene | maximumAbsoluteError=6.2e-05; host re-judged vs glibc PASS: practical=0 strict=124 max=6.2e-05 |
| terminator-m4 | terminator-replay-0 | 12.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m4 | terminator-replay-1 | 14.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m4 | terminator-replay-2 | 14.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m4 | terminator-replay-3 | 14.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m4 | terminator-replay-4 | 24.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| terminator-m4 | terminator-replay-5 | 24.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| terminator-m4 | terminator-replay-6 | 25.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| terminator-m4 | terminator-replay-7 | 24.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| terminator-m4 | terminator-replay-8 | 25.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.93e-05, strictImageFailures=8 |
| terminator-m4 | terminator-replay-9 | 33.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.91e-05, strictImageFailures=24 |
| spec-refusal-m4 | glass_render-spec | 61.3 | PASS | glibc, device-scene | strictImageFailures=97, strictStageFailures=1087; host re-judged vs glibc PASS: practical=0 strict=97 max=1.91e-06 |
| hostcap-linux-triangle | tri0 | 15.5 | PASS | source capture CPU film | maximumAbsoluteImageError=4.77e-07, strictImageFailures=12 |
| hostcap-linux-triangle | tri1 | 20.9 | PASS | source capture CPU film | maximumAbsoluteImageError=1.49e-07, strictImageFailures=1 |
| hostcap-linux-triangle | tri2 | 21.1 | PASS | source capture CPU film | maximumAbsoluteImageError=6.41e-07, strictImageFailures=14 |
| hostcap-linux-triangle | tri3 | 21.5 | PASS | source capture CPU film | maximumAbsoluteImageError=1.76e-06, strictImageFailures=236 |
| hostcap-linux-triangle | tri4 | 21.4 | PASS | source capture CPU film | maximumAbsoluteImageError=3.73e-07, strictImageFailures=8 |
| hostcap-linux-triangle | tri5 | 34.5 | PASS | source capture CPU film | maximumAbsoluteImageError=2.38e-06, strictImageFailures=258 |
| hostcap-linux-terminator | term0 | 12.5 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term1 | 14.4 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term2 | 14.7 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term3 | 14.9 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term4 | 25.5 | PASS | source capture CPU film | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| hostcap-linux-terminator | term5 | 24.9 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| hostcap-linux-terminator | term6 | 25.4 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| hostcap-linux-terminator | term7 | 25.2 | PASS | source capture CPU film | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| hostcap-linux-terminator | term8 | 25.3 | PASS | source capture CPU film | maximumAbsoluteImageError=2.93e-05, strictImageFailures=8 |
| hostcap-linux-terminator | term9 | 33.3 | PASS | source capture CPU film | maximumAbsoluteImageError=2.93e-05, strictImageFailures=22 |
| pixel-motion-own | object-moving-images | 44.5 | PASS | device's own CPU / gate | maximumAbsoluteError=6.08e-06 |
| pixel-motion-own | object-stationary-images | 35.6 | PASS | device's own CPU / gate | maximumAbsoluteError=6.08e-06 |
| pixel-motion-own | object-replay-motion-0 | 30.3 | PASS | source capture CPU film | maximumAbsoluteImageError=3.58e-07, strictImageFailures=10 |
| pixel-motion-own | object-replay-motion-1 | 31.3 | PASS | source capture CPU film | maximumAbsoluteImageError=2.86e-06, strictImageFailures=26 |
| pixel-motion-own | object-replay-motion-2 | 31.5 | PASS | source capture CPU film | maximumAbsoluteImageError=2.4e-06, strictImageFailures=290 |
| pixel-motion-own | object-replay-motion-3 | 31.2 | PASS | source capture CPU film | maximumAbsoluteImageError=1.02e-07, strictImageFailures=1 |
| pixel-motion-own | object-replay-motion-4 | 31.7 | PASS | source capture CPU film | maximumAbsoluteImageError=6.08e-06, strictImageFailures=295 |
| pixel-motion-own | object-replay-motion-5 | 31.4 | PASS | source capture CPU film | maximumAbsoluteImageError=4.53e-06, strictImageFailures=59 |
| pixel-motion-own | object-replay-motion-6 | 33.2 | PASS | source capture CPU film | maximumAbsoluteImageError=6.08e-06, strictImageFailures=342 |
| pixel-motion-own | object-replay-motion-7 | 31.5 | PASS | source capture CPU film | maximumAbsoluteImageError=6.02e-06, strictImageFailures=246 |
| pixel-motion-x64 | object-moving-images | 45.8 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1306 max=6.08e-06 |
| pixel-motion-x64 | object-stationary-images | 37.1 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1157 max=6.08e-06 |
| pixel-motion-x64 | object-replay-motion-0 | 31.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.68e-06, strictImageFailures=18 |
| pixel-motion-x64 | object-replay-motion-1 | 32.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=16 |
| pixel-motion-x64 | object-replay-motion-2 | 33.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.52e-06, strictImageFailures=303 |
| pixel-motion-x64 | object-replay-motion-3 | 33.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.08e-09, strictImageFailures=0 |
| pixel-motion-x64 | object-replay-motion-4 | 33.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=302 |
| pixel-motion-x64 | object-replay-motion-5 | 33.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.29e-06, strictImageFailures=72 |
| pixel-motion-x64 | object-replay-motion-6 | 33.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=344 |
| pixel-motion-x64 | object-replay-motion-7 | 33.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=251 |
| spec-refusal-m4b | glass_render-spec | 60.1 | PASS | glibc, device-scene | strictImageFailures=97, strictStageFailures=1087; host re-judged vs glibc PASS: practical=0 strict=97 max=1.91e-06 |
| spec-refusal-m4b | glass_render-nospec-refused | 0.4 | PASS | expected refusal |  |
| spec-refusal-m4b | glass_render-superset-refused | 0.3 | PASS | expected refusal |  |

### hp

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| materials-m4 | principled_svm | 164.8 | PASS | glibc, device-scene | strictFloatFailures=946, maximumAbsoluteError=9.54e-07 |
| materials-m4 | principled_bounce | 930.7 | PASS | device's own CPU / gate | strictFloatFailures=6265, maximumAbsoluteError=0.125 |
| materials-m4 | principled_render | 152.7 | PASS | glibc, device-scene | strictImageFailures=93, strictStageFailures=1028; host re-judged vs glibc PASS: practical=0 strict=93 max=6.44e-06 |
| materials-m4 | nodes_svm | 922.1 | PASS | glibc, device-scene | strictFloatFailures=10978, maximumAbsoluteError=0.0107 |
| materials-m4 | nodes_world | 261.7 | PASS | device's own CPU / gate | strictFloatFailures=9125, maximumAbsoluteError=6.1e-05 |
| materials-m4 | nodes_render | 111.8 | PASS | glibc, device-scene | strictImageFailures=1843, strictStageFailures=3987; host re-judged vs glibc PASS: practical=0 strict=1843 max=0.000103 |
| materials-m4 | principled-replay | 120.2 | PASS | source capture CPU film | maximumAbsoluteImageError=6.44e-06, strictImageFailures=95 |
| materials-m4 | nodes-replay | 80.0 | PASS | source capture CPU film | maximumAbsoluteImageError=0.000103, strictImageFailures=1849 |
| triangle-m4 | triangle_sample | 22.9 | PASS | glibc, device-scene | maximumAbsoluteError=0.000488; host re-judged vs glibc PASS: practical=0 strict=865 max=0.000488 |
| triangle-m4 | surface | 137.0 | PASS | device's own CPU / gate | strictFloatFailures=1809, maximumAbsoluteError=3.81e-06 |
| triangle-m4 | triangle_lighting | 90.8 | PASS | glibc, device-scene | maximumAbsoluteError=2.32e-06; host re-judged vs glibc PASS: practical=0 strict=887 max=2.32e-06 |
| triangle-m4 | replay-0 | 37.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=9 |
| triangle-m4 | replay-1 | 45.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| triangle-m4 | replay-2 | 46.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.41e-07, strictImageFailures=14 |
| triangle-m4 | replay-3 | 45.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-06, strictImageFailures=427 |
| triangle-m4 | replay-4 | 46.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.73e-07, strictImageFailures=10 |
| triangle-m4 | replay-5 | 64.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.26e-06, strictImageFailures=427 |
| emitter-m4 | emitter_sample | 20.0 | PASS | glibc, device-scene | maximumAbsoluteError=0.00513; host re-judged vs glibc PASS: practical=0 strict=235 max=0.00513 |
| lighting-m4 | lighting_mis | 48.3 | PASS | glibc, device-scene | maximumAbsoluteError=8.34e-06; host re-judged vs glibc PASS: practical=0 strict=2681 max=8.34e-06 |
| lighting-m4 | broad_lighting | 67.0 | PASS | glibc, device-scene | maximumAbsoluteError=5.34e-05; host re-judged vs glibc PASS: practical=0 strict=1741 max=5.34e-05 |
| lighting-m4 | lighting_mis-replay-0 | 39.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=3 |
| lighting-m4 | lighting_mis-replay-1 | 38.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=11 |
| lighting-m4 | broad_lighting-replay-0 | 38.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=1 |
| lighting-m4 | broad_lighting-replay-1 | 39.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.07e-07, strictImageFailures=26 |
| regression-m4 | glass_render | 70.7 | PASS | glibc, device-scene | strictImageFailures=104, strictStageFailures=1155; host re-judged vs glibc PASS: practical=0 strict=104 max=9.54e-07 |
| regression-m4 | lighting_mis | 48.5 | PASS | glibc, device-scene | maximumAbsoluteError=8.34e-06; host re-judged vs glibc PASS: practical=0 strict=2681 max=8.34e-06 |
| regression-m4 | transparent_lighting | 79.1 | PASS | glibc, device-scene | maximumAbsoluteError=7.75e-06; host re-judged vs glibc PASS: practical=0 strict=1682 max=7.75e-06 |
| transparent-m4 | transparent_shadow | 13.8 | PASS | device's own CPU / gate | maximumAbsoluteError=1.19e-07 |
| transparent-m4 | transparent_lighting | 80.0 | PASS | glibc, device-scene | maximumAbsoluteError=7.75e-06; host re-judged vs glibc PASS: practical=0 strict=1682 max=7.75e-06 |
| transparent-m4 | transparent_lighting-replay-0 | 43.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.49e-07, strictImageFailures=1 |
| transparent-m4 | transparent_lighting-replay-1 | 43.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=8 |
| transparent-m4 | transparent_lighting-replay-2 | 48.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=93 |
| transparent-m4 | transparent_lighting-replay-3 | 49.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.01e-06, strictImageFailures=323 |
| transparent-m4 | transparent_lighting-replay-4 | 49.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.75e-06, strictImageFailures=435 |
| transparent-m4 | transparent_lighting-replay-5 | 50.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.75e-07, strictImageFailures=4 |
| transparent-m4 | transparent_lighting-replay-6 | 48.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.56e-06, strictImageFailures=299 |
| transparent-m4 | transparent_lighting-replay-7 | 49.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.79e-06, strictImageFailures=519 |
| transparent-m4 | transparent-shadow-replay-direct | 6.2 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m4 | transparent-shadow-replay-indexed | 6.1 | PASS | source capture CPU film | maximumAbsoluteImageError=7.45e-09, strictImageFailures=0 |
| terminator-m4 | shadow_terminator | 157.4 | PASS | glibc, device-scene | maximumAbsoluteError=1.53e-05; host re-judged vs glibc PASS: practical=0 strict=97 max=1.53e-05 |
| terminator-m4 | terminator-replay-0 | 32.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m4 | terminator-replay-1 | 41.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m4 | terminator-replay-2 | 41.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m4 | terminator-replay-3 | 40.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m4 | terminator-replay-4 | 69.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=22 |
| terminator-m4 | terminator-replay-5 | 67.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=18 |
| terminator-m4 | terminator-replay-6 | 68.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=18 |
| terminator-m4 | terminator-replay-7 | 68.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=22 |
| terminator-m4 | terminator-replay-8 | 69.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.38e-07, strictImageFailures=5 |
| terminator-m4 | terminator-replay-9 | 81.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.1e-06, strictImageFailures=12 |
| spec-refusal-m4 | glass_render-spec | 70.6 | PASS | glibc, device-scene | strictImageFailures=104, strictStageFailures=1155; host re-judged vs glibc PASS: practical=0 strict=104 max=9.54e-07 |
| spec-refusal-m4 | glass_render-nospec-refused | 0.3 | PASS | expected refusal |  |
| spec-refusal-m4 | glass_render-superset-refused | 0.2 | PASS | expected refusal |  |
| hostcap-linux-triangle | tri0 | 39.7 | PASS | source capture CPU film | maximumAbsoluteImageError=4.77e-07, strictImageFailures=9 |
| hostcap-linux-triangle | tri1 | 48.8 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| hostcap-linux-triangle | tri2 | 50.9 | PASS | source capture CPU film | maximumAbsoluteImageError=6.41e-07, strictImageFailures=14 |
| hostcap-linux-triangle | tri3 | 48.9 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-06, strictImageFailures=430 |
| hostcap-linux-triangle | tri4 | 48.4 | PASS | source capture CPU film | maximumAbsoluteImageError=3.73e-07, strictImageFailures=10 |
| hostcap-linux-triangle | tri5 | 67.1 | PASS | source capture CPU film | maximumAbsoluteImageError=2.26e-06, strictImageFailures=425 |
| hostcap-linux-terminator | term0 | 34.3 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term1 | 41.9 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term2 | 42.6 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term3 | 42.5 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term4 | 69.2 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=22 |
| hostcap-linux-terminator | term5 | 69.8 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=18 |
| hostcap-linux-terminator | term6 | 72.2 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=18 |
| hostcap-linux-terminator | term7 | 70.0 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=22 |
| hostcap-linux-terminator | term8 | 70.6 | PASS | source capture CPU film | maximumAbsoluteImageError=2.38e-07, strictImageFailures=5 |
| hostcap-linux-terminator | term9 | 84.1 | PASS | source capture CPU film | maximumAbsoluteImageError=1.07e-06, strictImageFailures=17 |

### pixel

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| materials-p-principled_svm | principled_svm | 335.3 | TIMEOUT/cutoff | device's own CPU / gate |  |
| materials-p-principled_bounce | principled_bounce | 915.3 | TIMEOUT/cutoff | device's own CPU / gate |  |
| materials-p-principled_render | principled_render | 148.1 | TIMEOUT/cutoff | device's own CPU / gate |  |
| materials-p-principled_render | principled-replay | - | not run (source job failed) | device's own CPU / gate |  |
| materials-p-nodes_svm | nodes_svm | 461.2 | TIMEOUT/cutoff | device's own CPU / gate |  |
| materials-p-nodes_render | nodes_render | 216.8 | FAIL:  parameter Render iteration failed: WebGPU scheduler kernel reported invalid data (mask 24): invalid/uninitialized SVM stack unsupported SVM parameter | glibc, device-scene | ; host re-judged vs glibc FAIL: practical=0 strict=0 max=0 |
| materials-p-nodes_render | nodes-replay | - | not run (source job failed) | device's own CPU / gate |  |

### s20

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| transparent-q-transparent_lighting | transparent_lighting | 47.5 | FAIL:   Parent device is lost note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace | glibc, device-scene | ; host re-judged vs glibc FAIL: practical=0 strict=0 max=0 |
| transparent-q-transparent_lighting | transparent_lighting-replay-0 | - | not run (source job failed) | device's own CPU / gate |  |
| transparent-q-transparent_lighting | transparent_lighting-replay-1 | - | not run (source job failed) | device's own CPU / gate |  |
| transparent-q-transparent_lighting | transparent_lighting-replay-2 | - | not run (source job failed) | device's own CPU / gate |  |
| transparent-q-transparent_lighting | transparent_lighting-replay-3 | - | not run (source job failed) | device's own CPU / gate |  |
| transparent-q-transparent_lighting | transparent_lighting-replay-4 | - | not run (source job failed) | device's own CPU / gate |  |
| transparent-q-transparent_lighting | transparent_lighting-replay-5 | - | not run (source job failed) | device's own CPU / gate |  |
| transparent-q-transparent_lighting | transparent_lighting-replay-6 | - | not run (source job failed) | device's own CPU / gate |  |
| transparent-q-transparent_lighting | transparent_lighting-replay-7 | - | not run (source job failed) | device's own CPU / gate |  |
| transparent-q-transparent_shadow | transparent_shadow | 11.0 | FAIL: ted invalid data (mask 1): invalid state/data Transparent intersect failed: WebGPU scheduler kernel reported invalid data (mask 1): invalid state/data | device's own CPU / gate |  |
| transparent-q-transparent_shadow | transparent-shadow-replay-direct | - | not run (source job failed) | device's own CPU / gate |  |
| transparent-q-transparent_shadow | transparent-shadow-replay-indexed | - | not run (source job failed) | device's own CPU / gate |  |
| pixel-motion-q-object-moving-images | object-moving-images | 110.9 | FAIL: ask 8): invalid/uninitialized SVM stack Lighting stage failed: WebGPU scheduler kernel reported invalid data (mask 8): invalid/uninitialized SVM stack | glibc, device-scene | ; host re-judged vs glibc FAIL: practical=0 strict=0 max=0 |
| pixel-motion-q-object-moving-images | object-replay-motion-0 | - | not run (source job failed) | device's own CPU / gate |  |
| pixel-motion-q-object-moving-images | object-replay-motion-1 | - | not run (source job failed) | device's own CPU / gate |  |
| pixel-motion-q-object-moving-images | object-replay-motion-2 | - | not run (source job failed) | device's own CPU / gate |  |
| pixel-motion-q-object-moving-images | object-replay-motion-3 | - | not run (source job failed) | device's own CPU / gate |  |
| pixel-motion-q-object-moving-images | object-replay-motion-4 | - | not run (source job failed) | device's own CPU / gate |  |
| pixel-motion-q-object-moving-images | object-replay-motion-5 | - | not run (source job failed) | device's own CPU / gate |  |
| pixel-motion-q-object-moving-images | object-replay-motion-6 | - | not run (source job failed) | device's own CPU / gate |  |
| pixel-motion-q-object-moving-images | object-replay-motion-7 | - | not run (source job failed) | device's own CPU / gate |  |
| pixel-motion-q-deforming-moving-images | deforming-moving-images | 82.7 | FAIL: ask 8): invalid/uninitialized SVM stack Lighting stage failed: WebGPU scheduler kernel reported invalid data (mask 8): invalid/uninitialized SVM stack | glibc, device-scene | ; host re-judged vs glibc FAIL: practical=0 strict=0 max=0 |
| pixel-motion-q-deforming-moving-images | deforming-replay-motion-0 | - | not run (source job failed) | device's own CPU / gate |  |
| pixel-motion-q-deforming-moving-images | deforming-replay-motion-1 | - | not run (source job failed) | device's own CPU / gate |  |
| pixel-motion-q-deforming-moving-images | deforming-replay-motion-2 | - | not run (source job failed) | device's own CPU / gate |  |
| pixel-motion-q-deforming-moving-images | deforming-replay-motion-3 | - | not run (source job failed) | device's own CPU / gate |  |
| pixel-motion-q-deforming-moving-images | deforming-replay-motion-4 | - | not run (source job failed) | device's own CPU / gate |  |
| pixel-motion-q-deforming-moving-images | deforming-replay-motion-5 | - | not run (source job failed) | device's own CPU / gate |  |
| pixel-motion-q-deforming-moving-images | deforming-replay-motion-6 | - | not run (source job failed) | device's own CPU / gate |  |
| pixel-motion-q-deforming-moving-images | deforming-replay-motion-7 | - | not run (source job failed) | device's own CPU / gate |  |
| materials-q-nodes_world | nodes_world | 6.2 | FAIL: ta (mask 8): invalid/uninitialized SVM stack Compiled world 0: WebGPU scheduler kernel reported invalid data (mask 8): invalid/uninitialized SVM stack | device's own CPU / gate |  |
| materials-q-principled_svm | principled_svm | 401.1 | PASS | glibc, device-scene | strictFloatFailures=557, maximumAbsoluteError=3.59e-06 |
| materials-q-principled_bounce | principled_bounce | 915.3 | TIMEOUT/cutoff | device's own CPU / gate |  |
| materials-q-principled_render | principled_render | 233.1 | FAIL: .8700200915,-0.3449651897,-0.3522275686 rayBitsIdentical=0 tmin=0 bounce=2 stage 40 path.mis_ray_pdf path=2441 actual=1751.469238 expected=1747.274414 | glibc, device-scene | ; host re-judged vs glibc FAIL: practical=0 strict=0 max=0 |
| materials-q-principled_render | principled-replay | - | not run (source job failed) | device's own CPU / gate |  |
| materials-q-nodes_render | nodes_render | 17.2 | FAIL: Caused by:   In wgpuDeviceCreateComputePipeline     Parent device is lost | glibc, device-scene | ; host re-judged vs glibc FAIL: practical=0 strict=0 max=0 |
| materials-q-nodes_render | nodes-replay | - | not run (source job failed) | device's own CPU / gate |  |
