#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 1, Linux slot 2 (sequential): every gate added or changed by E1/E2/E3/X1/X2.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge1; V=$M/validation; S=$M/tools/slot.sh
T=$R/native/tools; B=$R/native/work/cycles-build/bin
cd $R
# X1: Linux glibc libm record and the Linux-side reference export (also the Linux results of these gates)
$S l2-libm-linux 900 python3 $T/libm_probe.py linux $M/reference/libm-linux-x64-glibc.json
$S l2-reference-export 1800 python3 $T/android_reference.py export --bin $B --output $M/reference/linux-x64 --libm-record $M/reference/libm-linux-x64-glibc.json --build-record $R/native/work/cycles-build.json --job triangle_sample=triangle_sample --job emitter_sample=emitter_sample --job triangle_lighting=triangle_lighting --job glass_render=glass_render --job lighting_mis=lighting_mis --job transparent_lighting=transparent_lighting
$S l2-triangle 900 python3 $T/triangle_lighting_gate.py --skip-build --output $V/l2-triangle/out
$S l2-shadow-terminator 900 python3 $T/shadow_terminator_gate.py --skip-build --output $V/l2-shadow-terminator/out
$S l2-scene-edit 900 python3 $T/scene_edit_gate.py --skip-build --output $V/l2-scene-edit/out
$S l2-closest-static 600 $B/codebrix_cycles_closest_gate $V/l2-closest-static/run
for m in motion deform deform-motion; do $S l2-closest-$m 600 $B/codebrix_cycles_closest_gate $V/l2-closest-$m/run --$m; done
$S l2-shadow 600 python3 $T/shadow_gate.py --skip-build --output $V/l2-shadow/out
$S l2-object-motion 900 python3 $T/object_motion_gate.py --skip-build --output $V/l2-object-motion/out
$S l2-deforming-motion 900 python3 $T/deforming_motion_gate.py --skip-build --output $V/l2-deforming-motion/out
$S l2-feature-combination 900 python3 $T/feature_combination_gate.py --skip-build --output $V/l2-feature-combination/out
$S l2-volume 1200 python3 $T/volume_gate.py --skip-build --output $V/l2-volume/out
$S l2-volume-boundary 1200 python3 $T/volume_boundary_gate.py --skip-build --output $V/l2-volume-boundary/out
$S l2-surface 900 $B/codebrix_cycles_surface_gate $V/l2-surface/run
$S l2-glass-spec-on 900 $B/codebrix_cycles_glass_render_gate $V/l2-glass-spec-on/run
CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0 $S l2-glass-spec-off 900 $B/codebrix_cycles_glass_render_gate $V/l2-glass-spec-off/run
$S l2-transparent-shadow 1800 python3 $T/transparent_shadow_gate.py --skip-build --output $V/l2-transparent-shadow/out
echo "[$(date +%H:%M:%S)] slot2 DONE" >> $M/timeline.log
