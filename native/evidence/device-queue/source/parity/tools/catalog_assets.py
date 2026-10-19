#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Read-only glTF metadata inventory; not a replacement for the Gltf2 importer."""
import collections
import json
import struct
import subprocess
from pathlib import Path
root=Path.home()/'GitHome/Oasys/Assets/ForestCommunity'
paths=subprocess.check_output(['rg','--files','-g','*.gltf','-g','*.glb',str(root)],text=True).splitlines()
items=[]
for text in sorted(paths):
    p=Path(text)
    try:
        if p.suffix=='.gltf': doc=json.loads(p.read_text())
        else:
            with p.open('rb') as f:
                magic,version,size=struct.unpack('<III',f.read(12)); length,kind=struct.unpack('<II',f.read(8))
                if magic!=0x46546c67 or kind!=0x4e4f534a: raise ValueError('Invalid GLB')
                doc=json.loads(f.read(length))
        accessors=doc.get('accessors',[]); triangles=0; primitives=0
        for mesh in doc.get('meshes',[]):
            for primitive in mesh['primitives']:
                primitives+=1
                count=accessors[primitive.get('indices',primitive['attributes']['POSITION'])]['count']
                mode=primitive.get('mode',4)
                triangles+=count//3 if mode==4 else max(count-2,0) if mode in (5,6) else 0
        materials=doc.get('materials',[])
        items.append({'path':str(p.relative_to(root)),'meshTrianglesBeforeInstancing':triangles,
            'meshes':len(doc.get('meshes',[])),'primitives':primitives,'nodes':len(doc.get('nodes',[])),
            'materials':len(materials),'images':len(doc.get('images',[])),
            'skins':len(doc.get('skins',[])),'animations':len(doc.get('animations',[])),
            'alphaModes':sorted(set(m.get('alphaMode','OPAQUE') for m in materials)),
            'extensionsUsed':doc.get('extensionsUsed',[]),'extensionsRequired':doc.get('extensionsRequired',[]),
            'copyright':doc.get('asset',{}).get('copyright')})
    except Exception as e: items.append({'path':str(p.relative_to(root)),'error':str(e)})
out=Path(__file__).resolve().parents[1]/'work/asset-catalog.json'; out.parent.mkdir(exist_ok=True)
out.write_text(json.dumps({'root':str(root),'metadataOnly':True,'models':items},indent=2)+'\n')
print(json.dumps({'files':len(items),'errors':sum('error' in i for i in items),
 'texturedModels':sum(i.get('images',0)>0 for i in items),
 'extensions':dict(collections.Counter(x for i in items for x in i.get('extensionsUsed',[]))),
 'examples':sorted([i for i in items if 'error' not in i],key=lambda i:i['meshTrianglesBeforeInstancing'])[::max(1,len(items)//12)]},indent=2))
