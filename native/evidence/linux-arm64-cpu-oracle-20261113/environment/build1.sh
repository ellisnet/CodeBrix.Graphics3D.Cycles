# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
cd /work/CodeBrix.Graphics3D.Cycles
export CODEBRIX_ISPC=/work/CodeBrix.Graphics3D.Cycles/native/work/ispc/extracted/ispc-v1.30.0-linux.aarch64/bin/ispc
git config --global --add safe.directory /work/CodeBrix.Graphics3D.Cycles
date; S=$(date +%s)
python3 tools/dependencies/build_linux_arm64.py --jobs 8; echo "DEPS EXIT $? wall $(( $(date +%s)-S ))s"
