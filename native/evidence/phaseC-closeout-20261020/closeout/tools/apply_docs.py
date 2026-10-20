# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Phase C close-out Step 4: descriptive doc edits (C2 ARM64 + C2 x64 proposals, C3 text already merged,
# the Adreno 640 table decision, device confirmation of the device-tier mechanism, open device items).
from pathlib import Path
R = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles'); S = Path('/tmp/claude-1000/-home-jeremy/c2281c28-97ca-4e0e-87ce-8b9b6c8aba81/scratchpad')
def sub(path, old, new):
    p = R/path; s = p.read_text(); assert s.count(old) == 1, (path, old[:70]); p.write_text(s.replace(old, new))

# ---- native/DEVICE.md
sub('native/DEVICE.md', 'cache. Android has not run this combination yet.\n',
    'cache. ARM64 and Android x64 device results are in the next two sections.\n')
sub('native/DEVICE.md', '## Device tiers: persisted record, known adapters and init canary\n',
    (S/'device_c2.md').read_text() + '## Device tiers: persisted record, known adapters and init canary\n')
sub('native/DEVICE.md', '| Pixel 4 XL Adreno 640 | `Adreno (TM) 640` | any | conservative |\n',
    '| Pixel 4 XL Adreno 640 | `Adreno (TM) 640` | any | full, 20 s compile budget |\n')
sub('native/DEVICE.md', '''full with a 20 s compile budget instead of 60 s (warnings only). An explicit
`CODEBRIX_CYCLES_PIPELINE_BUDGET_MS` replaces either budget.''',
'''full with a 20 s compile budget instead of 60 s (warnings only). A table entry
may carry its own budget: the Pixel 4 XL entry is full with 20 s, because full
tier meets the practical criterion there and the conservative tier was not
faster (see below). No table entry selects the conservative tier; it remains a
reactive fallback after a crash breadcrumb or a creation failure, or an explicit
opt-in. An explicit `CODEBRIX_CYCLES_PIPELINE_BUDGET_MS` replaces any budget.''')
sub('native/DEVICE.md', '''that keeps the full tier. Every film meets the practical criterion against the
original CPU session. Android devices have not run this mechanism yet.''',
'''that keeps the full tier. Every film meets the practical criterion against the
original CPU session.

**Device confirmation (2026-10-20).** Android builds of the merged tree (NDK
29.0.14206865, API 33, unchanged runtime) report these adapters and decisions,
with glass_render, transparent_lighting and lighting_mis passing the practical
criterion (outputs in `native/work/phaseC4/`):

| Device | Adapter device / driver description | Decision | Tier warnings |
| --- | --- | --- | --- |
| Samsung SM-X700 | `Adreno (TM) 730` / Adreno driver EV031.36.08.33 | full, `table` | none |
| HP Android x64 | `Intel(R) Xe Graphics (TGL GT2)` / `Mesa 24.0.8` | full, `table` | none |
| Pixel 4 XL | `Adreno (TM) 640` / Adreno driver EV031.31.04.01 | full, `table` (20 s budget) | compile-budget warnings only |

- On the Pixel 4 XL, the conservative tier (the former table entry) and the
  full tier (`CODEBRIX_CYCLES_PIPELINE_TIER=full`) give the same image errors and
  strict counts (glass_render film bit-identical); the conservative tier was not
  faster to compile (`shade_surface_light` up to 79 s against up to 70 s in
  transparent_lighting, 444 s against 410 s wall). The opt-in canary compiled in 2.0 s
  against its 10 s budget.
- The Samsung and HP glass_render films are bit-identical across runs with and
  without a device record. Specialized against unspecialized bit-identity cannot
  be checked on these devices, because the unspecialized surface module does not
  compile there (see "Open device items").
- A device record in a shell-writable folder under `/data/local/tmp` is created
  with the documented format, reused (`full`, reason `record`), and its
  `<record>.lock` serializes writers: with another process holding
  `flock(LOCK_EX)` for 60 s, a gate's record writes waited for it (about 56 s
  longer wall time) and the stage counts merged.
- On the HP, `CODEBRIX_CYCLES_TEST_CRASH_PIPELINE=prepare_surface` aborted the
  process; Android's signal chaining still let the handler append `signal=6` to
  the pending file. The next run consumed it as a breadcrumb and started
  conservative (reason `record`, workgroup size 1, practical criterion met, film
  bit-identical to full tier); after deleting the record the device returned to
  full (reason `table`).
- The Android x64 host gate passes on the HP with 38 exports.
''')
p = R/'native/DEVICE.md'; p.write_text(p.read_text().rstrip('\n') + '\n\n' + (S/'open_items.md').read_text())

# ---- PARITY.md
sub('PARITY.md', 'pass independent CPU images and replay on Linux. Static instanced triangle emitters remain rejected (1-3 ULP closest-hit barycentric divergence). Environment importance sampling/MIS, light trees, textured emission, linking and general visibility pending; Android revalidation of the integrated tree pending |',
    'pass independent CPU images and replay on Linux. Static instanced triangle emitters remain rejected (1-3 ULP closest-hit barycentric divergence). Environment importance sampling/MIS, light trees, textured emission, linking and general visibility pending. ARM64 Android (Samsung Adreno 730): point/sun/spot/area NEE+MIS, broad lighting and feature combination validated natively with replays; emissive triangles fail the ARM64 oracle (sampler and one image mode) and remain unvalidated on ARM64. Android x64 (HP Intel) passes spot/area, point/sun MIS, forward light and transparent shadows; emissive triangle lights fail on the HP compiler (memory) for moving emitters and the sampler probe; static triangle images 0-3 pass. Open device items are listed in `native/DEVICE.md` |')
sub('PARITY.md', 'The registered device separately implements single-scatter GGX reflection/refraction, glass and finite homogeneous volumes with independent CPU comparisons |',
    'The registered device separately implements single-scatter GGX reflection/refraction, glass and finite homogeneous volumes with independent CPU comparisons. Glass/GGX SVM, bounce and render pass natively on the Samsung SM-X700 (ARM64 normalization mode) and on Android x64 with SSE normalization (path 126 included). The Pixel 4 XL passes glass bounce/render but fails feature combination (microfacet lookup-table bounds error 512). Finite and closed homogeneous volumes and object, deforming and camera motion pass natively on the Samsung SM-X700 and the HP; the Pixel 4 XL fails the volume-boundary state test, the camera-inside closed volume and the object/deforming motion images |')
sub('PARITY.md', 'and reported through the host capability report; slow compiles only warn. Linux tier gates pass; Android devices have not run this mechanism yet |',
    'and reported through the host capability report; slow compiles only warn. Linux tier gates pass; on Android the Samsung SM-X700, HP Intel Xe and Pixel 4 XL (with a 20 s budget) select full tier by table, and the device record, its lock and a crash breadcrumb followed by a conservative start work on device. Scene specialization is required on Android: the unspecialized `shade_surface_light` crashes the Adreno 730 driver and exhausts compiler memory on the Adreno 640 and the HP (Scudo OOM at about 2.2-2.5 GB RSS). Cold compiles: Samsung specialized pipelines at most 32.6 s, except the emissive-triangle entry with object motion at 205 s; HP specialized prepare_surface 5-32 s, shade_surface_light 4 s (16 s with area lights, 25 s with triangle lights), connect_surface_light 3-11 s, unspecialized prepare_surface 156 s; Pixel up to about 70 s |')

# ---- native/README.md (no dates/history added)
sub('native/README.md', '''material/light/geometry coverage, Android revalidation of the integrated tree and
numerical discrepancies remain tracked there.''',
'''material/light/geometry coverage, the open Android device items (emissive
triangles on ARM64 and the HP, the unspecialized module on Android, and the
Pixel 4 XL motion, feature-combination and volume-boundary failures) and
numerical discrepancies remain tracked there.''')
print('docs applied')
