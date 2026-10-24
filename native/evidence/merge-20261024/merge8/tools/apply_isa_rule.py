#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Coordinator decision 09:05 (C1 scope): candidates gate against the CPU gold standard of their own host ISA family.
Installs compare-isa.py as native/oracle/compare.py and updates the oracle README, MAINTAINER-README and DEVICE.md item 25."""
import shutil
from pathlib import Path
R = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles')
shutil.copy(R/'native/work/merge8/compare-isa.py', R/'native/oracle/compare.py')
def edit(p, a, b):
    s = (R/p).read_text(); assert s.count(a) == 1, (p, a[:60]); (R/p).write_text(s.replace(a, b))
edit('native/oracle/compare.py', '''- gating: a candidate against a CPU gold standard (the CPU film is the reference b);
- reported: a candidate against a Metal gold standard, the two gold standards against each
  other, and two candidates against each other (same numbers, never a failure).''', '''- gating: a candidate against the CPU gold standard of its own host ISA family (the CPU film is
  the reference b): wgsl-linux-intel and every wgsl-android-* backend (validated through the
  Linux x64 glibc flow) against cpu-x64-*, an arm64-host candidate against cpu-arm64-*; when no
  same-ISA CPU film is present, against the CPU film that is (the report says so);
- reported: every cross-ISA pair, a candidate against a Metal gold standard, the gold standards
  against each other, and two candidates against each other (same numbers, never a failure).''')
edit('native/oracle/README.md', '''renderers; every other backend (`wgsl-*`) is a candidate. A candidate is judged
against the CPU gold standard: each candidate-versus-CPU pair is `gating`, with
the CPU film as the reference `b`, and the practical criterion''', '''renderers; every other backend (`wgsl-*`) is a candidate. A candidate is judged
against the CPU gold standard of its own host ISA family (`wgsl-linux-intel` and
every `wgsl-android-*` backend, validated through the Linux x64 glibc flow,
against `cpu-x64-linux`; an arm64-host candidate against `cpu-arm64-*`; when no
same-ISA CPU film is present, against the CPU film that is, and the report says
so): that pair is `gating`, with the CPU film as the reference `b`, and the practical criterion''')
edit('native/oracle/README.md', '''differences (`1e-7 + 1e-7 * abs(b)`) are reported. Candidate-versus-Metal pairs,
the pair of gold standards and candidate-versus-candidate pairs are `reported`''', '''differences (`1e-7 + 1e-7 * abs(b)`) are reported. Cross-ISA pairs (a CPU x64
film against a CPU arm64 film, an x64 candidate against a CPU arm64 film),
candidate-versus-Metal pairs, the pairs of gold standards and candidate-versus-candidate pairs are `reported`''')
edit('MAINTAINER-README.txt', '''candidates. compare.py judges every candidate against the CPU gold standard
(gating pairs: practical criterion required, strict recorded) and reports the
candidate-versus-Metal and CPU-versus-Metal pairs without judging them; Metal''', '''candidates. compare.py judges every candidate against the CPU gold standard of
its own host ISA family (x64 for the Linux and Android WGSL backends; gating
pairs: practical criterion required, strict recorded) and reports the
cross-ISA, candidate-versus-Metal and CPU-versus-Metal pairs without judging them; Metal''')
edit('native/DEVICE.md', '''   arm64 CPU film while perfect against the Linux one; the four starter scenes are perfect against both.''', '''   arm64 CPU film while perfect against the Linux one; the four starter scenes are perfect against both. Resolved in
   the comparator (merge wave 8): a candidate gates against the CPU film of its own host ISA family (x64 for the Linux
   and Android WGSL backends) and cross-ISA pairs are reported, so the bench and HDRI bench read "perfect" with the
   Mac films present; the cross-ISA CPU difference itself stays recorded here (the macOS arm64 recipe is untested at
   the fixed baseline on this side, item 16).''')
print('applied')
