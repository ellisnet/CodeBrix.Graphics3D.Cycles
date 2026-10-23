#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 5 part 2, Linux stream D: the ledger's cited Linux gates re-run on the final tree (as lane z0 did: plain gates and
# render --attributes/--images/--normals, nodes_svm, nodes_world; nodes_render comes from e-nodes-render-superset), then the two
# Linux command replays (Principled and node render) with the part-1 render_replay binary (no rebuild).
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge5; V=$M/validation; S=$M/tools/slot.sh; B=$R/native/work/cycles-build/bin
cd $R
run(){ g=$1; $S ev-$g 1200 env -u OCIO $B/codebrix_cycles_${g}_gate $V/ev-$g/run; }
for g in render device camera background closest svm_world svm_surface attribute image image_file normal; do run $g; done
$S ev-render-attributes 1200 env -u OCIO $B/codebrix_cycles_render_gate $V/ev-render-attributes/run --attributes
$S ev-render-images 1200 env -u OCIO $B/codebrix_cycles_render_gate $V/ev-render-images/run --images
$S ev-render-normals 1200 env -u OCIO $B/codebrix_cycles_render_gate $V/ev-render-normals/run --normals
$S ev-nodes_svm 1200 env -u OCIO $B/codebrix_cycles_nodes_svm_gate $V/ev-nodes_svm/run
$S ev-nodes_world 900 env -u OCIO $B/codebrix_cycles_nodes_world_gate $V/ev-nodes_world/run
until grep -q "END e-principled-superset exit=" $M/timeline.log && grep -q "END e-nodes-gate exit=" $M/timeline.log && grep -q "END e-nodes-render-superset exit=" $M/timeline.log; do sleep 20; done
$S e-principled-replay 900 python3 $M/tools/replay_nobuild.py $V/e-principled-superset/out/on $V/e-principled-replay/out
$S e-nodes-replay 900 python3 $M/tools/replay_nobuild.py $V/e-nodes-gate/out/nodes_render $V/e-nodes-replay/out
echo "[$(date +%H:%M:%S)] slotE_D DONE" >> $M/timeline.log
