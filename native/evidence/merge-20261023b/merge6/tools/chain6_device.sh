#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 6 part 2: device chain for a reference device (adapted from chain5_device.sh), in lock-sized batches so lane m1-oracle can interleave.
# Usage: chain6_device.sh DEVICE (samsung | hp). Nothing is built; every input was staged by part 1.
cd "$(dirname "$0")"; dev=$1; M=$(realpath ..); R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; V=$M/validation; ref=$M/reference/linux-x64
source ./lock6.sh
case $dev in
  hp) serial=192.168.86.225:5555; rid=android-x64; F="--reference $ref --device-scene"; FM="--reference $ref --device-scene --mesa-scratch-cache";;
  samsung) serial=R52W207XQ1V; rid=android-arm64; F="--reference $ref --device-scene"; FM="--reference $ref --device-scene";;
esac
A="/home/jeremy/Android/Sdk/platform-tools/adb -s $serial"
lg(){ echo "[$(date +%H:%M:%S)] $dev $*" >> $M/timeline.log; }
LOG=$M/device/$dev-chain.log
run(){ python3 run_corpus.py $dev "$@" >> $LOG 2>&1; lg "corpus $1-$2 exit=$?"; }
T=$V/c-triangle/out/triangle_lighting/run; S=$V/c-shadow-terminator/out/shadow_terminator/run
hostcap(){ python3 replay_host_captures.py $dev linux-triangle 3600 900 $(for c in 0 1 2 3 4 5; do echo tri$c=$T/case-$c; done) >> $LOG 2>&1; lg "hostcap triangle exit=$?"
           python3 replay_host_captures.py $dev linux-terminator 3600 900 $(for c in 0 1 2 3 4 5 6 7 8 9; do echo term$c=$S/case-$c; done) >> $LOG 2>&1; lg "hostcap terminator exit=$?"; }
hg(){ n=$1; echo "[$(date +%H:%M:%S)] DECLARED android host gate on $dev ($n; android_host_gate.py, --gpu-timeout 2400): budget 3000 s" >> $M/budgets.txt
  lg "START android-host-gate $n"; cd $R; s=$(date +%s)
  timeout 3000 python3 native/tools/android_host_gate.py --output $M/device/m6-hostgate-$dev-$n --references $V/a-host-gate/out --device $dev,$rid,$serial > $M/device/hostgate-$dev-$n.log 2>&1; c=$?
  lg "android-host-gate $n END exit=$c $(( $(date +%s)-s ))s"; cd $M/tools; }
app(){ mode=$1; out=$2; shift 2; echo "[$(date +%H:%M:%S)] DECLARED managed app $mode on $dev ($out; result timeout 2400 s, --uninstall): budget 2700 s" >> $M/budgets.txt
  lg "START app-$out"; cd $R; s=$(date +%s)
  timeout 2700 python3 native/tools/android_app_gate.py --build-output $M/android-app/validation-m6 --output $M/android-app/$out-$dev --device $dev,$rid,$serial --mode $mode "$@" --result-timeout 2400 --uninstall > $M/device/app-$out-$dev.log 2>&1; c=$?
  lg "app-$out END exit=$c $(( $(date +%s)-s ))s"; cd $M/tools; }
batch=${2:-all}
if [ $batch = all ] || [ $batch = A ]; then
  acquire $dev "chain6_device.sh $dev batch A (stage, materials, triangle, emitter, regression)" || exit 2
  echo "[$(date +%H:%M:%S)] DECLARED $dev staging push (stage.py push $dev): budget 1200 s" >> $M/budgets.txt
  timeout 1200 python3 stage.py push $dev >> $LOG 2>&1; c=$?; lg "staged exit=$c"
  if [ $c -ne 0 ]; then lg "STAGING FAILED: chain stopped (no binary run)"; $A shell 'rm -rf /data/local/tmp/codebrix-m6-*'; release $dev; exit 3; fi
  run materials m6 9000 2400 $FM
  run triangle m6 2400 1200 $F
  run emitter m6 600 300 $F
  run regression m6 1500 600 $F
  release $dev
  sleep 90
fi
if [ $batch = all ] || [ $batch = B ]; then
  acquire $dev "chain6_device.sh $dev batch B (lighting, transparent, terminator, spec-refusal, host-capture replays)" || exit 2
  run lighting m6 2400 1200 $F --jobs broad_lighting,lighting_mis --replays perjob2
  run transparent m6 2400 1200 $F
  run terminator m6 1800 900 $F
  run spec-refusal m6 1800 1200 $F
  hostcap
  release $dev
  sleep 90
fi
if [ $batch = all ] || [ $batch = C ]; then
  acquire $dev "chain6_device.sh $dev batch C (new device-scene rows, pixel motion)" || exit 2
  run arm m6 2400 1200 $F --jobs microfacet_render
  run motion m6 5400 1200 $F --jobs render-static,render-motion-0,render-motion-1,render-motion-2,render-motion-3,render-motion-4,render-motion-5,render-motion-6,render-motion-7
  if [ $dev = samsung ]; then
    run pixel-motion own 4800 2400 --jobs object-moving-images,object-stationary-images
    run pixel-motion x64 4800 2400 $F --jobs object-moving-images,object-stationary-images
  fi
  release $dev
  sleep 90
fi
if [ $batch = all ] || [ $batch = D ]; then
  acquire $dev "chain6_device.sh $dev batch D (volume rows)" || exit 2
  run volume m6 7200 1500 $F
  release $dev
  sleep 90
fi
if [ $batch = all ] || [ $batch = E ]; then
  acquire $dev "chain6_device.sh $dev batch E (Android host gate, managed app, bundle mode)" || exit 2
  hg run1
  app webgpu webgpu --linux-reference $M/reference/managed-linux-x64
  app bundle bundle-control --bundle $M/bundles/control --reference-film $M/oracles/control/oracle/gltf/textured-plane/cpu-x64-linux/film-0.f32
  app bundle bundle-bench --bundle $M/bundles/bench --reference-film $M/oracles/bench/oracle/assets/bench_01/cpu-x64-linux/film-0.f32
  release $dev
fi
if [ $batch = all ] || [ $batch = F ]; then
  sleep 60; acquire $dev "chain6_device.sh $dev batch F (cleanup, leftovers)" || exit 2
  bash cleanup6.sh $dev $serial leftovers
  release $dev
fi
