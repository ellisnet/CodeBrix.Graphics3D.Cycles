#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 4: DEVICE.md doc edits decided by Jeremy (b)-(e), the merge-wave-4 device section, and lane Q's
S20 section hand-applied at the end."""
from pathlib import Path
R = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles'); p = R/'native/DEVICE.md'; t = p.read_text()
def rep(old, new):
    global t
    assert t.count(old) == 1, old[:80]; t = t.replace(old, new)
# (c) tier-table row
rep("| Pixel 4 XL Adreno 640 | `Adreno (TM) 640` | any | full, 20 s compile budget |",
    "| Pixel 4 XL Adreno 640 | `Adreno (TM) 640` | any | full tier, 20 s compile budget, not a reference device, verdicts per the device reference policy |")
# lane P intro: the SVM stack moved
rep("The merged Pixel-specific fixes stay in place: the module-scope `svm_stack`, the volume-stack scalar arrays and the\nsliced sampler dispatch.",
    "The merged Pixel-specific fixes stay in place: the SVM stack outside `SvmState` (since lane S in a per-invocation\narena slot), the volume-stack scalar arrays and the sliced sampler dispatch.")
# (b) 5-second floor
rep("The per-job hard timeout is the cutoff, 3 x the Samsung wall (minimum 5 s for adb launch latency).",
    "The per-job hard timeout is the cutoff, 3 x the Samsung wall. The 3x rule carries a 5-second floor on cutoffs\n(adb launch latency): cutoff = max(5 s, 3 x the Samsung wall).")
# (d) provisional materials settled, dispositions decided
old_start = "- Provisional, because the Samsung also fails today (items 8 and 9, lane S): principled_svm (SVM error 8),"
i = t.index(old_start); j = t.index("Rerun these at merge, after lane S.", i)+len("Rerun these at merge, after lane S.")
t = t[:i]+"""- Materials, settled at merge wave 4 (2026-10-22) after lane S, one run each with cutoff max(5 s, 3 x that merge's
  Samsung wall): principled_svm (cutoff 335 s), principled_bounce (915 s), principled_render (148 s) and nodes_svm
  (461 s) were stopped at their cutoffs; nodes_render failed with SVM error 24 (8 + 16) at 217 s (cutoff 386 s). The
  materials and node replays did not run (their source jobs did not pass). Principled BSDF materials are therefore not
  supported on the Adreno 640 (3x) and texture/colour/utility nodes in surface shaders are not supported (fails). The
  Samsung walls of principled_render, nodes_svm and nodes_render are times to their (open) Samsung failures. Evidence:
  `native/work/merge4/runs/pixel/materials-p-*` and `native/work/merge4/device/pixel-cutoffs.json`.

Dispositions DECIDED by Jeremy (2026-10-22): the following are definitively not supported on Pixel 4 level hardware
(Adreno 640) and will NOT be rerun: object and deformation motion blur, node-based world shaders, transparent shadows
and transparent lighting, emissive triangle lights, feature combination, shadow terminator, stationary renders of
motion-capable scenes, the managed validation app, and most on-device replays. A full functionality test on a Pixel 5
(Adreno 620) later is the one exception the policy allows."""+t[j:]
# merge-wave-4 section, inserted before the open items
merge = open(R/'native/work/merge4/docs/device-merge-section.md').read()
k = t.index("## Open device items (session close, 2026-10-20)")
t = t[:k]+merge+"\n"+t[k:]
# (e) consolidated open items
a = t.index("## Open device items (session close, 2026-10-20)"); b = t.index("## Pixel 4 XL triage under the device reference policy")
t = t[:a]+open(R/'native/work/merge4/docs/device-open-items.md').read()+"\n"+t[b:]
# lane Q section at the end
t = t.rstrip('\n')+"\n\n"+open(R/'native/work/merge4/merge-q-section.md').read().rstrip('\n')+"\n"+open(R/'native/work/merge4/docs/s20-rerun.md').read()
p.write_text(t); print('DEVICE.md updated', len(t))
