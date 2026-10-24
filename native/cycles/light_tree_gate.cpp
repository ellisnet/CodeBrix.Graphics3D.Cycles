// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Light tree and light linking gate (integrator.use_light_tree,
// KERNEL_FEATURE_LIGHT_LINKING). Per case, against the unmodified pinned CPU
// kernels on the same device-prepared scene tables and random numbers:
//  - state: 4,096 path states through the WebGPU light selection
//    (light_selection_compute: light_tree_sample<false> or the flat CDF, then
//    light_link_object_match) and the light sampler, and the tree's forward pdf
//    of a target emitter (light_tree_pdf<false>); selection validity, emitter
//    index, sample validity, prim and object are exact, the rest practical;
//  - image: the native scheduler's GPU render against the original CPU render
//    (camera, closest, surface, light NEE, shadow, background, lamp forward),
//    twice (the repeat is captured for replays).
// Cases: 0 lamps (point, spot, area, sun), 1 many-triangle mesh lights, 2 lamps
// + emissive triangles + HDRI world with its background light, 3 an instanced
// emitter mesh, 4 a moving and deforming emitter, 5 light linking on the flat
// CDF (tree off), 6 light linking on the tree. All but case 5 use the tree.
#include "wavefront_test.h"
#include "render_capture.h"
#include "reference_policy.h"
#include "camera_reference.h"
#include "closest_reference.h"
#include "surface_reference.h"
#include "background_reference.h"
#include "light_reference.h"
#include "shadow_reference.h"
#include "environment_reference.h"
#include "light_tree_reference.h"
#include "environment_fixture.h"
#include "object_motion_fixture.h"
#include "deforming_motion_fixture.h"
#include "scene/scene.h"
#include "scene/background.h"
#include "scene/camera.h"
#include "scene/integrator.h"
#include "scene/light.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "util/progress.h"
#include "util/task.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
using namespace ccl;
using namespace wavefront_test;
constexpr unsigned width=32,height=24,samples=8,probe_count=4096;
constexpr int light_tree_cases=7;
struct Metrics {uint64_t exact=0,numeric=0,strict=0,selected=0,valid=0,triangles=0,lamps=0,rejected=0,forward=0,film=0,film_strict=0,repeat=0,shadows=0,visible=0,nee=0,guards=0,stages=0;double maximum=0,film_maximum=0;};
static void numeric(float actual,float expected,Metrics &m,const std::string &where){
  const double error=std::abs(double(actual)-expected);m.numeric++;m.maximum=std::max(m.maximum,error);m.strict+=error>1e-7+1e-7*std::abs(double(expected));
  require(std::isfinite(actual)&&std::isfinite(expected)&&error<=5e-5+2e-4*std::abs(double(expected)),where+" got="+std::to_string(actual)+" expected="+std::to_string(expected));
}
static Shader *material(Scene &scene,int variant){
  auto graph=make_unique<ShaderGraph>();auto diffuse=graph->create_node<DiffuseBsdfNode>();diffuse->set_color(make_float3(.55,.32,.18));diffuse->set_roughness(.31f);
  ShaderOutput *surface=diffuse->output("BSDF");
  if(variant){auto glossy=graph->create_node<GlossyBsdfNode>();glossy->set_distribution(CLOSURE_BSDF_MICROFACET_GGX_ID);glossy->set_color(make_float3(.2,.5,.8));glossy->set_roughness(.35f);
    auto mix=graph->create_node<MixClosureNode>();mix->set_fac(.4f);graph->connect(surface,mix->input("Closure1"));graph->connect(glossy->output("BSDF"),mix->input("Closure2"));surface=mix->output("Closure");}
  graph->connect(surface,graph->output()->input("Surface"));auto shader=scene.create_node<Shader>();shader->set_graph(std::move(graph));shader->tag_update(&scene);return shader;
}
static Mesh *quad_mesh(Scene &scene,Shader *shader,const std::array<float3,4> &vertices){
  auto mesh=scene.create_node<Mesh>();array<Node*> slots;slots.push_back_slow(shader);mesh->set_used_shaders(slots);mesh->resize_mesh(4,2);
  std::copy(vertices.begin(),vertices.end(),mesh->get_position_for_write());const int indices[]={0,1,2,0,2,3};std::copy_n(indices,6,mesh->get_triangles().data());
  std::ranges::fill(mesh->get_smooth(),false);std::ranges::fill(mesh->get_shader(),0);mesh->tag_triangles_modified();return mesh;
}
static Object *quad(Scene &scene,Shader *shader,const std::array<float3,4> &vertices){
  auto object=scene.create_node<Object>();object->set_geometry(quad_mesh(scene,shader,vertices));return object;
}
// A columns x rows grid of emissive quads (2 triangles each) facing -y at height y.
static Object *emitter_grid(Scene &scene,Shader *shader,int columns,int rows,float y){
  auto mesh=scene.create_node<Mesh>();array<Node*> slots;slots.push_back_slow(shader);mesh->set_used_shaders(slots);
  mesh->resize_mesh(columns*rows*4,columns*rows*2);auto *P=mesh->get_position_for_write();int *t=mesh->get_triangles().data();
  for(int r=0;r<rows;r++)for(int c=0;c<columns;c++){const int q=r*columns+c;const float x0=-3.f+6.f*c/columns+.12f,x1=-3.f+6.f*(c+1)/columns-.12f,z0=-3.f+4.f*r/rows+.1f,z1=-3.f+4.f*(r+1)/rows-.1f;
    const float dy=.05f*float((q*7)%5);
    P[4*q]=make_float3(x0,y+dy,z0);P[4*q+1]=make_float3(x0,y+dy,z1);P[4*q+2]=make_float3(x1,y+dy,z1);P[4*q+3]=make_float3(x1,y+dy,z0);
    const int k[]={0,1,2,0,2,3};for(int j=0;j<6;j++)t[6*q+j]=4*q+k[j];}
  std::ranges::fill(mesh->get_smooth(),false);std::ranges::fill(mesh->get_shader(),0);mesh->tag_triangles_modified();
  auto object=scene.create_node<Object>();object->set_geometry(mesh);return object;
}
static Object *lamp(Scene &scene,Light *light,Shader *shader,const Transform &tfm,float3 strength){
  light->set_strength(strength);light->set_use_mis(true);array<Node*> slots;slots.push_back_slow(shader);light->set_used_shaders(slots);
  auto object=scene.create_node<Object>();object->set_geometry(light);object->set_tfm(tfm);return object;
}
static Transform aim(float3 at,float yaw,float pitch){return transform_translate(at)*transform_rotate(yaw,make_float3(0,1,0))*transform_rotate(pitch,make_float3(1,0,0));}
struct Fixture {std::vector<Object*> receivers;bool tree=true,linking=false,motion=false,world=false;};
static Fixture fixture(Scene &scene,int c){
  Fixture f;f.tree=c!=5;f.linking=c>=5;f.motion=c==4;f.world=c==2;
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(f.tree);scene.integrator->set_use_adaptive_sampling(false);
  scene.integrator->set_aa_samples(samples);scene.integrator->set_min_bounce(2);scene.integrator->set_max_bounce(3);scene.integrator->set_filter_glossy(0);
  scene.film->set_use_sample_count(false);scene.camera->set_full_width(width);scene.camera->set_full_height(height);scene.camera->set_screen_size(width,height);
  scene.camera->set_fov(.9f);scene.camera->set_matrix(transform_translate(make_float3(0,0,-7)));scene.camera->compute_auto_viewplane();
  auto diffuse=material(scene,0),mixed=material(scene,1);
  auto wall=quad(scene,diffuse,{make_float3(-4,-3,3),make_float3(4,-3,3),make_float3(4,3,3),make_float3(-4,3,3)});
  auto floor=quad(scene,mixed,{make_float3(-4,-2,-2),make_float3(4,-2,-2),make_float3(4,-2,3),make_float3(-4,-2,3)});
  auto center=quad(scene,mixed,{make_float3(-.8f,-1.2f,0),make_float3(.8f,-1.2f,0),make_float3(.8f,1.1f,0),make_float3(-.8f,1.1f,0)});
  f.receivers={wall,floor,center};
  Shader *world=nullptr;
  if(f.world){world=environment_world(scene,EnvironmentWorld::Hdri,0);environment_light(scene,world,true,64);}
  else{auto background=make_unique<ShaderGraph>();auto bg=background->create_node<BackgroundNode>();bg->set_color(zero_float3());background->connect(bg->output("Background"),background->output()->input("Surface"));
    auto w=scene.create_node<Shader>();w->set_graph(std::move(background));w->tag_update(&scene);scene.background->set_shader(w);}
  auto emission_graph=make_unique<ShaderGraph>();auto emission=emission_graph->create_node<EmissionNode>();emission->set_color(one_float3());emission->set_strength(c==0?1.f:3.f);
  emission_graph->connect(emission->output("Emission"),emission_graph->output()->input("Surface"));auto emitter=scene.create_node<Shader>();emitter->set_graph(std::move(emission_graph));emitter->tag_update(&scene);
  const float3 warm=make_float3(140,100,70),cool=make_float3(60,90,150);
  if(c==0){
    auto p0=scene.create_node<PointLight>();p0->set_radius(.45f);p0->set_is_sphere(true);lamp(scene,p0,emitter,aim(make_float3(2.2f,2,-2),.3f,-.4f),warm);
    auto p1=scene.create_node<PointLight>();p1->set_radius(0.f);lamp(scene,p1,emitter,aim(make_float3(-2.7f,2,-2),.3f,-.4f),cool*.5f);
    auto s0=scene.create_node<SpotLight>();s0->set_radius(.3f);s0->set_is_sphere(true);s0->set_angle(1.f);s0->set_smooth(.3f);lamp(scene,s0,emitter,aim(make_float3(0,2.6f,-1.5f),0,1.2f),warm*.8f);
    auto s1=scene.create_node<SpotLight>();s1->set_radius(.2f);s1->set_is_sphere(false);s1->set_angle(.6f);s1->set_smooth(.1f);lamp(scene,s1,emitter,aim(make_float3(1.5f,1.f,-4.f),2.8f,-.4f),cool);
    auto a0=scene.create_node<AreaLight>();a0->set_sizeu(1.4f);a0->set_sizev(.9f);a0->set_spread(.9f);a0->set_normalize(true);lamp(scene,a0,emitter,aim(make_float3(-1.6f,1.4f,-2.5f),2.8f,-.4f),warm*.5f);
    auto a1=scene.create_node<AreaLight>();a1->set_sizeu(1.f);a1->set_sizev(1.f);a1->set_ellipse(true);a1->set_spread(M_PI_F);a1->set_normalize(false);lamp(scene,a1,emitter,aim(make_float3(2.5f,-.5f,-3.f),2.5f,.2f),cool*.4f);
    auto sun=scene.create_node<SunLight>();sun->set_angle(.22f);sun->set_normalize(false);lamp(scene,sun,emitter,aim(make_float3(0,0,0),2.85f,-.4f),make_float3(.3,.5,.7));
  }
  if(c==1){emitter_grid(scene,emitter,4,3,2.4f);auto panel=quad(scene,emitter,{make_float3(-.6,-.4,0),make_float3(.6,-.4,0),make_float3(.6,.4,0),make_float3(-.6,.4,0)});
    panel->set_tfm(aim(make_float3(2.f,.6f,-1.5f),.4f,.3f));
    auto p=scene.create_node<PointLight>();p->set_radius(.3f);p->set_is_sphere(true);lamp(scene,p,emitter,aim(make_float3(-2.4f,1.2f,-2.5f),0,0),warm*.4f);}
  if(c==2){auto panel=quad(scene,emitter,{make_float3(-.8,-.5,0),make_float3(.8,-.5,0),make_float3(.8,.5,0),make_float3(-.8,.5,0)});panel->set_tfm(aim(make_float3(1.65f,1.55f,-1.4f),.18f,.5f));
    auto p=scene.create_node<PointLight>();p->set_radius(.45f);p->set_is_sphere(true);lamp(scene,p,emitter,aim(make_float3(-2.f,2.f,-1.f),0,0),warm*.3f);
    auto a=scene.create_node<AreaLight>();a->set_sizeu(1.2f);a->set_sizev(.8f);a->set_spread(1.1f);a->set_normalize(true);lamp(scene,a,emitter,aim(make_float3(2.2f,2.f,-2.f),2.8f,-.4f),cool*.3f);}
  if(c==3){auto mesh=quad_mesh(scene,emitter,{make_float3(-.6,-.4,0),make_float3(.6,-.4,0),make_float3(.6,.4,0),make_float3(-.6,.4,0)});
    const Transform tfm[]={aim(make_float3(1.65f,1.55f,-1.4f),.18f,.5f),aim(make_float3(-2.f,1.5f,-1.f),-.3f,.6f)*transform_scale(make_float3(.7f,1.2f,1.f)),
      aim(make_float3(0.f,2.2f,-.5f),0.f,1.3f)*transform_scale(make_float3(-1.1f,.9f,1.f))};
    for(const auto &t:tfm){auto o=scene.create_node<Object>();o->set_geometry(mesh);o->set_tfm(t);}
    auto p=scene.create_node<PointLight>();p->set_radius(.3f);p->set_is_sphere(true);lamp(scene,p,emitter,aim(make_float3(2.4f,-.5f,-2.5f),0,0),warm*.3f);}
  if(c==4){scene.integrator->set_motion_blur(true);scene.camera->set_shuttertime(.6f);
    auto panel=quad(scene,emitter,{make_float3(-.8,-.5,0),make_float3(.8,-.5,0),make_float3(.8,.5,0),make_float3(-.8,.5,0)});
    panel->set_tfm(aim(make_float3(1.65f,1.55f,-1.4f),.18f,.5f));deforming_motion_fixture(scene,*panel,1);object_motion_fixture(scene,*panel,1);
    auto other=quad(scene,emitter,{make_float3(-.5,-.3,0),make_float3(.5,-.3,0),make_float3(.5,.3,0),make_float3(-.5,.3,0)});other->set_tfm(aim(make_float3(-1.8f,1.8f,-1.2f),-.3f,.7f));
    object_motion_fixture(scene,*other,2);
    auto p=scene.create_node<PointLight>();p->set_radius(.45f);p->set_is_sphere(true);lamp(scene,p,emitter,aim(make_float3(-2.f,2.f,-1.f),0,0),warm*.3f);}
  if(c>=5){
    auto a=scene.create_node<PointLight>();a->set_radius(.4f);a->set_is_sphere(true);auto la=lamp(scene,a,emitter,aim(make_float3(-2.4f,2.f,-2.f),0,0),warm*.6f);
    auto b=scene.create_node<PointLight>();b->set_radius(.3f);b->set_is_sphere(true);auto lb=lamp(scene,b,emitter,aim(make_float3(2.4f,2.f,-2.f),0,0),cool*.6f);
    auto s=scene.create_node<SpotLight>();s->set_radius(.25f);s->set_is_sphere(true);s->set_angle(1.1f);s->set_smooth(.2f);auto ls=lamp(scene,s,emitter,aim(make_float3(0,2.6f,-1.5f),0,1.2f),warm*.5f);
    auto panel=quad(scene,emitter,{make_float3(-.6,-.4,0),make_float3(.6,-.4,0),make_float3(.6,.4,0),make_float3(-.6,.4,0)});panel->set_tfm(aim(make_float3(1.65f,1.55f,-1.4f),.18f,.5f));
    // Receivers: wall set 1, floor set 2, center set 0 (default: every emitter).
    wall->set_receiver_light_set(1);floor->set_receiver_light_set(2);
    la->set_light_set_membership((1ull<<0)|(1ull<<1));lb->set_light_set_membership((1ull<<0)|(1ull<<2));
    ls->set_light_set_membership((1ull<<0)|(1ull<<1)|(1ull<<2));panel->set_light_set_membership(1ull<<0);
    f.receivers.push_back(panel);
  }
  for(auto shader:scene.shaders)shader->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
  if(c>=1)emitter->set_emission_sampling_method(c==3?EMISSION_SAMPLING_FRONT:EMISSION_SAMPLING_FRONT_BACK);
  if(world)world->set_emission_sampling_method(EMISSION_SAMPLING_AUTO);
  return f;
}
static void cpu_render(DeviceScene &scene,std::vector<float> &film,Metrics &metrics){
  std::vector<IntegratorStateCPU> state(width*height);std::vector<IntegratorShadowStateCPU> shadow(width*height);
  for(unsigned sample=0;sample<samples;sample++){
    std::fill(state.begin(),state.end(),IntegratorStateCPU{});KernelWorkTile tile{};tile.w=width;tile.h=height;tile.stride=width;tile.num_samples=1;tile.start_sample=sample;
    original_camera(scene,{tile},0,state,film);
    for(unsigned step=0;step<100;step++){
      std::map<DeviceKernel,std::vector<int>> queues;
      for(size_t i=0;i<state.size();i++)if(state[i].path.queued_kernel)queues[DeviceKernel(state[i].path.queued_kernel)].push_back(i);
      if(queues.empty())break;
      require(step<99,"CPU light tree paths did not terminate");
      for(auto &[kernel,ids]:queues){
        if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST)original_closest(scene,ids,state,film);
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND)original_background(scene,ids,state,film);
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD)original_light_forward(scene,ids,state,film);
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE){
          original_surface(scene,ids,state,film);std::vector<int> nee,pending;
          for(int i:ids){if(!state[i].shadow.shadow_path.queued_kernel)continue;shadow[i]=state[i].shadow;
            if(shadow[i].shadow_path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_NEE)nee.push_back(i);else pending.push_back(i);}
          metrics.nee+=nee.size();original_light_nee(scene,nee,shadow,film);
          for(int i:nee)if(shadow[i].shadow_path.queued_kernel){require(shadow[i].shadow_path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,"Unexpected CPU light NEE successor");pending.push_back(i);}
          metrics.shadows+=pending.size();original_shadow(scene,DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,pending,shadow,film);
          std::vector<int> clear;for(int i:pending)if(shadow[i].shadow_path.queued_kernel)clear.push_back(i);
          metrics.visible+=clear.size();original_shadow(scene,DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW,clear,shadow,film);
          for(int i:ids)if(state[i].shadow.shadow_path.queued_kernel)state[i].shadow=shadow[i];
        }else throw std::runtime_error("Unexpected CPU light tree kernel");
      }
    }
  }
}
// The probe compiles the surface module specialized to this scene, as the device does.
static const char probe_source[]=R"WGSL(
// Selection and sampling are separate entries, as on the device (one entry with
// both passes the Intel Mesa 25.0.7 16-bit instruction range and miscompiles).
@compute @workgroup_size(1) fn light_tree_select_probe(@builtin(global_invocation_id) invocation:vec3<u32>){
 svm_invocation(invocation.x);let i=invocation.x+p(30u);if(i>=p(1u)){return;}let input=p(2u)+i*16u;
 var sd:SurfaceData;sd.time=heap_float(state_address(S_ray_time,i,4u));sd.P=heap_vec3(state_address(S_ray_P,i,12u));sd.N=heap_vec3(state_address(S_ray_D,i,12u));sd.object_id=load_word(input);
 var r:SvmResult;if((state_word(S_path_flag,i)&PATH_RAY_MIS_HAD_TRANSMISSION)!=0u){r.runtime_flag=SR_BSDF_HAS_TRANSMISSION;}
 let result=light_selection_compute(i,sd,r,true,false);if(load_word(p(17u))!=0u){return;}
 light_selection_write(p(23u)+i*SURFACE_STAGE_BYTES,result.selection,0.0);
}
@compute @workgroup_size(1) fn light_tree_probe(@builtin(global_invocation_id) invocation:vec3<u32>){
 svm_invocation(invocation.x);let i=invocation.x+p(30u);if(i>=p(1u)){return;}let input=p(2u)+i*16u;
 var sd:SurfaceData;sd.time=heap_float(state_address(S_ray_time,i,4u));sd.P=heap_vec3(state_address(S_ray_P,i,12u));sd.N=heap_vec3(state_address(S_ray_D,i,12u));sd.object_id=load_word(input);
 var r:SvmResult;if((state_word(S_path_flag,i)&PATH_RAY_MIS_HAD_TRANSMISSION)!=0u){r.runtime_flag=SR_BSDF_HAS_TRANSMISSION;}
 let at=p(23u)+i*SURFACE_STAGE_BYTES;let selection=light_selection_read(at,vec3<f32>(0));light_select_record=at;
 triangle_light_prefill(i,sd,false,true);let ls=light_sample_native(i,sd,r);
 let tree=data_word(LT_D_use_light_tree)!=0u;var emitter=f32(selection.distribution);
 if(!tree){emitter=f32((selection.distribution-table_at(G_light_distribution,0u,SIZE_light_distribution))/SIZE_light_distribution);}
 let selected=select(true,selection.tree,tree);let valid=ls.pdf>0.0;let out=p(4u)+i*64u;
 // As the CPU record: the flat CDF's selection pdf, and zeros for a missing selection or sample.
 let pdf=select(data_float(D_integrator_distribution_pdf_lights),selection.pdf,tree);
 var values=array<f32,15>(select(0.0,1.0,selected),emitter,pdf,select(0.0,1.0,valid),ls.P.x,ls.P.y,ls.P.z,ls.D.x,ls.D.y,ls.D.z,ls.t,ls.pdf,ls.eval_fac,f32(ls.prim),f32(ls.object_id));
 if(!selected){for(var j=1u;j<15u;j++){values[j]=0.0;}}else if(!valid){for(var j=4u;j<15u;j++){values[j]=0.0;}}
 for(var j=0u;j<15u;j++){store_word(out+j*4u,bitcast<u32>(values[j]));}
}
@compute @workgroup_size(1) fn light_tree_importance_probe(@builtin(global_invocation_id) invocation:vec3<u32>){
 svm_invocation(invocation.x);let i=invocation.x+p(30u);if(i>=p(1u)){return;}let n=table_count(G_light_tree_nodes);let e=table_count(G_light_tree_emitters);
 let state=i/(n+e);let item=i%(n+e);let flag=state_word(S_path_flag,state);
 let P=heap_vec3(state_address(S_ray_P,state,12u));let N=heap_vec3(state_address(S_ray_D,state,12u));let tr=(flag&PATH_RAY_MIS_HAD_TRANSMISSION)!=0u;
 let v=lt_item_importance(P,N,tr,select(item-n,item,item<n),item<n);
 store_word(p(4u)+i*8u,bitcast<u32>(v.x));store_word(p(4u)+i*8u+4u,bitcast<u32>(v.y));
 if(item<n){var lp=-1.0;let at=lt_node(item);if(lt_type(at)==LT_LIGHT_TREE_INNER){var c:array<vec2<f32>,2>;
   for(var k=0u;k<2u;k++){let child=load_word(at+select(LTN_right_child,LTN_left_child,k==0u));let cn=lt_node(child);let m=load_word(cn+LTN_num_emitters);
     if(m==1u){c[k]=lt_item_importance(P,N,tr,load_word(cn+LTN_first_emitter),false);}else if(m!=0u){c[k]=lt_item_importance(P,N,tr,child,true);}else{c[k]=vec2<f32>(0);}}
   let tm=rounded_add(c[0].x,c[1].x);if(tm==0.0){lp=-2.0;}else{let tn=rounded_add(c[0].y,c[1].y);let pm=rounded_divide(c[0].x,tm);
     let pn=select(0.5*(select(0.0,1.0,c[0].x>0.0)+select(0.0,1.0,c[1].x==0.0)),rounded_divide(c[0].y,tn),tn>0.0);lp=0.5*rounded_add(pm,pn);}}
   store_word(p(5u)+(state*n+item)*4u,bitcast<u32>(lp));}
}
@compute @workgroup_size(1) fn light_tree_pdf_probe(@builtin(global_invocation_id) invocation:vec3<u32>){
 svm_invocation(invocation.x);let i=invocation.x+p(30u);if(i>=p(1u)){return;}let input=p(2u)+i*16u;let goal=bitcast<i32>(load_word(input+12u));
 var pdf=0.0;
 if(goal>=0&&data_word(LT_D_use_light_tree)!=0u){let flag=state_word(S_path_flag,i);
   pdf=light_tree_walk(false,heap_vec3(state_address(S_ray_P,i,12u)),heap_vec3(state_address(S_ray_D,i,12u)),(flag&PATH_RAY_MIS_HAD_TRANSMISSION)!=0u,
     select(NONE,load_word(input+4u),light_link_enabled()),0.0,load_word(input+8u),u32(goal)).pdf;}
 store_word(p(4u)+i*64u+60u,bitcast<u32>(pdf));
})WGSL";
static void probe(WebGPUDevice &d,Scene &scene,const std::filesystem::path &folder,std::vector<IntegratorStateCPU> &states,const std::vector<LightTreeProbeInput> &inputs,
                  const std::vector<float> &expected,Metrics &m){
  bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);
  require(scheduler.capacity()>=probe_count,"Light tree probe needs more path states");const auto layout=fields(d);
  std::vector<IntegratorStateCPU> all(scheduler.capacity());std::copy(states.begin(),states.end(),all.begin());upload(d,layout,all);
  device_vector<float> output(&d,"light_tree_samples",MEM_READ_WRITE);output.alloc(probe_count*16+16);std::fill_n(output.data(),output.size(),-1234.f);output.copy_to_device();
  device_vector<int> input(&d,"light_tree_inputs",MEM_READ_WRITE);input.alloc(probe_count*4);std::memcpy(input.data(),inputs.data(),probe_count*16);input.copy_to_device();
  device_vector<uint8_t> records(&d,"light_tree_records",MEM_READ_WRITE);records.alloc(size_t(probe_count)*(surface_data_cache_bytes+38*4));records.zero_to_device();
  std::string source=d.module_wgsl(surface_source())+probe_source;
  // Diagnostic (CODEBRIX_LIGHT_TREE_TRACE): the probe's walk records each inner node's left child, left probability and rand.
  if(std::getenv("CODEBRIX_LIGHT_TREE_TRACE")){
    auto replace=[&](const std::string &from,const std::string &to){const size_t at=source.find(from);require(at!=std::string::npos,"Trace anchor missing: "+from);source.replace(at,from.size(),to);};
    replace("struct LtWalk {","var<private> lt_trace:array<f32,30>;var<private> lt_trace_n:u32;\nstruct LtWalk {");
    replace("        let left_probability=0.5*rounded_add(probability_max,probability_min);\n",
            "        let left_probability=0.5*rounded_add(probability_max,probability_min);\n        if(sample&&lt_trace_n<27u){lt_trace[lt_trace_n]=f32(node);lt_trace[lt_trace_n+1u]=left_probability;lt_trace[lt_trace_n+2u]=rand;lt_trace_n+=3u;}\n");
    replace(" light_selection_write(p(23u)+i*SURFACE_STAGE_BYTES,result.selection,0.0);"," light_selection_write(p(23u)+i*SURFACE_STAGE_BYTES,result.selection,0.0);\n for(var j=0u;j<30u;j++){store_word(p(5u)+(i*30u+j)*4u,bitcast<u32>(lt_trace[j]));}");
  }
  std::ofstream(folder/"probe.wgsl")<<source;
  WGPUShaderSourceWGSL code{};code.chain.sType=WGPUSType_ShaderSourceWGSL;code.code={source.data(),source.size()};WGPUShaderModuleDescriptor descriptor{};descriptor.nextInChain=&code.chain;
  d.gpu.push_scope();cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> module(wgpuDeviceCreateShaderModule(d.gpu.get(),&descriptor));d.gpu.pop_scope();
  auto select_shader=d.gpu.pipeline(module,d.pipeline_layout,"light_tree_select_probe");
  auto sample_shader=d.gpu.pipeline(module,d.pipeline_layout,"light_tree_probe");auto pdf_shader=d.gpu.pipeline(module,d.pipeline_layout,"light_tree_pdf_probe");
  for(const char *entry:{"light_tree_select_probe","light_tree_probe","light_tree_pdf_probe","light_tree_importance_probe"})
    std::cerr<<"probe entry "<<entry<<" inlines to "<<wgsl_inlined_bytes(source,entry)<<" bytes\n";
  device_vector<float> trace(&d,"light_tree_trace",MEM_READ_WRITE);trace.alloc(probe_count*30);trace.zero_to_device();
  std::array<uint32_t,32> p{};p[5]=trace.device_pointer;p[1]=probe_count;p[2]=input.device_pointer;p[4]=output.device_pointer+32;p[6]=d.constants.at("data");p[7]=d.constants.at("integrator_state");
  p[9]=scene.dscene.sample_pattern_lut.device_pointer;p[14]=d.scene_tables();p[17]=d.error_word;p[23]=records.device_pointer;
  // Slices bound each submission (the Adreno drivers reset the GPU past about two seconds).
  constexpr unsigned slice=256;
  for(auto shader:{&select_shader,&sample_shader,&pdf_shader})for(unsigned first=0;first<probe_count;first+=slice){p[30]=first;d.dispatch(p,std::min(slice,probe_count-first),*shader,1);d.sync();}
  if(std::getenv("CODEBRIX_LIGHT_TREE_TRACE")){trace.copy_from_device();for(unsigned i:{2u,3u,6u}){std::cerr<<"trace "<<i;for(unsigned j=0;j<30;j++)std::cerr<<' '<<std::setprecision(9)<<trace[i*30+j];std::cerr<<"\n";}}
  output.copy_from_device();healthy(d);uint error=0;d.download(d.error_word,&error,4);require(error==0,"Light tree probe shader error");
  std::ofstream(folder/"probe.actual.bin",std::ios::binary).write(reinterpret_cast<const char*>(output.data()+8),probe_count*64);
  std::ofstream(folder/"probe.reference.bin",std::ios::binary).write(reinterpret_cast<const char*>(expected.data()),expected.size()*4);
  const bool tree=scene.dscene.data.integrator.use_light_tree;
  // Diagnostic (CODEBRIX_LIGHT_TREE_IMPORTANCE=N): every node and emitter importance of the first N states, GPU vs CPU.
  if(const char *env=std::getenv("CODEBRIX_LIGHT_TREE_IMPORTANCE");env&&tree){
    const unsigned n=std::min(probe_count,unsigned(std::stoul(env)));std::vector<IntegratorStateCPU> first(states.begin(),states.begin()+n);
    auto cpu=original_light_tree_importance(scene.dscene,first);const size_t nn=scene.dscene.light_tree_nodes.size();
    std::vector<float> cpu_lp;{std::vector<float> rest;size_t at=0;const size_t ne=scene.dscene.light_tree_emitters.size();
      for(unsigned s=0;s<n;s++){cpu_lp.insert(cpu_lp.end(),cpu.begin()+at,cpu.begin()+at+nn);at+=nn;rest.insert(rest.end(),cpu.begin()+at,cpu.begin()+at+2*(nn+ne));at+=2*(nn+ne);}cpu=rest;}
    const unsigned items=unsigned(cpu.size()/2);device_vector<float> lps(&d,"light_tree_lp",MEM_READ_WRITE);lps.alloc(n*nn+16);lps.copy_to_device();
    device_vector<float> values(&d,"light_tree_importance",MEM_READ_WRITE);values.alloc(cpu.size()+16);values.copy_to_device();
    auto shader=d.gpu.pipeline(module,d.pipeline_layout,"light_tree_importance_probe");auto q=p;q[1]=items;q[4]=values.device_pointer;q[5]=lps.device_pointer;
    for(unsigned first_item=0;first_item<items;first_item+=slice){q[30]=first_item;d.dispatch(q,std::min(slice,items-first_item),shader,1);d.sync();}
    values.copy_from_device();lps.copy_from_device();const unsigned per=items/n;unsigned differing=0;
    for(unsigned k=0;k<n*nn;k++)if(std::memcmp(&lps[k],&cpu_lp[k],4)&&differing++<40)std::cerr<<"leftprob state="<<k/nn<<" node="<<k%nn<<" gpu="<<std::setprecision(9)<<lps[k]<<" cpu="<<cpu_lp[k]<<"\n";
    for(unsigned k=0;k<items;k++)for(unsigned j=0;j<2;j++)if(std::memcmp(&values[2*k+j],&cpu[2*k+j],4)){
      if(differing++<40)std::cerr<<"importance state="<<k/per<<" item="<<k%per<<(k%per<scene.dscene.light_tree_nodes.size()?" node":" emitter")<<" "<<(j?"min":"max")<<" gpu="<<std::setprecision(9)<<values[2*k+j]<<" cpu="<<cpu[2*k+j]<<"\n";}
    std::cerr<<"importance differing="<<differing<<" of "<<2*items<<"\n";
    for(size_t k=0;k<scene.dscene.light_tree_nodes.size();k++){const auto &nd=scene.dscene.light_tree_nodes[k];
      std::cerr<<std::setprecision(9)<<"node "<<k<<" type="<<int(nd.type)<<" n="<<nd.num_emitters<<" first/left="<<nd.leaf.first_emitter<<" right="<<nd.inner.right_child<<" min=("<<nd.bbox.min.x<<","<<nd.bbox.min.y<<","<<nd.bbox.min.z<<") max=("<<nd.bbox.max.x<<","<<nd.bbox.max.y<<","<<nd.bbox.max.z<<") axis=("<<nd.bcone.axis.x<<","<<nd.bcone.axis.y<<","<<nd.bcone.axis.z<<") o="<<nd.bcone.theta_o<<" e="<<nd.bcone.theta_e<<" energy="<<nd.energy<<"\n";}
    for(unsigned k:{11u,22u})std::cerr<<"state "<<k<<" P=("<<states[k].ray.P.x<<","<<states[k].ray.P.y<<","<<states[k].ray.P.z<<") N=("<<states[k].ray.D.x<<","<<states[k].ray.D.y<<","<<states[k].ray.D.z<<") flag="<<states[k].path.flag<<"\n";
  }
  for(unsigned i=0;i<probe_count;i++){const float *a=output.data()+8+i*16,*e=expected.data()+i*16;const std::string where="probe index="+std::to_string(i);
    require(a[0]==e[0],"Selection validity differs "+where);m.exact++;
    if(std::getenv("CODEBRIX_LIGHT_TREE_CONTINUE")&&e[0]&&(a[1]!=e[1]||std::abs(a[2]-e[2])>5e-5+2e-4*std::abs(e[2]))){static int shown=0;if(shown++<20)std::cerr<<"mismatch "<<where<<" gpu="<<a[1]<<" cpu="<<e[1]<<" gpu_pdf="<<a[2]<<" cpu_pdf="<<e[2]<<" flag="<<states[i].path.flag<<"\n";continue;}
    if(e[0]){m.selected++;require(a[1]==e[1],"Selected emitter differs "+where+" got="+std::to_string(a[1])+" expected="+std::to_string(e[1]));m.exact++;
      numeric(a[2],e[2],m,"Selection pdf "+where);
      require(a[3]==e[3],"Sample validity differs "+where);m.exact++;m.rejected+=!e[3];
      if(e[3]){m.valid++;for(unsigned j=4;j<13;j++)numeric(a[j],e[j],m,"Sample field="+std::to_string(j)+" "+where);
        require(a[13]==e[13]&&a[14]==e[14],"Sample prim/object differs "+where);m.exact+=2;
        const bool lamp=tree?scene.dscene.light_tree_emitters[size_t(e[1])].light.id<0:scene.dscene.light_distribution[size_t(e[1])].prim<0;(lamp?m.lamps:m.triangles)++;}}
    if(tree&&inputs[i].target_emitter>=0){numeric(a[15],e[15],m,"Forward tree pdf "+where);m.forward+=e[15]>0;}
  }
  for(unsigned i=0;i<8;i++)require(output[i]==-1234.f&&output[output.size()-1-i]==-1234.f,"Light tree probe output guard overwritten");
}
// Host guards: corrupted tree tables are refused before any dispatch.
static void guards(WebGPUDevice &d,Metrics &m){
  const auto nodes=d.light_tree_nodes;const auto emitters=d.light_tree_emitters;const auto data=d.data;
  auto expect=[&](const char *reason){bool rejected=false;try{d.validate_lighting(true);}catch(const std::exception &e){rejected=std::string(e.what()).find(reason)!=std::string::npos;}
    d.light_tree_nodes=nodes;d.light_tree_emitters=emitters;d.data=data;require(rejected,std::string("Light tree guard did not reject: ")+reason);m.guards++;};
  d.validate_lighting(true);
  for(size_t i=0;i<nodes.size();i++)if(nodes[i].type==LIGHT_TREE_INNER){d.light_tree_nodes[i].inner.right_child=int(nodes.size());expect("Invalid native light tree child");break;}
  for(size_t i=0;i<nodes.size();i++)if(nodes[i].type>=LIGHT_TREE_LEAF){d.light_tree_nodes[i].num_emitters=int(emitters.size())+1;expect("Invalid native light tree leaf");break;}
  d.light_tree_nodes[0].type=LightTreeNodeType(3);expect("Invalid native light tree node type");
  for(size_t i=0;i<emitters.size();i++)if(emitters[i].light.id<0){d.light_tree_emitters[i].light.id=~int(d.light_records.size());expect("Invalid native light tree lamp");break;}
  d.data.light_link_sets[0].light_tree_root=uint(nodes.size());expect("Invalid native light link tree root");
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  reference_policy::use_reference_oracle_arithmetic();
  require(argc>=2,"Usage: light_tree_gate OUTPUT_DIRECTORY [CASE...]");const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
  std::vector<int> cases;for(int i=2;i<argc;i++)cases.push_back(std::stoi(argv[i]));if(cases.empty())for(int c=0;c<light_tree_cases;c++)cases.push_back(c);
  auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");Stats stats;Profiler profiler;
  auto owner=Device::create(infos[0],stats,profiler,true);auto &device=require_webgpu_device(owner.get(),"light_tree_gate device creation");Metrics total;
  const bool exchange_mode=reference_policy::prepare_only()||reference_policy::reference_only();
  for(int c:cases){
    require(c>=0&&c<light_tree_cases,"Unknown light tree case");std::cerr<<"light tree case="<<c<<'\n';
    auto folder=out/("case-"+std::to_string(c));std::filesystem::create_directories(folder);const auto prefix=(folder/"run").string();Metrics metrics;const std::string key="case-"+std::to_string(c);
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=false;params.use_texture_cache=false;
    Scene scene(params,&device);const Fixture f=fixture(scene,c);Progress progress;scene.device_update(&device,progress);healthy(device);require(!progress.get_error(),"Light tree scene preparation failed");
    auto &data=scene.dscene.data;
    require(bool(data.integrator.use_light_tree)==f.tree&&data.integrator.use_direct_light,"Unexpected native light tree configuration");
    require(bool(data.kernel_features&KERNEL_FEATURE_LIGHT_LINKING)==f.linking&&!(data.kernel_features&KERNEL_FEATURE_SHADOW_LINKING),"Unexpected native light linking configuration");
    if(f.tree)require(scene.dscene.light_tree_nodes.size()>1&&scene.dscene.light_tree_emitters.size()>1,"Missing native light tree");
    if(c==3){bool instance=false;for(size_t i=0;i<scene.dscene.light_tree_nodes.size();i++)instance|=scene.dscene.light_tree_nodes[i].type==LIGHT_TREE_INSTANCE;require(instance,"Instanced emitter fixture lacks instance nodes");}
    if(c==6){bool specialized=false;for(int s=1;s<LIGHT_LINK_SET_MAX;s++)specialized|=data.light_link_sets[s].light_tree_root!=0;require(specialized,"Light linking fixture lacks per-receiver trees");}
    if(f.motion)require(data.bvh.have_motion,"Moving emitter fixture lacks motion");
    // Probe states: points around the receivers, normals as ray D, receivers and forward targets cycling.
    std::vector<IntegratorStateCPU> states(probe_count);std::vector<LightTreeProbeInput> inputs(probe_count);
    std::vector<int> receivers;for(auto *o:f.receivers)receivers.push_back(int(o->get_device_index()));
    std::vector<std::pair<int,int>> targets;
    for(size_t e=0;e<scene.dscene.light_tree_emitters.size();e++){const auto &em=scene.dscene.light_tree_emitters[e];
      if(em.light.id<0)targets.push_back({0,int(e)});else if(em.object_id!=OBJECT_NONE)targets.push_back({em.object_id,int(e)});}
    for(unsigned i=0;i<probe_count;i++){auto &s=states[i];s.path.sample=i%64;s.path.rng_pixel=i*2654435761u;s.path.rng_offset=32*(i%4);s.path.bounce=i%3;
      s.path.flag=(i%3==0)?PATH_RAY_MIS_HAD_TRANSMISSION:0;s.ray.time=float(i%17)/16.f;
      s.ray.P=make_float3(float(i%23)*.3f-3.3f,float((i/23)%17)*.3f-2.4f,2.8f-float(i%31)*.2f);
      s.ray.D=normalize(make_float3(.3f*(int(i%5)-2),.2f*(int(i%7)-3)+.05f,i%11?-1.f:1.f));
      inputs[i].receiver=receivers[i%receivers.size()];inputs[i].forward_receiver=receivers[(i/3)%receivers.size()];
      if(!targets.empty()&&i%5!=4){inputs[i].target_object=targets[i%targets.size()].first;inputs[i].target_emitter=targets[i%targets.size()].second;}else{inputs[i].target_object=0;inputs[i].target_emitter=-1;}}
    struct PathInput {uint32_t sample,rng_pixel,rng_offset,bounce,flag;float time,P[3],D[3];LightTreeProbeInput in;};std::vector<PathInput> exchanged(probe_count);
    for(unsigned i=0;i<probe_count;i++){const auto &s=states[i];exchanged[i]={s.path.sample,s.path.rng_pixel,s.path.rng_offset,uint32_t(s.path.bounce),s.path.flag,s.ray.time,{s.ray.P.x,s.ray.P.y,s.ray.P.z},{s.ray.D.x,s.ray.D.y,s.ray.D.z},inputs[i]};}
    // Scene exchange (reference_policy.h): dump stops here; reference continues on the loaded tables.
    const bool exchange_only=reference_policy::exchange_scene(scene.dscene,key,{{"paths",exchanged.data(),exchanged.size()*sizeof(PathInput)}});
    if(reference_policy::reference_only())for(unsigned i=0;i<probe_count;i++){auto &s=states[i];const auto &in=exchanged[i];
      s.path.sample=in.sample;s.path.rng_pixel=in.rng_pixel;s.path.rng_offset=in.rng_offset;s.path.bounce=in.bounce;s.path.flag=in.flag;s.ray.time=in.time;
      s.ray.P=make_float3(in.P[0],in.P[1],in.P[2]);s.ray.D=make_float3(in.D[0],in.D[1],in.D[2]);inputs[i]=in.in;}
    const auto local_samples=original_light_tree_samples(scene.dscene,states,inputs);reference_policy::record_local(key+".samples",local_samples);
    const auto expected_samples=reference_policy::resolve(key+".samples",local_samples,(out/"reference-diagnostic.jsonl").string());
    std::vector<float> local(width*height*4),first;cpu_render(scene.dscene,local,metrics);reference_policy::record_local(key+".film",local);
    RenderCapture::file(prefix+".cpu.film.bin",local.data(),local.size()*4);
    const auto reference=reference_policy::resolve(key+".film",local,(out/"reference-diagnostic.jsonl").string());
    if(exchange_only)continue;
    if(c==0||c==6)guards(device,metrics);
    probe(device,scene,folder,states,inputs,expected_samples,metrics);
    require(data.film.pass_stride==4,"Light tree combined-only film expected");bool cancelled=false;Scheduler scheduler(&device,scene.film,&scene.dscene,&cancelled);
    scheduler.alloc_work_memory();scheduler.init_execution();healthy(device);auto buffers=scheduler.get_render_buffers();BufferParams bp;bp.width=width;bp.height=height;bp.pass_stride=4;buffers->reset(bp);
    for(unsigned repeat=0;repeat<2;repeat++){
      buffers->zero();std::unique_ptr<RenderCapture> capture;if(repeat==1)capture=std::make_unique<RenderCapture>(device,prefix);
      for(unsigned sample=0;sample<samples;sample++){
        scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Light tree reset failed");KernelWorkTile tile{};tile.w=width;tile.h=height;tile.stride=width;tile.num_samples=1;tile.start_sample=sample;
        scheduler.enqueue_work_tiles(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA,&tile,1,0,0);require(scheduler.update_queue_counter_and_cache(),"Light tree camera failed");
        for(unsigned step=0;step<200;step++){const auto kernel=scheduler.get_most_queued_kernel();if(kernel==DEVICE_KERNEL_NUM)break;require(step<199,"GPU light tree paths did not terminate");
          require(scheduler.enqueue_path_iteration(),"Native light tree scheduler stopped with pending paths");require(scheduler.update_queue_counter_and_cache(),"Light tree stage failed: "+device.error_message());metrics.stages++;}
      }
      if(capture)capture->finish(prefix,buffers->buffer.device_pointer,buffers->buffer.memory_size(),uintptr_t(device.state.path.queued_kernel),scheduler.capacity(),width,height,samples);
      buffers->buffer.copy_from_device();healthy(device);std::vector<float> actual(buffers->buffer.data(),buffers->buffer.data()+buffers->buffer.size());
      RenderCapture::file(prefix+(repeat?".repeat.film.bin":".gpu.film.bin"),actual.data(),actual.size()*4);
      for(size_t i=0;i<actual.size();i++){double error=std::abs(double(actual[i])-reference[i]);metrics.film_maximum=std::max(metrics.film_maximum,error);metrics.film++;
        metrics.film_strict+=error>1e-7+1e-7*std::abs(double(reference[i]));
        require(std::isfinite(actual[i])&&std::isfinite(reference[i])&&error<=5e-5+2e-4*std::abs(double(reference[i])),"Light tree film mismatch case="+std::to_string(c)+" component="+std::to_string(i)+" actual="+std::to_string(actual[i])+" expected="+std::to_string(reference[i]));
        if(repeat)metrics.repeat+=std::memcmp(&actual[i],&first[i],4)!=0;}
      if(!repeat)first=std::move(actual);
    }
    require(metrics.shadows>100&&metrics.visible>100,"Missing light tree direct lighting coverage");
    require(metrics.valid>1000&&metrics.selected>=metrics.valid,"Missing light tree probe coverage");
    if(f.tree)require(metrics.forward>100,"Missing forward tree pdf coverage");
    if(f.linking&&!f.tree)require(metrics.rejected>10,"Missing light linking rejections");
    if(c==2)require(metrics.nee>100,"Missing environment light NEE coverage");
    auto report=[&](std::ostream &stream){stream<<std::setprecision(17)<<"{\"passed\":true,\"case\":"<<c<<",\"tree\":"<<(f.tree?"true":"false")<<",\"linking\":"<<(f.linking?"true":"false")
      <<",\"treeNodes\":"<<scene.dscene.light_tree_nodes.size()<<",\"treeEmitters\":"<<scene.dscene.light_tree_emitters.size()
      <<",\"probeSelected\":"<<metrics.selected<<",\"probeValid\":"<<metrics.valid<<",\"probeLamps\":"<<metrics.lamps<<",\"probeTriangles\":"<<metrics.triangles<<",\"probeRejected\":"<<metrics.rejected
      <<",\"forwardPdfs\":"<<metrics.forward<<",\"exactChecks\":"<<metrics.exact<<",\"numericChecks\":"<<metrics.numeric<<",\"strictDifferences\":"<<metrics.strict<<",\"maximumAbsoluteError\":"<<metrics.maximum
      <<",\"guardCases\":"<<metrics.guards<<",\"shadows\":"<<metrics.shadows<<",\"visible\":"<<metrics.visible<<",\"lightNee\":"<<metrics.nee<<",\"stages\":"<<metrics.stages
      <<",\"filmChecks\":"<<metrics.film<<",\"filmStrictDifferences\":"<<metrics.film_strict<<",\"filmMaximumAbsoluteError\":"<<metrics.film_maximum<<",\"repeatBitwiseDifferences\":"<<metrics.repeat<<"}\n";};
    std::ofstream details(folder/"result.json");report(details);report(std::cerr);
    total.exact+=metrics.exact;total.numeric+=metrics.numeric;total.strict+=metrics.strict;total.maximum=std::max(total.maximum,metrics.maximum);total.selected+=metrics.selected;total.valid+=metrics.valid;
    total.forward+=metrics.forward;total.rejected+=metrics.rejected;total.guards+=metrics.guards;total.film+=metrics.film;total.film_strict+=metrics.film_strict;
    total.film_maximum=std::max(total.film_maximum,metrics.film_maximum);total.repeat+=metrics.repeat;total.shadows+=metrics.shadows;total.nee+=metrics.nee;
  }
  require(stats.mem_used==0,"Light tree gate leaked native allocations");std::ofstream(out/"library.txt")<<device.gpu.library_path;std::ofstream(out/"adapter.txt")<<device.gpu.adapter_name;
  if(exchange_mode)std::cout<<"{\"passed\":true,\"sceneExchangeOnly\":\""<<(reference_policy::prepare_only()?"dump":"reference")<<"\",\"cases\":"<<cases.size()<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
  else std::cout<<std::setprecision(17)<<"{\"passed\":true,\"cases\":"<<cases.size()<<",\"probeSelected\":"<<total.selected<<",\"probeValid\":"<<total.valid<<",\"forwardPdfs\":"<<total.forward
    <<",\"linkRejections\":"<<total.rejected<<",\"guardCases\":"<<total.guards<<",\"exactChecks\":"<<total.exact<<",\"numericChecks\":"<<total.numeric<<",\"strictDifferences\":"<<total.strict
    <<",\"maximumAbsoluteError\":"<<total.maximum<<",\"filmChecks\":"<<total.film<<",\"filmStrictDifferences\":"<<total.film_strict<<",\"filmMaximumAbsoluteError\":"<<total.film_maximum
    <<",\"repeatBitwiseDifferences\":"<<total.repeat<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<",\"pipelineReport\":"<<ccl::pipeline_process_report_json()<<"}\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
