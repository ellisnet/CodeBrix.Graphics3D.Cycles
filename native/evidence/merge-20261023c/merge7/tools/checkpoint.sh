#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 7 checkpoint: combined diff of main against d21bd4a5 (git diff --binary; new files are intent-to-add) + status.md.
# usage: checkpoint.sh "<status text>"
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge7; C=$M/checkpoint; mkdir -p $C; cd $R
git diff --binary d21bd4a58f489afeafe2a3046ab32a556578f0ec > $C/changes.patch
{ echo "# Merge wave 7 checkpoint $(date '+%Y-%m-%d %H:%M:%S')"; echo; echo "$1"; echo; echo "changes.patch sha256 $(sha256sum $C/changes.patch | cut -d' ' -f1) ($(grep -c '^diff --git' $C/changes.patch) files)"; echo; echo '## timeline (tail)'; tail -25 $M/timeline.log; } > $C/status.md
echo "[$(date +%H:%M:%S)] checkpoint written ($(sha256sum $C/changes.patch | cut -c1-16))" >> $M/timeline.log
