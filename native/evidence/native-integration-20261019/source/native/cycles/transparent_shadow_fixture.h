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
  graph->connect(surface,graph->output()->input("Surface"));auto shader=scene.create_node<Shader>();shader->set_graph(std::move(graph));shader->tag_update(&scene);return shader;
}
