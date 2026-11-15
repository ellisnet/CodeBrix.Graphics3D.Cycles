# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
cd /work/CodeBrix.Graphics3D.Cycles
R=native/work/linux-arm64-oracle-20261115-records
export CODEBRIX_ISPC=/work/CodeBrix.Graphics3D.Cycles/native/work/ispc/extracted/ispc-v1.30.0-linux.aarch64/bin/ispc
git config --global --add safe.directory /work/CodeBrix.Graphics3D.Cycles
s=$(date +%s); echo "deps start $(date)"
python3 tools/dependencies/build_linux_arm64.py --jobs 8 > $R/build-deps.log 2>&1; echo "deps exit $? wall $(( $(date +%s)-s ))s"
s=$(date +%s); echo "cycles start $(date)"
python3 native/tools/build_cycles_linux_arm64.py --jobs 8 --device-set > $R/build-cycles.log 2>&1; echo "cycles exit $? wall $(( $(date +%s)-s ))s"
RUNNER=$(find native/work/linux/linux-arm64 -name codebrix_cycles_oracle -type f -perm -u+x | grep -v host- | head -1)
HOSTLIB=native/work/linux/linux-arm64/host/build/lib/libcodebrix_cycles_host.so
echo "$RUNNER"; ls -l "$HOSTLIB"
