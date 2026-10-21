## Session close: the E4/E5/M4 merge on the devices (2026-10-20)

Merged tree: the materials merge plus Phase E4 (ShaderData arithmetic and the reference oracle), Phase E5
(sliced sampler dispatch) and the bump/derivative opcodes. Runtime unchanged; the shipped
`libcodebrix_wgpu.so` was hash-verified before every corpus. Every run was on the full tier, decided by the
table, with no tier change; adapter strings match the table rows. Image and sampler gates are judged against
the glibc reference computed from the device's own scene, and on-device replays of image captures are judged
against the same reference as their source gate (the bionic CPU film is a diagnostic). The SVM, world and
state gates compare against the device's own CPU.

| Gate | HP (Intel Xe, Mesa 24.0.8) | Samsung SM-X700 (Adreno 730) | Pixel 4 XL (Adreno 640) |
| --- | --- | --- | --- |
| triangle sampler | pass (0 practical, 865 strict) | 2 values in sample 3133 (open item 1) | pass (0 practical, 865 strict) |
| triangle images, modes 0-5, and 6 on-device replays | pass | pass | pass |
| Linux triangle (6) and terminator (10) captures | pass | pass | not run |
| emitter probe | pass | pass | pass |
| broad lighting, lighting_mis, transparent, terminator, regression | pass | not rerun | feature_combination pass |
| object-moving images | not rerun | not rerun | SVM error 8 (open item 2) |
| specialization off | refused (exit 1) | refused (not rerun) | refused (not rerun) |
| nodes_svm | pass (65 graphs) | the typed 2D noise program of item 9 | device lost creating the pipeline |
| nodes_world | pass (23 worlds) | pass (23 worlds) | SVM error 8 |
| nodes_render | pass, 0 practical vs glibc; replay pass | image component 6720 (item 9) | SVM error 8 and 16 |
| managed app, 3 scenes | 0 practical | 0 practical | 0 practical (compile warnings) |

The bump and derivative opcodes leave the Adreno 730 results unchanged (same noise program, same render
pixel) and the Adreno 640 still fails every node gate.

**Pixel 4 XL object motion under the reference oracle's arithmetic.** The object-moving motion images fail on
the Pixel with SVM error 8 when the gate runs with an imported reference. That is the only case in which
`lighting_gate.cpp` selects the x64 oracle's arithmetic adapters on an Android scalar build (Phase E4). Diagnostic
runs of the same image gate, with the same staged libraries:

| Binary | Default (device oracle) | `CODEBRIX_CYCLES_REFERENCE_ORACLE=linux-x64-sse4.2` |
| --- | --- | --- |
| merged tree | pass | SVM error 8 |
| Phase E4 sources without the materials work | pass | SVM error 8 |

So the failure follows the x64 adapter constants on the Adreno 640 (the materials code is not involved). The
product and the gates with local checks keep the device's own oracle and pass. Phase E4 validated the adapters
on the Pixel for the triangle images only.

**Specialization-off status.** The bump and derivative handlers make the unspecialized modules larger than
Intel Mesa 25.0.7 handles reliably, so the render gates are validated with specialization on. The Linux
specialization-off runs were made once each, with a declared budget:

| Render | Result | Unspecialized `prepare_surface` cold compile |
| --- | --- | --- |
| glass_render | pass; film bit-identical to specialization on | 649 s |
| principled_render | SVM error 8 during the render (wrong evaluation) | 607 s |
| nodes_render | pass; films bit-identical to specialization on | 617 s |

`nodes_gate.py` therefore runs only `nodes_world` unspecialized by default (`--unspecialized` selects
others). The compile smoke step measured the unspecialized node-render surface module:
- `prepare_surface`: 3,447,736 inlined bytes (2.3 MB is the Intel Android danger zone), 604 s cold, 10.0 GB peak;
- `shade_surface_light`: 1,578,630 bytes, 24 s.

Both are warnings only. Restoring a reliable specialization-off check is open item 11.

