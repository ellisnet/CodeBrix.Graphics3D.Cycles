#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 7 part 2, Linux stream E (labels p2-*): the Linux flow (device-scene exchange + judge) for the render_gate jobs that join it
# at wave 6, the glTF tool, bundles (re-generated into bundles-p2/, compared with the staged ones), oracle runs and the comparator.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge7; V=$M/validation; S=$M/tools/slot.sh; F=$M/tools/linux_flow.sh
B=$R/native/work/cycles-build; TOOL=$R/tools/gltf/CycleGltfRender/bin/Release/net10.0/CodeBrix.Graphics3D.Cycles.GltfRender
MODEL=/home/jeremy/GitHome/Oasys/Assets/ForestCommunity/Domes/ForestVillage/CommunityProps/bench_01.gltf
O="--runner $B/bin/codebrix_cycles_oracle --host-library $B/lib/libcodebrix_cycles_host.so --backend cpu-x64-linux --backend wgsl-linux-intel"
cd $R; mkdir -p $V/p2-gltf $M/bundles-p2
for j in nodes_render principled_render microfacet_render; do $S p2-s-$j 2400 bash $F $j $V/p2-s-$j/flow; done
$S p2-s-render-static 1800 bash $F render $V/p2-s-render-static/flow
for m in 0 1 2 3 4 5 6 7; do $S p2-s-render-motion-$m 1800 bash $F render $V/p2-s-render-motion-$m/flow --motion=$m; done
CODEBRIX_VOLUME_ABSORPTION_ONLY=1 $S p2-s-volume-absorption 1800 bash $F volume_render $V/p2-s-volume-absorption/flow
CODEBRIX_VOLUME_G=0 $S p2-s-volume-isotropic 1800 bash $F volume_render $V/p2-s-volume-isotropic/flow
CODEBRIX_VOLUME_G=.45 $S p2-s-volume-forward 1800 bash $F volume_render $V/p2-s-volume-forward/flow
CODEBRIX_VOLUME_G=-.65 $S p2-s-volume-backward 1800 bash $F volume_render $V/p2-s-volume-backward/flow
CODEBRIX_VOLUME_CLOSED_COUNT=1 $S p2-s-closed-single 1800 bash $F closed_volume_render $V/p2-s-closed-single/flow
CODEBRIX_VOLUME_CLOSED_COUNT=4 $S p2-s-closed-overlap 1800 bash $F closed_volume_render $V/p2-s-closed-overlap/flow
CODEBRIX_VOLUME_CLOSED_COUNT=4 CODEBRIX_VOLUME_CAMERA_INSIDE=1 $S p2-s-closed-inside 1800 bash $F closed_volume_render $V/p2-s-closed-inside/flow
$S p2-gltf-sample-write 300 $TOOL --write-sample $V/p2-gltf/sample.glb
$S p2-gltf-sample 900 $TOOL $V/p2-gltf/sample.glb --compare --output $V/p2-gltf/sample.film.bin
$S p2-gltf-plane-write 300 $TOOL --write-sample $V/p2-gltf/plane.glb --sample-textured
$S p2-gltf-plane 900 $TOOL $V/p2-gltf/plane.glb --material-mapping blender --textures $V/p2-gltf/plane.glb.textures --width 64 --height 48 --samples 16 --bundle-id gltf/textured-plane --compare --output $V/p2-gltf/plane.film.bin --write-bundle $M/bundles-p2/control
$S p2-bench-bundle 1800 python3 native/oracle/scenes/make_bench_bundle.py --model $MODEL --output $M/bundles-p2/bench --tool $TOOL
$S p2-oracle-starters 1800 python3 native/oracle/render_oracles.py --output $M/oracles-p2/starters $O
$S p2-oracle-bench 1800 python3 native/oracle/render_oracles.py --output $M/oracles-p2/bench $O --scene $M/bundles/bench/bench_01.json
$S p2-oracle-control 1800 python3 native/oracle/render_oracles.py --output $M/oracles-p2/control $O --scene $M/bundles/control/scene.json
$S p2-compare-selftest 600 python3 native/oracle/compare.py --self-test
mkdir -p $V/p2-gltf $V/p2-wgsl-dump-hdri
CODEBRIX_CYCLES_WGSL_DUMP=$V/p2-wgsl-dump-hdri $S p2-oracle-hdri 1800 python3 native/oracle/render_oracles.py --output $M/oracles-p2/hdri $O --scene $M/bundles/bench/bench_01-hdri.json
$S p2-gltf-bench 1800 $TOOL $MODEL --material-mapping blender --width 64 --height 48 --samples 16 --compare --output $V/p2-gltf/bench.film.bin
mkdir -p $V/p2-comparator
for sc in diffuse-triangle glass-and-area-light glossy-sphere-point-light textured-plane; do $S p2-compare-$sc 600 python3 native/oracle/compare.py --scene-root $M/oracles-p2/starters $sc --json $V/p2-comparator/$sc.json; done
$S p2-compare-control 600 python3 native/oracle/compare.py --scene-root $M/oracles-p2/control gltf/textured-plane --json $V/p2-comparator/control.json
$S p2-compare-bench 600 python3 native/oracle/compare.py --scene-root $M/oracles-p2/bench assets/bench_01 --json $V/p2-comparator/bench.json
$S p2-compare-hdri 600 python3 native/oracle/compare.py --scene-root $M/oracles-p2/hdri assets/bench_01-hdri --json $V/p2-comparator/hdri.json
$S p2-envelope-bench-hdri 3600 python3 native/tools/intel_compile_envelope.py --dump $V/p2-wgsl-dump-hdri --debug-file-directory $R/native/work/merge4/debug --output $V/p2-envelope-bench-hdri/out
echo "[$(date +%H:%M:%S)] slotP_E DONE" >> $M/timeline.log
