// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
struct VolumeBoundaryFixture {std::vector<Object*> objects;};
static Shader *volume_boundary_material(Scene &scene,unsigned id){
  auto graph=make_unique<ShaderGraph>();auto absorption=graph->create_node<AbsorptionVolumeNode>();absorption->set_density(.045f+.013f*id);absorption->set_color(make_float3(.8,.65,.4));
  auto scatter=graph->create_node<ScatterVolumeNode>();scatter->set_density(.035f+.009f*id);scatter->set_color(make_float3(.8-.1f*id,.4+.1f*id,.6));scatter->set_anisotropy(id<2?.35f:id==2?-.4f:0.f);
  auto add=graph->create_node<AddClosureNode>();graph->connect(absorption->output("Volume"),add->input("Closure1"));graph->connect(scatter->output("Volume"),add->input("Closure2"));ShaderOutput *volume=add->output("Closure");
  if(id==0&&std::getenv("CODEBRIX_VOLUME_COMPACTION_CASE")){auto extra=graph->create_node<ScatterVolumeNode>();extra->set_density(.017f);extra->set_color(make_float3(.2,.8,.5));extra->set_anisotropy(-.15f);auto mixture=graph->create_node<AddClosureNode>();graph->connect(volume,mixture->input("Closure1"));graph->connect(extra->output("Volume"),mixture->input("Closure2"));volume=mixture->output("Closure");}
  graph->connect(volume,graph->output()->input("Volume"));
  if(id==2){auto transparent=graph->create_node<TransparentBsdfNode>();transparent->set_color(make_float3(.8,.9,.95));graph->connect(transparent->output("BSDF"),graph->output()->input("Surface"));}
  if(id==3){auto glass=graph->create_node<GlassBsdfNode>();glass->set_distribution(CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID);glass->set_roughness(.18f);glass->set_IOR(1.33f);glass->set_color(make_float3(.8,.9,.95));graph->connect(glass->output("BSDF"),graph->output()->input("Surface"));}
  auto shader=scene.create_node<Shader>();shader->set_graph(std::move(graph));shader->set_emission_sampling_method(EMISSION_SAMPLING_NONE);shader->tag_update(&scene);return shader;
}
static Object *volume_boundary_box(Scene &scene,Shader *shader,float3 lo,float3 hi){
  auto mesh=scene.create_node<Mesh>();array<Node*> materials;materials.push_back_slow(shader);mesh->set_used_shaders(materials);mesh->resize_mesh(8,12);
  for(unsigned i=0;i<8;i++)mesh->get_position_for_write()[i]=make_float3(i&1?hi.x:lo.x,i&2?hi.y:lo.y,i&4?hi.z:lo.z);
  constexpr int triangles[]={0,2,1,1,2,3,4,5,6,5,7,6,0,4,2,2,4,6,1,3,5,3,7,5,0,1,4,1,5,4,2,6,3,3,6,7};
  for(unsigned i=0;i<36;i++)mesh->get_triangles()[i]=triangles[i];
  for(unsigned i=0;i<12;i++){mesh->get_shader()[i]=0;mesh->get_smooth()[i]=false;}mesh->tag_triangles_modified();
  auto object=scene.create_node<Object>();object->set_geometry(mesh);return object;
}
static VolumeBoundaryFixture add_volume_boundaries(Scene &scene,bool camera_inside=false,unsigned count=4){
  VolumeBoundaryFixture result;
  const float3 low[]={make_float3(-2.4,-2.2,camera_inside?-11.5f:-2.f),make_float3(-1.3,-1.4,-.5),make_float3(-.7,-.7,.2),make_float3(2.8,-.5,.2)};
  const float3 high[]={make_float3(1.6,2.3,4),make_float3(2.6,1.8,5),make_float3(.6,.8,2.5),make_float3(3.5,.5,2)};
  for(unsigned i=0;i<count;i++)result.objects.push_back(volume_boundary_box(scene,volume_boundary_material(scene,i),low[i],high[i]));
  scene.integrator->set_max_volume_bounce(4);return result;
}
