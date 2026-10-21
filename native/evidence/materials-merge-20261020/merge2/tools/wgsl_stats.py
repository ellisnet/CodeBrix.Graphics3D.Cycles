#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Static WGSL size statistics per entry point (measurement only).

Reachable functions are found from a textual call graph. 'inlinedBytes' expands
every call site recursively (drivers inline all WGSL functions), approximating the
size of the single function a Vulkan compiler must optimise for that entry.
"""
import json, re, sys
from functools import lru_cache
from pathlib import Path

def strip_comments(s): return re.sub(r'//[^\n]*', '', s)

def parse(text):
    text = strip_comments(text)
    fns = {}; i = 0
    for m in re.finditer(r'(?:@compute[^\n]*?\s*)?fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(', text):
        name = m.group(1); start = m.start()
        b = text.index('{', m.end()); depth = 0; j = b
        while True:
            c = text[j]
            if c == '{': depth += 1
            elif c == '}':
                depth -= 1
                if depth == 0: break
            j += 1
        fns[name] = text[start:j+1]
    return fns, text

def stats(path, entries=None):
    fns, text = parse(Path(path).read_text())
    calls = {n: [c for c in re.findall(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\(', body[body.index('{'):]) if c in fns and c != n] for n, body in fns.items()}
    entry_names = re.findall(r'@compute\s*@workgroup_size\(([^)]*)\)\s*fn\s+([A-Za-z_][A-Za-z0-9_]*)', text)
    sizes = {n: wg for wg, n in entry_names}
    @lru_cache(None)
    def inlined(n, depth=0):
        if depth > 60: return 0
        return len(fns[n]) + sum(inlined(c, depth+1) for c in calls[n])
    out = {'module': str(path), 'bytes': len(Path(path).read_bytes()), 'functions': len(fns),
           'switchCases': len(re.findall(r'\bcase\b', text)), 'loops': len(re.findall(r'\b(?:for|loop|while)\b', text)), 'entries': {}}
    for wg, e in entry_names:
        if entries and e not in entries: continue
        seen = set(); stack = [e]
        while stack:
            n = stack.pop()
            if n in seen: continue
            seen.add(n); stack += calls[n]
        body = ''.join(fns[n] for n in seen)
        out['entries'][e] = {'workgroupSize': wg.strip(), 'reachableFunctions': len(seen), 'reachableBytes': len(body),
            'reachableSwitchCases': len(re.findall(r'\bcase\b', body)), 'reachableLoops': len(re.findall(r'\b(?:for|loop|while)\b', body)),
            'callSites': sum(len(calls[n]) for n in seen), 'inlinedBytes': inlined(e),
            'privateArrays': len(re.findall(r'array<[^>]*,\s*\d+u?>', body)),
            'topInlined': sorted(((inlined(n), n) for n in seen), reverse=True)[:12]}
    return out

if __name__ == '__main__':
    for p in sys.argv[1:]:
        print(json.dumps(stats(p), indent=1))
