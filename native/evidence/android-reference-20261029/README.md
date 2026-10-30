# Android reference devices at HEAD, on the Linux x64 machine (2026-10-29)

Copyright (c) 2026 Jeremy Ellis and contributors

The Android ARM64 and x64 validation of commit `686f79282` with the CodeBrix.WebGpu.Native.MitLicenseForever
1.0.300.210 runtime, run from the Linux x64 machine (from 2026-10-29 the only machine for Linux x64 and Android
testing and validation). Every Android result is judged under the validation reference policy
(native/DEVICE.md): the device GPU against the Linux x64 glibc reference computed from the device's own scene, with
the device's bionic CPU as a diagnostic. The Linux side is `native/evidence/linux-x64-reference-20261029/`. Records
only; no binaries. A later run writes a new evidence directory; never edit this one.

## Devices and identity

| Device | RID | GPU / driver | Role |
|---|---|---|---|
| Samsung SM-X700 tablet, API 36 | android-arm64 | Adreno 730, EV031.36.08.33 | reference |
| HP Laptop 15-dy2xxx (87FE), Bliss OS, API 33 | android-x64 | Intel Xe (TGL GT2), Mesa 24.0.8 | reference |
| Samsung Galaxy S20 FE 5G, API 33 | android-arm64 | Adreno 650, EV031.32.02.10 | not a reference (fix or degrade gracefully) |

Staged binaries (`staging/staged-binaries-android-{arm64,x64}.json`, 79 files each, SHA-256 and ELF review):
`lib/libcodebrix_wgpu.so` is the 1.0.300.210 package file (arm64 `52dc9667...`, x64 `005633ea...`). Per-device staging:
`staging/staged-{samsung,hp,s20}.json`. .NET SDK 10.0.403. Managed validation APK SHA-256 `224e145e...`.

## Contents

| Path | What |
|---|---|
| `samsung-triangle-sample/` | The tablet's `triangle_sample` device-scene run, for the Raspberry Pi's step P4 (docs/TESTING-LINUX-ARM64.md, section 5): `scenes/triangle_sample/{samples.scene,samples.local.f32}` (the tablet's dumped scene and its bionic CPU reference), `device-scene-reference/` (the Linux x64 glibc reference of that scene and its provenance), `output/triangle_sample/{run.actual.bin,run.reference.bin,run.reference-diagnostic.jsonl}` (the tablet GPU samples and the run's reference files); `SHA256SUMS`; the run's `results.json`, `status.json`, reference policy. Verdict: PASS, glibc practical 0, strict 865, maximum 4.88e-04 |
| `results/runs/<device>/<job>/` | Every device job's `results.json`, `status.json`, reference-policy and command records (binary films and arena dumps are not committed) |
| `results/runs-table-{samsung,hp,s20}.txt`, `counts.txt` | Per-job verdict tables; counts against the 2026-10-27 run |
| `app-gate/` | The managed validation app gate records (WebGPU mode and bundle mode control, bench, HDRI; tablet and HP), judged against the Linux films of `native/evidence/linux-x64-reference-20261029/` |
| `results/run-report.md` | The run report: every verdict with strict counts and maxima beside 2026-10-27, failures verbatim, incidents |

## Results

| Device | Jobs | Replays | 2026-10-27 |
|---|---|---|---|
| Tablet | 28 pass, 2 fail | 80 pass, 6 fail | 28/2, 80/6 |
| HP | 30 pass, 0 fail | 85 pass, 1 fail | 30/0, 69/1 (the 16 host-capture replays ran on the HP this time; all pass) |
| S20 | 16 pass, 8 fail | 16 pass, 29 fail | 16/8, 16/29 |

No new failure; every failure is a known one (native/DEVICE.md "Open device items"):
- tablet and HP: the light-tree case-2 replay ("Native scheduler decision readback differs at command 51"; items 17
  and 24; the light-tree gate itself passes);
- tablet: `mnee` and `mnee_render`, "WebGPU scheduler kernel reported invalid data (mask 1)" (item 19);
- S20: `principled_render`, `principled_bounce`, object and deforming moving images ("mask 8: invalid/uninitialized
  SVM stack"), `light_tree` (179 practical), `mnee`/`mnee_render` (mask 1), the Adreno 650 AO refusal, environment
  refused at case 0.

Android host gate: tablet and HP pass (1,579 films each, 0 practical, 0 required strict; references
`linux-x64-reference-20261029/host-gate`). Managed app gate: all 8 runs pass (0 practical, tier full).

Smooth-shaded BSDF surfaces on Adreno/Vulkan (the Windows ARM64 F1 question, docs/TESTING-WINDOWS-ARM64.md):
glass_render, lighting_mis, broad_lighting, principled_render and nodes_render pass within the practical criterion on
the tablet (Adreno 730) and the HP; on the S20 (Adreno 650) glass_render and lighting_mis pass and principled_render
fails with the SVM-stack error. `microfacet_render` and the tablet/HP motion images are not in the device job set
(not run).
