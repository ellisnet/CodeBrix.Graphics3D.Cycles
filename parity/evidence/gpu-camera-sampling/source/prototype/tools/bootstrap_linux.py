#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Extract missing Debian development dependencies locally; do not install packages."""
import json
import re
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]/'work/deps'
debs = root/'debs'
debs.mkdir(parents=True, exist_ok=True)
packages = ['libopenimageio-dev', 'libboost-dev', 'libboost-iostreams-dev',
            'libboost-filesystem-dev', 'libboost-system-dev', 'libboost-thread-dev',
            'libboost-regex-dev', 'libpugixml-dev', 'libopencolorio-dev']
simulation = subprocess.check_output(['apt-get', '-s', 'install', '--no-install-recommends', *packages], text=True)
missing = re.findall(r'^Inst (\S+)', simulation, re.M)
(root/'resolution.txt').write_text(simulation)
if missing: subprocess.run(['apt-get','download',*missing],cwd=debs,check=True)
for archive in sorted(debs.glob('*.deb')):
    subprocess.run(['dpkg-deb','-x',str(archive),str(root)],check=True)
for link in (root/'usr/lib/x86_64-linux-gnu').glob('*.so'):
    if link.is_symlink() and not link.exists():
        target = link.parent/link.readlink()
        system = Path('/')/target.relative_to(root)
        if system.exists(): target.symlink_to(system)
(root/'packages.json').write_text(json.dumps(missing,indent=2)+'\n')
print('Local dependency prefix:',root/'usr')
