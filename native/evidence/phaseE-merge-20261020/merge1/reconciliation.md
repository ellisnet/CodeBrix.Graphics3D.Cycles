# Merge wave 1: shared-file reconciliation

Lanes E1 (triangle-light ARM + HP OOM), E2 (Pixel fixes), E3 (host edits, recovery, shadow terminator, Intel
recompile crash), X1 (Android reference policy) and X2 (exact traversal) were made against the same baseline
(`baseline-phaseE.json`, 39,205 files). Order: E2, E1, X2, X1, E3. Each lane's non-shared files were applied with
`git apply --exclude=<shared files>` (all five patches checked first, sha256 verified against handoff.json). The 12
shared files were merged three-way (`git merge-file`, base = the baseline copy in `base/`, each lane's sealed
`handoff/source/` copy in `lanes/<lane>/`, result in `merged/`), then reviewed by hand. Every one of the 53
single-owner files ended at its lane's recorded after-hash (`tools/expected.py check`, run before the wave-1
hardening edit below).

## Code and build files

| File | Lanes | Automatic result | Resolution |
| --- | --- | --- | --- |
| `native/cycles/device.cpp` | E1, E3 | clean | E1's Android `CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0` refusal in the constructor and E3's `tri_shader` smooth-triangle scan, `WebGPUDevice::recover` and the `webgpu_recover` host seam are in disjoint regions; both kept unchanged. |
| `native/cycles/emitter_sample_gate.cpp` | E1, X1, X2 | clean | E1's `triangle_light_prefill` call in the probe WGSL, X1's scene exchange / imported reference around the upload and output, X2's guard that now requires the renderer to accept static instanced emitters; all three kept. |
| `native/cycles/lighting_gate.cpp` | E3, X1, X2 | 1 conflict (E3 vs X2) | X2 deleted the mode-4 rejection block (mode 4 is rendered now, 6 cases); E3 changed the following `#else` into `#elif !defined(CODEBRIX_SHADOW_TERMINATOR_GATE)`. Resolution: X2's deletion plus E3's `#elif`; X1's `exchange_only` wrapper around the coverage checks is kept. |
| `native/shaders/wavefront_lighting.wgsl` | E1, E3 | clean | E1 factors `light_select` out of `light_sample_native` (shared with the prefill); E3 changes `surface_direct_light` (terminator-lifted origin, `surf_ray_offset_at`, self object/prim NONE when not skipping). Different functions; the light sample is taken from `sd.P` before the terminator lift in both lanes, as in Cycles. |
| `native/shaders/wavefront_surface.wgsl` | E1, E3 | clean | E1 restructures `shade_surface_light` (one prefill call site); E3 adds `surf_terminator`, splits `surf_evaluate`, adds `surf_ray_offset_at` and removes the two old terminator `fail()` lines in `prepare_surface`. Disjoint. |
| `native/tools/build_cycles.py` | E3, X2 | clean | X2 renames the AST `c_ray_triangle_intersect` to `_ast` (anchor-checked); E3 adds `wavefront_terminator.wgsl` to the surface module list. Disjoint lines. |
| `tests/.../WebGpuRenderTests.cs` | E3, X1 | clean | E3's moved-object and recovery tests and default terminator offsets; X1's `ExportReference` hook. |

## Documentation files

| File | Lanes | Resolution |
| --- | --- | --- |
| `AGENT-README.txt` | E3, X1 | 1 conflict in the pitfall list: E3 replaced "SMOOTH MESHES WITH LIGHTS ON WEBGPU" (offset now implemented) with "SHADOW TERMINATOR SHADING OFFSETS ABOVE 1 ON WEBGPU"; X1 added "JUDGING ANDROID GPU OUTPUT AGAINST AN ON-DEVICE CPU RENDER". Both new items kept, the obsolete smooth-mesh item dropped. |
| `MAINTAINER-README.txt` | E3, X1 | clean (different sections). |
| `native/host/ABI.md` | E3, X1 | clean: E3's edit/recovery contract sections and X1's Determinism libm bullet. |
| `native/DEVICE.md` | E1, E2, E3, X1, X2 | Rebuilt by `tools/assemble_device_md.py` (see below). |
| `docs/PARITY.md` | E1, E2, E3, X1, X2 | E3's copy as the base: host-ABI open-item rows, intro sentence. Material coverage row from E2. Memory and performance row from E1, with the Pixel maximum updated to the merged-tree 87 s. Full parity decision from X1. The Lighting row is rewritten from E1 + X1 + X2 and the merged-tree device results (one row, no duplicated claims). E3's "Android devices still need the confirmation runs" sentence updated: the confirmation runs were done in this wave. |

### native/DEVICE.md

The final file is regenerated from the baseline by `tools/assemble_device_md.py`, so the result is reproducible.
- Integrated-tree ARM64 section. The "Open on ARM64" bullets combine E2's Pixel bullet, X1's
  emitter/sampler bullet and X2's mode-2 outcome into one list of the run's open bullets, each with its
  Phase E outcome. E1's "not yet validated on ARM64" sentence is dropped as superseded.
- HP integrated-tree section. One closing sentence notes that the compile-memory failures were
  resolved (E1) and that spec-off is refused.
- E3's two in-place edits are applied: the next-event-estimation limits and the managed-scene
  terminator note.
- New sections, in order, before the open items:
  - E1 "Emissive-triangle compile load and the Android oracles' C library". Its "decision for the
    maintainer" sentence is replaced by a pointer to X1's policy, which is that decision.
  - E2 "Pixel 4 XL (Adreno 640) function-scope array fix", unchanged.
  - X1 "Validation reference policy". Its results table is labelled as the X1-worktree run before
    E1/X2, and its "open item 2/3" cells point to the merged outcome.
  - X2 "Exact traversal and intersection arithmetic". Its "device confirmation pending" bullet now
    gives the confirmed result.
  - E3 "Shadow terminator offsets". E3 had appended it after the open items, so it is moved before
    them, with the Linux results and the Intel recompile-crash paragraph.
  - The new "Merged tree on the devices (Phase E merge)" section, with the device results and the
    gate device-creation hardening.
- "Open device items" is rewritten as one list:
  - E1's items 1-4 and X1's items 1/3: item 1 "needs a decision" is superseded by X1's policy, items
    3 and 4 are resolved, and the closing sentence is dropped.
  - X2's item 2 is resolved.
  - E2's items 5-7 are resolved.
  - New items from the merge-wave device runs: Adreno static instanced emitters, the Pixel triangle
    sampler, Pixel compile times.
  - Remaining lane items: the ARM64 deforming emitter (Pi 5), the Intel Mesa crash (E3 + X2),
    X1's checks not yet on glibc, and E2's remaining function-scope arrays.
  - The resolved items are named once in the list's introduction.

### Other consolidation edits (not lane conflicts)
- `native/host/ABI.md`: the glass_render spec on/off film hash is updated to the merged film
  (`a072683c...`). X2's exact traversal changed the film; it equals X2's final film. The
  "static instanced triangle emitters are rejected" sentence is updated, since X2 removed the guard.
- `AGENT-README.txt`: the `SetMotionPositions` member line said "BEFORE THE FIRST RENDER". E3's
  MOTION section allows motion edits after a render, so the line now says "before or after a render".
- Gate hardening (task a): `require_webgpu_device` in `native/cycles/device.h`, used by every gate's
  unchecked `dynamic_cast<WebGPUDevice*>` (23 call sites in 18 gate sources). Recorded in
  `hardening.patch`; pre-hardening copies are in `prehardening/`.

### Not changed (reported only)
- `native/tools/triangle_lighting_gate.py` (X2-owned) still prints the old status-scope string
  "Static instanced triangle emitters are explicitly rejected ...", although the gate now renders
  and checks mode 4. Its after-hash was kept; a one-line fix for a later wave.
- Known-adapter tier table: unchanged. The adapter strings seen on the Samsung (Adreno (TM) 730,
  driver EV031.36.08.33), the HP (Intel(R) Xe Graphics (TGL GT2) / Mesa 24.0.8) and the Pixel
  (Adreno (TM) 640, EV031.31.04.01) all match their table rows; no mismatch.
