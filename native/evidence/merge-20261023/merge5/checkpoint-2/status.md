# Part 2 (evidence) status 2026-10-23 04:01:27  (cap 07:59)

automatic 30-minute checkpoint

## locks held
s20

## linux slot ends
  [01:00:54] END ev-render exit=0 30s
  [01:00:54] END ev-device exit=0 0s
  [01:00:58] END ev-camera exit=0 4s
  [01:01:05] END ev-background exit=0 7s
  [01:01:11] END ev-closest exit=0 6s
  [01:01:43] END ev-svm_world exit=0 32s
  [01:01:54] END ev-svm_surface exit=0 11s
  [01:02:17] END ev-attribute exit=0 23s
  [01:02:47] END ev-image exit=0 30s
  [01:02:48] END ev-image_file exit=0 1s
  [01:03:23] END ev-normal exit=0 35s
  [01:03:57] END ev-render-attributes exit=0 34s
  [01:04:35] END ev-render-images exit=0 38s
  [01:05:13] END e-glass-superset exit=0 290s
  [01:05:24] END ev-render-normals exit=0 49s
  [01:12:05] END ev-nodes_svm exit=0 401s
  [01:14:22] END ev-nodes_world exit=0 137s
  [01:20:43] END e-principled-superset exit=0 930s
  [01:22:55] END e-nodes-render-superset exit=0 132s
  [01:23:45] END e-abi-dump exit=0 50s
  [01:24:28] END e-transparent-dump exit=0 43s
  [01:27:12] END e-envelope-abi exit=0 164s
  [01:27:34] END e-all-lanes exit=0 1633s
  [01:29:41] END e-host-gate exit=0 127s
  [01:30:34] END e-dotnet-test exit=0 53s
  [01:31:26] END e-dotnet-test-debug exit=0 52s
  [01:31:59] END e-nodes-gate exit=0 1897s
  [01:32:04] END e-envelope-materials exit=0 292s
  [01:32:26] END e-libm-linux exit=0 22s
  [01:32:47] END e-principled-replay exit=0 45s
  [01:33:26] END e-nodes-replay exit=0 39s
  [01:36:29] END e-reference-export exit=0 243s
  [01:38:20] END e-triangle exit=0 111s
  [01:40:02] END e-shadow-terminator exit=0 102s
  [01:40:13] END e-surface exit=0 11s
  [01:47:13] END e-principled-gate exit=0 914s
  [01:47:33] END e-tier-gate exit=0 20s
  [01:49:44] END e-transparent-shadow exit=0 131s
  [01:49:45] END e-sim650-nodes-world exit=1 0s
  [01:50:16] END e-sim650-glass-render exit=0 31s
  [01:51:21] END e-size-guard exit=0 64s
  [01:51:34] END e-triangle-sample exit=0 13s
  [01:51:43] END e-svm-surface exit=0 9s
  [01:52:09] END e-svm-world exit=0 26s
  [01:52:40] END e-probe-normal exit=0 31s
  [01:53:00] END e-probe-attribute exit=0 20s
  [01:53:23] END e-probe-image exit=0 23s
  [01:53:23] END e-probe-image_file exit=0 0s
  [01:53:29] END e-probe-shader_data exit=0 6s

## device corpora (passed per run)
  hp/emitter-m5: complete=True passed=True wall=60.0 jobs=1 notPassed=[]
  hp/hostcap-linux-terminator: complete=True passed=True wall=608.1 jobs=10 notPassed=[]
  hp/hostcap-linux-triangle: complete=True passed=True wall=319.1 jobs=6 notPassed=[]
  hp/lighting-m5: complete=True passed=True wall=398.3 jobs=6 notPassed=[]
  hp/materials-m5: complete=True passed=True wall=2404.7 jobs=8 notPassed=[]
  hp/regression-m5: complete=True passed=True wall=323.5 jobs=3 notPassed=[]
  hp/spec-refusal-m5: complete=True passed=True wall=113.4 jobs=3 notPassed=[]
  hp/terminator-m5: complete=True passed=True wall=796.6 jobs=11 notPassed=[]
  hp/transparent-m5: complete=True passed=True wall=568.3 jobs=12 notPassed=[]
  hp/triangle-m5: complete=True passed=True wall=525.9 jobs=9 notPassed=[]
  s20/a650-refusals-m5: complete=True passed=True wall=22.4 jobs=3 notPassed=[]
  s20/materials-s20-principled_bounce: complete=False passed=None wall=None jobs=0 notPassed=[]
  s20/materials-s20-principled_render: complete=True passed=False wall=253.5 jobs=2 notPassed=['principled_render', 'principled-replay']
  s20/materials-s20-principled_svm: complete=True passed=False wall=246.2 jobs=1 notPassed=['principled_svm']
  s20/pixel-motion-s20-deforming-moving-images: complete=True passed=True wall=1350.8 jobs=9 notPassed=[]
  s20/pixel-motion-s20-object-moving-images: complete=True passed=True wall=1189.7 jobs=9 notPassed=[]
  s20/regression-s20-glass_render: complete=True passed=True wall=135.9 jobs=1 notPassed=[]
  s20/regression-s20-lighting_mis: complete=True passed=True wall=108.9 jobs=1 notPassed=[]
  s20/transparent-s20-transparent_lighting: complete=True passed=True wall=713.2 jobs=9 notPassed=[]
  s20/transparent-s20-transparent_shadow: complete=True passed=True wall=60.7 jobs=3 notPassed=[]
  s20/triangle-s20-surface: complete=True passed=True wall=65.3 jobs=1 notPassed=[]
  s20/triangle-s20-triangle_sample: complete=True passed=True wall=117.2 jobs=1 notPassed=[]
  samsung/emitter-m5: complete=True passed=True wall=46.1 jobs=1 notPassed=[]
  samsung/hostcap-linux-terminator: complete=True passed=True wall=233.9 jobs=10 notPassed=[]
  samsung/hostcap-linux-triangle: complete=True passed=True wall=153.5 jobs=6 notPassed=[]
  samsung/lighting-m5: complete=True passed=True wall=227.1 jobs=6 notPassed=[]
  samsung/materials-m5: complete=True passed=False wall=2821.6 jobs=8 notPassed=['principled_render', 'nodes_render', 'principled-replay', 'nodes-replay']
  samsung/pixel-motion-own: complete=True passed=True wall=366.9 jobs=10 notPassed=[]
  samsung/pixel-motion-x64: complete=True passed=True wall=415.2 jobs=10 notPassed=[]
  samsung/regression-m5: complete=True passed=True wall=272.0 jobs=3 notPassed=[]
  samsung/spec-refusal-m5: complete=True passed=True wall=101.8 jobs=3 notPassed=[]
  samsung/terminator-m5: complete=True passed=True wall=356.7 jobs=11 notPassed=[]
  samsung/transparent-m5: complete=True passed=True wall=363.3 jobs=12 notPassed=[]
  samsung/triangle-m5: complete=True passed=False wall=326.3 jobs=9 notPassed=['triangle_sample']

## timeline (tail)
[03:31:24] s20 pixel-motion-s20-object-moving-images: PASS object-replay-motion-6 122s readbacks=460 max=6.08e-06 strict=338 fmt=native-command-stream-v6
[03:31:24] s20 pixel-motion-s20-object-moving-images: START object-replay-motion-7 timeout=600s
[03:31:27] checkpoint-2 status written
[03:34:05] s20 pixel-motion-s20-object-moving-images: PASS object-replay-motion-7 161s readbacks=354 max=6.02e-06 strict=249 fmt=native-command-stream-v6
[03:34:24] s20 pixel-motion-s20-object-moving-images: END passed=True wall=1190s/3000s failures=0
[03:34:24] s20 corpus pixel-motion-s20-object-moving-images (object-moving-images) exit=0 timeout=600s
[03:34:46] s20 pixel-motion-s20-deforming-moving-images: START deforming-moving-images timeout=600s
[03:34:46] s20 pixel-motion-s20-deforming-moving-images: SCENE-DUMP deforming-moving-images
[03:35:06] s20 pixel-motion-s20-deforming-moving-images: SCENE-REFERENCE deforming-moving-images references=8
[03:40:27] s20 pixel-motion-s20-deforming-moving-images: REFERENCE deforming-moving-images host re-judged vs glibc: True
[03:40:27] s20 pixel-motion-s20-deforming-moving-images: PASS deforming-moving-images 315s maxPipelineMs=59113
[03:40:27] s20 pixel-motion-s20-deforming-moving-images: START deforming-replay-motion-0 timeout=600s
[03:41:53] s20 pixel-motion-s20-deforming-moving-images: PASS deforming-replay-motion-0 85s readbacks=420 max=2.38e-07 strict=2 fmt=native-command-stream-v6
[03:41:53] s20 pixel-motion-s20-deforming-moving-images: START deforming-replay-motion-1 timeout=600s
[03:43:53] s20 pixel-motion-s20-deforming-moving-images: PASS deforming-replay-motion-1 119s readbacks=454 max=5.96e-07 strict=19 fmt=native-command-stream-v6
[03:43:53] s20 pixel-motion-s20-deforming-moving-images: START deforming-replay-motion-2 timeout=600s
[03:45:53] s20 pixel-motion-s20-deforming-moving-images: PASS deforming-replay-motion-2 119s readbacks=460 max=2.4e-06 strict=304 fmt=native-command-stream-v6
[03:45:53] s20 pixel-motion-s20-deforming-moving-images: START deforming-replay-motion-3 timeout=600s
[03:47:53] s20 pixel-motion-s20-deforming-moving-images: PASS deforming-replay-motion-3 119s readbacks=412 max=9.31e-09 strict=0 fmt=native-command-stream-v6
[03:47:53] s20 pixel-motion-s20-deforming-moving-images: START deforming-replay-motion-4 timeout=600s
[03:49:54] s20 pixel-motion-s20-deforming-moving-images: PASS deforming-replay-motion-4 120s readbacks=460 max=6.08e-06 strict=280 fmt=native-command-stream-v6
[03:49:54] s20 pixel-motion-s20-deforming-moving-images: START deforming-replay-motion-5 timeout=600s
[03:51:54] s20 pixel-motion-s20-deforming-moving-images: PASS deforming-replay-motion-5 120s readbacks=426 max=4.17e-06 strict=49 fmt=native-command-stream-v6
[03:51:54] s20 pixel-motion-s20-deforming-moving-images: START deforming-replay-motion-6 timeout=600s
[03:54:04] s20 pixel-motion-s20-deforming-moving-images: PASS deforming-replay-motion-6 129s readbacks=460 max=6.08e-06 strict=300 fmt=native-command-stream-v6
[03:54:04] s20 pixel-motion-s20-deforming-moving-images: START deforming-replay-motion-7 timeout=600s
[03:56:55] s20 pixel-motion-s20-deforming-moving-images: PASS deforming-replay-motion-7 170s readbacks=359 max=6.02e-06 strict=259 fmt=native-command-stream-v6
[03:57:12] s20 pixel-motion-s20-deforming-moving-images: END passed=True wall=1351s/3000s failures=0
[03:57:12] s20 corpus pixel-motion-s20-deforming-moving-images (deforming-moving-images) exit=0 timeout=600s
[03:57:34] s20 materials-s20-principled_bounce: START principled_bounce timeout=2400s
