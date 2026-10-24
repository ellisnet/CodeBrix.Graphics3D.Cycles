#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Compare every film/image file (by relative path, sha256) between a merge-4 run folder and a merge-5 run folder.
usage: film_compare.py OLD_DIR NEW_DIR OUT.json"""
import hashlib, json, sys
from pathlib import Path
EXT = ('.film.bin', '.ppm', '.pfm', '.exr', '.png', '.f32')
def films(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.rglob('*'))
            if p.is_file() and p.name.endswith(EXT) and 'shader-cache' not in p.parts}
old, new = films(Path(sys.argv[1])), films(Path(sys.argv[2]))
same = sorted(k for k in old if new.get(k) == old[k]); diff = sorted(k for k in old if k in new and new[k] != old[k])
r = dict(old=str(sys.argv[1]), new=str(sys.argv[2]), compared=len(set(old) & set(new)), identical=len(same), different=diff,
         onlyOld=sorted(set(old) - set(new)), onlyNew=sorted(set(new) - set(old)))
Path(sys.argv[3]).write_text(json.dumps(r, indent=1) + '\n')
print(json.dumps({k: (v if not isinstance(v, list) else len(v)) for k, v in r.items()}))
