#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 5, Linux slot B: node and Principled gates, device-tier gate, transparent-shadow gate, the simulated
# Adreno 650 refusal check (lane A650), pipeline size guard check, triangle sampler gate, probe gates.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge5; V=$M/validation; S=$M/tools/slot.sh
T=$R/native/tools; B=$R/native/work/cycles-build/bin
A650='Qualcomm|adreno|Adreno (TM) 650|Driver Build: test, Compiler Version: EV031.32.02.10|0x5143|0x6050002'
cd $R
$S e-nodes-gate 3600 python3 $T/nodes_gate.py --skip-build --output $V/e-nodes-gate/out
$S e-principled-gate 2700 python3 $T/principled_gate.py --skip-build --output $V/e-principled-gate/out
$S e-tier-gate 1800 python3 $T/tier_gate.py --skip-build --output $V/e-tier-gate/out
$S e-transparent-shadow 900 python3 $T/transparent_shadow_gate.py --skip-build --output $V/e-transparent-shadow/out
CODEBRIX_CYCLES_TEST_ADAPTER="$A650" $S e-sim650-nodes-world 300 $B/codebrix_cycles_nodes_world_gate $V/e-sim650-nodes-world/run
CODEBRIX_CYCLES_TEST_ADAPTER="$A650" $S e-sim650-glass-render 900 $B/codebrix_cycles_glass_render_gate $V/e-sim650-glass-render/run
$S e-size-guard 1800 python3 $T/pipeline_size_guard_check.py --output $V/e-size-guard/out
$S e-triangle-sample 900 $B/codebrix_cycles_triangle_sample_gate $V/e-triangle-sample/run
$S e-svm-surface 900 $B/codebrix_cycles_svm_surface_gate $V/e-svm-surface/run
$S e-svm-world 900 $B/codebrix_cycles_svm_world_gate $V/e-svm-world/run
for g in normal attribute image image_file shader_data; do $S e-probe-$g 1200 $B/codebrix_cycles_${g}_gate $V/e-probe-$g/run; done
echo "[$(date +%H:%M:%S)] slotE_B DONE" >> $M/timeline.log
