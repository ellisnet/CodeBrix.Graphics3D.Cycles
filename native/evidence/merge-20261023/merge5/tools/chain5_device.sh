#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 5, part 2: device chain for a reference device (adapted from merge wave 4's chain_device.sh; nothing is built,
# every input is staged by part 1). Usage: chain5_device.sh DEVICE (samsung | hp)
cd "$(dirname "$0")"; dev=$1; M=$(realpath ..); L=/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/device-locks
ref=$M/reference/linux-x64; V=$M/validation; R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles
case $dev in
  hp) serial=192.168.86.225:5555; rid=android-x64; F="--reference $ref --device-scene"; FM="--reference $ref --device-scene --mesa-scratch-cache";;
  samsung) serial=R52W207XQ1V; rid=android-arm64; F="--reference $ref --device-scene"; FM="--reference $ref --device-scene";;
esac
A="/home/jeremy/Android/Sdk/platform-tools/adb -s $serial"
lg(){ echo "[$(date +%H:%M:%S)] $dev $*" >> $M/timeline.log; }
mkdir $L/$dev 2>/dev/null || { lg "lock HELD, chain not started"; exit 2; }
echo "merge-wave-5 evidence agent $(date -Iseconds) chain5_device.sh $dev" > $L/$dev/owner.txt; lg "lock taken"
echo "[$(date +%H:%M:%S)] DECLARED $dev staging push (stage.py push $dev): budget 1200 s" >> $M/budgets.txt
timeout 1200 python3 stage.py push $dev >> $M/device/$dev-chain.log 2>&1; c=$?; lg "staged exit=$c"
if [ $c -ne 0 ]; then lg "STAGING FAILED: chain stopped (no binary run)"; $A shell 'rm -rf /data/local/tmp/codebrix-m5-*'; rm $L/$dev/owner.txt && rmdir $L/$dev && lg "lock released"; exit 3; fi
run(){ python3 run_corpus.py $dev "$@" >> $M/device/$dev-chain.log 2>&1; lg "corpus $1-$2 exit=$?"; }
T=$V/c-triangle/out/triangle_lighting/run; S=$V/c-shadow-terminator/out/shadow_terminator/run
hostcap(){ python3 replay_host_captures.py $dev linux-triangle 3600 900 $(for c in 0 1 2 3 4 5; do echo tri$c=$T/case-$c; done) >> $M/device/$dev-chain.log 2>&1; lg "hostcap triangle exit=$?"
           python3 replay_host_captures.py $dev linux-terminator 3600 900 $(for c in 0 1 2 3 4 5 6 7 8 9; do echo term$c=$S/case-$c; done) >> $M/device/$dev-chain.log 2>&1; lg "hostcap terminator exit=$?"; }
run materials m5 7200 2400 $FM
run triangle m5 2400 1200 $F
run emitter m5 600 300 $F
run lighting m5 2400 1200 $F --jobs broad_lighting,lighting_mis --replays perjob2
run regression m5 1500 600 $F
run transparent m5 2400 1200 $F
run terminator m5 1800 900 $F
run spec-refusal m5 1800 1200 $F
hostcap
if [ $dev = samsung ]; then
  run pixel-motion own 4800 2400 --jobs object-moving-images,object-stationary-images
  run pixel-motion x64 4800 2400 $F --jobs object-moving-images,object-stationary-images
fi
hg(){ n=$1; echo "[$(date +%H:%M:%S)] DECLARED android host gate on $dev ($n; android_host_gate.py, --gpu-timeout 2400): budget 3000 s" >> $M/budgets.txt
  lg "START android-host-gate $n"; cd $R; s=$(date +%s)
  timeout 3000 python3 native/tools/android_host_gate.py --output $M/device/m5-hostgate-$dev-$n --references $V/a-host-gate/out --device $dev,$rid,$serial > $M/device/hostgate-$dev-$n.log 2>&1; c=$?
  lg "android-host-gate $n END exit=$c $(( $(date +%s)-s ))s"; cd $M/tools; }
hg run1
[ $dev = hp ] && hg run2
echo "[$(date +%H:%M:%S)] DECLARED managed app WebGPU mode on $dev (--linux-reference, result timeout 2400 s, --uninstall): budget 2700 s" >> $M/budgets.txt
lg "START app-webgpu"; cd $R; s=$(date +%s)
timeout 2700 python3 native/tools/android_app_gate.py --build-output $M/android-app/validation-m5b --output $M/android-app/webgpu-$dev --device $dev,$rid,$serial --mode webgpu --linux-reference $M/reference/managed-linux-x64 --result-timeout 2400 --uninstall > $M/device/app-$dev.log 2>&1; c=$?
lg "app-webgpu END exit=$c $(( $(date +%s)-s ))s"; cd $M/tools
$A shell 'rm -rf /data/local/tmp/codebrix-m5-* /data/local/tmp/codebrix-maintained-host-m5-hostgate-*'
left=$($A shell 'ls -d /data/local/tmp/codebrix-m5* /data/local/tmp/codebrix-maintained-host-m5* 2>/dev/null; pm list packages | grep "\.m5$"'; $A shell 'ps -A -o PID,ARGS' | grep -E 'codebrix|render_replay' | grep -v grep)
lg "cleaned (left: '$left')"
rm $L/$dev/owner.txt && rmdir $L/$dev && lg "lock released"
