// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Real native Principled BSDF graphs with dispersion (CBC_PRINCIPLED_FIXTURE 2), on the principled fixture's geometry,
// rays and cases: the Transmission Dispersion Scale and Abbe Number over their ranges (a scale above one, a zero and a
// negative Abbe number), an IOR below one and near one, both GGX distributions, a scale linked to a texture (zero on
// part of the paths at run time), thin wall and thin film, the other Principled layers beside the transmission lobe,
// closure mixtures, and two graphs that do not disperse (a zero scale; a zero transmission weight). The rays arrive
// from both sides of the triangles, and part of the input paths carry PATH_RAY_SPECTRAL already (surface_gate.cpp).
#if CBC_PRINCIPLED_FIXTURE==3
// The second set (CBC_PRINCIPLED_FIXTURE 3): the dispersion inputs and the inputs the dispersive index of refraction
// depends on, linked to a Checker Texture's Fac (0 or 1 at the shading position) through Math nodes, so that one graph
// takes two values at run time; two dispersive Principled nodes in one shader (one wavelength for both); and the
// dispersive glass beside transparent, holdout and emission closures.
static ShaderOutput *principled_material(ShaderGraph &g,unsigned test){
  auto glass=[&](float scale,float abbe,float ior,float roughness,bool multi){auto n=g.create_node<PrincipledBsdfNode>();
    n->set_distribution(multi?CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID:CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID);
    n->set_base_color(make_float3(.85f,.5f+.02f*test,.3f));n->set_roughness(roughness);n->set_ior(ior);n->set_transmission_weight(1);
    n->set_transmission_dispersion_scale(scale);n->set_transmission_dispersion_abbe_number(abbe);n->set_subsurface_weight(0);return n;};
  auto fac=[&](float scale){auto geo=g.create_node<GeometryNode>();auto checker=g.create_node<CheckerTextureNode>();checker->set_scale(scale);
    g.connect(geo->output("Position"),checker->input("Vector"));return checker->output("Fac");};
  // a * Fac + b.
  auto ramp=[&](ShaderOutput *in,float a,float b){auto m=g.create_node<MathNode>();m->set_math_type(NODE_MATH_MULTIPLY_ADD);m->set_value2(a);m->set_value3(b);
    g.connect(in,m->input("Value1"));return m->output("Value");};
  auto math=[&](NodeMathType type,ShaderOutput *in,float second){auto m=g.create_node<MathNode>();m->set_math_type(type);m->set_value2(second);
    g.connect(in,m->input("Value1"));return m->output("Value");};
  auto mix=[&](ShaderOutput *a,ShaderOutput *b,float f){auto m=g.create_node<MixClosureNode>();m->set_fac(f);g.connect(a,m->input("Closure1"));g.connect(b,m->input("Closure2"));return m;};
  auto add=[&](ShaderOutput *a,ShaderOutput *b){auto m=g.create_node<AddClosureNode>();g.connect(a,m->input("Closure1"));g.connect(b,m->input("Closure2"));return m->output("Closure");};
  auto n=glass(1,20,1.5f,test%3?.1f:0.f,test%2!=0);ShaderOutput *result=n->output("BSDF");
  const char *scale="Transmission Dispersion Scale",*abbe="Transmission Dispersion Abbe Number";
  switch(test){
    case 0:g.connect(ramp(fac(3.7f),30,10),n->input(abbe));break;                      // Abbe number 10 or 40
    case 1:g.connect(fac(2.9f),n->input(scale));n->set_transmission_dispersion_abbe_number(15);break; // scale 0 or 1
    case 2:g.connect(ramp(fac(3.7f),.6f,0),n->input(scale));g.connect(ramp(fac(5.1f),25,5),n->input(abbe));break; // both linked, two textures
    case 3:result=mix(result,glass(.7f,50,1.7f,.3f,true)->output("BSDF"),.4f)->output("Closure");n->set_transmission_dispersion_abbe_number(10);break; // two dispersive nodes mixed
    case 4:result=add(result,glass(1,30,1.6f,.2f,false)->output("BSDF"));n->set_transmission_dispersion_abbe_number(12);break; // two dispersive nodes added
    case 5:{auto transparent=g.create_node<TransparentBsdfNode>();transparent->set_color(make_float3(.7f,.8f,.9f));auto m=mix(result,transparent->output("BSDF"),.5f);
      g.connect(fac(3.7f),m->input("Fac"));result=m->output("Closure");break;}          // dispersive glass or transparent, by the texture
    case 6:g.connect(ramp(fac(3.7f),.6f,1.2f),n->input("IOR"));break;                   // IOR 1.2 or 1.8
    case 7:g.connect(ramp(fac(3.7f),.3f,0),n->input("Roughness"));g.connect(ramp(fac(2.3f),.5f,.5f),n->input("Transmission Weight"));break;
    case 8:g.connect(fac(3.7f),n->input("Transmission Weight"));break;                  // no transmission lobe on half of the paths
    case 9:g.connect(math(NODE_MATH_LESS_THAN,fac(3.7f),-1),n->input(scale));break;     // a linked scale that is 0 on every path
    case 10:{auto emission=g.create_node<EmissionNode>();emission->set_color(make_float3(.1f,.4f,.7f));emission->set_strength(2.3f);result=add(result,emission->output("Emission"));break;}
    case 11:g.connect(ramp(fac(3.7f),.5f,.4f),n->input("Alpha"));break;                 // alpha 0.4 or 0.9
    case 12:g.connect(ramp(fac(3.7f),3,0),n->input(scale));break;                       // scale 0 or 3 (saturated)
    case 13:g.connect(ramp(fac(3.7f),1,-.5f),n->input(scale));break;                    // scale -0.5 (saturated to 0) or 0.5
    case 14:g.connect(ramp(fac(3.7f),40,-10),n->input(abbe));break;                     // Abbe number -10 (fmaxf gives 0) or 30
    case 15:n->set_coat_weight(.8f);n->set_coat_roughness(.2f);n->set_coat_ior(1.6f);n->set_sheen_weight(.5f);n->set_sheen_roughness(.4f);
      g.connect(fac(3.7f),n->input(scale));break;                                       // under coat and sheen, a linked scale
    case 16:{auto holdout=g.create_node<HoldoutNode>();result=mix(result,holdout->output("Holdout"),.5f)->output("Closure");break;}
    case 17:g.connect(ramp(fac(3.7f),1e-3f,0),n->input("Roughness"));n->set_transmission_dispersion_abbe_number(8);break; // singular or near-singular
    case 18:{n->set_metallic(.5f);n->set_anisotropic(.6f);n->set_anisotropic_rotation(.21f);auto tangent=g.create_node<ColorNode>();tangent->set_value(make_float3(.8,.3,.2));
      g.connect(tangent->output("Color"),n->input("Tangent"));g.connect(fac(3.7f),n->input(scale));break;}
    default:{auto d=g.create_node<MathNode>();d->set_math_type(NODE_MATH_DIVIDE);d->set_value1(4);g.connect(fac(3.7f),d->input("Value2"));
      g.connect(d->output("Value"),n->input(abbe));break;}                              // Abbe number 4 / Fac: safe_divide gives 0, or 4
  }
  return result;
}
#else
struct DispersionCase {float scale,abbe,ior,roughness,transmission;bool multi,tinted;};
static const DispersionCase dispersion_cases[]={
  {1,20,1.5f,0,1,true,false},          // smooth glass, a low Abbe number
  {.5f,30,1.45f,.3f,1,false,false},    // rough glass, half scale, single GGX
  {.8f,45,1.33f,.1f,.6f,true,true},    // partial transmission, tinted
  {1,0,1.5f,.05f,1,true,false},        // Abbe number 0: safe_divide gives 0, nothing disperses
  {2,5,1.5f,.02f,1,true,false},        // scale above one (saturated), a very low Abbe number
  {.3f,-3,1.5f,.2f,1,true,false},      // negative Abbe number: fmaxf gives 0
  {1,25,.7f,.15f,1,true,true},         // IOR below one
  {1,10,1.00001f,.003f,1,true,false},  // near-unit IOR, near-singular
  {0,20,1.5f,.1f,1,true,false},        // scale 0: the shader needs no wavelength
  {1,20,1.5f,.4f,0,true,false},        // transmission weight 0: the shader needs no wavelength
  {1,20,1.5f,.1f,1,true,false},        // scale linked to a checker texture (below)
  {1,20,1.5f,.1f,1,true,false},        // thin wall: no dispersive closure is made
  {.7f,15,1.5f,.2f,1,true,true},       // under coat, sheen and emission
  {1,20,1.45f,.2f,.8f,true,true},      // beside a metallic lobe
  {1,12,1.5f,.25f,1,false,false},      // anisotropic
  {1,18,1.5f,.05f,1,true,false},       // thin film
  {1,20,1.5f,.1f,1,true,false},        // mixed with a diffuse closure
  {1,20,1.5f,.1f,1,true,false},        // added to a Glass BSDF
  {1,22,1.5f,.1f,1,true,false},        // alpha 0.6
  {1,60,2.4f,1,1,true,true}};          // rough, a high IOR
static ShaderOutput *principled_material(ShaderGraph &g,unsigned test){
  const auto &k=dispersion_cases[test];auto node=g.create_node<PrincipledBsdfNode>();
  node->set_distribution(k.multi?CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID:CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID);
  node->set_base_color(make_float3(.8f,.45f+.02f*test,.2f));node->set_roughness(k.roughness);node->set_ior(k.ior);
  node->set_transmission_weight(k.transmission);node->set_transmission_dispersion_scale(k.scale);node->set_transmission_dispersion_abbe_number(k.abbe);
  node->set_subsurface_weight(0);node->set_metallic(test==13?.5f:0);node->set_thin_wall(test==11);node->set_alpha(test==18?.6f:1);
  node->set_thin_film_thickness(test==15?400.f:0);node->set_thin_film_ior(1.33f);
  if(test==12){node->set_coat_weight(.7f);node->set_coat_roughness(.3f);node->set_coat_ior(1.6f);node->set_sheen_weight(.6f);node->set_sheen_roughness(.5f);
    node->set_emission_strength(3);node->set_emission_color(make_float3(1,.6f,.3f));}
  if(k.tinted){node->set_specular_tint(make_float3(.9f,.7f,.5f));node->set_coat_tint(make_float3(.6f,.8f,.9f));node->set_sheen_tint(make_float3(.9f,.5f,.7f));}
  if(test==10){auto geo=g.create_node<GeometryNode>();auto checker=g.create_node<CheckerTextureNode>();checker->set_scale(3.7f);
    g.connect(geo->output("Position"),checker->input("Vector"));g.connect(checker->output("Fac"),node->input("Transmission Dispersion Scale"));}
  if(test==14){node->set_anisotropic(.6f);node->set_anisotropic_rotation(.21f);
    auto tangent=g.create_node<ColorNode>();tangent->set_value(make_float3(.8,.3,.2));g.connect(tangent->output("Color"),node->input("Tangent"));}
  ShaderOutput *result=node->output("BSDF");
  if(test==16){auto diffuse=g.create_node<DiffuseBsdfNode>();diffuse->set_roughness(.43f);diffuse->set_color(make_float3(.7,.4,.2));
    auto mix=g.create_node<MixClosureNode>();mix->set_fac(.37f);g.connect(result,mix->input("Closure1"));g.connect(diffuse->output("BSDF"),mix->input("Closure2"));result=mix->output("Closure");}
  if(test==17){auto glass=g.create_node<GlassBsdfNode>();glass->set_distribution(CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID);glass->set_roughness(.2f);glass->set_IOR(1.5f);
    auto add=g.create_node<AddClosureNode>();g.connect(result,add->input("Closure1"));g.connect(glass->output("BSDF"),add->input("Closure2"));result=add->output("Closure");}
  return result;
}
#endif
