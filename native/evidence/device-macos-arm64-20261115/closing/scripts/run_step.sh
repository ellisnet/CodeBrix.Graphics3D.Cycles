#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Usage: run_step.sh NAME COMMAND...   (run from the repository root)
# Writes NAME.start, NAME.end (UTC ISO times), NAME.output.txt (stdout+stderr, then the /usr/bin/time -p lines) and NAME.exit.
R=/Volumes/VideoWork/GitHome/CodeBrix.Graphics3D.Cycles/native/work/macos-arm64-closing-20261115
name=$1; shift
cd /Volumes/VideoWork/GitHome/CodeBrix.Graphics3D.Cycles || exit 99
date -u +%FT%TZ > "$R/$name.start"
s=$(python3 -c 'import time;print(time.time())')
/usr/bin/time -p "$@" > "$R/$name.output.txt" 2>&1
rc=$?
e=$(python3 -c 'import time;print(time.time())')
date -u +%FT%TZ > "$R/$name.end"
echo "$rc" > "$R/$name.exit"
python3 -c "print(round($e-$s,2))" > "$R/$name.wall"
echo "exit $rc" >> "$R/$name.output.txt"
exit $rc
