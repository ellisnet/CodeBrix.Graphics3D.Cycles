#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 6 part 2, Linux slot B: node and Principled gates, device-tier gate, transparent-shadow gate, the simulated
# Adreno 650 refusal check (lane A650), pipeline size guard check, triangle sampler gate, probe gates.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge6; V=$M/validation; S=$M/tools/slot.sh
T=$R/native/tools; B=$R/native/work/cycles-build/bin
A650='Qualcomm|adreno|Adreno (TM) 650|Driver Build: test, Compiler Version: EV031.32.02.10|0x5143|0x6050002'
cd $R
$S p2-nodes-gate 3600 python3 $T/nodes_gate.py --skip-build --output $V/p2-nodes-gate/out
$S p2-principled-gate 2700 python3 $T/principled_gate.py --skip-build --output $V/p2-principled-gate/out
$S p2-tier-gate 1800 python3 $T/tier_gate.py --skip-build --output $V/p2-tier-gate/out
$S p2-transparent-shadow 900 python3 $T/transparent_shadow_gate.py --skip-build --output $V/p2-transparent-shadow/out
CODEBRIX_CYCLES_TEST_ADAPTER="$A650" $S p2-sim650-nodes-world 300 $B/codebrix_cycles_nodes_world_gate $V/p2-sim650-nodes-world/run
CODEBRIX_CYCLES_TEST_ADAPTER="$A650" $S p2-sim650-glass-render 900 $B/codebrix_cycles_glass_render_gate $V/p2-sim650-glass-render/run
$S p2-size-guard 1800 python3 $T/pipeline_size_guard_check.py --output $V/p2-size-guard/out
$S p2-triangle-sample 900 $B/codebrix_cycles_triangle_sample_gate $V/p2-triangle-sample/run
$S p2-svm-surface 900 $B/codebrix_cycles_svm_surface_gate $V/p2-svm-surface/run
$S p2-svm-world 900 $B/codebrix_cycles_svm_world_gate $V/p2-svm-world/run
for g in normal attribute image image_file shader_data; do $S p2-probe-$g 1200 $B/codebrix_cycles_${g}_gate $V/p2-probe-$g/run; done
echo "[$(date +%H:%M:%S)] slotP_B DONE" >> $M/timeline.log
