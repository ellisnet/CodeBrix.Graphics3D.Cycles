# Merge wave 6 (part 1) reconciliation (2026-10-23)

Base: main = d21bd4a5, clean (`git status --short` empty; `pre-merge.json`). Order: a650-2, s3, m1-asset; every patch
verified by sha256 first (`patches/` holds copies). Tool: `tools/apply_lane.py` (copy of merge 5's): single-owner files
`git apply --check` then `git apply --include=...`, after-hash verified against the lane's changes.json; files already
changed by an earlier lane in this wave: `git merge-file -p` with base = the d21bd4a5 file (`base/`, from `git show`),
ours = main now, theirs = the lane worktree file (verified equal to the lane's after-hash first); working copies in
`merge-<lane>/`. Which lanes touched which file: `touched.json`.

## Patches
| lane | patch sha256 | files | single-owner (after-hash match) | three-way |
| --- | --- | --- | --- | --- |
| a650-2 | ecdf96ca838a2a3b7a741bb68b6fb3544e9c38e87b2388db9d3e486c8a03afdc | 2 | 2/2 (DEVICE.md first owner, wavefront_svm_microfacet.wgsl) | - |
| s3 | 9639274b426afca6859e16548459fb17ff05d5e1f7b2dc106879fae1829ace7b | 7 | 6/6 (MAINTAINER-README and PARITY first owner, reference_policy.h, render_gate.cpp, wavefront_triangle_light.wgsl, android_reference.py) | DEVICE.md clean |
| m1-asset | db3481e3b3ad81e58594549ccc0c5f004d0187924c681256e6a43648649c770d | 40 | 37/37 (10 new files; author_starters.py is a mode change only, 100644 -> 100755) | MAINTAINER-README clean, DEVICE.md clean, PARITY 1 conflict |

Single-owner files: 45 applications in all (2 + 6 + 37), every one equal to its lane's recorded after-hash. New files
are registered with `git add -N` so they appear in `git diff`. The two WGSL files are disjoint (a650-2:
wavefront_svm_microfacet.wgsl; s3: wavefront_triangle_light.wgsl); m1-asset changes no shader.

## Conflicts and reconciliation edits
1. **docs/PARITY.md (s3 x m1-asset), 1 textual conflict.** The table rows are one line each. S3 rewrote lines 37
   (Material coverage: Samsung renders pass end to end on the glibc flow) and 38 (Lighting: the triangle sampler
   passes); m1-asset rewrote lines 36 (Real-asset images: bench with original materials) and 41 (Platforms/delivery:
   bundles). Adjacent hunks, no line changed by both: resolved by a line-wise union (script asserts no line was changed
   by both lanes). `merge-m1-asset/docs/PARITY.md.resolved`. Nothing dropped.
2. **native/DEVICE.md (a650-2, s3, m1-asset), clean three-way merges, then reordered.** As merged, S3's section sat
   just before "Open device items" (where the lane put it, like S2), a650-2's subsection "Adreno 650 lane 2" inside
   the "Adreno 650 lane" section, and m1-asset's section at the end. The brief orders the wave's sections
   chronologically A650 lane 2, S3, m1-asset, so S3's section ("Samsung render images and sampler on the glibc flow")
   was moved, unchanged, to just before m1-asset's section (after "Merge wave 5: device tables"). a650-2 stays a
   subsection of the Adreno 650 lane, where its lane put it. Pre-edit copy: `merge-m1-asset/native/DEVICE.md.after-3way`.
   - S3's open-items edits kept: items 1 and 4 marked resolved (with "Resolved" entries), item 5 rewritten.
   - Single list kept single: m1-asset's "Open items:" paragraph at the end of its section was moved into "Open device
     items" as items 9-13 (bench vs the Linux CPU oracle; camera-ray last-bit parity; HDRI bench on WGSL; size-guard
     warnings on the bench graph; first-render compile times). The section now points at them. Item 10 also records the
     factor-only bench pixel (45,22) (coordinator decision Q5 on m1-asset: an open item assigned with the camera-ray work).
   - Stale cross-references fixed: resolved [8] said "The remaining render failure is open item 4" (item 4 is now
     resolved by S3) -> "The remaining render failure (open item 4) was fixed by lane S3."; the list's intro said the
     Adreno 650 sections are "at the end" -> "further below" (two sections now follow them).
3. **MAINTAINER-README.txt (s3, m1-asset), clean three-way.** One structural fix: m1-asset inserted its new top-level
   section "GLTF EXAMPLE TOOL, SCENE BUNDLES AND THE BENCH ORACLE SCENE" in the middle of TESTING, so the rest of
   TESTING (native validation, envelope, size guard, other machines, licensing checks) fell under the glTF heading. The
   section was moved, unchanged, to the end of TESTING (before GENERATED OBJECT MODEL), and its underline changed from
   '-' to '=' like the other top-level headings.

## Coordinator additions
1. **Stage-exchange runner** -> `tools/run_corpus.py` (work-dir tool, untracked): copy of S3's
   `handoff/run_corpus.stage-exchange.py` (sha256 recorded in the report), plus `run_corpus-merge6.diff`:
   - C2 = native/work/merge6; remote prefixes `codebrix-m6-` / `codebrix-m6-stage`; the Android build trees and the
     replay build are read from merge 5's folder (built in place this wave);
   - REFERENCE_KINDS gains `microfacet_render`, `render`, `volume_render`, `closed_volume_render` as 'render', so
     every job of those gates (arm: microfacet_render; motion: render-static, render-motion-0..7; volume:
     volume-absorption/-isotropic/-forward/-backward, closed-single/-overlap/-inside) takes the device-scene flow
     (scene dump, Linux glibc reference, exchange run, Linux stage judge, replays judged against the glibc film);
   - the job's scene parameters (env keys not starting with CODEBRIX_CYCLES_, i.e. CODEBRIX_VOLUME_*) are passed to the
     Linux scene-reference and stage-judge passes too (they were device-only, which matters only for the volume jobs).
2. **Lane rule** (A650-2 Q2): one sentence in MAINTAINER-README TESTING, after the envelope sentence.
   Runner sha256: S3's copy b2810c44caeb1498b75c245bfbf698fa1ed55e3f9d522f64fd807aa88e5e3414 (kept as
   `tools/run_corpus.stage-exchange.orig.py`); merge-6 runner f7e1c82c1ae5d80dc9cb2773956937f01bd16b9ec1968b9168025a93b6ddd3eb.
   Linux self-test of the exchange for every newly added gate: `validation/s-*/flow` (judge = local lockstep exactly).
2. **Lane rule** (A650-2 Q2): MAINTAINER-README TESTING, after the envelope sentence: "Any change to WGSL that every
   device compiles also runs the Samsung Galaxy S20 FE (Adreno 650) principled_svm and transparent_shadow jobs as
   standard regression jobs whenever that phone is attached (about 7 minutes together): its shader compiler has
   miscompiled repeated inlined integer-arithmetic bodies in shared code (native/DEVICE.md, "Adreno 650 lane 2")."
3. **Films:** glass db8e84f9, node render b9d1da3f, Principled e81feb12, all unchanged. Principled superset: 11 groups
   (merge 5: 12), 40 features covered (merge 5: 39; SVM_NODE_WIREFRAME now fits, group 10), 8 unfit
   (`validation/c-principled-superset/groups.txt`).
4. **Bench bundles:** `bundles/bench` (make_bench_bundle.py, verified against the committed manifest: passed) and
   `bundles/control` (textured-plane control, `--bundle-id gltf/textured-plane`, byte-identical to lane m1-asset's
   plane2 bundle); every file hashed in `bundles/SHA256SUMS.txt`.

## Final state
wave6.patch sha256 82a9940b26b5eb9a9e50e2a4d9aee0287b7f95ff2132ba2332e056e6986de45e (45 paths: 33 modified, 12 added),
identical to the first checkpoint's diff: no tracked file changed after the gates started, so every result is for the
final tree. `git apply --check -R` passes. No file under native/evidence changed.
