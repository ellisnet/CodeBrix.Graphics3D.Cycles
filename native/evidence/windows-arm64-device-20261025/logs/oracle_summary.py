import json, os, re, sys

run, scenes = sys.argv[1], sys.argv[2]
d = json.load(open(run))
SMOOTH = re.compile(r'"smooth"\s*:\s*(\[\s*)?true')


def smooth(scene):
    try:
        text = open(os.path.join(scenes, scene + '.json')).read()
    except OSError:
        return '?'
    return 'smooth' if SMOOTH.search(text) else 'flat'


def ok(v):
    return v is True or (isinstance(v, dict) and v and all(x is True for x in v.values()))


cp = d['comparisonsPassed']
print('sample value:', repr(next(iter(cp.items()))))
passed = 0
for k in sorted(cp):
    if ok(cp[k]):
        passed += 1
        print('pass       %-40s %s' % (k, smooth(k)))
for k in sorted(cp):
    if not ok(cp[k]):
        print('NOT PASSED %-40s %-6s %s' % (k, smooth(k), str(cp[k])[:150]))
print('passed %d of %d' % (passed, len(cp)))
