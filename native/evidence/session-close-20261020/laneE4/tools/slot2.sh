#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Lane E4 final Linux slot 2 (sequential; copy of merge wave 1 slot2 without the libm record, which is reused).
R=/home/jeremy/ClaudeHome/cycles-worktrees/e4-mode4-adreno; M=$R/native/work/e4; V=$M/linux; S=$M/tools/slot.sh
T=$R/native/tools; B=$R/native/work/cycles-build/bin
cd $R
rm -rf $M/reference/linux-x64
$S f2-reference-export 1800 python3 $T/android_reference.py export --bin $B --output $M/reference/linux-x64 --libm-record $M/reference/libm-linux-x64-glibc.json --build-record $R/native/work/cycles-build.json --job triangle_sample=triangle_sample --job emitter_sample=emitter_sample --job triangle_lighting=triangle_lighting --job glass_render=glass_render --job lighting_mis=lighting_mis --job transparent_lighting=transparent_lighting
$S f2-triangle 900 python3 $T/triangle_lighting_gate.py --skip-build --output $V/f2-triangle/out
$S f2-shadow-terminator 900 python3 $T/shadow_terminator_gate.py --skip-build --output $V/f2-shadow-terminator/out
$S f2-closest-static 600 $B/codebrix_cycles_closest_gate $V/f2-closest-static/run
for m in motion deform deform-motion; do $S f2-closest-$m 600 $B/codebrix_cycles_closest_gate $V/f2-closest-$m/run --$m; done
$S f2-shadow 600 python3 $T/shadow_gate.py --skip-build --output $V/f2-shadow/out
$S f2-glass-spec-on 900 $B/codebrix_cycles_glass_render_gate $V/f2-glass-spec-on/run
CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0 $S f2-glass-spec-off 900 $B/codebrix_cycles_glass_render_gate $V/f2-glass-spec-off/run
$S f2-scene-edit 900 python3 $T/scene_edit_gate.py --skip-build --output $V/f2-scene-edit/out
$S f2-object-motion 900 python3 $T/object_motion_gate.py --skip-build --output $V/f2-object-motion/out
$S f2-deforming-motion 900 python3 $T/deforming_motion_gate.py --skip-build --output $V/f2-deforming-motion/out
$S f2-feature-combination 900 python3 $T/feature_combination_gate.py --skip-build --output $V/f2-feature-combination/out
$S f2-surface 900 $B/codebrix_cycles_surface_gate $V/f2-surface/run
$S f2-transparent-shadow 1800 python3 $T/transparent_shadow_gate.py --skip-build --output $V/f2-transparent-shadow/out
echo "[$(date +%H:%M:%S)] slot2 DONE" >> $M/handoff/timeline.log
