// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// CBC_BSDF_FIXTURE 4: the Hair BSDF node (reflection, transmission) and the Principled Hair BSDF node's Chiang model
// in its three colour parametrizations, real native shader graphs (SVM compiler), on the fixture's triangles and on
// curves of all three shapes (the same 20 graphs, one curve per graph). States on curves are not traversal hits:
// the state gates aim at the closures, as the triangle states do; real curve hits come in the render gate.
#include "scene/hair.h"
[[maybe_unused]] static ShaderOutput *hair_bsdf(ShaderGraph &g,ClosureType component,float u,float v,float offset,float3 color,bool tangent=false){
  auto n=g.create_node<HairBsdfNode>();n->set_component(component);n->set_roughness_u(u);n->set_roughness_v(v);n->set_offset(offset);n->set_color(color);
  if(tangent){auto t=g.create_node<ColorNode>();t->set_value(make_float3(.8,.3,.2));g.connect(t->output("Color"),n->input("Tangent"));}
  return n->output("BSDF");
}
struct HairChiangParams {NodePrincipledHairParametrization parametrization;float3 color,tint,absorption;float melanin,redness,random_color,roughness,radial,random_roughness,coat,ior,offset;bool link_random;};
[[maybe_unused]] static ShaderOutput *hair_chiang(ShaderGraph &g,const HairChiangParams &q){
  auto n=g.create_node<PrincipledHairBsdfNode>();n->set_model(NODE_PRINCIPLED_HAIR_CHIANG);n->set_parametrization(q.parametrization);
  n->set_color(q.color);n->set_tint(q.tint);n->set_absorption_coefficient(q.absorption);n->set_melanin(q.melanin);n->set_melanin_redness(q.redness);n->set_random_color(q.random_color);
  n->set_roughness(q.roughness);n->set_radial_roughness(q.radial);n->set_random_roughness(q.random_roughness);n->set_coat(q.coat);n->set_ior(q.ior);n->set_offset(q.offset);
  if(q.link_random){auto geometry=g.create_node<GeometryNode>();auto math=g.create_node<MathNode>();math->set_math_type(NODE_MATH_FRACTION);g.connect(geometry->output("Position"),math->input("Value1"));g.connect(math->output("Value"),n->input("Random"));}
  return n->output("BSDF");
}
[[maybe_unused]] static ShaderOutput *hair_bsdf_material(ShaderGraph &g,unsigned test){
  const ClosureType R=CLOSURE_BSDF_HAIR_REFLECTION_ID,T=CLOSURE_BSDF_HAIR_TRANSMISSION_ID;const float3 brown=make_float3(.45f,.27f,.12f);
  const NodePrincipledHairParametrization DIRECT=NODE_PRINCIPLED_HAIR_REFLECTANCE,MELANIN=NODE_PRINCIPLED_HAIR_PIGMENT_CONCENTRATION,ABSORPTION=NODE_PRINCIPLED_HAIR_DIRECT_ABSORPTION;
  const float3 white=make_float3(1,1,1),soft=make_float3(.8f,.5f,.3f);const float3 sig=make_float3(.8f,1.2f,2.1f);
  switch(test){
    case 0:return hair_bsdf(g,R,.2f,.3f,.1f,brown);
    case 1:return hair_bsdf(g,T,.2f,.3f,.1f,brown);
    case 2:return bsdf_mix(g,hair_bsdf(g,R,.1f,.4f,-.05f,brown),hair_bsdf(g,T,.5f,.2f,.05f,soft),.5f);
    case 3:return hair_bsdf(g,R,.35f,.15f,.2f,brown,true);
    case 4:return hair_bsdf(g,T,.15f,.35f,-.2f,soft,true);
    case 5:return hair_bsdf(g,R,.0001f,1,.03f,brown);
    case 6:return hair_bsdf(g,T,1,.0001f,-.03f,brown);
    case 7:return bsdf_mix(g,hair_bsdf(g,R,.25f,.25f,0,brown),bsdf_diffuse(g),.6f);
    case 8:return bsdf_add(g,hair_bsdf(g,T,.3f,.1f,.02f,soft),bsdf_emission(g));
    // No Transparent BSDF in this fixture: a curve shader with one asks for curve shadow transparency, refused until that lands.
    case 9:return bsdf_mix(g,hair_bsdf(g,R,.6f,.6f,.3f,brown),bsdf_glossy(g,CLOSURE_BSDF_MICROFACET_GGX_ID,.3f),.5f);
    // Chiang: direct colouring, melanin, absorption; rough and smooth (Mp's two branches at v = 0.1), coat, tilt, IOR, random input and attribute.
    case 10:return hair_chiang(g,{DIRECT,soft,white,sig,0,0,0,.3f,.3f,0,0,1.55f,.035f,false});
    case 11:return hair_chiang(g,{MELANIN,soft,white,sig,.8f,.2f,0,.3f,.4f,0,0,1.55f,.035f,false});
    case 12:return hair_chiang(g,{ABSORPTION,soft,white,sig,0,0,0,.25f,.3f,0,0,1.55f,.035f,false});
    case 13:return bsdf_mix(g,hair_chiang(g,{MELANIN,soft,make_float3(.9f,.6f,.4f),sig,.5f,.7f,.4f,.2f,.5f,.3f,.2f,1.45f,.07f,false}),bsdf_glossy(g,CLOSURE_BSDF_MICROFACET_GGX_ID,.25f),.5f);
    case 14:return hair_chiang(g,{DIRECT,make_float3(.9f,.2f,.05f),white,sig,0,0,0,.4f,.2f,.5f,.1f,1.3f,-.05f,true});
    case 15:return hair_chiang(g,{ABSORPTION,soft,white,make_float3(.05f,.3f,.9f),0,0,0,.05f,.1f,0,.5f,1.6f,.02f,false});
#ifdef CBC_HAIR_RENDER
    // The independent curve image keeps Coat below 1: at Coat 1 the primary reflection's variance sits at its clamp
    // (about 5e-7 after the fit), Mp's exponent scales a ray's last bit by about 2e6, and an independent CPU path a
    // last bit away from the GPU's differs past the practical tolerance (native/DEVICE.md item 77; graph 20 reproduces
    // it). The state gates keep Coat 1 here: with identical inputs the device matches.
    case 16:return hair_chiang(g,{MELANIN,soft,white,sig,.999f,1,1,1,1,1,.9f,2.2f,.2f,true});
    case 20:return hair_chiang(g,{MELANIN,soft,white,sig,.999f,1,1,1,1,1,1,2.2f,.2f,true});
#else
    case 16:return hair_chiang(g,{MELANIN,soft,white,sig,.999f,1,1,1,1,1,1,2.2f,.2f,true});
#endif
    case 17:return hair_chiang(g,{DIRECT,make_float3(.02f,.5f,.98f),white,sig,0,0,0,.002f,.002f,0,0,1.55f,0,false});
    case 18:return hair_chiang(g,{MELANIN,soft,make_float3(.3f,.9f,.6f),sig,.2f,0,.6f,.65f,.8f,.2f,.8f,1.1f,-.12f,false});
    default:return bsdf_mix(g,bsdf_mix(g,hair_chiang(g,{ABSORPTION,soft,white,make_float3(2,.1f,.6f),0,0,0,.15f,.25f,.4f,.3f,1.5f,.05f,false}),hair_bsdf(g,R,.2f,.2f,.1f,brown),.5f),bsdf_diffuse(g),.3f);
  }
}
static constexpr unsigned hair_fixture_keys=3;
[[maybe_unused]] static Hair *hair_fixture_geometry(Scene &scene,const std::vector<Node*> &shaders,CurveShapeType shape,bool random_attribute,float skew){
  auto hair=scene.create_node<Hair>();array<Node*> slots;for(auto sh:shaders)slots.push_back_slow(sh);hair->set_used_shaders(slots);hair->curve_shape=shape;
  const int curves=int(shaders.size());hair->resize_curves(curves,curves*hair_fixture_keys);
  auto P=hair->attributes.find(ATTR_STD_POSITION)->data_for_write<packed_float3>();auto R=hair->attributes.find(ATTR_STD_RADIUS)->data_for_write<float>();
  for(int t=0;t<curves;t++){const float x=float(t%4)*2.3f,y=float(t/4)*2.1f;
    for(unsigned k=0;k<hair_fixture_keys;k++){const float a=float(k)*.8f;P[t*hair_fixture_keys+k]=make_float3(x+a*(.6f+skew),y+a*.5f+.2f*k*k,.3f-a*(.3f+.1f*(t%3)));R[t*hair_fixture_keys+k]=.09f-.02f*k;}
    hair->get_curve_first_key()[t]=t*hair_fixture_keys;hair->get_curve_shader()[t]=t;}
  hair->tag_curve_first_key_modified();hair->tag_curve_shader_modified();
  if(random_attribute){auto values=hair->attributes.add(ATTR_STD_CURVE_RANDOM)->data_for_write<float>();for(int t=0;t<curves;t++)values[t]=.07f+.043f*float(t);}
  return hair;
}
// Three objects per shape over two geometries: the instances keep object space, the single-user geometry bakes its transform.
[[maybe_unused]] static void hair_fixture_objects(Scene &scene,const std::vector<Node*> &shaders){
  const CurveShapeType shapes[]={CURVE_RIBBON,CURVE_THICK,CURVE_THICK_LINEAR};
  for(unsigned s=0;s<3;s++){auto shared=hair_fixture_geometry(scene,shaders,shapes[s],s!=1,0);
    for(unsigned i=0;i<3;i++){auto ob=scene.create_node<Object>();ob->set_geometry(i<2?shared:hair_fixture_geometry(scene,shaders,shapes[s],true,.2f));
      ob->set_tfm(transform_translate(make_float3(-4,-4,float(i)*2-float(s)))*transform_rotate(.17f*i,make_float3(0,1,0))*transform_scale(make_float3(i==1?-1.2f:1.1f,.8f,1.4f)));}}
}
