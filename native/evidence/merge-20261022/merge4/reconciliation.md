# Merge wave 4 reconciliation (2026-10-22)

Base: main clean at 3392df3d (manifest check against baseline-session-close.json: 72,265 files, 0 missing/extra/changed;
`pre-merge.json` written). Order: L, then S, then P. Every patch verified by sha256 first.

## Lane L (changes.patch 4962eacc84e2051253a174806424f45234d2fa9c817b0462f7c825cff3dd01c9, 37 files)
- `git apply --check` then `git apply` on the clean tree. All 37 after-hashes from changes.json match.

## Lane S (changes.patch 0c2231c4519390eb7e1f4aba22c3c7bf3887ba699c46c619c19e457d7e94fb03, 14 files)
- 7 single-owner files by `git apply --include=...` (check first): emitter_sample_gate.cpp, render_gate.cpp,
  surface_svm_test.h, wavefront_background.wgsl, wavefront_light_eval.wgsl, wavefront_shadow.wgsl,
  wavefront_volume.wgsl. All 7 equal lane S's after-hashes.
- 7 overlap files merged three-way with `git merge-file` (ours = main after L, base = 3392df3d, theirs = lane S
  worktree file, which equals S's after-hash). Working copies in `merge-s/`.
  - device.h, svm_surface_gate.cpp, wavefront_svm.wgsl, wavefront_svm_nodes.wgsl, DEVICE.md: clean.
    - wavefront_svm.wgsl: S's arena-slot stack accessors + L's Gabor/superset changes; no direct `svm_stack[` indexing
      remains anywhere in native/shaders.
    - wavefront_svm_nodes.wgsl: L's Gabor body + S's `svmn_order` fade barrier, both kept.
  - device.cpp: 1 conflict (constructor). Resolution keeps BOTH: S's 16-byte error word, stack-region allocation and
    header upload (`error_word+4` = region address, slot count) AND L's `specialization_mode_from_environment(&superset_group)`
    (superset / =0 diagnostic modes). S's plain `getenv(...)=="0"` line is dropped because L's mode parser covers it.
  - wavefront_surface.wgsl: 1 conflict (light stage). L split the light stage into `surface_light_inputs`,
    `shade_surface_light` and the new `shade_surface_light_triangle`; S had inserted `svm_invocation(id.x)` as the
    first statement of the old `shade_surface_light`. Resolution: L's structure, with `svm_invocation(id.x);` added
    as the first statement of BOTH L entries (`shade_surface_light` and `shade_surface_light_triangle`), following
    S's rule that every entry point of an SVM-capable module names its invocation first.
- Feature bits: lane S uses none (grep of the patch: no `1ull<<6x`); lane L uses 63. Bit 62 remains free and unused.

## Lane P (changes.patch 47602c7698976bc21d2f676225c2d8226589f89538da6340fb9129481c72a023, docs only)
- AGENT-README.txt: three-way (main after L+S, base, P): clean (L's Gabor lines + P's new Adreno 640 section).
- native/DEVICE.md: three-way: 2 conflicts, both in the open-items list (items 1-2 and 8-10). Interim resolution:
  lane S's updated items 1, 8, 9 and lane P's "closed / partly closed" leads for items 2 and 10. The list is then
  consolidated by hand (Jeremy's doc edit (e)); see the docs section at the end of this file.

## Lane Q (changes.patch 3aeba5d1f2de40c027467bbc2ab0a90af7494829b2610d5f57a5f1f1a9c80ce9, DEVICE.md only)
- Not applied mechanically (DEVICE.md moved under it, as the coordinator expected). Its single hunk is a pure addition
  at the end of DEVICE.md; the added text (134 lines, section "Samsung Galaxy S20 FE 5G (Adreno 650) sweep (lane Q,
  2026-10-22)") was extracted verbatim from the patch (`merge-q-section.md`) and appended at the end of DEVICE.md,
  followed by a new subsection "S20 rerun on the merged tree (merge wave 4)". The consolidated open-items list points
  to it (S20 failures are not open items of the reference devices).

## Docs (applied after every device run, because native/DEVICE.md and docs/PARITY.md are pinned production sources)
- AGENT-README.txt: (a) opening sentence of the Adreno 640 section; Provisional subsection settled (Principled ->
  "more than three times", surface nodes -> "fails"; subsection removed); Gabor consumer note: known issue on the
  Adreno 730 when combined with Fresnel/Layer Weight (merge-wave-4 finding).
- native/DEVICE.md (tools/apply_device_docs.py): (c) tier row; (b) 5-second floor; (d) materials settled + decided
  dispositions (replaces "Rerun these at merge"); lane P intro sentence on the SVM stack updated; new section
  "Merge wave 4: lanes L, S and P on the devices"; (e) open items rewritten as one list (8 open, resolved list);
  lane Q section + S20 rerun appended.
- docs/PARITY.md: Material row (Samsung/HP/Pixel state, Gabor on the HP and the Adreno 730 caveat), Lighting row
  (Pixel motion closed under the policy), Memory row (arena stack size). Lane L's Gabor/superset/light-split rows kept.
- Notices regenerated: byte-identical (provenance unchanged; Gabor was already covered).

## Runner changes (work tools only, native/work/merge4/tools)
- run_corpus.py (copy of lane S's): nodes_svm 67 graphs, nodes_world 24 worlds; new job glass_render-superset-refused;
  s20 device entry; refusal jobs skip the device-scene dump pass (the first Samsung spec-refusal run failed on that,
  rerun as spec-refusal-m4b: PASS).
