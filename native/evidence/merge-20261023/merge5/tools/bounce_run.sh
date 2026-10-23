#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 5: run the four surface_gate.cpp gates from a given bin folder (fresh Mesa cache each), for the
# principled_bounce shared-device before/after identity proof. usage: bounce_run.sh <label> <bin-dir>
M=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/merge5; L=$1; B=$2; O=$M/bounce/$L
for g in surface microfacet_bounce glass_bounce principled_bounce; do
  mkdir -p $O/$g; echo "[$(date +%H:%M:%S)] DECLARED bounce-$L-$g budget=900s: $B/codebrix_cycles_${g}_gate $O/$g/run" >> $M/budgets.txt
  ( s=$(date +%s); MESA_SHADER_CACHE_DIR=$O/$g/shader-cache timeout 900 $B/codebrix_cycles_${g}_gate $O/$g/run > $O/$g/stdout.txt 2> $O/$g/stderr.txt; c=$?
    echo "[$(date +%H:%M:%S)] END bounce-$L-$g exit=$c $(( $(date +%s)-s ))s" >> $M/timeline.log ) &
done; wait
