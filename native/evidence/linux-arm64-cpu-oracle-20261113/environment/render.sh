# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
cd /work/CodeBrix.Graphics3D.Cycles
RUN=native/work/oracles/linux-arm64-20261113
RUNNER=native/work/linux/linux-arm64/host/build/bin/codebrix_cycles_oracle
HOSTLIB=native/work/linux/linux-arm64/host/build/lib/libcodebrix_cycles_host.so
export RUN RUNNER HOSTLIB
n=$(find native/oracle/scenes -name "*.json" ! -name manifest.json ! -path "*/assets/*" | wc -l); echo "find count $n"
date; S=$(date +%s)
bash -c 'scenes=(); for f in $(find native/oracle/scenes -name "*.json" ! -name manifest.json ! -path "*/assets/*" | sort) native/work/oracle-assets/bench_01/bench_01.json native/work/oracle-assets/bench_01/bench_01-hdri.json; do scenes+=(--scene "$f"); done; python3 native/oracle/render_oracles.py --output "$RUN" --runner "$RUNNER" --host-library "$HOSTLIB" --backend cpu-arm64-linux "${scenes[@]}"'
echo "RENDER EXIT $? wall $(( $(date +%s)-S ))s"
