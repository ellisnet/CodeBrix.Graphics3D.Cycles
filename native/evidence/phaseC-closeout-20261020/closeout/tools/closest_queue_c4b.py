# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Phase C close-out, second attempt: the maintained closest_queue_gate.py 'run' action unchanged, but its
# device_gate.py prerequisite is skipped. That prerequisite ran in the first attempt (closest-queue/device-queue):
# the device gate binary passed, and only device_gate.py's own stale expectations failed
# (selectionCases 14 vs 7, sortCases 12 vs 6: gate.cpp now repeats selection/sorting with and without
# transparent_shadows, committed at HEAD). device_gate.py was not changed.
import sys
from pathlib import Path
sys.path.insert(0, '/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/tools')
import camera_queue_gate, closest_queue_gate
out = Path(sys.argv[1]).resolve()
camera_queue_gate.run_controls = lambda: None
camera_queue_gate.OUT = out/'camera-queue'; closest_queue_gate.OUT = out/'closest-queue'
closest_queue_gate.run()
