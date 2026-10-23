#!/bin/bash
# Lane Q: one isolated confirmation run (fresh stage, device idle >= 60 s first) of the jobs that ran directly after a
# device-lost process (transparent_shadow after transparent_lighting; nodes_world after nodes_svm), so their verdicts do not
# depend on a preceding GPU fault. Same runner, same criteria; not a diagnosis.
R=/home/jeremy/ClaudeHome/cycles-worktrees/q-s20-sweep; Q=$R/native/work/q; cd $Q/tools
G=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/merge3; ref=$G/reference/linux-x64
S=RFCRB0MPHYH; A="adb -s $S"; L=/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/device-locks/s20
lg(){ echo "[$(date +%H:%M:%S)] $*" >> $Q/timeline.log; }
mkdir $L 2>/dev/null || { lg "lock s20 HELD; rerun not started"; exit 2; }
echo "lane Q (q-s20-sweep) pid=$$ $(date -Iseconds) rerun_isolated.sh" > $L/owner.txt; lg "lock s20 taken (pid $$) for isolated reruns"
echo "[$(date +%H:%M:%S)] DECLARED s20 staging push (stage.py push s20) timeout=1200s" >> $Q/budgets.txt
timeout 1200 python3 stage.py push s20 > $Q/device/stage-iso.log 2>&1; lg "staged exit=$?"; sleep 60
echo "[$(date +%H:%M:%S)] DECLARED s20 transparent_shadow-iso: run_corpus.py s20 transparent iso budget=1500 jobTimeout=600 timeout=1800s" >> $Q/budgets.txt
lg "START transparent_shadow-iso"; timeout 1800 python3 run_corpus.py s20 transparent iso 1500 600 --reference $ref --device-scene --jobs transparent_shadow > $Q/device/transparent_shadow-iso.log 2>&1; lg "END transparent_shadow-iso exit=$?"; sleep 60
echo "[$(date +%H:%M:%S)] DECLARED s20 nodes_world-iso: run_corpus.py s20 materials nwiso budget=1800 jobTimeout=1529 timeout=2100s" >> $Q/budgets.txt
lg "START nodes_world-iso"; timeout 2100 python3 run_corpus.py s20 materials nwiso 1800 1529 --reference $ref --jobs nodes_world > $Q/device/nodes_world-iso.log 2>&1; lg "END nodes_world-iso exit=$?"
$A shell 'rm -rf /data/local/tmp/codebrix-q-*'
left=$($A shell 'ls -d /data/local/tmp/codebrix-q* 2>/dev/null'; $A shell 'ps -A -o PID,ARGS' | grep -E 'codebrix-q|render_replay' | grep -v grep); lg "device cleaned (left: '$left')"
rm $L/owner.txt && rmdir $L && lg "lock s20 released"
