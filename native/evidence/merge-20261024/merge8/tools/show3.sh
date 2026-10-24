#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# show diff3-style conflict hunks of merge-<lane>/<path>: show3.sh <lane> <path>
D=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/merge8/merge-$1/$2
git merge-file -p --diff3 -L ours -L base -L theirs "$D.ours" "$D.base" "$D.theirs" > "$D.diff3"
awk '/^<<<<<<< ours/{on=1; print "---- line " NR} on{print} /^>>>>>>> theirs/{on=0}' "$D.diff3"
