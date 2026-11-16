#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Survey helper (scratch): decode the table scenes of row 2 (Integer Math, Boolean Math, Get Vector Component) from a
render_oracles.py run. For each backend and each cell: the operands (from the scene document's face attributes), the
rendered result (Integer Math from the AOV digits D0..D3, checked against R; Boolean Math from B; Get Vector Component
from V), whether all CELLxCELL pixels of the cell agree, and the pinned value of the case (pinned_integer_math of the
author script: strict for defined cases; for undefined cases the two's-complement/AArch64 model, marked "model").
Reported, never judged. Usage: decode_tables.py RUN_DIR SCENE_ID --builder DIR [--json OUT]"""
import argparse,array,json,math,struct,sys
from pathlib import Path
import importlib.util


def load_builder(directory):
    path=Path(directory).resolve()/'author_surface_nodes.py';sys.path.insert(0,str(path.parent))
    spec=importlib.util.spec_from_file_location('decode_author_surface_nodes',path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
    return m


def read_pass(directory,meta,name,repeat=0):
    entry=next(e for e in meta['passes'] if e['name']==name)
    values=array.array('f');values.frombytes((directory/f"pass-{entry['index']}-{repeat}.f32").read_bytes());return values


def main():
    p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('scene');p.add_argument('--builder',type=Path,required=True)
    p.add_argument('--json',type=Path);a=p.parse_args();m=load_builder(a.builder)
    root=a.run/'oracle'/a.scene;backends=sorted(d.name for d in root.iterdir() if (d/'meta.json').is_file())
    meta0=json.loads((root/backends[0]/'meta.json').read_text());scene_file=Path(meta0['sceneFile'])
    repo=Path(__file__).resolve().parents[3];document=json.loads((repo/scene_file).read_text())
    mesh=next(n for n in document['nodes'] if n['handle']=='table');attrs={x['name']:x for x in mesh['mesh']['attributes']}
    materials=mesh['set']['used_shaders'];shaders=mesh['set']['shader']
    cells=[]
    for k in range(len(shaders)//2):
        face=2*k;mat=materials[shaders[face]];row={'cell':k+1,'material':mat}
        ints={}
        for name in 'abc':
            if name+'_hi' in attrs:ints[name]=int(attrs[name+'_hi']['data'][face])+int(attrs[name+'_lo']['data'][face])
        row['operands']=ints
        if 'vec' in attrs:row['vector']=attrs['vec']['data'][3*face:3*face+3]
        if mat.startswith('im-'):
            op=mat[3:];args=(ints.get('a',0),ints.get('b',0),ints.get('c',0))
            try:row['pinned']=m.pinned_integer_math(op,*args,strict=True);row['pinnedKind']='defined'
            except m.Undefined:row['pinned']=m.pinned_integer_math(op,*args,strict=False);row['pinnedKind']='model (undefined behaviour)'
        elif mat.startswith('bm-'):row['pinned']=m.pinned_boolean_math(mat[3:],ints['a'],ints['b']);row['pinnedKind']='defined'
        elif mat=='gvc':
            i=ints['a'];row['pinned']=struct.unpack('<f',struct.pack('<f',row['vector'][i]))[0] if 0<=i<=2 else 0.0;row['pinnedKind']='defined'
        cells.append(row)
    report=dict(run=str(a.run),scene=a.scene,sceneFile=str(scene_file),backends={})
    for backend in backends:
        d=root/backend;meta=json.loads((d/'meta.json').read_text());W,H=meta['width'],meta['height']
        names=[e['name'] for e in meta.get('passes',[])]
        per_repeat=[]
        for r in range(meta['repeats']):
            P={n:read_pass(d,meta,n,r) for n in names};cellmap={}
            for px in range(W*H):
                c=P['CELL'][px]
                if c==0:continue
                cellmap.setdefault(c,[]).append(px)
            out=[]
            for row in cells:
                pix=cellmap.get(float(row['cell']),[])
                entry=dict(cell=row['cell'],pixels=len(pix))
                if 'D0' in P:
                    digits={n:sorted({P[n][q] for q in pix}) for n in ('D0','D1','D2','D3','R')}
                    entry['uniform']=all(len(v)==1 for v in digits.values())
                    if entry['uniform'] and pix:
                        D=[digits[n][0] for n in ('D0','D1','D2','D3')]
                        exact=all(float(v).is_integer() for v in D)
                        value=int(D[0])+256*int(D[1])+65536*int(D[2])+16777216*int(D[3]) if exact else None
                        entry.update(digits=D,value=value,R=digits['R'][0],
                                     rConsistent=(value is not None and float(value)==digits['R'][0]) or (value is not None and abs(float(value)-digits['R'][0])<=abs(value)*2**-23))
                    else:entry['digits']={n:v[:4] for n,v in digits.items()}
                else:
                    key='B' if 'B' in P else 'V';vals=sorted({P[key][q] for q in pix})
                    entry['uniform']=len(vals)==1;entry['value']=vals[0] if len(vals)==1 else vals[:4]
                entry['matchesPinned']=entry.get('value')==row['pinned'] if entry.get('uniform') else None
                out.append(entry)
            per_repeat.append(out)
        report['backends'][backend]=dict(repeats=per_repeat,repeatsAgree=all(x==per_repeat[0] for x in per_repeat))
    report['cells']=cells
    lines=[f"{a.scene} ({a.run.name})",f"{'cell':>4} {'material':18} {'operands':40} {'pinned':>13} kind | "+' | '.join(backends)]
    for i,row in enumerate(cells):
        vals=[]
        for b in backends:
            e=report['backends'][b]['repeats'][0][i]
            mark='' if e.get('matchesPinned') else (' (differs)' if e.get('matchesPinned') is False else ' (non-uniform)')
            vals.append(f"{e.get('value')}{mark}")
        ops=json.dumps(row['operands'])+(' v='+json.dumps(row['vector']) if 'vector' in row else '')
        lines.append(f"{row['cell']:>4} {row['material']:18} {ops:40} {row['pinned']!s:>13} {row['pinnedKind'][:7]} | "+' | '.join(vals))
    for b in backends:
        rs=report['backends'][b]['repeats'][0]
        lines.append(f"{b}: repeats agree {report['backends'][b]['repeatsAgree']}; cells uniform {sum(1 for e in rs if e.get('uniform'))}/{len(rs)}; "
                     f"match pinned {sum(1 for e in rs if e.get('matchesPinned'))}/{len(rs)}; "
                     f"match pinned (defined cases) {sum(1 for e,c in zip(rs,cells) if e.get('matchesPinned') and c['pinnedKind']=='defined')}/{sum(1 for c in cells if c['pinnedKind']=='defined')}")
    text='\n'.join(lines)+'\n';print(text,end='')
    if a.json:a.json.write_text(json.dumps(report,indent=1)+'\n');a.json.with_suffix('.txt').write_text(text)


if __name__=='__main__':main()
