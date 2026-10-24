#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 6, slot E (lane m1-asset checks, after slot A's dotnet builds): glTF tool on its sample model and the
# textured-plane control (CPU vs WGSL), the bench bundle (make_bench_bundle.py, verified against the committed manifest)
# and the control bundle for part 2, oracle runs (starters, bench, control), comparator proof (starters perfect,
# corrupted films fail).
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge8; V=$M/validation; S=$M/tools/slot.sh
B=$R/native/work/cycles-build; TOOL=$R/tools/gltf/CycleGltfRender/bin/Release/net10.0/CodeBrix.Graphics3D.Cycles.GltfRender
MODEL=/home/jeremy/GitHome/Oasys/Assets/ForestCommunity/Domes/ForestVillage/CommunityProps/bench_01.gltf
O="--runner $B/bin/codebrix_cycles_oracle --host-library $B/lib/libcodebrix_cycles_host.so --backend cpu-x64-linux --backend wgsl-linux-intel"
cd $R; mkdir -p $V/e-gltf
$S e-gltf-sample-write 300 $TOOL --write-sample $V/e-gltf/sample.glb
$S e-gltf-sample 900 $TOOL $V/e-gltf/sample.glb --compare --output $V/e-gltf/sample.film.bin
$S e-gltf-plane-write 300 $TOOL --write-sample $V/e-gltf/plane.glb --sample-textured
$S e-gltf-plane 900 $TOOL $V/e-gltf/plane.glb --material-mapping blender --textures $V/e-gltf/plane.glb.textures --width 64 --height 48 --samples 16 --bundle-id gltf/textured-plane --compare --output $V/e-gltf/plane.film.bin --write-bundle $M/bundles/control
$S e-bench-bundle 1800 python3 native/oracle/scenes/make_bench_bundle.py --model $MODEL --output $M/bundles/bench --tool $TOOL
$S e-oracle-starters 1800 python3 native/oracle/render_oracles.py --output $M/oracles/starters $O
$S e-oracle-bench 1800 python3 native/oracle/render_oracles.py --output $M/oracles/bench $O --scene $M/bundles/bench/bench_01.json
$S e-oracle-control 1800 python3 native/oracle/render_oracles.py --output $M/oracles/control $O --scene $M/bundles/control/scene.json
$S e-compare-selftest 600 python3 native/oracle/compare.py --self-test
mkdir -p $V/e-wgsl-dump-hdri
CODEBRIX_CYCLES_WGSL_DUMP=$V/e-wgsl-dump-hdri $S e-oracle-hdri 1800 python3 native/oracle/render_oracles.py --output $M/oracles/hdri $O --scene $M/bundles/bench/bench_01-hdri.json
$S e-gltf-bench 1800 $TOOL $MODEL --material-mapping blender --width 64 --height 48 --samples 16 --compare --output $V/e-gltf/bench.film.bin
mkdir -p $V/e-comparator
for sc in diffuse-triangle glass-and-area-light glossy-sphere-point-light textured-plane; do $S e-compare-$sc 600 python3 native/oracle/compare.py --scene-root $M/oracles/starters $sc --json $V/e-comparator/$sc.json; done
$S e-compare-control 600 python3 native/oracle/compare.py --scene-root $M/oracles/control gltf/textured-plane --json $V/e-comparator/control.json
$S e-compare-bench 600 python3 native/oracle/compare.py --scene-root $M/oracles/bench assets/bench_01 --json $V/e-comparator/bench.json
$S e-compare-hdri 600 python3 native/oracle/compare.py --scene-root $M/oracles/hdri assets/bench_01-hdri --json $V/e-comparator/hdri.json
$S e-envelope-bench-hdri 3600 python3 native/tools/intel_compile_envelope.py --dump $V/e-wgsl-dump-hdri --debug-file-directory $R/native/work/merge4/debug --output $V/e-envelope-bench-hdri/out
echo "[$(date +%H:%M:%S)] slotE DONE" >> $M/timeline.log
