// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Principled Hair, Huang model: the materials of the state and image gates. Roughness from the closure's floor up; tilt;
// IOR; circular and elliptical cross-sections on both sides of 1 (setup switches major and minor axis above 1); the
// three colour parametrizations; lobes on and off; a mix with a diffuse closure and with a second hair closure.
#include "scene/scene.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include <memory>
namespace ccl {
struct HuangMaterial {const char *name;float roughness,tilt_degrees,ior,aspect;NodePrincipledHairParametrization parametrization;float R,TT,TRT,random_roughness,random_color;unsigned mix;};
inline const std::vector<HuangMaterial> &huang_materials(){
  static const std::vector<HuangMaterial> materials={
    {"default",.3f,2.f,1.55f,.85f,NODE_PRINCIPLED_HAIR_PIGMENT_CONCENTRATION,1,1,1,0,0,0},
    {"circular",.3f,2.f,1.55f,1.f,NODE_PRINCIPLED_HAIR_PIGMENT_CONCENTRATION,1,1,1,0,0,0},
    {"rough-circular-reflectance",.7f,-4.f,1.4f,1.f,NODE_PRINCIPLED_HAIR_REFLECTANCE,1,1,1,0,0,0},
    {"smooth-absorption",.08f,3.f,1.6f,1.f,NODE_PRINCIPLED_HAIR_DIRECT_ABSORPTION,1,1,1,0,0,0},
    {"ellipse-wide",.25f,1.f,1.5f,1.6f,NODE_PRINCIPLED_HAIR_REFLECTANCE,1,1,1,0,0,0},
    {"ellipse-narrow",.4f,0.f,1.55f,.4f,NODE_PRINCIPLED_HAIR_DIRECT_ABSORPTION,1,1,1,0,0,0},
    {"r-only",.2f,2.f,1.55f,1.f,NODE_PRINCIPLED_HAIR_PIGMENT_CONCENTRATION,1,0,0,0,0,0},
    {"tt-only",.2f,2.f,1.55f,.8f,NODE_PRINCIPLED_HAIR_PIGMENT_CONCENTRATION,0,1,0,0,0,0},
    {"trt-only",.2f,2.f,1.55f,1.f,NODE_PRINCIPLED_HAIR_REFLECTANCE,0,0,1,0,0,0},
    {"lobes-scaled",.35f,2.f,1.55f,.9f,NODE_PRINCIPLED_HAIR_PIGMENT_CONCENTRATION,.5f,1.5f,.25f,0,0,0},
    {"random",.3f,2.f,1.55f,1.f,NODE_PRINCIPLED_HAIR_PIGMENT_CONCENTRATION,1,1,1,.5f,.4f,0},
    {"low-roughness-longitudinal",.02f,2.f,1.55f,1.f,NODE_PRINCIPLED_HAIR_PIGMENT_CONCENTRATION,1,1,1,0,0,0},
    {"mix-diffuse",.3f,2.f,1.55f,.85f,NODE_PRINCIPLED_HAIR_PIGMENT_CONCENTRATION,1,1,1,0,0,1},
    {"mix-two-hairs",.3f,2.f,1.55f,1.f,NODE_PRINCIPLED_HAIR_PIGMENT_CONCENTRATION,1,1,1,0,0,2},
  };
  return materials;
}
inline Shader *huang_shader(Scene &scene,const HuangMaterial &m){
  auto g=make_unique<ShaderGraph>();
  auto hair=[&](const HuangMaterial &h,float scale){
    auto n=g->create_node<PrincipledHairBsdfNode>();n->set_model(NODE_PRINCIPLED_HAIR_HUANG);n->set_parametrization(h.parametrization);
    n->set_roughness(h.roughness*scale);n->set_offset(h.tilt_degrees*3.14159265f/180.f);n->set_ior(h.ior);n->set_aspect_ratio(h.aspect);n->set_R(h.R);n->set_TT(h.TT);n->set_TRT(h.TRT);
    n->set_random_roughness(h.random_roughness);n->set_random_color(h.random_color);n->set_melanin(.6f);n->set_melanin_redness(.7f);n->set_tint(make_float3(.9f,.75f,.6f));
    n->set_color(make_float3(.55f,.3f,.15f));n->set_absorption_coefficient(make_float3(.6f,1.1f,2.2f));n->set_random(.37f);return n->output("BSDF");};
  ShaderOutput *out=hair(m,1);
  if(m.mix){
    auto mix=g->create_node<MixClosureNode>();mix->set_fac(.4f);g->connect(out,mix->input("Closure1"));
    if(m.mix==1){auto d=g->create_node<DiffuseBsdfNode>();d->set_color(make_float3(.5f,.4f,.3f));g->connect(d->output("BSDF"),mix->input("Closure2"));}
    else g->connect(hair(m,.5f),mix->input("Closure2"));
    out=mix->output("Closure");
  }
  g->connect(out,g->output()->input("Surface"));
  auto shader=scene.create_node<Shader>();shader->set_graph(std::move(g));shader->set_use_transparent_shadow(false);shader->tag_update(&scene);return shader;
}
}
