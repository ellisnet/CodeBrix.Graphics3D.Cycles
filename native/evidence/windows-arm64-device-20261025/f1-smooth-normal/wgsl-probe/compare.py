import math, struct, sys

root, a_name, b_name = sys.argv[1], sys.argv[2], sys.argv[3]


def load(name):
    b = open('%s/smooth.%s.bin' % (root, name), 'rb').read()
    return struct.unpack('<%df' % (len(b) // 4), b)


a, b = load(a_name), load(b_name)
heap = open(root + '/heap.bin', 'rb').read()
n = struct.unpack('<I', heap[:4])[0]
fields = ['n0'] * 3 + ['n1'] * 3 + ['n2'] * 3 + ['interp'] * 3 + ['weight']
bad_cases = 0
by_field = {}
shown = 0
for i in range(n):
    bad = []
    for k in range(13):
        x, y = a[i * 16 + k], b[i * 16 + k]
        if not (x == y or (math.isnan(x) and math.isnan(y))):
            bad.append((fields[k], k, x, y))
            by_field[fields[k]] = by_field.get(fields[k], 0) + 1
    if bad:
        bad_cases += 1
        if shown < 8:
            shown += 1
            words = struct.unpack('<IIIff', heap[4 + i * 20: 24 + i * 20])
            print('case %d words %08x %08x %08x u %.4f v %.4f' % ((i,) + words))
            for f, k, x, y in bad[:6]:
                print('   %-6s[%d] %s %.9g  %s %.9g' % (f, k % 3, a_name, x, b_name, y))
print('%d of %d cases differ; components by field: %s' % (bad_cases, n, by_field))
