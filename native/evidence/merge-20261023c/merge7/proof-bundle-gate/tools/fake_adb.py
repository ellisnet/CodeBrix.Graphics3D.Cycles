#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 7: a fake adb that replays one recorded bundle-mode app run (Linux proof of the app gate's verdict, no
device). Env FAKE_ADB_RUN = the recorded device folder (app-result.json + extracted/files), FAKE_ADB_STATE = a scratch dir."""
import hashlib, io, json, os, shutil, sys, tarfile
from pathlib import Path
run = Path(os.environ['FAKE_ADB_RUN']); st = Path(os.environ['FAKE_ADB_STATE']); st.mkdir(parents=True, exist_ok=True)
a = sys.argv[1:]
if a[0] == '-s': a = a[2:]
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
if a[0] == 'install': (st/'apk').write_text(a[-1]); sys.exit(0)
if a[0] == 'uninstall': (st/'apk').unlink(missing_ok=True); sys.exit(0)
if a[0] == 'push': (st/'pushed').mkdir(exist_ok=True); shutil.copy2(a[1], st/'pushed'/Path(a[2]).name); sys.exit(0)
if a[0] == 'exec-out':
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode='w') as t: t.add(run/'extracted'/'files', arcname='files')
    sys.stdout.buffer.write(buf.getvalue()); sys.exit(0)
if a[0] == 'shell':
    c = a[1:]
    if c[:2] == ['pm', 'path']:
        if (st/'apk').exists(): print('package:/data/app/fake/base.apk')
        sys.exit(0)
    if c[0] == 'sha256sum': print(sha((st/'apk').read_text()) + '  ' + c[1]); sys.exit(0)
    if c[0] == 'getprop': print(json.loads((run/'app-result.json').read_text()).get('apiLevel', 33)); sys.exit(0)
    if c[0] == 'run-as':
        c = c[2:]
        if c[0] == 'sha256sum': print(sha(st/'pushed'/Path(c[1]).name) + '  ' + c[1]); sys.exit(0)
        if c[:2] == ['cat', 'files/result.json']: sys.stdout.write((run/'app-result.json').read_text()); sys.exit(0)
        sys.exit(0)
    sys.exit(0)
sys.exit(0)
