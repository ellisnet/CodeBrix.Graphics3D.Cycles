// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Remaining SVM BSDF families on the native geometry/state fixture, real native
// shader graphs (SVM compiler), alone and mixed with each other and with the
// closures the device already had. CBC_BSDF_FIXTURE 1: Metallic (physical
// conductor and F82 tint; GGX, multiscatter GGX and Beckmann; F82 thin film) and
// Beckmann glossy/anisotropic/refraction/glass. CBC_BSDF_FIXTURE 2:
// Ashikhmin-Shirley. CBC_BSDF_FIXTURE 3: Ashikhmin velvet and the diffuse and
// glossy Toon BSDFs. Three fixtures keep each gate's surface entries inside the
// Intel compile envelope.
[[maybe_unused]] static ShaderOutput *bsdf_tangent(ShaderGraph &g,ShaderNode *node){
  auto tangent=g.create_node<ColorNode>();tangent->set_value(make_float3(.8,.3,.2));g.connect(tangent->output("Color"),node->input("Tangent"));return nullptr;
}
[[maybe_unused]] static ShaderOutput *bsdf_mix(ShaderGraph &g,ShaderOutput *a,ShaderOutput *b,float fac){
  auto mix=g.create_node<MixClosureNode>();mix->set_fac(fac);g.connect(a,mix->input("Closure1"));g.connect(b,mix->input("Closure2"));return mix->output("Closure");
}
[[maybe_unused]] static ShaderOutput *bsdf_add(ShaderGraph &g,ShaderOutput *a,ShaderOutput *b){
  auto add=g.create_node<AddClosureNode>();g.connect(a,add->input("Closure1"));g.connect(b,add->input("Closure2"));return add->output("Closure");
}
[[maybe_unused]] static ShaderOutput *bsdf_metallic(ShaderGraph &g,ClosureType fresnel,ClosureType distribution,float roughness,float anisotropy=0,float rotation=0,float film=0){
  auto m=g.create_node<MetallicBsdfNode>();m->set_fresnel_type(fresnel);m->set_distribution(distribution);m->set_roughness(roughness);
  m->set_color(make_float3(.9f,.62f,.31f));m->set_edge_tint(make_float3(.95f,.86f,.7f));
  m->set_ior(make_float3(.18f,.42f,1.37f));m->set_k(make_float3(3.42f,2.35f,1.77f));
  m->set_anisotropy(anisotropy);m->set_rotation(rotation);m->set_thin_film_thickness(film);m->set_thin_film_ior(1.45f);
  if(anisotropy!=0)bsdf_tangent(g,m);
  return m->output("BSDF");
}
[[maybe_unused]] static ShaderOutput *bsdf_glossy(ShaderGraph &g,ClosureType distribution,float roughness,float anisotropy=0,float rotation=0){
  auto n=g.create_node<GlossyBsdfNode>();n->set_distribution(distribution);n->set_roughness(roughness);n->set_color(make_float3(.3f,.6f,.85f));
  n->set_anisotropy(anisotropy);n->set_rotation(rotation);if(anisotropy!=0)bsdf_tangent(g,n);return n->output("BSDF");
}
[[maybe_unused]] static ShaderOutput *bsdf_diffuse(ShaderGraph &g,float roughness=.43f){
  auto d=g.create_node<DiffuseBsdfNode>();d->set_roughness(roughness);d->set_color(make_float3(.7,.4,.2));return d->output("BSDF");
}
[[maybe_unused]] static ShaderOutput *bsdf_sheen(ShaderGraph &g,float sigma,float3 color){
  auto n=g.create_node<SheenBsdfNode>();n->set_distribution(CLOSURE_BSDF_ASHIKHMIN_VELVET_ID);n->set_roughness(sigma);n->set_color(color);return n->output("BSDF");
}
[[maybe_unused]] static ShaderOutput *bsdf_toon(ShaderGraph &g,ClosureType component,float size,float smooth,float3 color){
  auto n=g.create_node<ToonBsdfNode>();n->set_component(component);n->set_size(size);n->set_smooth(smooth);n->set_color(color);return n->output("BSDF");
}
[[maybe_unused]] static ShaderOutput *bsdf_transparent(ShaderGraph &g){auto t=g.create_node<TransparentBsdfNode>();t->set_color(make_float3(.7,.8,.9));return t->output("BSDF");}
[[maybe_unused]] static ShaderOutput *bsdf_emission(ShaderGraph &g){auto e=g.create_node<EmissionNode>();e->set_color(make_float3(.1,.2,.3));e->set_strength(1.5f);return e->output("Emission");}
[[maybe_unused]] static ShaderOutput *bsdf_refraction(ShaderGraph &g,float roughness,float ior){
  auto n=g.create_node<RefractionBsdfNode>();n->set_distribution(CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID);n->set_roughness(roughness);n->set_IOR(ior);
  n->set_color(make_float3(.9f,.95f,1));return n->output("BSDF");
}
[[maybe_unused]] static ShaderOutput *bsdf_glass(ShaderGraph &g,float roughness,float ior){
  auto n=g.create_node<GlassBsdfNode>();n->set_distribution(CLOSURE_BSDF_MICROFACET_BECKMANN_GLASS_ID);n->set_roughness(roughness);n->set_IOR(ior);
  n->set_color(make_float3(.8f,.9f,.95f));return n->output("BSDF");
}
[[maybe_unused]] static ShaderOutput *bsdf_material(ShaderGraph &g,unsigned test){
  [[maybe_unused]] const ClosureType F82=CLOSURE_BSDF_F82_CONDUCTOR,PHYS=CLOSURE_BSDF_PHYSICAL_CONDUCTOR;
  [[maybe_unused]] const ClosureType GGX=CLOSURE_BSDF_MICROFACET_GGX_ID,MULTI=CLOSURE_BSDF_MICROFACET_MULTI_GGX_ID,BECK=CLOSURE_BSDF_MICROFACET_BECKMANN_ID;
#if CBC_BSDF_FIXTURE==1
  switch(test){
    case 0:return bsdf_metallic(g,F82,GGX,.35f);
    case 1:return bsdf_metallic(g,F82,MULTI,.5f,.6f,.2f);
    case 2:return bsdf_metallic(g,PHYS,GGX,.25f);
    case 3:return bsdf_metallic(g,PHYS,MULTI,.6f);
    case 4:return bsdf_add(g,bsdf_mix(g,bsdf_metallic(g,F82,MULTI,0),bsdf_diffuse(g),.4f),bsdf_transparent(g));
    case 5:return bsdf_mix(g,bsdf_metallic(g,PHYS,BECK,.45f,.9f,-.3f),bsdf_diffuse(g),.3f);
    case 6:return bsdf_glossy(g,BECK,.3f);
    case 7:return bsdf_glossy(g,BECK,.5f,-.5f,.15f);
    case 8:return bsdf_refraction(g,.2f,1.45f);
    case 9:return bsdf_glass(g,.35f,1.5f);
    case 10:return bsdf_glossy(g,BECK,.05f,.8f,-.2f);
    case 11:return bsdf_mix(g,bsdf_refraction(g,.6f,1.33f),bsdf_diffuse(g),.45f);
    case 12:return bsdf_glass(g,0,1.45f);
    case 13:return bsdf_metallic(g,PHYS,GGX,.4f,.5f,.35f);
    case 14:return bsdf_metallic(g,F82,BECK,.2f);
    case 15:return bsdf_mix(g,bsdf_glossy(g,BECK,.25f),bsdf_glossy(g,GGX,.25f),.5f);
    case 16:return bsdf_add(g,bsdf_metallic(g,PHYS,BECK,.3f),bsdf_emission(g));
    case 17:return bsdf_add(g,bsdf_metallic(g,F82,MULTI,.4f,0,0,300),bsdf_emission(g));
    case 18:return bsdf_mix(g,bsdf_glossy(g,BECK,0),bsdf_metallic(g,F82,GGX,.3f),.5f);
    default:return bsdf_mix(g,bsdf_glass(g,.2f,1.33f),bsdf_metallic(g,PHYS,GGX,.3f),.5f);
  }
#elif CBC_BSDF_FIXTURE==2
  const ClosureType AS=CLOSURE_BSDF_ASHIKHMIN_SHIRLEY_ID;
  const auto glossy=[&](ClosureType kind,float roughness,float anisotropy=0,float rotation=0){return bsdf_glossy(g,kind,roughness,anisotropy,rotation);};
  switch(test){
    case 0:return glossy(AS,.3f);
    case 1:return bsdf_mix(g,glossy(AS,.45f,.6f,.1f),bsdf_diffuse(g),.35f);
    case 2:return bsdf_mix(g,glossy(AS,.0001f),glossy(GGX,.3f),.5f);
    case 3:return glossy(AS,1);
    case 4:return bsdf_add(g,glossy(AS,.2f),bsdf_transparent(g));
    case 5:return glossy(AS,.35f,-.7f,-.4f);
    case 6:return bsdf_mix(g,glossy(GGX,.3f),glossy(AS,.3f),.5f);
    case 7:return bsdf_add(g,glossy(AS,.6f,.3f,.25f),bsdf_emission(g));
    case 8:return glossy(AS,.05f);
    case 9:return bsdf_mix(g,glossy(AS,.15f,.9f,0),bsdf_diffuse(g,0),.5f);
    case 10:return glossy(AS,.8f,-.2f,.5f);
    case 11:return bsdf_mix(g,glossy(AS,.25f),glossy(AS,.5f,.5f,.1f),.4f);
    case 12:return bsdf_add(g,bsdf_mix(g,glossy(AS,.4f),bsdf_diffuse(g),.5f),bsdf_transparent(g));
    case 13:return glossy(AS,.0001f,.5f,0);
    case 14:return bsdf_mix(g,glossy(AS,.3f,.99f,.2f),glossy(GGX,.5f),.3f);
    case 15:return glossy(AS,.12f,-.4f,.3f);
    case 16:{auto t=g.create_node<TranslucentBsdfNode>();t->set_color(make_float3(.6,.2,.1));return bsdf_mix(g,t->output("BSDF"),glossy(AS,.3f),.5f);}
    case 17:return glossy(AS,.5f);
    case 18:return bsdf_mix(g,glossy(AS,.7f),bsdf_diffuse(g),.7f);
    default:return bsdf_add(g,glossy(AS,.5f,.4f,.3f),bsdf_transparent(g));
  }
#else
  const ClosureType DTOON=CLOSURE_BSDF_DIFFUSE_TOON_ID,GTOON=CLOSURE_BSDF_GLOSSY_TOON_ID;
  switch(test){
    case 0:return bsdf_sheen(g,.4f,make_float3(.8f,.3f,.5f));
    case 1:return bsdf_mix(g,bsdf_sheen(g,1,make_float3(.4f,.7f,.3f)),bsdf_diffuse(g),.5f);
    case 2:return bsdf_add(g,bsdf_sheen(g,.01f,make_float3(.6f,.5f,.9f)),bsdf_transparent(g));
    case 3:return bsdf_toon(g,DTOON,.5f,.1f,make_float3(.9f,.5f,.2f));
    case 4:return bsdf_toon(g,GTOON,.3f,.2f,make_float3(.6f,.8f,.9f));
    case 5:return bsdf_add(g,bsdf_mix(g,bsdf_toon(g,DTOON,.8f,0,make_float3(.5f,.6f,.7f)),bsdf_toon(g,GTOON,.1f,.05f,make_float3(.9f,.9f,.8f)),.3f),bsdf_transparent(g));
    case 6:return bsdf_toon(g,DTOON,1,1,make_float3(.7f,.6f,.5f));
    case 7:return bsdf_toon(g,GTOON,.01f,0,make_float3(.8f,.8f,.6f));
    case 8:return bsdf_mix(g,bsdf_glossy(g,GGX,.2f),bsdf_sheen(g,.6f,make_float3(.6f,.5f,.9f)),.5f);
    case 9:return bsdf_add(g,bsdf_mix(g,bsdf_toon(g,DTOON,.4f,.3f,make_float3(.4f,.5f,.6f)),bsdf_sheen(g,.5f,make_float3(.9f,.6f,.4f)),.5f),bsdf_emission(g));
    case 10:return bsdf_sheen(g,.2f,make_float3(.5f,.9f,.6f));
    case 11:return bsdf_mix(g,bsdf_diffuse(g),bsdf_toon(g,GTOON,.2f,.1f,make_float3(.9f,.9f,.9f)),.6f);
    case 12:return bsdf_mix(g,bsdf_mix(g,bsdf_sheen(g,.3f,make_float3(.7f,.4f,.6f)),bsdf_glossy(g,GGX,.4f),.5f),bsdf_toon(g,DTOON,.6f,.2f,make_float3(.5f,.7f,.4f)),.4f);
    case 13:{auto t=g.create_node<TranslucentBsdfNode>();t->set_color(make_float3(.6,.2,.1));return bsdf_mix(g,t->output("BSDF"),bsdf_sheen(g,.5f,make_float3(.5f,.5f,.8f)),.5f);}
    case 14:{auto rt=g.create_node<RadialTilingNode>();rt->set_r_gon_sides(3.5f);rt->set_r_gon_roundness(.6f);
      auto geometry=g.create_node<GeometryNode>();g.connect(geometry->output("Position"),rt->input("Vector"));
      auto combine=g.create_node<CombineXYZNode>();g.connect(rt->output("Segment ID"),combine->input("X"));g.connect(rt->output("Segment Width"),combine->input("Y"));
      g.connect(rt->output("Segment Rotation"),combine->input("Z"));auto d=g.create_node<DiffuseBsdfNode>();g.connect(combine->output("Vector"),d->input("Color"));return d->output("BSDF");}
    case 15:return bsdf_toon(g,GTOON,.7f,.3f,make_float3(.8f,.6f,.9f));
    case 16:{auto rt=g.create_node<RadialTilingNode>();rt->set_r_gon_sides(5);rt->set_r_gon_roundness(.3f);rt->set_use_normalize(true);
      auto geometry=g.create_node<GeometryNode>();g.connect(geometry->output("Position"),rt->input("Vector"));
      auto toon=g.create_node<ToonBsdfNode>();toon->set_component(CLOSURE_BSDF_DIFFUSE_TOON_ID);toon->set_size(.5f);toon->set_smooth(.2f);
      g.connect(rt->output("Segment Coordinates"),toon->input("Color"));return toon->output("BSDF");}
    case 17:return bsdf_mix(g,bsdf_toon(g,GTOON,.4f,0,make_float3(.7f,.7f,.7f)),bsdf_sheen(g,.9f,make_float3(.3f,.6f,.9f)),.5f);
    case 18:return bsdf_mix(g,bsdf_toon(g,DTOON,.3f,.6f,make_float3(.6f,.4f,.3f)),bsdf_diffuse(g,0),.5f);
    default:return bsdf_add(g,bsdf_mix(g,bsdf_toon(g,GTOON,.25f,.15f,make_float3(.8f,.8f,.9f)),bsdf_sheen(g,.45f,make_float3(.5f,.6f,.7f)),.5f),bsdf_transparent(g));
  }
#endif
}
