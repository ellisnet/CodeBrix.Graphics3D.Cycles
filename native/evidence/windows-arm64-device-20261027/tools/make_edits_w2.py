import json,sys
edits=json.load(open(sys.argv[1]))  # start from the shader_data edit
edits+=[
 # wavefront_mnee.wgsl:197
 ['vtx.ng=mn_normalize(rounded_cross(dp_du,dp_dv));if((sd.object_flag&SD_OBJECT_NEGATIVE_SCALE)!=0u){vtx.ng=-vtx.ng;}',
  'let ng_unsigned=mn_normalize(rounded_cross(dp_du,dp_dv));vtx.ng=select(ng_unsigned,-ng_unsigned,(sd.object_flag&SD_OBJECT_NEGATIVE_SCALE)!=0u);'],
 # wavefront_light_tree.wgsl:158-160
 ['var dir=lt_safe_normalize(rounded_cross(v[1]-v[0],v[2]-v[0]));if(sampling==LT_EMISSION_SAMPLING_BACK){dir=-dir;}',
  'let dir_front=lt_safe_normalize(rounded_cross(v[1]-v[0],v[2]-v[0]));let dir=select(dir_front,-dir_front,sampling==LT_EMISSION_SAMPLING_BACK);'],
 ['if((flags&LT_SD_OBJECT_TRANSFORM_APPLIED)!=0u&&(flags&LT_SD_OBJECT_NEGATIVE_SCALE)!=0u){dir=-dir;}cone.axis=dir;',
  'cone.axis=select(dir,-dir,(flags&LT_SD_OBJECT_TRANSFORM_APPLIED)!=0u&&(flags&LT_SD_OBJECT_NEGATIVE_SCALE)!=0u);'],
 # wavefront_triangle_light.wgsl:217-218
 ['var n=rounded_cross(e0,e1);\n    if((load_word(table_at(G_object_flag,object,SIZE_object_flag))&SD_OBJECT_NEGATIVE_SCALE)!=0u){n= -n;}',
  'let n_unsigned=rounded_cross(e0,e1);\n    let n=select(n_unsigned,-n_unsigned,(load_word(table_at(G_object_flag,object,SIZE_object_flag))&SD_OBJECT_NEGATIVE_SCALE)!=0u);'],
 # wavefront_volume_stack.wgsl:82
 ['var cross_normal=rounded_cross(b-a,c-a);if(applied&&(flags&SD_OBJECT_NEGATIVE_SCALE)!=0u){cross_normal=rounded_cross(c-a,b-a);}',
  'let swap_winding=applied&&(flags&SD_OBJECT_NEGATIVE_SCALE)!=0u;let cross_normal=rounded_cross(select(b-a,c-a,swap_winding),select(c-a,b-a,swap_winding));'],
]
json.dump(edits,open(sys.argv[2],'w'))
# Report which source files contain each old text verbatim (generated modules copy the source).
from pathlib import Path
src={p.name:p.read_text() for p in Path(r'C:\GitHome\CodeBrix.Graphics3D.Cycles\native\shaders').glob('*.wgsl')}
for old,_ in edits:print([n for n,t in src.items() if old in t],old[:60])
