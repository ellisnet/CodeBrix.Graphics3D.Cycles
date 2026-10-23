## Merge wave 4: lanes L, S and P on the devices (2026-10-22)

Tree: 3392df3d plus lane L (Intel Mesa envelope: light-stage split with `shade_surface_light_triangle`, recorder v8
entry 17, superset groups, Gabor on bit 63, 68-triangle node render), lane S (SVM stack in a per-invocation arena slot,
noise fade ordering barrier, device-scene reference flow for `render_gate.cpp` images and the SVM probe gates) and
lane P (docs). Runtime unchanged (shipped `libcodebrix_wgpu.so` hash-verified before every corpus); full tier by table
on every device. Image, sampler, render and SVM-probe gates are judged against the glibc reference computed from the
device's own dumped scene; replays of image captures are judged against the same reference; state gates against the
device's own CPU. Evidence: `native/work/merge4/runs/<device>/`, tables in `native/work/merge4/device/device-tables.md`.

| Gate | Samsung SM-X700 (reference) | HP (reference, Mesa 24.0.8) |
| --- | --- | --- |
| triangle sampler | sample 3133 (open item 1) | pass |
| triangle images 0-5 + 6 replays (v8 captures) | pass | pass |
| Linux triangle (6, v8) and terminator (10) captures | pass | pass |
| emitter probe | pass | pass |
| broad lighting, lighting_mis + 4 replays | pass | pass |
| regression (glass, lighting_mis, transparent lighting) | pass | pass |
| transparent shadow + lighting + 10 replays | pass | pass |
| shadow terminator + 10 replays | pass | pass |
| specialization refusals (`=0` and `superset`) | refused (exit 1); glass spec on passes | refused (exit 1); glass spec on passes |
| object-moving/stationary images, own oracle and x64 oracle, 8 + 8 replays | pass | not run |
| principled_svm / principled_bounce | pass / pass | pass / pass |
| principled_render | stage 40 `mis_ray_pdf` (open item 2) | pass, replay pass |
| nodes_svm (67 graphs incl. Gabor) | compiled graph 44 mis-evaluates (open item 3) | pass |
| nodes_world (24 worlds incl. Gabor) | pass | pass |
| nodes_render (68 triangles) | component 6720 (open item 4) | pass, replay pass |
| Android host gate (48 exports) | pass | pass |
| managed app, 3 scenes | pass | pass |

**No HP exception is needed.** Every HP job passed, so no Mesa 24.0.8 or Bliss OS determination was required. The
specialization diagnostics (`=0` and `superset`) are refused on Android by design (the device constructor throws
before any compile), which the refusal jobs verified.

**Samsung node SVM probe: Gabor in the module breaks graph 44 (new).** Lane S's run (without Gabor) passed the
compiled graphs and 512 typed cases. With lane L's Gabor graphs in the fixture, the scene-specialized probe module
contains the Gabor handler, and graph 44 (Fresnel and two Layer Weight nodes) gives a closure weight of 0.070653
against 0.079591 (glibc; the device CPU agrees with glibc) on 27 paths of all three objects. The captured dispatch
(`native/work/merge4/diag/svm309`) replayed with `arena_replay`: on Linux the merged module matches glibc exactly; on
the Samsung the merged module, a variant with lane S's unrolled Horner steps, and a variant with the Horner arrays
replaced by a switch lookup all fail identically; a variant with the Gabor handler body emptied makes graph 44 equal
to Linux and glibc exactly. So the Adreno 730 mis-evaluates Fresnel/Layer Weight when the Gabor handler is in the
same module; the Horner loop arrays are not involved. Not fixed in the merge.

**Pixel 4 XL (Adreno 640).** Only the provisional materials jobs ran (see the lane P section): all five are not
supported. **Samsung Galaxy S20 FE 5G (Adreno 650).** Lane Q's failing jobs were rerun on the merged tree (timeout
max(600 s, 3 x this merge's Samsung wall)); the results are added to the lane Q section at the end.

