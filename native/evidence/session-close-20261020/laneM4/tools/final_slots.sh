#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# M4 final validation slots (M3 copy, trimmed to the box). A: all-lanes + host gate; B: nodes, glass on/off, replay, principled.
R=/home/jeremy/ClaudeHome/cycles-worktrees/m4-bump-derivative; F=$R/native/work/m4/validation/final; T=$R/native/work/m4/tools
cd $R; mkdir -p $F/extra
if [ "$1" = A ]; then
  export MESA_SHADER_CACHE_DIR=$R/native/work/m4/mesa-fa; rm -rf $MESA_SHADER_CACHE_DIR; mkdir -p $MESA_SHADER_CACHE_DIR
  s=$(date +%s); python3 $T/linux_validate.py --tree . --output $F/all-lanes --skip-build > $F/all-lanes.log 2>&1; c=$?; echo "[$(date +%H:%M:%S)] all-lanes exit=$c seconds=$(( $(date +%s)-s ))" >> $F/slotA.txt
  rm -rf $MESA_SHADER_CACHE_DIR; mkdir -p $MESA_SHADER_CACHE_DIR
  s=$(date +%s); timeout 2400 python3 native/tools/host_gate.py --output $F/host-gate > $F/host-gate.log 2>&1; c=$?; echo "[$(date +%H:%M:%S)] host_gate exit=$c seconds=$(( $(date +%s)-s ))" >> $F/slotA.txt
  rm -rf $MESA_SHADER_CACHE_DIR
else
  export MESA_SHADER_CACHE_DIR=$R/native/work/m4/mesa-fb; rm -rf $MESA_SHADER_CACHE_DIR; mkdir -p $MESA_SHADER_CACHE_DIR
  s=$(date +%s); python3 native/tools/nodes_gate.py --skip-build --output $F/nodes-gate > $F/nodes-gate.log 2>&1; c=$?; echo "[$(date +%H:%M:%S)] nodes_gate exit=$c seconds=$(( $(date +%s)-s ))" >> $F/slotB.txt
  unset MESA_SHADER_CACHE_DIR
  bash $T/run_gate.sh f-glass $F/extra/glass_render 900 codebrix_cycles_glass_render_gate >> $F/slotB.txt
  CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0 bash $T/run_gate.sh f-glass-off $F/extra/glass_render_specoff 1800 codebrix_cycles_glass_render_gate >> $F/slotB.txt
  timeout 900 python3 $T/replay.py $F/nodes-gate/nodes_render $F/extra/nodes_render_replay > $F/extra/nodes_replay.log 2>&1; echo "[$(date +%H:%M:%S)] nodes replay exit=$?" >> $F/slotB.txt
  export MESA_SHADER_CACHE_DIR=$R/native/work/m4/mesa-fb; rm -rf $MESA_SHADER_CACHE_DIR; mkdir -p $MESA_SHADER_CACHE_DIR
  s=$(date +%s); python3 native/tools/principled_gate.py --skip-build --output $F/principled-gate > $F/principled-gate.log 2>&1; c=$?; echo "[$(date +%H:%M:%S)] principled_gate exit=$c seconds=$(( $(date +%s)-s ))" >> $F/slotB.txt
  rm -rf $MESA_SHADER_CACHE_DIR
  echo "[$(date +%H:%M:%S)] slotB done" >> $F/slotB.txt
fi
