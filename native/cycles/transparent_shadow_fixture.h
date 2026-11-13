// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "scene/scene.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
// Native graphs, compiled by the original Cycles SVM compiler for both oracles.
static ccl::Shader *transparent_material(ccl::Scene &scene,unsigned mode){
  using namespace ccl;auto graph=make_unique<ShaderGraph>();auto transparent=graph->create_node<TransparentBsdfNode>();
  transparent->set_color(mode==0?one_float3():mode==4?zero_float3():make_float3(.82f,.57f,.31f));
  ShaderOutput *surface=transparent->output("BSDF");
  if(mode==2){auto diffuse=graph->create_node<DiffuseBsdfNode>();diffuse->set_color(make_float3(.2f,.4f,.6f));
    auto mix=graph->create_node<MixClosureNode>();mix->set_fac(.65f);graph->connect(diffuse->output("BSDF"),mix->input("Closure1"));graph->connect(surface,mix->input("Closure2"));surface=mix->output("Closure");}
  if(mode==3){auto geometry=graph->create_node<GeometryNode>();auto checker=graph->create_node<CheckerTextureNode>();checker->set_scale(2.3f);
    checker->set_color1(make_float3(.8f,.3f,.6f));checker->set_color2(make_float3(.2f,.9f,.4f));
    graph->connect(geometry->output("Position"),checker->input("Vector"));graph->connect(checker->output("Color"),transparent->input("Color"));}
  if(mode==5){
    // The Light Path node's depths in a shadow evaluation (LIGHT_PATH_STATE): the pane's colour scaled by
    // 0.4 + 0.1 * Ray Depth + 0.07 * Transparent Depth + 0.03 * Diffuse Depth + 0.05 * Glossy Depth + 0.02 * Transmission Depth.
    auto path=graph->create_node<LightPathNode>();ShaderOutput *sum=nullptr;
    const std::pair<const char*,float> terms[]={{"Ray Depth",.1f},{"Transparent Depth",.07f},{"Diffuse Depth",.03f},{"Glossy Depth",.05f},{"Transmission Depth",.02f}};
    for(auto [name,weight]:terms){auto term=graph->create_node<MathNode>();term->set_math_type(NODE_MATH_MULTIPLY_ADD);term->set_value2(weight);term->set_value3(sum?0.f:.4f);
      graph->connect(path->output(name),term->input("Value1"));
      if(sum){auto add=graph->create_node<MathNode>();add->set_math_type(NODE_MATH_ADD);graph->connect(sum,add->input("Value1"));graph->connect(term->output("Value"),add->input("Value2"));sum=add->output("Value");}
      else sum=term->output("Value");}
    auto scale=graph->create_node<VectorMathNode>();scale->set_math_type(NODE_VECTOR_MATH_SCALE);scale->set_vector1(make_float3(.82f,.57f,.31f));
    graph->connect(sum,scale->input("Scale"));graph->connect(scale->output("Vector"),transparent->input("Color"));}
  graph->connect(surface,graph->output()->input("Surface"));auto shader=scene.create_node<Shader>();shader->set_graph(std::move(graph));shader->tag_update(&scene);return shader;
}
