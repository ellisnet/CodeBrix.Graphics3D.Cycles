import math, os, struct, sys

win, mac = sys.argv[1], sys.argv[2]


def load(p):
    b = open(p, 'rb').read()
    return struct.unpack('<%df' % (len(b) // 4), b)


def compare(a, r):
    if len(a) != len(r):
        return None
    mx = 0.0; practical = 0; strict = 0; nonfinite = 0
    for x, y in zip(a, r):
        if not (math.isfinite(x) and math.isfinite(y)):
            nonfinite += 1; continue
        e = abs(x - y); mx = max(mx, e)
        if e > 5e-5 + 2e-4 * abs(y): practical += 1
        if e > 1e-7 + 1e-7 * abs(y): strict += 1
    return mx, practical, strict, nonfinite


rows = []
for group in sorted(os.listdir(win)):
    for scene in sorted(os.listdir(os.path.join(win, group))):
        mfilm = os.path.join(mac, group, scene, 'cpu-arm64-macos', 'film-0.f32')
        if not os.path.exists(mfilm):
            continue
        m = load(mfilm)
        out = ['%-34s' % (group + '/' + scene)]
        for backend in ('cpu-arm64-windows', 'wgsl-windows-arm64-d3d12'):
            f = os.path.join(win, group, scene, backend, 'film-0.f32')
            if not os.path.exists(f):
                out.append('%-26s %s' % (backend[:14], 'missing')); continue
            c = compare(load(f), m)
            if c is None:
                out.append('%-14s size differs' % backend[:14]); continue
            mx, p, s, nf = c
            out.append('%-14s max %.2e practical %4d strict %5d%s' % (backend[:14], mx, p, s, ' nonfinite %d' % nf if nf else ''))
        rows.append('  '.join(out))
print('vs macOS arm64 CPU oracle (film-0):')
print('\n'.join(rows))
