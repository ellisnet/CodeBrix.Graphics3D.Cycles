#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 8 (from merge wave 7 part 2) part 2, Linux slot C: glass/Principled/node-render superset identity with WGSL dumps, ABI and transparent-lighting
# WGSL dumps, the Intel compile envelope over all of them; then the device reference export and the Linux triangle and
# terminator captures that part 2 replays on the devices.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge8; V=$M/validation; S=$M/tools/slot.sh
T=$R/native/tools; B=$R/native/work/cycles-build/bin
cd $R; mkdir -p $M/reference
$S w8-glass-superset 1800 python3 $M/tools/superset_dump.py glass_render $V/w8-glass-superset/out 900
$S w8-principled-superset 2400 python3 $M/tools/superset_dump.py principled_render $V/w8-principled-superset/out 900
$S w8-nodes-render-superset 1800 python3 $M/tools/superset_dump.py nodes_render $V/w8-nodes-render-superset/out 900
mkdir -p $V/w8-abi-dump/dump $V/w8-transparent-dump/dump
CODEBRIX_CYCLES_WGSL_DUMP=$V/w8-abi-dump/dump $S w8-abi-dump 600 $B/codebrix_host_abi_test $V/w8-abi-dump/run gpu
CODEBRIX_CYCLES_WGSL_DUMP=$V/w8-transparent-dump/dump $S w8-transparent-dump 900 $B/codebrix_cycles_transparent_lighting_gate $V/w8-transparent-dump/run
$S w8-envelope-abi 3600 python3 $T/intel_compile_envelope.py --dump $V/w8-abi-dump/dump --debug-file-directory $R/native/work/merge4/debug --output $V/w8-envelope-abi/out
rm -rf $M/envelope-set; mkdir -p $M/envelope-set; for g in glass principled nodes-render; do cp -r $V/w8-$g-superset/out/dump $M/envelope-set/$g; done; cp -r $V/w8-transparent-dump/dump $M/envelope-set/transparent-lighting
$S w8-envelope-materials 5400 python3 $T/intel_compile_envelope.py --dump $M/envelope-set --debug-file-directory $R/native/work/merge4/debug --output $V/w8-envelope-materials/out --modules surface,background --entries prepare_surface,shade_surface,shade_background,shade_surface_light,shade_surface_light_triangle,connect_surface_light
$S w8-libm-linux 900 python3 $T/libm_probe.py linux $M/reference/libm-linux-x64-glibc.json
$S w8-reference-export 2400 python3 $T/android_reference.py export --bin $B --libm-record $M/reference/libm-linux-x64-glibc.json --output $M/reference/linux-x64 --build-record $R/native/work/cycles-build.json --job triangle_sample=triangle_sample --job emitter_sample=emitter_sample --job triangle_lighting=triangle_lighting --job glass_render=glass_render --job lighting_mis=lighting_mis --job transparent_lighting=transparent_lighting --job principled_render=principled_render --job nodes_render=nodes_render
$S w8-triangle 900 python3 $T/triangle_lighting_gate.py --skip-build --output $V/w8-triangle/out
$S w8-shadow-terminator 900 python3 $T/shadow_terminator_gate.py --skip-build --output $V/w8-shadow-terminator/out
$S w8-surface 900 $B/codebrix_cycles_surface_gate $V/w8-surface/run
echo "[$(date +%H:%M:%S)] slot8_C DONE" >> $M/timeline.log
