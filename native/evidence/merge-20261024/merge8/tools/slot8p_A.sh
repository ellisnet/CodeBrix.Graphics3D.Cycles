#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 8 part 2, Linux stream A (labels p8-*): all-lanes, host gate, dotnet TEST only (no build/pack: nothing is built here).
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge8; V=$M/validation; S=$M/tools/slot.sh
cd $R
$S p8-all-lanes 2700 python3 native/evidence/phaseC-closeout-20261020/closeout/tools/linux_validate.py --tree $R --output $V/p8-all-lanes/out --skip-build
$S p8-host-gate 1500 python3 native/tools/host_gate.py --output $V/p8-host-gate/out
CODEBRIX_CYCLES_REFERENCE_EXPORT=$M/reference-p2/managed-linux-x64 $S p8-dotnet-test 1500 dotnet test --solution CodeBrix.Graphics3D.Cycles.slnx --configuration Release --no-build
$S p8-dotnet-test-debug 1500 dotnet test --solution CodeBrix.Graphics3D.Cycles.slnx --configuration Debug --no-build
echo "[$(date +%H:%M:%S)] slot8p_A DONE" >> $M/timeline.log
