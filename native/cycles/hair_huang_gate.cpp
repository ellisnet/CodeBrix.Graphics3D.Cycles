// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Principled Hair, Huang model: closure setup, evaluation, sampling and pdf against the unmodified CPU kernels
// (hair_huang_reference.cpp), on curve hits (ribbons, thick and thick linear curves, camera and non-camera rays, so
// the near and far field) and triangle hits, followed by the whole surface stage on the same states against
// original_surface. The random number position (sd->lcg_state) after every evaluation and sample, labels, closure counts,
// runtime flags, path and shadow state integers and queue counters are exact; floats must pass the practical criterion;
// the strict criterion and the bit differences are reported.
#include "wavefront_test.h"
#include "closest_reference.h"
#include "surface_reference.h"
#include "hair_huang_reference.h"
#include "hair_huang_fixture.h"
#include "curve_fixture.h"
#include "surface_closure.h"
#include "wavefront.h"
#include "pipelines.h"
#include "scene/background.h"
#include "scene/camera.h"
#include "scene/film.h"
#include "scene/integrator.h"
#include "scene/light.h"
#include "util/progress.h"
#include "util/task.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
using namespace ccl;
using namespace wavefront_test;
struct ShadowScheduler:Scheduler {
  using Scheduler::Scheduler;
  int shadow_count(){integrator_next_shadow_path_index_.copy_from_device();return integrator_next_shadow_path_index_[0];}
  void shadow_reset(){integrator_next_shadow_path_index_[0]=0;integrator_next_shadow_path_index_.copy_to_device();}
};
struct Tally {uint64_t floats=0,bits=0,strict=0,practical=0,ints=0;double maximum=0;
  void add(float a,float e){floats++;const uint ab=std::bit_cast<uint>(a),eb=std::bit_cast<uint>(e);bits+=ab!=eb;if(ab==eb)return;const double error=std::abs(double(a)-e);
    strict+=error>1e-7+1e-7*std::abs(double(e));const bool ok=std::isfinite(a)&&std::isfinite(e)&&error<=5e-5+2e-4*std::abs(double(e));practical+=!ok;if(ok)maximum=std::max(maximum,error);}
};
struct Row {std::string material;std::string shapes;uint64_t paths=0,curve=0,triangle=0,near=0,far=0,transparent=0,two=0;Tally setup,eval,sample,stage,shadow;uint64_t lcg=0,labels=0,counts=0,integers=0,stage_integers=0,shadow_integers=0,shadow_paths=0,queue=0,zero_eval=0,nonzero_eval=0,zero_sample=0,lobes[5]={};};
static std::ofstream mismatch_log;
static std::string probe_source(WebGPUDevice &d){
  std::ostringstream c;c<<surface_source();
  const auto put=[&](const std::string &base,size_t offset,const std::string &expr,bool floating,unsigned count){
    const char *comp[]={".x",".y",".z"};for(unsigned k=0;k<count;k++)c<<" store_word("<<base<<"+"<<offset+k*4<<"u,"<<(floating?"bitcast<u32>(":"")<<expr<<(count>1?comp[k]:"")<<(floating?")":"")<<");\n";};
  // Closure setup: the CPU's fields.
  c<<"@compute @workgroup_size(64) fn huang_setup_probe(@builtin(global_invocation_id) id:vec3<u32>){\n"
     " svm_invocation(id.x);if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}\n"
     " let sd=surface_cached(id.x);if(sd.error!=0u){return;}let r=svm_evaluate(index,sd,data_word(D_max_closures),p(23u)+id.x*SVM_CLOSURE_CAPACITY*SVM_CLOSURE_BYTES);if(r.error!=0u){return;}\n"
     " let base=p(4u)+id.x*"<<sizeof(HuangGateRecord)<<"u;\n";
#define O(f) offsetof(HuangGateRecord,f)
  put("base",O(count),"r.count",false,1);put("base",O(remaining),"r.remaining",false,1);put("base",O(runtime_flag),"r.runtime_flag",false,1);put("base",O(lcg),"r.lcg_state",false,1);put("base",O(N),"r.N",true,3);put("base",O(transparency),"r.transparency.x",true,1);
  c<<" var q=r;q.count=SVM_CLOSURE_CAPACITY;for(var j=0u;j<"<<huang_gate_closures<<"u&&j<r.count;j++){let cl=svm_closure_read(q,j);let cb=base+"<<O(closures)<<"u+j*"<<sizeof(HuangGateClosure)<<"u;\n";
#define C(f) offsetof(HuangGateClosure,f)
  put("cb",C(weight),"cl.weight",true,3);put("cb",C(sample_weight),"cl.sample_weight",true,1);put("cb",C(N),"cl.N",true,3);put("cb",C(kind),"cl.kind",false,1);
  c<<" if(cl.kind==HH_CLOSURE_BSDF_HAIR_HUANG_ID){\n";
  put("cb",C(roughness),"cl.roughness",true,1);put("cb",C(tilt),"cl.a",true,1);put("cb",C(eta),"cl.ior",true,1);put("cb",C(aspect_ratio),"cl.b",true,1);put("cb",C(h),"cl.alpha_x",true,1);put("cb",C(sigma),"cl.multiscatter",true,3);put("cb",C(Y),"cl.T",true,3);put("cb",C(Z),"cl.reflection_tint",true,3);
  put("cb",C(radius),"cl.energy_scale",true,1);put("cb",C(pixel_coverage),"cl.alpha_y",true,1);put("cb",C(R),"cl.transmission_tint.x",true,1);put("cb",C(TT),"cl.transmission_tint.y",true,1);put("cb",C(TRT),"cl.transmission_tint.z",true,1);
  c<<" let wi=vec3<f32>(rounded_dot(sd.wi,cl.N),rounded_dot(sd.wi,cl.T),rounded_dot(sd.wi,cl.reflection_tint));let e2=rounded_add(1.0,-(cl.b*cl.b));\n";
  put("cb",C(wi),"wi",true,3);put("cb",C(e2),"e2",true,1);
  c<<" }}\n}\n";
#undef C
  // bsdf_eval through surf_evaluate; the record's r is rebuilt from the setup's output.
  const auto head=[&](const char *name){c<<"@compute @workgroup_size(1) fn "<<name<<"(@builtin(global_invocation_id) id:vec3<u32>){\n"
     " svm_invocation(id.x);if(id.x>=p(1u)||load_word(p(17u))!=0u){return;}\n"
     " var sd=surface_cached(id.x);if(sd.error!=0u){return;}let base=p(4u)+id.x*"<<sizeof(HuangGateRecord)<<"u;let input=p(30u)+id.x*"<<sizeof(HuangGateInput)<<"u;\n"
     " var r:SvmResult;r.closure_base=p(23u)+id.x*SVM_CLOSURE_CAPACITY*SVM_CLOSURE_BYTES;r.count=load_word(base+"<<O(count)<<"u);r.N=heap_vec3(base+"<<O(N)<<"u);sd.N=r.N;\n"
     " let closures=min(r.count,"<<huang_gate_closures<<"u);\n";};
  head("huang_eval_probe");
  c<<" huang_base=r.closure_base;huang_lcg=load_word(base+"<<O(lcg)<<"u);\n"
     " for(var k=0u;k<"<<huang_gate_directions<<"u;k++){let wo=heap_vec3(input+k*12u);for(var j=0u;j<closures;j++){let cl=svm_closure_read(r,j);\n"
     "  var e=vec4<f32>(0);if(surf_bsdf(cl.kind)){if(cl.kind==HH_CLOSURE_BSDF_HAIR_HUANG_ID){huang_eval_store(sd,cl,wo);}e=surf_evaluate(sd,cl,wo);}\n"
     "  let at=base+"<<O(eval)<<"u+(k*"<<huang_gate_closures<<"u+j)*"<<sizeof(HuangGateEval)<<"u;\n";
  put("at",offsetof(HuangGateEval,value),"e.xyz",true,3);put("at",offsetof(HuangGateEval,pdf),"e.w",true,1);put("at",offsetof(HuangGateEval,lcg),"huang_lcg",false,1);
  c<<" }}\n}\n";
  head("huang_sample_probe");
  c<<" huang_lcg=load_word(base+"<<O(eval)<<"u+("<<huang_gate_directions-1<<"u*"<<huang_gate_closures<<"u+closures-1u)*"<<sizeof(HuangGateEval)<<"u+"<<offsetof(HuangGateEval,lcg)<<"u);\n"
     " huang_base=r.closure_base;\n"
     " for(var j=0u;j<closures;j++){let cl=svm_closure_read(r,j);let random=heap_vec3(input+"<<offsetof(HuangGateInput,random)<<"u+j*12u);\n"
     "  var out:SurfaceSample;if(cl.kind==HH_CLOSURE_BSDF_HAIR_HUANG_ID){out=huang_sample(surf_sample_data(sd,cl),cl,random);}\n"
     "  let at=base+"<<O(sample)<<"u+j*"<<sizeof(HuangGateSample)<<"u;\n";
  put("at",offsetof(HuangGateSample,wo),"out.wo",true,3);put("at",offsetof(HuangGateSample,pdf),"out.pdf",true,1);put("at",offsetof(HuangGateSample,eval),"out.eval",true,3);put("at",offsetof(HuangGateSample,label),"out.label",false,1);put("at",offsetof(HuangGateSample,lcg),"huang_lcg",false,1);
  c<<" }\n}\n";
#undef O
  (void)d;return c.str();
}
struct ShadowField {std::string name;uint32_t address;size_t stride,offset;bool floating;};
static std::vector<ShadowField> shadow_fields(WebGPUDevice &d){std::vector<ShadowField> out;
#define KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) if(d.state.parent.name)out.push_back({#parent "." #name,uint32_t(uintptr_t(d.state.parent.name)),sizeof(gpu_state_storage<type>::gpu_type),offsetof(IntegratorShadowStateCPU,parent.name),!std::is_integral_v<type>});
#define KERNEL_STRUCT_MEMBER_PACKED KERNEL_STRUCT_MEMBER
#define KERNEL_STRUCT_ARRAY_MEMBER(parent,type,name,feature)
#define KERNEL_STRUCT_END(name)
#define KERNEL_STRUCT_END_ARRAY(name,cpu_size,gpu_size)
#include "kernel/integrator/shadow_state_template.h"
#undef KERNEL_STRUCT_BEGIN
#undef KERNEL_STRUCT_BEGIN_PACKED
#undef KERNEL_STRUCT_MEMBER
#undef KERNEL_STRUCT_MEMBER_PACKED
#undef KERNEL_STRUCT_ARRAY_MEMBER
#undef KERNEL_STRUCT_END
#undef KERNEL_STRUCT_END_ARRAY
  return out;
}
static void scene_setup(Scene &scene,Shader *shader,const std::vector<CurveShapeType> &shapes,CurveFixture &f){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);scene.integrator->set_aa_samples(64);scene.integrator->set_seed(17);
  scene.integrator->set_filter_glossy(0.f);scene.integrator->set_max_bounce(8);scene.integrator->set_min_bounce(8);scene.film->set_use_sample_count(false);
  scene.camera->set_full_width(64);scene.camera->set_full_height(64);scene.camera->set_screen_size(64,64);scene.camera->compute_auto_viewplane();
  f=curve_fixture(scene,shader,shapes,shader);
  auto eg=make_unique<ShaderGraph>();auto emission=eg->create_node<EmissionNode>();emission->set_color(one_float3());emission->set_strength(1);
  eg->connect(emission->output("Emission"),eg->output()->input("Surface"));auto emitter=scene.create_node<Shader>();emitter->set_graph(std::move(eg));emitter->tag_update(&scene);
  auto point=scene.create_node<PointLight>();point->set_radius(.6f);point->set_is_sphere(true);point->set_use_mis(true);point->set_strength(make_float3(60,55,50));array<Node*> slots;slots.push_back_slow(emitter);point->set_used_shaders(slots);
  auto lamp=scene.create_node<Object>();lamp->set_geometry(point);lamp->set_tfm(transform_translate(make_float3(3,3,7)));
  for(auto s:scene.shaders)s->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
}
static float3 unit_direction(std::mt19937 &rng){std::normal_distribution<float> g;for(;;){float3 v=make_float3(g(rng),g(rng),g(rng));const float l=len(v);if(l>1e-3f)return v/l;}}
static void run_case(WebGPUDevice &d,const HuangMaterial &material,const char *shape_name,const std::vector<CurveShapeType> &shapes,unsigned seed,unsigned count,std::vector<Row> &rows,std::map<std::string,std::array<cbc::Pipeline,4>> &cache){
  Row row;row.material=material.name;row.shapes=shape_name;
  SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=true;params.use_texture_cache=false;
  Scene scene(params,&d);CurveFixture f;Shader *shader=huang_shader(scene,material);scene_setup(scene,shader,shapes,f);
  Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Hair scene update failed");
  auto &data=scene.dscene.data;data.integrator.transparent_shadows=0;d.const_copy_to("data",&data,sizeof(KernelData));
  bool cancelled=false;ShadowScheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);
  const auto layout=fields(d);const auto shadow_layout=shadow_fields(d);BufferParams bp;bp.width=64;bp.height=64;bp.pass_stride=data.film.pass_stride;
  auto buffers=scheduler.get_render_buffers();buffers->reset(bp);buffers->zero();
  const unsigned n=std::min<unsigned>(unsigned(scheduler.capacity()),count);
  // Pipelines by module text: every case of a feature set shares them.
  const std::string source=d.module_wgsl(probe_source(d));auto &pipes=cache[source];
  if(!pipes[0].value){
    // A new feature set: the previous probe pipelines are released first.
    for(auto it=cache.begin();it!=cache.end();)it=it->first==source?std::next(it):cache.erase(it);
    WGPUShaderSourceWGSL wgsl{};wgsl.chain.sType=WGPUSType_ShaderSourceWGSL;wgsl.code={source.data(),source.size()};
    WGPUShaderModuleDescriptor desc{};desc.nextInChain=&wgsl.chain;d.gpu.push_scope();
    cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> module(wgpuDeviceCreateShaderModule(d.gpu.get(),&desc));d.gpu.pop_scope();
    pipes[0]=d.gpu.pipeline(module,d.pipeline_layout,"prepare_shader_data");pipes[1]=d.gpu.pipeline(module,d.pipeline_layout,"huang_setup_probe");
    pipes[2]=d.gpu.pipeline(module,d.pipeline_layout,"huang_eval_probe");pipes[3]=d.gpu.pipeline(module,d.pipeline_layout,"huang_sample_probe");
  }
  const auto rays=curve_rays(f,n,seed);
  scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Hair reset failed");
  std::vector<IntegratorStateCPU> states(scheduler.capacity());std::vector<int> all;std::mt19937 rng(seed*31u+7u);std::uniform_real_distribution<float> unit(0.f,1.f);
  for(unsigned i=0;i<n;i++){auto &s=states[i];const bool camera=i%2==0;s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST;s.path.visibility=camera?PATH_RAY_VISIBILITY_CAMERA:PATH_RAY_VISIBILITY_GLOSSY;
    s.path.flag=camera?0u:uint32_t(PATH_RAY_REFLECT|PATH_RAY_SURFACE_PASS);s.path.bounce=camera?0:1;s.path.throughput=make_spectrum(.75f);s.path.continuation_probability=1;s.path.sample=i%64;s.path.render_pixel_index=i;s.path.rng_pixel=0x9e3779b9u*i+11u;s.path.rng_offset=uint16_t(32u+(i%5)*32u);s.path.min_ray_pdf=1e6f;s.path.mis_ray_pdf=1e6f;
    s.ray.P=rays[i].P;s.ray.D=rays[i].D;s.ray.tmin=rays[i].tmin;s.ray.tmax=rays[i].tmax;s.ray.time=.5f;s.ray.dP=(i%4==0)?1e-4f:(i%4==1?4e-3f:.05f);s.ray.dD=(i%3==0)?1e-4f:3e-3f;s.isect.prim=PRIM_NONE;s.isect.object=OBJECT_NONE;
    all.push_back(int(i));}
  std::vector<float> scratch(buffers->buffer.size());original_closest(scene.dscene,all,states,scratch);
  // Surface hits only: the closest kernel queues lamp hits (the visible sphere light) for SHADE_LIGHT.
  std::vector<int> hits;for(unsigned i=0;i<n;i++)if(states[i].path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE)hits.push_back(int(i));else states[i].path.queued_kernel=0;
  require(!hits.empty(),"No hair hits");
  std::vector<HuangGateInput> inputs(hits.size());
  for(auto &in:inputs){for(auto &w:in.wo)w=unit_direction(rng);for(auto &x:in.random)x=make_float3(unit(rng),unit(rng),unit(rng));}
  const auto want=original_hair_huang(scene.dscene,hits,states,inputs);
  upload(d,layout,states);
  // The setup, evaluation and sample probes: batches, so no single dispatch runs long.
  device_vector<int> chosen(&d,"huang_indices",MEM_READ_WRITE);chosen.alloc(hits.size());std::copy(hits.begin(),hits.end(),chosen.data());chosen.copy_to_device();
  device_vector<uint> output(&d,"huang_output",MEM_READ_WRITE);output.alloc(16+(hits.size()+1)*sizeof(HuangGateRecord)/4);std::fill_n(output.data(),output.size(),0u);output.copy_to_device();
  device_vector<uint> closures(&d,"huang_closures",MEM_READ_WRITE);closures.alloc(16+hits.size()*MAX_CLOSURE*sizeof(SurfaceClosureRecord)/4);std::fill_n(closures.data(),closures.size(),0u);closures.copy_to_device();
  device_vector<uint> given(&d,"huang_inputs",MEM_READ_WRITE);given.alloc(hits.size()*sizeof(HuangGateInput)/4+16);std::memcpy(given.data(),inputs.data(),inputs.size()*sizeof(HuangGateInput));given.copy_to_device();
  device_vector<uint> cache_storage(&d,"huang_surface_cache",MEM_READ_WRITE);cache_storage.alloc(16+hits.size()*surface_data_cache_bytes/4);std::fill_n(cache_storage.data(),cache_storage.size(),0u);cache_storage.copy_to_device();
  std::array<uint32_t,32> p{};p[1]=hits.size();p[2]=chosen.device_pointer;p[4]=output.device_pointer+32;p[6]=d.constants.at("data");p[7]=d.constants.at("integrator_state");p[10]=uint32_t(states.size());p[14]=d.scene_tables();p[17]=d.error_word;
  p[21]=scene.dscene.svm_nodes.device_pointer;p[22]=scene.dscene.svm_nodes.size();p[23]=closures.device_pointer+32;p[26]=cache_storage.device_pointer+32;p[27]=surface_data_cache_bytes;p[30]=given.device_pointer;
  d.dispatch(p,hits.size(),pipes[0],64);d.dispatch(p,hits.size(),pipes[1],64);d.sync();healthy(d);
  for(unsigned entry=2;entry<4;entry++){d.dispatch(p,hits.size(),pipes[entry],1);d.sync();healthy(d);}
  uint error_word=0;d.download(d.error_word,&error_word,4);require(error_word==0,"Hair probe error word "+std::to_string(error_word));
  output.copy_from_device();healthy(d);
  const auto bytes=reinterpret_cast<const uint8_t*>(output.data())+32;
  for(size_t i=0;i<hits.size();i++){
    HuangGateRecord got;std::memcpy(&got,bytes+i*sizeof(got),sizeof(got));const auto &e=want[i];const auto &st=states[hits[i]];row.paths++;
    const bool curve=st.isect.type&PRIMITIVE_CURVE;row.curve+=curve;row.triangle+=!curve;row.two+=e.count>1;
    auto miss=[&](const char *what,double a,double b){if(mismatch_log)mismatch_log<<std::setprecision(9)<<material.name<<" "<<shape_name<<" path="<<hits[i]<<" curve="<<curve<<" "<<what<<" gpu="<<a<<" cpu="<<b<<"\n";};
    auto integer=[&](const char *what,uint64_t &bucket,uint a,uint b){if(a!=b){bucket++;miss(what,a,b);}};
    auto number=[&](const char *what,Tally &t,float a,float b){const auto before=t.practical;const auto bits=t.bits;t.add(a,b);if(t.practical!=before||(t.bits!=bits&&std::getenv("CODEBRIX_HAIR_GATE_BITS")))miss(what,a,b);};
    auto vec=[&](const char *what,Tally &t,const packed_float3 &a,const packed_float3 &b){number(what,t,a.x,b.x);number(what,t,a.y,b.y);number(what,t,a.z,b.z);};
    integer("count",row.counts,got.count,e.count);integer("remaining",row.counts,got.remaining,e.remaining);integer("runtime_flag",row.integers,got.runtime_flag,e.runtime_flag);integer("lcg-init",row.lcg,got.lcg,e.lcg);
    vec("N",row.setup,got.N,e.N);number("transparency",row.setup,got.transparency,e.transparency);
    const unsigned count_closures=std::min<unsigned>(e.count,huang_gate_closures);
    for(unsigned j=0;j<count_closures;j++){const auto &a=got.closures[j];const auto &b=e.closures[j];
      integer("kind",row.integers,a.kind,b.kind);if(b.kind==CLOSURE_BSDF_TRANSPARENT_ID)row.transparent++;
      vec("weight",row.setup,a.weight,b.weight);number("sample_weight",row.setup,a.sample_weight,b.sample_weight);vec("closure N",row.setup,a.N,b.N);
      if(b.kind==CLOSURE_BSDF_HAIR_HUANG_ID){
        if(b.radius>b.pixel_coverage)row.near++;else row.far++;
        number("roughness",row.setup,a.roughness,b.roughness);number("tilt",row.setup,a.tilt,b.tilt);number("eta",row.setup,a.eta,b.eta);number("aspect",row.setup,a.aspect_ratio,b.aspect_ratio);number("h",row.setup,a.h,b.h);
        vec("sigma",row.setup,a.sigma,b.sigma);vec("Y",row.setup,a.Y,b.Y);vec("Z",row.setup,a.Z,b.Z);vec("wi",row.setup,a.wi,b.wi);
        number("radius",row.setup,a.radius,b.radius);number("e2",row.setup,a.e2,b.e2);number("pixel_coverage",row.setup,a.pixel_coverage,b.pixel_coverage);number("R",row.setup,a.R,b.R);number("TT",row.setup,a.TT,b.TT);number("TRT",row.setup,a.TRT,b.TRT);
      }
      for(unsigned k=0;k<huang_gate_directions;k++){const auto &x=got.eval[k][j];const auto &y=e.eval[k][j];
        vec("eval",row.eval,x.value,y.value);number("eval pdf",row.eval,x.pdf,y.pdf);integer("eval lcg",row.lcg,x.lcg,y.lcg);
        if(y.value.x==0&&y.value.y==0&&y.value.z==0)row.zero_eval++;else row.nonzero_eval++;}
      if(b.kind==CLOSURE_BSDF_HAIR_HUANG_ID){const auto &x=got.sample[j];const auto &y=e.sample[j];integer("sample label",row.labels,x.label,y.label);integer("sample lcg",row.lcg,x.lcg,y.lcg);
        number("sample pdf",row.sample,x.pdf,y.pdf);
        if(y.label){vec("sample wo",row.sample,x.wo,y.wo);vec("sample eval",row.sample,x.eval,y.eval);}else row.zero_sample++;}
    }
  }
  // The whole surface stage on the same states.
  {
    std::vector<int> surface=hits;auto input=states;std::vector<float> film(buffers->buffer.size(),0.f);auto expected=states;original_surface(scene.dscene,surface,expected,film);
    upload(d,layout,input);IntegratorQueueCounter counters{};counters.num_queued[DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE]=hits.size();d.upload(uintptr_t(d.state.queue_counter),&counters,sizeof(counters));
    scheduler.set_active(int(n));buffers->zero();scheduler.shadow_reset();require(scheduler.update_queue_counter_and_cache(),"Hair counters failed");
    scheduler.enqueue_path_iteration(DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE);require(scheduler.update_queue_counter_and_cache(),"Hair stage failed: "+d.error_message());
    IntegratorQueueCounter actual_counter{};d.download(uintptr_t(d.state.queue_counter),&actual_counter,sizeof(actual_counter));
    IntegratorQueueCounter expected_counter{};for(int i:hits)if(expected[i].path.queued_kernel)expected_counter.num_queued[expected[i].path.queued_kernel]++;
    const auto actual=download(d,layout,n);
    for(int i:hits)for(auto &field:layout){
      const auto a=reinterpret_cast<const char*>(&actual[i])+field.offset;const auto e=reinterpret_cast<const char*>(&expected[i])+field.offset;
      if(field.floating){for(size_t k=0;k<field.stride;k+=4){float av,ev;std::memcpy(&av,a+k,4);std::memcpy(&ev,e+k,4);const auto before=row.stage.practical;row.stage.add(av,ev);
          if(row.stage.practical!=before&&mismatch_log)mismatch_log<<std::setprecision(9)<<material.name<<" "<<shape_name<<" stage path="<<i<<" "<<field.name<<"["<<k/4<<"] gpu="<<av<<" cpu="<<ev<<"\n";}}
      else if(std::memcmp(a,e,field.stride)){row.stage_integers++;if(mismatch_log)mismatch_log<<material.name<<" "<<shape_name<<" stage path="<<i<<" integer "<<field.name<<"\n";}
    }
    // Shadow paths count in the same counters.
    for(int i:hits)if(expected[i].shadow.shadow_path.queued_kernel)expected_counter.num_queued[expected[i].shadow.shadow_path.queued_kernel]++;
    if(std::memcmp(&actual_counter,&expected_counter,sizeof(actual_counter))){row.queue++;
      for(int k=0;k<int(sizeof(actual_counter.num_queued)/sizeof(actual_counter.num_queued[0]));k++)if(actual_counter.num_queued[k]!=expected_counter.num_queued[k]&&mismatch_log)mismatch_log<<material.name<<" "<<shape_name<<" queue "<<k<<" gpu="<<actual_counter.num_queued[k]<<" cpu="<<expected_counter.num_queued[k]<<"\n";}
    // Shadow paths, matched by render pixel (unique here): the CPU's shadow state against the GPU's.
    const int shadow_paths=scheduler.shadow_count();std::vector<IntegratorShadowStateCPU> shadow_actual(shadow_paths);
    for(auto &field:shadow_layout){std::vector<uint8_t> raw(size_t(shadow_paths)*field.stride);if(shadow_paths)d.download(field.address,raw.data(),raw.size());for(int i=0;i<shadow_paths;i++)std::memcpy(reinterpret_cast<uint8_t*>(&shadow_actual[i])+field.offset,raw.data()+i*field.stride,field.stride);}
    // The device reserves one shadow slot per path (surface.cpp); the slots no path took stay unqueued.
    int expected_shadows=0,queued_shadows=0;for(int i:hits)expected_shadows+=expected[i].shadow.shadow_path.queued_kernel!=0;
    for(int s=0;s<shadow_paths;s++)queued_shadows+=shadow_actual[s].shadow_path.queued_kernel!=0;
    if(queued_shadows!=expected_shadows){row.shadow_integers++;if(mismatch_log)mismatch_log<<material.name<<" "<<shape_name<<" shadow count gpu="<<queued_shadows<<" cpu="<<expected_shadows<<"\n";}
    row.shadow_paths+=queued_shadows;
    for(int s=0;s<shadow_paths;s++){if(!shadow_actual[s].shadow_path.queued_kernel)continue;const int pixel=int(shadow_actual[s].shadow_path.render_pixel_index);
      if(pixel<0||pixel>=int(n)||!expected[pixel].shadow.shadow_path.queued_kernel){row.shadow_integers++;continue;}
      if(shadow_actual[s].shadow_path.queued_kernel!=expected[pixel].shadow.shadow_path.queued_kernel)row.shadow_integers++;
      // packed_num_hits: written by the shadow intersection (intersect_shadow.h) before any read; the CPU's is the
      // zero-initialized state, the device's slot is not written by the surface stage.
      for(auto &field:shadow_layout){if(field.name=="shadow_path.queued_kernel"||field.name=="shadow_path.packed_num_hits")continue;
        const auto a=reinterpret_cast<const char*>(&shadow_actual[s])+field.offset;const auto e=reinterpret_cast<const char*>(&expected[pixel].shadow)+field.offset;
        if(field.floating){for(size_t k=0;k<field.stride;k+=4){float av,ev;std::memcpy(&av,a+k,4);std::memcpy(&ev,e+k,4);const auto before=row.shadow.practical;row.shadow.add(av,ev);
            if(row.shadow.practical!=before&&mismatch_log)mismatch_log<<std::setprecision(9)<<material.name<<" "<<shape_name<<" shadow pixel="<<pixel<<" "<<field.name<<"["<<k/4<<"] gpu="<<av<<" cpu="<<ev<<"\n";}}
        else if(std::memcmp(a,e,field.stride)){row.shadow_integers++;uint av=0,ev=0;std::memcpy(&av,a,std::min<size_t>(4,field.stride));std::memcpy(&ev,e,std::min<size_t>(4,field.stride));
          if(mismatch_log)mismatch_log<<material.name<<" "<<shape_name<<" shadow pixel="<<pixel<<" integer "<<field.name<<" gpu="<<av<<" cpu="<<ev<<"\n";}}
    }
  }
  rows.push_back(row);
  std::cerr<<material.name<<" "<<shape_name<<" paths="<<row.paths<<" lcg="<<row.lcg<<" labels="<<row.labels<<" ints="<<row.integers<<" setupP="<<row.setup.practical<<" evalP="<<row.eval.practical<<" sampleP="<<row.sample.practical<<" stageP="<<row.stage.practical<<" stageI="<<row.stage_integers<<" shadowP="<<row.shadow.practical<<" shadowI="<<row.shadow_integers<<"\n";
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc>=2,"Usage: hair_huang_gate OUTPUT_DIRECTORY [MATERIAL_NAME]");
  const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);mismatch_log.open(out/"mismatches.txt");
  auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");Stats stats;Profiler profiler;
  std::vector<Row> rows;
  struct ShapeCase {const char *name;std::vector<CurveShapeType> shapes;};
  const std::vector<ShapeCase> shape_cases={{"ribbon",{CURVE_RIBBON}},{"thick",{CURVE_THICK}},{"linear",{CURVE_THICK_LINEAR}},{"mixed",{CURVE_RIBBON,CURVE_THICK,CURVE_THICK_LINEAR}}};
  unsigned seed=20261107u;
  // One device per material, as curve_gate does per case.
  for(const auto &m:huang_materials()){if(argc>=3&&m.name!=std::string(argv[2]))continue;
    auto owner=Device::create(infos[0],stats,profiler,true);auto &d=require_webgpu_device(owner.get(),"hair_huang_gate device");std::map<std::string,std::array<cbc::Pipeline,4>> cache;
    for(const auto &s:shape_cases){const bool low=m.roughness<.05f;run_case(d,m,s.name,s.shapes,seed+=97u,low?256u:1024u,rows,cache);}}
  std::ostringstream json;json<<std::setprecision(9)<<"{\"passed\":";std::ostringstream body;bool passed=true;
  auto tally=[](std::ostream &o,const char *name,const Tally &t){o<<",\""<<name<<"\":{\"floats\":"<<t.floats<<",\"bits\":"<<t.bits<<",\"strict\":"<<t.strict<<",\"practical\":"<<t.practical<<",\"maximum\":"<<t.maximum<<"}";};
  for(size_t i=0;i<rows.size();i++){const auto &r=rows[i];
    passed&=!(r.lcg||r.labels||r.counts||r.integers||r.stage_integers||r.shadow_integers||r.queue||r.setup.practical||r.eval.practical||r.sample.practical||r.stage.practical||r.shadow.practical);
    body<<(i?",":"")<<"{\"material\":\""<<r.material<<"\",\"shapes\":\""<<r.shapes<<"\",\"paths\":"<<r.paths<<",\"curve\":"<<r.curve<<",\"triangle\":"<<r.triangle<<",\"near\":"<<r.near<<",\"far\":"<<r.far<<",\"transparent\":"<<r.transparent<<",\"twoClosures\":"<<r.two<<",\"zeroEval\":"<<r.zero_eval<<",\"nonzeroEval\":"<<r.nonzero_eval<<",\"zeroSample\":"<<r.zero_sample
        <<",\"lcg\":"<<r.lcg<<",\"labels\":"<<r.labels<<",\"counts\":"<<r.counts<<",\"integers\":"<<r.integers<<",\"stageIntegers\":"<<r.stage_integers<<",\"shadowPaths\":"<<r.shadow_paths<<",\"shadowIntegers\":"<<r.shadow_integers<<",\"queue\":"<<r.queue;
    tally(body,"setup",r.setup);tally(body,"eval",r.eval);tally(body,"sample",r.sample);tally(body,"stage",r.stage);tally(body,"shadow",r.shadow);body<<"}";}
  json<<(passed?"true":"false")<<",\"rows\":["<<body.str()<<"],\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
  std::cout<<json.str();if(!passed)status=2;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
