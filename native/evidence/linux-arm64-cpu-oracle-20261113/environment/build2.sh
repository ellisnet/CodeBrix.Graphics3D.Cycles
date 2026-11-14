# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
cd /work/CodeBrix.Graphics3D.Cycles
export CODEBRIX_ISPC=/work/CodeBrix.Graphics3D.Cycles/native/work/ispc/extracted/ispc-v1.30.0-linux.aarch64/bin/ispc
J=${1:-8}
date; S=$(date +%s)
python3 native/tools/build_cycles_linux_arm64.py --jobs $J --device-set; echo "CYCLES EXIT $? wall $(( $(date +%s)-S ))s"
