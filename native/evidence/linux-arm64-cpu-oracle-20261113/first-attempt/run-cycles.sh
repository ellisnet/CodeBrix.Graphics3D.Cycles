# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
cd <repo>
R=native/work/linux-arm64-oracle-20261113-records
J=${1:-8}; OUT=${2:-04b-cycles.txt}
date > $R/$OUT
docker exec cbc-arm64-oracle-20261113 bash -c "export CODEBRIX_ISPC=/work/CodeBrix.Graphics3D.Cycles/native/work/ispc/extracted/ispc-v1.30.0-linux.aarch64/bin/ispc; time python3 native/tools/build_cycles_linux_arm64.py --jobs $J --device-set" >> $R/$OUT 2>&1
echo "exit=$?" >> $R/$OUT; date >> $R/$OUT
