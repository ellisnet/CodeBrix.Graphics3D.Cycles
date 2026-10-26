import json, math, os, random, struct, sys

root = sys.argv[1]
random.seed(7)


def encode(n):
    # Octahedral 16+16 packing (upper/lower hemisphere fold), nearest grid point.
    s = abs(n[0]) + abs(n[1]) + abs(n[2])
    x, y = n[0] / s, n[1] / s
    if n[2] < 0:
        x, y = (1 - abs(y)) * math.copysign(1, x), (1 - abs(x)) * math.copysign(1, y)
    qx = max(0, min(65535, int(math.floor((x * 0.5 + 0.5) * 65535 + 0.5))))
    qy = max(0, min(65535, int(math.floor((y * 0.5 + 0.5) * 65535 + 0.5))))
    return qx | (qy << 16)


cases = []
up = encode((0.0, 1.0, 0.0))
for u, v in [(0.0, 0.0), (0.25, 0.25), (0.5, 0.25), (0.1, 0.8), (1 / 3, 1 / 3), (0.9, 0.05)]:
    cases.append((up, up, up, u, v))
for qx in (32767, 32768):
    w = qx | (65535 << 16)
    cases.append((w, w, w, 0.3, 0.3))
while len(cases) < 256:
    vs = []
    for _ in range(3):
        a = [random.gauss(0, 1) for _ in range(3)]
        l = math.sqrt(sum(c * c for c in a))
        vs.append(encode([c / l for c in a]))
    u = random.random(); v = random.random() * (1 - u)
    cases.append((vs[0], vs[1], vs[2], u, v))

heap = struct.pack('<I', len(cases))
for a, b, c, u, v in cases:
    heap += struct.pack('<IIIff', a, b, c, u, v)
open(os.path.join(root, 'heap.bin'), 'wb').write(heap)
open(os.path.join(root, 'out.bin'), 'wb').write(bytes(len(cases) * 16 * 4))
src = open(os.path.join(root, 'float.wgsl')).read() + '\n' + open(os.path.join(root, 'kernel.wgsl')).read()
open(os.path.join(root, 'smooth.wgsl'), 'w').write(src)
json.dump([{'Name': 'smooth', 'Shader': 'smooth.wgsl', 'Buffers': ['heap.bin', 'out.bin'], 'Output': 1,
            'Dispatches': [{'Entry': 'smooth_normal', 'Groups': 4}]}], open(os.path.join(root, 'cases.json'), 'w'))
print('up word 0x%08x, %d cases' % (up, len(cases)))
