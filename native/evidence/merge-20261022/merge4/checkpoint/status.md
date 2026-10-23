# Merge wave 4 status (18:10:39)
- MERGED in main (uncommitted): L, S, P (+ lane Q DEVICE.md section by hand). Docs applied. Notices identical; audit PASS.
- Linux: all gates exit 0 (see ../validation/*/exit.json); envelope l3 PASS (worst 52,509); films glass db8e84f9,
  Principled fd3a7f2b, node render b9d1da3f.
- Samsung: expected failures sampler 3133, principled_render stage 40, nodes_render 6720; NEW: nodes_svm compiled
  graph 44 with Gabor in the module (diagnosed, not fixed; ../diag/svm309). Everything else PASS.
- HP: every job PASS, no exception needed. Pixel: 5 provisional materials jobs not supported (AGENT-README settled).
  S20: rerun recorded (principled_svm passes; others fail).
- All locks free, devices clean.
- Next: freeze native/evidence/merge-20261022 (tools/freeze_checkpoint.py), audit again (exceptions if needed), delete
  scratch with hashes, post-merge.json + baseline-merge-2026-10-22.json, report.
