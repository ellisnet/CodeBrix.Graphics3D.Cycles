import json, hashlib, os, sys
MAIN='/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles'
def sha(p):
    return hashlib.sha256(open(p,'rb').read()).hexdigest() if os.path.exists(p) else None
p=sys.argv[1]; j=json.load(open(p)); files=j.get('files') or j.get('changes')
mode=sys.argv[2]  # before|after
res={'match':[], 'mismatch':[]}
for f in files:
    cur=sha(os.path.join(MAIN,f['path']))
    exp=f['beforeSha256'] if mode=='before' else f['afterSha256']
    (res['match'] if cur==exp else res['mismatch']).append((f['path'],f['change'],cur==None))
print(mode, 'match', len(res['match']), 'mismatch', len(res['mismatch']))
for m in res['mismatch']: print('  MISMATCH', m)
