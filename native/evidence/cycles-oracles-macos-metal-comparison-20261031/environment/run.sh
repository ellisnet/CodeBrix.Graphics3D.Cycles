#!/bin/bash
# docs/TESTING-MACOS-ARM64.md section 9, steps 1-3: render the frozen comparison scenes and bsdf/velvet on the three
# macOS backends, then compare every film pairwise with compare.py (reference first).
cd /Volumes/VideoWork/GitHome/CodeBrix.Graphics3D.Cycles
W=native/work/metal-comparison-20261031
E=native/evidence/metal-comparison-linux-x64-20261031
RUN=native/work/oracles/metal-comparison-20261031
A64=native/work/oracles/linux-arm64-20261031
git status --porcelain > $W/render-status-start.txt; git rev-parse HEAD > $W/render-commit.txt
scenes=(); for f in $E/scenes/*.json native/oracle/scenes/bsdf/velvet.json; do scenes+=(--scene "$f"); done
date -u +%FT%TZ > $W/render-start.txt
/usr/bin/time -p -o $W/wall-render.txt python3 native/oracle/render_oracles.py --output $RUN \
  --runner native/work/macos/osx-arm64/oracle-build/codebrix_cycles_oracle \
  --host-library native/work/macos/osx-arm64/host/build/lib/libcodebrix_cycles_host.dylib \
  --backend cpu-arm64-macos --backend metal-m2pro --backend metal-m2pro-metalrt "${scenes[@]}" > $W/render.output.txt 2>&1
echo "render exit $?" > $W/render-exit.txt
date -u +%FT%TZ > $W/render-end.txt
mkdir -p $RUN/compare; : > $W/compare.output.txt
pair(){ # reference-label reference-dir candidate-label candidate-dir scene-file-label
  [ -d "$2" ] || { echo "skip (no reference film): $1 for $5" >> $W/compare.output.txt; return; }
  [ -d "$4" ] || { echo "skip (no candidate film): $3 for $5" >> $W/compare.output.txt; return; }
  echo "== $5: $3 vs $1" >> $W/compare.output.txt
  python3 native/oracle/compare.py "$2" "$4" --json "$RUN/compare/$5--$3--vs--$1.json" >> $W/compare.output.txt 2>&1
  echo "compare exit $? $5 $3 vs $1" >> $W/compare.output.txt; }
for s in velvet-subnormal radial-sides2-r0 radial-sides2-r0.3 radial-sides2-r0.5 radial-sides2-r0.75 radial-sides2-r1 radial-sides3-r0.5; do
  id=metal-comparison/$s
  for c in metal-m2pro metal-m2pro-metalrt cpu-arm64-macos; do
    pair cpu-x64-linux $E/cpu-x64-linux/oracle/$id/cpu-x64-linux $c $RUN/oracle/$id/$c $s
    pair cpu-x64-linux-ftz $E/cpu-x64-linux-ftz/oracle/$id/cpu-x64-linux $c $RUN/oracle/$id/$c $s
    pair wgsl-linux-intel $E/wgsl-linux-intel/oracle/$id/wgsl-linux-intel $c $RUN/oracle/$id/$c $s
  done
  for c in metal-m2pro metal-m2pro-metalrt; do pair cpu-arm64-macos $RUN/oracle/$id/cpu-arm64-macos $c $RUN/oracle/$id/$c $s; done
  pair metal-m2pro $RUN/oracle/$id/metal-m2pro metal-m2pro-metalrt $RUN/oracle/$id/metal-m2pro-metalrt $s
done
# bsdf/velvet has no Linux x64 film in the repository (added in af0a254dd); the Linux ARM64 CPU film of today's corpus run instead.
id=bsdf/velvet
for c in metal-m2pro metal-m2pro-metalrt; do pair cpu-arm64-macos $RUN/oracle/$id/cpu-arm64-macos $c $RUN/oracle/$id/$c bsdf__velvet; done
for c in metal-m2pro metal-m2pro-metalrt cpu-arm64-macos; do pair cpu-arm64-linux $A64/oracle/$id/cpu-arm64-linux $c $RUN/oracle/$id/$c bsdf__velvet; done
pair metal-m2pro $RUN/oracle/$id/metal-m2pro metal-m2pro-metalrt $RUN/oracle/$id/metal-m2pro-metalrt bsdf__velvet
echo done > $W/run-done.txt
