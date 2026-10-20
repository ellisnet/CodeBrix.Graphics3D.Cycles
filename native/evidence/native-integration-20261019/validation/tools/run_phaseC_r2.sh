#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Phase C Linux validation: each corpus once with a fresh empty Mesa shader cache.
cd /home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles
V=$PWD/native/work/phaseC/05-validation-r2; T=$PWD/native/work/phaseC/tools; BIN=$PWD/native/work/cycles-build/bin
log(){ echo "[$(date +%H:%M:%S)] $*" | tee -a $V/run.log; }
run(){ # name budget_seconds command...
  local name=$1 budget=$2; shift 2; mkdir -p $V/mesa/$name
  log "START $name budget=${budget}s"
  local t0=$(date +%s)
  MESA_SHADER_CACHE_DIR=$V/mesa/$name CODEBRIX_CYCLES_PIPELINE_LOG=$V/pipelines-$name.jsonl timeout --signal=TERM --kill-after=30 $budget "$@" > $V/$name.stdout.txt 2> $V/$name.stderr.txt
  local rc=$?; local dt=$(( $(date +%s)-t0 ))
  [ $rc = 124 ] && log "TIMEOUT $name after ${dt}s (budget exceeded; partial output kept)"
  log "END $name rc=$rc seconds=$dt"; echo "{\"name\":\"$name\",\"rc\":$rc,\"seconds\":$dt,\"budgetSeconds\":$budget}" >> $V/runs.jsonl
}
G='--gate glass_svm=glass_svm --gate glass_bounce=glass_bounce --gate glass_render=glass_render --gate microfacet_svm=microfacet_svm --gate microfacet_bounce=microfacet_bounce --gate microfacet_render=microfacet_render --gate shader_data=shader_data --gate closest=closest --gate feature_combination=feature_combination'
run all-lanes 2700 python3 $T/linux_validate.py --tree . --output $V/all-lanes --skip-build
E='--gate attribute=attribute --gate render-attributes=render:--attributes --gate image=image --gate render-images=render:--images --gate normal=normal --gate render-normals=render:--normals --gate image_file=image_file --gate svm_world=svm_world --gate background=background --gate render=render --gate surface=surface --gate closest=closest --gate svm_surface=svm_surface --gate shader_data=shader_data --gate camera=camera'
run triangle 900 python3 native/tools/triangle_lighting_gate.py --skip-build --output $V/triangle
run close-set 900 python3 $T/restored_regressions_phaseC.py $V/close-set
run shadow 900 python3 native/tools/shadow_gate.py --skip-build --output $V/shadow
CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0 run glass-render-unspecialized 900 python3 $T/gate_binaries.py --bin $BIN --output $V/glass-render-unspecialized --budget 900 --gate glass_render=glass_render
run host 1200 python3 native/tools/host_gate.py --output $V/host --jobs 8
log "ALL DONE"
