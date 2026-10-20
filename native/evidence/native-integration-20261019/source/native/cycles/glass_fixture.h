// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Real native shader graphs exercise singular/rough, near-unit/backfacing IOR,
// TIR, reflection/refraction, colored glass and closure mixtures.
static ShaderOutput *glass_material(ShaderGraph &g,unsigned test){
  const float rough[]={0,.003f,.004f,.02f,.15f,.35f,.65f,1,-.2f,1.2f};
  const float ior[]={1,1.00001f,1.33f,1.5f,.7f,2.4f,1.5f,.8f,1.1f,1.8f};
  ShaderOutput *result;
  if(test<10){auto node=g.create_node<RefractionBsdfNode>();node->set_distribution(CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID);
    node->set_roughness(rough[test]);node->set_IOR(ior[test]);node->set_color(make_float3(.2,.6,.9));result=node->output("BSDF");}
  else{auto node=g.create_node<GlassBsdfNode>();node->set_distribution(CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID);
    node->set_roughness(rough[test%10]);node->set_IOR(ior[test%10]);node->set_color(test%5==0?one_float3():make_float3(.2,.6,.9));result=node->output("BSDF");}
  if(test%10>=5){auto diffuse=g.create_node<DiffuseBsdfNode>();diffuse->set_roughness(.43f);diffuse->set_color(make_float3(.7,.4,.2));
    auto mix=g.create_node<MixClosureNode>();mix->set_fac(.37f);g.connect(result,mix->input("Closure1"));g.connect(diffuse->output("BSDF"),mix->input("Closure2"));result=mix->output("Closure");}
  if(test%10==7){auto transparent=g.create_node<TransparentBsdfNode>();auto add=g.create_node<AddClosureNode>();g.connect(result,add->input("Closure1"));g.connect(transparent->output("BSDF"),add->input("Closure2"));result=add->output("Closure");}
  if(test%10==9){auto emission=g.create_node<EmissionNode>();emission->set_color(make_float3(.1,.2,.3));auto add=g.create_node<AddClosureNode>();g.connect(result,add->input("Closure1"));g.connect(emission->output("Emission"),add->input("Closure2"));result=add->output("Closure");}
  return result;
}
