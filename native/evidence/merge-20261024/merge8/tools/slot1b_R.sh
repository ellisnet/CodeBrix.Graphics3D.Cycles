#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 8, slot R: what the failed all-lanes and host-gate runs did not reach: the all-lanes gates after
# volume_boundary_gate (volume_gate, glass_gate, camera_motion_gate) and the host gate continued past managed-reload.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge8; V=$M/validation; S=$M/tools/slot.sh; T=$R/native/tools
cd $R
for g in volume_gate glass_gate camera_motion_gate; do $S b8-al-$g 2400 python3 $T/$g.py --skip-build --output $V/b8-al-$g/out; done
$S b8-host-gate-continue 1500 python3 $M/tools/host_gate_continue.py --output $V/b8-host-gate-continue/out
echo "[$(date +%H:%M:%S)] slot1b_R DONE" >> $M/timeline.log
