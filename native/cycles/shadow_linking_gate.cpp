// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Shadow linking gate (KERNEL_FEATURE_SHADOW_LINKING: blocker sets, INTERSECT_DEDICATED_LIGHT, SHADE_DEDICATED_LIGHT).
// Per case, against the unmodified pinned CPU kernels on the same device-prepared scene tables:
//  - state: real path states (every path the CPU render queues at the dedicated light, up to row_limit) through the WebGPU
//    kernels: the successor kernel, the picked emitter, the hit count (dedicated_light_weight), the stored and restored
//    self primitives, every integer of the dedicated shadow path and the shadow traversal's blocker-set decision
//    (and hit count) are exact; the MIS weight (from both samplers), throughput and rays practical, strict reported;
//  - image: the native scheduler's GPU render against the CPU render, twice (the repeat is captured for replays).
// Cases: 0 lamps and emissive panels, flat CDF; 1 the same on the light tree; 2 with an HDRI world, flat; 3 with an HDRI
// world, tree; 4 transparent blockers, an emissive transparent panel and non-constant emitters (SHADE_LIGHT_NEE), flat;
// 5 light linking and shadow linking, flat; 6 the same on the tree; 7 a moving blocker, a moving emitter and a deforming emitter, tree; 8 a
// glass receiver (PATH_RAY_MIS_HAD_TRANSMISSION forward pdfs), tree; 9 a scattering box with an HDRI world (phase scatter to the dedicated light kernels, shadow transmittance), flat;
// 10 shadow caustics from a linked point lamp past a linked blocker, flat; 11 the same on the tree; 12 a moving blocker and one emitter both moving and deforming, tree.
// The combinations of the pinned kernel (each its own case): 13 a shadow catcher floor (the split paths through both kernels, the catcher passes), flat;
// 14 fast GI with additive AO (AO shadow rays beside the dedicated ones), flat; 15 the AO pass, tree; 16 a scattering box with spot and area lamps, flat;
// 17 a scattering box with emissive triangles sampled (their next-event and dedicated rays from phase scatters), flat; 18 a scattering box with
// lamps, spot, area and triangle lights on the light tree (the segment's previous_dt and importance in the forward pdfs); 19 a scattering box with the
// volume and light passes, flat; 20 an emissive scattering box with the emission pass and a light group, flat.
// Shadow linking switched off on a device that rendered it (the pinned kernel tests KERNEL_FEATURE_SHADOW_LINKING at run time,
// shadow_linking_scene_need_shadow_ray): 21 case 0's scene rendered, then every membership and blocker set reset and rendered again;
// 22 case 0's scene rendered, then a second scene without shadow linking on the same device. Their image is judged against the CPU
// kernels and no path may reach the dedicated light kernels.
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
#include "shadow_linking_reference.h"
#include "volume_reference.h"
#include "mnee_reference.h"
#include "environment_fixture.h"
#include "object_motion_fixture.h"
#include "deforming_motion_fixture.h"
#include "transparent_shadow_fixture.h"
#include "mnee_fixture.h"
#include "scene/scene.h"
#include "scene/background.h"
#include "scene/camera.h"
#include "scene/integrator.h"
#include "scene/light.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/pass.h"
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
#include <sstream>
using namespace ccl;
#include "curve_gate_tuft.h"
using namespace wavefront_test;
constexpr unsigned width=32,height=24,samples=16;
constexpr size_t row_limit=3072,miss_limit=512,shadow_row_limit=4096;
constexpr int shadow_linking_cases=23;
struct Metrics {uint64_t splits=0,catcher_rows=0,ao=0,ao_visible=0,volume_rows=0,volume_shapes=0,volume_triangles=0,exact=0,numeric=0,strict=0,rows=0,none=0,lamps=0,triangles=0,several=0,mis_weights=0,mis_below_one=0,shadow_paths=0,blocked=0,clear=0,nee=0,transparent_hits=0,guards=0,film=0,film_strict=0,repeat=0,cpu_dedicated=0,cpu_shaded=0,stages=0,motion_words=0,motion_different=0,mnee_sampled=0,nee_rays=0,nee_clear=0,nee_blocked=0,linked_through=0;double maximum=0,film_maximum=0;};
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
static Shader *glass_material(Scene &scene){
  auto graph=make_unique<ShaderGraph>();auto glass=graph->create_node<GlassBsdfNode>();glass->set_distribution(CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID);glass->set_roughness(.25f);glass->set_IOR(1.45f);
  glass->set_color(make_float3(.9,.95,1));auto diffuse=graph->create_node<DiffuseBsdfNode>();diffuse->set_color(make_float3(.55,.32,.18));diffuse->set_roughness(.31f);
  auto mix=graph->create_node<MixClosureNode>();mix->set_fac(.35f);graph->connect(glass->output("BSDF"),mix->input("Closure1"));graph->connect(diffuse->output("BSDF"),mix->input("Closure2"));
  graph->connect(mix->output("Closure"),graph->output()->input("Surface"));auto shader=scene.create_node<Shader>();shader->set_graph(std::move(graph));shader->tag_update(&scene);return shader;
}
static Shader *emission_material(Scene &scene,float strength,bool transparent){
  auto graph=make_unique<ShaderGraph>();auto emission=graph->create_node<EmissionNode>();emission->set_color(make_float3(1,.9,.75));emission->set_strength(strength);ShaderOutput *surface=emission->output("Emission");
  if(transparent){auto see_through=graph->create_node<TransparentBsdfNode>();see_through->set_color(make_float3(.8,.7,.6));auto mix=graph->create_node<MixClosureNode>();mix->set_fac(.5f);
    graph->connect(surface,mix->input("Closure1"));graph->connect(see_through->output("BSDF"),mix->input("Closure2"));surface=mix->output("Closure");}
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
static Object *lamp(Scene &scene,Light *light,Shader *shader,const Transform &tfm,float3 strength){
  light->set_strength(strength);light->set_use_mis(true);array<Node*> slots;slots.push_back_slow(shader);light->set_used_shaders(slots);
  auto object=scene.create_node<Object>();object->set_geometry(light);object->set_tfm(tfm);return object;
}
static Transform aim(float3 at,float yaw,float pitch){return transform_translate(at)*transform_rotate(yaw,make_float3(0,1,0))*transform_rotate(pitch,make_float3(1,0,0));}
constexpr uint64_t all_sets=LIGHT_LINK_MASK_ALL,not_center=all_sets&~(1ull<<1),not_shelf=all_sets&~(1ull<<2),neither=not_center&not_shelf;
// catcher: case 13; ao: 14 (fast GI, additive) and 15 (the AO pass); shapes: spot and area lamps with volumes (16, 18-20); triangles: sampled
// emissive triangles with volumes (17-20); passes: 19; emission: 20.
struct Fixture {bool tree=false,linking=false,motion=false,world=false,transparent=false,volume=false,caustics=false,catcher=false,ao=false,shapes=false,triangles=false,passes=false,emission=false;};
static Fixture fixture(Scene &scene,int c){
  Fixture f;f.tree=c==1||c==3||c==6||c==7||c==8||c==11||c==12||c==15||c==18;f.linking=c==5||c==6;f.motion=c==7||c==12;f.world=c==2||c==3||c==9;f.transparent=c==4;f.volume=c==9||c>=16;f.caustics=c==10||c==11;
  f.catcher=c==13;f.ao=c==14||c==15;f.shapes=c==16||c>=18;f.triangles=c>=17;f.passes=c==19;f.emission=c==20;
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(f.tree);scene.integrator->set_use_adaptive_sampling(false);
  scene.integrator->set_aa_samples(samples);scene.integrator->set_min_bounce(3);scene.integrator->set_max_bounce(4);scene.integrator->set_filter_glossy(0);
  scene.film->set_use_sample_count(false);scene.camera->set_full_width(width);scene.camera->set_full_height(height);scene.camera->set_screen_size(width,height);
  scene.camera->set_fov(.9f);scene.camera->set_matrix(transform_translate(make_float3(0,0,-7)));scene.camera->compute_auto_viewplane();
  auto diffuse=material(scene,0),mixed=material(scene,1);
  auto wall=quad(scene,diffuse,{make_float3(-4,-3,3),make_float3(4,-3,3),make_float3(4,3,3),make_float3(-4,3,3)});
  auto floor=quad(scene,mixed,{make_float3(-4,-2,-3),make_float3(4,-2,-3),make_float3(4,-2,3),make_float3(-4,-2,3)});
  // The blockers: the centre quad (blocker set 1), the shelf (set 2), the column (default set 0: blocks every light).
  auto center=quad(scene,c==8?glass_material(scene):f.transparent?transparent_material(scene,1):mixed,{make_float3(-.8f,-1.2f,0),make_float3(.8f,-1.2f,0),make_float3(.8f,1.1f,0),make_float3(-.8f,1.1f,0)});
  auto shelf=quad(scene,mixed,{make_float3(-3.2f,.9f,-1.2f),make_float3(-1.1f,.9f,-1.2f),make_float3(-1.1f,.9f,2.2f),make_float3(-3.2f,.9f,2.2f)});
  auto column=quad(scene,diffuse,{make_float3(1.5f,-2,1),make_float3(2.3f,-2,1),make_float3(2.3f,1.2f,1),make_float3(1.5f,1.2f,1)});(void)column;
  center->set_blocker_shadow_set(1);shelf->set_blocker_shadow_set(2);
  Shader *world=nullptr;
  if(f.world){world=environment_world(scene,EnvironmentWorld::Hdri,0);environment_light(scene,world,true,64);}
  else{auto background=make_unique<ShaderGraph>();auto bg=background->create_node<BackgroundNode>();bg->set_color(make_float3(.02f,.025f,.03f));background->connect(bg->output("Background"),background->output()->input("Surface"));
    auto w=scene.create_node<Shader>();w->set_graph(std::move(background));w->tag_update(&scene);scene.background->set_shader(w);}
  Object *box=nullptr;
  if(f.volume){auto graph=make_unique<ShaderGraph>();auto medium=graph->create_node<ScatterVolumeNode>();medium->set_density(.09f);medium->set_color(make_float3(.8f,.75f,.7f));medium->set_anisotropy(.2f);
    // Case 20: the medium also emits (film_write_volume_emission: the emission pass and the light group).
    if(f.emission){auto glow=graph->create_node<EmissionNode>();glow->set_color(make_float3(1,.6f,.3f));glow->set_strength(.06f);auto add=graph->create_node<AddClosureNode>();
      graph->connect(medium->output("Volume"),add->input("Closure1"));graph->connect(glow->output("Emission"),add->input("Closure2"));graph->connect(add->output("Closure"),graph->output()->input("Volume"));}
    else{graph->connect(medium->output("Volume"),graph->output()->input("Volume"));}auto shader=scene.create_node<Shader>();shader->set_graph(std::move(graph));shader->tag_update(&scene);
    auto mesh=scene.create_node<Mesh>();array<Node*> slots;slots.push_back_slow(shader);mesh->set_used_shaders(slots);mesh->resize_mesh(8,12);const float3 lo=make_float3(-3.5f,-1.9f,-2.f),hi=make_float3(3.5f,1.5f,2.8f);
    for(int i=0;i<8;i++)mesh->get_position_for_write()[i]=make_float3(i&1?hi.x:lo.x,i&2?hi.y:lo.y,i&4?hi.z:lo.z);
    constexpr int tris[]={0,2,1,1,2,3,4,5,6,5,7,6,0,4,2,2,4,6,1,3,5,3,7,5,0,1,4,1,5,4,2,6,3,3,6,7};
    std::copy_n(tris,36,mesh->get_triangles().data());std::ranges::fill(mesh->get_smooth(),false);std::ranges::fill(mesh->get_shader(),0);mesh->tag_triangles_modified();box=scene.create_node<Object>();box->set_geometry(mesh);scene.integrator->set_max_volume_bounce(3);}
  if(f.catcher){floor->set_is_shadow_catcher(true);scene.background->set_transparent(true);}
  if(f.ao){scene.integrator->set_ao_distance(1.6f);
    if(c==14){scene.integrator->set_ao_bounces(1);scene.integrator->set_ao_factor(.5f);scene.integrator->set_ao_additive_factor(.65f);}
    else{auto pass=scene.create_node<Pass>();pass->set_type(PASS_AO);pass->set_name(ustring("AO"));}}
  if(f.passes)for(auto [type,name]:{std::pair{PASS_VOLUME_DIRECT,"Volume Direct"},std::pair{PASS_VOLUME_INDIRECT,"Volume Indirect"},std::pair{PASS_DIFFUSE_DIRECT,"Diffuse Direct"},
                                     std::pair{PASS_DIFFUSE_INDIRECT,"Diffuse Indirect"},std::pair{PASS_GLOSSY_DIRECT,"Glossy Direct"},std::pair{PASS_GLOSSY_INDIRECT,"Glossy Indirect"}}){
    auto pass=scene.create_node<Pass>();pass->set_type(type);pass->set_name(ustring(name));}
  if(f.emission){auto pass=scene.create_node<Pass>();pass->set_type(PASS_EMISSION);pass->set_name(ustring("Emission"));
    // A named Combined pass first: Film::finalize_passes merges an unnamed combined pass into the first named one of the same type and mode.
    auto combined=scene.create_node<Pass>();combined->set_type(PASS_COMBINED);combined->set_name(ustring("Combined"));
    auto group=scene.create_node<Pass>();group->set_type(PASS_COMBINED);group->set_name(ustring("Combined_one"));group->set_lightgroup(ustring("one"));box->set_lightgroup(ustring("one"));}
  if(f.caustics){floor->set_is_caustics_receiver(true);wall->set_is_caustics_receiver(true);auto ball=mnee_sphere(scene,mnee_shader(scene,1),make_float3(.2f,-1.f,-1.4f),.9f);ball->set_is_caustics_caster(true);}
  auto constant=emission_material(scene,1.f,false),panel_emission=emission_material(scene,3.f,false),veil_emission=emission_material(scene,2.f,true);
  const float3 warm=make_float3(60,45,30),cool=make_float3(25,40,60);
  auto a=scene.create_node<PointLight>();a->set_radius(.7f);a->set_is_sphere(true);auto la=lamp(scene,a,constant,aim(make_float3(-2.3f,2.1f,-1.4f),0,0),warm);
  auto b=scene.create_node<PointLight>();b->set_radius(.6f);b->set_is_sphere(false);auto lb=lamp(scene,b,constant,aim(make_float3(2.3f,2.f,-1.7f),3.1f,-.4f),cool);
  // With volumes (case 9) lamps and a sun only, the panels by forward hits; spot and area lamps (shapes) and sampled panels (triangles) in cases 16-20.
  Object *ls=nullptr,*lc=nullptr;
  if(!f.volume||f.shapes){auto s=scene.create_node<SpotLight>();s->set_radius(.5f);s->set_is_sphere(true);s->set_angle(1.2f);s->set_smooth(.3f);ls=lamp(scene,s,c==4?environment_emitter(scene,0):constant,aim(make_float3(0,2.5f,-1.2f),0,1.2f),warm*.8f);
    auto area=scene.create_node<AreaLight>();area->set_sizeu(1.6f);area->set_sizev(1.1f);area->set_spread(1.3f);area->set_normalize(true);lc=lamp(scene,area,constant,aim(make_float3(-1.5f,1.5f,-2.6f),2.8f,-.4f),cool*.8f);}
  auto def=scene.create_node<PointLight>();def->set_radius(.4f);def->set_is_sphere(true);lamp(scene,def,constant,aim(make_float3(.8f,1.8f,-2.8f),0,0),warm*.3f);
  auto sun=scene.create_node<SunLight>();sun->set_angle(.35f);sun->set_normalize(false);auto lsun=lamp(scene,sun,constant,aim(make_float3(0,0,0),2.85f,-.5f),make_float3(.8,1.1,1.5));
  auto panel=quad(scene,panel_emission,{make_float3(-.9f,-.6f,0),make_float3(.9f,-.6f,0),make_float3(.9f,.6f,0),make_float3(-.9f,.6f,0)});panel->set_tfm(aim(make_float3(1.65f,1.7f,-.9f),.18f,.5f));
  auto plain=quad(scene,panel_emission,{make_float3(-.5f,-.4f,0),make_float3(.5f,-.4f,0),make_float3(.5f,.4f,0),make_float3(-.5f,.4f,0)});plain->set_tfm(aim(make_float3(-.4f,2.4f,1.5f),0,1.2f));
  if(f.caustics){a->set_use_caustics(true);a->set_radius(0.f);a->set_is_sphere(false);}
  la->set_shadow_set_membership(not_shelf);lb->set_shadow_set_membership(not_center);if(ls){ls->set_shadow_set_membership(neither);lc->set_shadow_set_membership(not_center);}lsun->set_shadow_set_membership(not_center);panel->set_shadow_set_membership(neither);
  Shader *emitter_nonconstant=nullptr;
  if(c==4){emitter_nonconstant=environment_emitter(scene,1);auto textured=quad(scene,emitter_nonconstant,{make_float3(-.6f,-.5f,0),make_float3(.6f,-.5f,0),make_float3(.6f,.5f,0),make_float3(-.6f,.5f,0)});textured->set_tfm(aim(make_float3(-.2f,.3f,1.6f),0,0));textured->set_shadow_set_membership(not_center);
    // A transparent emitter in front of the shelf: the pick walks through it and takes it as a candidate.
    auto veil=quad(scene,veil_emission,{make_float3(-1,-.8f,0),make_float3(1,-.8f,0),make_float3(1,.8f,0),make_float3(-1,.8f,0)});veil->set_tfm(aim(make_float3(-1.2f,1.5f,.6f),.2f,.4f));veil->set_shadow_set_membership(not_shelf);}
  if(c==7){scene.integrator->set_motion_blur(true);scene.camera->set_shuttertime(.6f);object_motion_fixture(scene,*center,2);object_motion_fixture(scene,*panel,1);deforming_motion_fixture(scene,*plain,1);plain->set_shadow_set_membership(not_shelf);}
  // One emitter both moving and deforming: its next-event rays end on the other triangle's plane near the diagonal.
  if(c==12){scene.integrator->set_motion_blur(true);scene.camera->set_shuttertime(.6f);object_motion_fixture(scene,*center,2);deforming_motion_fixture(scene,*panel,1);object_motion_fixture(scene,*panel,1);}
  if(f.linking){wall->set_receiver_light_set(1);floor->set_receiver_light_set(2);
    la->set_light_set_membership((1ull<<0)|(1ull<<1));lb->set_light_set_membership((1ull<<0)|(1ull<<2));if(ls)ls->set_light_set_membership((1ull<<0)|(1ull<<1)|(1ull<<2));panel->set_light_set_membership((1ull<<0)|(1ull<<2));}
  for(auto shader:scene.shaders)shader->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
  if(!f.volume||f.triangles)panel_emission->set_emission_sampling_method(EMISSION_SAMPLING_FRONT_BACK);
  // Case 13: one lamp is itself a shadow catcher (no LIGHT_EXCLUDE_SHADOW_CATCHER), the others are excluded from catcher-pass rays; case 20: a lamp in the light group.
  if(f.catcher)lb->set_is_shadow_catcher(true);
  if(f.emission)la->set_lightgroup(ustring("one"));
  veil_emission->set_emission_sampling_method(EMISSION_SAMPLING_FRONT_BACK);
  if(emitter_nonconstant)emitter_nonconstant->set_emission_sampling_method(EMISSION_SAMPLING_FRONT);
  if(world)world->set_emission_sampling_method(EMISSION_SAMPLING_AUTO);
  return f;
}
// The CPU shadow catcher split (state + 1 in the CPU kernel) into the next free state (shadow_catcher_gate.cpp closest_split, no sort keys).
static void closest_split(DeviceScene &scene,std::vector<int> ids,std::vector<IntegratorStateCPU> &states,std::vector<float> &film,uint32_t &next,Metrics &m){
  std::sort(ids.begin(),ids.end());
  for(int i:ids){
    std::vector<IntegratorStateCPU> pair{states[i],states[i]};const uint32_t before=pair[0].path.flag;
    original_closest(scene,{0},pair,film);states[i]=pair[0];
    if(!(before&PATH_RAY_SHADOW_CATCHER_HIT)&&(states[i].path.flag&PATH_RAY_SHADOW_CATCHER_HIT)){
      const uint32_t j=next++;require(j<states.size()&&!states[j].path.queued_kernel,"CPU shadow catcher split slot is not free");
      IntegratorStateCPU copy=states[i];const auto kernel=pair[1].path.queued_kernel;
      copy.path.flag|=PATH_RAY_SHADOW_CATCHER_PASS;if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND)copy.path.flag|=PATH_RAY_SHADOW_CATCHER_BACKGROUND;
      require(copy.path.flag==pair[1].path.flag,"CPU shadow catcher split flags");copy.path.queued_kernel=kernel;states[j]=copy;m.splits++;
    }
  }
}
static void cpu_render(DeviceScene &scene,const Fixture &f,int c,std::vector<float> &film,std::vector<DedicatedRow> &rows,std::vector<ShadowRow> &shadow_rows,Metrics &metrics){
  size_t misses=0;const size_t pixels=width*height;
  // A shadow catcher scene has a split state per path (the CPU kernel's state + 1).
  std::vector<IntegratorStateCPU> state(f.catcher?2*pixels:pixels);std::vector<IntegratorShadowStateCPU> shadow(state.size()),ao_shadow(state.size());
  for(unsigned sample=0;sample<samples;sample++){
    std::fill(state.begin(),state.end(),IntegratorStateCPU{});KernelWorkTile tile{};tile.w=width;tile.h=height;tile.stride=width;tile.num_samples=1;tile.start_sample=sample;
    original_camera(scene,{tile},0,state,film);uint32_t next=uint32_t(pixels);
    for(unsigned step=0;step<200;step++){
      std::map<DeviceKernel,std::vector<int>> queues;
      for(size_t i=0;i<state.size();i++)if(state[i].path.queued_kernel)queues[DeviceKernel(state[i].path.queued_kernel)].push_back(i);
      if(queues.empty())break;
      require(step<199,"CPU shadow linking paths did not terminate");
      for(auto &[kernel,ids]:queues){
        if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST){if(f.catcher)closest_split(scene,ids,state,film,next,metrics);else original_closest(scene,ids,state,film);}
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK)original_volume_stack(scene,ids,state);
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_MNEE){original_mnee(scene,ids,state);for(int i:ids)metrics.mnee_sampled+=(state[i].path.mnee&PATH_MNEE_SAMPLED)!=0;}
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND)original_background(scene,ids,state,film);
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD)original_light_forward(scene,ids,state,film);
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE||kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_DEDICATED_LIGHT||kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME){
          if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE)original_surface(scene,ids,state,film);else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME)original_volume(scene,ids,state,film);else{metrics.cpu_shaded+=ids.size();original_dedicated(scene,kernel,ids,state);}
          std::vector<int> nee,pending;
          for(int i:ids){if(!state[i].shadow.shadow_path.queued_kernel)continue;shadow[i]=state[i].shadow;
            if(shadow[i].shadow_path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_NEE)nee.push_back(i);else pending.push_back(i);}
          original_light_nee(scene,nee,shadow,film);
          for(int i:nee)if(shadow[i].shadow_path.queued_kernel){require(shadow[i].shadow_path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,"Unexpected CPU light NEE successor");pending.push_back(i);}
          // The first next-event rays towards shadow-linked emitters: their blocker-set decisions.
          // From case 16 on, the volume's next-event rays too (spot, area and triangle lights from scatter positions).
          if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE||(c>=16&&kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME))for(int i:pending)if(shadow_rows.size()<shadow_row_limit&&shadow[i].shadow_ray.self_light_object!=OBJECT_NONE&&scene.objects[shadow[i].shadow_ray.self_light_object].shadow_set_membership!=LIGHT_LINK_MASK_ALL)shadow_rows.push_back(original_shadow_row(scene,shadow[i]));
          original_shadow(scene,DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,pending,shadow,film);
          std::vector<int> clear;for(int i:pending)if(shadow[i].shadow_path.queued_kernel)clear.push_back(i);
          original_shadow(scene,DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW,clear,shadow,film);
          for(int i:ids)if(state[i].shadow.shadow_path.queued_kernel){state[i].shadow.shadow_path.queued_kernel=0;}
          // Ambient occlusion shadow paths (state->ao in the CPU kernel): intersect, then shade the unoccluded ones.
          if(f.ao&&kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE){std::vector<int> ao;for(int i:ids)if(state[i].ao.shadow_path.queued_kernel){ao_shadow[i]=state[i].ao;ao.push_back(i);}
            metrics.ao+=ao.size();original_shadow(scene,DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,ao,ao_shadow,film);
            std::vector<int> ao_clear;for(int i:ao)if(ao_shadow[i].shadow_path.queued_kernel)ao_clear.push_back(i);
            metrics.ao_visible+=ao_clear.size();original_shadow(scene,DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW,ao_clear,ao_shadow,film);
            for(int i:ao)state[i].ao.shadow_path.queued_kernel=0;}
        }
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_DEDICATED_LIGHT){
          metrics.cpu_dedicated+=ids.size();
          // Every path that picks an emitter while the table has room, and a bounded number that pick none.
          for(int i:ids)if(rows.size()<row_limit){auto row=original_dedicated_row(scene,state[i]);const bool picked=row.after_intersect==DEVICE_KERNEL_INTEGRATOR_SHADE_DEDICATED_LIGHT;
            if(picked||misses<miss_limit){rows.push_back(row);misses+=!picked;}}
          original_dedicated(scene,kernel,ids,state);
        }
        else throw std::runtime_error("Unexpected CPU shadow linking kernel "+std::to_string(int(kernel)));
      }
    }
  }
}
// The row's path state, as the GPU kernels read it (the CPU state layout; wavefront_test.h uploads the allocated fields).
static void fill_state(IntegratorStateCPU &s,const DedicatedRow &r){
  s=IntegratorStateCPU{};s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_INTERSECT_DEDICATED_LIGHT;
  s.ray.P=make_float3(r.P[0],r.P[1],r.P[2]);s.ray.D=make_float3(r.D[0],r.D[1],r.D[2]);s.ray.tmin=r.tmin;s.ray.tmax=r.tmax;s.ray.time=r.time;s.ray.dP=r.dP;s.ray.dD=r.dD;s.ray.previous_dt=r.previous_dt;
  s.isect.t=r.isect_t;s.isect.u=r.isect_u;s.isect.v=r.isect_v;s.isect.object=r.isect_object;s.isect.prim=r.isect_prim;s.isect.type=r.isect_type;
  s.path.flag=r.flag;s.path.visibility=r.visibility;s.path.rng_pixel=r.rng_pixel;s.path.rng_offset=r.rng_offset;s.path.sample=r.sample;s.path.bounce=r.bounce;s.path.diffuse_bounce=r.diffuse_bounce;s.path.glossy_bounce=r.glossy_bounce;
  s.path.transmission_bounce=r.transmission_bounce;s.path.transparent_bounce=r.transparent_bounce;s.path.volume_bounds_bounce=r.volume_bounds_bounce;s.path.render_pixel_index=r.render_pixel_index;
  s.path.throughput=make_float3(r.throughput[0],r.throughput[1],r.throughput[2]);s.path.mis_ray_pdf=r.mis_ray_pdf;s.path.mis_origin_n=make_float3(r.mis_origin_n[0],r.mis_origin_n[1],r.mis_origin_n[2]);s.path.mis_ray_object=r.mis_ray_object;
  s.path.pass_diffuse_weight=make_float3(r.pass_diffuse[0],r.pass_diffuse[1],r.pass_diffuse[2]);s.path.pass_glossy_weight=make_float3(r.pass_glossy[0],r.pass_glossy[1],r.pass_glossy[2]);
}
static void state_gate(WebGPUDevice &d,Scene &scene,const std::vector<DedicatedRow> &rows,Metrics &m){
  bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);
  const int n=int(rows.size());require(scheduler.capacity()>=n,"Shadow linking rows need more path states");
  const auto layout=fields(d);std::vector<IntegratorStateCPU> states(rows.size());for(size_t i=0;i<rows.size();i++)fill_state(states[i],rows[i]);upload(d,layout,states);
  device_vector<int> selected(&d,"dedicated_indices",MEM_READ_WRITE);selected.alloc(std::max<size_t>(1,rows.size()));for(int i=0;i<n;i++)selected[i]=i;selected.copy_to_device();
  device_vector<float> film(&d,"dedicated_film",MEM_READ_WRITE);film.alloc(width*height*4);std::fill_n(film.data(),film.size(),0.f);film.copy_to_device();
  std::vector<uint16_t> idle(d.remaining(uintptr_t(d.state.shadow_path.queued_kernel))/2,0);d.upload(uintptr_t(d.state.shadow_path.queued_kernel),idle.data(),idle.size()*2);
  const uint32_t zero=0;d.upload(uintptr_t(d.state.next_shadow_path_index),&zero,4);
  auto counters=[&](std::initializer_list<std::pair<DeviceKernel,uint32_t>> values){IntegratorQueueCounter c{};for(auto &[kernel,count]:values)c.num_queued[kernel]=count;d.upload(uintptr_t(d.state.queue_counter),&c,sizeof(c));};
  auto check_counters=[&](std::initializer_list<std::pair<DeviceKernel,uint32_t>> values,const std::string &where){IntegratorQueueCounter wanted{},after{};for(auto &[kernel,count]:values)wanted.num_queued[kernel]=count;
    d.download(uintptr_t(d.state.queue_counter),&after,sizeof(after));require(!std::memcmp(&wanted,&after,sizeof(after)),where+" queue counters");m.exact++;};
  auto queue=d.gpu_queue_create();
  counters({{DEVICE_KERNEL_INTEGRATOR_INTERSECT_DEDICATED_LIGHT,uint32_t(n)}});
  {int work=n;DeviceKernelArguments args;args.add(&selected.device_pointer);args.add(&work);
   require(queue->enqueue(DEVICE_KERNEL_INTEGRATOR_INTERSECT_DEDICATED_LIGHT,n,args),"Intersect dedicated light: "+d.error_message());require(queue->synchronize(),"Intersect dedicated light: "+d.error_message());}
  auto after=download(d,layout,unsigned(n));std::vector<int> shade;
  auto exact=[&](int64_t actual,int64_t expected,const std::string &where){m.exact++;require(actual==expected,where+" got="+std::to_string(actual)+" expected="+std::to_string(expected));};
  auto float_exact=[&](float actual,float expected,const std::string &where){m.exact++;uint32_t a,e;std::memcpy(&a,&actual,4);std::memcpy(&e,&expected,4);require(a==e,where+" got="+std::to_string(actual)+" expected="+std::to_string(expected));};
  for(int i=0;i<n;i++){const auto &r=rows[i];const auto &s=after[i];const std::string w=" path="+std::to_string(i);m.rows++;
    exact(s.path.queued_kernel,r.after_intersect,"Dedicated pick successor"+w);
    if(r.after_intersect!=DEVICE_KERNEL_INTEGRATOR_SHADE_DEDICATED_LIGHT){m.none++;
      exact(s.isect.object,r.isect_object,"Untouched self object"+w);exact(s.isect.prim,r.isect_prim,"Untouched self prim"+w);continue;}
    shade.push_back(i);
    exact(s.isect.object,r.out_object,"Picked object"+w);exact(s.isect.prim,r.out_prim,"Picked prim"+w);exact(s.isect.type,r.out_type,"Picked type"+w);
    float_exact(s.shadow_link.dedicated_light_weight,r.weight,"Hit count"+w);exact(s.shadow_link.last_isect_prim,r.last_prim,"Stored self prim"+w);exact(s.shadow_link.last_isect_object,r.last_object,"Stored self object"+w);
    numeric(s.isect.t,r.out_t,m,"Picked t"+w);numeric(s.isect.u,r.out_u,m,"Picked u"+w);numeric(s.isect.v,r.out_v,m,"Picked v"+w);
    (r.out_type==PRIMITIVE_LAMP?m.lamps:m.triangles)++;m.several+=r.weight>1.f;m.catcher_rows+=(r.flag&PATH_RAY_SHADOW_CATCHER_PASS)!=0;
    // Dedicated rays from phase scatters: spot and area lamps, emissive triangles.
    if(r.visibility&PATH_RAY_VISIBILITY_VOLUME_SCATTER){m.volume_rows++;
      if(r.out_type==PRIMITIVE_LAMP){const int type=scene.dscene.lights[r.out_prim].type;m.volume_shapes+=type==LIGHT_SPOT||type==LIGHT_AREA;}else m.volume_triangles++;}
  }
  check_counters({{DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST,uint32_t(n-shade.size())},{DEVICE_KERNEL_INTEGRATOR_SHADE_DEDICATED_LIGHT,uint32_t(shade.size())}},"Intersect dedicated light");
  if(shade.empty())return;
  {device_vector<int> list(&d,"dedicated_shade",MEM_READ_WRITE);list.alloc(shade.size());std::copy(shade.begin(),shade.end(),list.data());list.copy_to_device();
   counters({{DEVICE_KERNEL_INTEGRATOR_SHADE_DEDICATED_LIGHT,uint32_t(shade.size())}});const uint32_t none=0;d.upload(uintptr_t(d.state.next_shadow_path_index),&none,4);
   int work=int(shade.size());DeviceKernelArguments args;args.add(&list.device_pointer);args.add(&film.device_pointer);args.add(&work);
   require(queue->enqueue(DEVICE_KERNEL_INTEGRATOR_SHADE_DEDICATED_LIGHT,work,args),"Shade dedicated light: "+d.error_message());require(queue->synchronize(),"Shade dedicated light: "+d.error_message());}
  auto restored=download(d,layout,unsigned(n));uint32_t allocated=0;d.download(uintptr_t(d.state.next_shadow_path_index),&allocated,4);exact(allocated,int64_t(shade.size()),"Reserved shadow slots");
  std::vector<uint32_t> records(shade.size()*8);d.download(d.dedicated_records,records.data(),records.size()*4);
  // The dedicated shadow path (slot = position in the dispatch, control op 5).
  struct Read {std::vector<uint8_t> bytes;const char *name;};
  auto read=[&](const void *pointer,size_t stride){std::vector<uint8_t> out(shade.size()*stride);d.download(uint32_t(uintptr_t(pointer)),out.data(),out.size());return out;};
  auto &sp=d.state.shadow_path;auto &sr=d.state.shadow_ray;const auto kernels=read(sp.queued_kernel,2),throughput=read(sp.throughput,12),P=read(sr.P,12),D=read(sr.D,12),tmin=read(sr.tmin,4),tmax=read(sr.tmax,4),time=read(sr.time,4),dP=read(sr.dP,4),dD=read(sr.dD,4),
    lobj=read(sr.self_light_object,4),lprim=read(sr.self_light_prim,4),self_object=read(d.state.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].object,4),self_prim=read(d.state.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].prim,4),
    bounce=read(sp.bounce,2),diffuse=read(sp.diffuse_bounce,2),glossy=read(sp.glossy_bounce,2),transmission=read(sp.transmission_bounce,2),transparent=read(sp.transparent_bounce,2),bounds=read(sp.volume_bounds_bounce,2),group=read(sp.lightgroup,1),
    flag=read(sp.flag,4),visibility=read(sp.visibility,1),pixel=read(sp.render_pixel_index,4),sample_id=read(sp.sample,4),rng_pixel=read(sp.rng_pixel,4),rng_offset=read(sp.rng_offset,2);
  std::vector<uint8_t> average;if(sp.bsdf_eval_average)average=read(sp.bsdf_eval_average,4);
  // shadow_isect[0]: the picked intersection of a path queued for SHADE_LIGHT_NEE (integrator_state_write_shadow_light_isect).
  const auto &li=d.state.shadow_isect[0];const auto li_t=read(li.t,4),li_u=read(li.u,4),li_v=read(li.v,4),li_object=read(li.object,4),li_prim=read(li.prim,4),li_type=read(li.type,4);
  auto u16=[](const std::vector<uint8_t> &v,size_t k){uint16_t x;std::memcpy(&x,v.data()+k*2,2);return int64_t(x);};auto s32=[](const std::vector<uint8_t> &v,size_t k){int32_t x;std::memcpy(&x,v.data()+k*4,4);return int64_t(x);};auto u32=[](const std::vector<uint8_t> &v,size_t k){uint32_t x;std::memcpy(&x,v.data()+k*4,4);return int64_t(x);};
  auto f32=[](const std::vector<uint8_t> &v,size_t k,size_t c=0){float x;std::memcpy(&x,v.data()+k*4+c*4,4);return x;};auto f3=[](const std::vector<uint8_t> &v,size_t k,size_t c){float x;std::memcpy(&x,v.data()+k*12+c*4,4);return x;};
  std::vector<int> intersect;uint32_t nee_count=0;
  for(size_t k=0;k<shade.size();k++){const int i=shade[k];const auto &r=rows[i];const auto &s=restored[i];const std::string w=" path="+std::to_string(i);
    exact(s.path.queued_kernel,r.after_shade,"Shade successor"+w);exact(s.isect.prim,r.restored_prim,"Restored self prim"+w);exact(s.isect.object,r.restored_object,"Restored self object"+w);
    if(r.mis_valid){float mis;std::memcpy(&mis,&records[k*8+1],4);numeric(mis,r.mis,m,"MIS weight"+w);m.mis_weights++;m.mis_below_one+=r.mis<1.f;}
    exact(u16(kernels,k),r.shadow_kernel,"Shadow kernel"+w);if(!r.shadow_kernel)continue;m.shadow_paths++;
    for(unsigned c=0;c<3;c++){numeric(f3(throughput,k,c),r.s_throughput[c],m,"Shadow throughput"+w);numeric(f3(P,k,c),r.s_P[c],m,"Shadow P"+w);numeric(f3(D,k,c),r.s_D[c],m,"Shadow D"+w);}
    float_exact(f32(tmin,k),r.s_tmin,"Shadow tmin"+w);numeric(f32(tmax,k),r.s_tmax,m,"Shadow tmax"+w);numeric(f32(time,k),r.s_time,m,"Shadow time"+w);numeric(f32(dP,k),r.s_dP,m,"Shadow dP"+w);numeric(f32(dD,k),r.s_dD,m,"Shadow dD"+w);
    exact(s32(lobj,k),r.s_light_object,"Shadow light object"+w);exact(s32(lprim,k),r.s_light_prim,"Shadow light prim"+w);exact(s32(self_object,k),r.s_self_object,"Shadow self object"+w);exact(s32(self_prim,k),r.s_self_prim,"Shadow self prim"+w);
    exact(u16(bounce,k),r.s_bounce,"Shadow bounce"+w);exact(u16(diffuse,k),r.s_diffuse,"Shadow diffuse bounce"+w);exact(u16(glossy,k),r.s_glossy,"Shadow glossy bounce"+w);exact(u16(transmission,k),r.s_transmission,"Shadow transmission bounce"+w);
    exact(u16(transparent,k),r.s_transparent,"Shadow transparent bounce"+w);exact(u16(bounds,k),r.s_volume_bounds,"Shadow volume bounds"+w);exact(group[k],r.s_group,"Shadow light group"+w);exact(u32(flag,k),r.s_flag,"Shadow flag"+w);exact(visibility[k],r.s_visibility,"Shadow visibility"+w);
    exact(u32(pixel,k),r.s_pixel,"Shadow pixel"+w);exact(u32(sample_id,k),r.s_sample,"Shadow sample"+w);exact(u32(rng_pixel,k),r.s_rng_pixel,"Shadow rng pixel"+w);exact(u16(rng_offset,k),r.s_rng_offset,"Shadow rng offset"+w);
    if(!average.empty())numeric(f32(average,k),r.s_average,m,"Shadow BSDF average"+w);
    if(r.shadow_kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW)intersect.push_back(int(k));
    else{nee_count++;
      // The light shader is evaluated at the intersection the pick left: the device's own picked hit bit for bit, the oracle's
      // integers exactly and its t, u, v by the practical criterion (as the pick itself).
      exact(s32(li_object,k),r.s_isect_object,"Light isect object"+w);exact(s32(li_prim,k),r.s_isect_prim,"Light isect prim"+w);exact(s32(li_type,k),r.s_isect_type,"Light isect type"+w);
      float_exact(f32(li_t,k),after[i].isect.t,"Light isect t"+w);float_exact(f32(li_u,k),after[i].isect.u,"Light isect u"+w);float_exact(f32(li_v,k),after[i].isect.v,"Light isect v"+w);
      numeric(f32(li_t,k),r.s_isect_t,m,"Light isect t"+w);numeric(f32(li_u,k),r.s_isect_u,m,"Light isect u"+w);numeric(f32(li_v,k),r.s_isect_v,m,"Light isect v"+w);}
  }
  m.nee+=nee_count;
  if(intersect.empty())return;
  // The blocker-set decision: integrator_intersect_shadow on the same rays.
  {device_vector<int> list(&d,"dedicated_shadow",MEM_READ_WRITE);list.alloc(intersect.size());std::copy(intersect.begin(),intersect.end(),list.data());list.copy_to_device();
   counters({{DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,uint32_t(intersect.size())}});
   int work=int(intersect.size());DeviceKernelArguments args;args.add(&list.device_pointer);args.add(&work);
   require(queue->enqueue(DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,work,args),"Intersect shadow: "+d.error_message());require(queue->synchronize(),"Intersect shadow: "+d.error_message());}
  const auto decided=read(sp.queued_kernel,2);const auto packed=read(sp.packed_num_hits,2);uint32_t clear=0;
  for(int k:intersect){const auto &r=rows[shade[k]];const std::string w=" path="+std::to_string(shade[k]);exact(u16(decided,k),r.shadow_after_intersect,"Shadow blocker decision"+w);
    if(r.shadow_after_intersect){clear++;m.clear++;exact(u16(packed,k),r.shadow_hits,"Shadow hit count"+w);m.transparent_hits+=r.shadow_hits>0;}else m.blocked++;}
  check_counters({{DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW,clear}},"Intersect shadow");
}
// The moving object's transform and inverse at many times, bit by bit against this host's CPU kernels: next-event rays near a shared
// edge decide their fold on these last bits. Reported here; the Linux reference driver requires no difference.
static void motion_transform_sweep(WebGPUDevice &d,Scene &scene,Metrics &m){
  constexpr int count=4096;std::ostringstream code;code<<d.module_wgsl(shadow_source());
  code<<"@compute @workgroup_size(64) fn motion_transform_probe(@builtin(global_invocation_id) id:vec3<u32>){if(id.x>=p(1u)){return;}let time=bitcast<f32>(load_word(p(2u)+id.x*4u));\n"
        " let tfm=motion_object(p(3u),time);let inverse=motion_inverse(tfm);let at=p(4u)+id.x*96u;\n"
        " for(var k=0u;k<4u;k++){store_word(at+k*4u,bitcast<u32>(tfm.x[k]));store_word(at+16u+k*4u,bitcast<u32>(tfm.y[k]));store_word(at+32u+k*4u,bitcast<u32>(tfm.z[k]));\n"
        "  store_word(at+48u+k*4u,bitcast<u32>(inverse.x[k]));store_word(at+64u+k*4u,bitcast<u32>(inverse.y[k]));store_word(at+80u+k*4u,bitcast<u32>(inverse.z[k]));}\n}\n";
  const std::string source=code.str();WGPUShaderSourceWGSL wgsl{};wgsl.chain.sType=WGPUSType_ShaderSourceWGSL;wgsl.code={source.data(),source.size()};WGPUShaderModuleDescriptor desc{};desc.nextInChain=&wgsl.chain;d.gpu.push_scope();
  cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> module(wgpuDeviceCreateShaderModule(d.gpu.get(),&desc));d.gpu.pop_scope();auto shader=d.gpu.pipeline(module,d.pipeline_layout,"motion_transform_probe");
  device_vector<float> times(&d,"motion_sweep_times",MEM_READ_WRITE);times.alloc(count);for(int i=0;i<count;i++)times[i]=float(i)/(count-1);times.copy_to_device();
  device_vector<float> out(&d,"motion_sweep_out",MEM_READ_WRITE);out.alloc(count*24);std::fill_n(out.data(),out.size(),0.f);out.copy_to_device();
  for(size_t object=0;object<scene.dscene.object_flag.size();object++){if(!(scene.dscene.object_flag[object]&SD_OBJECT_MOTION))continue;
    std::array<uint32_t,32> p{};p[1]=count;p[2]=times.device_pointer;p[3]=uint32_t(object);p[4]=out.device_pointer;p[6]=d.constants.at("data");p[14]=d.scene_tables();p[17]=d.error_word;
    d.dispatch(p,count,shader);out.copy_from_device();healthy(d);
    for(int i=0;i<count;i++){const auto cpu=original_motion_transform(scene.dscene,int(object),times[i]);
      for(int w=0;w<24;w++){m.motion_words++;m.motion_different+=std::memcmp(&cpu[w],&out[i*24+w],4)!=0;numeric(out[i*24+w],cpu[w],m,"Motion transform");}}}
  require(m.motion_words>0,"Missing moving objects");
}
// Next-event shadow rays towards shadow-linked emitters through intersect_shadow: the decision and hit count exact.
static void shadow_ray_gate(WebGPUDevice &d,Scene &scene,const std::vector<ShadowRow> &rows,Metrics &m){
  if(rows.empty())return;
  bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);
  const size_t n=rows.size();auto &sp=d.state.shadow_path;auto &sr=d.state.shadow_ray;require(d.remaining(uintptr_t(sp.queued_kernel))/2>=n,"Shadow rows need more shadow paths");
  auto put=[&](const void *pointer,auto value){std::vector<decltype(value(rows[0]))> column;column.reserve(n);for(auto &r:rows)column.push_back(value(r));d.upload(uint32_t(uintptr_t(pointer)),column.data(),column.size()*sizeof(column[0]));};
  put(sp.queued_kernel,[](const ShadowRow&){return uint16_t(DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW);});
  put(sr.P,[](const ShadowRow &r){return packed_float3(make_float3(r.P[0],r.P[1],r.P[2]));});put(sr.D,[](const ShadowRow &r){return packed_float3(make_float3(r.D[0],r.D[1],r.D[2]));});
  put(sr.tmin,[](const ShadowRow &r){return r.tmin;});put(sr.tmax,[](const ShadowRow &r){return r.tmax;});put(sr.time,[](const ShadowRow &r){return r.time;});
  put(sr.self_light_object,[](const ShadowRow &r){return r.light_object;});put(sr.self_light_prim,[](const ShadowRow &r){return r.light_prim;});
  put(d.state.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].object,[](const ShadowRow &r){return r.self_object;});put(d.state.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].prim,[](const ShadowRow &r){return r.self_prim;});
  put(sp.transparent_bounce,[](const ShadowRow &r){return uint16_t(r.transparent_bounce);});put(sp.flag,[](const ShadowRow &r){return r.flag;});put(sp.visibility,[](const ShadowRow &r){return uint8_t(r.visibility);});put(sp.packed_num_hits,[](const ShadowRow&){return uint16_t(0);});
  device_vector<int> selected(&d,"dedicated_nee_indices",MEM_READ_WRITE);selected.alloc(n);for(size_t i=0;i<n;i++)selected[i]=int(i);selected.copy_to_device();
  IntegratorQueueCounter c{};c.num_queued[DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW]=uint32_t(n);d.upload(uintptr_t(d.state.queue_counter),&c,sizeof(c));
  auto queue=d.gpu_queue_create();int work=int(n);DeviceKernelArguments args;args.add(&selected.device_pointer);args.add(&work);
  require(queue->enqueue(DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,work,args),"Next-event intersect shadow: "+d.error_message());require(queue->synchronize(),"Next-event intersect shadow: "+d.error_message());
  std::vector<uint16_t> decided(n),hits(n);d.download(uint32_t(uintptr_t(sp.queued_kernel)),decided.data(),n*2);d.download(uint32_t(uintptr_t(sp.packed_num_hits)),hits.data(),n*2);
  for(size_t i=0;i<n;i++){const auto &r=rows[i];const std::string w=" ray="+std::to_string(i)+" object="+std::to_string(r.light_object)+" self="+std::to_string(r.self_object)+"/"+std::to_string(r.self_prim)+" light="+std::to_string(r.light_prim)+" blocker="+std::to_string(r.blocker_object)+"/"+std::to_string(r.blocker_prim)+" t="+std::to_string(r.blocker_t)+" tmax="+std::to_string(r.tmax)+" time="+std::to_string(r.time)+" unlinkedBlocked="+std::to_string(r.unlinked_blocked)+" P="+std::to_string(r.P[0])+","+std::to_string(r.P[1])+","+std::to_string(r.P[2])+" D="+std::to_string(r.D[0])+","+std::to_string(r.D[1])+","+std::to_string(r.D[2]);m.exact++;require(decided[i]==r.after,"Next-event blocker decision"+w+" got="+std::to_string(decided[i])+" expected="+std::to_string(r.after));m.nee_rays++;
    if(r.after){m.nee_clear++;m.exact++;require(hits[i]==r.hits,"Next-event hit count"+w);m.linked_through+=r.unlinked_blocked!=0;}else m.nee_blocked++;}
}
// The guard: the combinations of shadow linking with the catcher, AO and volumes pass validate_shadow_linking before any dispatch.
static void guards(WebGPUDevice &d,Metrics &m){
  const auto data=d.data;
  // The combinations the gate's own cases render pass the guard (none is refused there any more; path guiding, outside the target, is
  // refused by enqueue_dedicated_light; subsurface scattering with shadow linking renders).
  auto accept=[&](){d.validate_shadow_linking();d.data=data;m.guards++;};
  d.validate_shadow_linking();
  d.data.kernel_features|=KERNEL_FEATURE_VOLUME;d.data.film.pass_volume_direct=0;d.data.film.pass_volume_indirect=3;accept();
  d.data.kernel_features|=KERNEL_FEATURE_VOLUME;d.data.integrator.use_light_tree=0;accept();
  d.data.kernel_features|=KERNEL_FEATURE_VOLUME;d.data.integrator.use_light_tree=1;accept();
  d.data.integrator.has_shadow_catcher=1;accept();
  d.data.kernel_features|=KERNEL_FEATURE_AO_ADDITIVE|KERNEL_FEATURE_AO_PASS;accept();
}
// One GPU render of the native scheduler (as the image loop of main), counting stages that queue the dedicated light kernels.
static std::vector<float> gpu_render(WebGPUDevice &device,Scene &scene,unsigned stride,const std::string &prefix,bool capture_it,Metrics &metrics,uint64_t &dedicated){
  bool cancelled=false;Scheduler scheduler(&device,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(device);
  auto buffers=scheduler.get_render_buffers();BufferParams bp;bp.width=width;bp.height=height;bp.pass_stride=int(stride);buffers->reset(bp);buffers->zero();
  std::unique_ptr<RenderCapture> capture;if(capture_it)capture=std::make_unique<RenderCapture>(device,prefix);
  for(unsigned sample=0;sample<samples;sample++){
    scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Shadow linking reset failed");KernelWorkTile tile{};tile.w=width;tile.h=height;tile.stride=width;tile.num_samples=1;tile.start_sample=sample;
    scheduler.enqueue_work_tiles(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA,&tile,1,0,0);require(scheduler.update_queue_counter_and_cache(),"Shadow linking camera failed");
    for(unsigned step=0;step<400;step++){const auto kernel=scheduler.get_most_queued_kernel();if(kernel==DEVICE_KERNEL_NUM)break;require(step<399,"GPU shadow linking paths did not terminate");
      dedicated+=kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_DEDICATED_LIGHT||kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_DEDICATED_LIGHT;
      require(scheduler.enqueue_path_iteration(),"Native shadow linking scheduler stopped with pending paths");
      require(scheduler.update_queue_counter_and_cache(),"Shadow linking stage failed: "+std::string(device_kernel_as_string(kernel))+": "+device.error_message());metrics.stages++;}
  }
  if(capture)capture->finish(prefix,buffers->buffer.device_pointer,buffers->buffer.memory_size(),uintptr_t(device.state.path.queued_kernel),scheduler.capacity(),width,height,samples,stride);
  buffers->buffer.copy_from_device();healthy(device);return std::vector<float>(buffers->buffer.data(),buffers->buffer.data()+buffers->buffer.size());
}
static void strip_shadow_linking(Scene &scene){
  for(Object *object:scene.objects){object->set_shadow_set_membership(LIGHT_LINK_MASK_ALL);object->set_blocker_shadow_set(0);object->tag_update(&scene);}
}
// Cases 21 and 22: the scene that runs after a shadow-linking scene on the same device, against the CPU kernels.
static void linking_off_case(WebGPUDevice &device,int c,const std::string &prefix,Metrics &metrics){
  SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=false;params.use_texture_cache=false;gate_curve_params(params);
  uint64_t warm_dedicated=0,dedicated=0;
  auto first=std::make_unique<Scene>(params,&device);fixture(*first,0);gate_curve_tufts(*first);{Progress progress;first->device_update(&device,progress);healthy(device);require(!progress.get_error(),"Shadow linking scene preparation failed");}
  require(first->dscene.data.kernel_features&KERNEL_FEATURE_SHADOW_LINKING,"Case 0's scene lacks shadow linking");
  gpu_render(device,*first,unsigned(first->dscene.data.film.pass_stride),prefix,false,metrics,warm_dedicated);require(warm_dedicated>0,"The shadow-linking render queued no dedicated light kernel");
  std::unique_ptr<Scene> second;Scene *scene=first.get();
  if(c==21){strip_shadow_linking(*first);}
  else{second=std::make_unique<Scene>(params,&device);fixture(*second,0);strip_shadow_linking(*second);gate_curve_tufts(*second);scene=second.get();}
  {Progress progress;scene->device_update(&device,progress);healthy(device);require(!progress.get_error(),"Scene preparation without shadow linking failed");}
  auto &data=scene->dscene.data;require(!(data.kernel_features&KERNEL_FEATURE_SHADOW_LINKING),"Shadow linking left on after the memberships were reset");
  const unsigned stride=unsigned(data.film.pass_stride);std::vector<float> reference(width*height*stride);std::vector<DedicatedRow> rows;std::vector<ShadowRow> shadow_rows;
  cpu_render(scene->dscene,Fixture{},c,reference,rows,shadow_rows,metrics);require(metrics.cpu_dedicated==0,"The CPU kernels scheduled the dedicated light kernels without shadow linking");
  RenderCapture::file(prefix+".cpu.film.bin",reference.data(),reference.size()*4);
  std::vector<float> first_film;
  for(unsigned repeat=0;repeat<2;repeat++){
    auto actual=gpu_render(device,*scene,stride,prefix,repeat==1,metrics,dedicated);RenderCapture::file(prefix+(repeat?".repeat.film.bin":".gpu.film.bin"),actual.data(),actual.size()*4);
    for(size_t i=0;i<actual.size();i++){double error=std::abs(double(actual[i])-reference[i]);metrics.film_maximum=std::max(metrics.film_maximum,error);metrics.film++;
      metrics.film_strict+=error>1e-7+1e-7*std::abs(double(reference[i]));
      require(std::isfinite(actual[i])&&std::isfinite(reference[i])&&error<=5e-5+2e-4*std::abs(double(reference[i])),"Shadow linking film mismatch case="+std::to_string(c)+" component="+std::to_string(i)+" actual="+std::to_string(actual[i])+" expected="+std::to_string(reference[i]));
      if(repeat)metrics.repeat+=std::memcmp(&actual[i],&first_film[i],4)!=0;}
    if(!repeat)first_film=std::move(actual);
  }
  require(dedicated==0,"A path reached the dedicated light kernels without shadow linking");
  metrics.guards=warm_dedicated;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  reference_policy::use_reference_oracle_arithmetic();
  require(argc>=2,"Usage: shadow_linking_gate OUTPUT_DIRECTORY [CASE...]");const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
  std::vector<int> cases;for(int i=2;i<argc;i++)cases.push_back(std::stoi(argv[i]));if(cases.empty())for(int c=0;c<shadow_linking_cases;c++)cases.push_back(c);
  auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");Stats stats;Profiler profiler;
  Metrics total;
  const bool exchange_mode=reference_policy::prepare_only()||reference_policy::reference_only();
  for(int c:cases){
    require(c>=0&&c<shadow_linking_cases,"Unknown shadow linking case");std::cerr<<"shadow linking case="<<c<<'\n';
    // One device per case: the device's specialization only grows (device.h enabled_features), and each case judges its own scene's modules.
    auto owner=Device::create(infos[0],stats,profiler,true);auto &device=require_webgpu_device(owner.get(),"shadow_linking_gate device creation");
    std::ofstream(out/"library.txt")<<device.gpu.library_path;std::ofstream(out/"adapter.txt")<<device.gpu.adapter_name;
    auto folder=out/("case-"+std::to_string(c));std::filesystem::create_directories(folder);const auto prefix=(folder/"run").string();Metrics metrics;const std::string key="case-"+std::to_string(c);
    if(c>=21){
      linking_off_case(device,c,prefix,metrics);
      std::ofstream details(folder/"result.json");
      for(std::ostream *stream:{static_cast<std::ostream*>(&details),static_cast<std::ostream*>(&std::cerr)})*stream<<std::setprecision(17)<<"{\"passed\":true,\"case\":"<<c<<",\"warmDedicatedStages\":"<<metrics.guards<<",\"stages\":"<<metrics.stages<<",\"filmChecks\":"<<metrics.film
        <<",\"filmStrictDifferences\":"<<metrics.film_strict<<",\"filmMaximumAbsoluteError\":"<<metrics.film_maximum<<",\"repeatBitwiseDifferences\":"<<metrics.repeat<<"}\n";
      total.film+=metrics.film;total.film_strict+=metrics.film_strict;total.film_maximum=std::max(total.film_maximum,metrics.film_maximum);total.repeat+=metrics.repeat;
      continue;
    }
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=false;params.use_texture_cache=false;gate_curve_params(params);
    Scene scene(params,&device);const Fixture f=fixture(scene,c);gate_curve_tufts(scene);Progress progress;scene.device_update(&device,progress);healthy(device);require(!progress.get_error(),"Shadow linking scene preparation failed");
    auto &data=scene.dscene.data;
    require(bool(data.integrator.use_light_tree)==f.tree&&data.integrator.use_direct_light&&(data.kernel_features&KERNEL_FEATURE_SHADOW_LINKING),"Unexpected native shadow linking configuration");
    require(bool(data.kernel_features&KERNEL_FEATURE_LIGHT_LINKING)==f.linking,"Unexpected native light linking configuration");
    require(bool(data.kernel_features&KERNEL_FEATURE_VOLUME)==f.volume&&bool(data.integrator.use_caustics)==f.caustics,"Unexpected native volume or caustics configuration");
    require(bool(data.integrator.has_shadow_catcher)==f.catcher&&bool(data.kernel_features&KERNEL_FEATURE_AO_ADDITIVE)==(c==14)&&bool(data.kernel_features&KERNEL_FEATURE_AO_PASS)==(c==15),"Unexpected native shadow catcher or AO configuration");
    if(f.catcher)require(data.film.pass_shadow_catcher!=PASS_UNUSED&&data.film.pass_shadow_catcher_matte!=PASS_UNUSED&&data.film.pass_shadow_catcher_sample_count!=PASS_UNUSED,"Missing shadow catcher passes");
    require((data.film.pass_volume_direct!=PASS_UNUSED&&data.film.pass_volume_indirect!=PASS_UNUSED)==f.passes&&(!f.passes||(data.kernel_features&KERNEL_FEATURE_LIGHT_PASSES)),"Unexpected native light pass configuration");
    if(f.emission)require(data.film.pass_emission!=PASS_UNUSED&&data.film.pass_lightgroup!=PASS_UNUSED,"Missing emission or light group pass: emission="+std::to_string(data.film.pass_emission)+" lightgroup="+std::to_string(data.film.pass_lightgroup));
    if(f.motion)require(data.bvh.have_motion,"Moving fixture lacks motion");
    if(f.transparent)require(data.integrator.transparent_shadows,"Transparent fixture lacks transparent shadows");
    const bool exchange_only=reference_policy::exchange_scene(scene.dscene,key,{});
    const unsigned stride=unsigned(data.film.pass_stride);std::vector<float> local(width*height*stride);std::vector<DedicatedRow> local_rows;std::vector<ShadowRow> local_shadow;cpu_render(scene.dscene,f,c,local,local_rows,local_shadow,metrics);
    std::vector<float> flat_shadow(local_shadow.size()*sizeof(ShadowRow)/4);if(!flat_shadow.empty())std::memcpy(flat_shadow.data(),local_shadow.data(),flat_shadow.size()*4);reference_policy::record_local(key+".shadow",flat_shadow);
    std::vector<float> flat_rows(local_rows.size()*sizeof(DedicatedRow)/4);if(!flat_rows.empty())std::memcpy(flat_rows.data(),local_rows.data(),flat_rows.size()*4);
    reference_policy::record_local(key+".rows",flat_rows);reference_policy::record_local(key+".film",local);RenderCapture::file(prefix+".cpu.film.bin",local.data(),local.size()*4);
    const auto diagnostic=(out/"reference-diagnostic.jsonl").string();const auto resolved_rows=reference_policy::resolve(key+".rows",flat_rows,diagnostic);const auto reference=reference_policy::resolve(key+".film",local,diagnostic);
    // Resolved before an exchange pass stops, so the Linux reference pass exports the shadow rows the device run imports.
    const auto resolved_shadow=reference_policy::resolve(key+".shadow",flat_shadow,diagnostic);
    if(exchange_only)continue;
    std::vector<DedicatedRow> rows(resolved_rows.size()*4/sizeof(DedicatedRow));if(!rows.empty())std::memcpy(rows.data(),resolved_rows.data(),rows.size()*sizeof(DedicatedRow));
    if(c==0)guards(device,metrics);
    if(c==12)motion_transform_sweep(device,scene,metrics);
    state_gate(device,scene,rows,metrics);
    {const auto &resolved=resolved_shadow;std::vector<ShadowRow> shadow_rows(resolved.size()*4/sizeof(ShadowRow));if(!shadow_rows.empty())std::memcpy(shadow_rows.data(),resolved.data(),shadow_rows.size()*sizeof(ShadowRow));
     shadow_ray_gate(device,scene,shadow_rows,metrics);}
    require(f.volume||f.catcher||c==15||stride==4,"Shadow linking combined-only film expected");bool cancelled=false;Scheduler scheduler(&device,scene.film,&scene.dscene,&cancelled);
    scheduler.alloc_work_memory();scheduler.init_execution();healthy(device);if(f.catcher)require(scheduler.capacity()>=int(2*width*height),"Shadow catcher split needs two paths per pixel");auto buffers=scheduler.get_render_buffers();BufferParams bp;bp.width=width;bp.height=height;bp.pass_stride=int(stride);buffers->reset(bp);std::vector<float> first;
    for(unsigned repeat=0;repeat<2;repeat++){
      // A shadow catcher scene: free split slots after the camera paths (shadow_catcher_gate.cpp; PathTraceWorkGPU::enqueue_work_tiles sets
      // next_main_path_index to the paths in flight and predicts a split per camera path).
      buffers->zero();if(f.catcher)upload(device,fields(device),std::vector<IntegratorStateCPU>(scheduler.capacity()));
      std::unique_ptr<RenderCapture> capture;if(repeat==1)capture=std::make_unique<RenderCapture>(device,prefix);
      for(unsigned sample=0;sample<samples;sample++){
        scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Shadow linking reset failed");KernelWorkTile tile{};tile.w=width;tile.h=height;tile.stride=width;tile.num_samples=1;tile.start_sample=sample;
        const uint32_t pixels=width*height;if(f.catcher)device.upload(uint32_t(uintptr_t(device.state.next_main_path_index)),&pixels,4);
        scheduler.enqueue_work_tiles(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA,&tile,1,0,f.catcher?int(pixels):0);require(scheduler.update_queue_counter_and_cache(),"Shadow linking camera failed");
        for(unsigned step=0;step<400;step++){const auto kernel=scheduler.get_most_queued_kernel();if(kernel==DEVICE_KERNEL_NUM)break;require(step<399,"GPU shadow linking paths did not terminate");
          require(scheduler.enqueue_path_iteration(),"Native shadow linking scheduler stopped with pending paths");require(scheduler.update_queue_counter_and_cache(),"Shadow linking stage failed: "+std::string(device_kernel_as_string(kernel))+": "+device.error_message());metrics.stages++;}
      }
      if(capture)capture->finish(prefix,buffers->buffer.device_pointer,buffers->buffer.memory_size(),uintptr_t(device.state.path.queued_kernel),scheduler.capacity(),width,height,samples,stride);
      buffers->buffer.copy_from_device();healthy(device);std::vector<float> actual(buffers->buffer.data(),buffers->buffer.data()+buffers->buffer.size());
      RenderCapture::file(prefix+(repeat?".repeat.film.bin":".gpu.film.bin"),actual.data(),actual.size()*4);
      for(size_t i=0;i<actual.size();i++){double error=std::abs(double(actual[i])-reference[i]);metrics.film_maximum=std::max(metrics.film_maximum,error);metrics.film++;
        metrics.film_strict+=error>1e-7+1e-7*std::abs(double(reference[i]));
        require(std::isfinite(actual[i])&&std::isfinite(reference[i])&&error<=5e-5+2e-4*std::abs(double(reference[i])),"Shadow linking film mismatch case="+std::to_string(c)+" component="+std::to_string(i)+" actual="+std::to_string(actual[i])+" expected="+std::to_string(reference[i]));
        if(repeat)metrics.repeat+=std::memcmp(&actual[i],&first[i],4)!=0;}
      if(!repeat)first=std::move(actual);
    }
    // Coverage: the gate has seen what it claims to judge.
    require(metrics.rows>=200&&metrics.lamps>=20&&metrics.shadow_paths>=20&&metrics.blocked+metrics.clear>=20,"Missing shadow linking state coverage");
    require(metrics.clear>=10,"Missing clear dedicated shadow rays");
    require(metrics.linked_through>=20&&metrics.nee_blocked>=20,"Missing blocker-set decisions");
    if(c!=7)require(metrics.triangles>=5,"Missing dedicated emissive triangles");
    if(f.catcher)require(metrics.splits>100&&metrics.catcher_rows>=20,"Missing shadow catcher splits in the dedicated light kernels");
    if(f.ao)require(metrics.ao>=100&&metrics.ao_visible>=20,"Missing ambient occlusion shadow rays");
    if(c>=16)require(metrics.volume_rows>=50,"Missing dedicated rays from phase scatters");
    if(f.shapes)require(metrics.volume_shapes>=5,"Missing dedicated spot or area lamps from phase scatters");
    if(f.triangles)require(metrics.volume_triangles>=5,"Missing dedicated emissive triangles from phase scatters");
    if(f.caustics)require(metrics.mnee_sampled>=5,"Missing shadow caustics");
    if(c==4)require(metrics.nee>=5&&metrics.transparent_hits>=5,"Missing transparent or non-constant dedicated coverage");
    auto report=[&](std::ostream &stream){stream<<std::setprecision(17)<<"{\"passed\":true,\"case\":"<<c<<",\"tree\":"<<(f.tree?"true":"false")<<",\"linking\":"<<(f.linking?"true":"false")
      <<",\"rows\":"<<metrics.rows<<",\"noLight\":"<<metrics.none<<",\"pickedLamps\":"<<metrics.lamps<<",\"pickedTriangles\":"<<metrics.triangles<<",\"severalHits\":"<<metrics.several
      <<",\"misWeights\":"<<metrics.mis_weights<<",\"misBelowOne\":"<<metrics.mis_below_one<<",\"shadowPaths\":"<<metrics.shadow_paths<<",\"blocked\":"<<metrics.blocked<<",\"clear\":"<<metrics.clear<<",\"lightNee\":"<<metrics.nee<<",\"transparentHits\":"<<metrics.transparent_hits<<",\"neeRays\":"<<metrics.nee_rays<<",\"neeClear\":"<<metrics.nee_clear<<",\"neeBlocked\":"<<metrics.nee_blocked<<",\"linkedThrough\":"<<metrics.linked_through
      <<",\"mneeSampled\":"<<metrics.mnee_sampled<<",\"catcherSplits\":"<<metrics.splits<<",\"catcherRows\":"<<metrics.catcher_rows<<",\"aoPaths\":"<<metrics.ao<<",\"aoVisible\":"<<metrics.ao_visible
      <<",\"volumeRows\":"<<metrics.volume_rows<<",\"volumeShapes\":"<<metrics.volume_shapes<<",\"volumeTriangles\":"<<metrics.volume_triangles<<",\"cpuDedicatedPaths\":"<<metrics.cpu_dedicated<<",\"cpuShadedPaths\":"<<metrics.cpu_shaded<<",\"exactChecks\":"<<metrics.exact<<",\"numericChecks\":"<<metrics.numeric<<",\"strictDifferences\":"<<metrics.strict<<",\"maximumAbsoluteError\":"<<metrics.maximum
      <<",\"guardCases\":"<<metrics.guards<<",\"motionTransformWords\":"<<metrics.motion_words<<",\"motionTransformDifferences\":"<<metrics.motion_different<<",\"stages\":"<<metrics.stages<<",\"filmChecks\":"<<metrics.film<<",\"filmStrictDifferences\":"<<metrics.film_strict<<",\"filmMaximumAbsoluteError\":"<<metrics.film_maximum<<",\"repeatBitwiseDifferences\":"<<metrics.repeat<<"}\n";};
    std::ofstream details(folder/"result.json");report(details);report(std::cerr);
    total.rows+=metrics.rows;total.lamps+=metrics.lamps;total.triangles+=metrics.triangles;total.several+=metrics.several;total.mis_weights+=metrics.mis_weights;total.shadow_paths+=metrics.shadow_paths;total.blocked+=metrics.blocked;total.clear+=metrics.clear;total.nee_rays+=metrics.nee_rays;total.nee_clear+=metrics.nee_clear;total.nee_blocked+=metrics.nee_blocked;total.linked_through+=metrics.linked_through;
    total.exact+=metrics.exact;total.numeric+=metrics.numeric;total.strict+=metrics.strict;total.maximum=std::max(total.maximum,metrics.maximum);total.guards+=metrics.guards;total.film+=metrics.film;total.film_strict+=metrics.film_strict;
    total.film_maximum=std::max(total.film_maximum,metrics.film_maximum);total.repeat+=metrics.repeat;total.motion_words+=metrics.motion_words;total.motion_different+=metrics.motion_different;
  }
  require(stats.mem_used==0,"Shadow linking gate leaked native allocations");
  if(exchange_mode)std::cout<<"{\"passed\":true,\"sceneExchangeOnly\":\""<<(reference_policy::prepare_only()?"dump":"reference")<<"\",\"cases\":"<<cases.size()<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
  else std::cout<<std::setprecision(17)<<"{\"passed\":true,\"cases\":"<<cases.size()<<",\"rows\":"<<total.rows<<",\"pickedLamps\":"<<total.lamps<<",\"pickedTriangles\":"<<total.triangles<<",\"severalHits\":"<<total.several<<",\"misWeights\":"<<total.mis_weights
    <<",\"shadowPaths\":"<<total.shadow_paths<<",\"blocked\":"<<total.blocked<<",\"clear\":"<<total.clear<<",\"neeRays\":"<<total.nee_rays<<",\"neeBlocked\":"<<total.nee_blocked<<",\"linkedThrough\":"<<total.linked_through<<",\"guardCases\":"<<total.guards<<",\"motionTransformWords\":"<<total.motion_words<<",\"motionTransformDifferences\":"<<total.motion_different<<",\"exactChecks\":"<<total.exact<<",\"numericChecks\":"<<total.numeric<<",\"strictDifferences\":"<<total.strict
    <<",\"maximumAbsoluteError\":"<<total.maximum<<",\"filmChecks\":"<<total.film<<",\"filmStrictDifferences\":"<<total.film_strict<<",\"filmMaximumAbsoluteError\":"<<total.film_maximum
    <<",\"repeatBitwiseDifferences\":"<<total.repeat<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<",\"pipelineReport\":"<<ccl::pipeline_process_report_json()<<"}\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
