// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Reuse the native geometry/state fixture, varying only its actual shader graphs.
static ShaderOutput *microfacet_material(ShaderGraph &g,unsigned test){
  auto glossy=g.create_node<GlossyBsdfNode>();
  glossy->set_distribution(test<10?CLOSURE_BSDF_MICROFACET_GGX_ID:CLOSURE_BSDF_MICROFACET_MULTI_GGX_ID);
  const float rough[]={0,.003f,.004f,.02f,.15f,.35f,.65f,1,-.2f,1.2f};
  glossy->set_roughness(rough[test%10]);glossy->set_color(test%5==0?one_float3():make_float3(.2,.6,.9));
  if(test%10>=4&&test%10<=7){
    const float aniso[]={-.99f,-.65f,.65f,.99f};glossy->set_anisotropy(aniso[test%10-4]);
    glossy->set_rotation(.17f*int(test%5)-.3f);
    auto tangent=g.create_node<ColorNode>();tangent->set_value(make_float3(.8,.3,.2));g.connect(tangent->output("Color"),glossy->input("Tangent"));
  }
  ShaderOutput *result=glossy->output("BSDF");
  if(test%10>=5){
    auto diffuse=g.create_node<DiffuseBsdfNode>();diffuse->set_roughness(.43f);diffuse->set_color(make_float3(.7,.4,.2));
    auto mix=g.create_node<MixClosureNode>();mix->set_fac(.37f);g.connect(result,mix->input("Closure1"));g.connect(diffuse->output("BSDF"),mix->input("Closure2"));result=mix->output("Closure");
  }
  if(test%10==7){auto transparent=g.create_node<TransparentBsdfNode>();auto add=g.create_node<AddClosureNode>();g.connect(result,add->input("Closure1"));g.connect(transparent->output("BSDF"),add->input("Closure2"));result=add->output("Closure");}
  if(test%10==9){auto emission=g.create_node<EmissionNode>();emission->set_color(make_float3(.1,.2,.3));auto add=g.create_node<AddClosureNode>();g.connect(result,add->input("Closure1"));g.connect(emission->output("Emission"),add->input("Closure2"));result=add->output("Closure");}
  return result;
}
