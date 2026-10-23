#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 5, slot D: Intel compile envelope rerun with merge wave 4's Mesa debug-link folder (slot C pointed at an
# empty folder, so gdb had no symbols and every entry reported no IP; exit 1, not a measurement).
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge5; V=$M/validation; S=$M/tools/slot.sh
T=$R/native/tools; DBG=$R/native/work/merge4/debug
cd $R
$S d-envelope-abi 3600 python3 $T/intel_compile_envelope.py --dump $V/c-abi-dump/dump --debug-file-directory $DBG --output $V/d-envelope-abi/out
$S d-envelope-materials 5400 python3 $T/intel_compile_envelope.py --dump $M/envelope-set --debug-file-directory $DBG --output $V/d-envelope-materials/out --modules surface,background --entries prepare_surface,shade_surface,shade_background,shade_surface_light,shade_surface_light_triangle,connect_surface_light
echo "[$(date +%H:%M:%S)] slotD DONE" >> $M/timeline.log
