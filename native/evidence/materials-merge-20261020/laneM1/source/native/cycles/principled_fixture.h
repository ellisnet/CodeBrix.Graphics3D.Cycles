// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Real native Principled BSDF graphs: a parameter sweep over IOR (including
// below one and near one), roughness (singular to rough), metallic F82 tint,
// coat (IOR, roughness, tint, normal), sheen (including invalid-LTC low
// roughness), transmission (including ior < 1 backfaces), specular IOR level and
// tint, anisotropy with tangent/rotation, alpha, emission, diffuse roughness,
// both GGX distributions, thin-film iridescence (dielectric, metallic, sub-1 nm
// blend), thin-walled transmission and closure mixtures. Subsurface stays zero
// here; its rejection is a separate gate case.
struct PrincipledCase {float metallic,roughness,ior,transmission,coat,coat_roughness,coat_ior,sheen,sheen_roughness,level,anisotropic,alpha,emission,diffuse_roughness;bool multi,tinted;};
static const PrincipledCase principled_cases[]={
  {0,.5f,1.45f,0,0,0,1.5f,0,0,.5f,0,1,0,0,true,false},       // dielectric default
  {1,.2f,1.45f,0,0,0,1.5f,0,0,.5f,0,1,0,0,true,true},         // tinted metal (F82)
  {0,0,1.5f,1,0,0,1.5f,0,0,.5f,0,1,0,0,true,false},           // smooth glass
  {0,.35f,1.33f,1,0,0,1.5f,0,0,.5f,0,1,0,.5f,false,true},     // rough tinted glass, single GGX
  {0,.4f,1.45f,0,1,.1f,1.5f,0,0,.5f,0,1,0,0,true,true},       // clear coat, tinted
  {0,.6f,1.45f,0,0,0,1.5f,1,.3f,.5f,0,1,0,1,true,true},       // sheen
  {.5f,.3f,2.4f,.5f,.3f,.6f,2,.5f,1,.8f,.5f,1,0,.3f,true,true},// everything mixed
  {0,.003f,1.00001f,0,0,0,1.5f,0,0,.5f,0,1,0,0,true,false},  // near-unit IOR, near-singular
  {0,.25f,.7f,.6f,0,0,1.5f,0,0,.5f,0,1,0,0,true,true},        // IOR below one
  {0,.5f,1,0,0,0,1.5f,0,0,.5f,0,1,0,0,true,false},            // eta == 1: no specular lobe
  {0,.45f,1.45f,0,0,0,1.5f,0,0,0,0,1,0,.2f,true,true},        // specular IOR level 0
  {0,.15f,1.45f,0,0,0,1.5f,0,0,1,1,1,0,0,true,true},          // anisotropic, level 1
  {.8f,.5f,1.45f,0,0,0,1.5f,0,0,.5f,.7f,1,0,0,false,true},    // anisotropic metal, single GGX
  {0,.5f,1.45f,0,0,0,1.5f,0,0,.5f,0,.6f,0,0,true,false},      // alpha 0.6
  {0,.5f,1.45f,0,.5f,.2f,1,.4f,0,.5f,0,0,0,0,true,true},      // alpha 0, coat IOR 1, sheen roughness 0
  {0,.5f,1.45f,0,.7f,.3f,1.6f,.6f,.5f,.5f,0,1,3,.4f,true,true},// emission under coat and sheen
  {1,1,1.45f,0,0,0,1.5f,0,0,.5f,0,1,0,0,true,false},          // white rough metal
  {0,1,2.4f,1,1,1,3,1,1,1,1,1,1,1,true,true},                 // extremes
  {.3f,.05f,1.2f,.3f,.2f,.05f,1.3f,.2f,.8f,.6f,.2f,.8f,.5f,.6f,false,true},
  {0,.7f,1.45f,0,0,0,1.5f,0,0,.5f,0,1,0,0,true,true}};
static ShaderOutput *principled_material(ShaderGraph &g,unsigned test){
  const auto &k=principled_cases[test];auto node=g.create_node<PrincipledBsdfNode>();
  node->set_distribution(k.multi?CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID:CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID);
  node->set_base_color(make_float3(.8f,.45f+.02f*test,.2f));node->set_metallic(k.metallic);node->set_roughness(k.roughness);node->set_ior(k.ior);
  node->set_transmission_weight(k.transmission);node->set_coat_weight(k.coat);node->set_coat_roughness(k.coat_roughness);node->set_coat_ior(k.coat_ior);
  node->set_sheen_weight(k.sheen);node->set_sheen_roughness(k.sheen_roughness);node->set_specular_ior_level(k.level);node->set_anisotropic(k.anisotropic);
  node->set_alpha(k.alpha);node->set_emission_strength(k.emission);node->set_emission_color(make_float3(1,.6f,.3f));node->set_diffuse_roughness(k.diffuse_roughness);
  node->set_subsurface_weight(0);node->set_thin_film_thickness(test==12?350.f:test==17?800.f:test==6?.6f:test==19?.1f:0);node->set_thin_film_ior(test==17?1.6f:1.33f);
  node->set_thin_wall(test==2||test==18);
  if(k.tinted){node->set_specular_tint(make_float3(.9f,.7f,.5f));node->set_coat_tint(make_float3(.6f,.8f,.9f));node->set_sheen_tint(make_float3(.9f,.5f,.7f));}
  if(k.anisotropic>0){node->set_anisotropic_rotation(.17f*float(test%5)-.3f);
    auto tangent=g.create_node<ColorNode>();tangent->set_value(make_float3(.8,.3,.2));g.connect(tangent->output("Color"),node->input("Tangent"));}
  if(test==6||test==17){auto normal=g.create_node<ColorNode>();normal->set_value(make_float3(.2,.3,1));g.connect(normal->output("Color"),node->input("Coat Normal"));}
  ShaderOutput *result=node->output("BSDF");
  if(test==7||test==18){auto diffuse=g.create_node<DiffuseBsdfNode>();diffuse->set_roughness(.43f);diffuse->set_color(make_float3(.7,.4,.2));
    auto mix=g.create_node<MixClosureNode>();mix->set_fac(.37f);g.connect(result,mix->input("Closure1"));g.connect(diffuse->output("BSDF"),mix->input("Closure2"));result=mix->output("Closure");}
  if(test==13){auto transparent=g.create_node<TransparentBsdfNode>();auto add=g.create_node<AddClosureNode>();g.connect(result,add->input("Closure1"));g.connect(transparent->output("BSDF"),add->input("Closure2"));result=add->output("Closure");}
  if(test==19){auto glass=g.create_node<GlassBsdfNode>();glass->set_distribution(CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID);glass->set_roughness(.2f);glass->set_IOR(1.5f);
    auto add=g.create_node<AddClosureNode>();g.connect(result,add->input("Closure1"));g.connect(glass->output("BSDF"),add->input("Closure2"));result=add->output("Closure");}
  return result;
}
