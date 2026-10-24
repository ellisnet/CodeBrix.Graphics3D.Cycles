// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Shadow catcher gate (native Cycles scene, native PathTraceWorkGPU scheduler):
//  - state cases (no direct light): every GPU stage of the first repeat is judged
//    against the unmodified Cycles CPU kernels on the same input states (split
//    decisions, path flags, visibility, sort keys, queue counters, the new path
//    index and every film float including the three shadow catcher passes), plus
//    the shadow catcher split-count kernel against the states; an independent CPU
//    image and a bitwise repeat;
//  - image cases (direct light, MIS, HDRI world, transparent shadows, the
//    approximate shadow catcher, a mesh light): the raw film (Combined and the
//    shadow catcher passes) against an independent CPU render of the same scene;
//  - a refusal case: a shadow catcher with a volume fails with the explicit error.
// The CPU split (state + 1 in the CPU kernel) is mapped onto the GPU path index the
// device assigns (ascending path order, wavefront_shadow_catcher_split.wgsl).
// Device-scene flow (reference_policy.h): images through the scene exchange, the
// state stages through a stage record (case-N/run.stages.bin) judged on Linux
// (CODEBRIX_CYCLES_STAGE_JUDGE=the device run's output directory).
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
#include "environment_fixture.h"
#include "transparent_shadow_fixture.h"
#include "scene/film.h"
#include "scene/integrator.h"
#include "scene/camera.h"
#include "scene/pass.h"
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
constexpr unsigned width=32,height=24,samples=8,pixels=width*height;
// 0-3 state cases, 4-9 image cases, 10 volume refusal.
constexpr int state_cases=4,image_cases=6,cases=state_cases+image_cases+1;
struct SplitScheduler:Scheduler {
  using Scheduler::Scheduler;
  using PathTraceWorkGPU::shadow_catcher_count_possible_splits;
  void set_active_with_splits(int n){max_active_main_path_index_=n;}
};
struct Metrics {uint64_t stages=0,integers=0,floats=0,strict=0,splits=0,background_splits=0,catcher_paths=0,count_checks=0,shadows=0,visible=0,nee=0,repeat_differences=0;double maximum=0;
  std::string maximum_field;};
static void number(float a,float e,const std::string &where,Metrics &m){
  const double error=std::abs(double(a)-e);if(error>m.maximum){m.maximum=error;m.maximum_field=where;}m.floats++;m.strict+=error>1e-7+1e-7*std::abs(double(e));
  require(std::isfinite(a)&&std::isfinite(e)&&error<=5e-5+2e-4*std::abs(double(e)),where+" actual="+std::to_string(a)+" expected="+std::to_string(e));
}
static Shader *graph_shader(Scene &scene,std::unique_ptr<ShaderGraph> graph){auto s=scene.create_node<Shader>();s->set_graph(std::move(graph));s->tag_update(&scene);return s;}
static Shader *diffuse_material(Scene &scene,float3 color,float glossy){
  auto g=make_unique<ShaderGraph>();auto d=g->create_node<DiffuseBsdfNode>();d->set_color(color);d->set_roughness(.25f);ShaderOutput *out=d->output("BSDF");
  if(glossy>0){auto gl=g->create_node<GlossyBsdfNode>();gl->set_distribution(CLOSURE_BSDF_MICROFACET_GGX_ID);gl->set_color(make_float3(.7,.75,.8));gl->set_roughness(.3f);
    auto mix=g->create_node<MixClosureNode>();mix->set_fac(glossy);g->connect(out,mix->input("Closure1"));g->connect(gl->output("BSDF"),mix->input("Closure2"));out=mix->output("Closure");}
  g->connect(out,g->output()->input("Surface"));return graph_shader(scene,std::move(g));
}
static Object *quad(Scene &scene,Shader *shader,const std::array<float3,4> &v){
  auto mesh=scene.create_node<Mesh>();array<Node*> slots;slots.push_back_slow(shader);mesh->set_used_shaders(slots);mesh->resize_mesh(4,2);
  std::copy(v.begin(),v.end(),mesh->get_position_for_write());const int indices[]={0,1,2,0,2,3};std::copy_n(indices,6,mesh->get_triangles().data());
  std::ranges::fill(mesh->get_smooth(),false);std::ranges::fill(mesh->get_shader(),0);mesh->tag_triangles_modified();auto object=scene.create_node<Object>();object->set_geometry(mesh);return object;
}
static Shader *emission_shader(Scene &scene,float strength){
  auto g=make_unique<ShaderGraph>();auto e=g->create_node<EmissionNode>();e->set_color(one_float3());e->set_strength(strength);
  g->connect(e->output("Emission"),g->output()->input("Surface"));return graph_shader(scene,std::move(g));
}
static Object *lamp(Scene &scene,Shader *emitter,Light *light,float3 strength,Transform tfm,bool catcher){
  light->set_strength(strength);light->set_use_mis(true);array<Node*> slots;slots.push_back_slow(emitter);light->set_used_shaders(slots);
  auto object=scene.create_node<Object>();object->set_geometry(light);object->set_tfm(tfm);object->set_is_shadow_catcher(catcher);return object;
}
static void fixture(Scene &scene,int mode){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);scene.integrator->set_use_denoise(false);
  scene.integrator->set_aa_samples(samples);scene.integrator->set_min_bounce(2);scene.integrator->set_max_bounce(3);scene.integrator->set_filter_glossy(0);
  scene.film->set_use_sample_count(false);scene.camera->set_full_width(width);scene.camera->set_full_height(height);scene.camera->set_screen_size(width,height);
  scene.camera->set_fov(.9f);scene.camera->set_matrix(transform_translate(make_float3(0,.5f,-7))*transform_rotate(.12f,make_float3(1,0,0)));scene.camera->compute_auto_viewplane();
  const bool approximate=mode==1||mode==8;
  scene.background->set_transparent(!approximate);scene.film->set_use_approximate_shadow_catcher(approximate);
  // The shadow catcher floor, a mixed diffuse/glossy box face and a second
  // object; the wall behind is a regular object in some cases.
  auto floor=quad(scene,diffuse_material(scene,make_float3(.6,.55,.5),mode==9?.5f:0.f),{make_float3(-6,-2,-3),make_float3(6,-2,-3),make_float3(6,-2,6),make_float3(-6,-2,6)});
  floor->set_is_shadow_catcher(true);
  auto object=diffuse_material(scene,make_float3(.2,.45,.75),.35f);
  quad(scene,object,{make_float3(-1.,-2,0),make_float3(.6f,-2,.4f),make_float3(.6f,.6f,.4f),make_float3(-1.,.6f,0)});
  quad(scene,diffuse_material(scene,make_float3(.8,.3,.15),0),{make_float3(1.2f,-1.2f,-1.f),make_float3(2.4f,-1.2f,-1.f),make_float3(2.4f,-1.2f,.6f),make_float3(1.2f,-1.2f,.6f)});
  if(mode==0||mode==4||mode==7)quad(scene,diffuse_material(scene,make_float3(.5,.5,.5),0),{make_float3(-6,-2,5),make_float3(6,-2,5),make_float3(6,4,5),make_float3(-6,4,5)});
  if(mode==2||mode==7){scene.integrator->set_transparent_max_bounce(8);auto pane=transparent_material(scene,mode==2?1:2);
    quad(scene,pane,{make_float3(-2.5f,-2,-1.6f),make_float3(-.4f,-2,-1.6f),make_float3(-.4f,1.f,-1.6f),make_float3(-2.5f,1.f,-1.6f)});}
  if(mode==3){auto holdout=quad(scene,diffuse_material(scene,make_float3(.4,.8,.4),0),{make_float3(-3.2f,-2,1),make_float3(-2.f,-2,1),make_float3(-2.f,-.4f,1),make_float3(-3.2f,-.4f,1)});holdout->set_use_holdout(true);}
  Shader *world=nullptr;
  if(mode==6){world=environment_world(scene,EnvironmentWorld::Hdri,0);environment_light(scene,world,true,64);}
  else if(mode==10){auto g=make_unique<ShaderGraph>();auto bg=g->create_node<BackgroundNode>();bg->set_color(make_float3(.3,.4,.5));g->connect(bg->output("Background"),g->output()->input("Surface"));
    auto absorption=g->create_node<AbsorptionVolumeNode>();absorption->set_color(make_float3(.8,.65,.4));absorption->set_density(.04f);g->connect(absorption->output("Volume"),g->output()->input("Volume"));
    world=graph_shader(scene,std::move(g));scene.background->set_shader(world);scene.integrator->set_max_volume_bounce(2);}
  else world=environment_world(scene,EnvironmentWorld::Constant);
  if(mode>=4&&mode!=6&&mode!=10){
    auto emitter=emission_shader(scene,1);
    auto point=scene.create_node<PointLight>();point->set_radius(.45f);point->set_is_sphere(true);
    lamp(scene,emitter,point,make_float3(140,100,70),transform_translate(make_float3(2.2f,2.5f,-2)),mode==5);
    if(mode==5){auto sun=scene.create_node<SunLight>();sun->set_angle(.22f);sun->set_normalize(false);
      lamp(scene,emitter,sun,make_float3(.3,.5,.7),transform_rotate(2.85f,make_float3(0,1,0))*transform_rotate(-.9f,make_float3(1,0,0)),false);}
  }
  if(mode==6){auto area=scene.create_node<AreaLight>();area->set_sizeu(1.4f);area->set_sizev(.9f);area->set_spread(M_PI_F);
    lamp(scene,emission_shader(scene,1),area,make_float3(90,80,60),transform_translate(make_float3(-2,3,-1.5f))*transform_rotate(2.4f,make_float3(1,0,0)),false);}
  for(auto shader:scene.shaders)shader->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
  if(mode==9){auto panel_shader=emission_shader(scene,4);panel_shader->set_emission_sampling_method(EMISSION_SAMPLING_FRONT_BACK);
    auto panel=quad(scene,panel_shader,{make_float3(-.8,-.5,0),make_float3(.8,-.5,0),make_float3(.8,.5,0),make_float3(-.8,.5,0)});
    panel->set_tfm(transform_translate(make_float3(1.6f,2.2f,-1.2f))*transform_rotate(1.1f,make_float3(1,0,0)));}
  (void)world;
}
// The CPU shadow catcher split (state + 1 in the CPU kernel) mapped onto the GPU path
// index: new indices in ascending order of the splitting paths, from `next`.
static uint32_t sort_key(DeviceScene &scene,const IntegratorStateCPU &s,size_t index,unsigned divisor){
  return (scene.tri_shader[s.isect.prim]&SHADER_MASK)+uint(scene.data.max_shaders)*uint32_t(index/divisor);}
// divisor 0: no GPU sort keys (the independent CPU image).
static void closest_split(DeviceScene &scene,std::vector<int> ids,std::vector<IntegratorStateCPU> &states,std::vector<float> &film,uint32_t &next,Metrics &m,unsigned divisor){
  std::sort(ids.begin(),ids.end());
  for(int i:ids){
    std::vector<IntegratorStateCPU> pair{states[i],states[i]};const uint32_t before=pair[0].path.flag;
    original_closest(scene,{0},pair,film);states[i]=pair[0];
    if(divisor&&states[i].path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE)states[i].path.shader_sort_key=sort_key(scene,states[i],i,divisor);
    if(!(before&PATH_RAY_SHADOW_CATCHER_HIT)&&(states[i].path.flag&PATH_RAY_SHADOW_CATCHER_HIT)){
      const uint32_t j=next++;require(j<states.size()&&!states[j].path.queued_kernel,"CPU shadow catcher split slot is not free");
      IntegratorStateCPU copy=states[i];const auto kernel=pair[1].path.queued_kernel;
      copy.path.flag|=PATH_RAY_SHADOW_CATCHER_PASS;if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND){copy.path.flag|=PATH_RAY_SHADOW_CATCHER_BACKGROUND;m.background_splits++;}
      require(copy.path.flag==pair[1].path.flag,"CPU shadow catcher split flags");copy.path.queued_kernel=kernel;
      if(divisor&&kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE)copy.path.shader_sort_key=sort_key(scene,copy,j,divisor);
      states[j]=copy;m.splits++;
    }
  }
}
static void cpu_render(DeviceScene &scene,std::vector<float> &film,Metrics &m){
  std::vector<IntegratorStateCPU> state(2*pixels);std::vector<IntegratorShadowStateCPU> shadow(2*pixels);
  for(unsigned sample=0;sample<samples;sample++){
    std::fill(state.begin(),state.end(),IntegratorStateCPU{});KernelWorkTile tile{};tile.w=width;tile.h=height;tile.stride=width;tile.num_samples=1;tile.start_sample=sample;
    original_camera(scene,{tile},0,state,film);uint32_t next=pixels;
    for(unsigned step=0;step<150;step++){
      std::map<DeviceKernel,std::vector<int>> queues;
      for(size_t i=0;i<state.size();i++)if(state[i].path.queued_kernel)queues[DeviceKernel(state[i].path.queued_kernel)].push_back(i);
      if(queues.empty())break;require(step<149,"CPU shadow catcher paths did not terminate");
      for(auto &[kernel,ids]:queues){
        if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST)closest_split(scene,ids,state,film,next,m,0);
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND)original_background(scene,ids,state,film);
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD)original_light_forward(scene,ids,state,film);
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE){
          for(int i:ids)m.catcher_paths+=(state[i].path.flag&PATH_RAY_SHADOW_CATCHER_PASS)!=0;
          original_surface(scene,ids,state,film);std::vector<int> nee,pending;
          for(int i:ids){if(!state[i].shadow.shadow_path.queued_kernel)continue;shadow[i]=state[i].shadow;
            if(shadow[i].shadow_path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_NEE)nee.push_back(i);else pending.push_back(i);}
          m.nee+=nee.size();original_light_nee(scene,nee,shadow,film);
          for(int i:nee)if(shadow[i].shadow_path.queued_kernel){require(shadow[i].shadow_path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,"Unexpected CPU light NEE successor");pending.push_back(i);}
          m.shadows+=pending.size();original_shadow(scene,DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,pending,shadow,film);
          std::vector<int> clear;for(int i:pending)if(shadow[i].shadow_path.queued_kernel)clear.push_back(i);
          m.visible+=clear.size();original_shadow(scene,DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW,clear,shadow,film);
          for(int i:ids)if(state[i].shadow.shadow_path.queued_kernel)state[i].shadow=shadow[i];
        }else throw std::runtime_error("Unexpected CPU shadow catcher kernel "+std::string(device_kernel_as_string(kernel)));
      }
    }
  }
}
// One state stage: the CPU kernel on the GPU's input states, then every field,
// film float, queue counter, the next path index and the split count.
static void stage_oracle(DeviceKernel kernel,DeviceScene &scene,std::vector<IntegratorStateCPU> &states,std::vector<float> &film,uint32_t &next,unsigned divisor,Metrics &m){
  std::vector<int> ids;for(size_t i=0;i<states.size();i++)if(states[i].path.queued_kernel==kernel)ids.push_back(int(i));
  if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST)closest_split(scene,ids,states,film,next,m,divisor);
  else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE){for(int i:ids)m.catcher_paths+=(states[i].path.flag&PATH_RAY_SHADOW_CATCHER_PASS)!=0;original_surface(scene,ids,states,film);}
  else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND){original_background(scene,ids,states,film);
    for(int i:ids)if(states[i].path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE)states[i].path.shader_sort_key=sort_key(scene,states[i],i,divisor);}
  else throw std::runtime_error("Unexpected state stage kernel "+std::string(device_kernel_as_string(kernel)));
}
static uint32_t possible_splits(const std::vector<IntegratorStateCPU> &states,size_t active){
  uint32_t n=0;for(size_t i=0;i<std::min(active,states.size());i++){auto &p=states[i].path;n+=p.queued_kernel&&!(p.flag&PATH_RAY_SHADOW_CATCHER_HIT)&&(p.flag&PATH_RAY_TRANSPARENT_BACKGROUND);}return n;
}
static void compare_stage(DeviceScene &scene,const std::vector<Field> &layout,const std::vector<IntegratorStateCPU> &expected,const std::vector<IntegratorStateCPU> &actual,
                          const float *film,const std::vector<float> &wanted,const IntegratorQueueCounter &counter,uint32_t next_actual,uint32_t next_wanted,uint32_t count_actual,size_t active,Metrics &m){
  for(auto &f:layout)for(size_t i=0;i<expected.size();i++){
    auto a=reinterpret_cast<const char*>(&actual[i])+f.offset;auto e=reinterpret_cast<const char*>(&expected[i])+f.offset;
    if(f.floating){for(size_t k=0;k<f.stride;k+=4){float av,ev;std::memcpy(&av,a+k,4);std::memcpy(&ev,e+k,4);number(av,ev,"stage "+std::to_string(m.stages)+" "+f.name+" path="+std::to_string(i),m);}}
    else{require(!std::memcmp(a,e,f.stride),"stage "+std::to_string(m.stages)+" integer "+f.name+" path="+std::to_string(i));m.integers++;}
  }
  for(size_t i=0;i<wanted.size();i++)number(film[i],wanted[i],"stage "+std::to_string(m.stages)+" film "+std::to_string(i)+" pixel="+std::to_string(i/scene.data.film.pass_stride)+" offset="+std::to_string(i%scene.data.film.pass_stride),m);
  IntegratorQueueCounter wanted_counter{};for(auto &s:expected)if(s.path.queued_kernel)wanted_counter.num_queued[s.path.queued_kernel]++;
  require(!std::memcmp(&counter,&wanted_counter,sizeof(counter)),"stage "+std::to_string(m.stages)+" queue counters differ");
  require(next_actual==next_wanted,"stage "+std::to_string(m.stages)+" next main path index "+std::to_string(next_actual)+" expected "+std::to_string(next_wanted));
  require(count_actual==possible_splits(expected,active),"stage "+std::to_string(m.stages)+" shadow catcher possible splits "+std::to_string(count_actual));
  m.integers+=3;m.count_checks++;m.stages++;
}
static void put_u32(std::ostream &out,uint32_t v){out.write(reinterpret_cast<const char*>(&v),4);}
// Stage record (device pass): per stage the kernel, the input rows that changed, the
// film floats that changed, the next index before; then the output rows/film delta,
// queue counter, next index and split count. Rows cover the active path range only.
struct StageRecord {
  std::ofstream out;const std::vector<Field> &layout;size_t row=0,active;std::vector<uint8_t> rows;std::vector<float> film;uint32_t count=0;
  StageRecord(const std::string &path,const std::vector<Field> &l,size_t active_,size_t film_floats):out(path,std::ios::binary),layout(l),active(active_){
    require(bool(out),"Could not write "+path);out<<"CBCSCSTG1";put_u32(out,uint32_t(layout.size()));
    for(auto &f:layout){put_u32(out,uint32_t(f.name.size()));out<<f.name;put_u32(out,uint32_t(f.stride));row+=f.stride;}
    put_u32(out,uint32_t(active));put_u32(out,uint32_t(film_floats));rows.assign(row*active,0);film.assign(film_floats,0.f);}
  void delta(const std::vector<IntegratorStateCPU> &states,const float *f){
    std::vector<uint32_t> changed;std::vector<uint8_t> next(row*active);
    for(size_t i=0;i<active;i++){size_t at=i*row;for(auto &x:layout){std::memcpy(next.data()+at,reinterpret_cast<const uint8_t*>(&states[i])+x.offset,x.stride);at+=x.stride;}
      if(std::memcmp(next.data()+i*row,rows.data()+i*row,row))changed.push_back(uint32_t(i));}
    put_u32(out,uint32_t(changed.size()));for(auto i:changed){put_u32(out,i);out.write(reinterpret_cast<const char*>(next.data()+i*row),std::streamsize(row));}
    rows.swap(next);std::vector<uint32_t> floats;for(size_t i=0;i<film.size();i++)if(std::memcmp(&f[i],&film[i],4)){floats.push_back(uint32_t(i));film[i]=f[i];}
    put_u32(out,uint32_t(floats.size()));for(auto i:floats){put_u32(out,i);out.write(reinterpret_cast<const char*>(&film[i]),4);}
  }
};
static void judge_stages(const std::string &path,DeviceScene &scene,const std::vector<Field> &layout,Metrics &m){
  const auto bytes=reference_policy::read_file(path);size_t at=0;
  auto u32=[&](){uint32_t v;require(at+4<=bytes.size(),"Truncated stage record");std::memcpy(&v,bytes.data()+at,4);at+=4;return v;};
  auto take=[&](void *t,size_t n){require(at+n<=bytes.size(),"Truncated stage record");std::memcpy(t,bytes.data()+at,n);at+=n;};
  require(!bytes.compare(0,9,"CBCSCSTG1"),"Not a shadow catcher stage record: "+path);at=9;std::vector<Field> order;size_t row=0;
  const uint32_t fields=u32();require(fields==layout.size(),"Stage record field count differs from this host");
  for(uint32_t i=0;i<fields;i++){const uint32_t n=u32();const std::string name=bytes.substr(at,n);at+=n;const uint32_t stride=u32();
    auto f=std::find_if(layout.begin(),layout.end(),[&](const Field &x){return x.name==name;});require(f!=layout.end()&&f->stride==stride,"Stage record field differs: "+name);order.push_back(*f);row+=stride;}
  const uint32_t active=u32(),film_floats=u32();require(film_floats==size_t(pixels)*scene.data.film.pass_stride,"Stage record film size differs");
  std::vector<uint8_t> rows(size_t(row)*active,0);std::vector<float> film(film_floats,0.f);
  auto delta=[&](){const uint32_t n=u32();for(uint32_t k=0;k<n;k++){const uint32_t i=u32();require(i<active,"Stage record row");take(rows.data()+size_t(i)*row,row);}
    const uint32_t f=u32();for(uint32_t k=0;k<f;k++){const uint32_t i=u32();require(i<film_floats,"Stage record film index");take(&film[i],4);}};
  auto unpack=[&](std::vector<IntegratorStateCPU> &states){for(size_t i=0;i<active;i++){size_t o=i*row;for(auto &f:order){std::memcpy(reinterpret_cast<uint8_t*>(&states[i])+f.offset,rows.data()+o,f.stride);o+=f.stride;}}};
  for(;;){const uint32_t tag=u32();if(tag==0)break;require(tag==1,"Stage record tag");
    const auto kernel=DeviceKernel(u32());const unsigned divisor=u32();uint32_t next=u32();delta();
    std::vector<IntegratorStateCPU> expected(active),actual(active);unpack(expected);std::vector<float> wanted=film;
    stage_oracle(kernel,scene,expected,wanted,next,divisor,m);
    delta();unpack(actual);IntegratorQueueCounter counter{};take(&counter,sizeof(counter));const uint32_t next_actual=u32(),count_actual=u32();
    compare_stage(scene,order,expected,actual,film.data(),wanted,counter,next_actual,next,count_actual,active,m);
  }
  require(m.stages>0&&at==bytes.size(),"Empty or malformed stage record");
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2,"Usage: shadow_catcher_gate OUTPUT_DIRECTORY");
  const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
  const char *judge=reference_policy::environment("CODEBRIX_CYCLES_STAGE_JUDGE");
  const bool record=reference_policy::environment("CODEBRIX_CYCLES_REFERENCE_IMPORT")&&reference_policy::environment("CODEBRIX_CYCLES_SCENE_VERIFY");
  reference_policy::use_reference_oracle_arithmetic();
  auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");Stats stats;Profiler profiler;
  auto owner=Device::create(infos[0],stats,profiler,true);auto &device=require_webgpu_device(owner.get(),"shadow_catcher_gate device creation");
  Metrics total;std::ostringstream reports;bool refused=false;std::string refusal;
  for(int mode=0;mode<cases;mode++){
    std::cerr<<"shadow catcher case="<<mode<<'\n';auto folder=out/("case-"+std::to_string(mode));std::filesystem::create_directories(folder);const auto prefix=(folder/"run").string();Metrics m;
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=false;params.use_texture_cache=false;
    Scene scene(params,&device);fixture(scene,mode);Progress progress;scene.device_update(&device,progress);healthy(device);require(!progress.get_error(),"Shadow catcher scene preparation failed");
    const auto &data=scene.dscene.data;const bool state_case=mode<state_cases;
    std::cerr<<"stride="<<data.film.pass_stride<<" catcher="<<data.film.pass_shadow_catcher<<" count="<<data.film.pass_shadow_catcher_sample_count<<" matte="<<data.film.pass_shadow_catcher_matte
             <<" background="<<data.film.pass_background<<" direct="<<data.integrator.use_direct_light<<" transparent shadows="<<data.integrator.transparent_shadows<<'\n';
    require(data.integrator.has_shadow_catcher&&(data.kernel_features&KERNEL_FEATURE_SHADOW_CATCHER)&&data.film.pass_shadow_catcher!=PASS_UNUSED&&
            data.film.pass_shadow_catcher_sample_count!=PASS_UNUSED&&data.film.pass_shadow_catcher_matte!=PASS_UNUSED,"Missing native shadow catcher configuration");
    require(bool(data.integrator.use_direct_light)==!state_case||mode==10,"Unexpected native direct-light configuration");
    require((data.film.pass_background!=PASS_UNUSED)==(mode==1||mode==8),"Unexpected native background pass");
    if(mode==7)require(data.integrator.transparent_shadows,"Missing native transparent shadows");
    if(mode==10&&(reference_policy::prepare_only()||reference_policy::reference_only()))continue;
    if(mode==10){
      require(data.kernel_features&KERNEL_FEATURE_VOLUME,"Missing native volume feature");
      bool cancelled=false;SplitScheduler scheduler(&device,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(device);
      auto buffers=scheduler.get_render_buffers();BufferParams bp;bp.width=width;bp.height=height;bp.pass_stride=data.film.pass_stride;buffers->reset(bp);buffers->zero();
      scheduler.enqueue_reset();scheduler.update_queue_counter_and_cache();KernelWorkTile tile{};tile.w=width;tile.h=height;tile.stride=width;tile.num_samples=1;
      scheduler.enqueue_work_tiles(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA,&tile,1,0,pixels);bool ok=scheduler.update_queue_counter_and_cache();
      for(unsigned step=0;ok&&step<20;step++){const auto kernel=scheduler.get_most_queued_kernel();if(kernel==DEVICE_KERNEL_NUM)break;scheduler.enqueue_path_iteration(kernel);ok=scheduler.update_queue_counter_and_cache();}
      refusal=device.error_message();refused=!ok&&refusal.find("WebGPU shadow catcher with volumes not implemented")!=std::string::npos;
      require(refused,"Shadow catcher with a volume was not refused explicitly: "+refusal);
      std::string reason;require(device.recover(reason),"Device did not recover after the refusal: "+reason);
      reports<<",{\"case\":10,\"refused\":true,\"message\":\""<<reference_policy::escape(refusal)<<"\"}";continue;
    }
    const std::string key="case-"+std::to_string(mode);
    // Scene exchange: a dump pass stops after the local CPU image; a reference pass
    // exports the glibc image from the loaded scene and, with the stage judge, judges
    // the device's stage record on the loaded scene.
    const bool exchange_only=reference_policy::exchange_scene(scene.dscene,key,{});
    if(exchange_only){std::vector<float> local(size_t(pixels)*data.film.pass_stride);Metrics cpu_metrics;cpu_render(scene.dscene,local,cpu_metrics);
      RenderCapture::file(prefix+".cpu.film.bin",local.data(),local.size()*4);reference_policy::record_local(key+".film",local);
      reference_policy::resolve(key+".film",local,(out/"reference-diagnostic.jsonl").string());
      if(judge&&state_case&&reference_policy::reference_only()){
        bool cancelled=false;SplitScheduler scheduler(&device,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(device);
        judge_stages((std::filesystem::path(judge)/key/"run.stages.bin").string(),scene.dscene,fields(device),m);
        reports<<",{\"case\":"<<mode<<",\"stageJudge\":true,\"stages\":"<<m.stages<<",\"splits\":"<<m.splits<<",\"backgroundSplits\":"<<m.background_splits<<",\"catcherSurfacePaths\":"<<m.catcher_paths
               <<",\"integerChecks\":"<<m.integers<<",\"floatChecks\":"<<m.floats<<",\"strictStageFailures\":"<<m.strict<<",\"maximumStageError\":"<<std::setprecision(17)<<m.maximum<<"}";
        require(m.splits>100,"Missing judged shadow catcher splits");
      }
      total.stages+=m.stages;total.integers+=m.integers;total.floats+=m.floats;total.strict+=m.strict;total.maximum=std::max(total.maximum,m.maximum);continue;}
    // A scene the known-adapter table refuses on this GPU class (not on a reference
    // device) is recorded with the refusal text instead of rendered.
    try{device.shader_features();}catch(const std::exception &error){
      const std::string text=error.what();require(text.find(" GPUs: ")!=std::string::npos&&text.rfind("WebGPU: ",0)==0,"Unexpected refusal: "+text);
      reports<<",{\"case\":"<<mode<<",\"refusedOnThisGpu\":true,\"message\":\""<<reference_policy::escape(text)<<"\"}";std::cerr<<"case "<<mode<<" refused: "<<text<<'\n';continue;}
    bool cancelled=false;SplitScheduler scheduler(&device,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(device);
    require(size_t(scheduler.capacity())>=2*pixels,"Path capacity below twice the camera paths");
    auto buffers=scheduler.get_render_buffers();BufferParams bp;bp.width=width;bp.height=height;bp.pass_stride=data.film.pass_stride;buffers->reset(bp);
    std::vector<float> local(size_t(pixels)*data.film.pass_stride),first;Metrics cpu;cpu_render(scene.dscene,local,cpu);reference_policy::record_local(key+".film",local);
    RenderCapture::file(prefix+".cpu.film.bin",local.data(),local.size()*4);
    const auto reference=reference_policy::resolve(key+".film",local,(out/"reference-diagnostic.jsonl").string());
    const auto layout=fields(device);std::unique_ptr<StageRecord> recorder;
    if(record&&state_case)recorder=std::make_unique<StageRecord>(prefix+".stages.bin",layout,2*pixels,local.size());
    for(unsigned repeat=0;repeat<2;repeat++){
      buffers->zero();upload(device,layout,std::vector<IntegratorStateCPU>(scheduler.capacity()));
      std::unique_ptr<RenderCapture> capture;if(repeat==1)capture=std::make_unique<RenderCapture>(device,prefix);
      for(unsigned sample=0;sample<samples;sample++){
        scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Reset failed");
        const uint32_t start=pixels;device.upload(uint32_t(uintptr_t(device.state.next_main_path_index)),&start,4);
        KernelWorkTile tile{};tile.w=width;tile.h=height;tile.stride=width;tile.num_samples=1;tile.start_sample=sample;
        scheduler.enqueue_work_tiles(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA,&tile,1,0,pixels);require(scheduler.update_queue_counter_and_cache(),"Camera failed: "+device.error_message());
        for(unsigned step=0;step<200;step++){
          const auto kernel=scheduler.get_most_queued_kernel();if(kernel==DEVICE_KERNEL_NUM)break;require(step<199,"GPU shadow catcher paths did not terminate");
          if(state_case&&repeat==0){
            auto expected=download(device,layout,2*pixels);buffers->buffer.copy_from_device();healthy(device);std::vector<float> wanted(buffers->buffer.data(),buffers->buffer.data()+buffers->buffer.size());
            uint32_t next=0;device.download(uint32_t(uintptr_t(device.state.next_main_path_index)),&next,4);healthy(device);
            const unsigned divisor=unsigned(device.state.sort_partition_divisor);
            if(recorder){put_u32(recorder->out,1);put_u32(recorder->out,uint32_t(kernel));put_u32(recorder->out,divisor);put_u32(recorder->out,next);recorder->delta(expected,wanted.data());}
            else stage_oracle(kernel,scene.dscene,expected,wanted,next,divisor,m);
            scheduler.enqueue_path_iteration(kernel);require(scheduler.update_queue_counter_and_cache(),"Stage failed: "+device.error_message());
            const uint32_t count=uint32_t(scheduler.shadow_catcher_count_possible_splits());
            auto actual=download(device,layout,2*pixels);buffers->buffer.copy_from_device();healthy(device);
            uint32_t next_actual=0;device.download(uint32_t(uintptr_t(device.state.next_main_path_index)),&next_actual,4);
            IntegratorQueueCounter counter{};device.download(uint32_t(uintptr_t(device.state.queue_counter)),&counter,sizeof(counter));healthy(device);
            if(recorder){recorder->delta(actual,buffers->buffer.data());recorder->out.write(reinterpret_cast<const char*>(&counter),sizeof(counter));put_u32(recorder->out,next_actual);put_u32(recorder->out,count);recorder->count++;m.stages++;}
            else compare_stage(scene.dscene,layout,expected,actual,buffers->buffer.data(),wanted,counter,next_actual,next,count,2*pixels,m);
          }else{scheduler.enqueue_path_iteration();require(scheduler.update_queue_counter_and_cache(),"Stage failed: "+device.error_message());}
        }
      }
      if(capture)capture->finish(prefix,buffers->buffer.device_pointer,buffers->buffer.memory_size(),uintptr_t(device.state.path.queued_kernel),scheduler.capacity(),width,height,samples,data.film.pass_stride);
      buffers->buffer.copy_from_device();healthy(device);std::vector<float> actual(buffers->buffer.data(),buffers->buffer.data()+buffers->buffer.size());
      RenderCapture::file(prefix+(repeat?".repeat.film.bin":".gpu.film.bin"),actual.data(),actual.size()*4);
      Metrics &image=repeat?total:m;(void)image;
      for(size_t i=0;i<actual.size();i++){
        if(!repeat)number(actual[i],reference[i],"case "+std::to_string(mode)+" film "+std::to_string(i)+" pixel="+std::to_string(i/data.film.pass_stride)+" offset="+std::to_string(i%data.film.pass_stride),cpu);
        else m.repeat_differences+=std::memcmp(&actual[i],&first[i],4)!=0;}
      if(!repeat)first=std::move(actual);
    }
    if(recorder){put_u32(recorder->out,0);recorder->out.close();require(!recorder->out.fail(),"Could not write the stage record");}
    require(cpu.splits>100&&cpu.catcher_paths>100,"Missing CPU shadow catcher split coverage");
    if(mode==1||mode==8)require(cpu.background_splits>100,"Missing shadow catcher background splits");
    if(state_case&&!recorder)require(m.splits>100&&m.count_checks>0,"Missing lockstep shadow catcher splits");
    if(!state_case)require(cpu.shadows>100&&cpu.visible>100,"Missing direct lighting coverage");
    if(mode==6)require(cpu.nee>0||cpu.shadows>100,"Missing world light coverage");
    reports<<",{\"case\":"<<mode<<",\"stateStages\":"<<m.stages<<",\"lockstep\":\""<<(state_case?(recorder?"recorded":"local"):"none")<<"\",\"splits\":"<<cpu.splits<<",\"backgroundSplits\":"<<cpu.background_splits
           <<",\"catcherSurfacePaths\":"<<cpu.catcher_paths<<",\"stateIntegerChecks\":"<<m.integers<<",\"stateFloatChecks\":"<<m.floats<<",\"strictStageFailures\":"<<m.strict<<",\"maximumStageError\":"<<std::setprecision(17)<<m.maximum
           <<",\"shadows\":"<<cpu.shadows<<",\"visible\":"<<cpu.visible<<",\"filmChecks\":"<<cpu.floats<<",\"strictImageDifferences\":"<<cpu.strict<<",\"maximumImageError\":"<<cpu.maximum
           <<",\"repeatBitwiseDifferences\":"<<m.repeat_differences<<",\"passStride\":"<<data.film.pass_stride<<"}";
    total.stages+=m.stages;total.integers+=m.integers;total.floats+=m.floats+cpu.floats;total.strict+=m.strict+cpu.strict;total.maximum=std::max({total.maximum,m.maximum,cpu.maximum});total.repeat_differences+=m.repeat_differences;
  }
  require(stats.mem_used==0,"Shadow catcher gate leaked native allocations");std::ofstream(out/"library.txt")<<device.gpu.library_path;std::ofstream(out/"adapter.txt")<<device.gpu.adapter_name;
  std::string cases_json=reports.str();if(!cases_json.empty())cases_json[0]='[';else cases_json="[";cases_json+="]";
  const bool exchange_only=reference_policy::prepare_only()||reference_policy::reference_only();
  std::cout<<std::setprecision(17)<<"{\"passed\":true,\"stageJudge\":"<<(judge?"true":"false")<<",\"nativeSceneAndScheduler\":true,\"independentOriginalCpuImage\":true,\"exchangeOnly\":"<<(exchange_only?"true":"false")<<(exchange_only?(reference_policy::prepare_only()?",\"sceneExchangeOnly\":\"dump\"":",\"sceneExchangeOnly\":\"reference\""):"")
           <<",\"stateStages\":"<<total.stages<<",\"integerChecks\":"<<total.integers<<",\"floatChecks\":"<<total.floats<<",\"strictDifferences\":"<<total.strict<<",\"maximumAbsoluteError\":"<<total.maximum
           <<",\"repeatBitwiseDifferences\":"<<total.repeat_differences<<",\"volumeRefused\":"<<(refused?"true":"false")<<",\"cases\":"<<cases_json
           <<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<",\"pipelineReport\":"<<ccl::pipeline_process_report_json()<<"}\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
