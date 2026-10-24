#!/bin/bash
# Merge wave 8 (copied from merge wave 7) part 2: build comparator roots (Linux CPU + Linux WGSL from oracles-p2, plus the device WGSL films of the managed bundle-mode runs) and run compare.py.
M=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/merge8; R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; C=$M/validation/p2-cmp-root; rm -rf $C; mkdir -p $M/validation/p2-comparator-all
cd $R
mk(){ name=$1; scenepath=$2; src=$3   # name: control|bench|hdri ; scenepath e.g. gltf/textured-plane ; src: oracles-p2 dir
  mkdir -p $C/$name/oracle/$scenepath; cp -r $M/oracles-p2/$src/oracle/$scenepath/. $C/$name/oracle/$scenepath/
  for dev in samsung hp; do d=$M/android-app/bundle-$4-$dev/$dev/oracle/wgsl-android-$dev; [ -d $d ] && cp -r $d $C/$name/oracle/$scenepath/wgsl-android-$dev || echo "MISSING $d"; done; }
mk control gltf/textured-plane control control
mk bench assets/bench_01 bench bench
mk hdri assets/bench_01-hdri hdri hdri
for s in "control gltf/textured-plane" "bench assets/bench_01" "hdri assets/bench_01-hdri"; do set -- $s
  python3 native/oracle/compare.py --scene-root $C/$1 $2 --json $M/validation/p2-comparator-all/$1-all4.json > $M/validation/p2-comparator-all/$1-all4.txt 2>&1; echo "$1 exit=$?"; tail -3 $M/validation/p2-comparator-all/$1-all4.txt | cut -c1-300; done
