#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 3 device chain. Usage: chain_device.sh DEVICE (hp | samsung | pixel)
# Lock protocol: mkdir the lock (stop if held), owner.txt, stage + verify, corpora (budgets are declared by
# run_corpus.py / replay_host_captures.py before each run), managed app, clean the device, release the lock.
cd "$(dirname "$0")"; dev=$1; M=$(realpath ..); L=/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/device-locks
ref=$M/reference/linux-x64; V=$M/validation
case $dev in
  hp) serial=192.168.86.225:5555; rid=android-x64; F="--reference $ref --device-scene"; FM="--reference $ref --mesa-scratch-cache";;
  samsung) serial=R52W207XQ1V; rid=android-arm64; F="--reference $ref --device-scene"; FM="--reference $ref";;
  pixel) serial=98271FFBA0048D; rid=android-arm64; F="--reference $ref --device-scene --pipeline-budget-ms 20000"; FM="--reference $ref --pipeline-budget-ms 20000";;
esac
A="/home/jeremy/Android/Sdk/platform-tools/adb -s $serial"
lg(){ echo "[$(date +%H:%M:%S)] $dev $*" >> $M/timeline.log; }
mkdir $L/$serial 2>/dev/null || { lg "lock HELD, chain not started"; exit 2; }
echo "merge-wave-3 agent (session-close merge E4/E5/M4) $(date -Iseconds) chain_device.sh $dev" > $L/$serial/owner.txt; lg "lock taken"
echo "[$(date +%H:%M:%S)] DECLARED $dev staging push (stage.py push $dev): budget 1200 s" >> $M/budgets.txt
timeout 1200 python3 stage.py push $dev >> $M/device/$dev-chain.log 2>&1; lg "staged exit=$?"
run(){ python3 run_corpus.py $dev "$@" >> $M/device/$dev-chain.log 2>&1; lg "corpus $1 exit=$?"; }
T=$V/l2-triangle/out/triangle_lighting/run; S=$V/l2-shadow-terminator/out/shadow_terminator/run
hostcap(){ until grep -q "END l2-shadow-terminator exit=0" $M/timeline.log; do grep -q "END l2-shadow-terminator exit=[1-9]" $M/timeline.log && { lg "hostcap skipped (terminator gate failed)"; return; }; sleep 15; done
           python3 replay_host_captures.py $dev linux-triangle 3600 900 $(for c in 0 1 2 3 4 5; do echo tri$c=$T/case-$c; done) >> $M/device/$dev-chain.log 2>&1; lg "hostcap triangle exit=$?"
           python3 replay_host_captures.py $dev linux-terminator 3600 900 $(for c in 0 1 2 3 4 5 6 7 8 9; do echo term$c=$S/case-$c; done) >> $M/device/$dev-chain.log 2>&1; lg "hostcap terminator exit=$?"; }
case $dev in
  hp)
    run triangle mw3 2400 1200 $F
    run emitter mw3 600 300 $F
    run lighting mw3 2400 1200 $F --jobs broad_lighting,lighting_mis --replays perjob2
    run transparent mw3 2400 1200 $F
    run terminator mw3 1800 900 $F
    run regression mw3 1500 600 $F
    run spec-refusal mw3 1800 1200 $F
    hostcap
    run materials mw3 7200 2400 $FM --jobs nodes_svm,nodes_world,nodes_render;;
  samsung)
    run triangle mw3 2400 900 $F
    run emitter mw3 600 300 $F
    hostcap
    run materials mw3 7200 2400 $FM --jobs nodes_svm,nodes_world,nodes_render;;
  pixel)
    run triangle mw3 7200 2400 $F
    run emitter mw3 1200 600 $F
    run pixel-lighting mw3 4800 2400 $F --jobs feature_combination --replays perjob1
    run pixel-motion mw3 4800 2400 $F --jobs object-moving-images --replays perjob1
    run materials mw3 7200 2400 $FM --jobs nodes_svm,nodes_world,nodes_render;;
esac
bash chain_app.sh $dev $rid $serial "APK build exit=0" >> $M/device/$dev-chain.log 2>&1
$A shell 'rm -rf /data/local/tmp/codebrix-mw3-*'
left=$($A shell 'ls -d /data/local/tmp/codebrix-mw3* 2>/dev/null; pm list packages | grep mw3'; $A shell 'ps -A -o PID,ARGS' | grep -E 'codebrix|render_replay' | grep -v grep)
lg "cleaned (left: '$left')"
rm $L/$serial/owner.txt && rmdir $L/$serial && lg "lock released"
