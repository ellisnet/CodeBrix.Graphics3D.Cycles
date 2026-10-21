#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""resolve.py FILE CHOICES...: resolve git merge-file conflict blocks in order.
Each choice: ours | theirs | both (ours then theirs) | tb (theirs then ours) | @path (replacement text file)."""
import sys
from pathlib import Path
p = Path(sys.argv[1]); choices = sys.argv[2:]; lines = p.read_text().splitlines(keepends=True)
out = []; i = 0; k = 0
while i < len(lines):
    if lines[i].startswith('<<<<<<< '):
        j = i + 1; ours = []; theirs = []
        while not lines[j].startswith('======='): ours.append(lines[j]); j += 1
        j += 1
        while not lines[j].startswith('>>>>>>> '): theirs.append(lines[j]); j += 1
        c = choices[k]; k += 1
        out += {'ours': ours, 'theirs': theirs, 'both': ours + theirs, 'tb': theirs + ours}.get(c) or Path(c[1:]).read_text().splitlines(keepends=True)
        i = j + 1
    else: out.append(lines[i]); i += 1
assert k == len(choices), (k, len(choices))
p.write_text(''.join(out)); print('resolved', k)
