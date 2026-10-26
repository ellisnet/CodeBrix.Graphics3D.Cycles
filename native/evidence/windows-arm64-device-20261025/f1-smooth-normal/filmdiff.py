import struct, sys

def load(p):
    b = open(p, 'rb').read()
    return struct.unpack('<%df' % (len(b) // 4), b)

def main(cpu_path, gpu_path, w, h):
    c, g = load(cpu_path), load(gpu_path)
    fail = [[' '] * w for _ in range(h)]
    stats = {'gpu_brighter': 0, 'gpu_darker': 0, 'gpu_zero_cpu_nonzero': 0, 'nan': 0}
    ratios = []
    worst = []
    for y in range(h):
        for x in range(w):
            i = (y * w + x) * 4
            bad = False
            for k in range(4):
                a, b = g[i + k], c[i + k]
                if a != a or b != b:
                    stats['nan'] += 1; bad = True; continue
                if abs(a - b) > 5e-5 + 2e-4 * abs(b):
                    bad = True
                    if k < 3:
                        if a > b: stats['gpu_brighter'] += 1
                        else: stats['gpu_darker'] += 1
                        if a == 0 and b != 0: stats['gpu_zero_cpu_nonzero'] += 1
                        if b > 1e-4: ratios.append(a / b)
                        worst.append((abs(a - b), x, y, 'RGBA'[k], b, a))
            if bad:
                fail[y][x] = '#'
    print('failure map (row 0 = bottom printed last; # = practical failure)')
    for y in reversed(range(h)):
        print(''.join(fail[y]))
    print(stats)
    if ratios:
        ratios.sort()
        print('gpu/cpu ratio over failing rgb: min %.3f median %.3f max %.3f' % (ratios[0], ratios[len(ratios)//2], ratios[-1]))
    worst.sort(reverse=True)
    for d, x, y, ch, b, a in worst[:12]:
        print('px %2d,%2d %s cpu %.5f gpu %.5f diff %.5f' % (x, y, ch, b, a, d))
    # luminance maps for eyeballing
    def lum_map(f):
        chars = ' .:-=+*#%@'
        out = []
        for y in reversed(range(h)):
            row = ''
            for x in range(w):
                i = (y * w + x) * 4
                l = (f[i] + f[i+1] + f[i+2]) / 3
                row += chars[min(9, int(l * 9 / 1.0))]
            out.append(row)
        return out
    for name, f in (('CPU', c), ('GPU', g)):
        print(name + ' luminance (0..1):')
        print('\n'.join(lum_map(f)))
    # alpha channel summary
    ca = [c[i] for i in range(3, len(c), 4)]; ga = [g[i] for i in range(3, len(g), 4)]
    print('alpha cpu min/max %.3f %.3f  gpu min/max %.3f %.3f' % (min(ca), max(ca), min(ga), max(ga)))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]))
