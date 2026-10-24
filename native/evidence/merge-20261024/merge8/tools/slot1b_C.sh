#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 8 (from merge wave 7 part 2) part 2, Linux slot C: glass/Principled/node-render superset identity with WGSL dumps, ABI and transparent-lighting
# WGSL dumps, the Intel compile envelope over all of them; then the device reference export and the Linux triangle and
# terminator captures that part 2 replays on the devices.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge8; V=$M/validation; S=$M/tools/slot.sh
T=$R/native/tools; B=$R/native/work/cycles-build/bin
cd $R; mkdir -p $M/reference-1b
$S b8-glass-superset 1800 python3 $M/tools/superset_dump.py glass_render $V/b8-glass-superset/out 900
$S b8-principled-superset 2400 python3 $M/tools/superset_dump.py principled_render $V/b8-principled-superset/out 900
$S b8-nodes-render-superset 1800 python3 $M/tools/superset_dump.py nodes_render $V/b8-nodes-render-superset/out 900
mkdir -p $V/b8-abi-dump/dump $V/b8-transparent-dump/dump
CODEBRIX_CYCLES_WGSL_DUMP=$V/b8-abi-dump/dump $S b8-abi-dump 600 $B/codebrix_host_abi_test $V/b8-abi-dump/run gpu
CODEBRIX_CYCLES_WGSL_DUMP=$V/b8-transparent-dump/dump $S b8-transparent-dump 900 $B/codebrix_cycles_transparent_lighting_gate $V/b8-transparent-dump/run
$S b8-envelope-abi 3600 python3 $T/intel_compile_envelope.py --dump $V/b8-abi-dump/dump --debug-file-directory $R/native/work/merge4/debug --output $V/b8-envelope-abi/out
rm -rf $M/envelope-set-1b; mkdir -p $M/envelope-set-1b; for g in glass principled nodes-render; do cp -r $V/b8-$g-superset/out/dump $M/envelope-set-1b/$g; done; cp -r $V/b8-transparent-dump/dump $M/envelope-set-1b/transparent-lighting
$S b8-envelope-materials 5400 python3 $T/intel_compile_envelope.py --dump $M/envelope-set-1b --debug-file-directory $R/native/work/merge4/debug --output $V/b8-envelope-materials/out --modules surface,background --entries prepare_surface,shade_surface,shade_background,shade_surface_light,shade_surface_light_triangle,connect_surface_light
$S b8-libm-linux 900 python3 $T/libm_probe.py linux $M/reference-1b/libm-linux-x64-glibc.json
$S b8-reference-export 2400 python3 $T/android_reference.py export --bin $B --libm-record $M/reference-1b/libm-linux-x64-glibc.json --output $M/reference-1b/linux-x64 --build-record $R/native/work/cycles-build.json --job triangle_sample=triangle_sample --job emitter_sample=emitter_sample --job triangle_lighting=triangle_lighting --job glass_render=glass_render --job lighting_mis=lighting_mis --job transparent_lighting=transparent_lighting --job principled_render=principled_render --job nodes_render=nodes_render
$S b8-triangle 900 python3 $T/triangle_lighting_gate.py --skip-build --output $V/b8-triangle/out
$S b8-shadow-terminator 900 python3 $T/shadow_terminator_gate.py --skip-build --output $V/b8-shadow-terminator/out
$S b8-surface 900 $B/codebrix_cycles_surface_gate $V/b8-surface/run
echo "[$(date +%H:%M:%S)] slot1b_C DONE" >> $M/timeline.log
