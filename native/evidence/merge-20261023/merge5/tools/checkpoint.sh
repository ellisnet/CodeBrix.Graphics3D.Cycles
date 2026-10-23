#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 5 checkpoint: combined diff of main against pre-merge (tracked diff vs HEAD is merge 4 + wave 5, so the
# wave-5-only diff is produced from pre-merge copies) + status.md. usage: checkpoint.sh "<status text>"
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge5; C=$M/checkpoint; mkdir -p $C
python3 $M/tools/wave5_diff.py $C/changes.patch > /dev/null
{ echo "# Merge wave 5 checkpoint $(date '+%Y-%m-%d %H:%M:%S')"; echo; echo "$1"; echo; echo "changes.patch sha256 $(sha256sum $C/changes.patch | cut -d' ' -f1)"; echo; echo '## timeline (tail)'; tail -25 $M/timeline.log; } > $C/status.md
echo "[$(date +%H:%M:%S)] checkpoint written ($(sha256sum $C/changes.patch | cut -c1-16))" >> $M/timeline.log
