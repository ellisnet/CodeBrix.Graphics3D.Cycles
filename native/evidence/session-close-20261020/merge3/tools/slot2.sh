#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 3, Linux slot 2 (sequential): E4/E5 triangle + emitter + precision + terminator + scene-edit gates,
# glass spec on, material gates (M1-M4, specialized), replays, probe gates, the Linux reference export for the devices.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge3; V=$M/validation; S=$M/tools/slot.sh
T=$R/native/tools; B=$R/native/work/cycles-build/bin
cd $R
pl(){ echo "CODEBRIX_CYCLES_PIPELINE_LOG=$V/$1/pipelines.jsonl"; }
$S l2-libm-linux 900 python3 $T/libm_probe.py linux $M/reference/libm-linux-x64-glibc.json
$S l2-reference-export 2400 python3 $T/android_reference.py export --bin $B --libm-record $M/reference/libm-linux-x64-glibc.json --output $M/reference/linux-x64 --build-record $R/native/work/cycles-build.json --job triangle_sample=triangle_sample --job emitter_sample=emitter_sample --job triangle_lighting=triangle_lighting --job glass_render=glass_render --job lighting_mis=lighting_mis --job transparent_lighting=transparent_lighting --job principled_render=principled_render --job nodes_render=nodes_render
$S l2-triangle 900 python3 $T/triangle_lighting_gate.py --skip-build --output $V/l2-triangle/out
$S l2-shadow-terminator 900 python3 $T/shadow_terminator_gate.py --skip-build --output $V/l2-shadow-terminator/out
$S l2-emitter-sample 600 $B/codebrix_cycles_emitter_sample_gate $V/l2-emitter-sample/run
$S l2-closest-static 600 $B/codebrix_cycles_closest_gate $V/l2-closest-static/run
for m in motion deform deform-motion; do $S l2-closest-$m 600 $B/codebrix_cycles_closest_gate $V/l2-closest-$m/run --$m; done
$S l2-shadow 600 python3 $T/shadow_gate.py --skip-build --output $V/l2-shadow/out
$S l2-scene-edit 900 python3 $T/scene_edit_gate.py --skip-build --output $V/l2-scene-edit/out
env $(pl l2-glass-spec-on) $S l2-glass-spec-on 900 $B/codebrix_cycles_glass_render_gate $V/l2-glass-spec-on/run
$S l2-nodes-gate 2400 python3 $T/nodes_gate.py --skip-build --output $V/l2-nodes-gate/out
$S l2-principled-gate 1500 python3 $T/principled_gate.py --skip-build --output $V/l2-principled-gate/out
env $(pl l2-principled-render-on) $S l2-principled-render-on 900 $B/codebrix_cycles_principled_render_gate $V/l2-principled-render-on/run
$S l2-principled-replay 900 python3 $M/tools/replay.py $V/l2-principled-render-on $V/l2-principled-replay/out
$S l2-nodes-replay 900 python3 $M/tools/replay.py $V/l2-nodes-gate/out/nodes_render $V/l2-nodes-replay/out
env $(pl l2-principled-bench) $S l2-principled-bench 2400 $B/codebrix_cycles_principled_render_gate $V/l2-principled-bench/run $R/native/work/render/bench/triangles.bin
$S l2-svm-surface 900 $B/codebrix_cycles_svm_surface_gate $V/l2-svm-surface/run
$S l2-svm-world 900 $B/codebrix_cycles_svm_world_gate $V/l2-svm-world/run
$S l2-surface 900 $B/codebrix_cycles_surface_gate $V/l2-surface/run
for g in normal attribute image image_file shader_data; do $S l2-probe-$g 1200 $B/codebrix_cycles_${g}_gate $V/l2-probe-$g/run; done
echo "[$(date +%H:%M:%S)] slot2 DONE" >> $M/timeline.log
