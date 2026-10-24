#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 7 part 2, Linux stream F: environment gate, camera gate (static + motion), environment envelope, ledger gate for environment-light.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge7; V=$M/validation; S=$M/tools/slot.sh
T=$R/native/tools; B=$R/native/work/cycles-build/bin
cd $R; mkdir -p $V/p2-environment-dump $V/p2-camera-gate $V/p2-camera-motion
CODEBRIX_CYCLES_WGSL_DUMP=$V/p2-environment-dump $S p2-environment-gate 1200 $B/codebrix_cycles_environment_gate $V/p2-environment-gate/out
$S p2-camera-gate 900 $B/codebrix_cycles_camera_gate $V/p2-camera-gate/run
$S p2-camera-motion 900 $B/codebrix_cycles_camera_gate $V/p2-camera-motion/run --motion
$S p2-envelope-environment 3600 python3 $T/intel_compile_envelope.py --dump $V/p2-environment-dump --debug-file-directory $R/native/work/merge4/debug --output $V/p2-envelope-environment/out
echo "[$(date +%H:%M:%S)] slotP_F DONE" >> $M/timeline.log
