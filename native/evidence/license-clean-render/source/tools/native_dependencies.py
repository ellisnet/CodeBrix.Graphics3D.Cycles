# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Locate only this checkout's source-built native dependency prefix."""
import hashlib
import json
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/licensing'))
from dependency_sources import verify


def prefix():
    verify()
    installed=ROOT/'native/work/dependencies/install'
    marker=installed/'codebrix-source-build.json'
    if not marker.exists():
        raise RuntimeError('Build the vendored dependencies first: python3 tools/dependencies/build.py')
    report=json.loads(marker.read_text())
    expected=hashlib.sha256((ROOT/'third_party/dependency-imports.json').read_bytes()).hexdigest()
    if report['sourceManifestSha256']!=expected:
        raise RuntimeError('Dependency prefix is stale; rebuild from the current verified snapshots')
    if report['recipeSha256']!=hashlib.sha256((ROOT/'tools/dependencies/build.py').read_bytes()).hexdigest():
        raise RuntimeError('Dependency build recipe changed; rebuild the local prefix')
    for name,digest in report['libraries'].items():
        if hashlib.sha256((installed/name).read_bytes()).hexdigest()!=digest:
            raise RuntimeError('Source-built dependency cache changed: '+name)
    return installed
