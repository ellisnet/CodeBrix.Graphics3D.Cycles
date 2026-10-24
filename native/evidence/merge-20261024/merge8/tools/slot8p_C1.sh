#!/bin/bash
# Merge wave 8 part 2: Linux re-runs on the part-1c binary (volume_gate rebuilt): volume gate, volume_boundary_gate, host gate (managed-reload phase). Old-binary results (p8-*) are kept.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge8; V=$M/validation; S=$M/tools/slot.sh; T=$R/native/tools
cd $R
$S p8c-al-volume_gate 2400 python3 $T/volume_gate.py --skip-build --output $V/p8c-al-volume_gate/out
$S p8c-al-volume_boundary_gate 2400 python3 $T/volume_boundary_gate.py --skip-build --output $V/p8c-al-volume_boundary_gate/out
$S p8c-host-gate 1500 python3 native/tools/host_gate.py --output $V/p8c-host-gate/out
echo "[$(date +%H:%M:%S)] slot8p_C1 DONE" >> $M/timeline.log
