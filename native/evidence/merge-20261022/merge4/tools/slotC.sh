#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 4, Linux slot C: WGSL dumps (dump folders created first; slot B's l2 dumps wrote nothing because
# CODEBRIX_CYCLES_WGSL_DUMP does not create its folder) and the Intel compile envelope over them.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge4; V=$M/validation; S=$M/tools/slot.sh
T=$R/native/tools; B=$R/native/work/cycles-build/bin
cd $R
mkdir -p $V/l3-abi-dump/dump
CODEBRIX_CYCLES_WGSL_DUMP=$V/l3-abi-dump/dump $S l3-abi-dump 600 $B/codebrix_host_abi_test $V/l3-abi-dump/run gpu
$S l3-envelope-abi 3600 python3 $T/intel_compile_envelope.py --dump $V/l3-abi-dump/dump --debug-file-directory $M/debug --output $V/l3-envelope-abi/out
$S l3-glass-superset 1800 python3 $M/tools/superset_dump.py glass_render $V/l3-glass-superset/out 900
$S l3-principled-superset 2400 python3 $M/tools/superset_dump.py principled_render $V/l3-principled-superset/out 900
$S l3-nodes-render-superset 1800 python3 $M/tools/superset_dump.py nodes_render $V/l3-nodes-render-superset/out 900
rm -rf $M/envelope-set; mkdir -p $M/envelope-set; for g in glass principled nodes-render; do cp -r $V/l3-$g-superset/out/dump $M/envelope-set/$g; done
$S l3-envelope-materials 5400 python3 $T/intel_compile_envelope.py --dump $M/envelope-set --debug-file-directory $M/debug --output $V/l3-envelope-materials/out --modules surface,background --entries prepare_surface,shade_surface,shade_background,shade_surface_light,shade_surface_light_triangle,connect_surface_light
echo "[$(date +%H:%M:%S)] slotC DONE" >> $M/timeline.log
