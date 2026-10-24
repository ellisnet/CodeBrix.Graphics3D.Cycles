# Merge wave 8 reconciliation record (2026-10-24)

Main = `5aa565a2` (clean, verified 07:04; `pre-merge.json`). Every wave-A lane patch was made against `aa71c740`;
main is ahead by the 66 upstream files of `m2-shared/main-moved-2026-10-24.md`. Tool: `tools/apply_lane8.py`
(single-owner files: `git apply --check` then `git apply --include`, after-hash verified against the lane's
`changes.json`; new files registered with `git add -N`; files whose main copy differs from the lane's before-hash:
`git merge-file` under `merge-<lane>/` with base = `git show aa71c740:<path>` (hash-checked against sha256Before),
ours = main after the earlier patches, theirs = the lane worktree file (hash-checked against sha256After)). Conflict
blocks were resolved with `tools/resolve.py` and the per-file specs in `specs/` (the resolved copies are
`merge-<lane>/<path>.resolved`); JSON ledgers with `tools/json3.py` (key-wise three-way merge, aborts on a real
conflict); PARITY rows with `tools/rowmerge.py` (word-level three-way per row). Logs `apply-<lane>.log`, lane patch
copies in `patches/`. `added-lines.txt` (`tools/added_lines.py`) lists every lane-added line that is not verbatim in
the merged tree: each one is a reconciliation listed below.

Patch order (C2): m2-shrink, m2-lighttree, m2-shadowcatcher, m2-ao (m2-mnee = part 1b).

## m2-shrink (c60c93966f7cc6d8d0bd1287e7d575eec8b266a7b10b722673f4ec7a408e9faf, 7 files)
- 6 single-owner files applied, every one equal to its after-hash (compatibility/evidence.json, svm_layout.cpp,
  wavefront_float/shader_data/svm/trig.wgsl).
- native/DEVICE.md three-way clean (main's 11 upstream lines + the lane's section and open items 12/13).

## m2-lighttree (ef566bb587605e139cf16344c8351a3d70141ef8d7ec7487c1c484e15f945eaf, 42 files)
- 37 single-owner files applied, every one equal to its after-hash (9 new, `git add -N`).
- Three-way clean: AGENT-README.txt, compatibility/evidence.json (valid JSON), docs/PARITY.md, native/host/host_api.inc.
- native/DEVICE.md, 1 conflict: both m2-shrink and m2-lighttree appended a section at the end of the aa71c740 file.
  Resolved as an append in patch order (m2-shrink's section, then m2-lighttree's).

## m2-shadowcatcher (6064eacd93210628e63c11c49412d39dec03737ac86e40cb3847b5fc1c49ac73, 42 files)
- 13 single-owner files applied, every one equal to its after-hash (9 new, including the lane-created evidence
  checkpoint `native/evidence/m2-shadowcatcher-20261023/shadow-catcher/validation.json`: kept as the lane made it, cited
  by `compatibility/evidence.json`, never overwritten; C2 (g)).
- Three-way clean: compatibility/evidence.json, closest.cpp, device.h, wavefront.h, ABI.md, host README.md,
  wavefront_background/closest/light_eval/lighting/surface.wgsl, CapabilityReportTests.cs, and the lane's
  `WebGpuRenderTests.cs` hunk applied to main's renamed `GpuRenderTests.cs` (C2 (d); the new theory
  `gpu_shadow_catcher_passes_match_the_cpu_reference` is in GpuRenderTests.cs once).
- Conflicts (16 files) and their resolution:
  - pipelines.h/.cpp (C2 (b)): bit definitions of both lanes kept; `SF_ALL` = union (0-61, 63, 64, 65, 94-96, 102),
    names table both rows, static_assert count 69 with both lanes' bit tests.
  - Capture entries (C2 (a)): renumbered in patch order. m2-lighttree keeps 21 (`shade_surface_light_select`, surface)
    and 22 (`light_tree_forward`, background); m2-shadowcatcher's split module moves from 21-23 to 23-25
    (`shadow_catcher_assign/split/count`, module `shadow_catcher`). ONE format bump: v10 = any of entries 21-25 (26 after
    m2-ao). device.cpp entry table, render_capture.h (array size, v10 condition, comment), render_replay.cpp (names,
    entries, array sizes) and render_replay.py (comment, module list) changed together. The capture.json
    `pipelineDispatches` list keeps 21 counts for formats before v10 (m2-shadowcatcher's form, so older-format captures
    keep their wave-7 metadata; m2-lighttree wrote every count); `render_replay.py` pads either form.
    NOTE: the coordinator's C2 text says "lighttree 21, shadowcatcher 22-24, ao 25"; m2-lighttree has TWO entries
    (21 and 22, its report and touches file), so patch order gives shadowcatcher 23-25 and ao 26 (question in the report).
  - background.cpp, surface.cpp refusals: both lanes' removals kept (LIGHT_LINKING out of the refusal with the
    linking-state check; has_shadow_catcher out with `validate_shadow_catcher()`); surface's pass refusal masks the
    shadow catcher passes.
  - host_api.inc (C2 (c)): passes = combined + the three shadow catcher passes; rejections "light trees" -> "shadow
    linking" (m2-lighttree).
  - build_cycles.py: background module = ... + shadow catcher helpers + background + light eval + light tree; closest
    module = ... + shadow catcher helpers + closest + light link; `shadow_catcher_shader.inc` written.
  - build_cycles_android.py GATE_TARGETS + light_tree + shadow_catcher; inject_cycles.cmake both gate targets.
  - AGENT-README.txt "WHAT THIS PACKAGE DOES NOT DO" bullet: one sentence holding both lanes' facts (passes other than
    Combined and the shadow catcher passes; light tree and linking render).
  - docs/PARITY.md: adjacent rows (m2-lighttree's Lighting row, m2-shadowcatcher's Output/features row): line union.
  - compatibility/reviews.json: key-wise union (`json3.py`); the shadow catcher rows' test path
    `tests/.../WebGpuRenderTests.cs` -> `tests/.../GpuRenderTests.cs` (main's rename; the theory lives there).
  - native/DEVICE.md: append conflict; m2-shadowcatcher's section after m2-lighttree's.

## m2-ao (c569fd6555b68bca720143eded3302de14bebc4e0daa03f915398837a5aad4c7, 38 files)
- 13 single-owner files applied, every one equal to its after-hash (8 new, including the managed test
  `WebGpuAmbientOcclusionTests.cs`).
- Three-way clean: compatibility/evidence.json, device.h, svm_layout.cpp (m2-shrink's `svm_closure_bsdf` + m2-ao's
  AO layout), wavefront_svm.wgsl (m2-shrink's one line + the AO node handler).
- Conflicts (21 files) and their resolution:
  - pipelines.h/.cpp: AO bits 106/107/108 added; `SF_ALL` = union (0-61, 63, 64, 65, 94-96, 102, 106, 108; 107 is a
    module variant, not in SF_ALL, its own static_assert kept); SF_SUPERSET_SCENE_ONLY = union (SHADOW_CATCHER,
    NODE_AO, AO_SHADOW); static_assert count 71; `enabled_features` keeps both the shadow catcher and the AO_SHADOW lines.
  - Capture entry: `prepare_surface_raytrace` (module `surface_raytrace`) moves from 21 to 26; v10 = any of 21-26;
    render_capture.h keeps both new module writes (shadow_catcher.wgsl when the scene has a catcher;
    surface_raytrace.wgsl, which m2-ao writes for every capture).
  - queue.cpp: COUNT_POSSIBLE_SPLITS case (m2-shadowcatcher) + SHADE_SURFACE_RAYTRACE routing (m2-ao).
  - shadow.cpp / surface.cpp refusals: KERNEL_FEATURE_AO removed (m2-ao) and has_shadow_catcher replaced by
    `validate_shadow_catcher()` (m2-shadowcatcher); surface.cpp `surface_source()` keeps `shadow_catcher_layout()` and
    `enqueue_surface` takes m2-ao's kernel argument. (The first build failed on one missing parenthesis in my
    shadow.cpp composition; corrected before any run.)
  - surface_gate.cpp: test 1 = m2-lighttree's expectation ("Missing native light tree"), test 2 = m2-ao's
    (PATH_GUIDING instead of AO).
  - host_api.inc + CapabilityReportTests.cs (C2 (c)): written passes = combined, shadow_catcher,
    shadow_catcher_sample_count, shadow_catcher_matte, ao (one expectation holding both lanes' facts).
  - wavefront_shadow.wgsl (4 blocks): the three flag checks compose both lanes: m2-ao's `shadow_flag_supported(...)`
    call, inside m2-shadowcatcher's `//#if SHADOW_CATCHER else <m2-ao line>` region with the catcher flags masked
    (`shadow_flag_supported(flag&~SC_FLAGS)`). Without the catcher bit the text is m2-ao's line; without AO_SHADOW,
    `shadow_flag_supported` tests (UNSUPPORTED|AO flag), so the catcher-only test equals the lane's
    `(flag&(UNSUPPORTED&~SC_FLAGS))` (the catcher flags and the AO flag are disjoint). shadow_write_film: m2-ao's
    AO_SHADOW region (which returns for AO shadow paths) is placed before m2-shadowcatcher's regioned combined /
    adaptive lines (as each lane wrote them). See finding "shadow catcher + AO" in the report.
  - wavefront_surface.wgsl prepare check: m2-ao's `surf_queue()` + m2-shadowcatcher's regioned flag line.
  - build_cycles.py surface module: + wavefront_ao.wgsl (m2-ao) and + light_tree (m2-lighttree).
  - inject_cycles.cmake: all four new gate targets.
  - compatibility/reviews.json: key-wise union.
  - AGENT-README.txt RENDER-TIME LIMITS bullet: passes other than Combined, AO and the three shadow catcher passes;
    the "DOES NOT DO" bullet gains AO in the same way (consistency with m2-ao's facts; m2-ao had not edited that line).
  - native/host/README.md: "only Combined, AO and the shadow catcher passes".
  - docs/PARITY.md: Path integration row word-level union (m2-ao's fast GI sentence + m2-shadowcatcher's shadow
    catcher sentence); Material coverage row = m2-ao's (only it changed it); Output/features row: sentence union
    (shadow catcher sentence, then AO sentence, then "Other data passes ... pending").
  - native/DEVICE.md: open item 17 = m2-lighttree's (replays with shadow-path compaction) + one merge sentence on the
    m2-ao allocator interaction; m2-ao's "17. Ambient occlusion remainder" renumbered 18 (one consolidated list, each
    lane's wording kept). m2-ao had inserted its section before "Merge wave 7"; moved to the end so the sections are in
    patch order (m2-shrink, m2-lighttree, m2-shadowcatcher, m2-ao).

## Shadow-slot allocator interaction (brief)
m2-lighttree left the allocator as it was (its open item 17: case 2's compaction counts follow the atomic
compare-exchange slot order in `surface_direct_light`, so that capture does not replay exactly). m2-ao reserves two
shadow slots per surface input (control op 5) and uses the stable serial path-array scan (`queue.cpp` p[21]) whenever
KERNEL_FEATURE_AO is set. Merged code = the union: `reserve_shadow=(use_direct_light&&transparent_shadows)||ao`,
`reserve[1]=ao?2*n:n`, `p[21]=transparent_shadows||AO`; for scenes without AO both expressions reduce to the aa71c740
code (no behaviour change). Light-tree case 2 has no AO, so it still does not replay; recorded in open item 17.

## Coordinator-decided changes
- C1 (two gold standards): `native/oracle/compare.py`: backends classified by name (cpu-* and metal-* gold standards,
  others candidates); every pair computed and reported with `role` = gating (candidate vs CPU; CPU film is the
  reference b) or reported (candidate vs Metal, gold vs gold, candidate vs candidate); only gating pairs add to
  `problems` (practical failures, sample mismatch); `goldStandards`, `candidates`, `gatingPairs` in the JSON; table
  marks each pair and each backend's role; `--scene-root` sorts cpu-*, then metal-*, then the rest; no CPU backend =
  Invalid ("no gold standard", exit 2). Self-test: the 10 existing checks (films renamed cpu-*/wgsl-* so each run has a
  CPU gold standard) + metalFarWgslClosePerfect, wgslFarFromCpuNotPerfect, goldOnlyPerfectPairReported,
  cpuIsTheReference, noGoldStandardInvalid: 15/15 PASS. Docs: native/oracle/README.md (comparator paragraph),
  MAINTAINER-README.txt (gold standards and candidates paragraph, TESTING), AGENT-README.txt ("Results" bullet,
  consumer wording, no versions/dates), native/DEVICE.md ("Two gold standards" paragraph in "Validation reference
  policy", pointer to the 2026-10-19 record kept as history), native/host/MACOS_ARM64.md (one sentence after the
  2026-10-23 findings table), docs/PARITY.md (Full parity decision row), and the two remaining "bounded deviation"
  sentences (docs/TESTING-MACOS-ARM64.md, native/host/ABI.md).
- C2: see the per-lane records above (patch order; capture renumbering with one v10 bump; SF_ALL union; passes union;
  test expectation; renamed test file; upstream-changed docs three-way; evidence folder kept). The ledger stale list
  and the HP/tablet host-capture re-record are part 2's (report).
- tools/gltf and native/tests/host/example.c keep the light tree off (checked: GltfSceneConverter.cs lines 80 and 167,
  example.c line 48; unchanged).

## Doc consistency edits required by the union (not new facts)
- native/host/README.md "Known limits": "needs the integrator's denoising, adaptive sampling, guiding and light tree
  switched off" -> without "light tree" (m2-lighttree's fact; the lane had not edited this file).
- native/host/ABI.md "Facts measured": passes = Combined, AO and the three shadow catcher passes; "light trees are not
  implemented" -> "the light tree and light linking render".
- native/tools/build_cycles_android.py GATE_TARGETS: the brief requires the default list to hold the new light-tree,
  shadow-catcher and AO gates; m2-lighttree and m2-shadowcatcher had added theirs, m2-ao had not, so `ao_state` and
  `ao_render` were added (24 default gates). native/tools/build_cycles_linux_arm64.py's count comment (28 -> 32: 8 host
  outputs + 24 gates; the assert is the formula) and docs/TESTING-LINUX-ARM64.md's default-set sentence follow.

## Ledger (C2 (f))
`compatibility/evidence.json` merged key-wise: 22 checkpoints, ALL stale on the merged tree (each pins sources that
another wave-A lane changed): the 13 that m2-shrink re-recorded in its lane, the 6 replay checkpoints
(render_replay.cpp/.py changed by the v10 renumbering), and the three new ones (light-tree, shadow-catcher,
ambient-occlusion). List: `validation/ledger-stale-part1a.txt` (`tools/ledger_stale.py`). Three lanes cite lane-local
reports under `native/work/<lane>/ledger/` (m2-shrink 13, m2-lighttree 1, m2-ao 1): those folders were COPIED from the
worktrees into main's `native/work/` (ignored, 296 KB) so the citations resolve on this machine; part 2 re-records
every entry after the full corpus and freezes the reports into its checkpoint. `inventory.py check` exits 1 at the
first stale entry (native-render, background.cpp), as expected.

## Main moved during the merge
07:59 (coordinator, Jeremy's request): main fast-forwarded 5aa565a2 -> 139b24f0, one added file `.gitattributes`
(no overlap with any merged file; working tree, index and diffs unchanged; no renormalization run). From then on the
checkpoints and the seal diff against 139b24f0; `handoff/wave8.patch` = `git diff --binary 139b24f0`.

## Part 1b: m2-mnee (24ccae3f5992fe5b300807c7ffc3ea7cbd2991ff72b416cc0c4200174b338537, 39 files)
Arrived 07:50 while the part-1a gates ran (they pin the source tree), so the merge was STAGED first
(`merge-m2-mnee/<path>.result`, resolved with `MERGE8_STAGE=1`) and installed at 08:25 after the last part-1a gate
(`tools/install_mnee.py`: main's file still equal to the staged "ours", single-owner/new results equal to the lane
after-hash). 6 single-owner/new + 2 more new files equal their after-hashes; three-way clean: AGENT-README.txt,
wavefront.h, wavefront_background/closest/lighting.wgsl; 25 files with conflicts resolved:
- pipelines.h/.cpp: bit 110 SF_MNEE in SF_ALL and SF_SUPERSET_SCENE_ONLY; names table; static_assert count 72 with
  SF_MNEE.test(110); `enabled_features` keeps the catcher, AO_SHADOW and MNEE lines.
- Capture entries: MNEE's 21-27 (mnee_receiver ... mnee_contribute) -> 27-33, mnee_cleanup 28 -> 34, unknown 35
  (`mnee_entry_of` returns 27+i); v10 = any entry from 21 on; arrays 35 in render_capture.h and render_replay.cpp;
  render_replay.py names + ['surface']*8. NOTE: the coordinator's message says 26-33 "after ao's 25"; in this merge ao
  is 26 (m2-lighttree has two entries), so MNEE is 27-34 (question).
- device.h: shadow catcher members + MNEE members; `enqueue_surface` with m2-ao's kernel argument + MNEE declarations.
- background.cpp / closest.cpp / surface.cpp / shadow.cpp refusals: every lane's removal kept (MNEE and use_caustics
  out, `if(use_caustics)validate_mnee()` added); layouts = ... + shadow_catcher_layout() + mnee_layout(); surface light
  NEE refusal without LIGHT_TREE/LIGHT_LINKING (m2-lighttree) and without MNEE (m2-mnee).
- surface.cpp shadow-slot reservation = union: `(use_direct_light&&(transparent_shadows||use_caustics))||ao`;
  queue.cpp p[21] stable scan = transparent_shadows || AO || use_caustics (for scenes with none of the three: unchanged).
- queue.cpp: INTERSECT_MNEE routing + the catcher count + the raytrace routing.
- closest_gate.cpp: test 4 = m2-mnee's expectation, test 5 = m2-lighttree's. gate.cpp: m2-mnee's compaction cases +
  m2-ao's INTERSECT_SUBSURFACE probe.
- wavefront_light_eval.wgsl integrate_suns: m2-lighttree's LIGHT_TREE slot region, then m2-mnee's MNEE culling region
  (MNEE refuses the light tree, so the two never run together).
- build_cycles.py surface module: ... + ao + light tree + `//#if MNEE` closest traversal `//#endif` + wavefront_mnee.wgsl
  (the closest-traversal prefix holds the same regions as in the lane; checked).
- build_cycles_android.py GATE_TARGETS + mnee, mnee_render (26 default gates; ARM64 count comment and docs/TESTING-LINUX-ARM64.md updated to 26 gates / 34 artifacts before the 1b builds).
- inject_cycles.cmake: shadow_catcher.cpp and mnee.cpp in the device sources; both MNEE gate targets.
- compatibility/evidence.json, reviews.json: key-wise union (json3.py). evidence `mnee` cites
  `native/work/m2-mnee/ledger/mnee/validation.json`, copied from the worktree like the others.
- docs/PARITY.md Lighting row: m2-mnee's inserted sentence placed after "non-constant point/spot/area/sun shaders"
  ahead of m2-lighttree's light-tree sentence; its "open item 17" -> "open item 19".
- native/DEVICE.md: m2-mnee's open item 17 -> 19 (lane wording kept); new item 20 records the MNEE refusals with the
  light tree (coordinator (6)); m2-mnee's section appended last ("Manifold next-event estimation (caustics)"); its
  device-table and section references "open item 17" -> 19. Consolidated list: 17 replays with compaction (m2-lighttree),
  18 AO remainder (m2-ao), 19 MNEE on Adreno 730/650 (m2-mnee), 20 MNEE refusals, 21-26 merge findings.
- Part-1b validation incident: my first composition of `wavefront_light_eval.wgsl` (LIGHT_TREE slot region + MNEE
  culling region) lost one `//#endif` (git merge-file had kept the two lanes' shared `//#endif` outside the conflict
  block and my "both" resolution dropped it), so every surface module failed to specialize ("Unterminated WGSL
  feature marker"). Found by the first 1b run (light tree, shadow catcher, AO image and MNEE gates failed at once),
  that run stopped (my own process groups only), the `//#endif` restored, every shader file checked for balanced
  markers, outputs of the invalid run removed, the whole 1b set re-run.

## Coordinator decision 09:05 (C1 scope): CPU gold standard of the candidate's own host ISA family
Implemented in `native/oracle/compare.py`: `isa_of()` (wgsl-android-* = x64, otherwise arm64 when the name says
arm64, else x64); a candidate-versus-CPU pair gates only within one ISA family; cross-ISA pairs are reported; a
candidate with no same-ISA CPU film present gates against the CPU film present and the report lists it under
`crossIsaGating` (table note). Self-test +3 (crossIsaReported, arm64CandidateGatesOnArm64,
noSameIsaCpuGatesOnPresentCpu): 18/18 PASS. Docs: comparator paragraph of native/oracle/README.md, the
MAINTAINER-README gold-standards paragraph, DEVICE.md item 25 (resolution sentence; the cross-ISA CPU difference
stays recorded). All comparator runs repeated with it (`validation/b8f-*`): every Linux run perfect, and with the Mac
films added the four starters, bench and HDRI bench read perfect (the arm64 pairs reported).

## Seal
Android gate trees re-run (run 3, no compile) after the last tracked edits so the staged lists' source pins equal the
tree; the APK was built after that. HEAD at seal 139b24f0 (pre-merge 5aa565a2 + `.gitattributes`); wave8.patch =
`git diff --binary 139b24f0`.

## Coordinator answers after the seal (recorded, no action)
(1) Capture numbering confirmed: lighttree 21-22, shadowcatcher 23-25, ao 26, MNEE 27-34, one v10 bump. (2) Gold-only
run reads perfect; WGSL-vs-WGSL pairs reported, never gating (now the written rule). (3)/(4) Items 21, 22 and 23 stay
documented for Jeremy's decision. (5) Linux-only ledger entries left for part 2. Part 1 ACCEPTED as sealed.

## Part 1c (Jeremy's GO 11:28 on DEVICE.md items 21 and 22 only; part 2 running in the same checkout)
- Item 21: `native/cycles/volume_gate.cpp` rejection case 9 expects "shadow catcher with volumes" (substring of the
  refusal "WebGPU shadow catcher with volumes not implemented"); cases 7 and 8 unchanged.
- Item 22: `tools/hosttests/ImageModes/Program.cs` reads `imageInput.fileReading` by kind. Mapping: JSON true -> files
  are read; JSON false -> not read (the `CBC_HOST_NO_IMAGE_FILES` builds, osx-arm64); the string "development-only"
  (what `host_api.inc` emits when image file readers are compiled in) -> files are read; any other value throws
  "Unknown imageInput.fileReading value" so a new host value cannot silently skip the four file cases.
- Builds: Linux `codebrix_cycles_volume_gate` only (1 compile; no-op re-run 0 compiles); Linux binary 20a5463e ->
  5a963979. Android volume gate only, in place in the merge-5 trees, then `stage.py review` both RIDs (only the
  volume gate hash changed in each list): arm64 195446d0 -> 9ce0bb4a, x64 21df8e96 -> 352ff9c5. Old lists kept as
  `validation/staged-binaries-android-*.before-1c.json`. The validation APK was NOT rebuilt (not asked; its embedded
  source pins predate 1c).
- Linux: volume gate PASS (every case, incl. 10 and later, films identical to wave 7's all-lanes run),
  volume_boundary_gate PASS (films identical), host_gate.py PASS with every phase (3,146 comparisons, 0 practical,
  149 strict, max 1.53e-5).
- DEVICE.md items 21 and 22 marked resolved (text kept as history) with "Resolved" entries.
- The re-emitted wave8.patch (98 files) also contains an AGENT-README.txt paragraph on shadow catcher + AO written at
  11:32 by someone else (not by the merge agent); included as it stands in the tree.
- Caution for part 2: volume_gate.cpp, Program.cs and DEVICE.md changed while it runs; its gate drivers pin the source
  tree (`render_gate.inputs()`), so any part-2 Linux gate that started before ~11:33 and ended after may report
  inputsUnchanged false.
