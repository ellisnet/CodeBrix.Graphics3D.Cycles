#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 1: assemble native/DEVICE.md from the baseline, the five lanes' sections and the merge results."""
from pathlib import Path
M = Path(__file__).resolve().parents[1]; W = M/'docs-work'
s = (M/'base/native/DEVICE.md').read_text()
def rep(a, b, count=1):
    global s
    assert s.count(a) == count, (s.count(a), a[:80]); s = s.replace(a, b)
# ARM64 integrated-tree section: open bullets now point at their Phase E outcome (E2 + X1 + X2 wording).
a = s.index('- Open on ARM64 (also listed under "Open device items" below):'); b = s.index('## Android x64 (HP, Intel Mesa 24.0.8)')
s = s[:a] + '''- Open on ARM64 in this run, each revisited in Phase E (sections below):
  - Emissive-triangle sampling and the emitter-sample probe failed against the on-device (bionic) oracle. Under the
    validation reference policy the probe passes and only the deforming emitter still fails the sampler gate.
  - Triangle image mode 2 diverged by one path decision on Adreno 730, even when replaying the x64 capture; it passes
    since the exact traversal.
  - On the Pixel 4 XL, object/deforming motion images, feature combination, the volume-boundary state test and the
    camera-inside closed volume failed in this run; they pass after the Adreno 640 fix described in
    "Pixel 4 XL (Adreno 640) function-scope array fix" below.

''' + s[b:]
rep('''run there. The HP is not yet a full-fidelity device for emissive triangles.''',
    '''run there. Phase E resolved this: the first two compile since the emissive-triangle compile-load change, and
the unspecialized configuration is refused on Android (sections below).''')
# E3's two in-place edits.
rep('''shaders, transparent shadows and smooth shadow-terminator geometry offsets remain
explicitly unsupported. These remain required work. The direct-light contribution''',
    '''shaders and transparent shadows remain explicitly unsupported (smooth
shadow-terminator offsets were added in Phase E3; see below). These remain required work. The direct-light contribution''')
rep('''the Samsung and the Pixel. Smooth-shaded objects in these scenes use a zero
shadow-terminator geometry offset: the WebGPU device does not implement that
offset (found on Linux; a smooth mesh lit by a sun otherwise fails with
"WebGPU scheduler kernel reported invalid data (mask 1)").''',
    '''the Samsung and the Pixel. Smooth-shaded objects in these scenes used a zero
shadow-terminator geometry offset at the time; Phase E3 implemented the offsets
and the scenes now keep the default (confirmed on all three devices in the Phase E merge).''')
e1 = (W/'e1.md').read_text()
e1 = e1.replace('''oracles on the GPU means reproducing bionic's libm (a separate, licensed
implementation, different per Android ABI); that is a decision for the
maintainer, not a rounding adapter.''', '''oracles on the GPU would mean reproducing bionic's libm (a separate, licensed
implementation, different per Android ABI). The decision taken instead is the
validation reference policy below: the correctly rounded glibc reference decides,
bionic is a diagnostic.''')
e1 = e1.replace("Evidence for this section: `native/work/e1/` (`libm/`, `analysis/`, `logs/probe-table-*.jsonl`, `runs/`).", "Evidence for this section: `native/work/e1/` in the E1 worktree (`libm/`, `analysis/`, `logs/probe-table-*.jsonl`, `runs/`); the sealed handoff is in `native/evidence/phaseE-merge-20261020/laneE1/`.")
assert 'phaseE-merge-20261020/laneE1' in e1
assert 'decision for the\nmaintainer' not in e1
x1 = (W/'x1.md').read_text()
for a_, b_ in [('Results under the policy (worktree `x1-android-reference`, unchanged 1.0.290.1433 runtime; evidence\n`native/work/x1/runs/`; strict counts are of the reached cases):',
                'Results under the policy in the X1 worktree, before E1 and X2 were merged (unchanged 1.0.290.1433 runtime;\nstrict counts are of the reached cases; the merged-tree results are in "Merged tree on the devices"):'),
               ('**mode 2 FAIL** (component 1484, 0.351310 vs 0.311020: open item 2)', '**mode 2 FAIL** (component 1484, 0.351310 vs 0.311020; passes since the exact traversal)'),
               ('not reached: compiler memory (open item 3)', 'not reached: compiler memory (compiles since the E1 change; passes in the merged tree)'),
               ('mode 5 not reached (open item 3)', 'mode 5 not reached (passes in the merged tree)'),
               ('tri2 FAIL (open item 2); HP tri0-3 PASS, tri4 compiler memory', 'tri2 FAIL (passes in the merged tree); HP tri0-3 PASS, tri4 compiler memory (passes in the merged tree)'),
               ("(with E1's compile\nfix, not in this tree)", "(with E1's compile\nfix)"),
               ('`native/work/x1/tools/run_corpus.py\n--device-scene --reference <export>`.', '`native/work/x1/tools/run_corpus.py\n--device-scene --reference <export>` (copied for the merge as `native/work/merge1/tools/run_corpus.py`).')]:
    assert x1.count(a_) == 1, a_[:60]; x1 = x1.replace(a_, b_)
x1 = x1.replace('records in `native/work/x1/libm/`', 'records in the X1 worktree\'s `native/work/x1/libm/`; the Linux record of the merge is `native/work/merge1/reference/libm-linux-x64-glibc.json`')
x2 = (W/'x2.md').read_text()
a_ = '''- Device confirmation is pending: a Samsung rerun of the mode-2 capture, then
  the HP and Pixel.'''
assert x2.count(a_) == 1
x2 = x2.replace(a_, '''- Confirmed on the devices in the Phase E merge: the Samsung and the Pixel replay
  the re-recorded Linux mode-2 capture within tolerance and pass mode 2 natively
  ("Merged tree on the devices").''')
e3 = (W/'e3.md').read_text().rstrip('\n') + '''

Results (Linux; the merged-tree numbers): `shadow_terminator_gate.py` 10/10 cases against the original
CPU kernels, 0 practical, max 1.53e-5, with 10 native command replays; the offsets change the CPU image
(sun geometry 69 components, sun shading 390, point geometry 21, point shading 384), and zero offsets
leave every deterministic image film unchanged.

**Intel Mesa recompile crash (Linux).** Creating `shade_surface_light` again when `LIGHT_AREA` or
`MOTION_OBJECT` appears later in one device lifetime intermittently crashes the Intel Mesa 25.0.7
backend (`brw_opt_combine_constants` reading a garbage register number; not a WGSL error). Area lamp
sampling now runs its arcsine series as loops (same operations and order; films unchanged), which
took the `LIGHT_AREA` recompile from 3 of 4 cold crashes to 0 of 7; a later recompile after an area
light still crashed 2 of 4, so the GPU ABI test keeps its area light on the CPU (open item 4).
'''
mw1 = (W/'mw1-section.md').read_text()
assert '@' not in mw1, 'unfilled placeholder'
a = s.index('## Open device items (2026-10-20)')
s = s[:a] + e1.rstrip('\n') + '\n\n' + (W/'e2.md').read_text().rstrip('\n') + '\n\n' + x1.rstrip('\n') + '\n\n' + x2.rstrip('\n') + '\n\n' + e3 + '\n' + mw1.rstrip('\n') + '\n\n' + (W/'open-items.md').read_text()
out = M/'docs-work/DEVICE.md'; out.write_text(s); print('wrote', out, len(s.splitlines()), 'lines')
