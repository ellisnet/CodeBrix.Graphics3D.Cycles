#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 6, Linux slot C: glass/Principled/node-render superset identity with WGSL dumps, ABI and transparent-lighting
# WGSL dumps, the Intel compile envelope over all of them; then the device reference export and the Linux triangle and
# terminator captures that part 2 replays on the devices.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge7; V=$M/validation; S=$M/tools/slot.sh
T=$R/native/tools; B=$R/native/work/cycles-build/bin
cd $R
$S c-glass-superset 1800 python3 $M/tools/superset_dump.py glass_render $V/c-glass-superset/out 900
$S c-principled-superset 2400 python3 $M/tools/superset_dump.py principled_render $V/c-principled-superset/out 900
$S c-nodes-render-superset 1800 python3 $M/tools/superset_dump.py nodes_render $V/c-nodes-render-superset/out 900
mkdir -p $V/c-abi-dump/dump $V/c-transparent-dump/dump
CODEBRIX_CYCLES_WGSL_DUMP=$V/c-abi-dump/dump $S c-abi-dump 600 $B/codebrix_host_abi_test $V/c-abi-dump/run gpu
CODEBRIX_CYCLES_WGSL_DUMP=$V/c-transparent-dump/dump $S c-transparent-dump 900 $B/codebrix_cycles_transparent_lighting_gate $V/c-transparent-dump/run
$S c-envelope-abi 3600 python3 $T/intel_compile_envelope.py --dump $V/c-abi-dump/dump --debug-file-directory $R/native/work/merge4/debug --output $V/c-envelope-abi/out
rm -rf $M/envelope-set; mkdir -p $M/envelope-set; for g in glass principled nodes-render; do cp -r $V/c-$g-superset/out/dump $M/envelope-set/$g; done; cp -r $V/c-transparent-dump/dump $M/envelope-set/transparent-lighting
$S c-envelope-materials 5400 python3 $T/intel_compile_envelope.py --dump $M/envelope-set --debug-file-directory $R/native/work/merge4/debug --output $V/c-envelope-materials/out --modules surface,background --entries prepare_surface,shade_surface,shade_background,shade_surface_light,shade_surface_light_triangle,connect_surface_light
$S c-libm-linux 900 python3 $T/libm_probe.py linux $M/reference/libm-linux-x64-glibc.json
$S c-reference-export 2400 python3 $T/android_reference.py export --bin $B --libm-record $M/reference/libm-linux-x64-glibc.json --output $M/reference/linux-x64 --build-record $R/native/work/cycles-build.json --job triangle_sample=triangle_sample --job emitter_sample=emitter_sample --job triangle_lighting=triangle_lighting --job glass_render=glass_render --job lighting_mis=lighting_mis --job transparent_lighting=transparent_lighting --job principled_render=principled_render --job nodes_render=nodes_render
$S c-triangle 900 python3 $T/triangle_lighting_gate.py --skip-build --output $V/c-triangle/out
$S c-shadow-terminator 900 python3 $T/shadow_terminator_gate.py --skip-build --output $V/c-shadow-terminator/out
$S c-surface 900 $B/codebrix_cycles_surface_gate $V/c-surface/run
echo "[$(date +%H:%M:%S)] slotC DONE" >> $M/timeline.log
