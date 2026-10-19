#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Fail closed on unreviewed source licenses and changed notice exceptions."""
import argparse
import gzip
import hashlib
import io
import json
import re
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from cycles_source import verify as verify_cycles
from dependency_sources import verify as verify_dependencies
from notices import generate as verify_notices

ALLOWED={'Apache-2.0','MIT','BSD-2-Clause','BSD-3-Clause','ISC','Zlib','BSL-1.0',
         'libpng-2.0','Libpng','IJG','libtiff','FTL','CC0-1.0','LLVM-exception',
         'NCSA','Unlicense','MIT-CMU','BSD-2-Clause-Views','HPND-Pbmplus',
         'FSFULLRWD','fmt-exception'}
KEYWORDS=re.compile(r'\b(?:A?GPL|LGPL|SSPL)(?:[-. ]?[0-9]|\b)|General Public License|'
                    r'Mozilla Public License|\b(?:MPL|CDDL|EPL)-[0-9]|CC-BY-(?:NC|ND|SA)|'
                    r'JSON License',re.I)
TAG=re.compile(r'^\s*(?://|#|/\*|\*)?\s*SPDX-License-Identifier:\s*(.+?)(?:\*/)?$',re.M)
CODE={'.py','.cpp','.c','.h','.hpp','.cxx','.cc','.cs','.wgsl','.cmake','.sh','.rs'}
EXCEPTIONS='third_party/license-review-exceptions.json'
COMPILED_MAGIC=(b'\x7fELF',b'!<arch>\n',b'MZ',b'\x00asm',
                b'\xfe\xed\xfa\xce',b'\xce\xfa\xed\xfe',
                b'\xfe\xed\xfa\xcf',b'\xcf\xfa\xed\xfe',
                b'\xca\xfe\xba\xbe',b'\xbe\xba\xfe\xca',
                b'\xca\xfe\xba\xbf',b'\xbf\xba\xfe\xca')


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def candidates():
    names=subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=ROOT,text=True)
    return sorted(set(n for n in names.split('\0') if n and (ROOT/n).exists()))


def source_audit():
    verify_cycles();verify_dependencies();verify_notices(check=True)
    reviewed=json.loads((ROOT/EXCEPTIONS).read_text())['files']
    errors=[];rows=[];used=[]
    for name in candidates():
        p=ROOT/name
        if p.is_symlink():errors.append('Source candidate symlink: '+name);continue
        data=p.read_bytes();digest=hashlib.sha256(data).hexdigest()
        rows.append(dict(path=name,sha256=digest,bytes=len(data)))
        magic=data[:8]
        if data.startswith(b'\x1f\x8b'):
            with gzip.GzipFile(fileobj=io.BytesIO(data)) as stream:magic=stream.read(8)
        if magic.startswith(COMPILED_MAGIC):
            errors.append('Compiled artifact in source publication candidate: '+name)
        if name==EXCEPTIONS:continue  # Structured policy data, never implementation.
        try:text=data.decode('utf-8')
        except UnicodeDecodeError:continue
        exception=reviewed.get(name)
        exact=bool(exception and exception['sha256']==digest and exception.get('reason'))
        if KEYWORDS.search(text):
            if exact:used.append(name)
            else:errors.append('Unreviewed restricted-license wording: '+name)
        tags=TAG.findall(text)
        for tag in tags:
            terms=set(re.findall(r'[A-Za-z0-9][A-Za-z0-9.+-]*',tag)) - {'AND','OR','WITH','and','or'}
            if not terms<=ALLOWED and not exact:
                errors.append('Unreviewed SPDX expression: '+name+' '+tag)
        active='evidence' not in p.relative_to(ROOT).parts and not name.startswith('third_party/')
        if active and (p.suffix in CODE or p.name=='CMakeLists.txt'):
            if not tags:errors.append('Missing source SPDX: '+name)
            if 'Copyright (c) 2026 Jeremy Ellis and contributors' not in text:
                errors.append('Missing original contribution copyright: '+name)
    # No exception silently changes with a file edit. Obsolete entries are also
    # errors, so an exclusion cannot conceal a newly repurposed file.
    current={r['path']:r['sha256'] for r in rows}
    for name,entry in reviewed.items():
        if current.get(name)!=entry['sha256']:errors.append('Stale reviewed exception: '+name)
    history=subprocess.check_output(['git','log','--all','--format=','--name-only'],cwd=ROOT,text=True)
    return dict(passed=not errors,errors=errors,sourceFiles=rows,
                reviewedExceptionFiles=used,historyPaths=sorted(set(history.split())),
                scope='Current proposed source contents, verified vendor snapshots and generated notices. Binary and package audits are separate.')


def package_audit(package,manifest_path):
    """Inspect an actual candidate against a separately reviewed exact payload.

    The expected manifest must be created/reviewed by the release process;
    this tool deliberately cannot approve files by discovering them in a ZIP.
    """
    expected=json.loads(manifest_path.read_text())
    if expected.get('approvedForPackaging') is not True:
        raise RuntimeError('Payload manifest is not release-reviewed')
    with zipfile.ZipFile(package) as archive:
        files=[n for n in archive.namelist() if not n.endswith('/')]
        if len(files)!=len(set(files)):raise RuntimeError('Duplicate package paths')
        actual={n:hashlib.sha256(archive.read(n)).hexdigest() for n in files}
        if actual!=expected['files']:raise RuntimeError('Package differs from reviewed exact payload')
        for name in files:
            if '..' in Path(name).parts or Path(name).is_absolute():raise RuntimeError('Unsafe package path')
        for required in ['LICENSE','NOTICE','THIRD-PARTY-NOTICES.txt']:
            if required not in actual:raise RuntimeError('Missing package notice: '+required)
        if any('third_party/atomic/' in n or n.endswith('dependency_targets.cmake') for n in files):
            raise RuntimeError('Excluded implementation in package')
    return dict(passed=True,packageSha256=sha(package),payloadManifestSha256=sha(manifest_path),files=actual)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path)
    parser.add_argument('--package',type=Path);parser.add_argument('--payload-manifest',type=Path)
    args=parser.parse_args()
    if args.package:
        if not args.payload_manifest:parser.error('--payload-manifest is required for a package audit')
        report=package_audit(args.package,args.payload_manifest)
    else:report=source_audit()
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ['sourceFiles','files','reviewedExceptionFiles']},indent=2))
    sys.exit(0 if report['passed'] else 1)
