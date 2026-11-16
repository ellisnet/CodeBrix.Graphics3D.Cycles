# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
cd /work/CodeBrix.Graphics3D.Cycles
export RUN=native/work/oracles/linux-arm64-20261115
export RUNNER=native/work/linux/linux-arm64/host/build/bin/codebrix_cycles_oracle
export HOSTLIB=native/work/linux/linux-arm64/host/build/lib/libcodebrix_cycles_host.so
s=$(date +%s); echo "render start $(date)"
bash -c 'scenes=(); for f in $(find native/oracle/scenes -name "*.json" ! -name manifest.json ! -path "*/assets/*" | sort) native/work/oracle-assets/bench_01/bench_01.json native/work/oracle-assets/bench_01/bench_01-hdri.json; do scenes+=(--scene "$f"); done; python3 native/oracle/render_oracles.py --output "$RUN" --runner "$RUNNER" --host-library "$HOSTLIB" --backend cpu-arm64-linux "${scenes[@]}"' > native/work/linux-arm64-oracle-20261115-records/render.log 2>&1
echo "render exit $? wall $(( $(date +%s)-s ))s"
