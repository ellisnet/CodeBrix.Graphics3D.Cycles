#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 2, Linux slot 2 (sequential): material gates (M1/M2/M3), spec on/off identities, replays, SVM gates,
# and the Linux reference export for the device runs (X1 policy).
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge2; V=$M/validation; S=$M/tools/slot.sh
T=$R/native/tools; B=$R/native/work/cycles-build/bin
cd $R
pl(){ echo "CODEBRIX_CYCLES_PIPELINE_LOG=$V/$1/pipelines.jsonl"; }
$S l2-principled-gate 1200 python3 $T/principled_gate.py --skip-build --output $V/l2-principled-gate/out
$S l2-nodes-gate 2400 python3 $T/nodes_gate.py --skip-build --output $V/l2-nodes-gate/out
$S l2-unspecialized-smoke 2400 python3 $T/unspecialized_compile_smoke.py --wgsl $V/l2-nodes-gate/out/nodes_render-unspecialized/surface.wgsl --output $V/l2-unspecialized-smoke/out
env $(pl l2-principled-render-on) $S l2-principled-render-on 900 $B/codebrix_cycles_principled_render_gate $V/l2-principled-render-on/run
env $(pl l2-principled-render-off) CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0 $S l2-principled-render-off 1800 $B/codebrix_cycles_principled_render_gate $V/l2-principled-render-off/run
$S l2-principled-replay 900 python3 $M/tools/replay.py $V/l2-principled-render-on $V/l2-principled-replay/out
$S l2-nodes-replay 900 python3 $M/tools/replay.py $V/l2-nodes-gate/out/nodes_render $V/l2-nodes-replay/out
env $(pl l2-glass-spec-on) $S l2-glass-spec-on 900 $B/codebrix_cycles_glass_render_gate $V/l2-glass-spec-on/run
env $(pl l2-glass-spec-off) CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0 $S l2-glass-spec-off 1800 $B/codebrix_cycles_glass_render_gate $V/l2-glass-spec-off/run
env $(pl l2-principled-bench) $S l2-principled-bench 2400 $B/codebrix_cycles_principled_render_gate $V/l2-principled-bench/run $R/native/work/render/bench/triangles.bin
$S l2-svm-surface 900 $B/codebrix_cycles_svm_surface_gate $V/l2-svm-surface/run
$S l2-svm-world 900 $B/codebrix_cycles_svm_world_gate $V/l2-svm-world/run
$S l2-surface 900 $B/codebrix_cycles_surface_gate $V/l2-surface/run
# Probe gates now compiling scene-specialized probe modules (merge wave 2 option 1)
for g in normal attribute image image_file shader_data; do $S l2-probe-$g 1200 $B/codebrix_cycles_${g}_gate $V/l2-probe-$g/run; done
$S l2-libm-linux 900 python3 $T/libm_probe.py linux $M/reference/libm-linux-x64-glibc.json
$S l2-reference-export 2400 python3 $T/android_reference.py export --bin $B --libm-record $M/reference/libm-linux-x64-glibc.json --output $M/reference/linux-x64 --build-record $R/native/work/cycles-build.json --job glass_render=glass_render --job principled_render=principled_render --job nodes_render=nodes_render
echo "[$(date +%H:%M:%S)] slot2 DONE" >> $M/timeline.log
