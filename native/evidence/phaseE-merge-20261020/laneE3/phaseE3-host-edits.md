# Phase E3: host edits after a render, device-error recovery, shadow terminator, spot/area compile crash

Worktree: `/home/jeremy/ClaudeHome/cycles-worktrees/e3-host-edits` (detached at 01eb27b6 + Phase D2-D5 seed).
Linux x64 only, Intel UHD (ADL-S GT1), Mesa 25.0.7, runtime 1.0.290.1433 unchanged. No commits, no Android devices.
Work output: `native/work/e3/` (budgets in `native/work/e3/budgets.txt`, final validation in `native/work/e3/final/`).
Sealed handoff: `native/work/e3/handoff/` (changes.patch, changes.json, source/, validation/, handoff.json).

## Timeline

| Time | Step |
| --- | --- |
| [08:14:54] | Start; read MAINTAINER-README, ABI.md, host sources, D3/D4/D5 reports. |
| [08:17:02-08:26] | Cold full native build (8 min 39 s, `--skip-dependencies`). Wrote `scene_edit_consumer.c` (13 edit scenarios + recovery). |
| [08:26] | Baseline probe (CPU): object `tfm` edit ignored (1,204 practical failures vs a fresh render), object motion added wrong (834), deformation motion refused (`CBC_NOT_READY`); camera motion, material and light edits already correct. |
| [08:26-08:28] | `host_edits.inc` (authored-attribute capture and restore). CPU: all 13 scenarios bit-identical to a fresh host. |
| [08:28-08:31] | WebGPU probe: 12/13 practical; smooth-toggled failed "invalid data (mask 1)" (shadow terminator). |
| [08:31-08:40] | Shadow terminator WGSL + `shadow_terminator_gate` variant: 10/10 cases practical vs original CPU kernels. Device recovery: WebGPU probe 13/13 + both recoveries practical. |
| [08:41-08:43] | Spot/area crash reproduced (abi_test GPU, cold): driver SIGSEGV creating `shade_surface_light`; symbolized: `deallocate_slots` <- `parcel_out_registers` <- `brw_opt_combine_constants` (reg_nr=13056). |
| [08:46-09:01] | Baseline gate binaries (later found mtime-flawed and redone), lamp disk dedupe (no help: 2/3 crash) and area arcsine loops (0/3 crash). |
| [09:08-09:27] | Image-gate bit-identity (first pass), managed API/tests, dev runs of both new gates (PASS), licensing/provenance and docs. |
| [09:23-09:38] | Clean native build (4 min 31 s); host gate twice FAILED in abi-gpu: the same driver crash at the object-motion recompile. |
| [09:41-10:16] | Proper baseline host library (sources swapped with touched mtimes). Repro statistics (below) showed the lamp disk dedupe raised the motion-recompile crash rate; reverted it, kept the arcsine loops; GPU ABI test keeps its area light on the CPU. |
| [10:17-10:35] | Final cold validation driver (`native/work/e3/final/validate.sh`). |
| [10:36-10:41] | Managed test fix (default Cycles surface shader is unsupported on WebGPU; test now uses a diffuse material), dotnet rerun, transparent-shadow gate rerun, baseline image gates. |
| [10:45-11:06] | Audits (reviewed-exception hashes updated), all-lanes PASS (1,371 s), handoff sealed. |

## 1. Edits after a render

**Root cause.** `ObjectManager::apply_static_transforms` (static BVH, the host's only BVH type) rewrites a single-user mesh/hair/point cloud's position, normal and radius attributes in place with its object transform and sets `Geometry::transform_applied`. Cycles has no inverse; its interactive hosts re-export geometry. So an object `tfm` edit kept the old baked positions, motion added to a baked object/mesh could not instance it, and `cbc_host_motion_positions` refused.

**Design (`native/host/host_edits.inc`, wired from `cycles_host.cpp`, `host_properties.inc`, `host_scene.inc`, `host_api.inc`).**
- `reconcile_bakes` runs in `cbc_host_render` before the graphs and the device update. For every baked geometry it computes what a fresh scene would do (`expected_bake`, a mirror of the apply conditions: one user, no surface BSSRDF or true displacement, no subdivision, uniform scale for hair/points, not `motion_blur && object.use_motion()`, `apply_to_motion` from `need_motion`). If the fresh bake differs from the recorded one (other object, different `tfm` bits, `apply_to_motion`, a used shader whose dirty graph may add displacement/subsurface) it restores the authored attributes (`AttributeSet::update` from a deep copy), clears `transform_applied`, `transform_negative_scaled`, `transform_normal` and tags a rebuild. Then it captures a deep copy of the authored attributes of every geometry the coming render may bake. `prune_bakes` drops captures Cycles did not bake.
- `before_edit` runs before every host edit of a node (`set_floats/ints/uints/string/strings/nodes`, `cbc_host_mesh`, attribute set/remove, `cbc_host_motion_positions`): a baked geometry gets its authored attributes back first, so the edit applies to authored data. Cycles' own `Scene::device_update` then re-bakes or instances exactly as for a new scene (BVH rebuild; refit is never used because the static BVH path always rebuilds after geometry changes).
- `cbc_host_attribute_get_*` answer from the authored copy for baked geometry. Deleting a geometry drops its capture; host destroy/exit clear all captures before the session.
- Cost: one extra attribute copy of each single-user mesh/hair/point cloud while baked; an edit of such a geometry rebuilds the BVH.

**Applied now** (CPU bit-identical to a fresh host, WebGPU practical): object `tfm`; object motion added/removed; camera motion added; deformation motion added after a render (was `CBC_NOT_READY`); motion blur switched on; material value; light strength and holder `tfm`; `cbc_host_mesh` replacement; a new instance of a rendered mesh; per-vertex attribute replacement; `smooth` flags. Camera motion and material/light edits were already applied before E3 (D4's "camera motion silently ignored" did not reproduce through the C ABI; the managed `CameraNode_Motion_added_after_a_render...` test passes).

**Refused explicitly** (`CBC_NOT_READY`, message names the rule): edits of a baked `Volume` geometry (voxel `ImageHandle` attributes cannot be copied), of its object's `tfm`/`motion`/`geometry`, giving it another user, and (render-time backstop) a fresh-bake difference on a baked volume. Not exercised by a gate (volumes need OpenVDB-less voxel data); reported as untested.

**Gate `native/tools/scene_edit_gate.py`** (`codebrix_host_scene_edit_test`, 48x36, 16 spp): per scenario base render, edit, edited render; "replay" = the same sequence in a new host; oracle = fresh CPU host built in the same node order with the edited values. Final cold run: CPU 13/13 edited, replay and before films bit-identical; WebGPU 39 films (edited/replay/before) 0 practical failures, max 5.72e-6, 14,499 strict differences in total (about 370 per film, atomic accumulation at 16 spp, the same envelope as the before films). Command-stream replay is not available for host-level renders; the shadow-terminator gate carries native replays.

**Managed (typed API).** `CyclesScene` remarks document the edit semantics; `MeshNode.SetMotionPositions` docs (generator EXTRAS, regenerated, `--check` clean) no longer require "before the first render"; `SetMeshMotion` maps `CBC_NOT_READY` to `InvalidOperationException` with the native message. Tests: `SetMotionPositions_after_the_first_render_matches_a_scene_built_with_motion`, `ObjectNode_Tfm_edited_after_a_render_matches_a_scene_built_with_it`, `ObjectNode_Motion_added_after_...`, `CameraNode_Motion_added_after_...` (CPU, bit-identical), `gpu_object_moved_after_a_render_matches_the_cpu_scene_built_with_it` (WebGPU, practical).

## 2. Device-error recovery contract

Preferred option implemented. `cbc_host_render` calls `recover_device` (cycles_host.cpp) instead of failing on a device that carries an error:
- WebGPU (`WebGPUDevice::recover`, device.cpp): refuses when the WebGPU device has a fatal error (`gpu.check()` throws: lost device, uncaptured validation error); otherwise drops the pending command encoder, zeroes the kernel error word, clears `Device::error_msg`.
- The host then tags the whole scene for a complete device update (what `Scene::reset` tags, without adding default shaders; every shader/geometry/object modified, geometry rebuild), emits `CBC_EVENT_STATUS` "Device recovered after an earlier error (...); scene data re-uploaded", and renders. Device record and tier logic untouched.
- No recovery path (fatal WebGPU error, CPU or Metal device): every later render returns `CBC_FAILED` "Host faulted: the device cannot recover from an earlier error (...); destroy the host and create a new one".
- Managed: `IsFaulted` set on `CBC_FAILED`, cleared by a successful render; docs give the guidance.
- Tests: scene_edit_gate recovery after a rejected depth pass ("surface data passes not implemented", mid-render) and an orthographic camera (before dispatch): both later renders on the same host 0 practical vs fresh CPU (max 3.81e-6), 2 error events, 2 "recovered" status events. abi_consumer GPU: render after the rejection now `CBC_OK` (host gate expects 0, was 3). Managed `gpu_scene_recovers_after_a_rejected_render_once_the_cause_is_removed`.

## 3. Shadow terminator offsets

`native/shaders/wavefront_terminator.wgsl` (new; Apache-2.0 AND BSD-3-Clause AND MIT):
- Geometry offset: `shadow_ray_offset` + `shadow_ray_smooth_surface_offset` (local vertices and raw normals incl. corner normals and deforming frames, parabolic envelope, `object_dir_transform` for non-applied/motion objects); `surface_direct_light` (wavefront_lighting.wgsl) uses the lifted origin for the light direction, applies `integrate_surface_ray_offset` (`surf_ray_offset_at`) only when skip-self holds, and writes NONE self object/prim otherwise, exactly as `shadow_ray_setup`.
- Shading offset: `shift_cos_in` with `fast_acosf` (binary32 adapters) and the existing `native_disk_sincos` cosine; applied after the bump term in BSDF eval (`cos >= 0`) and in sampling (reflection labels) via `surf_terminator` (wavefront_surface.wgsl).
- Specialization: two new feature bits appended, `SF_TERMINATOR_GEOMETRY=1<<30` (direct light + a smooth triangle + an object geometry offset > 0) and `SF_TERMINATOR_SHADING=1<<31` (any object multiplier > 1), `SF_ALL=(1<<32)-1`; fallbacks report error 2. The device keeps a smooth-triangle flag from the `tri_shader` upload.
- Rejected explicitly (surface.cpp, before dispatch): shading multiplier > 2 (offset > 1; the cosine argument would leave `native_disk_sincos`' [-pi, pi]) with "WebGPU shadow terminator shading offset outside [0, 1] not implemented"; nonfinite geometry offset. The old "mask 1" fail lines in `prepare_surface` are gone. `surface_gate` test 12 now expects that rejection (offset 3).
- Gate `native/tools/shadow_terminator_gate.py` (`codebrix_cycles_shadow_terminator_gate`, lighting_gate.cpp variant + `shadow_terminator_fixture.h`): coarse smooth UV sphere on a floor, sun (0-3) / point (4-7) with geometry 0.1/0 x shading 0.5/0, two non-uniformly scaled instances with geometry 0.3 + shading 1 (8), moving + deforming sphere (9). Final cold run: 10/10 practical, max 1.76e-5, 358 strict of 61,440, 10 native command replays PASS; offsets change the CPU image (sun geometry-only 69 components, sun shading-only 390, point geometry-only 21, point shading-only 384).
- Zero offsets bit-identical: image gates (lighting, lighting_mis, broad_lighting, transparent_lighting, feature_combination, triangle_lighting, object/deforming motion, glass_render) against a properly rebuilt baseline: 175 of 197 films identical (every CPU film and every deterministic GPU film); the 22 differing GPU films are all in the 12 cases whose GPU film is not bitwise repeatable within one run (gate `repeatBitwiseDifferences` > 0, atomic accumulation). `glass_render` a8e69a34... with specialization on and off. transparent_shadow_gate.py (9 gates incl. shadow, transparent_shadow, broad_lighting, lighting_mis) PASS.
- D5 workarounds reverted: `WebGpuValidationScenes.cs` and `GltfSceneConverter.cs` keep default offsets; managed WebGPU tests pass with geometry 0.1 (the DiffuseSun smooth sphere).
- Licensing: shift_cos_in is Appleseed MIT code inside Cycles bsdf.h; fast_acosf is BSD-3-Clause (math_fast.h). Updated `native/THIRD-PARTY-NOTICES.txt` (removed "shift_cos_in is not ported", added the entry), its hash in `third_party/dependencies.json`, regenerated `THIRD-PARTY-NOTICES.txt`/`NOTICE`, added `nativeShadowTerminatorDerivations` to `third_party/provenance.json`, and refreshed the three reviewed-exception hashes in `third_party/license-review-exceptions.json` (content they already carried is unchanged). No new dependency; MIT/BSD are already reviewed licenses. Jeremy should confirm these manifest edits as the license review.

## 4. Spot-after-point compile crash

Diagnosis: the known Intel Mesa 25.0.7 `brw_opt_combine_constants` bug (`deallocate_slots` reads `regs[reg_nr]` with a garbage register number: 13056, 63819), intermittent and heap-layout dependent; not a WGSL error (the same modules compile on other runs). Spot after point adds no specialization bit and never recompiles; the crashing compiles are the `shade_surface_light` recompiles when `LIGHT_AREA` or `MOTION_OBJECT` appear in one device lifetime.

Cold repro statistics (fresh MESA cache each run):

| Variant | Baseline | Lamp disk dedupe | Final (arcsine loops, no dedupe) |
| --- | --- | --- | --- |
| point -> +area (`LIGHT_AREA`) | 3/4 crash (+2/3 in abi runs) | 2/3 | 0/7 |
| point -> +object motion | 0/4 | 2/4 | 0/4 |
| point -> area -> +object motion | 1/1 (area survived once) | n/a | 2/4 |

Mitigation kept (confined to area lamp sampling, `wavefront_emitters.wgsl`): `emitter_asin`'s 16 Horner steps and `area_rectangle`'s four arcsines are loops (same operations, same order; films unchanged). The lamp disk dedupe was reverted (it raised the motion-recompile crash rate). The GPU ABI test renders point, spot and sun again (was point only) and keeps its area light on the CPU; area lights stay covered by broad/transparent lighting gates. Residual risk: later recompiles after an area light in the same device lifetime (2/4). A driver fix, or a smaller/separate lamp stage, remains open.

## Validation (final, cold Mesa caches, budgets in budgets.txt)

| Check | Result |
| --- | --- |
| Clean native build (`tools/build.py --skip-dependencies`) | PASS 4 min 31 s; incremental after the last WGSL change 173 s |
| Host gate | PASS 192 s: 48 exports, 3,142 comparisons, max 1.19e-7, 0 practical, 0 strict |
| dotnet build Release | 0 warnings, 0 errors |
| dotnet test | 162/162 pass (first run 161/162: the new moved-object test used the default surface shader, fixed) |
| dotnet pack | PASS |
| scene_edit_gate | PASS (above) |
| shadow_terminator_gate | PASS (above) |
| transparent_shadow_gate.py | PASS (rerun; the first run passed every gate but failed its final source-pin assertion because I edited a test file during it) |
| Image gates vs baseline | 175/197 bit-identical, rest atomic-nondeterministic cases only |
| glass_render spec on / off | a8e69a34... / a8e69a34... |
| surface_gate | PASS (test 12 rejection message) |
| all-lanes (29 targets, phaseC closeout linux_validate.py copy, own build, binary audit) | PASS 1,371 s of 2,700 s (an earlier `--skip-build` attempt failed in 10 s: gate binaries absent after the clean build) |
| audit.py | PASS (after the reviewed-exception hash refresh) |
| notices.py --check, guard tests, compatibility tests, generator --check, git diff --check | PASS |

Docs updated after some runs (DEVICE.md trailing newline, license-review-exceptions hashes); all-lanes and the audits ran on the final tree.

## What remains

- Android confirmation of the shadow terminator, recovery and edit semantics (list below).
- Volume edits after a render are refused, not restored; untested by a gate.
- Intel Mesa recompile crash after an area light (2/4 cold) stays open; the GPU ABI test avoids that sequence.
- Host-level renders have no command-stream replay; scene_edit_gate "replay" re-runs the sequence in a new host.
- Shading offsets > 1 rejected on WebGPU (CPU renders them).

## Device confirmation list (merge)

- Samsung SM-X700, HP x64, Pixel 4 XL: Android app `--mode webgpu` (the three validation scenes now with default shadow-terminator offset 0.1; DiffuseSun's smooth sphere exercises the geometry offset), plus `codebrix_cycles_shadow_terminator_gate` replays of the 10 Linux captures (`native/work/e3/final/shadow-terminator-gate/shadow_terminator/run/case-*`).
- Recovery on device: the AndroidHost WebGPU checks should add a rejected-pass-then-render step (not added here; no device lane).
- broad_lighting / transparent_lighting replays on each device (emitter arcsine loop changed the area WGSL text).

## Merge notes

Files E1 also touches: `native/shaders/wavefront_lighting.wgsl` (E1 restructures `light_sample_native`'s selection; E3 only changes `surface_direct_light`), `native/shaders/wavefront_surface.wgsl` (E1: `shade_surface_light`; E3: `surf_evaluate`/`surf_sample`/`surf_ray_offset`, `prepare_surface` fail lines removed), `native/cycles/device.cpp` (E1: Android spec=0 refusal in the constructor; E3: tri_shader scan, `recover`, seam), `native/cycles/lighting_gate.cpp` (E3 adds a `CODEBRIX_SHADOW_TERMINATOR_GATE` variant in separate ifdef blocks), `native/tools/inject_cycles.cmake`, `native/cycles/pipelines.{h,cpp}` (E3 appends bits 30-31: if E1 also appends, renumber the later lane and keep `SF_ALL` contiguous), `native/THIRD-PARTY-NOTICES.txt` + its hash in `dependencies.json` + regenerated root notices + `license-review-exceptions.json` hashes (regenerate after merging all lanes). `native/tools/build_cycles.py` adds `wavefront_terminator.wgsl` to the surface module.
