// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Curves in another feature's state gate (CODEBRIX_CYCLES_GATE_CURVES=1, off otherwise): tufts of every shape among the
// gate's own geometry, baked and instanced, with BVH2's unaligned nodes, so the feature's traversal and hit paths meet curves.
#include "curve_fixture.h"
#include <cstdlib>
namespace ccl {
inline bool gate_curves_enabled(){return std::getenv("CODEBRIX_CYCLES_GATE_CURVES")!=nullptr;}
inline void gate_curve_params(SceneParams &params){if(gate_curves_enabled())params.use_bvh_unaligned_nodes=true;}
// instanced: two objects share the thick tuft. A gate whose fixture traverses a transform-applied object locally passes false:
// this device refuses a local traversal from the scene root once the scene has instances, for any primitive.
inline void gate_curve_tufts(Scene &scene,Shader *shader=nullptr,bool instanced=true){
  if(!gate_curves_enabled())return;
  if(!shader)shader=scene.default_surface;
  std::vector<CurveFixtureGeometry> geometry(3);
  const CurveShapeType shapes[]={CURVE_RIBBON,CURVE_THICK,CURVE_THICK_LINEAR};
  for(unsigned s=0;s<3;s++)curve_fixture_hair(scene,geometry[s],shapes[s],shader,2026711u+s*13u);
  const Transform tfm[]={transform_translate(make_float3(-1.5f,-.5f,.5f)),transform_translate(make_float3(1.5f,-.5f,.5f))*transform_rotate(.7f,normalize(make_float3(.9f,.2f,.4f)))*transform_scale(make_float3(1.2f,.9f,1.f)),
    transform_translate(make_float3(0,.5f,-.5f))*transform_rotate(-.4f,normalize(make_float3(.1f,.8f,.5f)))*transform_scale(make_float3(.8f)),transform_translate(make_float3(.5f,1.f,1.f))*transform_scale(make_float3(-1.f,1.f,1.f))};
  const unsigned use[]={0,1,2,1};
  // Without instances every object is baked: a uniform scale (a curve with a non-uniform one stays an instance).
  const Transform baked[]={tfm[0],transform_translate(make_float3(1.5f,-.5f,.5f))*transform_rotate(.7f,normalize(make_float3(.9f,.2f,.4f)))*transform_scale(make_float3(1.1f)),tfm[2]};
  for(unsigned i=0;i<(instanced?4u:3u);i++){auto object=scene.create_node<Object>();object->set_geometry(geometry[use[i]].hair);object->set_tfm(instanced?tfm[i]:baked[i]);}
}
}
