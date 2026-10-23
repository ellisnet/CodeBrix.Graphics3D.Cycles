#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Lane Q: S20 FE 5G (RFCRB0MPHYH, Adreno 650) sweep chain. One job at a time; per-job timeout = max(600, 3 x tablet wall).
# chain_s20b.sh = chain_s20.sh plus step hostcap2 (Linux captures re-recorded in this worktree; merge3's capture arenas had been deleted).
# Usage: chain_s20.sh [STEP ...]  (default: every step in order). Steps are skipped when runs/s20/<corpus>-<label>/status.json exists.
R=/home/jeremy/ClaudeHome/cycles-worktrees/q-s20-sweep; Q=$R/native/work/q; cd $Q/tools
G=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/merge3   # read-only inputs (Linux references and captures; code identical, docs-only diff)
ref=$G/reference/linux-x64; F="--reference $ref --device-scene"; FM="--reference $ref"
S=RFCRB0MPHYH; A="adb -s $S"; L=/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/device-locks/s20
lg(){ echo "[$(date +%H:%M:%S)] $*" >> $Q/timeline.log; }
decl(){ echo "[$(date +%H:%M:%S)] DECLARED $1 timeout=$2s" >> $Q/budgets.txt; }
ckpt(){ python3 sweep_table.py > /dev/null 2>> $Q/logs/sweep-table.err; python3 status_md.py "$1" >> $Q/logs/sweep-table.err 2>&1; }
health(){ # device still attached, no codebrix process of ours left (exact PIDs only from our active.pid files)
  adb devices | grep -q "^$S\s*device" || { lg "DEVICE NOT ATTACHED after $1; adb reconnect"; adb reconnect >/dev/null 2>&1; sleep 10; adb devices | grep -q "^$S\s*device" || lg "device still missing"; }
  left=$($A shell 'ps -A -o PID,ARGS' | grep -E 'codebrix-q|codebrix-maintained-host-codebrix-q' | grep -v grep); [ -n "$left" ] && lg "WARNING processes left after $1: $left"; }
corpus(){ # name corpus label jobtimeout budget args...
  local name=$1 c=$2 l=$3 jt=$4 b=$5; shift 5
  [ -f $Q/runs/s20/$c-$l/status.json ] && { lg "skip $name (already run)"; return; }
  decl "s20 $name: run_corpus.py s20 $c $l budget=$b jobTimeout=$jt $*" $((b+300))
  lg "START $name"; local s=$(date +%s)
  timeout $((b+300)) python3 run_corpus.py s20 $c $l $b $jt "$@" > $Q/device/$name.log 2>&1; local x=$?
  lg "END $name exit=$x $(( $(date +%s)-s ))s"; health $name; ckpt $name; }
hostcap(){ local name=$1 lab=$2; shift 2; [ -f $Q/runs/s20/hostcap-$lab/status.json ] && { lg "skip $name"; return; }
  lg "START $name"; local s=$(date +%s); timeout 7200 python3 replay_host_captures.py s20 $lab 7000 600 "$@" > $Q/device/$name.log 2>&1
  lg "END $name exit=$? $(( $(date +%s)-s ))s"; health $name; ckpt $name; }
hostgate(){ [ -f $Q/device/codebrix-q-host-gate/status.json ] && { lg "skip host gate"; return; }
  decl "s20 android_host_gate.py (48 exports, --gpu-timeout 600, references $G/validation/l1-host-gate/out; tablet wall 62 s)" 600
  lg "START android-host-gate"; local s=$(date +%s)
  ( cd $R && timeout 600 python3 native/tools/android_host_gate.py --output $Q/device/codebrix-q-host-gate --references $G/validation/l1-host-gate/out --device s20,android-arm64,$S --gpu-timeout 600 ) > $Q/device/android-host-gate.log 2>&1; local x=$?
  lg "END android-host-gate exit=$x $(( $(date +%s)-s ))s"
  if [ $x = 124 ]; then pid=$($A shell 'cat /data/local/tmp/codebrix-maintained-host-codebrix-q-host-gate-s20/active.pid' 2>/dev/null | tr -d '\r');
     exe=$($A shell "readlink /proc/$pid/exe" 2>/dev/null | tr -d '\r'); case "$exe" in /data/local/tmp/codebrix-maintained-host-codebrix-q-host-gate-s20/bin/*) $A shell "kill -KILL $pid"; lg "TIMEOUT host gate at 600 s; killed $pid ($exe)";; esac; fi
  $A shell 'rm -rf /data/local/tmp/codebrix-maintained-host-codebrix-q-host-gate-s20'; health host-gate; ckpt host-gate; }
app(){ [ -f $Q/android-app/webgpu-s20/status.json ] && { lg "skip app"; return; }
  w=0; until grep -q "APK build exit=0" $Q/timeline.log; do grep -q "APK build exit=[1-9]" $Q/timeline.log && { lg "app SKIPPED (APK build failed)"; return; }; w=$((w+15)); [ $w -gt 3600 ] && { lg "app SKIPPED (no APK after 1 h)"; return; }; sleep 15; done
  decl "s20 managed app WebGPU mode (--linux-reference $G/reference/managed-linux-x64, --result-timeout 600, --uninstall; tablet 173 s)" 900
  lg "START app-webgpu"; local s=$(date +%s)
  ( cd $R && timeout 900 python3 native/tools/android_app_gate.py --build-output $Q/android-app/validation-q --output $Q/android-app/webgpu-s20 --device s20,android-arm64,$S --mode webgpu --linux-reference $G/reference/managed-linux-x64 --result-timeout 600 --uninstall ) > $Q/device/app-s20.log 2>&1
  lg "END app-webgpu exit=$? $(( $(date +%s)-s ))s"; $A shell 'pm list packages' | grep -q hostvalidation.q && { $A uninstall com.codebrix.cycles.hostvalidation.q; lg "app uninstalled by chain"; }; health app; ckpt app; }
T=$G/validation/l2-triangle/out/triangle_lighting/run; TS=$G/validation/l2-shadow-terminator/out/shadow_terminator/run
step(){ case $1 in
  stage) decl "s20 staging push (stage.py push s20)" 1200; timeout 1200 python3 stage.py push s20 > $Q/device/stage.log 2>&1; lg "staged exit=$?";;
  hostgate) hostgate;;
  canary) corpus canary canary q 600 900 $F;;
  triangle) corpus triangle triangle q 600 6000 $F;;
  emitter) corpus emitter emitter q 600 900 $F;;
  lighting) corpus lighting lighting q 600 4200 $F --jobs lighting_mis,broad_lighting --replays perjob2;;
  regression) corpus regression regression q 600 2100 $F;;
  transparent) corpus transparent transparent q 600 7800 $F;;
  terminator) corpus terminator terminator q 600 7200 $F;;
  specrefusal) corpus spec-refusal spec-refusal q 600 1500 $F;;
  featurecomb) corpus feature_combination lighting fc 600 2100 $F --jobs feature_combination --replays perjob2;;
  motion) corpus motion motion q 600 4200 $F --jobs object-moving-images,deforming-moving-images,camera-static,camera-motion --replays perjob1;;
  precision) corpus precision precision q 600 2700 $F;;
  volume) corpus volume volume q 600 7200 $F --jobs volume-state,volume-absorption,volume-isotropic,volume-forward,volume-backward,boundary-state,boundary-compaction-guard
          corpus closed-single volume cs 643 1600 $F --jobs closed-single
          corpus closed-overlap volume co 960 2100 $F --jobs closed-overlap
          corpus closed-inside volume ci 909 2100 $F --jobs closed-inside;;
  hostcap) hostcap hostcap-linux-triangle linux-triangle $(for c in 0 1 2 3 4 5; do echo tri$c=$T/case-$c; done)
           hostcap hostcap-linux-terminator linux-terminator $(for c in 0 1 2 3 4 5 6 7 8 9; do echo term$c=$TS/case-$c; done);;
  hostcap2) T2=$Q/validation/linux-triangle_lighting/out/triangle_lighting/run; TS2=$Q/validation/linux-shadow_terminator/out/shadow_terminator/run
           hostcap hostcap-linux-triangle-q2 linux-triangle-q2 $(for c in 0 1 2 3 4 5; do echo tri$c=$T2/case-$c; done)
           hostcap hostcap-linux-terminator-q2 linux-terminator-q2 $(for c in 0 1 2 3 4 5 6 7 8 9; do echo term$c=$TS2/case-$c; done);;
  app) app;;
  materials) corpus principled materials pr 600 2700 $FM --jobs principled_svm,principled_bounce,principled_render
             corpus nodes_svm materials nsvm 2638 2900 $FM --jobs nodes_svm
             corpus nodes_world materials nw 1529 1800 $FM --jobs nodes_world
             corpus nodes_render materials nr 600 1500 $FM --jobs nodes_render;;
  esac; }
mkdir $L 2>/dev/null || { lg "lock s20 HELD by $(cat $L/owner.txt 2>/dev/null); chain not started"; exit 2; }
echo "lane Q (q-s20-sweep) pid=$$ $(date -Iseconds) chain_s20.sh $*" > $L/owner.txt; lg "lock s20 taken (pid $$)"
STEPS=${*:-stage hostgate canary triangle emitter lighting regression transparent terminator specrefusal featurecomb motion precision volume hostcap app materials}
for st in $STEPS; do step $st; done
$A shell 'rm -rf /data/local/tmp/codebrix-q-* /data/local/tmp/codebrix-maintained-host-codebrix-q-*'
left=$($A shell 'ls -d /data/local/tmp/codebrix-q* /data/local/tmp/codebrix-maintained-host-codebrix-q* 2>/dev/null; pm list packages | grep hostvalidation.q'; $A shell 'ps -A -o PID,ARGS' | grep -E 'codebrix-q|render_replay' | grep -v grep)
lg "device cleaned (left: '$left')"
rm $L/owner.txt && rmdir $L && lg "lock s20 released"
