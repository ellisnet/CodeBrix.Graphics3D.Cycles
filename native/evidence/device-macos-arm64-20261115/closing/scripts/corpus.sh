#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Part E closing corpus (docs/TESTING-MACOS-ARM64.md step 4 with the kit's find command, all families):
# the whole committed corpus plus the two bench documents, on cpu-arm64-macos, metal-m2pro and metal-m2pro-metalrt.
cd /Volumes/VideoWork/GitHome/CodeBrix.Graphics3D.Cycles || exit 99
R=native/work/macos-arm64-closing-20261115
find native/oracle/scenes -name "*.json" ! -name manifest.json ! -path "*/assets/*" | wc -l > $R/corpus-find-count.txt
git status --porcelain > $R/corpus-git-status-before.txt
if [ -s $R/corpus-git-status-before.txt ]; then echo "working tree not clean; render not started" >> $R/corpus-git-status-before.txt; exit 98; fi
git rev-parse HEAD > $R/corpus-head.txt
scenes=()
for f in $(find native/oracle/scenes -name "*.json" ! -name manifest.json ! -path "*/assets/*" | sort) native/work/oracle-assets/bench_01/bench_01.json native/work/oracle-assets/bench_01/bench_01-hdri.json; do scenes+=(--scene "$f"); done
echo "${#scenes[@]} arguments ($(( ${#scenes[@]} / 2 )) scenes)" >> $R/corpus-find-count.txt
python3 native/oracle/render_oracles.py --output native/work/oracles/macos-arm64-closing-20261115 \
  --runner native/work/macos/osx-arm64/oracle-build/codebrix_cycles_oracle \
  --host-library native/work/macos/osx-arm64/host/build/lib/libcodebrix_cycles_host.dylib \
  --backend cpu-arm64-macos --backend metal-m2pro --backend metal-m2pro-metalrt "${scenes[@]}"
rc=$?
git status --porcelain > $R/corpus-git-status-after.txt
exit $rc
