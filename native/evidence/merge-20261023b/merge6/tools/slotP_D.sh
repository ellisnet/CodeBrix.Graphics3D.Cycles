#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 6 part 2, Linux stream D: the ledger's cited Linux gates re-run on the final tree (as lane z0 did: plain gates and
# render --attributes/--images/--normals, nodes_svm, nodes_world; nodes_render comes from p2-nodes-render-superset), then the two
# Linux command replays (Principled and node render) with the part-1 render_replay binary (no rebuild).
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge6; V=$M/validation; S=$M/tools/slot.sh; B=$R/native/work/cycles-build/bin
cd $R
run(){ g=$1; $S p2v-$g 1200 env -u OCIO $B/codebrix_cycles_${g}_gate $V/p2v-$g/run; }
for g in render device camera background closest svm_world svm_surface attribute image image_file normal; do run $g; done
$S p2v-render-attributes 1200 env -u OCIO $B/codebrix_cycles_render_gate $V/p2v-render-attributes/run --attributes
$S p2v-render-images 1200 env -u OCIO $B/codebrix_cycles_render_gate $V/p2v-render-images/run --images
$S p2v-render-normals 1200 env -u OCIO $B/codebrix_cycles_render_gate $V/p2v-render-normals/run --normals
$S p2v-nodes_svm 1200 env -u OCIO $B/codebrix_cycles_nodes_svm_gate $V/p2v-nodes_svm/run
$S p2v-nodes_world 900 env -u OCIO $B/codebrix_cycles_nodes_world_gate $V/p2v-nodes_world/run
until grep -q "END p2-principled-superset exit=" $M/timeline.log && grep -q "END p2-nodes-gate exit=" $M/timeline.log && grep -q "END p2-nodes-render-superset exit=" $M/timeline.log; do sleep 20; done
$S p2-principled-replay 900 python3 $M/tools/replay_nobuild.py $V/p2-principled-superset/out/on $V/p2-principled-replay/out
$S p2-nodes-replay 900 python3 $M/tools/replay_nobuild.py $V/p2-nodes-gate/out/nodes_render $V/p2-nodes-replay/out
echo "[$(date +%H:%M:%S)] slotP_D DONE" >> $M/timeline.log
