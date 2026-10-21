#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""M3 job 2: apply the Gabor / wavelength / wireframe opcode changes to the tree (exact anchors)."""
import json,sys
from pathlib import Path
ROOT=Path(sys.argv[1]).resolve();DEV=Path(__file__).resolve().parent
def edit(rel,pairs):
    p=ROOT/rel;s=p.read_text()
    for a,b in pairs:
        if s.count(a)!=1:raise SystemExit(f'{rel}: anchor count {s.count(a)}: {a[:80]!r}')
        s=s.replace(a,b)
    p.write_text(s)
# 1. WGSL handlers (inside the file-wide SVM_NODES_ANY region).
nodes=(DEV/'m3_nodes_head.wgsl').read_text()
edit('native/shaders/wavefront_svm_nodes.wgsl',[
 ('// light_path}.h, closure/bsdf_util.h, util/hash.h, util/color.h, util/transform.h and\n// kernel/tables.h (blackbody fit), translated to WGSL for the SVM interpreter.',
  '// light_path,gabor,wavelength,wireframe}.h, closure/bsdf_util.h, util/hash.h,\n// util/color.h, util/transform.h, util/projection.h, kernel/util/colorspace.h,\n// kernel/util/differential.h and kernel/tables.h (blackbody fit, CIE colour\n// matching rows), translated to WGSL for the SVM interpreter.'),
 ('    svm_optional_store(s,svm_byte(node+N_TexVoronoi_radius_offset),radius);\n}\n//#endif\n//#endif',
  '    svm_optional_store(s,svm_byte(node+N_TexVoronoi_radius_offset),radius);\n}\n//#endif\n'+nodes+'//#endif')])
# 2. Interpreter size switch and dispatch.
edit('native/shaders/wavefront_svm.wgsl',[
 ('//#if SVM_NODE_LIGHT_PATH\n            case NODE_LIGHT_PATH:{size=SZ_LightPath;}\n//#endif\n',
  '//#if SVM_NODE_LIGHT_PATH\n            case NODE_LIGHT_PATH:{size=SZ_LightPath;}\n//#endif\n'
  '//#if SVM_NODE_TEX_GABOR\n            case NODE_TEX_GABOR:{size=SZ_TexGabor;}\n//#endif\n'
  '//#if SVM_NODE_WAVELENGTH\n            case NODE_WAVELENGTH:{size=SZ_Wavelength;}\n//#endif\n'
  '//#if SVM_NODE_WIREFRAME\n            case NODE_WIREFRAME:{size=SZ_Wireframe;}\n//#endif\n'),
 ('            case NODE_LIGHT_PATH:{\n                svm_light_path(&s,node,index,flag,visibility);\n            }\n//#endif\n',
  '            case NODE_LIGHT_PATH:{\n                svm_light_path(&s,node,index,flag,visibility);\n            }\n//#endif\n'
  '//#if SVM_NODE_TEX_GABOR\n            case NODE_TEX_GABOR:{\n                svm_gabor_texture(&s,node);\n            }\n//#endif\n'
  '//#if SVM_NODE_WAVELENGTH\n            case NODE_WAVELENGTH:{\n                svm_wavelength(&s,node);\n            }\n//#endif\n'
  '//#if SVM_NODE_WIREFRAME\n            case NODE_WIREFRAME:{\n                svm_wireframe(&s,node);\n            }\n//#endif\n')])
# 3. Feature bits (after the M1+M2 table; 30/31 stay reserved for E3).
edit('native/cycles/pipelines.h',[
 ('  SF_TEX_VORONOI=1ull<<52,\n  SF_ALL=(1ull<<53)-1,',
  '  SF_TEX_VORONOI=1ull<<52,\n  // Gabor texture, wavelength and wireframe handlers (wavefront_svm_nodes.wgsl).\n  SF_TEX_GABOR=1ull<<53, SF_WAVELENGTH=1ull<<54, SF_WIREFRAME=1ull<<55,\n  SF_ALL=(1ull<<56)-1,')])
edit('native/cycles/pipelines.cpp',[
 ('    {"SVM_NODE_TEX_VORONOI",SF_TEX_VORONOI},\n',
  '    {"SVM_NODE_TEX_VORONOI",SF_TEX_VORONOI},{"SVM_NODE_TEX_GABOR",SF_TEX_GABOR},{"SVM_NODE_WAVELENGTH",SF_WAVELENGTH},{"SVM_NODE_WIREFRAME",SF_WIREFRAME},\n'),
 ('      SF_FRESNEL|SF_VECTOR_ROTATE|SF_BLACKBODY|SF_NORMAL|SF_SHADING_INFO|SF_LIGHT_PATH|SF_TEX_VORONOI},',
  '      SF_FRESNEL|SF_VECTOR_ROTATE|SF_BLACKBODY|SF_NORMAL|SF_SHADING_INFO|SF_LIGHT_PATH|SF_TEX_VORONOI|SF_TEX_GABOR|SF_WAVELENGTH|SF_WIREFRAME},'),
 ('    {"SVM_NODES_DOT",SF_MAPPING|SF_FRESNEL|SF_VECTOR_ROTATE|SF_BLACKBODY|SF_NORMAL|SF_SHADING_INFO},',
  '    {"SVM_NODES_DOT",SF_MAPPING|SF_FRESNEL|SF_VECTOR_ROTATE|SF_BLACKBODY|SF_NORMAL|SF_SHADING_INFO|SF_WAVELENGTH|SF_WIREFRAME},'),
 ('    {"SVM_NODES_HASH",SF_TEX_NOISE|SF_TEX_WHITE_NOISE|SF_TEX_WAVE|SF_TEX_BRICK|SF_TEX_VORONOI},',
  '    {"SVM_NODES_HASH",SF_TEX_NOISE|SF_TEX_WHITE_NOISE|SF_TEX_WAVE|SF_TEX_BRICK|SF_TEX_VORONOI|SF_TEX_GABOR},'),
 ('    OP(NODE_VECTOR_TRANSFORM,VectorTransform,SF_SHADING_INFO) OP(NODE_LIGHT_PATH,LightPath,SF_LIGHT_PATH)\n',
  '    OP(NODE_VECTOR_TRANSFORM,VectorTransform,SF_SHADING_INFO) OP(NODE_LIGHT_PATH,LightPath,SF_LIGHT_PATH)\n'
  '    OP(NODE_TEX_GABOR,TexGabor,SF_TEX_GABOR) OP(NODE_WAVELENGTH,Wavelength,SF_WAVELENGTH) OP(NODE_WIREFRAME,Wireframe,SF_WIREFRAME)\n')])
# 4. Layout constants inside the node region.
edit('native/cycles/svm_layout.cpp',[
 ('FIELD(TexVoronoi,w_out_offset);FIELD(TexVoronoi,radius_offset);\n  source<<"//#endif\\n";',
  'FIELD(TexVoronoi,w_out_offset);FIELD(TexVoronoi,radius_offset);\n'
  '  SIZE(TexGabor);FIELD(TexGabor,gabor_type);FIELD(TexGabor,orientation_3d);FIELD(TexGabor,scale);FIELD(TexGabor,frequency);FIELD(TexGabor,anisotropy);\n'
  '  FIELD(TexGabor,orientation_2d);FIELD(TexGabor,coordinates);FIELD(TexGabor,value_offset);FIELD(TexGabor,phase_offset);FIELD(TexGabor,intensity_offset);\n'
  '  SIZE(Wavelength);FIELD(Wavelength,wavelength);FIELD(Wavelength,color_offset);\n'
  '  SIZE(Wireframe);FIELD(Wireframe,in_size);FIELD(Wireframe,bump_filter_width);FIELD(Wireframe,use_pixel_size);FIELD(Wireframe,bump_offset);FIELD(Wireframe,out_fac_offset);\n'
  '  source<<"//#endif\\n";'),
 ('constant("SVMN_D_scene_time_time",offsetof(KernelData,scene_time.time));constant("SVMN_D_scene_time_frame",offsetof(KernelData,scene_time.frame));\n',
  'constant("SVMN_D_scene_time_time",offsetof(KernelData,scene_time.time));constant("SVMN_D_scene_time_frame",offsetof(KernelData,scene_time.frame));\n'
  '  constant("SVMN_D_film_xyz_to_r",offsetof(KernelData,film.xyz_to_r));constant("SVMN_D_film_xyz_to_g",offsetof(KernelData,film.xyz_to_g));constant("SVMN_D_film_xyz_to_b",offsetof(KernelData,film.xyz_to_b));\n')])
# 5. Compiled graphs (60 -> 68) and the render order.
graphs=(DEV/'fixture_graphs.txt').read_text()
edit('native/cycles/nodes_fixture.h',[
 ('// rotate, blackbody and normal. Inputs vary',
  '// rotate, blackbody, normal, Gabor, wavelength and wireframe. Inputs vary'),
 ('static constexpr unsigned NODES_GRAPH_COUNT=60;','static constexpr unsigned NODES_GRAPH_COUNT=68;'),
 ('  static constexpr unsigned order[NODES_GRAPH_COUNT]={0,52,24,30,36,40,53,1,25,31,37,43,54,2,26,32,38,44,55,3,28,33,39,45,56,48,49,50,51,57,46,47,41,42,58,34,35,29,27,59,\n    4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23};',
  '  // Graphs 60-67 (Gabor, wavelength, wireframe) take visible slots of repeated\n  // families; the displaced repeats follow graph 59.\n'
  '  static constexpr unsigned order[NODES_GRAPH_COUNT]={0,52,24,30,36,40,53,60,25,64,65,43,54,61,26,62,66,44,63,67,28,33,39,45,56,48,49,50,51,57,46,47,41,42,58,34,35,29,27,59,\n    1,31,37,2,32,38,55,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23};'),
 ('  else if(test>=52){\n    // Voronoi','  else if(test>=52&&test<60){\n    // Voronoi'),
 ('  else{auto n=g.create_node<NormalNode>();',graphs+'  else{auto n=g.create_node<NormalNode>();')])
# 6. Compiled worlds (20 -> 23).
edit('native/cycles/svm_world_gate.cpp',[
 ('static constexpr unsigned WORLD_COUNT=20;','static constexpr unsigned WORLD_COUNT=23;'),
 ('  if(test==7){\n    // Is Camera Ray',(DEV/'world_graphs.txt').read_text()+'  if(test==7){\n    // Is Camera Ray')])
# 7. Typed programs and rejections.
edit('native/cycles/svm_surface_gate.cpp',[
 ('    if(test==1)program.words.push_back(NODE_TEX_GABOR);','    if(test==1)program.words.push_back(NODE_LIGHT_FALLOFF);'),
 ('  auto &ds=scene.dscene;const auto original=ds.data;constexpr unsigned count=20;\n  const uint expected[count]={2,2,2,2,16,16,16,4,16,16,16,16,16,2,2,2,2,2,2,2};',
  '  auto &ds=scene.dscene;const auto original=ds.data;constexpr unsigned count=21;\n  const uint expected[count]={2,2,2,2,16,16,16,4,16,16,16,16,16,2,2,2,2,2,2,2,16};'),
 ('    const uint deferred[]={NODE_RAYCAST,NODE_TEX_GABOR,NODE_IES,NODE_WAVELENGTH};','    const uint deferred[]={NODE_RAYCAST,NODE_LIGHT_FALLOFF,NODE_IES,NODE_AOV_START};'),
 ('    const uint more[]={NODE_AMBIENT_OCCLUSION,NODE_BEVEL,NODE_PARTICLE_INFO,NODE_HAIR_INFO,NODE_TEX_SKY,NODE_WIREFRAME,NODE_POINT_INFO};\n    if(test>=13)program.words.push_back(more[test-13]);',
  '    const uint more[]={NODE_AMBIENT_OCCLUSION,NODE_BEVEL,NODE_PARTICLE_INFO,NODE_HAIR_INFO,NODE_TEX_SKY,NODE_RADIAL_TILING,NODE_POINT_INFO};\n    if(test>=13&&test<20)program.words.push_back(more[test-13]);\n'
  '    if(test==20)program.add(NODE_TEX_GABOR,SVMNodeTexGabor{.gabor_type=NodeGaborType(2),.orientation_3d=color(1,0,0),.scale=value(1),.frequency=value(2),.anisotropy=value(1),.orientation_2d=value(0),\n'
  '      .coordinates=0,.value_offset=3,.phase_offset=SVM_STACK_INVALID,.intensity_offset=SVM_STACK_INVALID});'),
 ('    for(unsigned variant=0;variant<12;variant++){\n      Program p(ds);begin(p);const float temperature[]',
  (DEV/'typed_programs.txt').read_text()+'    for(unsigned variant=0;variant<12;variant++){\n      Program p(ds);begin(p);const float temperature[]')])
# 8. Gate expectations.
edit('native/tools/nodes_gate.py',[
 ("report['compiledGraphs']==60 and report['closureFamilies']==5 and report['invalidCasesRejected']==31","report['compiledGraphs']==68 and report['closureFamilies']==5 and report['invalidCasesRejected']==32"),
 ("if kind=='nodes_world':assert report['compiledWorlds']==20","if kind=='nodes_world':assert report['compiledWorlds']==23"),
 ('compiled graphs, typed programs and rejections; compiled worlds;','compiled graphs (including Gabor, wavelength and wireframe), typed programs and rejections; compiled worlds;')])
# 9. Inventory rows (status as M2 set them: integrated-tested / partial, no evidence checkpoint).
p=ROOT/'compatibility/opcodes.json';text=p.read_text()
for symbol in ('NODE_TEX_GABOR','NODE_WAVELENGTH','NODE_WIREFRAME'):
    at=text.index('"symbol": "'+symbol+'"');end=text.index('}',at)
    row=text[at:end];new=row.replace('"status": "unassessed"','"status": "integrated-tested"',1).replace('"coverage": "unassessed"','"coverage": "partial"',1)
    if new==row:raise SystemExit('opcodes row '+symbol)
    text=text[:at]+new+text[end:]
p.write_text(text);json.loads(text)
print('applied')
