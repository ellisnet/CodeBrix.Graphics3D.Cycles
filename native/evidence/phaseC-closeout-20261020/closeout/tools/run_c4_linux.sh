#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Phase C close-out Linux validation (pattern of C3 run_c3.sh / C1 run_phaseC_r2.sh): every corpus with a
# fresh, empty Mesa shader cache; declared budgets; two lanes at most.
cd /home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles
V=$PWD/native/work/phaseC4/03-linux-validation; T=$PWD/native/work/phaseC4/tools; BIN=$PWD/native/work/cycles-build/bin
mkdir -p $V
log(){ echo "[$(date +%H:%M:%S)] $*" | tee -a $V/run.log; }
run(){ # name budget_seconds command...
  local name=$1 budget=$2; shift 2; mkdir -p $V/mesa/$name
  echo "[$(date +%H:%M:%S)] DECLARED linux $name: ${budget} s (cold MESA_SHADER_CACHE_DIR=$V/mesa/$name)" >> $PWD/native/work/phaseC4/budgets.txt
  log "START $name budget=${budget}s"
  local t0=$(date +%s)
  MESA_SHADER_CACHE_DIR=$V/mesa/$name CODEBRIX_CYCLES_PIPELINE_LOG=$V/pipelines-$name.jsonl timeout --signal=TERM --kill-after=30 $budget "$@" > $V/$name.stdout.txt 2> $V/$name.stderr.txt
  local rc=$?; local dt=$(( $(date +%s)-t0 ))
  [ $rc = 124 ] && log "TIMEOUT $name after ${dt}s (budget exceeded; partial output kept)"
  log "END $name rc=$rc seconds=$dt"; echo "{\"name\":\"$name\",\"rc\":$rc,\"seconds\":$dt,\"budgetSeconds\":$budget}" >> $V/runs.jsonl
}
runwarm(){ # warm user Mesa cache (legacy v5 replay only)
  local name=$1 budget=$2; shift 2
  echo "[$(date +%H:%M:%S)] DECLARED linux $name: ${budget} s (WARM user Mesa cache)" >> $PWD/native/work/phaseC4/budgets.txt
  log "START $name budget=${budget}s (warm user cache)"
  local t0=$(date +%s)
  CODEBRIX_CYCLES_PIPELINE_LOG=$V/pipelines-$name.jsonl timeout --signal=TERM --kill-after=30 $budget "$@" > $V/$name.stdout.txt 2> $V/$name.stderr.txt
  local rc=$?; local dt=$(( $(date +%s)-t0 ))
  [ $rc = 124 ] && log "TIMEOUT $name after ${dt}s"
  log "END $name rc=$rc seconds=$dt"; echo "{\"name\":\"$name\",\"rc\":$rc,\"seconds\":$dt,\"budgetSeconds\":$budget,\"warmUserCache\":true}" >> $V/runs.jsonl
}
laneA(){ run all-lanes 2700 python3 $T/linux_validate.py --tree . --output $V/all-lanes --skip-build; }
laneB(){
  run triangle 900 python3 native/tools/triangle_lighting_gate.py --skip-build --output $V/triangle
  run close-set 900 python3 $T/restored_regressions_c3.py $V/close-set
  run legacy-v5-cold 600 python3 $T/legacy_v5.py $V/legacy-v5-cold
  runwarm legacy-v5-warm 600 python3 $T/legacy_v5.py $V/legacy-v5-warm
  run shadow 900 python3 native/tools/shadow_gate.py --skip-build --output $V/shadow
  run glass-render-specialized 900 python3 $T/gate_binaries.py --bin $BIN --output $V/glass-render-specialized --budget 900 --gate glass_render=glass_render
  CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0 run glass-render-unspecialized 900 python3 $T/gate_binaries.py --bin $BIN --output $V/glass-render-unspecialized --budget 900 --gate glass_render=glass_render
  run tier 900 python3 native/tools/tier_gate.py --skip-build --output $V/tier
}
log "C4 validation start"
laneA & A=$!
laneB
wait $A
run closest-queue 600 python3 $T/closest_queue_c4.py $V/closest-queue
run host 1200 python3 native/tools/host_gate.py --output $V/host --jobs 6
log "ALL DONE"
