import json, os, shutil, struct, subprocess, sys

SCRATCH = sys.argv[1]
VARIANTS = sys.argv[2].split(',')
BASE = os.path.join(SCRATCH, 'gltf', 'bundle')
TOOL = r'C:\GitHome\Cycles-arm64-test\tools\gltf\CycleGltfRender\bin\Release\net10.0\CodeBrix.Graphics3D.Cycles.GltfRender.dll'
W, H = 48, 32
PROBE = (20, 5)  # a ground pixel lit by the sun on the CPU, black on the GPU


def node(d, handle):
    return next(n for n in d['nodes'] if n.get('handle') == handle)


def drop(d, *handles):
    d['nodes'] = [n for n in d['nodes'] if n.get('handle') not in handles]


def edit(name, d):
    if name == 'pattern-default':
        node(d, 'integrator')['set'].pop('sampling_pattern')
    elif name == 'no-integrator':
        drop(d, 'integrator')
    elif name == 'no-sun':
        drop(d, 'light-shader', 'light-0', 'object-4')
    elif name == 'ground-only':
        drop(d, 'material-1', 'material-2', 'mesh-1', 'object-1', 'object-2', 'material-3', 'mesh-2', 'object-3')
    elif name == 'ground-only-no-sun':
        edit('ground-only', d); edit('no-sun', d)
    elif name == 'sobol-burley':
        node(d, 'integrator')['set']['sampling_pattern'] = 'sobol_burley'
    elif name == 'bounce-default':
        s = node(d, 'integrator')['set']
        for k in ('min_bounce', 'max_bounce', 'max_diffuse_bounce'): s.pop(k)
    elif name == 'gns-flat':
        edit('ground-only-no-sun', d)
        m = node(d, 'mesh-0'); m['mesh']['attributes'] = []; m['set'].pop('smooth')
    elif name == 'gns-smooth-noN':
        edit('ground-only-no-sun', d)
        m = node(d, 'mesh-0'); m['mesh']['attributes'] = [a for a in m['mesh']['attributes'] if a['name'] != 'N']
    elif name == 'gns-N-flat':
        edit('ground-only-no-sun', d)
        m = node(d, 'mesh-0'); m['set'].pop('smooth')
    elif name == 'gns-noUV':
        edit('ground-only-no-sun', d)
        m = node(d, 'mesh-0'); m['mesh']['attributes'] = [a for a in m['mesh']['attributes'] if a['name'] != 'uv']
    elif name == 'gns-smooth-term0':
        edit('ground-only-no-sun', d)
        node(d, 'object-0')['set'].update({'shadow_terminator_geometry_offset': 0.0, 'shadow_terminator_shading_offset': 0.0})
    elif name == 'gns-smooth-geo0':
        edit('ground-only-no-sun', d)
        node(d, 'object-0')['set'].update({'shadow_terminator_geometry_offset': 0.0})
    elif name == 'gns-smooth-emission':
        edit('ground-only-no-sun', d)
        node(d, 'material-0')['graph'] = {'nodes': [{'handle': 'e', 'type': 'emission', 'set': {'color': [0.6, 0.55, 0.5], 'strength': 1}}], 'connect': [['e', 'Emission', 'output', 'Surface']]}
    elif name == 'gns-smooth-glossy':
        edit('ground-only-no-sun', d)
        node(d, 'material-0')['graph'] = {'nodes': [{'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'color': [0.6, 0.55, 0.5], 'roughness': 0.0}}], 'connect': [['g', 'BSDF', 'output', 'Surface']]}
    elif name == 'base':
        pass
    else:
        raise SystemExit('unknown variant ' + name)


def px(path, x, y):
    b = open(path, 'rb').read()
    i = (y * W + x) * 16
    return struct.unpack('<4f', b[i:i + 16])


for v in VARIANTS:
    out = os.path.join(SCRATCH, 'gltf', 'v-' + v)
    if os.path.exists(out): shutil.rmtree(out)
    shutil.copytree(BASE, out)
    p = os.path.join(out, 'scene.json')
    d = json.load(open(p))
    edit(v, d)
    json.dump(d, open(p, 'w'), indent=1)
    r = subprocess.run(['dotnet', TOOL, '--bundle', p, '--output', os.path.join(out, 'film.bin'), '--compare'],
                       capture_output=True, text=True)
    lines = [l for l in (r.stdout + r.stderr).splitlines() if 'compare' in l or 'error' in l.lower() or 'faulted' in l]
    c = px(os.path.join(out, 'film.cpu.bin'), *PROBE) if os.path.exists(os.path.join(out, 'film.cpu.bin')) else None
    g = px(os.path.join(out, 'film.gpu.bin'), *PROBE) if os.path.exists(os.path.join(out, 'film.gpu.bin')) else None
    fmt = lambda t: 'n/a' if t is None else '(%.4f %.4f %.4f)' % t[:3]
    print('%-20s exit %d | probe cpu %s gpu %s | %s' % (v, r.returncode, fmt(c), fmt(g), ' / '.join(lines)), flush=True)
