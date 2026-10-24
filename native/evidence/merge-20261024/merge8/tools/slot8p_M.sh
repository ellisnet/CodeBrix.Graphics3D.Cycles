#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 8, slot M (C1): the comparator with the Mac gold-standard films added (frozen evidence
# cycles-oracles-macos-20261023 starters, -2 bench and HDRI bench) to this wave's Linux CPU + WGSL films. Waits for slot E.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge8; V=$M/validation; S=$M/tools/slot.sh
E1=$R/native/evidence/cycles-oracles-macos-20261023/oracle; E2=$R/native/evidence/cycles-oracles-macos-20261023-2/oracle
cd $R; mkdir -p $V/p8-comparator-mac
until grep -q "END p8-oracle-hdri exit=" $M/timeline.log; do sleep 20; done
mac(){ echo $1/cpu-arm64-macos $1/metal-m2pro $1/metal-m2pro-metalrt; }
for sc in diffuse-triangle glass-and-area-light glossy-sphere-point-light textured-plane; do
  L=$M/oracles-p2/starters/oracle/starter/$sc
  $S p8-macmp-$sc 600 python3 native/oracle/compare.py $L/cpu-x64-linux $L/wgsl-linux-intel $(mac $E1/starter/$sc) --json $V/p8-comparator-mac/$sc.json
done
L=$M/oracles-p2/bench/oracle/assets/bench_01; $S p8-macmp-bench 600 python3 native/oracle/compare.py $L/cpu-x64-linux $L/wgsl-linux-intel $(mac $E2/assets/bench_01) --json $V/p8-comparator-mac/bench.json
L=$M/oracles-p2/hdri/oracle/assets/bench_01-hdri; $S p8-macmp-hdri 600 python3 native/oracle/compare.py $L/cpu-x64-linux $L/wgsl-linux-intel $(mac $E2/assets/bench_01-hdri) --json $V/p8-comparator-mac/hdri.json
echo "[$(date +%H:%M:%S)] slot8p_M DONE" >> $M/timeline.log
