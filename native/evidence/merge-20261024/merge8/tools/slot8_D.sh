#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 8 (from merge wave 7 part 2) part 2, Linux stream D: the ledger's cited Linux gates re-run on the final tree (as lane z0 did: plain gates and
# render --attributes/--images/--normals, nodes_svm, nodes_world; nodes_render comes from w8-nodes-render-superset), then the two
# Linux command replays (Principled and node render) with the part-1 render_replay binary (no rebuild).
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge8; V=$M/validation; S=$M/tools/slot.sh; B=$R/native/work/cycles-build/bin
cd $R
run(){ g=$1; $S w8v-$g 1200 env -u OCIO $B/codebrix_cycles_${g}_gate $V/w8v-$g/run; }
for g in render device camera background closest svm_world svm_surface attribute image image_file normal; do run $g; done
$S w8v-render-attributes 1200 env -u OCIO $B/codebrix_cycles_render_gate $V/w8v-render-attributes/run --attributes
$S w8v-render-images 1200 env -u OCIO $B/codebrix_cycles_render_gate $V/w8v-render-images/run --images
$S w8v-render-normals 1200 env -u OCIO $B/codebrix_cycles_render_gate $V/w8v-render-normals/run --normals
$S w8v-nodes_svm 1200 env -u OCIO $B/codebrix_cycles_nodes_svm_gate $V/w8v-nodes_svm/run
$S w8v-nodes_world 900 env -u OCIO $B/codebrix_cycles_nodes_world_gate $V/w8v-nodes_world/run
until grep -q "END w8-principled-superset exit=" $M/timeline.log && grep -q "END w8-nodes-gate exit=" $M/timeline.log && grep -q "END w8-nodes-render-superset exit=" $M/timeline.log; do sleep 20; done
$S w8-principled-replay 900 python3 $M/tools/replay_nobuild.py $V/w8-principled-superset/out/on $V/w8-principled-replay/out
$S w8-nodes-replay 900 python3 $M/tools/replay_nobuild.py $V/w8-nodes-gate/out/nodes_render $V/w8-nodes-replay/out
echo "[$(date +%H:%M:%S)] slot8_D DONE" >> $M/timeline.log
