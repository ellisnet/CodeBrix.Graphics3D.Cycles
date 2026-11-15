#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Kit rule 9, Part E closing check (docs/KIT-MACOS-ARM64.md sections 2 and 8).

rule9_closing.py CLOSING BASE [PREVIOUS] > rule9.json

CLOSING  the closing oracle run directory (run.json, oracle/<scene>/<backend>/meta.json and films)
BASE     the base run of rule 9 (native/evidence/cycles-oracles-macos-20261113)
PREVIOUS optional: an earlier CPU-only corpus run, compared for information only

For every scene of the base run, every cpu-arm64-macos film is compared by the filmSha256 list of meta.json, index by
index, with the closing run; the closing films' bytes are also hashed against their own meta.json, and the base
films' bytes against theirs. Passes (meta.json passes[].filmSha256) are compared the same way. The closing run's CPU
films must all be repeat-identical. Metal repeat statistics and the pairwise practical counts of comparisons/*.json
are reported as information only."""
import collections
import hashlib
import json
import sys
from pathlib import Path

CPU = 'cpu-arm64-macos'
METAL = ['metal-m2pro', 'metal-m2pro-metalrt']
REMOVED_BY_RULING_34 = 'lamps/area-degenerate-infinite-axis'
# A wall-clock measurement: differs between any two renders by design (kit report section 3, item 5). Reported apart.
WALL_CLOCK_PASSES = {'RenderTime'}
REPO = Path(__file__).resolve().parents[3]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def meta(run, scene, backend):
    path = run/'oracle'/scene/backend/'meta.json'
    return json.loads(path.read_text()) if path.is_file() else None


def bytes_check(run, scene, backend, m):
    """Film and pass files of one backend directory against their own meta.json hashes."""
    folder, bad = run/'oracle'/scene/backend, []
    for i, digest in enumerate(m['filmSha256']):
        if sha(folder/f'film-{i}.f32') != digest:
            bad.append(f'film-{i}.f32')
    for p in m.get('passes') or []:
        if not p.get('written'):
            continue
        for i, digest in enumerate(p['filmSha256']):
            if sha(folder/f"pass-{p['index']}-{i}.f32") != digest:
                bad.append(f"pass-{p['index']}-{i}.f32")
    return bad


def main():
    closing, base = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    previous = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else None
    run = json.loads((closing/'run.json').read_text())
    base_run = json.loads((base/'run.json').read_text())
    closing_scenes, base_scenes = set(run['scenes']), set(base_run['scenes'])

    films = dict(compared=0, identical=0, moved=[], indexMismatch=[])
    passes = dict(compared=0, identical=0, moved=[], indexMismatch=[], wallClockPassesNotCompared=[])
    missing, base_bytes_bad, closing_bytes_bad = [], [], []
    for scene in sorted(base_scenes):
        old, new = meta(base, scene, CPU), meta(closing, scene, CPU)
        if old is None:
            missing.append([scene, 'no cpu-arm64-macos meta.json in the base run'])
            continue
        if new is None:
            missing.append([scene, 'no cpu-arm64-macos film in the closing run'])
            continue
        bad = bytes_check(base, scene, CPU, old)
        if bad:
            base_bytes_bad.append([scene, bad])
        if len(old['filmSha256']) != len(new['filmSha256']):
            films['indexMismatch'].append([scene, len(old['filmSha256']), len(new['filmSha256'])])
        for i, (a, b) in enumerate(zip(old['filmSha256'], new['filmSha256'])):
            films['compared'] += 1
            if a == b:
                films['identical'] += 1
            else:
                films['moved'].append([scene, i, a, b])
        old_p = {p['index']: p for p in old.get('passes') or [] if p.get('written')}
        new_p = {p['index']: p for p in new.get('passes') or [] if p.get('written')}
        if set(old_p) != set(new_p):
            passes['indexMismatch'].append([scene, sorted(old_p), sorted(new_p)])
        for index in sorted(set(old_p) & set(new_p)):
            if old_p[index]['name'] != new_p[index]['name']:
                passes['indexMismatch'].append([scene, index, old_p[index]['name'], new_p[index]['name']])
            if old_p[index]['name'] in WALL_CLOCK_PASSES:
                passes['wallClockPassesNotCompared'].append([scene, index, old_p[index]['name']])
                continue
            for i, (a, b) in enumerate(zip(old_p[index]['filmSha256'], new_p[index]['filmSha256'])):
                passes['compared'] += 1
                if a == b:
                    passes['identical'] += 1
                else:
                    passes['moved'].append([scene, index, old_p[index]['name'], i, a, b])

    # Every closing CPU film: bytes against meta.json, and repeat identity.
    repeat_not_identical, wall_clock_repeat, cpu_film_count = [], [], 0
    for scene in sorted(closing_scenes):
        m = meta(closing, scene, CPU)
        if m is None:
            continue
        cpu_film_count += len(m['filmSha256'])
        bad = bytes_check(closing, scene, CPU, m)
        if bad:
            closing_bytes_bad.append([scene, bad])
        differing = [p['name'] for p in m.get('passes') or [] if p.get('written') and len(set(p['filmSha256'])) != 1]
        if differing and set(differing) <= WALL_CLOCK_PASSES:
            wall_clock_repeat.append([scene, differing])
        differing = [n for n in differing if n not in WALL_CLOCK_PASSES]
        if not (m['repeatBitwiseIdentical'] and len(set(m['filmSha256'])) == 1 and m['repeatMaxAbsDifference'] == 0 and not differing):
            repeat_not_identical.append([scene, m['repeatBitwiseIdentical'], m['repeatMaxAbsDifference'], differing])

    new_scenes = sorted(closing_scenes - base_scenes)
    families = collections.Counter(s.split('/')[0] for s in new_scenes)

    per_backend = {}
    for backend in [CPU] + METAL:
        rendered = [s for s in closing_scenes if backend in run['scenes'][s]]
        entry = dict(scenesRendered=len(rendered), films=0)
        maxima, identical = [], 0
        warnings = collections.Counter()
        for s in rendered:
            m = meta(closing, s, backend)
            entry['films'] += len(m['filmSha256'])
            identical += bool(m['repeatBitwiseIdentical'])
            maxima.append((m['repeatMaxAbsDifference'], s))
            for w in m.get('warnings') or []:
                warnings[w] += 1
        maxima.sort(reverse=True)
        entry.update(repeatBitwiseIdentical=identical, repeatNotIdentical=len(rendered)-identical,
                     repeatMaxAbsDifference=maxima[0] if maxima else None, top10=maxima[:10],
                     repeatMaxAbsHistogram={label: sum(1 for v, _ in maxima if lo <= v < hi) for label, lo, hi in
                                            [('0', 0, 1e-300), ('<1e-7', 1e-300, 1e-7), ('1e-7..1e-6', 1e-7, 1e-6),
                                             ('1e-6..1e-5', 1e-6, 1e-5), ('1e-5..1e-4', 1e-5, 1e-4), ('1e-4..1e-3', 1e-4, 1e-3),
                                             ('1e-3..1e-2', 1e-3, 1e-2), ('>=1e-2', 1e-2, float('inf'))]},
                     warnings=dict(warnings))
        per_backend[backend] = entry

    pairs = collections.defaultdict(lambda: dict(pairs=0, practicalFailing=0, scenesFailing=set(), worst=(0.0, None)))
    for report in sorted((closing/'comparisons').glob('*.json')):
        c = json.loads(report.read_text())
        for p in c['pairs']:
            key = f"{p['reference']} vs {p['candidate']}"
            e = pairs[key]
            e['pairs'] += 1
            if p['practicalFailures']:
                e['practicalFailing'] += 1
                e['scenesFailing'].add(c['sceneId'])
            if p['maxAbsoluteDifference'] > e['worst'][0]:
                e['worst'] = (p['maxAbsoluteDifference'], c['sceneId'])
    pairs = {k: dict(pairs=v['pairs'], practicalFailing=v['practicalFailing'], scenesFailing=len(v['scenesFailing']),
                     worstMaxAbs=v['worst']) for k, v in sorted(pairs.items())}
    comparisons = run.get('comparisonsPassed') or {}

    report = dict(
        closingRun=str(closing.relative_to(REPO)), baseRun=str(base.relative_to(REPO)),
        closingScenes=len(closing_scenes), baseScenes=len(base_scenes),
        runFailures=run.get('failures') or {}, kernelCompileSeconds=run.get('kernelCompileSeconds'),
        comparisonsPassed=dict(true=sum(1 for v in comparisons.values() if v is True),
                               false=sorted(k for k, v in comparisons.items() if v is False),
                               none=sorted(k for k, v in comparisons.items() if v is None)),
        rule9=dict(films=films, passes=passes, baseScenesMissingFromClosing=missing,
                   baseFilmBytesNotMatchingMeta=base_bytes_bad,
                   verdict='PASS' if not (films['moved'] or films['indexMismatch'] or missing or passes['moved']
                                          or passes['indexMismatch'] or base_bytes_bad or closing_bytes_bad) else 'FINDING'),
        ruling34=dict(scene=REMOVED_BY_RULING_34, inBaseRun=REMOVED_BY_RULING_34 in base_scenes,
                      inClosingRun=REMOVED_BY_RULING_34 in closing_scenes,
                      committedDocument=(REPO/'native/oracle/scenes'/(REMOVED_BY_RULING_34+'.json')).is_file()),
        newScenes=dict(count=len(new_scenes), byFamily=dict(sorted(families.items())), scenes=new_scenes),
        closingCpu=dict(films=cpu_film_count, filmBytesNotMatchingMeta=closing_bytes_bad,
                        repeatNotIdentical=repeat_not_identical, wallClockPassesDifferingBetweenRepeats=wall_clock_repeat),
        backends=per_backend, pairs=pairs)

    if previous:
        prev = json.loads((previous/'run.json').read_text())
        compared = identical = 0
        moved = []
        for scene in sorted(set(prev['scenes']) & closing_scenes):
            a, b = meta(previous, scene, CPU), meta(closing, scene, CPU)
            if a is None or b is None:
                continue
            for i, (x, y) in enumerate(zip(a['filmSha256'], b['filmSha256'])):
                compared += 1
                identical += x == y
                if x != y:
                    moved.append([scene, i])
        report['previousCorpusInformation'] = dict(
            run=str(previous.relative_to(REPO)), scenes=len(prev['scenes']), compared=compared, identical=identical,
            moved=moved, onlyInPrevious=sorted(set(prev['scenes']) - closing_scenes),
            onlyInClosing=sorted(closing_scenes - set(prev['scenes'])))
    print(json.dumps(report, indent=1))


if __name__ == '__main__':
    main()
