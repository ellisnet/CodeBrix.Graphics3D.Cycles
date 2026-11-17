#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""After a recording run: keep the committed films and manifest entries of every existing Metal reference and add only
the entries recorded for the given new names, in the recorder's text format. keep_new_references.py <recorded dir> NAME..."""
import json, subprocess, sys
from pathlib import Path
F = Path('tests/CodeBrix.Graphics3D.Cycles.Tests/References/metal-osx-arm64')
recorded, names = Path(sys.argv[1]), sys.argv[2:]
head = lambda p: subprocess.run(['git', 'show', 'HEAD:' + p.as_posix()], capture_output=True, check=True).stdout
committed_text = head(F / 'manifest.json').decode()
recorded_text = (recorded / 'manifest.json').read_text()
committed = json.loads(committed_text)['references']; rec = json.loads(recorded_text)['references']
assert list(rec)[-len(names):] == names, 'the new names are not the last recorded entries'
for name, entry in committed.items():                      # restore every committed film
    (F / entry['file']).write_bytes(head(F / entry['file']))
start = recorded_text.index('    "%s": {' % names[0])
block = recorded_text[start:recorded_text.rindex('\n  }\n}\n') + 1]
tail = '    }\n  }\n}\n'
assert committed_text.endswith(tail)
merged = committed_text[:-len(tail)] + '    },\n' + block + '  }\n}\n'
m = json.loads(merged)['references']
assert list(m) == list(committed) + names and all(m[n] == committed[n] for n in committed) and all(m[n] == rec[n] for n in names)
(F / 'manifest.json').write_text(merged)
for n in names: assert (F / rec[n]['file']).read_bytes() == (recorded / rec[n]['file']).read_bytes()
print('kept', len(committed), 'committed entries and films; added', names)
