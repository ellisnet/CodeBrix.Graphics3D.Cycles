# Merge wave 7 reconciliation record (2026-10-23)

Main = d21bd4a5 + wave 6 (both parts, verified against baseline-merge6-2026-10-23.json: 126,099 files, 0 missing /
extra / changed; pre-merge.json sha256 c2e50a4c...). Tool: `tools/apply_lane7.py` (single-owner files: `git apply
--check` then `git apply --include`, after-hash verified; files whose main copy differs from the lane's before-hash:
`git merge-file` under `merge-<lane>/` with base = the file as the lane saw it, hash-checked against sha256Before:
d21bd4a5 for m1-lighting (`git show`), the lane's staged baseline index for m1-oracle (`git show :path` in its
worktree)). Logs: `apply-m1-lighting.log`, `apply-m1-oracle.log`; lane patch copies in `patches/`.

## m1-lighting (0021054819610c9278d77b49e899661c329da17c640aa134fd1bcefcdafd753e, 34 files)
- 29 single-owner files applied, every one equal to its after-hash (7 new files registered with `git add -N`).
- Three-way clean: AGENT-README.txt (m1-asset's bundle text + m1-lighting's environment world), compatibility/evidence.json
  (wave-6 part 2's re-recorded rows + m1-lighting's environment-light checkpoint row; valid JSON), native/cycles/device.cpp
  (m1-asset's arena-exhausted message + m1-lighting's lines; both kept).
- docs/PARITY.md, 1 conflict: the Lighting row was edited by S3 (wave 6: "the triangle sampler passes since ...") and
  by m1-lighting (environment lighting text replacing "Environment importance sampling/MIS, ..., textured emission").
  The two edits touch different words: resolved by a word-level union, script-checked (applying S3's change to the
  lane's row gives the same line as applying the lane's change to main's row). Real-asset and Material rows: main's.
- native/DEVICE.md, 1 conflict: m1-lighting appended its section at the end of the d21bd4a5 file, where wave 6 had
  appended S3, m1-asset and the wave-6 device tables. Resolved as an append after the wave-6 tables (the lane's file
  is a pure append to its base, checked). Chronological order: wave-6 sections, "Environment lighting and
  non-constant emitters", then m1-oracle's section.

## m1-oracle (8c1cca99e5bd2e6697a5e184c9ae3302ac17e78c06d27cc96c4b4cae6d252ad2, 27 files)
- 24 single-owner files applied, every one equal to its after-hash (AGENT-README.txt included: main's copy after
  m1-lighting equals the lane's staged baseline; native/cycles/cpu_arithmetic.cpp registered with `git add -N`).
- Three-way clean: MAINTAINER-README.txt (oracle-build paragraph in BUILDING; wave 6's moved glTF section intact).
- docs/PARITY.md, 1 conflict (adjacent rows): main changed Material + Lighting (S3, m1-lighting), the lane only the
  Real-asset row; main's Real-asset row equals the lane's base, so the lane's row replaces it (line-wise union).
- native/DEVICE.md, 1 conflict: the lane rewrote m1-asset's "Open items" paragraph (wave 6 had already replaced that
  paragraph with the pointer to items 9-13) and appended its section after m1-lighting's. Kept main's pointer
  (rewritten under coordinator change 2 below) and appended the lane's section after m1-lighting's (m1-lighting's
  section in main is byte-identical to the lane's base copy, checked).

## Coordinator changes applied after the patches
1. Open items 9-13 (native/DEVICE.md, "Open device items"): 9, 10 resolved (m1-oracle), 11 resolved (m1-lighting),
   each with a "Resolved" entry; 12 open with the measured 62,626 (62,514 before m1-oracle), below 65,535, renders
   correctly, first pre-Milestone-2 follow-up; 13 open, extended with the bench first-render walls and wave 6's
   Samsung nodes_world 95.8 s and HP 58.9/58.3 s. New items: 14 texture-sampling last-bit parity (0.59x / 0.91x),
   15 motion arithmetic last bits (`motion_acos`, shared motion interpolation), 16 macOS recipes untested at the
   baseline (folded from m1-oracle's own "Open items" paragraph so there is one list). Item 5 gains wave 6's fact (the
   other render_gate binaries now run in the device-scene flow) and the follow-up (remaining state gates onto the
   glibc flow, which also retires the device-CPU flag question).
2. Citations: m1-asset's evidence -> `native/evidence/merge-20261023b/m1-asset/...` (mapping from the wave-6 evidence
   report); m1-lighting's runs -> `native/evidence/merge-20261023c/m1-lighting/runs/...`; m1-oracle's hash tables and
   evidence -> `native/evidence/merge-20261023c/m1-oracle/...` (part 2 freezes them; list in the report). m1-lighting's
   Samsung row "except open items 1 and 4" gains "(the lane's tree predates their fix by lane S3)".
3. AGENT-README map-resolution sentence: "1024 or less is safe" removed; set the map resolution explicitly; a large map
   (0 = 4096 x 2048) needs an adapter whose limits allow a binding that large (WebGPU default 128 MiB, pointer to the
   storage-binding pitfall); otherwise the render fails with the device's allocation error.
4. native/oracle/README.md already states the fixed-baseline rule (m1-oracle's text): no edit.
5./6. Film hashes and ledger: see the report.
7. Bundle app gate verdict (native/tools/android_app_gate.py): the bundle branch returned True whatever the judgement;
   now the device result is the judgement (every GPU repeat must meet the practical criterion against the reference
   film), failures listed in status.json and comparisons.json ("failures", row "passed"), overall exit 1.
8. Bundle naming: new `--bundle-scene NAME` (default scene.json, else the only cbcOracleScene document; required for
   the bench folder with two documents); the chosen document is pushed as scene.json (the app's name), other
   documents stay behind. Control bundle: the `--bundle-id gltf/textured-plane` recipe is documented in
   MAINTAINER-README (the tool's default id is gltf/<file name>; the tool is unchanged).
9. Wave-6 evidence facts: summary paragraph at the head of "Merge wave 6: device tables" (116/96/41, S20 principled_svm
   418 s and principled_render through the exchange, Linux films unchanged, the four Samsung strict-count drops,
   Samsung nodes_world 95.8 s, 59 scratch folders removed on each of the Samsung and the HP).

## Findings during validation (no tolerance or gate changed)
- Host gate (`native/tools/host_gate.py`) failed 3 of 4 runs on the merged tree: `gpu-exit/run.3` against `cpu64/run.1`,
  component 1742: GPU 0.30171284 vs fixed-baseline CPU 0.30171299, difference 1.49e-7 > strict bound 1.30e-7, and this
  film requires strict. Every GPU host film differs run to run (atomic accumulation); component 1742 was seen at
  0.30171284 / 287 / 290 / 293 across waves 6-7 and the lane. The old (-march=native) CPU value 0.30171296 left a
  margin of one float step; the new CPU value removes it. Runs 1-3 (concurrent GPU load) failed, run 4 (GPU otherwise
  idle) passed. The APK was built from run 4's references (the CPU references are identical in all four runs except
  the 12 capDate input EXRs, as in every wave). Question for the coordinator (criterion policy, not changed here).
- Wave 6 staged Android `render_replay` binaries were merge-5 builds, stale against `native/src/webgpu_device.cpp`
  (m1-asset's limit change). Wave 7 rebuilds every replay binary in `replay-build/` (stage.py and run_corpus.py point
  there); `render_replay.cpp` changed in m1-lighting (recorder v9) anyway.
- The repo's default Android gate list (`build_cycles_android.py` GATE_TARGETS, 17 kinds) is shared with the Linux ARM64
  recipe, which asserts 25 artifacts for its default build; not changed. The merge tooling list (`tools/android_builds.sh`)
  already held principled_svm and transparent_shadow and now adds environment (37 kinds).

## Amendment (coordinator 16:05)
1. Host gate run.3 film: practical required, strict recorded (`host_gate.py` gpu loop `strict=i != 3`, applied to both
   `gpu/run.3` and `gpu-exit/run.3`, which are the same binary's 64-sample film; `android_host_gate.py` the same for
   its gpu/gpu-exit run.3; docstring updated). DEVICE.md: "Merge wave 7" section + policy-table row note.
2. `build_cycles_android.py` GATE_TARGETS + principled_svm, transparent_shadow, environment (20 gates); Linux ARM64
   recipe count = len(HOST_TARGETS)+len(GATE_TARGETS) (28); `android_host_gate.py` inspect count likewise (it asserted
   25 and blocked the APK build); docs/TESTING-LINUX-ARM64.md updated (default set, explicit list trimmed).
3. Item 12: 61,690 current, 62,626 pre-wave-6, INFERENCE on the rotation loop.
5. DEVICE.md line 120 generalised.
