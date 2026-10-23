#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 5, Linux slot C: glass/Principled/node-render superset identity with WGSL dumps, ABI and transparent-lighting
# WGSL dumps, the Intel compile envelope over all of them; then the device reference export and the Linux triangle and
# terminator captures that part 2 replays on the devices.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge5; V=$M/validation; S=$M/tools/slot.sh
T=$R/native/tools; B=$R/native/work/cycles-build/bin
cd $R; mkdir -p $M/reference-e
$S e-glass-superset 1800 python3 $M/tools/superset_dump.py glass_render $V/e-glass-superset/out 900
$S e-principled-superset 2400 python3 $M/tools/superset_dump.py principled_render $V/e-principled-superset/out 900
$S e-nodes-render-superset 1800 python3 $M/tools/superset_dump.py nodes_render $V/e-nodes-render-superset/out 900
mkdir -p $V/e-abi-dump/dump $V/e-transparent-dump/dump
CODEBRIX_CYCLES_WGSL_DUMP=$V/e-abi-dump/dump $S e-abi-dump 600 $B/codebrix_host_abi_test $V/e-abi-dump/run gpu
CODEBRIX_CYCLES_WGSL_DUMP=$V/e-transparent-dump/dump $S e-transparent-dump 900 $B/codebrix_cycles_transparent_lighting_gate $V/e-transparent-dump/run
$S e-envelope-abi 3600 python3 $T/intel_compile_envelope.py --dump $V/e-abi-dump/dump --debug-file-directory $R/native/work/merge4/debug --output $V/e-envelope-abi/out
rm -rf $M/envelope-set-e; mkdir -p $M/envelope-set-e; for g in glass principled nodes-render; do cp -r $V/e-$g-superset/out/dump $M/envelope-set-e/$g; done; cp -r $V/e-transparent-dump/dump $M/envelope-set-e/transparent-lighting
$S e-envelope-materials 5400 python3 $T/intel_compile_envelope.py --dump $M/envelope-set-e --debug-file-directory $R/native/work/merge4/debug --output $V/e-envelope-materials/out --modules surface,background --entries prepare_surface,shade_surface,shade_background,shade_surface_light,shade_surface_light_triangle,connect_surface_light
$S e-libm-linux 900 python3 $T/libm_probe.py linux $M/reference-e/libm-linux-x64-glibc.json
$S e-reference-export 2400 python3 $T/android_reference.py export --bin $B --libm-record $M/reference-e/libm-linux-x64-glibc.json --output $M/reference-e/linux-x64 --build-record $R/native/work/cycles-build.json --job triangle_sample=triangle_sample --job emitter_sample=emitter_sample --job triangle_lighting=triangle_lighting --job glass_render=glass_render --job lighting_mis=lighting_mis --job transparent_lighting=transparent_lighting --job principled_render=principled_render --job nodes_render=nodes_render
$S e-triangle 900 python3 $T/triangle_lighting_gate.py --skip-build --output $V/e-triangle/out
$S e-shadow-terminator 900 python3 $T/shadow_terminator_gate.py --skip-build --output $V/e-shadow-terminator/out
$S e-surface 900 $B/codebrix_cycles_surface_gate $V/e-surface/run
echo "[$(date +%H:%M:%S)] slotE_C DONE" >> $M/timeline.log
