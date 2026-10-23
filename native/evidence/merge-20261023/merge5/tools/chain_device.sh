#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 4 device chain (reference devices). Usage: chain_device.sh DEVICE (samsung | hp)
# Lock protocol: mkdir the lock by device name (stop if held), owner.txt, stage + verify, corpora (budgets are declared
# by run_corpus.py / replay_host_captures.py before each run), Android host gate, managed app, clean, release the lock.
cd "$(dirname "$0")"; dev=$1; M=$(realpath ..); L=/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/device-locks
ref=$M/reference/linux-x64; V=$M/validation; R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles
case $dev in
  hp) serial=192.168.86.225:5555; rid=android-x64; F="--reference $ref --device-scene"; FM="--reference $ref --device-scene --mesa-scratch-cache";;
  samsung) serial=R52W207XQ1V; rid=android-arm64; F="--reference $ref --device-scene"; FM="--reference $ref --device-scene";;
esac
A="/home/jeremy/Android/Sdk/platform-tools/adb -s $serial"
lg(){ echo "[$(date +%H:%M:%S)] $dev $*" >> $M/timeline.log; }
waitfor(){ until grep -q "$1" $M/timeline.log; do grep -q "$2" $M/timeline.log && return 1; sleep 15; done; }
waitfor "review $rid exit=0" "android gate build $rid exit=[1-9]" || { lg "chain not started (android build failed)"; exit 1; }
waitfor "END l2-reference-export exit=0" "END l2-reference-export exit=[1-9]" || { lg "chain not started (reference export failed)"; exit 1; }
mkdir $L/$dev 2>/dev/null || { lg "lock HELD, chain not started"; exit 2; }
echo "merge-wave-4 agent (merge S+L+P into main) $(date -Iseconds) chain_device.sh $dev" > $L/$dev/owner.txt; lg "lock taken"
echo "[$(date +%H:%M:%S)] DECLARED $dev staging push (stage.py push $dev): budget 1200 s" >> $M/budgets.txt
timeout 1200 python3 stage.py push $dev >> $M/device/$dev-chain.log 2>&1; lg "staged exit=$?"
run(){ python3 run_corpus.py $dev "$@" >> $M/device/$dev-chain.log 2>&1; lg "corpus $1-$2 exit=$?"; }
T=$V/l2-triangle/out/triangle_lighting/run; S=$V/l2-shadow-terminator/out/shadow_terminator/run
hostcap(){ waitfor "END l2-shadow-terminator exit=0" "END l2-shadow-terminator exit=[1-9]" || { lg "hostcap skipped (terminator gate failed)"; return; }
           python3 replay_host_captures.py $dev linux-triangle 3600 900 $(for c in 0 1 2 3 4 5; do echo tri$c=$T/case-$c; done) >> $M/device/$dev-chain.log 2>&1; lg "hostcap triangle exit=$?"
           python3 replay_host_captures.py $dev linux-terminator 3600 900 $(for c in 0 1 2 3 4 5 6 7 8 9; do echo term$c=$S/case-$c; done) >> $M/device/$dev-chain.log 2>&1; lg "hostcap terminator exit=$?"; }
run materials m4 7200 2400 $FM
run triangle m4 2400 1200 $F
run emitter m4 600 300 $F
run lighting m4 2400 1200 $F --jobs broad_lighting,lighting_mis --replays perjob2
run regression m4 1500 600 $F
run transparent m4 2400 1200 $F
run terminator m4 1800 900 $F
run spec-refusal m4 1800 1200 $F
hostcap
if [ $dev = samsung ]; then
  run pixel-motion own 4800 2400 --jobs object-moving-images,object-stationary-images
  run pixel-motion x64 4800 2400 $F --jobs object-moving-images,object-stationary-images
fi
# Android host gate (48-export Linux host-gate references), once the host gate and the Android host builds exist.
if waitfor "APK build exit=" "slots NOT started"; then
  echo "[$(date +%H:%M:%S)] DECLARED android host gate on $dev (android_host_gate.py, --gpu-timeout 2400): budget 3000 s" >> $M/budgets.txt
  lg "START android-host-gate"; cd $R; s=$(date +%s)
  timeout 3000 python3 native/tools/android_host_gate.py --output $M/device/m4-hostgate-$dev --references $V/l1-host-gate/out --device $dev,$rid,$serial > $M/device/hostgate-$dev.log 2>&1; c=$?
  lg "android-host-gate END exit=$c $(( $(date +%s)-s ))s"; cd $M/tools
fi
bash chain_app.sh $dev $rid $serial "APK build exit=0" >> $M/device/$dev-chain.log 2>&1
$A shell 'rm -rf /data/local/tmp/codebrix-m5-* /data/local/tmp/codebrix-maintained-host-m5-hostgate-*'
left=$($A shell 'ls -d /data/local/tmp/codebrix-m5* /data/local/tmp/codebrix-maintained-host-m5* 2>/dev/null; pm list packages | grep "\.m4$"'; $A shell 'ps -A -o PID,ARGS' | grep -E 'codebrix|render_replay' | grep -v grep)
lg "cleaned (left: '$left')"
rm $L/$dev/owner.txt && rmdir $L/$dev && lg "lock released"
