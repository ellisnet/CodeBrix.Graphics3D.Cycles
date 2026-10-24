#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 8 part 2, Linux slot L: light tree replays 3-6 run individually (the gate driver stops at the known case-2 replay, item 24) and case 2 once by itself.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge8; V=$M/validation; S=$M/tools/slot.sh
cd $R
until grep -q "END p8-light-tree exit=" $M/timeline.log; do sleep 20; done
$S p8-light-tree-replays 1800 python3 $M/tools/replay_cases.py $V/p8-light-tree/out/light_tree/run $V/p8-light-tree-replays/out light_tree 3 4 5 6
$S p8-light-tree-replay-2-alone 900 python3 $M/tools/replay_cases.py $V/p8-light-tree/out/light_tree/run $V/p8-light-tree-replay-2-alone/out light_tree 2
echo "[$(date +%H:%M:%S)] slot8p_L DONE" >> $M/timeline.log
