#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# step.sh <name> <command...>: runs the command from the repository root and records
# its exact command, UTC start, wall seconds, exit status and combined output.
name=$1; shift
d=native/work/macos-kit-20261117/steps
printf '%q ' "$@" > "$d/$name.cmd"; echo >> "$d/$name.cmd"
date -u +%Y-%m-%dT%H:%M:%SZ > "$d/$name.start"
s=$(python3 -c 'import time;print(time.time())')
"$@" > "$d/$name.output.txt" 2>&1
rc=$?
e=$(python3 -c 'import time;print(time.time())')
python3 -c "print(round($e-$s,2))" > "$d/$name.wall"
echo $rc > "$d/$name.exit"
echo "$name: exit $rc, wall $(cat "$d/$name.wall") s"
exit $rc
