#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 8 (copied from merge wave 7) (from lane S3's linux_flow.sh): Linux self-test of the render_gate stage-state exchange:
# local run, dump, export, exchange run, judge. usage: linux_flow.sh GATE OUT [ARG...]  (job env from the caller's environment)
set -u
GATE=$1; OUT=$2; shift 2
B=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/cycles-build/bin/codebrix_cycles_${GATE}_gate
mkdir -p $OUT; cd $OUT
t(){ date +%H:%M:%S; }
echo "[$(t)] local"; /usr/bin/time -f %e $B $OUT/local "$@" > local.json 2> local.err; echo "local exit $?"
echo "[$(t)] dump"; CODEBRIX_CYCLES_SCENE_DUMP=$OUT/scene $B $OUT/dump "$@" > dump.json 2> dump.err; echo "dump exit $?"
echo "[$(t)] export"; CODEBRIX_CYCLES_SCENE_LOAD=$OUT/scene CODEBRIX_CYCLES_REFERENCE_EXPORT=$OUT/ref $B $OUT/export "$@" > export.json 2> export.err; echo "export exit $?"
echo "[$(t)] exchange"; CODEBRIX_CYCLES_REFERENCE_IMPORT=$OUT/ref CODEBRIX_CYCLES_SCENE_VERIFY=$OUT/scene $B $OUT/exch "$@" > exch.json 2> exch.err; echo "exchange exit $?"
echo "[$(t)] judge"; /usr/bin/time -f %e env CODEBRIX_CYCLES_SCENE_LOAD=$OUT/scene CODEBRIX_CYCLES_STAGE_JUDGE=$OUT/exch.stages.bin $B $OUT/judge "$@" > judge.json 2> judge.err; echo "judge exit $?"
echo "[$(t)] done"
