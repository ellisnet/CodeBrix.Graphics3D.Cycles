#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Refresh stale license-review exception hashes only when the restricted-license
wording (tools/licensing/audit.py KEYWORDS matches and SPDX tags) of the file is
unchanged relative to the version the exception was recorded for. Any wording
change is reported and the entry is left stale for human review."""
import argparse, hashlib, json, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT/'tools/licensing'))
import audit  # noqa: E402

def restricted_tag(tag):
    import re
    terms = set(re.findall(r'[A-Za-z0-9][A-Za-z0-9.+-]*', tag)) - {'AND', 'OR', 'WITH', 'and', 'or'}
    return not terms <= audit.ALLOWED

def wording(text):
    # Restricted keyword matches plus SPDX expressions outside the allowed set
    # (the two conditions audit.py requires an exact review for).
    return sorted(m.group(0) for m in audit.KEYWORDS.finditer(text)), sorted(t for t in audit.TAG.findall(text) if restricted_tag(t))

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--reference', required=True, help='git revision holding the reviewed versions')
    ap.add_argument('--reference-dir', type=Path, help='directory tree holding reviewed versions (checked first)')
    ap.add_argument('--report', type=Path, required=True); a = ap.parse_args()
    path = ROOT/audit.EXCEPTIONS; data = json.loads(path.read_text()); report = {'reference': a.reference, 'refreshed': {}, 'refused': {}}
    for name, entry in data['files'].items():
        f = ROOT/name
        if not f.is_file(): continue
        digest = hashlib.sha256(f.read_bytes()).hexdigest()
        if digest == entry['sha256']: continue
        old = (a.reference_dir/name).read_bytes() if a.reference_dir and (a.reference_dir/name).is_file() and hashlib.sha256((a.reference_dir/name).read_bytes()).hexdigest() == entry['sha256'] else subprocess.run(['git', 'show', a.reference+':'+name], cwd=ROOT, capture_output=True).stdout
        if hashlib.sha256(old).hexdigest() != entry['sha256']:
            report['refused'][name] = 'reference revision does not hold the reviewed version'; continue
        if wording(old.decode('utf-8')) != wording(f.read_text()):
            report['refused'][name] = 'restricted-license wording changed'; continue
        report['refreshed'][name] = {'from': entry['sha256'], 'to': digest, 'keywordMatches': len(wording(f.read_text())[0])}
        entry['sha256'] = digest
    path.write_text(json.dumps(data, indent=2)+'\n')
    a.report.write_text(json.dumps(report, indent=2)+'\n'); print(json.dumps(report, indent=2))
    sys.exit(1 if report['refused'] else 0)

if __name__ == '__main__': main()
