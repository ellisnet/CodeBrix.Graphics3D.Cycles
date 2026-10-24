#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 8 part 1b, final slot: the three runs that met leftovers of the aborted first 1b run (fresh folders),
# and the eight C1 comparator runs + the six Mac comparisons with the ISA-family compare.py.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge8; V=$M/validation; S=$M/tools/slot.sh; T=$R/native/tools; B=$R/native/work/cycles-build/bin
TOOL=$R/tools/gltf/CycleGltfRender/bin/Release/net10.0/CodeBrix.Graphics3D.Cycles.GltfRender
E1=$R/native/evidence/cycles-oracles-macos-20261023/oracle; E2=$R/native/evidence/cycles-oracles-macos-20261023-2/oracle
cd $R; mkdir -p $M/bundles-1b $V/b8f-comparator $V/b8f-comparator-mac
$S b8f-gltf-plane 900 $TOOL $V/b8-gltf/plane.glb --material-mapping blender --textures $V/b8-gltf/plane.glb.textures --width 64 --height 48 --samples 16 --bundle-id gltf/textured-plane --compare --output $V/b8-gltf/plane-f.film.bin --write-bundle $M/bundles-1b/control
$S b8f-bench-bundle 1800 python3 native/oracle/scenes/make_bench_bundle.py --output $M/bundles-1b/bench --tool $TOOL
$S b8f-reference-export 2400 python3 $T/android_reference.py export --bin $B --libm-record $M/reference-1b/libm-linux-x64-glibc.json --output $M/reference-1b/linux-x64 --build-record $R/native/work/cycles-build.json --job triangle_sample=triangle_sample --job emitter_sample=emitter_sample --job triangle_lighting=triangle_lighting --job glass_render=glass_render --job lighting_mis=lighting_mis --job transparent_lighting=transparent_lighting --job principled_render=principled_render --job nodes_render=nodes_render
$S b8f-compare-selftest 600 python3 native/oracle/compare.py --self-test
for sc in diffuse-triangle glass-and-area-light glossy-sphere-point-light textured-plane; do
  $S b8f-compare-$sc 600 python3 native/oracle/compare.py --scene-root $M/oracles-1b/starters starter/$sc --json $V/b8f-comparator/$sc.json
  L=$M/oracles-1b/starters/oracle/starter/$sc
  $S b8f-macmp-$sc 600 python3 native/oracle/compare.py $L/cpu-x64-linux $L/wgsl-linux-intel $E1/starter/$sc/cpu-arm64-macos $E1/starter/$sc/metal-m2pro $E1/starter/$sc/metal-m2pro-metalrt --json $V/b8f-comparator-mac/$sc.json
done
$S b8f-compare-control 600 python3 native/oracle/compare.py --scene-root $M/oracles-1b/control gltf/textured-plane --json $V/b8f-comparator/control.json
$S b8f-compare-bench 600 python3 native/oracle/compare.py --scene-root $M/oracles-1b/bench assets/bench_01 --json $V/b8f-comparator/bench.json
$S b8f-compare-hdri 600 python3 native/oracle/compare.py --scene-root $M/oracles-1b/hdri assets/bench_01-hdri --json $V/b8f-comparator/hdri.json
L=$M/oracles-1b/bench/oracle/assets/bench_01; $S b8f-macmp-bench 600 python3 native/oracle/compare.py $L/cpu-x64-linux $L/wgsl-linux-intel $E2/assets/bench_01/cpu-arm64-macos $E2/assets/bench_01/metal-m2pro $E2/assets/bench_01/metal-m2pro-metalrt --json $V/b8f-comparator-mac/bench.json
L=$M/oracles-1b/hdri/oracle/assets/bench_01-hdri; $S b8f-macmp-hdri 600 python3 native/oracle/compare.py $L/cpu-x64-linux $L/wgsl-linux-intel $E2/assets/bench_01-hdri/cpu-arm64-macos $E2/assets/bench_01-hdri/metal-m2pro $E2/assets/bench_01-hdri/metal-m2pro-metalrt --json $V/b8f-comparator-mac/hdri.json
for n in light-tree_lamps-and-mesh-lights light-tree_light-linking shadow-catcher_catcher-point-light shadow-catcher_catcher-approximate ao_fast-gi-additive ao_ao-node mnee_glass-sphere-caustic; do
  id=$(echo $n | sed 's#_#/#'); $S b8f-compare-$n 600 python3 native/oracle/compare.py --scene-root $M/oracles-1b/m2/$n $id --json $V/b8f-comparator/$n.json; done
echo "[$(date +%H:%M:%S)] slot1b_F2 DONE" >> $M/timeline.log
