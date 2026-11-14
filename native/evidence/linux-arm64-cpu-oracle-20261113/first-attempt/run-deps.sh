# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
cd <repo>
R=native/work/linux-arm64-oracle-20261113-records
J=${1:-8}
date > $R/04a-deps.txt
docker exec cbc-arm64-oracle-20261113 bash -c "export CODEBRIX_ISPC=/work/CodeBrix.Graphics3D.Cycles/native/work/ispc/extracted/ispc-v1.30.0-linux.aarch64/bin/ispc; git config --global --add safe.directory /work/CodeBrix.Graphics3D.Cycles; time python3 tools/dependencies/build_linux_arm64.py --jobs $J" >> $R/04a-deps.txt 2>&1
echo "exit=$?" >> $R/04a-deps.txt; date >> $R/04a-deps.txt
