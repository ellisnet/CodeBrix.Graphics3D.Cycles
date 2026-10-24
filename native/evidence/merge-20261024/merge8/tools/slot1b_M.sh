#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 8, slot M (C1): the comparator with the Mac gold-standard films added (frozen evidence
# cycles-oracles-macos-20261023 starters, -2 bench and HDRI bench) to this wave's Linux CPU + WGSL films. Waits for slot E.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge8; V=$M/validation; S=$M/tools/slot.sh
E1=$R/native/evidence/cycles-oracles-macos-20261023/oracle; E2=$R/native/evidence/cycles-oracles-macos-20261023-2/oracle
cd $R; mkdir -p $V/b8-comparator-mac
until grep -q "END b8-oracle-hdri exit=" $M/timeline.log; do sleep 20; done
mac(){ echo $1/cpu-arm64-macos $1/metal-m2pro $1/metal-m2pro-metalrt; }
for sc in diffuse-triangle glass-and-area-light glossy-sphere-point-light textured-plane; do
  L=$M/oracles-1b/starters/oracle/starter/$sc
  $S b8-macmp-$sc 600 python3 native/oracle/compare.py $L/cpu-x64-linux $L/wgsl-linux-intel $(mac $E1/starter/$sc) --json $V/b8-comparator-mac/$sc.json
done
L=$M/oracles-1b/bench/oracle/assets/bench_01; $S b8-macmp-bench 600 python3 native/oracle/compare.py $L/cpu-x64-linux $L/wgsl-linux-intel $(mac $E2/assets/bench_01) --json $V/b8-comparator-mac/bench.json
L=$M/oracles-1b/hdri/oracle/assets/bench_01-hdri; $S b8-macmp-hdri 600 python3 native/oracle/compare.py $L/cpu-x64-linux $L/wgsl-linux-intel $(mac $E2/assets/bench_01-hdri) --json $V/b8-comparator-mac/hdri.json
echo "[$(date +%H:%M:%S)] slot1b_M DONE" >> $M/timeline.log
