#!/bin/bash
# docs/TESTING-LINUX-ARM64-ORACLE.md sections 4 (paths), 5 (render) and 6 (compare), run inside the container.
cd /work/CodeBrix.Graphics3D.Cycles
L=native/work/linux-arm64-oracle-20261031
RUN=native/work/oracles/linux-arm64-20261031
RUNNER=$(find native/work/linux/linux-arm64 -name codebrix_cycles_oracle -type f -perm -u+x | head -1)
HOSTLIB=native/work/linux/linux-arm64/host/build/lib/libcodebrix_cycles_host.so
{ echo "RUNNER=$RUNNER"; echo "HOSTLIB=$HOSTLIB"; ls -l "$RUNNER" "$HOSTLIB"; sha256sum "$RUNNER" "$HOSTLIB"; } > $L/paths.txt 2>&1
git rev-parse HEAD > $L/render-commit.txt; git status --porcelain > $L/render-status-start.txt
scenes=(); for f in $(find native/oracle/scenes -name "*.json" ! -name manifest.json ! -path "*/assets/*" | sort) \
    native/work/oracle-assets/bench_01/bench_01.json native/work/oracle-assets/bench_01/bench_01-hdri.json; do
  scenes+=(--scene "$f"); done
echo "${#scenes[@]} arguments ($(( ${#scenes[@]} / 2 )) scenes)" > $L/render-scenes.txt
printf '%s\n' "${scenes[@]}" | grep -v '^--scene$' >> $L/render-scenes.txt
date -u +%FT%TZ > $L/render-start.txt
/usr/bin/time -p -o $L/wall-render.txt python3 native/oracle/render_oracles.py --output "$RUN" \
  --runner "$RUNNER" --host-library "$HOSTLIB" --backend cpu-arm64-linux "${scenes[@]}" > $L/render.output.txt 2>&1
echo "render exit $?" > $L/render-exit.txt
date -u +%FT%TZ > $L/render-end.txt
# Section 6: information only. Corpus films first, the bench films under oracles/bench.
X=native/evidence/linux-x64-reference-20261030/oracles; mkdir -p "$RUN/vs-x64"
: > $L/compare.output.txt
for d in "$RUN"/oracle/*/*/cpu-arm64-linux; do s=${d#"$RUN"/oracle/}; s=${s%/cpu-arm64-linux}
  ref=""; for set in corpus bench; do [ -d "$X/$set/oracle/$s/cpu-x64-linux" ] && { ref="$X/$set/oracle/$s/cpu-x64-linux"; break; }; done
  [ -n "$ref" ] || { echo "no x64 film: $s" >> $L/compare.output.txt; continue; }
  echo "== $s ($ref)" >> $L/compare.output.txt
  python3 native/oracle/compare.py "$ref" "$d" --json "$RUN/vs-x64/${s//\//__}.json" >> $L/compare.output.txt 2>&1
  echo "compare exit $? $s" >> $L/compare.output.txt
done
echo done > $L/render-done.txt
