#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Checks of the closing build (read only): warnings by origin, the commit record, watch-points.
build_check.py > build-check.json"""
import collections
import json
import re
from pathlib import Path

REPO = Path('/Volumes/VideoWork/GitHome/CodeBrix.Graphics3D.Cycles')
B = REPO/'native/work/macos/osx-arm64'
WARN = re.compile(r'warning:')


def origin(line):
    s = line.replace(str(REPO)+'/', '<repo>/')
    if '<repo>/native/work/macos/osx-arm64/host/cycles-source/' in s or '/third_party/cycles/' in s:
        return 'pinned'
    if '<repo>/native/work/macos/osx-arm64/dependencies/' in s or '<repo>/third_party/deps/' in s:
        return 'dependency'
    if re.search(r'<repo>/(native/(host|tests|oracle|tools|cycles|src)|tools|src)/', s):
        return 'own'
    return 'other'


report = {}
lines = []
for log in [B/'host/build.log', B/'host/configure.log', REPO/'native/work/macos-arm64-closing-20261115/build.output.txt']:
    if log.is_file():
        for line in log.read_text(errors='replace').splitlines():
            if WARN.search(line):
                lines.append((log.relative_to(REPO).as_posix(), line.strip()))
by_origin = collections.defaultdict(list)
for log, line in lines:
    by_origin[origin(line)].append([log, line.replace(str(REPO)+'/', '<repo>/')])
report['hostAndConsoleWarnings'] = {k: dict(count=len(v), distinct=sorted(set(l for _, l in v))) for k, v in by_origin.items()}

dep = collections.Counter()
dep_lines = 0
for log in sorted((B/'dependencies').rglob('*.log')):
    for line in log.read_text(errors='replace').splitlines():
        if WARN.search(line):
            dep_lines += 1
            dep[re.sub(r'^.*?warning:', 'warning:', line.strip())] += 1
report['dependencyLogWarnings'] = dict(lines=dep_lines, distinct=len(dep), top=dep.most_common(40))

inv = json.loads((B/'host/invocation.json').read_text())
st = json.loads((B/'host/status.json').read_text())
report['commitRecord'] = dict(
    invocationSourceCommit=inv.get('sourceCommit'), invocationMetalSourceStatus=inv.get('metalSourceStatus'),
    invocationSourceDateEpoch=inv.get('sourceDateEpoch'),
    statusSourceAtEnd=st.get('sourceAtEnd'), statusPassed=st.get('passed'), statusComplete=st.get('complete'),
    statusPhase=st.get('phase'), exports=st.get('exports') if not isinstance(st.get('exports'), list) else len(st['exports']),
    kernelSourceFiles=st.get('kernelSourceFiles'))

db = json.loads((B/'host/build/compile_commands.json').read_text())
cmd = lambda r: r.get('command') or ' '.join(r.get('arguments', []))
report['watchPoints'] = dict(
    compileCommands=len(db),
    withDlss=sum('WITH_DLSS' in cmd(r) for r in db),
    withWebgpu=sum('WITH_WEBGPU' in cmd(r) for r in db),
    embreeCompileHasWithEmbree=[('-DWITH_EMBREE' in cmd(r)) for r in db if r['file'].endswith('src/bvh/embree.cpp')],
    halfHArmNeonIncludes=(B/'host/cycles-source/src/util/half.h').read_text().count('include <arm_neon.h>'))
ks = json.loads((B/'host/kernel-sources.json').read_text())
report['watchPoints']['overlaidInClosure'] = ks.get('overlaidInClosure')
staged = B/'host/build/lib/codebrix-cycles/source'
files = sorted(p for p in staged.rglob('*') if p.is_file())
differ = [str(p.relative_to(staged)) for p in files if (REPO/'third_party/cycles/src'/p.relative_to(staged)).read_bytes() != p.read_bytes()]
report['watchPoints'].update(stagedKernelFiles=len(files), stagedDifferFromPinned=differ,
                             tablesD65InClosure=(staged/'kernel/tables_d65_codebrix.h').is_file())
print(json.dumps(report, indent=1))
