#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 6 part 2, Linux slot C: glass/Principled/node-render superset identity with WGSL dumps, ABI and transparent-lighting
# WGSL dumps, the Intel compile envelope over all of them; then the device reference export and the Linux triangle and
# terminator captures that part 2 replays on the devices.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge6; V=$M/validation; S=$M/tools/slot.sh
T=$R/native/tools; B=$R/native/work/cycles-build/bin
cd $R; mkdir -p $M/reference-p2
$S p2-glass-superset 1800 python3 $M/tools/superset_dump.py glass_render $V/p2-glass-superset/out 900
$S p2-principled-superset 2400 python3 $M/tools/superset_dump.py principled_render $V/p2-principled-superset/out 900
$S p2-nodes-render-superset 1800 python3 $M/tools/superset_dump.py nodes_render $V/p2-nodes-render-superset/out 900
mkdir -p $V/p2-abi-dump/dump $V/p2-transparent-dump/dump
CODEBRIX_CYCLES_WGSL_DUMP=$V/p2-abi-dump/dump $S p2-abi-dump 600 $B/codebrix_host_abi_test $V/p2-abi-dump/run gpu
CODEBRIX_CYCLES_WGSL_DUMP=$V/p2-transparent-dump/dump $S p2-transparent-dump 900 $B/codebrix_cycles_transparent_lighting_gate $V/p2-transparent-dump/run
$S p2-envelope-abi 3600 python3 $T/intel_compile_envelope.py --dump $V/p2-abi-dump/dump --debug-file-directory $R/native/work/merge4/debug --output $V/p2-envelope-abi/out
rm -rf $M/envelope-set-p2; mkdir -p $M/envelope-set-p2; for g in glass principled nodes-render; do cp -r $V/p2-$g-superset/out/dump $M/envelope-set-p2/$g; done; cp -r $V/p2-transparent-dump/dump $M/envelope-set-p2/transparent-lighting
$S p2-envelope-materials 5400 python3 $T/intel_compile_envelope.py --dump $M/envelope-set-p2 --debug-file-directory $R/native/work/merge4/debug --output $V/p2-envelope-materials/out --modules surface,background --entries prepare_surface,shade_surface,shade_background,shade_surface_light,shade_surface_light_triangle,connect_surface_light
$S p2-libm-linux 900 python3 $T/libm_probe.py linux $M/reference-p2/libm-linux-x64-glibc.json
$S p2-reference-export 2400 python3 $T/android_reference.py export --bin $B --libm-record $M/reference-p2/libm-linux-x64-glibc.json --output $M/reference-p2/linux-x64 --build-record $R/native/work/cycles-build.json --job triangle_sample=triangle_sample --job emitter_sample=emitter_sample --job triangle_lighting=triangle_lighting --job glass_render=glass_render --job lighting_mis=lighting_mis --job transparent_lighting=transparent_lighting --job principled_render=principled_render --job nodes_render=nodes_render
$S p2-triangle 900 python3 $T/triangle_lighting_gate.py --skip-build --output $V/p2-triangle/out
$S p2-shadow-terminator 900 python3 $T/shadow_terminator_gate.py --skip-build --output $V/p2-shadow-terminator/out
$S p2-surface 900 $B/codebrix_cycles_surface_gate $V/p2-surface/run
echo "[$(date +%H:%M:%S)] slotP_C DONE" >> $M/timeline.log
