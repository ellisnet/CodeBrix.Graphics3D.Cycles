#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the cbcOracleScene files of feature combinations that build the largest entries. Rerunning
reproduces them exactly.
mnee/huang-hair-moving-receiver: mnee/huang-hair-receiver (thick Huang hair curves and the floor as caustic receivers of a rough glass
sphere) with object motion blur on the floor receiver: caustics, thick curves and a moving receiver in one scene, the combination that
builds the largest MNEE walk (native/DEVICE.md 126).
subsurface/aov-principled-bump: subsurface/aov-cryptomatte-bump with Principled BSDFs (subsurface weight 1, random walk and
Christensen-Burley) in place of the Subsurface Scattering nodes, the Bump node on their Normal: the prepare_surface of
native/DEVICE.md 130.
"""
import json
from pathlib import Path
HERE=Path(__file__).resolve().parent
def node(nodes,handle):return next(n for n in nodes if n['handle']==handle)
def moving_receiver():
 data=json.loads((HERE/'mnee/huang-hair-receiver.json').read_text());data['id']='mnee/huang-hair-moving-receiver'
 data['description']=('MNEE shadow caustics on Huang hair curves and a moving floor: a rough glass sphere between them and the caustic lamp, '
  'the floor receiver moves over the shutter (object motion blur)')
 data['sourceFixture']='native/oracle/scenes/author_mnee_size.py (derived from mnee/huang-hair-receiver)';nodes=data['nodes']
 node(nodes,'integrator')['set']['motion_blur']=True;node(nodes,'camera')['set']['shuttertime']=0.6
 node(nodes,'floor-object')['set']['motion']=[[0.9987503,0.0,-0.0499792,-0.2, 0.0,1.0,0.0,-0.0, 0.0499792,0.0,0.9987503,-0.1],
  [1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0, 0.0,0.0,1.0,0.0],[0.9987503,0.0,0.0499792,0.2, 0.0,1.0,0.0,0.0, -0.0499792,0.0,0.9987503,0.1]]
 return data
def principled_bump():
 data=json.loads((HERE/'subsurface/aov-cryptomatte-bump.json').read_text());data['id']='subsurface/aov-principled-bump'
 data['description']=('Bump-mapped Principled BSDFs with full subsurface weight, random walk (middle) and Christensen-Burley (right), with AOV '
  'outputs and cryptomatte passes (derived from subsurface/aov-cryptomatte-bump)')
 data['sourceFixture']='native/oracle/scenes/author_mnee_size.py (derived from subsurface/aov-cryptomatte-bump)'
 for handle,method in (('sss','random_walk'),('sss-burley','burley')):
  s=node(node(data['nodes'],handle)['graph']['nodes'],'s');old=s['set']
  s['type']='principled_bsdf';s['set']=dict(distribution='multi_ggx',base_color=old['color'],roughness=0.35,ior=1.45,subsurface_weight=1.0,
   subsurface_method=method,subsurface_radius=old['radius'],subsurface_scale=old['scale'])
  g=node(data['nodes'],handle)['graph'];g['connect']=[['s','BSDF','output','Surface'] if c==['s','BSSRDF','output','Surface'] else c for c in g['connect']]
 return data
def main():
 for data in [moving_receiver(),principled_bump()]:
  path=HERE/(data['id']+'.json');path.write_text(json.dumps(data,indent=1)+'\n');print(path.relative_to(HERE))
if __name__=='__main__':main()
