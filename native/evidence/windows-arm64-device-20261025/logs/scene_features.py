import json, os, sys

run, scenes = sys.argv[1], sys.argv[2]
cp = json.load(open(run))['comparisonsPassed']


def walk(o, out):
    if isinstance(o, dict):
        s = o.get('set') if isinstance(o.get('set'), dict) else {}
        sm = s.get('smooth')
        if isinstance(sm, list):
            out['smooth_faces'] += sum(1 for x in sm if x)
        elif sm is True:
            out['smooth_faces'] += 1
        mesh = o.get('mesh')
        if isinstance(mesh, dict):
            for a in mesh.get('attributes', []):
                out['attrs'].add(a.get('name'))
            if mesh.get('motion') or mesh.get('motionPositionsFile') or 'motion' in json.dumps(mesh)[:2000]:
                out['mesh_motion'] = True
        g = o.get('graph')
        if isinstance(g, dict):
            for n in g.get('nodes', []):
                out['nodes'].add(n.get('type'))
        for k, v in s.items():
            if k in ('motion_blur',) and v: out['motion_blur'] = True
        for v in o.values():
            walk(v, out)
    elif isinstance(o, list):
        for v in o:
            walk(v, out)


for name in sorted(cp):
    p = os.path.join(scenes, name + '.json')
    if not os.path.exists(p):
        continue
    out = {'smooth_faces': 0, 'attrs': set(), 'nodes': set(), 'motion_blur': False, 'mesh_motion': False}
    walk(json.load(open(p)), out)
    verdict = 'pass' if cp[name] is True else ('FAIL' if cp[name] is False else 'n/a ')
    print('%s %-34s smooth %4d mblur %-5s attrs %-28s nodes %s' % (
        verdict, name, out['smooth_faces'], out['motion_blur'], ','.join(sorted(x for x in out['attrs'] if x)),
        ','.join(sorted(x for x in out['nodes'] if x))[:90]))
