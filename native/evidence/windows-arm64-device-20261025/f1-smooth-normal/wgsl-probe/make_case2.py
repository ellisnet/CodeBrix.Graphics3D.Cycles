import json, os, random, struct, sys

root = sys.argv[1]
random.seed(11)
UP = 0xffff8000
normals = [UP] * 32 + [random.getrandbits(32) for _ in range(32)]
cases = []
for (vi, off) in [((0, 1, 2), 0), ((0, 2, 3), 0), ((100, 101, 102), -100), ((7, 5, 3), 0), ((40, 41, 42), 0)]:
    for (u, v) in [(0.0, 0.0), (0.25, 0.25), (0.6, 0.3), (0.1, 0.1)]:
        cases.append((vi, off, 4, u, v))
while len(cases) < 128:
    vi = tuple(random.randrange(64) for _ in range(3))
    u = random.random(); v = random.random() * (1 - u)
    cases.append((vi, 0, 4, u, v))
header_words = 4
case_words = 8
table_base = (header_words + case_words * len(cases)) * 4
heap = struct.pack('<IIII', len(cases), 0, table_base, len(normals))
for vi, off, stride, u, v in cases:
    heap += struct.pack('<IIIiIffI', vi[0], vi[1], vi[2], off, stride, u, v, 0)
heap += struct.pack('<%dI' % len(normals), *normals)
open(os.path.join(root, 'heap2.bin'), 'wb').write(heap)
open(os.path.join(root, 'out2.bin'), 'wb').write(bytes(len(cases) * 8 * 4))
src = open(os.path.join(root, 'float.wgsl')).read() + '\n' + open(os.path.join(root, 'kernel2.wgsl')).read()
open(os.path.join(root, 'branch.wgsl'), 'w').write(src)
json.dump([{'Name': 'branch', 'Shader': 'branch.wgsl', 'Buffers': ['heap2.bin', 'out2.bin'], 'Output': 1,
            'Dispatches': [{'Entry': 'smooth_branch', 'Groups': 2}]}], open(os.path.join(root, 'cases.json'), 'w'))
print(len(cases), 'cases')
