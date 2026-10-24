# Part 2 (evidence) status 2026-10-23 14:05:57  (cap 7 h from launch 11:15, i.e. 18:15)

all device chains and Linux streams finished 14:02; ledger rewritten; freezing checkpoint next

## locks held

## linux slot ends
  [10:43:26] END p2v-render exit=0 29s
  [10:43:27] END p2v-device exit=0 1s
  [10:43:30] END p2v-camera exit=0 3s
  [10:43:39] END p2v-background exit=0 9s
  [10:43:46] END p2v-closest exit=0 7s
  [10:44:18] END p2v-svm_world exit=0 32s
  [10:44:30] END p2v-svm_surface exit=0 12s
  [10:44:55] END p2v-attribute exit=0 25s
  [10:45:28] END p2v-image exit=0 33s
  [10:45:29] END p2v-image_file exit=0 1s
  [10:46:08] END p2v-normal exit=0 39s
  [10:46:46] END p2v-render-attributes exit=0 37s
  [10:47:26] END p2v-render-images exit=0 40s
  [10:48:04] END p2-glass-superset exit=0 307s
  [10:48:17] END p2v-render-normals exit=0 51s
  [10:55:43] END p2v-nodes_svm exit=0 446s
  [10:58:21] END p2v-nodes_world exit=0 158s
  [11:04:31] END p2-principled-superset exit=0 987s
  [11:07:06] END p2-nodes-render-superset exit=0 155s
  [11:08:02] END p2-abi-dump exit=0 56s
  [11:08:45] END p2-transparent-dump exit=0 43s
  [11:11:36] END p2-envelope-abi exit=0 171s
  [11:14:07] END p2-all-lanes exit=0 1870s
  [11:16:21] END p2-host-gate exit=0 134s
  [11:16:59] END p2-envelope-materials exit=0 323s
  [11:17:23] END p2-libm-linux exit=0 24s
  [11:18:21] END p2-nodes-gate exit=0 2124s
  [11:18:27] END p2-dotnet-test exit=0 126s
  [11:19:33] END p2-principled-replay exit=0 52s
  [11:20:18] END p2-nodes-replay exit=0 45s
  [11:20:39] END p2-dotnet-test-debug exit=0 132s
  [11:22:03] END p2-reference-export exit=0 280s
  [11:24:23] END p2-triangle exit=0 140s
  [11:26:13] END p2-shadow-terminator exit=0 110s
  [11:26:27] END p2-surface exit=0 14s
  [11:34:06] END p2-principled-gate exit=0 945s
  [11:34:27] END p2-tier-gate exit=0 21s
  [11:36:34] END p2-transparent-shadow exit=0 127s
  [11:36:34] END p2-sim650-nodes-world exit=1 0s
  [11:37:08] END p2-sim650-glass-render exit=0 34s
  [11:38:20] END p2-size-guard exit=0 72s
  [11:38:34] END p2-triangle-sample exit=0 14s
  [11:38:46] END p2-svm-surface exit=0 12s
  [11:39:15] END p2-svm-world exit=0 29s
  [11:39:47] END p2-probe-normal exit=0 32s
  [11:40:07] END p2-probe-attribute exit=0 20s
  [11:40:32] END p2-probe-image exit=0 24s
  [11:40:33] END p2-probe-image_file exit=0 1s
  [11:40:39] END p2-probe-shader_data exit=0 6s
  [11:41:56] END p2-s-nodes_render exit=0 77s
  [11:43:20] END p2-s-principled_render exit=0 84s
  [11:44:18] END p2-s-microfacet_render exit=0 58s
  [11:45:11] END p2-s-render-static exit=0 53s
  [11:46:08] END p2-s-render-motion-0 exit=0 57s
  [11:47:04] END p2-s-render-motion-1 exit=0 56s
  [11:47:59] END p2-s-render-motion-2 exit=0 55s
  [11:48:58] END p2-s-render-motion-3 exit=0 59s
  [11:49:53] END p2-s-render-motion-4 exit=0 55s
  [11:50:50] END p2-s-render-motion-5 exit=0 57s
  [11:51:45] END p2-s-render-motion-6 exit=0 55s
  [11:52:39] END p2-s-render-motion-7 exit=0 54s
  [11:53:57] END p2-s-volume-absorption exit=0 78s
  [11:55:50] END p2-s-volume-isotropic exit=0 113s
  [11:57:41] END p2-s-volume-forward exit=0 111s
  [11:59:29] END p2-s-volume-backward exit=0 108s
  [12:01:41] END p2-s-closed-single exit=0 132s
  [12:05:22] END p2-s-closed-overlap exit=0 221s
  [12:08:31] END p2-s-closed-inside exit=0 189s
  [12:08:32] END p2-gltf-sample-write exit=0 1s
  [12:08:59] END p2-gltf-sample exit=0 27s
  [12:08:59] END p2-gltf-plane-write exit=0 0s
  [12:09:58] END p2-gltf-plane exit=0 59s
  [12:09:59] END p2-bench-bundle exit=0 1s
  [12:12:02] END p2-oracle-starters exit=0 123s
  [12:13:02] END p2-oracle-bench exit=0 60s
  [12:14:02] END p2-oracle-control exit=0 60s
  [12:14:02] END p2-compare-selftest exit=0 0s
  [12:18:21] END p2-gltf-plane2 exit=0 64s

## device corpora (passed per run)
  hp/arm-m6: complete=True passed=True wall=145.5 jobs=1 notPassed=[]
  hp/emitter-m6: complete=True passed=True wall=65.4 jobs=1 notPassed=[]
  hp/hostcap-linux-terminator: complete=True passed=True wall=607.5 jobs=10 notPassed=[]
  hp/hostcap-linux-triangle: complete=True passed=True wall=320.6 jobs=6 notPassed=[]
  hp/lighting-m6: complete=True passed=True wall=422.8 jobs=6 notPassed=[]
  hp/materials-m6: complete=True passed=True wall=2513.2 jobs=8 notPassed=[]
  hp/motion-m6: complete=True passed=True wall=1012.0 jobs=9 notPassed=[]
  hp/regression-m6: complete=True passed=True wall=380.0 jobs=3 notPassed=[]
  hp/spec-refusal-m6: complete=True passed=True wall=143.6 jobs=3 notPassed=[]
  hp/terminator-m6: complete=True passed=True wall=796.9 jobs=11 notPassed=[]
  hp/transparent-m6: complete=True passed=True wall=572.5 jobs=12 notPassed=[]
  hp/triangle-m6: complete=True passed=True wall=534.5 jobs=9 notPassed=[]
  hp/volume-m6: complete=True passed=True wall=1748.6 jobs=17 notPassed=[]
  s20/a650-refusals-m6: complete=True passed=True wall=28.6 jobs=3 notPassed=[]
  s20/materials-s20-principled_bounce: complete=True passed=True wall=359.2 jobs=1 notPassed=[]
  s20/materials-s20-principled_render: complete=True passed=True wall=466.8 jobs=2 notPassed=[]
  s20/materials-s20-principled_svm: complete=True passed=True wall=579.2 jobs=1 notPassed=[]
  s20/pixel-motion-s20-deforming-moving-images: complete=True passed=True wall=1559.1 jobs=9 notPassed=[]
  s20/pixel-motion-s20-object-moving-images: complete=True passed=True wall=1345.5 jobs=9 notPassed=[]
  s20/regression-s20-glass_render: complete=True passed=True wall=177.5 jobs=1 notPassed=[]
  s20/regression-s20-lighting_mis: complete=True passed=True wall=127.4 jobs=1 notPassed=[]
  s20/transparent-s20-transparent_lighting: complete=True passed=True wall=731.4 jobs=9 notPassed=[]
  s20/transparent-s20-transparent_shadow: complete=True passed=True wall=65.3 jobs=3 notPassed=[]
  s20/triangle-s20-surface: complete=True passed=True wall=71.9 jobs=1 notPassed=[]
  s20/triangle-s20-triangle_sample: complete=True passed=True wall=133.4 jobs=1 notPassed=[]
  samsung/arm-m6: complete=True passed=True wall=124.8 jobs=1 notPassed=[]
  samsung/emitter-m6: complete=True passed=True wall=51.5 jobs=1 notPassed=[]
  samsung/hostcap-linux-terminator: complete=True passed=True wall=236.9 jobs=10 notPassed=[]
  samsung/hostcap-linux-triangle: complete=True passed=True wall=154.4 jobs=6 notPassed=[]
  samsung/lighting-m6: complete=True passed=True wall=243.6 jobs=6 notPassed=[]
  samsung/materials-m6: complete=True passed=True wall=2974.7 jobs=8 notPassed=[]
  samsung/motion-m6: complete=True passed=True wall=898.7 jobs=9 notPassed=[]
  samsung/pixel-motion-own: complete=True passed=True wall=364.5 jobs=10 notPassed=[]
  samsung/pixel-motion-x64: complete=True passed=True wall=424.1 jobs=10 notPassed=[]
  samsung/regression-m6: complete=True passed=True wall=305.9 jobs=3 notPassed=[]
  samsung/spec-refusal-m6: complete=True passed=True wall=123.6 jobs=3 notPassed=[]
  samsung/terminator-m6: complete=True passed=True wall=366.1 jobs=11 notPassed=[]
  samsung/transparent-m6: complete=True passed=True wall=373.4 jobs=12 notPassed=[]
  samsung/triangle-m6: complete=True passed=True wall=329.0 jobs=9 notPassed=[]
  samsung/volume-m6: complete=True passed=True wall=1519.9 jobs=17 notPassed=[]

## timeline (tail)
[13:42:48] hp volume-m6: START closed-replay-overlap timeout=1500s
[13:43:16] checkpoint-2 status written
[13:43:49] hp volume-m6: PASS closed-replay-overlap 53s readbacks=1192 max=4.58e-05 strict=1260 fmt=native-command-stream-v6
[13:43:49] hp volume-m6: START closed-replay-inside timeout=1500s
[13:44:52] hp volume-m6: PASS closed-replay-inside 54s readbacks=1116 max=2.29e-05 strict=2284 fmt=native-command-stream-v6
[13:45:12] hp volume-m6: END passed=True wall=1749s/7200s failures=0
[13:45:12] hp corpus volume-m6 exit=0
[13:45:12] hp lock released
[13:46:42] hp lock taken (chain6_device.sh hp batch E (Android host gate, managed app, bundle mode), waited 0s)
[13:46:42] hp START android-host-gate run1
[13:48:55] hp android-host-gate run1 END exit=0 133s
[13:48:55] hp START app-webgpu
[13:52:22] hp app-webgpu END exit=0 207s
[13:52:22] hp START app-bundle-control
[13:56:44] hp app-bundle-control END exit=0 262s
[13:56:44] hp START app-bundle-bench
[13:56:45] hp app-bundle-bench END exit=1 1s
[13:56:45] hp lock released
[13:56:56] hp lock taken (bench_app.sh hp (bench bundle managed app), waited 0s)
[13:56:56] hp START app-bundle-bench
[13:57:45] hp lock HELD by merge-wave-6 evidence agent 2026-10-23T13:56:56-07:00 bench_app.sh hp (bench bundle managed app); waiting (chain6_device.sh hp batch F (cleanup, leftovers))
[14:01:19] hp app-bundle-bench END exit=0 263s
[14:01:20] hp cleaned (left: '')
[14:01:20] hp lock released
[14:01:45] hp lock taken (chain6_device.sh hp batch F (cleanup, leftovers), waited 240s)
[14:01:46] hp leftovers listed: 59 folders, 5807936 KB, listing /home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/merge6/device/hp-leftovers-listing.txt; kept: codebrix-ncdbg (2026-09-08) 
[14:01:47] hp leftovers removed; remaining codebrix-*: /data/local/tmp/codebrix-ncdbg 
[14:01:47] hp cleaned (left: '')
[14:01:47] hp lock released
[14:05:46] device tables applied; ledger re-recorded (make_evidence6) ; inventory generate exit above
