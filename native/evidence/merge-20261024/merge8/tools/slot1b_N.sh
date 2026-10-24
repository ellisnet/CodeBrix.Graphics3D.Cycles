#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 8 part 1b, slot N (+ MNEE gate and oracle scene): the gates new in wave A (light tree 7 cases + replays, shadow catcher 11 cases + 6 replays,
# AO state 9 (incl. the raytrace-queue section) and AO image 8) with WGSL dumps, their Intel compile envelope, and the
# lanes' oracle scenes (Linux CPU + WGSL) through the comparator.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge8; V=$M/validation; S=$M/tools/slot.sh
T=$R/native/tools; B=$R/native/work/cycles-build
O="--runner $B/bin/codebrix_cycles_oracle --host-library $B/lib/libcodebrix_cycles_host.so --backend cpu-x64-linux --backend wgsl-linux-intel"
cd $R; mkdir -p $V/b8-m2-dump/light-tree $V/b8-m2-dump/shadow-catcher $V/b8-m2-dump/ao-state $V/b8-m2-dump/ao-render $V/b8-m2-dump/mnee $V/b8-ao-state $V/b8-ao-render
CODEBRIX_CYCLES_WGSL_DUMP=$V/b8-m2-dump/light-tree $S b8-light-tree 2400 python3 $T/light_tree_gate.py --skip-build --output $V/b8-light-tree/out
CODEBRIX_CYCLES_WGSL_DUMP=$V/b8-m2-dump/shadow-catcher $S b8-shadow-catcher 2400 python3 $T/shadow_catcher_gate.py --skip-build --output $V/b8-shadow-catcher/out
CODEBRIX_CYCLES_WGSL_DUMP=$V/b8-m2-dump/ao-state $S b8-ao-state 900 $B/bin/codebrix_cycles_ao_state_gate $V/b8-ao-state/run
CODEBRIX_CYCLES_WGSL_DUMP=$V/b8-m2-dump/ao-render $S b8-ao-render 1800 $B/bin/codebrix_cycles_ao_render_gate $V/b8-ao-render/run
CODEBRIX_CYCLES_WGSL_DUMP=$V/b8-m2-dump/mnee $S b8-mnee 2400 python3 $T/mnee_gate.py --skip-build --output $V/b8-mnee/out
mkdir -p $V/b8-comparator-m2
for sc in light-tree/lamps-and-mesh-lights light-tree/light-linking shadow-catcher/catcher-point-light shadow-catcher/catcher-approximate ao/fast-gi-additive ao/ao-node mnee/glass-sphere-caustic; do
  n=$(echo $sc | tr / _)
  $S b8-oracle-$n 1800 python3 native/oracle/render_oracles.py --output $M/oracles-1b/m2/$n $O --scene native/oracle/scenes/$sc.json
  id=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" native/oracle/scenes/$sc.json)
  $S b8-compare-$n 600 python3 native/oracle/compare.py --scene-root $M/oracles-1b/m2/$n $id --json $V/b8-comparator-m2/$n.json
done
$S b8-envelope-m2 5400 python3 $T/intel_compile_envelope.py --dump $V/b8-m2-dump --debug-file-directory $R/native/work/merge4/debug --output $V/b8-envelope-m2/out
echo "[$(date +%H:%M:%S)] slot1b_N DONE" >> $M/timeline.log
