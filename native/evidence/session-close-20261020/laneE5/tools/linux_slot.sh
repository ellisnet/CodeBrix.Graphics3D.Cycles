#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Phase E5 Linux validation of the final tree, sequential (one GPU gate at a time).
R=/home/jeremy/ClaudeHome/cycles-worktrees/e5-pixel-sampler; M=$R/native/work/e5; V=$M/linux; S=$M/tools/slot.sh; T=$R/native/tools; B=$R/native/work/cycles-build/bin
cd $R
$S triangle 900 python3 $T/triangle_lighting_gate.py --skip-build --output $V/triangle/out
$S emitter-sample 600 $B/codebrix_cycles_emitter_sample_gate $V/emitter-sample/run
$S all-lanes 2700 python3 native/evidence/phaseC-closeout-20261020/closeout/tools/linux_validate.py --tree $R --output $V/all-lanes/out --skip-build
$S glass-spec-on 900 $B/codebrix_cycles_glass_render_gate $V/glass-spec-on/run
CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0 $S glass-spec-off 900 $B/codebrix_cycles_glass_render_gate $V/glass-spec-off/run
$S host-gate 1500 python3 native/tools/host_gate.py --output $V/host-gate/out
$S dotnet-build 900 dotnet build --configuration Release CodeBrix.Graphics3D.Cycles.slnx
$S dotnet-test 1500 dotnet test --solution CodeBrix.Graphics3D.Cycles.slnx --configuration Release --no-build
echo "[$(date +%H:%M:%S)] linux slot DONE" >> $M/timeline.log
