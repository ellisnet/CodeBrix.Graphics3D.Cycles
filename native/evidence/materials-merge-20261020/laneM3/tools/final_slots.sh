#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# M3 final validation slots (A: all-lanes + host gate; B: material gates + extras).
R=/home/jeremy/ClaudeHome/cycles-worktrees/m3-procedural; F=$R/native/work/m3/validation/final; T=$R/native/work/m3/tools
cd $R
log(){ echo "[$(date +%H:%M:%S)] $*" >> $F/$1.txt; }
if [ "$1" = A ]; then
  export MESA_SHADER_CACHE_DIR=$R/native/work/m3/mesa-fa; rm -rf $MESA_SHADER_CACHE_DIR; mkdir -p $MESA_SHADER_CACHE_DIR
  s=$(date +%s); python3 $T/linux_validate.py --tree . --output $F/all-lanes --skip-build > $F/all-lanes.log 2>&1; c=$?; echo "[$(date +%H:%M:%S)] all-lanes exit=$c seconds=$(( $(date +%s)-s ))" >> $F/slotA.txt
  rm -rf $MESA_SHADER_CACHE_DIR; mkdir -p $MESA_SHADER_CACHE_DIR
  s=$(date +%s); timeout 2400 python3 native/tools/host_gate.py --output $F/host-gate > $F/host-gate.log 2>&1; c=$?; echo "[$(date +%H:%M:%S)] host_gate exit=$c seconds=$(( $(date +%s)-s ))" >> $F/slotA.txt
else
  export MESA_SHADER_CACHE_DIR=$R/native/work/m3/mesa-fb; rm -rf $MESA_SHADER_CACHE_DIR; mkdir -p $MESA_SHADER_CACHE_DIR
  s=$(date +%s); python3 native/tools/nodes_gate.py --skip-build --output $F/nodes-gate > $F/nodes-gate.log 2>&1; c=$?; echo "[$(date +%H:%M:%S)] nodes_gate exit=$c seconds=$(( $(date +%s)-s ))" >> $F/slotB.txt
  rm -rf $MESA_SHADER_CACHE_DIR; mkdir -p $MESA_SHADER_CACHE_DIR
  s=$(date +%s); python3 native/tools/principled_gate.py --skip-build --output $F/principled-gate > $F/principled-gate.log 2>&1; c=$?; echo "[$(date +%H:%M:%S)] principled_gate exit=$c seconds=$(( $(date +%s)-s ))" >> $F/slotB.txt
  unset MESA_SHADER_CACHE_DIR
  bash $T/run_gate.sh f-prin $F/extra/principled_render 900 codebrix_cycles_principled_render_gate >> $F/slotB.txt
  CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0 bash $T/run_gate.sh f-prin-off $F/extra/principled_render_specoff 1800 codebrix_cycles_principled_render_gate >> $F/slotB.txt
  timeout 900 python3 $T/replay.py $F/extra/principled_render $F/extra/principled_render_replay > $F/extra/principled_replay.log 2>&1; echo "[$(date +%H:%M:%S)] principled replay exit=$?" >> $F/slotB.txt
  timeout 900 python3 $T/replay.py $F/nodes-gate/nodes_render $F/extra/nodes_render_replay > $F/extra/nodes_replay.log 2>&1; echo "[$(date +%H:%M:%S)] nodes replay exit=$?" >> $F/slotB.txt
  bash $T/run_gate.sh f-glass $F/extra/glass_render 900 codebrix_cycles_glass_render_gate >> $F/slotB.txt
  CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0 bash $T/run_gate.sh f-glass-off $F/extra/glass_render_specoff 1800 codebrix_cycles_glass_render_gate >> $F/slotB.txt
  bash $T/run_gate.sh f-svm $F/extra/svm_surface 900 codebrix_cycles_svm_surface_gate >> $F/slotB.txt
  echo "[$(date +%H:%M:%S)] slotB done" >> $F/slotB.txt
fi
