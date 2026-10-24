#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 7 part 2, Linux stream A (labels p2-*): all-lanes, host gate, dotnet TEST only (no build/pack: nothing is built here).
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge7; V=$M/validation; S=$M/tools/slot.sh
cd $R
$S p2-all-lanes 2700 python3 native/evidence/phaseC-closeout-20261020/closeout/tools/linux_validate.py --tree $R --output $V/p2-all-lanes/out --skip-build
$S p2-host-gate 1500 python3 native/tools/host_gate.py --output $V/p2-host-gate/out
CODEBRIX_CYCLES_REFERENCE_EXPORT=$M/reference-p2/managed-linux-x64 $S p2-dotnet-test 1500 dotnet test --solution CodeBrix.Graphics3D.Cycles.slnx --configuration Release --no-build
$S p2-dotnet-test-debug 1500 dotnet test --solution CodeBrix.Graphics3D.Cycles.slnx --configuration Debug --no-build
echo "[$(date +%H:%M:%S)] slotP_A DONE" >> $M/timeline.log
