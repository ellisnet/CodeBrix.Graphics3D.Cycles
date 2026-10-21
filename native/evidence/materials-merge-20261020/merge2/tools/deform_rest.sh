#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 2 diagnostic: the deforming_motion_gate jobs after normal-deform (all-lanes stopped there), run directly.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge2; B=$R/native/work/cycles-build/bin; O=$M/validation/diag-deform-rest
mkdir -p $O; cd $R
run(){ n=$1; shift; mkdir -p $O/$n $M/mesa-dr; echo "[$(date +%H:%M:%S)] DECLARED diag-deform-rest $n budget=900s" >> $M/budgets.txt
  MESA_SHADER_CACHE_DIR=$M/mesa-dr timeout 900 "$@" > $O/$n/stdout.txt 2> $O/$n/stderr.txt; echo "$n rc=$?"; rm -rf $M/mesa-dr; }
for f in deform-motion motion; do for g in closest shader_data shadow normal; do run $g-$f $B/codebrix_cycles_${g}_gate $O/$g-$f/run --$f; done; done
for g in closest shader_data shadow; do run $g-static $B/codebrix_cycles_${g}_gate $O/$g-static/run; done
run moving-images $B/codebrix_cycles_deforming_motion_render_gate $O/moving-images/run
run stationary-images $B/codebrix_cycles_deforming_motion_render_gate $O/stationary-images/run --stationary
run object-motion-regression $B/codebrix_cycles_object_motion_render_gate $O/object-motion-regression/run
