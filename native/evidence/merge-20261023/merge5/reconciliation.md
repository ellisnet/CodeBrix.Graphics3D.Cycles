# Merge wave 5 (part 1) reconciliation (2026-10-22)

Base: main = 3392df3d + merge wave 4 uncommitted. Manifest check against
`~/ClaudeHome/cycles-wgsl-2026-10-22/baseline-merge-2026-10-22.json` (sha256 ff0580bd...): 91,601 files, 0 missing,
0 extra, 0 changed; `pre-merge.json` written (sha256 cd3f73fa...). Order: zc, f, x64, z0, s2, a650; every patch
verified by sha256 first (`patches/` holds copies). Tool: `tools/apply_lane.py` (single-owner files: `git apply
--check` then `git apply --include=...`, after-hash verified against the lane's changes.json; files already changed by
an earlier lane in this wave: `git merge-file -p` with base = pre-merge copy in `base/`, ours = main now, theirs = the
lane worktree file, which the tool first verifies equals the lane's after-hash; working copies in `merge-<lane>/`).
Record of which lanes touched which file: `touched.json`.

## Patches
| lane | patch sha256 | files | single-owner (after-hash match) | three-way |
| --- | --- | --- | --- | --- |
| zc | 1baedc7e8b3869c928810c636c9504626d4c61370f614731818d1e517b708008 | 3 | 3/3 (AGENT-README, MAINTAINER-README, DEVICE.md: first owner) | - |
| f | 0e0aa801620a0410bc9d944e8560ad599ca81bc218be4cdfc34823b59dc04250 | 5 | 4/4 | MAINTAINER-README: clean |
| x64 | 6618d29b94dbfce681812f77af6ae5b24f421f64227c84fd802fc2c6fdb32703 | 11 | 11/11 | - |
| z0 | 35500e89bcbcc646d8c82bb5424e53b945fd66e51969fbd5e47d8d5d19872c56 | 55 | 52/52 (incl. 30 files of the new checkpoint `native/evidence/step-zero-20261022`, docs/PARITY.md, pipelines.cpp/.h, surface_svm_test.h as first owner) | AGENT-README clean, DEVICE.md clean, MAINTAINER-README 1 conflict (below) |
| s2 | e282b3a809c32a8a24f3922fb53e2e18b7168ab8700df63bdb7271475d86d930 | 10 | 6/6 | AGENT-README, PARITY, DEVICE.md, surface_svm_test.h: clean (surface_svm_test.h then needed a type port, below) |
| a650 | 04431d452498bdd60b65b6e943795fb5bdfd03337c4b2771029484c1424b8c1c | 11 | 7/7 | AGENT-README clean, pipelines.cpp clean (then type port, below), PARITY 1 conflict, DEVICE.md 1 conflict |

Single-owner files: 83 files in all, every one equal to its lane's recorded after-hash at the time it was applied.
The S2/A650 WGSL files are disjoint (S2: wavefront_svm_nodes, wavefront_trig, wavefront_svm_microfacet; A650:
wavefront_svm, wavefront_svm_derivatives, wavefront_shadow_traversal, wavefront_shader_data), as the brief says (the
plan's note that both edit wavefront_svm.wgsl/_derivatives does not match the patches).

## Conflicts and ports
1. **MAINTAINER-README.txt (z0 x f)**, TESTING section: f inserted the "Testing on other machines" paragraph after
   the envelope paragraph; z0 appended its size-guard and feature-mask text to that same paragraph. Resolution: z0's
   text continues the envelope paragraph (it describes the product side of the same 60,000 limit), then f's
   paragraph, then "Licensing and source checks". Nothing dropped. (`merge-z0/MAINTAINER-README.txt.resolved`)
2. **docs/PARITY.md (a650 x s2)**, Material coverage row (one long table line): S2 rewrote the Samsung part of the row
   (Gabor note removed, node SVM probe and Principled lockstep pass, render images still differ from glibc); A650's
   change is one pure insertion (the Adreno 650 refusal sentence) after "...under the device reference policy; see
   `native/DEVICE.md`.". The insertion was placed at the same anchor in S2's row (anchor unique, verified by script).
3. **native/DEVICE.md (a650 x zc)**: both appended a section at the end of the file. Resolution: "Pipeline cache
   (lane ZC)" then "Adreno 650 lane" (seal order 18:42 / 22:17). The other wave-5 sections stay where their lanes put
   them: "Samsung follow-up fixes (lane S2)" just before "Open device items" (S2 rewrote that list), "Step zero (lane
   z0)" just after it. The open-items list says the Adreno 650 results are "at the end", which stays true.
   One stale cross-reference fixed: resolved item [8] said "The remaining render failure is open item 2"; S2 resolved
   item 2, the remaining render failure is item 4. Reworded to "open item 4".
4. **pipelines.cpp (a650 x z0), semantic**: A650's adapter-class refusal in `shader_features()` built
   `uint64_t region` from `shader_feature_names()` and tested `referenced.features&region`. After z0 both are
   `ShaderFeatures` (256 bits). Ported: `ShaderFeatures region=SF_NONE; ... region=n.bit;` and
   `if(const ShaderFeatures refused=referenced.features&region)` (explicit bool conversion = any bit set); the message
   still lists the refused features through `shader_feature_list(refused)`. Same semantics: region bits by the name
   in `refused_region` (SVM_NODES_ANY, identical bit set in both lanes, Gabor bit 63 included).
5. **surface_svm_test.h (s2 x z0), semantic, found by the first build** (b1 failed in 15 s): S2's opt-in
   `CODEBRIX_SVM_PROBE_DUMP` wrote `std::hex<<probe_features` (a uint64_t in S2's base); after z0 it is
   `ShaderFeatures`, which has no stream operator. Ported: hex of the words from the highest non-zero word down
   (lower words zero-padded to 16 digits), so any mask within bits 0-63 prints exactly as S2's lane did; `<iomanip>`
   included; loop braced (no new compiler warning).

## Coordinator/maintainer additions
- **Metal instance patch** (`patches/metal-instance.patch`, sha256 4442054bd2070ca90ce75d6eb3192f645f136f9c3429b38d63f6395cd470cefb)
  applied to `native/src/webgpu_device.cpp` with `git apply --check` first. Non-Apple branch requests
  WGPUInstanceBackend_Vulkan and produces the same two error strings as before ("Cannot create Vulkan WebGPU
  instance", "No hardware Vulkan adapter"). docs/TESTING-MACOS-X64.md: the "Blocking gap ... Vulkan only" MISSING
  section became "WebGPU backend on Apple platforms: Metal" with an UNTESTED marker and the expected grep output;
  the later "GPU parts need the blocking gap above to be closed first" sentence now points at it (UNTESTED markers
  14 -> 16). native/host/MACOS_X64.md: "Known gap" section rewritten the same way (UNTESTED).
- **Size guard 1,110,000 bytes**: `pipeline_size_warning_default_bytes` in pipelines.h (comment rewritten: 65,535 x
  16.9 = 1,107,542 rounded up, ~55,500 at the central density, entries that measure safe may warn; first wording said
  the node-render scene warns, true at Step zero's 1,126,723 bytes but not on the merged tree, where shade_background
  is 1,095,718: corrected after the gates in the comment (same line count), MAINTAINER-README and DEVICE.md, then
  Linux and Android rebuilt and hash-compared, see "Doc fix after the gates"), `native/tools/pipeline_size_guard_check.py` DEFAULT_BYTES, the managed test
  `WebGpuRenderTests.gpu_device_report_states_the_pipeline_size_guard` (asserted 1200000; it would have failed),
  MAINTAINER-README and both DEVICE.md places. `CapabilityReportTests` keeps its synthetic 1200000 sample (a parse
  test of a hand-written report, not the product default).
- **principled_bounce shares one device**: `native/cycles/surface_gate.cpp` `rejections()` creates one device before
  the 21-case loop and calls `WebGPUDevice::recover()` at the end of every case (after that case's checks, before its
  scene/scheduler/buffers are released); a failed recovery fails the gate. Every case still builds its own scene and
  is evaluated exactly as before. The file builds four gates (surface, microfacet_bounce, glass_bounce,
  principled_bounce); all four were run before (b2 binaries) and after (b3) - see "Linux validation".

## Linux validation (summary; detail in the report and `validation/<label>/`)
- Builds: b1 failed (port 5 above), b2 (after the port; first full incremental build, 127 objects), b3 (bounce change +
  brace fix), b4 (comment-only doc fix): 57/57 Linux binaries byte-identical b3 -> b4. All Linux gates ran on b3.
- principled_bounce identity: `bounce/compare.json` - all four surface_gate.cpp gates: every stdout field except the
  pipeline report equal, output files equal, stderr equal (timestamps stripped); pipelines compiled 40 -> 10 per gate.
- Films: glass `db8e84f9...` (all-lanes, superset on + 4 groups, simulated-Adreno-650 glass run), node render
  `b9d1da3f...` (nodes gate, superset), Principled `e81feb12...` (principled gate main + 12 superset groups, superset
  dump). all-lanes vs merge 4: 259/280 films identical; the 21 that differ are 12 cases that are run-to-run variant
  (atomic accumulation; 11 of the 12 cases already listed by lane A650, all with a gpu/repeat difference inside one run
  in merge 4 or merge 5, except deforming stationary case-5, whose gpu/repeat pair agrees within each run but which
  lane A650 also saw change between runs). Nodes gate: 176/176 films identical to merge 4. Principled gate: only the
  GPU films differ (e81feb12, accepted); CPU films identical. Host gate: 3,967/3,997 binary films identical; the 30 are
  the 18 known run-to-run GPU films (gpu 6, gpu-exit 5, abi-gpu 7) and 12 generated input EXRs whose only difference
  is the capDate header.

## Doc fix after the gates
On the merged tree the node render's `shade_background` inlines to 1,095,718 bytes (z0 measured 1,126,723 before S2
and A650 shrank the module), so no gate scene warns at 1,110,000. The first wording of the threshold text said the
node-render scene warns. Corrected in pipelines.h (comment only, same line count), MAINTAINER-README.txt and
native/DEVICE.md; then Linux rebuilt (b4: binaries identical) and the Android gate builds, host builds and the APK
rebuilt so that their build records match the final sources (staged Android binaries 134/134 identical to the first
build; APK native payloads 56/56 identical, the APK file itself differs by packaging).

## Final state
`pre-merge.json` cd3f73fa... -> `post-merge.json` (see report): 85 paths changed against pre-merge (43 modified, 42
added: the 30-file `native/evidence/step-zero-20261022` checkpoint and 12 new source/doc files), 0 removed, no existing
evidence file changed. `handoff/wave5.patch` (+ `.sha256`) = that diff, `git apply --check -R` passes on main.
