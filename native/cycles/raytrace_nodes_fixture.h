// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Bevel and Raycast node shaders for the raytrace-node gates (ao_state_gate.cpp and
// ao_render_gate.cpp built with CBC_RAYTRACE_NODES_FIXTURE): real native graphs.
// variant 0 Bevel driving a diffuse normal mixed with glossy; 1 Raycast along the
// normal, Is Hit mixing two BSDFs and Hit Distance tinting one; 2 Raycast with Hit
// Position, Hit Normal and a "generated" attribute output at the hit; 3 Bevel with a
// linked normal plus an only_local Raycast into the object (the instanced box);
// 4 Bevel with linked radius and the Raycast's hit normal.
static Shader *raytrace_node_shader(Scene &scene,unsigned variant){
  auto g=make_unique<ShaderGraph>();
  auto d=g->create_node<DiffuseBsdfNode>();d->set_roughness(.2f);d->set_color(make_float3(.6f,.5f,.35f));
  auto glossy=g->create_node<GlossyBsdfNode>();glossy->set_distribution(CLOSURE_BSDF_MICROFACET_GGX_ID);glossy->set_color(make_float3(.3,.4,.8));glossy->set_roughness(.3f);
  auto mix=g->create_node<MixClosureNode>();mix->set_fac(.4f);g->connect(glossy->output("BSDF"),mix->input("Closure1"));g->connect(d->output("BSDF"),mix->input("Closure2"));
  if(variant==0||variant==3||variant==4){auto bevel=g->create_node<BevelNode>();bevel->set_radius(variant==3?.15f:.35f);bevel->set_samples(variant==0?8:5);
    if(variant==3){auto geometry=g->create_node<GeometryNode>();auto tilt=g->create_node<VectorMathNode>();tilt->set_math_type(NODE_VECTOR_MATH_ADD);tilt->set_vector2(make_float3(.2f,0,.1f));
      g->connect(geometry->output("Normal"),tilt->input("Vector1"));g->connect(tilt->output("Vector"),bevel->input("Normal"));}
    if(variant==4){auto radius=g->create_node<ValueNode>();radius->set_value(.25f);g->connect(radius->output("Value"),bevel->input("Radius"));}
    g->connect(bevel->output("Normal"),d->input("Normal"));g->connect(bevel->output("Normal"),glossy->input("Normal"));}
  if(variant>=1){auto ray=g->create_node<RaycastNode>();ray->set_length(variant==3?1.6f:2.5f);ray->set_only_local(variant==3);
    if(variant==3){auto direction=g->create_node<ColorNode>();direction->set_value(make_float3(.15f,-1,.35f));g->connect(direction->output("Color"),ray->input("Direction"));}
    g->connect(ray->output("Is Hit"),mix->input("Fac"));
    if(variant==1){auto tint=g->create_node<MathNode>();tint->set_math_type(NODE_MATH_MULTIPLY);tint->set_value2(.3f);g->connect(ray->output("Hit Distance"),tint->input("Value1"));
      auto combine=g->create_node<CombineXYZNode>();g->connect(tint->output("Value"),combine->input("X"));g->connect(tint->output("Value"),combine->input("Z"));g->connect(combine->output("Vector"),d->input("Color"));}
    if(variant==2){ray->add_output_attribute_socket(ustring("generated"),RaycastNode::ATTR_OUTPUT_FLOAT3,ustring("Generated"));
      g->connect(ray->output("Generated"),d->input("Color"));g->connect(ray->output("Hit Normal"),glossy->input("Color"));
      auto scale=g->create_node<VectorMathNode>();scale->set_math_type(NODE_VECTOR_MATH_SCALE);scale->set_scale(.1f);g->connect(ray->output("Hit Position"),scale->input("Vector1"));
      auto glossy2=g->create_node<GlossyBsdfNode>();glossy2->set_distribution(CLOSURE_BSDF_MICROFACET_GGX_ID);glossy2->set_roughness(.5f);g->connect(scale->output("Vector"),glossy2->input("Color"));
      auto add=g->create_node<AddClosureNode>();g->connect(mix->output("Closure"),add->input("Closure1"));g->connect(glossy2->output("BSDF"),add->input("Closure2"));
      g->connect(add->output("Closure"),g->output()->input("Surface"));auto sh=scene.create_node<Shader>();sh->set_graph(std::move(g));sh->tag_update(&scene);return sh;}
    if(variant==4)g->connect(ray->output("Hit Normal"),glossy->input("Color"));}
  g->connect(mix->output("Closure"),g->output()->input("Surface"));auto sh=scene.create_node<Shader>();sh->set_graph(std::move(g));sh->tag_update(&scene);return sh;
}
