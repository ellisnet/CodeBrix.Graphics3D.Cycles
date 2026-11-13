// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Transparent shadows through curves against the unmodified CPU kernels (curve_fixture.h): the baked transparency words
// (the same scene through Hair::update_shadow_transparency on the CPU device and on the WebGPU device, bit for bit: the kernel
// runs in the middle of the device update, when the object tables are not yet final)
// and integrator_intersect_shadow with shadow_all: queue, recorded hit count, baked throughput and the recorded hits exact,
// on every shape, with transparent triangles beside the curves, hit limits, the opacity cutoff and motion.
#include "wavefront_test.h"
#include "shadow_reference.h"
#include "curve_fixture.h"
#include "wavefront.h"
#include "pipelines.h"
#include "scene/camera.h"
#include "scene/integrator.h"
#include "util/progress.h"
#include "util/task.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace ccl;
using namespace wavefront_test;
struct ShadowScheduler:Scheduler {
  using Scheduler::Scheduler;
  void active(int n){integrator_next_shadow_path_index_[0]=n;integrator_next_shadow_path_index_.copy_to_device();}
};
struct ShadowField {std::string name;uint32_t address;size_t stride,offset;};
static std::vector<ShadowField> shadow_fields(WebGPUDevice &d){std::vector<ShadowField> out;
#define KERNEL_STRUCT_BEGIN(name) for(int index=0;;index++){
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature) KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) if(d.state.parent.name)out.push_back({#parent "." #name,uint32_t(uintptr_t(d.state.parent.name)),sizeof(gpu_state_storage<type>::gpu_type),offsetof(IntegratorShadowStateCPU,parent.name)});
#define KERNEL_STRUCT_MEMBER_PACKED KERNEL_STRUCT_MEMBER
#define KERNEL_STRUCT_ARRAY_MEMBER(parent,type,name,feature) if(d.state.parent[index].name)out.push_back({#parent "." #name+std::to_string(index),uint32_t(uintptr_t(d.state.parent[index].name)),sizeof(gpu_state_storage<type>::gpu_type),offsetof(IntegratorShadowStateCPU,parent)+size_t(index)*sizeof(IntegratorShadowStateCPU::parent[0])+offsetof(std::remove_reference_t<decltype(IntegratorShadowStateCPU::parent[0])>,name)});
#define KERNEL_STRUCT_END(name) (void)index;break;}
#define KERNEL_STRUCT_END_ARRAY(name,cpu_size,gpu_size) if(index>=gpu_size-1)break;}
#define KERNEL_STRUCT_VOLUME_STACK_SIZE d.data.volume_stack_size
#include "kernel/integrator/shadow_state_template.h"
#undef KERNEL_STRUCT_BEGIN
#undef KERNEL_STRUCT_BEGIN_PACKED
#undef KERNEL_STRUCT_MEMBER
#undef KERNEL_STRUCT_MEMBER_PACKED
#undef KERNEL_STRUCT_ARRAY_MEMBER
#undef KERNEL_STRUCT_END
#undef KERNEL_STRUCT_END_ARRAY
#undef KERNEL_STRUCT_VOLUME_STACK_SIZE
  return out;
}
enum Material {MATERIAL_CHECKER,MATERIAL_CONSTANT,MATERIAL_LIGHT_PATH,MATERIAL_DENSE,MATERIAL_OPAQUE};
// Compiled by the original SVM compiler for both oracles. Checker: a mix with diffuse, the colour varying along the
// curves. Constant: pure transparent. Light path: the node's depths through the bake mask. Dense: below the opacity cutoff
// after two hits. Opaque: no transparency, the attribute removed.
static Shader *curve_material(Scene &scene,Material kind){
  auto g=make_unique<ShaderGraph>();auto transparent=g->create_node<TransparentBsdfNode>();
  ShaderOutput *surface=transparent->output("BSDF");
  transparent->set_color(kind==MATERIAL_DENSE?make_float3(.03f,.01f,.02f):make_float3(.62f,.35f,.8f));
  if(kind==MATERIAL_CHECKER){auto geometry=g->create_node<GeometryNode>();auto checker=g->create_node<CheckerTextureNode>();checker->set_scale(2.3f);
    checker->set_color1(make_float3(.8f,.3f,.6f));checker->set_color2(make_float3(.2f,.9f,.4f));
    g->connect(geometry->output("Position"),checker->input("Vector"));g->connect(checker->output("Color"),transparent->input("Color"));
    auto diffuse=g->create_node<DiffuseBsdfNode>();auto mix=g->create_node<MixClosureNode>();mix->set_fac(.65f);
    g->connect(diffuse->output("BSDF"),mix->input("Closure1"));g->connect(surface,mix->input("Closure2"));surface=mix->output("Closure");}
  if(kind==MATERIAL_LIGHT_PATH){auto path=g->create_node<LightPathNode>();auto combine=g->create_node<CombineColorNode>();
    auto depth=g->create_node<MathNode>();depth->set_math_type(NODE_MATH_MULTIPLY);depth->set_value2(.3f);g->connect(path->output("Ray Depth"),depth->input("Value1"));
    auto other=g->create_node<MathNode>();other->set_math_type(NODE_MATH_ADD);other->set_value2(.25f);g->connect(path->output("Transparent Depth"),other->input("Value1"));
    g->connect(depth->output("Value"),combine->input("Red"));g->connect(other->output("Value"),combine->input("Green"));g->connect(path->output("Is Shadow Ray"),combine->input("Blue"));
    g->connect(combine->output("Color"),transparent->input("Color"));}
  if(kind==MATERIAL_OPAQUE){auto diffuse=g->create_node<DiffuseBsdfNode>();surface=diffuse->output("BSDF");}
  g->connect(surface,g->output()->input("Surface"));auto shader=scene.create_node<Shader>();shader->set_graph(std::move(g));shader->tag_update(&scene);return shader;
}
struct Case {std::string name;std::vector<CurveShapeType> shapes;Material material;bool transparent_floor;int limit;CurveMotion motion;};
struct Row {std::string name;uint64_t words=0,word_bits=0,nonzero=0,attributes=0,paths=0,clear=0,blocked=0,attenuated=0,recorded=0,decision=0,hit_count=0,throughput_bits=0,hit_bits=0,practical=0;double maximum=0;};
static std::ofstream mismatch_log;
static void run_case(const DeviceInfo &info,const Case &c,bool unaligned,std::vector<Row> &rows){
  Row row;row.name=c.name;
  Stats stats;Profiler profiler;auto owner=Device::create(info,stats,profiler,true);auto *backend=dynamic_cast<WebGPUDevice*>(owner.get());require(backend,"Curve shadow gate device creation failed");auto &d=*backend;
  {
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=unaligned;params.use_texture_cache=false;
    auto build=[&](Scene &scene){
      scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);
      scene.integrator->set_aa_samples(64);scene.integrator->set_seed(17);scene.film->set_use_sample_count(false);scene.integrator->set_transparent_max_bounce(c.limit);
      scene.camera->set_full_width(64);scene.camera->set_full_height(48);scene.camera->set_screen_size(64,48);scene.camera->compute_auto_viewplane();
      Shader *shader=curve_material(scene,c.material);Shader *floor=c.transparent_floor?curve_material(scene,MATERIAL_CONSTANT):scene.default_surface;
      auto built=curve_fixture(scene,shader,c.shapes,floor);
      if(c.motion.steps||c.motion.object){curve_fixture_motion(built,scene,c.motion);scene.integrator->set_motion_blur(true);scene.camera->set_shuttertime(1.f);}
      return built;};
    Scene scene(params,&d);auto fixture=build(scene);
    Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Curve scene update failed");
    auto &data=scene.dscene.data;require(data.bvh.have_curves&&data.integrator.transparent_shadows,"Fixture lacks transparent shadows through curves");
    // The baked words: each hair's attribute on the CPU device.
    {
      Stats cpu_stats;Profiler cpu_profiler;auto cpu_infos=Device::available_devices(DEVICE_MASK_CPU);require(!cpu_infos.empty(),"No CPU device");
      auto cpu_device=Device::create(cpu_infos[0],cpu_stats,cpu_profiler,true);Scene cpu_scene(params,cpu_device.get());auto cpu_fixture=build(cpu_scene);
      Progress cpu_progress;cpu_scene.device_update(cpu_device.get(),cpu_progress);require(!cpu_progress.get_error()&&!cpu_device->have_error(),"CPU curve scene update failed");
      for(size_t g=0;g<fixture.geometry.size();g++){
        Attribute *baked=fixture.geometry[g].hair->attributes.find(ATTR_STD_SHADOW_TRANSPARENCY);Attribute *expected=cpu_fixture.geometry[g].hair->attributes.find(ATTR_STD_SHADOW_TRANSPARENCY);
        require(bool(baked)==bool(expected),"Baked transparency attribute presence differs");if(!baked)continue;row.attributes++;
        require(baked->size==expected->size,"Baked transparency size differs");const float *words=baked->data<float>();const float *cpu=expected->data<float>();
        for(size_t k=0;k<baked->size;k++){row.words++;uint32_t a,e;std::memcpy(&a,&words[k],4);std::memcpy(&e,&cpu[k],4);row.nonzero+=e!=0;
          if(a!=e){row.word_bits++;if(mismatch_log)mismatch_log<<"word "<<c.name<<" geometry="<<g<<" k="<<k<<std::hex<<" gpu="<<a<<" cpu="<<e<<std::dec<<"\n";}}
      }
    }
    bool cancelled=false;ShadowScheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);
    const auto layout=shadow_fields(d);const unsigned n=std::min(scheduler.capacity(),4096);
    for(unsigned round=0;round<3;round++){
      const auto rays=curve_rays(fixture,n,20261107u+round*7919u+(unaligned?1u:0u),c.motion);
      scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Curve shadow reset failed");
      std::vector<IntegratorShadowStateCPU> shadow(n);std::vector<int> selected;
      for(unsigned i=0;i<n;i++){auto &s=shadow[i];s.shadow_path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW;s.shadow_path.flag=0;s.shadow_path.render_pixel_index=i;
        s.shadow_path.throughput=make_float3(.9f,.7f,1.3f);s.shadow_path.transparent_bounce=i%3;s.shadow_path.packed_num_hits=333;
        s.shadow_ray.P=rays[i].P;s.shadow_ray.D=rays[i].D;s.shadow_ray.tmin=rays[i].tmin;s.shadow_ray.tmax=rays[i].tmax;s.shadow_ray.time=rays[i].time;
        s.shadow_ray.self_light_object=OBJECT_NONE;s.shadow_ray.self_light_prim=PRIM_NONE;
        for(auto &hit:s.shadow_isect){hit.object=OBJECT_NONE;hit.prim=PRIM_NONE;}
        selected.push_back(int(i));}
      for(auto &f:layout){std::vector<uint8_t> bytes(size_t(n)*f.stride);for(size_t i=0;i<n;i++)std::memcpy(bytes.data()+i*f.stride,reinterpret_cast<const uint8_t*>(&shadow[i])+f.offset,f.stride);d.upload(f.address,bytes.data(),bytes.size());}
      IntegratorQueueCounter counter{};counter.num_queued[DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW]=n;d.upload(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));
      scheduler.active(int(n));require(scheduler.update_queue_counter_and_cache(),"Curve shadow counters failed");
      std::vector<float> film(48);original_shadow(scene.dscene,DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,selected,shadow,film);
      scheduler.enqueue_path_iteration(DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW);require(scheduler.update_queue_counter_and_cache(),"Curve shadow failed: "+d.error_message());
      std::vector<uint16_t> queued(n),hits(n);std::vector<packed_float3> throughput(n);
      d.download(uintptr_t(d.state.shadow_path.queued_kernel),queued.data(),n*2);d.download(uintptr_t(d.state.shadow_path.packed_num_hits),hits.data(),n*2);d.download(uintptr_t(d.state.shadow_path.throughput),throughput.data(),n*sizeof(packed_float3));
      std::array<std::vector<float>,INTEGRATOR_SHADOW_ISECT_SIZE_GPU> ts,us,vs;std::array<std::vector<int>,INTEGRATOR_SHADOW_ISECT_SIZE_GPU> os,ps,ty;
      for(unsigned k=0;k<INTEGRATOR_SHADOW_ISECT_SIZE_GPU;k++){ts[k].resize(n);us[k].resize(n);vs[k].resize(n);os[k].resize(n);ps[k].resize(n);ty[k].resize(n);
        d.download(uintptr_t(d.state.shadow_isect[k].t),ts[k].data(),n*4);d.download(uintptr_t(d.state.shadow_isect[k].u),us[k].data(),n*4);d.download(uintptr_t(d.state.shadow_isect[k].v),vs[k].data(),n*4);
        d.download(uintptr_t(d.state.shadow_isect[k].object),os[k].data(),n*4);d.download(uintptr_t(d.state.shadow_isect[k].prim),ps[k].data(),n*4);d.download(uintptr_t(d.state.shadow_isect[k].type),ty[k].data(),n*4);}
      healthy(d);
      for(unsigned i=0;i<n;i++){const auto &e=shadow[i];row.paths++;const bool clear=e.shadow_path.queued_kernel!=0;row.clear+=clear;row.blocked+=!clear;
        if(queued[i]!=e.shadow_path.queued_kernel){row.decision++;if(mismatch_log)mismatch_log<<"queue "<<c.name<<" i="<<i<<" gpu="<<queued[i]<<" cpu="<<e.shadow_path.queued_kernel<<"\n";continue;}
        if(!clear)continue;
        if(hits[i]!=e.shadow_path.packed_num_hits){row.hit_count++;if(mismatch_log)mismatch_log<<"count "<<c.name<<" i="<<i<<" gpu="<<hits[i]<<" cpu="<<e.shadow_path.packed_num_hits<<"\n";continue;}
        row.recorded+=e.shadow_path.packed_num_hits;
        const float3 want=e.shadow_path.throughput;row.attenuated+=!(want.x==.9f&&want.y==.7f&&want.z==1.3f);
        for(int k=0;k<3;k++){const float av=(&throughput[i].x)[k],ev=want[k];const double error=std::abs(double(av)-ev);row.maximum=std::max(row.maximum,error);
          if(std::bit_cast<uint32_t>(av)!=std::bit_cast<uint32_t>(ev)){row.throughput_bits++;if(mismatch_log)mismatch_log<<"throughput "<<c.name<<" i="<<i<<" k="<<k<<std::setprecision(9)<<" gpu="<<av<<" cpu="<<ev<<"\n";}
          if(!(std::isfinite(av)&&error<=5e-5+2e-4*std::abs(double(ev))))row.practical++;}
        const unsigned recorded=std::min<unsigned>(e.shadow_path.packed_num_hits,INTEGRATOR_SHADOW_ISECT_SIZE_GPU);
        for(unsigned k=0;k<recorded;k++){const auto &w=e.shadow_isect[k];
          const bool same=std::bit_cast<uint32_t>(w.t)==std::bit_cast<uint32_t>(ts[k][i])&&std::bit_cast<uint32_t>(w.u)==std::bit_cast<uint32_t>(us[k][i])&&std::bit_cast<uint32_t>(w.v)==std::bit_cast<uint32_t>(vs[k][i])&&w.object==os[k][i]&&w.prim==ps[k][i]&&w.type==ty[k][i];
          if(!same){row.hit_bits++;if(mismatch_log)mismatch_log<<"hit "<<c.name<<" i="<<i<<" slot="<<k<<std::setprecision(9)<<" gpu t="<<ts[k][i]<<" u="<<us[k][i]<<" v="<<vs[k][i]<<" obj="<<os[k][i]<<" prim="<<ps[k][i]<<" type="<<ty[k][i]<<" cpu t="<<w.t<<" u="<<w.u<<" v="<<w.v<<" obj="<<w.object<<" prim="<<w.prim<<" type="<<w.type<<"\n";}}
      }
    }
  }
  require(stats.mem_used==0,"Curve shadow gate leaked Cycles allocations");
  rows.push_back(row);
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc>=2,"Usage: curve_shadow_gate OUTPUT_PREFIX [case ...]");const std::string output=argv[1];std::vector<std::string> only(argv+2,argv+argc);
  mismatch_log.open(output+".mismatches.txt");
  auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(!infos.empty(),"No WebGPU device");
  std::vector<Row> rows;const std::vector<CurveShapeType> all{CURVE_RIBBON,CURVE_THICK,CURVE_THICK_LINEAR};
  const std::vector<Case> cases={
    {"ribbon-checker",{CURVE_RIBBON},MATERIAL_CHECKER,false,12,{}},{"thick-checker",{CURVE_THICK},MATERIAL_CHECKER,false,12,{}},{"linear-checker",{CURVE_THICK_LINEAR},MATERIAL_CHECKER,false,12,{}},
    {"ribbon-constant",{CURVE_RIBBON},MATERIAL_CONSTANT,true,12,{}},{"thick-constant",{CURVE_THICK},MATERIAL_CONSTANT,true,12,{}},{"mixed-constant",all,MATERIAL_CONSTANT,true,12,{}},
    {"mixed-light-path",all,MATERIAL_LIGHT_PATH,false,12,{}},{"mixed-dense",all,MATERIAL_DENSE,false,12,{}},{"mixed-limit",all,MATERIAL_CONSTANT,true,2,{}},{"mixed-opaque",all,MATERIAL_OPAQUE,true,12,{}},
    {"deform-thick",{CURVE_THICK},MATERIAL_CHECKER,true,12,{5,true,false}},{"object-ribbon",{CURVE_RIBBON},MATERIAL_CONSTANT,true,12,{0,false,true}},{"deform-object-mixed",all,MATERIAL_CHECKER,true,12,{3,true,true,0b100110u}}};
  for(const auto &c:cases){if(!only.empty()&&std::find(only.begin(),only.end(),c.name)==only.end())continue;
    for(bool unaligned:{true,false}){std::cerr<<"[curve shadow] "<<c.name<<" unaligned="<<unaligned<<"\n";run_case(infos[0],c,unaligned,rows);rows.back().name=c.name+(unaligned?"":"-aligned");
      const auto &r=rows.back();std::cerr<<"  words="<<r.words<<" wordBits="<<r.word_bits<<" nonzero="<<r.nonzero<<" paths="<<r.paths<<" clear="<<r.clear<<" blocked="<<r.blocked<<" attenuated="<<r.attenuated<<" recorded="<<r.recorded<<" decision="<<r.decision<<" hitCount="<<r.hit_count<<" throughputBits="<<r.throughput_bits<<" hitBits="<<r.hit_bits<<" practical="<<r.practical<<" max="<<r.maximum<<"\n";}}
  bool passed=true;for(const auto &r:rows)passed=passed&&r.word_bits==0&&r.decision==0&&r.hit_count==0&&r.practical==0&&r.hit_bits==0&&r.throughput_bits==0;
  std::cout<<"{\"passed\":"<<(passed?"true":"false")<<",\"arithmetic\":\""<<wgsl_arithmetic()<<"\",\"rows\":[";bool first=true;
  for(const auto &r:rows){if(!first)std::cout<<",";first=false;
    std::cout<<std::setprecision(17)<<"{\"case\":\""<<r.name<<"\",\"attributes\":"<<r.attributes<<",\"words\":"<<r.words<<",\"nonzeroWords\":"<<r.nonzero<<",\"wordBits\":"<<r.word_bits<<",\"paths\":"<<r.paths<<",\"clear\":"<<r.clear<<",\"blocked\":"<<r.blocked<<",\"attenuated\":"<<r.attenuated<<",\"recorded\":"<<r.recorded<<",\"decisionMismatches\":"<<r.decision<<",\"hitCountMismatches\":"<<r.hit_count<<",\"throughputBits\":"<<r.throughput_bits<<",\"hitBits\":"<<r.hit_bits<<",\"practicalFailures\":"<<r.practical<<",\"maximum\":"<<r.maximum<<"}";}
  std::cout<<"]}\n";
  status=passed?0:1;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
