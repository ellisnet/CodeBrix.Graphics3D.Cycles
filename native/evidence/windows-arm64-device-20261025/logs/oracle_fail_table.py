import os, re, struct, sys

log, root = sys.argv[1], sys.argv[2]
text = open(log, encoding='utf-8', errors='replace').read()
blocks = re.split(r'\n(?=\S+/\S+ \(\d+x\d+\)\n)', text)


def load(p):
    b = open(p, 'rb').read()
    return struct.unpack('<%df' % (len(b) // 4), b)


for b in blocks:
    m = re.match(r'(\S+/\S+) \((\d+)x(\d+)\)', b)
    if not m or 'FAILED' not in b:
        continue
    scene, w, h = m.group(1), int(m.group(2)), int(m.group(3))
    stats = re.search(r'vs cpu-arm64-windows\s+(\S+)\s+(\S+)\s+(\S+)\s+(\d+)\s+(\d+)', b)
    worst = re.search(r'worst (.*)', b)
    cdir = os.path.join(root, scene, 'cpu-arm64-windows', 'film-0.f32')
    gdir = os.path.join(root, scene, 'wgsl-windows-arm64-d3d12', 'film-0.f32')
    brighter = darker = zeros = bad_px = 0
    if os.path.exists(cdir) and os.path.exists(gdir):
        c, g = load(cdir), load(gdir)
        for p in range(w * h):
            failed = False
            for k in range(3):
                a, r = g[p * 4 + k], c[p * 4 + k]
                if abs(a - r) > 5e-5 + 2e-4 * abs(r):
                    failed = True
                    if a > r: brighter += 1
                    else: darker += 1
                    if a == 0 and r != 0: zeros += 1
            bad_px += failed
    print('%-34s maxAbs %-7s practical %-5s | px %4d/%-5d brighter %4d darker %4d gpu0 %4d | %s' % (
        scene, stats.group(1) if stats else '?', stats.group(4) if stats else '?', bad_px, w * h,
        brighter, darker, zeros, worst.group(1)[:70] if worst else ''))
