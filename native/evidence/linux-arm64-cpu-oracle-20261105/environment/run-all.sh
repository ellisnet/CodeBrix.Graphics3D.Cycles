#!/bin/bash
# docs/TESTING-LINUX-ARM64-ORACLE.md sections 4 (build), 5 (render), 6 (compare) and 9 (checklist), inside the container.
cd /work/CodeBrix.Graphics3D.Cycles
L=native/work/linux-arm64-oracle-20261105
RUN=native/work/oracles/linux-arm64-20261105
export CODEBRIX_ISPC=/work/CodeBrix.Graphics3D.Cycles/native/work/ispc/extracted/ispc-v1.30.0-linux.aarch64/bin/ispc
{ echo "CODEBRIX_ISPC=$CODEBRIX_ISPC"; "$CODEBRIX_ISPC" --version; sha256sum "$CODEBRIX_ISPC"; } > $L/ispc.txt 2>&1
git rev-parse HEAD > $L/commit.txt; git status --porcelain > $L/status-before-build.txt
# Section 4: build, from an empty native/work/linux/linux-arm64/.
date -u +%FT%TZ > $L/build-start.txt
/usr/bin/time -p -o $L/wall-dependencies.txt python3 tools/dependencies/build_linux_arm64.py --jobs 8 > $L/dependencies.output.txt 2>&1
rc=$?; echo "deps exit $rc" > $L/build-exit.txt
[ $rc -eq 0 ] || { date -u +%FT%TZ > $L/build-end.txt; exit 1; }
/usr/bin/time -p -o $L/wall-cycles.txt python3 native/tools/build_cycles_linux_arm64.py --jobs 8 --device-set > $L/cycles-build.output.txt 2>&1
rc=$?; echo "cycles exit $rc" >> $L/build-exit.txt; date -u +%FT%TZ > $L/build-end.txt
[ $rc -eq 0 ] || exit 1
RUNNER=$(find native/work/linux/linux-arm64 -name codebrix_cycles_oracle -type f -perm -u+x | head -1)
HOSTLIB=native/work/linux/linux-arm64/host/build/lib/libcodebrix_cycles_host.so
{ echo "RUNNER=$RUNNER"; echo "HOSTLIB=$HOSTLIB"; ls -l "$RUNNER" "$HOSTLIB"; sha256sum "$RUNNER" "$HOSTLIB"; ldd "$HOSTLIB" | grep wgpu; } > $L/paths.txt 2>&1
# Section 5 / checklist step 2: the whole committed corpus plus the two bench bundle documents.
git status --porcelain > $L/render-status-start.txt
scenes=(); for f in $(find native/oracle/scenes -name "*.json" ! -name manifest.json ! -path "*/assets/*" | sort) \
    native/work/oracle-assets/bench_01/bench_01.json native/work/oracle-assets/bench_01/bench_01-hdri.json; do
  scenes+=(--scene "$f"); done
expected=$(( $(find native/oracle/scenes -name "*.json" ! -name manifest.json ! -path "*/assets/*" | wc -l) + 2 ))
{ echo "expected (count rule) $expected; listed $(( ${#scenes[@]} / 2 ))"; printf '%s\n' "${scenes[@]}" | grep -v '^--scene$'; } > $L/render-scenes.txt
date -u +%FT%TZ > $L/render-start.txt
/usr/bin/time -p -o $L/wall-render.txt python3 native/oracle/render_oracles.py --output "$RUN" \
  --runner "$RUNNER" --host-library "$HOSTLIB" --backend cpu-arm64-linux "${scenes[@]}" > $L/render.output.txt 2>&1
echo "render exit $?" > $L/render-exit.txt; date -u +%FT%TZ > $L/render-end.txt
# Section 6: information only, against the newest committed x64 reference. Corpus films first, the bench films under oracles/bench.
X=native/evidence/linux-x64-reference-20261101/oracles; mkdir -p "$RUN/vs-x64"; : > $L/compare.output.txt
for d in "$RUN"/oracle/*/*/cpu-arm64-linux; do s=${d#"$RUN"/oracle/}; s=${s%/cpu-arm64-linux}
  ref=""; for set in corpus bench; do [ -d "$X/$set/oracle/$s/cpu-x64-linux" ] && { ref="$X/$set/oracle/$s/cpu-x64-linux"; break; }; done
  [ -n "$ref" ] || { echo "no x64 film: $s" >> $L/compare.output.txt; continue; }
  echo "== $s ($ref)" >> $L/compare.output.txt
  python3 native/oracle/compare.py "$ref" "$d" --json "$RUN/vs-x64/${s//\//__}.json" >> $L/compare.output.txt 2>&1
  echo "compare exit $? $s" >> $L/compare.output.txt
done
echo done > $L/run-done.txt
