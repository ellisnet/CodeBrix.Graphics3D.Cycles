#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Lane E4 final Linux slot 1 (sequential; copy of merge wave 1 slot1): host gate, .NET build/test/pack, all-lanes.
R=/home/jeremy/ClaudeHome/cycles-worktrees/e4-mode4-adreno; M=$R/native/work/e4; V=$M/linux; S=$M/tools/slot.sh
cd $R
$S f1-host-gate 1500 python3 native/tools/host_gate.py --output $V/f1-host-gate/out
$S f1-dotnet-build 900 dotnet build --configuration Release CodeBrix.Graphics3D.Cycles.slnx
$S f1-dotnet-test 1500 dotnet test --solution CodeBrix.Graphics3D.Cycles.slnx --configuration Release --no-build
$S f1-all-lanes 2700 python3 $M/tools/linux_validate.py --tree $R --output $V/f1-all-lanes/out --skip-build
echo "[$(date +%H:%M:%S)] slot1 DONE" >> $M/handoff/timeline.log
