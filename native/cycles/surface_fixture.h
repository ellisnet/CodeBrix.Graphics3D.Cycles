// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#ifdef CBC_GLASS_FIXTURE
#include "glass_fixture.h"
#endif
#ifdef CBC_MICROFACET_FIXTURE
#include "microfacet_fixture.h"
#endif
#ifdef CBC_PRINCIPLED_FIXTURE
#include "principled_fixture.h"
#endif
#ifdef CBC_BSDF_FIXTURE
#include "bsdf_fixture.h"
#endif
#ifdef CBC_NODES_FIXTURE
#include "nodes_fixture.h"
static constexpr unsigned GRAPH_COUNT=NODES_GRAPH_COUNT;
#else
static constexpr unsigned GRAPH_COUNT=20;
#endif
static void prepare(Scene &scene){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);scene.integrator->set_aa_samples(16);
  scene.film->set_use_sample_count(false);scene.camera->set_full_width(64);scene.camera->set_full_height(48);scene.camera->set_screen_size(64,48);scene.camera->compute_auto_viewplane();
  std::vector<Node*> shaders;
  for(unsigned test=0;test<GRAPH_COUNT;test++){
    auto g=make_unique<ShaderGraph>();auto diffuse=g->create_node<DiffuseBsdfNode>();diffuse->set_color(make_float3(.2,.6,.9));diffuse->set_roughness(test==0?0:.43f);
    auto trans=g->create_node<TranslucentBsdfNode>();trans->set_color(make_float3(.6,.2,.1));
    auto transparent=g->create_node<TransparentBsdfNode>();transparent->set_color(make_float3(.7,.8,.9));
    auto emission=g->create_node<EmissionNode>();emission->set_color(make_float3(.1,.4,.7));emission->set_strength(2.3f);
    auto holdout=g->create_node<HoldoutNode>();ShaderOutput *result=diffuse->output("BSDF");
    if(test==2)result=trans->output("BSDF");
    if(test==3)result=transparent->output("BSDF");
    if(test==4)result=emission->output("Emission");
    if(test==5)result=holdout->output("Holdout");
    if(test>=6&&test<=9){
      auto mix=g->create_node<MixClosureNode>();mix->set_fac(.37f);g->connect(diffuse->output("BSDF"),mix->input("Closure1"));g->connect(transparent->output("BSDF"),mix->input("Closure2"));result=mix->output("Closure");
      if(test>=7){auto geo=g->create_node<GeometryNode>();auto checker=g->create_node<CheckerTextureNode>();checker->set_scale(3.7f);g->connect(geo->output("Position"),checker->input("Vector"));g->connect(checker->output("Fac"),mix->input("Fac"));g->connect(checker->output("Color"),diffuse->input("Color"));}
      if(test>=8){auto add=g->create_node<AddClosureNode>();g->connect(result,add->input("Closure1"));g->connect(test==8?emission->output("Emission"):holdout->output("Holdout"),add->input("Closure2"));result=add->output("Closure");}
    }
    if(test>=10&&test<16){auto tc=g->create_node<TextureCoordinateNode>();const char *names[]={"Object","Normal","Reflection","Camera","Window","Object"};
      if(test==15){tc->set_use_transform(true);tc->set_ob_tfm(transform_translate(make_float3(.3,.2,.1))*transform_scale(make_float3(.4,.5,.6)));}
      g->connect(tc->output(names[test-10]),emission->input("Color"));auto add=g->create_node<AddClosureNode>();g->connect(emission->output("Emission"),add->input("Closure1"));g->connect(diffuse->output("BSDF"),add->input("Closure2"));result=add->output("Closure");}
    if(test>=16){const float rough[]={1e-5f,.0001f,.001f,.02f};diffuse->set_roughness(rough[test-16]);}
#ifdef CBC_MICROFACET_FIXTURE
    result=microfacet_material(*g,test);
#endif
#ifdef CBC_GLASS_FIXTURE
    result=glass_material(*g,test);
#endif
#ifdef CBC_PRINCIPLED_FIXTURE
    result=principled_material(*g,test);
#endif
#ifdef CBC_BSDF_FIXTURE
    result=bsdf_material(*g,test);
#endif
#ifdef CBC_NODES_FIXTURE
    result=nodes_material(*g,test);
#endif
    g->connect(result,g->output()->input("Surface"));auto sh=scene.create_node<Shader>();sh->set_graph(std::move(g));sh->tag_update(&scene);shaders.push_back(sh);
  }
  auto mesh=[&](){auto m=scene.create_node<Mesh>();array<Node*> material_slots;for(auto sh:shaders)material_slots.push_back_slow(sh);m->set_used_shaders(material_slots);m->resize_mesh(GRAPH_COUNT*3,GRAPH_COUNT);
    for(unsigned t=0;t<GRAPH_COUNT;t++){const float x=float(t%4)*2.3f,y=float(t/4)*2.1f;auto v=m->get_position_for_write();v[3*t]=make_float3(x,y,.2f);v[3*t+1]=make_float3(x+1.3f,y,.4f);v[3*t+2]=make_float3(x,y+1.1f,-.1f);
      for(unsigned j=0;j<3;j++)m->get_triangles()[3*t+j]=3*t+j;
      m->get_shader()[t]=t;m->get_smooth()[t]=t%2;}
    m->tag_triangles_modified();auto n=m->attributes.add(ATTR_STD_VERTEX_NORMAL)->data_for_write<packed_normal>();for(unsigned i=0;i<GRAPH_COUNT*3;i++)n[i]=packed_normal(normalize(make_float3(-.25f+.03f*(i%7),.3f+.02f*(i%5),1)));
    return m;};auto shared=mesh();
  for(unsigned i=0;i<3;i++){auto ob=scene.create_node<Object>();ob->set_geometry(i<2?shared:mesh());ob->set_tfm(transform_translate(make_float3(-4,-4,float(i)*2))*transform_rotate(.17f*i,make_float3(0,1,0))*transform_scale(make_float3(i==1?-1.2f:1.1f,.8f,1.4f)));}
}
[[maybe_unused]] static std::vector<int> states_for(Scene &scene,std::vector<IntegratorStateCPU> &states){
  // The node fixture has more graphs; fewer rays per triangle keep it within capacity.
  // Graphs 60+ (wavelength, wireframe, bump, Gabor) follow every earlier graph, so
  // the paths of graphs 0-59 keep their state indices. With the two Gabor graphs
  // (65, 66) the late graphs take 5 rays each (7 before) to stay within capacity.
#ifdef CBC_NODES_FIXTURE
  constexpr unsigned rays=7,first_graphs=60,late_rays=5;
#else
  constexpr unsigned rays=17,first_graphs=GRAPH_COUNT,late_rays=0;
#endif
  std::vector<int> ids;for(unsigned pass=0;pass<2;pass++)for(auto ob:scene.objects)for(unsigned t=pass?first_graphs:0;t<(pass?GRAPH_COUNT:first_graphs);t++)for(unsigned j=0;j<(pass?late_rays:rays);j++){
    unsigned i=ids.size()*3;require(i<states.size(),"SVM fixture capacity");ids.push_back(i);auto &s=states[i];s.isect.object=ob->get_device_index();s.isect.prim=ob->get_geometry()->prim_offset+t;s.isect.type=PRIMITIVE_TRIANGLE;s.isect.t=.1f*j;
    s.isect.u=.05f*(j%9);s.isect.v=.04f*(j%11);s.ray.P=make_float3(.1,.2,-2);s.ray.D=normalize(make_float3(.13f*j-.8f,.4f-.07f*j,j%2?1.f:-1.f));s.ray.time=float(j)/16;s.ray.dP=.0001f*j;s.ray.dD=.0003f*j;
    s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE;s.path.visibility=PATH_RAY_VISIBILITY_CAMERA;s.path.rng_pixel=0x12345678u+i*0x9e3779b9u;s.path.rng_offset=uint16_t(65500u+i*79u);s.path.sample=j*137u;
  }return ids;
}
