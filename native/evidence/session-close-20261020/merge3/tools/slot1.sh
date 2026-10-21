#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 3, Linux slot 1 (sequential): all-lanes corpus, host gate, .NET build/test/pack (+ managed Linux
# reference films), then the specialization-off measurements (Jeremy's 17:52 decision: once each, declared budget).
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge3; V=$M/validation; S=$M/tools/slot.sh
B=$R/native/work/cycles-build/bin; T=$R/native/tools
cd $R
pl(){ echo "CODEBRIX_CYCLES_PIPELINE_LOG=$V/$1/pipelines.jsonl"; }
$S l1-all-lanes 2700 python3 native/evidence/phaseC-closeout-20261020/closeout/tools/linux_validate.py --tree $R --output $V/l1-all-lanes/out --skip-build
$S l1-host-gate 1500 python3 native/tools/host_gate.py --output $V/l1-host-gate/out
$S l1-dotnet-build 900 dotnet build --configuration Release CodeBrix.Graphics3D.Cycles.slnx
CODEBRIX_CYCLES_REFERENCE_EXPORT=$M/reference/managed-linux-x64 $S l1-dotnet-test 1500 dotnet test --solution CodeBrix.Graphics3D.Cycles.slnx --configuration Release --no-build
$S l1-dotnet-pack 600 dotnet pack --configuration Release --no-build CodeBrix.Graphics3D.Cycles.slnx
env $(pl l1-glass-spec-off) CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0 $S l1-glass-spec-off 1800 $B/codebrix_cycles_glass_render_gate $V/l1-glass-spec-off/run
env $(pl l1-principled-render-off) CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0 $S l1-principled-render-off 1800 $B/codebrix_cycles_principled_render_gate $V/l1-principled-render-off/run
env $(pl l1-nodes-render-off) CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0 $S l1-nodes-render-off 1800 $B/codebrix_cycles_nodes_render_gate $V/l1-nodes-render-off/run
echo "[$(date +%H:%M:%S)] slot1 DONE" >> $M/timeline.log
