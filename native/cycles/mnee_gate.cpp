// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Lockstep state gate for DEVICE_KERNEL_INTEGRATOR_INTERSECT_MNEE: every path the
// pinned CPU wavefront queues at INTERSECT_MNEE (all bounces, every fixture case of
// mnee_fixture.h) runs once through the WebGPU kernel and once through the
// unmodified CPU integrator_intersect_mnee on the same input state. Main-path
// fields, the queue transition and sort keys, and the MNEE shadow slot payload
// (light sample, receiver direction, vertex count, throughput, shadow ray, owner)
// are compared: integers and flags exactly, floats with the practical criterion
// (strict recorded). The GPU walk diagnostics (caster count, solver outcome,
// iteration count) are reported per case. The stage-state exchange below moves the
// judgement to Linux x64 (glibc) on devices whose C library is not correctly rounded.
#include "wavefront_test.h"
#include "wavefront.h"
#include "reference_policy.h"
#include "camera_reference.h"
#include "closest_reference.h"
#include "surface_reference.h"
#include "background_reference.h"
#include "shadow_reference.h"
#include "light_reference.h"
#include "mnee_reference.h"
#include "mnee_fixture.h"
#include "surface_closure.h"
#include "util/progress.h"
#include "util/task.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <algorithm>
#include <sstream>
using namespace ccl;
using namespace wavefront_test;
constexpr unsigned width=32,height=24,samples=8;
static uint64_t diagnosed=0;
// MNEE stage-state exchange (native/DEVICE.md, "Validation reference policy"; render_gate.cpp has the same for its
// render stages). Every check here is CPU-driven lockstep: the GPU runs on the CPU's path states and is compared with
// the pinned CPU code run on the same states, in this process. Where this process's C library is not correctly rounded
// that CPU result is only a diagnostic, so the comparison moves to Linux x64 (glibc):
//   device pass  CODEBRIX_CYCLES_STAGE_RECORD=FILE: every check runs on the GPU as usual and records its inputs (the
//                checked paths' rows) and every value read back from the GPU, in order; the local CPU comparison is kept
//                as a counted diagnostic. The scene tables this pass prepared are written to FILE.scenes/case-N.scene,
//                so the judge uses exactly the GPU's inputs (Cycles leaves some KernelObject words uninitialized -
//                velocity_scale and alignment padding of objects without volume motion - so two preparations of
//                one scene are not byte-identical and a separate dump pass cannot be verified against).
//   Linux judge  CODEBRIX_CYCLES_SCENE_LOAD=DIR with CODEBRIX_CYCLES_STAGE_JUDGE=FILE: on the device's scene tables each
//                recorded check runs the same code with this host's CPU and the recorded GPU reads (no GPU work), so
//                the same criteria judge the device GPU against glibc. Where this host's CPU asks for a read the record
//                lacks (one of its decisions differs from the device CPU's) the judge fails.
// CODEBRIX_CYCLES_SCENE_DUMP=DIR alone writes the scene tables without GPU work. Without these variables the gate
// behaves exactly as before.
namespace exchange {
enum Mode {Off,Record,Judge};
struct Tape {Mode mode=Off;std::ofstream out;std::string in;size_t at=0;uint64_t checks=0,local_differences=0;std::string first_local_difference;};
static Tape tape;
static void put(const void *data,size_t size){tape.out.write(static_cast<const char*>(data),std::streamsize(size));}
static void put_u64(uint64_t v){put(&v,8);}
static void put_text(const std::string &s){put_u64(s.size());put(s.data(),s.size());}
static void take(void *data,size_t size){require(tape.at+size<=tape.in.size(),"Truncated MNEE stage exchange");std::memcpy(data,tape.in.data()+tape.at,size);tape.at+=size;}
static uint64_t take_u64(){uint64_t v;take(&v,8);return v;}
static std::string take_text(){const uint64_t n=take_u64();require(tape.at+n<=tape.in.size(),"Truncated MNEE stage exchange");std::string s=tape.in.substr(tape.at,size_t(n));tape.at+=size_t(n);return s;}
static bool gpu(){return tape.mode!=Judge;}
// IntegratorStateCPU rows are about 50 KB, mostly fixed arrays these paths leave zero: a row travels as its non-zero
// 64-byte blocks.
constexpr size_t block=64;
static void put_row(const IntegratorStateCPU &row){
  const auto *b=reinterpret_cast<const uint8_t*>(&row);std::vector<uint32_t> live;
  for(size_t k=0;k*block<sizeof(row);k++){const size_t length=std::min(block,sizeof(row)-k*block);
    if(std::any_of(b+k*block,b+k*block+length,[](uint8_t v){return v!=0;}))live.push_back(uint32_t(k));}
  put_u64(live.size());for(auto k:live){put(&k,4);put(b+k*block,std::min(block,sizeof(row)-k*block));}}
static void take_row(IntegratorStateCPU &row){
  auto *b=reinterpret_cast<uint8_t*>(&row);std::fill(b,b+sizeof(row),uint8_t(0));const uint64_t count=take_u64();
  for(uint64_t n=0;n<count;n++){uint32_t k;take(&k,4);require(size_t(k)*block<sizeof(row),"MNEE stage exchange row block");take(b+size_t(k)*block,std::min(block,sizeof(row)-size_t(k)*block));}}
static void open(){
  const char *record=reference_policy::environment("CODEBRIX_CYCLES_STAGE_RECORD"),*judge=reference_policy::environment("CODEBRIX_CYCLES_STAGE_JUDGE");
  require(!(record&&judge),"MNEE stage record and judge are mutually exclusive");
  if(record){tape.mode=Record;tape.out.open(record,std::ios::binary);require(bool(tape.out),std::string("Could not write MNEE stage exchange ")+record);tape.out<<"CBCMNEE1";}
  if(judge){require(reference_policy::reference_only(),"The MNEE stage judge needs the device's scene (CODEBRIX_CYCLES_SCENE_LOAD)");
    tape.mode=Judge;tape.in=reference_policy::read_file(judge);require(!tape.in.compare(0,8,"CBCMNEE1"),std::string("Not an MNEE stage exchange file: ")+judge);tape.at=8;}
}
// A tagged item: the judge requires the same tag and size, in the same order.
static void item(const std::string &tag,void *data,size_t size){
  if(tape.mode==Record){put_text(tag);put_u64(size);put(data,size);}
  else if(tape.mode==Judge){const std::string seen=take_text();
    require(seen==tag,"MNEE stage exchange: the device pass read \""+seen+"\" here, this host's CPU reached \""+tag+"\" (a CPU decision differs from the device pass)");
    require(take_u64()==size,"MNEE stage exchange size differs at \""+tag+"\"");take(data,size);}
}
// One GPU read: performed on the device (and recorded), served from the record by the judge.
static void read(const std::string &tag,void *data,size_t size,const std::function<void()> &fetch){if(gpu())fetch();item(tag,data,size);}
// A GPU-sized array (its length can differ between adapters): the judge takes the device's length.
template<class T> static void read_vector(const std::string &tag,std::vector<T> &v,const std::function<void()> &fetch){
  uint64_t n=v.size();item(tag+" length",&n,8);if(!gpu())v.assign(size_t(n),T{});read(tag,v.data(),v.size()*sizeof(T),fetch);}
// Path states after a GPU stage: the rows of IDS, the only rows the checks read. The records carry raw
// IntegratorStateCPU rows; both hosts are x86-64 and the layout is checked per case (begin_case).
static std::vector<IntegratorStateCPU> states(const std::string &tag,WebGPUDevice &d,const std::vector<Field> &layout,size_t count,const std::vector<int> &ids){
  std::vector<IntegratorStateCPU> out;if(gpu())out=download(d,layout,unsigned(count));else out.assign(count,IntegratorStateCPU{});
  for(int i:ids){
    if(tape.mode==Record){put_text(tag);put_row(out[i]);}
    else if(tape.mode==Judge){const std::string seen=take_text();
      require(seen==tag,"MNEE stage exchange: the device pass read \""+seen+"\" here, this host's CPU reached \""+tag+"\" (a CPU decision differs from the device pass)");
      take_row(out[i]);}}
  return out;}
// A check's inputs: written by the device pass when the check starts, read back by the judge to run it.
static void check(const std::string &kind,const std::string &label,const std::vector<int> &ids,const std::vector<IntegratorStateCPU> &input){
  if(tape.mode!=Record)return;
  put_u64(1);put_text(kind);put_text(label);put_u64(ids.size());put(ids.data(),ids.size()*sizeof(int));
  for(int i:ids){put_row(input[i]);}
  tape.checks++;}
static bool next(std::string &kind,std::string &label,std::vector<int> &ids,std::vector<IntegratorStateCPU> &input){
  if(take_u64()!=1)return false;
  kind=take_text();label=take_text();ids.resize(size_t(take_u64()));take(ids.data(),ids.size()*sizeof(int));
  std::fill(input.begin(),input.end(),IntegratorStateCPU{});
  for(int i:ids){require(i>=0&&size_t(i)<input.size(),"MNEE stage exchange path index");take_row(input[i]);}
  tape.checks++;return true;}
static std::string abi(const std::vector<Field> &layout){
  std::ostringstream o;o<<sizeof(IntegratorStateCPU)<<'/'<<sizeof(IntegratorShadowStateCPU);for(auto &f:layout)o<<';'<<f.name<<'@'<<f.offset<<'+'<<f.stride;return o.str();}
static void begin_case(int mode,const std::vector<Field> &layout){
  if(tape.mode==Record){put_u64(2);put_u64(uint64_t(mode));put_text(abi(layout));}
  else if(tape.mode==Judge){require(take_u64()==2&&take_u64()==uint64_t(mode),"MNEE stage exchange case order differs");
    require(take_text()==abi(layout),"MNEE stage exchange: the device's IntegratorStateCPU layout differs from this host's");}}
static void end_case(){if(tape.mode==Record)put_u64(0);}
static void finish(){
  if(tape.mode==Record){put_u64(3);put_u64(tape.checks);tape.out.close();require(!tape.out.fail(),"Could not write MNEE stage exchange");}
  else if(tape.mode==Judge)require(take_u64()==3&&take_u64()==tape.checks&&tape.at==tape.in.size(),"MNEE stage exchange trailer differs");}
}
// A comparison of the lockstep checks. In the exchange's device pass the local CPU (this host's C library) only
// records a diagnostic and the run continues; the Linux judge decides with the same comparison.
static void verdict(bool ok,const std::string &message){
  if(ok)return;
  if(exchange::tape.mode==exchange::Record){if(!exchange::tape.local_differences++)exchange::tape.first_local_difference=message;return;}
  require(ok,message);
}
struct Metrics {uint64_t paths=0,sampled=0,integers=0,floats=0,strict=0,dispatches=0;double maximum=0;std::string maximum_field;
  std::map<unsigned,uint64_t> solver,iterations,casters;std::map<std::string,double> field_ratio;};
static void fixture(Scene &scene,int mode){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);
  scene.integrator->set_aa_samples(samples);scene.integrator->set_min_bounce(2);scene.integrator->set_max_bounce(3);scene.integrator->set_filter_glossy(0);
  scene.film->set_use_sample_count(false);scene.camera->set_full_width(width);scene.camera->set_full_height(height);scene.camera->set_screen_size(width,height);
  scene.camera->set_fov(.9f);scene.camera->set_matrix(transform_translate(make_float3(0,0,-7)));scene.camera->compute_auto_viewplane();
  auto background=make_unique<ShaderGraph>();auto bg=background->create_node<BackgroundNode>();bg->set_color(make_float3(.02f,.025f,.03f));background->connect(bg->output("Background"),background->output()->input("Surface"));
  auto world=scene.create_node<Shader>();world->set_graph(std::move(background));world->tag_update(&scene);scene.background->set_shader(world);
  auto emission_graph=make_unique<ShaderGraph>();auto emission=emission_graph->create_node<EmissionNode>();emission->set_color(one_float3());emission->set_strength(1);
  emission_graph->connect(emission->output("Emission"),emission_graph->output()->input("Surface"));auto emitter=scene.create_node<Shader>();emitter->set_graph(std::move(emission_graph));emitter->tag_update(&scene);
  mnee_fixture(scene,mode,emitter);for(auto shader:scene.shaders)shader->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
}
// MNEE shadow-slot fields written by integrator_state_write_mnee.
struct SlotField {const char *name;uint32_t address;size_t stride,offset;bool floating;};
static std::vector<SlotField> slot_fields(WebGPUDevice &d){
  std::vector<SlotField> out;auto &s=d.state;using S=IntegratorShadowStateCPU;
#define F(field,member,type,fl) out.push_back({#field "." #member,uint32_t(uintptr_t(s.field.member)),sizeof(type),offsetof(S,field.member),fl});
  F(shadow_path,queued_kernel,uint16_t,false) F(shadow_path,throughput,packed_float3,true)
  F(shadow_ray,P,packed_float3,true) F(shadow_ray,D,packed_float3,true) F(shadow_ray,tmin,float,true) F(shadow_ray,tmax,float,true) F(shadow_ray,time,float,true)
  F(shadow_ray,dP,float,true) F(shadow_ray,dD,float,true) F(shadow_ray,self_light_object,int,false) F(shadow_ray,self_light_prim,int,false)
  F(shadow_isect[0],t,float,true) F(shadow_isect[0],u,float,true) F(shadow_isect[0],v,float,true) F(shadow_isect[0],object,int,false) F(shadow_isect[0],prim,int,false) F(shadow_isect[0],type,int,false)
  F(shadow_isect[1],t,float,true) F(shadow_isect[1],u,float,true) F(shadow_isect[1],v,float,true) F(shadow_isect[1],object,int,false) F(shadow_isect[1],prim,int,false) F(shadow_isect[1],type,int,false)
#undef F
  return out;
}
// The shadow path the surface stage builds from an MNEE slot (integrate_direct_light_shadow_init_common).
static std::vector<SlotField> shadow_fields(WebGPUDevice &d){
  std::vector<SlotField> out;auto &s=d.state;using S=IntegratorShadowStateCPU;
#define F(field,member,type,fl) out.push_back({#field "." #member,uint32_t(uintptr_t(s.field.member)),sizeof(type),offsetof(S,field.member),fl});
  F(shadow_path,queued_kernel,uint16_t,false) F(shadow_path,throughput,packed_float3,true) F(shadow_path,render_pixel_index,uint32_t,false)
  F(shadow_path,transmission_bounce,uint16_t,false) F(shadow_path,diffuse_bounce,uint16_t,false) F(shadow_path,bounce,uint16_t,false)
  F(shadow_path,glossy_bounce,uint16_t,false) F(shadow_path,transparent_bounce,uint16_t,false) F(shadow_path,lightgroup,uint8_t,false) F(shadow_path,flag,uint32_t,false)
  F(shadow_path,bsdf_eval_average,float,true)
  F(shadow_ray,P,packed_float3,true) F(shadow_ray,D,packed_float3,true) F(shadow_ray,tmin,float,true) F(shadow_ray,tmax,float,true) F(shadow_ray,time,float,true)
  F(shadow_ray,dP,float,true) F(shadow_ray,dD,float,true) F(shadow_ray,self_light_object,int,false) F(shadow_ray,self_light_prim,int,false)
  // Ray self: the last shadow_isect slot (GPU size on the device, CPU size in the oracle state).
  out.push_back({"shadow_isect[last].object",uint32_t(uintptr_t(s.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].object)),sizeof(int),offsetof(S,shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_CPU-1].object),false});
  out.push_back({"shadow_isect[last].prim",uint32_t(uintptr_t(s.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].prim)),sizeof(int),offsetof(S,shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_CPU-1].prim),false});
#undef F
  return out;
}
static void lockstep(WebGPUDevice &d,Scene &scene,const std::vector<Field> &layout,const std::vector<SlotField> &slots,
                     std::vector<IntegratorStateCPU> &states,const std::vector<int> &ids,const std::string &label,Metrics &m){
  exchange::check("mnee",label,ids,states);
  auto expected=states;original_mnee(scene.dscene,ids,expected);
  upload(d,layout,states);
  device_vector<int> selected(&d,"mnee_indices",MEM_READ_WRITE);selected.alloc(std::max<size_t>(1,ids.size()));std::copy(ids.begin(),ids.end(),selected.data());selected.copy_to_device();
  IntegratorQueueCounter counter{};counter.num_queued[DEVICE_KERNEL_INTEGRATOR_INTERSECT_MNEE]=ids.size();d.upload(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));
  const uint32_t zero=0;d.upload(uintptr_t(d.state.next_shadow_path_index),&zero,4);
  const auto keys=uintptr_t(d.state.sort_key_counter[DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE]);std::vector<uint32_t> sort(d.remaining(keys)/4,0);d.upload(keys,sort.data(),sort.size()*4);
  std::vector<uint16_t> idle(d.remaining(uintptr_t(d.state.shadow_path.queued_kernel))/2,0);d.upload(uintptr_t(d.state.shadow_path.queued_kernel),idle.data(),idle.size()*2);
  auto queue=d.gpu_queue_create();int n=int(ids.size());DeviceKernelArguments args;args.add(&selected.device_pointer);args.add(&n);
  if(exchange::gpu()){require(queue->enqueue(DEVICE_KERNEL_INTEGRATOR_INTERSECT_MNEE,n,args),label+": "+d.error_message());require(queue->synchronize(),label+": "+d.error_message());}m.dispatches++;
  auto actual=exchange::states("mnee states",d,layout,states.size(),ids);
  auto number=[&](float a,float e,const std::string &where){double error=std::abs(double(a)-e);if(error>m.maximum){m.maximum=error;m.maximum_field=where;}m.floats++;
    {auto key=where.substr(0,where.find(" path="));if(key.rfind("surface film",0)==0)key="surface film";double &r=m.field_ratio[key];r=std::max(r,error/(5e-5+2e-4*std::abs(double(e))));}
    m.strict+=error>1e-7+1e-7*std::abs(double(e));
    if(std::getenv("CODEBRIX_MNEE_GATE_DIAGNOSE")){if(!(std::isfinite(a)&&std::isfinite(e)&&error<=5e-5+2e-4*std::abs(double(e))))diagnosed++;std::cerr<<"DIAGNOSE "<<label<<" "<<where<<" actual="<<a<<" expected="<<e<<"\n";return;}
    verdict(std::isfinite(a)&&std::isfinite(e)&&error<=5e-5+2e-4*std::abs(double(e)),label+" "+where+" actual="+std::to_string(a)+" expected="+std::to_string(e));};
  for(auto &f:layout){if(f.name=="path.mnee_shadow_state"||f.name=="path.shader_sort_key")continue;
    for(int i:ids){auto a=reinterpret_cast<const char*>(&actual[i])+f.offset;auto e=reinterpret_cast<const char*>(&expected[i])+f.offset;
      if(f.floating){for(size_t k=0;k<f.stride;k+=4){float av,ev;std::memcpy(&av,a+k,4);std::memcpy(&ev,e+k,4);number(av,ev,f.name+" path="+std::to_string(i)+" component="+std::to_string(k/4));}}
      else{verdict(!std::memcmp(a,e,f.stride),label+" integer "+f.name+" path="+std::to_string(i)+" actual="+std::to_string(int(*reinterpret_cast<const uint8_t*>(a)))+" expected="+std::to_string(int(*reinterpret_cast<const uint8_t*>(e))));m.integers++;}}}
  // Sort keys: same shader bucket as integrator_path_next_sorted.
  std::vector<uint32_t> keys_after(sort.size());exchange::read_vector("mnee sort keys",keys_after,[&]{d.download(keys,keys_after.data(),keys_after.size()*4);});
  std::vector<uint32_t> keys_expected(keys_after.size(),0);for(int i:ids)keys_expected[actual[i].path.shader_sort_key]++;
  for(int i:ids){const uint32_t key=(scene.dscene.tri_shader[expected[i].isect.prim]&SHADER_MASK)+scene.dscene.data.max_shaders*(uint32_t(i)/d.state.sort_partition_divisor);
    verdict(actual[i].path.shader_sort_key==key,label+" sort key path="+std::to_string(i));m.integers++;}
  verdict(keys_after==keys_expected,label+" sort key counters");
  // Shadow slots of the sampled paths.
  uint32_t allocated=0;exchange::read("mnee allocated",&allocated,4,[&]{d.download(uintptr_t(d.state.next_shadow_path_index),&allocated,4);});
  std::vector<int32_t> owners(std::max<uint32_t>(1,allocated));exchange::read("mnee slot owners",owners.data(),allocated*4,[&]{if(allocated)d.download(uintptr_t(d.state.shadow_isect[2].prim),owners.data(),allocated*4);});
  unsigned sampled=0;
  for(int i:ids){const bool on=expected[i].path.mnee&PATH_MNEE_SAMPLED;if(!on)continue;sampled++;
    const uint32_t slot=uint32_t(actual[i].path.mnee_shadow_state);verdict(slot<allocated&&owners[slot]==i,label+" MNEE slot owner path="+std::to_string(i));m.integers++;
    for(auto &f:slots){std::vector<char> a(f.stride);exchange::read(std::string("mnee slot ")+f.name,a.data(),f.stride,[&]{d.download(f.address+slot*f.stride,a.data(),f.stride);});const char *e=reinterpret_cast<const char*>(&expected[i].shadow)+f.offset;
      if(f.floating){for(size_t k=0;k<f.stride;k+=4){float av,ev;std::memcpy(&av,a.data()+k,4);std::memcpy(&ev,e+k,4);number(av,ev,std::string("slot ")+f.name+" path="+std::to_string(i)+" component="+std::to_string(k/4));}}
      else{verdict(!std::memcmp(a.data(),e,f.stride),label+" slot integer "+f.name+" path="+std::to_string(i));m.integers++;}}}
  // Stable reservation: one slot per dispatched path, the sampled ones live (owner checked above).
  verdict(allocated==ids.size(),label+" MNEE slot reservation "+std::to_string(allocated)+" expected "+std::to_string(ids.size()));
  {std::vector<uint16_t> queued(allocated);exchange::read("mnee queued",queued.data(),allocated*2,[&]{if(allocated)d.download(uintptr_t(d.state.shadow_path.queued_kernel),queued.data(),allocated*2);});
   unsigned pending=0;for(auto q:queued)pending+=q==DEVICE_KERNEL_INTEGRATOR_SHADOW_PATH_MNEE_PENDING;
   verdict(pending==sampled&&std::all_of(queued.begin(),queued.end(),[](uint16_t q){return q==0||q==DEVICE_KERNEL_INTEGRATOR_SHADOW_PATH_MNEE_PENDING;}),label+" MNEE pending slots");m.integers++;}
  IntegratorQueueCounter after{};exchange::read("mnee queue counter",&after,sizeof(after),[&]{d.download(uintptr_t(d.state.queue_counter),&after,sizeof(after));});
  IntegratorQueueCounter wanted{};wanted.num_queued[DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE]=ids.size();wanted.num_queued[DEVICE_KERNEL_INTEGRATOR_SHADOW_PATH_MNEE_PENDING]=sampled;
  verdict(!std::memcmp(&after,&wanted,sizeof(after)),label+" queue counters");
  // GPU walk diagnostics (wavefront_mnee.wgsl record meta).
  // and the walk itself against the pinned helpers replayed step by step (original_mnee_trace):
  // solver outcome and iteration count exact, n_offset and final vertex positions practical.
  std::vector<uint32_t> record(2720/4);
  for(size_t k=0;k<ids.size();k++){exchange::read("mnee walk record",record.data(),record.size()*4,[&]{d.download(d.mnee_records+uint32_t(k*(2720+MAX_CLOSURE*sizeof(SurfaceClosureRecord))),record.data(),record.size()*4);});
    const uint32_t solver=record[(316+8)/4],iterations=record[(316+4)/4],count=record[316/4];
    m.casters[count]++;if(solver)m.solver[solver]++;if(iterations)m.iterations[iterations]++;
    if(!solver)continue;
    const auto trace=original_mnee_trace(scene.dscene,states[ids[k]]);const std::string where=" path="+std::to_string(ids[k]);
    if(std::getenv("CODEBRIX_MNEE_GATE_DIAGNOSE")&&(int(solver)!=trace.solved||int(iterations)!=trace.iterations)){
      diagnosed++;std::cerr<<"DIAGNOSE "<<label<<" walk"<<where<<" gpu solver="<<solver<<" iterations="<<iterations<<" cpu solver="<<trace.solved<<" iterations="<<trace.iterations<<"\n";
      for(int it=0;it<48&&it<trace.iterations;it++){float g;std::memcpy(&g,&record[380/4+it],4);std::cerr<<"   norm "<<it<<" gpu="<<std::setprecision(9)<<g<<" cpu="<<trace.norms[it]<<"\n";}}
    else{verdict(int(count)==trace.count&&int(solver)==trace.solved&&int(iterations)==trace.iterations,label+" walk outcome"+where+" gpu="+std::to_string(solver)+"/"+std::to_string(iterations)+
      " cpu="+std::to_string(trace.solved)+"/"+std::to_string(trace.iterations));m.integers+=3;}
    for(uint32_t v=0;v<count&&v<6;v++){const size_t at=(572+v*176)/4;
      for(int c=0;c<3;c++){float g;std::memcpy(&g,&record[at+c],4);number(g,trace.final_p[v][c],"walk vertex"+std::to_string(v)+where);}
      for(int c=0;c<2;c++){float g;std::memcpy(&g,&record[at+27+c],4);number(g,trace.n_offset[v][c],"walk n_offset"+std::to_string(v)+where);}}
  }
  // Surface lockstep of the same paths (MNEE consumption and cleanup), with the GPU slot indices.
  {auto input=expected;for(int i:ids)input[i].path.mnee_shadow_state=actual[i].path.mnee_shadow_state;
    auto wanted_states=input;std::vector<float> wanted_film(width*height*4,0.f);original_surface(scene.dscene,ids,wanted_states,wanted_film);
    upload(d,layout,input);IntegratorQueueCounter c{};c.num_queued[DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE]=ids.size();c.num_queued[DEVICE_KERNEL_INTEGRATOR_SHADOW_PATH_MNEE_PENDING]=sampled;
    d.upload(uintptr_t(d.state.queue_counter),&c,sizeof(c));
    device_vector<float> gpu_film(&d,"mnee_surface_film",MEM_READ_WRITE);gpu_film.alloc(width*height*4);std::fill_n(gpu_film.data(),gpu_film.size(),0.f);gpu_film.copy_to_device();
    DeviceKernelArguments sargs;device_ptr selection=selected.device_pointer;sargs.add(&selection);sargs.add(&gpu_film.device_pointer);sargs.add(&n);
    if(exchange::gpu()){require(queue->enqueue(DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,n,sargs),label+" surface: "+d.error_message());require(queue->synchronize(),label+" surface: "+d.error_message());}
    auto after_surface=exchange::states("surface states",d,layout,states.size(),ids);
    for(auto &f:layout){if(f.name=="path.mnee_shadow_state"||f.name=="path.shader_sort_key")continue;
      for(int i:ids){auto a=reinterpret_cast<const char*>(&after_surface[i])+f.offset;auto e=reinterpret_cast<const char*>(&wanted_states[i])+f.offset;
        if(f.floating){for(size_t k=0;k<f.stride;k+=4){float av,ev;std::memcpy(&av,a+k,4);std::memcpy(&ev,e+k,4);number(av,ev,"surface "+f.name+" path="+std::to_string(i)+" component="+std::to_string(k/4));}}
        else{verdict(!std::memcmp(a,e,f.stride),label+" surface integer "+f.name+" path="+std::to_string(i));m.integers++;}}}
    exchange::read("surface film",gpu_film.data(),gpu_film.size()*4,[&]{gpu_film.copy_from_device();healthy(d);});for(size_t k=0;k<gpu_film.size();k++)number(gpu_film[k],wanted_film[k],"surface film="+std::to_string(k));
    for(int i:ids){if(!(expected[i].path.mnee&PATH_MNEE_SAMPLED))continue;const uint32_t slot=uint32_t(actual[i].path.mnee_shadow_state);
      const bool live=wanted_states[i].shadow.shadow_path.queued_kernel!=0;
      for(auto &f:shadow_fields(d)){if(!live&&std::string(f.name)!="shadow_path.queued_kernel")continue;
        std::vector<char> a(f.stride);exchange::read(std::string("surface slot ")+f.name,a.data(),f.stride,[&]{d.download(f.address+slot*f.stride,a.data(),f.stride);});const char *e=reinterpret_cast<const char*>(&wanted_states[i].shadow)+f.offset;
        if(f.floating){for(size_t k=0;k<f.stride;k+=4){float av,ev;std::memcpy(&av,a.data()+k,4);std::memcpy(&ev,e+k,4);number(av,ev,std::string("surface slot ")+f.name+" path="+std::to_string(i)+" component="+std::to_string(k/4));}}
        else{verdict(!std::memcmp(a.data(),e,f.stride),label+" surface slot integer "+f.name+" path="+std::to_string(i));m.integers++;}}}
    // Shadow lockstep of the live MNEE shadow paths: intersect then shade, film compared.
    std::vector<int> paths,gpu_slots;for(int i:ids)if((expected[i].path.mnee&PATH_MNEE_SAMPLED)&&wanted_states[i].shadow.shadow_path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW){paths.push_back(i);gpu_slots.push_back(int(actual[i].path.mnee_shadow_state));}
    if(!paths.empty()){
      std::vector<IntegratorShadowStateCPU> shadows(states.size());for(int i:paths)shadows[i]=wanted_states[i].shadow;std::vector<float> cpu_film(width*height*4,0.f);
      original_shadow(scene.dscene,DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,paths,shadows,cpu_film);
      std::vector<int> clear;std::map<int,bool> cpu_clear;for(int i:paths){cpu_clear[i]=shadows[i].shadow_path.queued_kernel!=0;if(cpu_clear[i])clear.push_back(i);}
      original_shadow(scene.dscene,DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW,clear,shadows,cpu_film);
      std::fill_n(gpu_film.data(),gpu_film.size(),0.f);gpu_film.copy_to_device();
      IntegratorQueueCounter q{};q.num_queued[DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW]=paths.size();d.upload(uintptr_t(d.state.queue_counter),&q,sizeof(q));
      device_vector<int> list(&d,"mnee_shadow_slots",MEM_READ_WRITE);list.alloc(gpu_slots.size());std::copy(gpu_slots.begin(),gpu_slots.end(),list.data());list.copy_to_device();
      int ns=int(gpu_slots.size());DeviceKernelArguments iargs;iargs.add(&list.device_pointer);iargs.add(&ns);
      if(exchange::gpu()){require(queue->enqueue(DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,ns,iargs),label+" shadow: "+d.error_message());require(queue->synchronize(),label+" shadow: "+d.error_message());}
      std::vector<int> gclear;for(size_t k=0;k<paths.size();k++){uint16_t qk=0;exchange::read("shadow queued",&qk,2,[&]{d.download(uintptr_t(d.state.shadow_path.queued_kernel)+gpu_slots[k]*2,&qk,2);});
        if(exchange::gpu()&&bool(qk)!=cpu_clear[paths[k]]){const auto &w=wanted_states[paths[k]].shadow;std::ostringstream o;o<<std::hexfloat;
          float g[8];d.download(uintptr_t(d.state.shadow_ray.P)+gpu_slots[k]*12,g,12);d.download(uintptr_t(d.state.shadow_ray.D)+gpu_slots[k]*12,g+3,12);
          d.download(uintptr_t(d.state.shadow_ray.tmin)+gpu_slots[k]*4,g+6,4);d.download(uintptr_t(d.state.shadow_ray.tmax)+gpu_slots[k]*4,g+7,4);
          o<<" gpu P="<<g[0]<<","<<g[1]<<","<<g[2]<<" D="<<g[3]<<","<<g[4]<<","<<g[5]<<" t="<<g[6]<<","<<g[7]<<" cpu P="<<w.shadow_ray.P.x<<","<<w.shadow_ray.P.y<<","<<w.shadow_ray.P.z
           <<" D="<<w.shadow_ray.D.x<<","<<w.shadow_ray.D.y<<","<<w.shadow_ray.D.z<<" t="<<w.shadow_ray.tmin<<","<<w.shadow_ray.tmax<<" self="<<w.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_CPU-1].object<<"/"<<w.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_CPU-1].prim
           <<" gpu blocked="<<!qk;
          const float wp[3]={w.shadow_ray.P.x,w.shadow_ray.P.y,w.shadow_ray.P.z},wd[3]={w.shadow_ray.D.x,w.shadow_ray.D.y,w.shadow_ray.D.z};
          auto h=original_ray_hit(scene.dscene,wp,wd,w.shadow_ray.tmin,w.shadow_ray.tmax,w.shadow_ray.time,w.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_CPU-1].object,w.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_CPU-1].prim,
            w.shadow_ray.self_light_object,w.shadow_ray.self_light_prim,PATH_RAY_VISIBILITY_SHADOW_OPAQUE);
          o<<" cpu intersect_shadow again queued="<<original_intersect_shadow_one(scene.dscene,w)<<" after="<<shadows[paths[k]].shadow_path.queued_kernel;
          o<<" cpu closest-opaque hit object="<<h[0]<<" prim="<<h[1]<<" t="<<h[2]<<" light="<<w.shadow_ray.self_light_object<<"/"<<w.shadow_ray.self_light_prim<<" flag="<<std::hex<<w.shadow_path.flag;std::cerr<<o.str()<<"\n";}
        verdict(bool(qk)==cpu_clear[paths[k]],label+" shadow blocked decision path="+std::to_string(paths[k]));m.integers++;if(qk)gclear.push_back(gpu_slots[k]);}
      if(!gclear.empty()){device_vector<int> list2(&d,"mnee_shadow_slots2",MEM_READ_WRITE);list2.alloc(gclear.size());std::copy(gclear.begin(),gclear.end(),list2.data());list2.copy_to_device();
        int nc=int(gclear.size());DeviceKernelArguments sh;sh.add(&list2.device_pointer);sh.add(&gpu_film.device_pointer);sh.add(&nc);
        if(exchange::gpu()){require(queue->enqueue(DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW,nc,sh),label+" shade shadow: "+d.error_message());require(queue->synchronize(),label+" shade shadow: "+d.error_message());}}
      exchange::read("shadow film",gpu_film.data(),gpu_film.size()*4,[&]{gpu_film.copy_from_device();healthy(d);});for(size_t k=0;k<gpu_film.size();k++)number(gpu_film[k],cpu_film[k],"shadow film="+std::to_string(k));
    }
  }
  m.paths+=ids.size();m.sampled+=sampled;states=expected;
  std::cerr<<"Passed "<<label<<" ("<<ids.size()<<" paths, "<<sampled<<" sampled)\n";
}
// Surface lockstep of ordinary surface paths in a caustic scene (caster transmission skip, PATH_MNEE flags):
// main-path fields and film; their new shadow slots are allocated in GPU order and not compared here.
static void surface_lockstep(WebGPUDevice &d,Scene &scene,const std::vector<Field> &layout,const std::vector<IntegratorStateCPU> &states,
                             const std::vector<int> &ids,const std::string &label,Metrics &m){
  exchange::check("surface",label,ids,states);
  auto wanted=states;std::vector<float> wanted_film(width*height*4,0.f);original_surface(scene.dscene,ids,wanted,wanted_film);
  upload(d,layout,states);IntegratorQueueCounter c{};c.num_queued[DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE]=ids.size();d.upload(uintptr_t(d.state.queue_counter),&c,sizeof(c));
  const uint32_t zero=0;d.upload(uintptr_t(d.state.next_shadow_path_index),&zero,4);
  device_vector<int> selected(&d,"mnee_surface_indices",MEM_READ_WRITE);selected.alloc(std::max<size_t>(1,ids.size()));std::copy(ids.begin(),ids.end(),selected.data());selected.copy_to_device();
  device_vector<float> film(&d,"mnee_surface_film2",MEM_READ_WRITE);film.alloc(width*height*4);std::fill_n(film.data(),film.size(),0.f);film.copy_to_device();
  auto queue=d.gpu_queue_create();int n=int(ids.size());DeviceKernelArguments args;args.add(&selected.device_pointer);args.add(&film.device_pointer);args.add(&n);
  if(exchange::gpu()){require(queue->enqueue(DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,n,args),label+" surface: "+d.error_message());require(queue->synchronize(),label+" surface: "+d.error_message());}
  auto actual=exchange::states("plain surface states",d,layout,states.size(),ids);
  auto number=[&](float a,float e,const std::string &where){double error=std::abs(double(a)-e);m.floats++;m.strict+=error>1e-7+1e-7*std::abs(double(e));
    if(error>m.maximum){m.maximum=error;m.maximum_field=where;}
    verdict(std::isfinite(a)&&std::isfinite(e)&&error<=5e-5+2e-4*std::abs(double(e)),label+" "+where+" actual="+std::to_string(a)+" expected="+std::to_string(e));};
  for(auto &f:layout){if(f.name=="path.mnee_shadow_state")continue;
    for(int i:ids){auto a=reinterpret_cast<const char*>(&actual[i])+f.offset;auto e=reinterpret_cast<const char*>(&wanted[i])+f.offset;
      if(f.floating){for(size_t k=0;k<f.stride;k+=4){float av,ev;std::memcpy(&av,a+k,4);std::memcpy(&ev,e+k,4);number(av,ev,"surface "+f.name+" path="+std::to_string(i));}}
      else{verdict(!std::memcmp(a,e,f.stride),label+" surface integer "+f.name+" path="+std::to_string(i));m.integers++;}}}
  exchange::read("plain surface film",film.data(),film.size()*4,[&]{film.copy_from_device();healthy(d);});for(size_t k=0;k<film.size();k++)number(film[k],wanted_film[k],"surface film="+std::to_string(k));
  uint32_t allocated=0;exchange::read("plain surface allocated",&allocated,4,[&]{d.download(uintptr_t(d.state.next_shadow_path_index),&allocated,4);});uint32_t live=0;for(int i:ids)live+=wanted[i].shadow.shadow_path.queued_kernel!=0;
  // Caustic scenes reserve one stable shadow slot per surface input (surface.cpp); the live ones follow the CPU.
  verdict(allocated==ids.size(),label+" surface shadow reservation "+std::to_string(allocated)+" expected "+std::to_string(ids.size()));
  std::vector<uint16_t> queued(allocated);exchange::read("plain surface queued",queued.data(),allocated*2,[&]{if(allocated)d.download(uintptr_t(d.state.shadow_path.queued_kernel),queued.data(),allocated*2);});
  uint32_t used=0;for(auto q:queued)used+=q!=0;
  verdict(used==live,label+" surface shadow count "+std::to_string(used)+" expected "+std::to_string(live));m.integers++;
}
// Closest lockstep in a caustic scene (receiver routing to INTERSECT_MNEE, PATH_MNEE flags).
static void closest_lockstep(WebGPUDevice &d,Scene &scene,const std::vector<Field> &layout,const std::vector<IntegratorStateCPU> &states,
                             const std::vector<int> &ids,const std::string &label,Metrics &m){
  exchange::check("closest",label,ids,states);
  auto wanted=states;std::vector<float> wanted_film(width*height*4,0.f);original_closest(scene.dscene,ids,wanted,wanted_film);
  upload(d,layout,states);IntegratorQueueCounter c{};c.num_queued[DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST]=ids.size();d.upload(uintptr_t(d.state.queue_counter),&c,sizeof(c));
  const auto keys=uintptr_t(d.state.sort_key_counter[DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE]);std::vector<uint32_t> sort(d.remaining(keys)/4,0);d.upload(keys,sort.data(),sort.size()*4);
  device_vector<int> selected(&d,"mnee_closest_indices",MEM_READ_WRITE);selected.alloc(std::max<size_t>(1,ids.size()));std::copy(ids.begin(),ids.end(),selected.data());selected.copy_to_device();
  device_vector<float> film(&d,"mnee_closest_film",MEM_READ_WRITE);film.alloc(width*height*4);std::fill_n(film.data(),film.size(),0.f);film.copy_to_device();
  auto queue=d.gpu_queue_create();int n=int(ids.size());DeviceKernelArguments args;args.add(&selected.device_pointer);args.add(&film.device_pointer);args.add(&n);
  if(exchange::gpu()){require(queue->enqueue(DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST,n,args),label+" closest: "+d.error_message());require(queue->synchronize(),label+" closest: "+d.error_message());}
  auto actual=exchange::states("closest states",d,layout,states.size(),ids);
  for(auto &f:layout){if(f.name=="path.mnee_shadow_state"||f.name=="path.shader_sort_key")continue;
    for(int i:ids){auto a=reinterpret_cast<const char*>(&actual[i])+f.offset;auto e=reinterpret_cast<const char*>(&wanted[i])+f.offset;
      if(f.floating){for(size_t k=0;k<f.stride;k+=4){float av,ev;std::memcpy(&av,a+k,4);std::memcpy(&ev,e+k,4);double error=std::abs(double(av)-ev);m.floats++;
          verdict(std::isfinite(av)==std::isfinite(ev)&&(!std::isfinite(ev)||error<=5e-5+2e-4*std::abs(double(ev))),label+" closest "+f.name+" path="+std::to_string(i));}}
      else{verdict(!std::memcmp(a,e,f.stride),label+" closest integer "+f.name+" path="+std::to_string(i)+" gpu="+std::to_string(int(*(const uint8_t*)a))+" cpu="+std::to_string(int(*(const uint8_t*)e)));m.integers++;}}}
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  reference_policy::use_reference_oracle_arithmetic();exchange::open();
  require(argc==2,"Usage: mnee_gate OUTPUT_PREFIX");const std::string prefix=argv[1];
  auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");Stats stats;Profiler profiler;
  auto owner=Device::create(infos[0],stats,profiler,true);auto &d=require_webgpu_device(owner.get(),"mnee_gate device creation");Metrics total;
  std::ostringstream cases;
  for(int mode=0;mode<mnee_cases;mode++){
    if(mode==3)continue; // caustics off: no INTERSECT_MNEE (covered by the image gate)
    Metrics m;SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=false;params.use_texture_cache=false;
    Scene scene(params,&d);fixture(scene,mode);Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"MNEE scene preparation failed");
    require(scene.dscene.data.integrator.use_caustics&&(scene.dscene.data.kernel_features&KERNEL_FEATURE_MNEE),"MNEE fixture without caustics");
    // Scene exchange (reference_policy.h): a dump pass stops here, the judge continues on the device's loaded tables,
    // and the device record pass writes the tables its GPU uses beside the record.
    const std::string key="case-"+std::to_string(mode);
    if(reference_policy::exchange_scene(scene.dscene,key,{})&&reference_policy::prepare_only())continue;
    if(exchange::tape.mode==exchange::Record){
      const std::filesystem::path folder=std::string(reference_policy::environment("CODEBRIX_CYCLES_STAGE_RECORD"))+".scenes";
      std::filesystem::create_directories(folder);const auto bytes=reference_policy::serialize(scene.dscene,{});
      std::ofstream file(folder/(key+".scene"),std::ios::binary);file.write(bytes.data(),std::streamsize(bytes.size()));
      require(bool(file),"Could not write the MNEE record's scene "+key);}
    bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);
    auto layout=fields(d);auto slots=slot_fields(d);
    exchange::begin_case(mode,layout);
    if(exchange::tape.mode==exchange::Judge){
      // Linux judge: run each recorded check on its recorded inputs (the device CPU's path states).
      std::string kind,label;std::vector<int> ids;std::vector<IntegratorStateCPU> input(width*height);
      while(exchange::next(kind,label,ids,input)){
        if(kind=="closest")closest_lockstep(d,scene,layout,input,ids,label,m);
        else if(kind=="mnee")lockstep(d,scene,layout,slots,input,ids,label,m);
        else if(kind=="surface")surface_lockstep(d,scene,layout,input,ids,label,m);
        else throw std::runtime_error("Unknown MNEE stage exchange check "+kind);
      }
    }else{
    std::vector<IntegratorStateCPU> state(width*height);std::vector<IntegratorShadowStateCPU> shadow(width*height);std::vector<float> film(width*height*4);
    for(unsigned sample=0;sample<samples;sample++){
      std::fill(state.begin(),state.end(),IntegratorStateCPU{});KernelWorkTile tile{};tile.w=width;tile.h=height;tile.stride=width;tile.num_samples=1;tile.start_sample=sample;
      original_camera(scene.dscene,{tile},0,state,film);
      for(unsigned step=0;step<100;step++){
        std::map<DeviceKernel,std::vector<int>> queues;
        for(size_t i=0;i<state.size();i++)if(state[i].path.queued_kernel)queues[DeviceKernel(state[i].path.queued_kernel)].push_back(i);
        if(queues.empty())break;require(step<99,"CPU MNEE paths did not terminate");
        for(auto &[kernel,ids]:queues){
          if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST){closest_lockstep(d,scene,layout,state,ids,"case "+std::to_string(mode)+" sample "+std::to_string(sample)+" step "+std::to_string(step)+" closest",m);original_closest(scene.dscene,ids,state,film);}
          else if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_MNEE)lockstep(d,scene,layout,slots,state,ids,"case "+std::to_string(mode)+" sample "+std::to_string(sample)+" step "+std::to_string(step),m);
          else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND)original_background(scene.dscene,ids,state,film);
          else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD)original_light_forward(scene.dscene,ids,state,film);
          else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE){
            {std::vector<int> plain;for(int i:ids)if(!(state[i].path.mnee&PATH_MNEE_SAMPLED))plain.push_back(i);
             if(!plain.empty())surface_lockstep(d,scene,layout,state,plain,"case "+std::to_string(mode)+" sample "+std::to_string(sample)+" step "+std::to_string(step)+" plain surface",m);}
            original_surface(scene.dscene,ids,state,film);std::vector<int> pending;
            for(int i:ids)if(state[i].shadow.shadow_path.queued_kernel){shadow[i]=state[i].shadow;pending.push_back(i);}
            original_shadow(scene.dscene,DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,pending,shadow,film);
            std::vector<int> clear;for(int i:pending)if(shadow[i].shadow_path.queued_kernel)clear.push_back(i);
            original_shadow(scene.dscene,DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW,clear,shadow,film);for(int i:pending)state[i].shadow=shadow[i];}
          else throw std::runtime_error("Unexpected CPU MNEE kernel");
        }
      }
    }
    }
    exchange::end_case();
    require(m.paths>100&&m.sampled>=(mode==0||mode==1?5u:0u),"Missing MNEE state coverage case "+std::to_string(mode));
    auto histogram=[&](const std::map<unsigned,uint64_t> &h){std::ostringstream o;o<<'{';bool first=true;for(auto [k,v]:h){if(!first)o<<',';first=false;o<<'"'<<k<<"\":"<<v;}o<<'}';return o.str();};
    if(cases.tellp()>0)cases<<',';
    cases<<std::setprecision(17)<<"{\"case\":"<<mode<<",\"paths\":"<<m.paths<<",\"sampled\":"<<m.sampled<<",\"integerChecks\":"<<m.integers<<",\"floatChecks\":"<<m.floats<<",\"strict\":"<<m.strict
         <<",\"maximumAbsoluteError\":"<<m.maximum<<",\"maximumField\":\""<<m.maximum_field<<"\",\"casterCounts\":"<<histogram(m.casters)<<",\"solverOutcomes\":"<<histogram(m.solver)<<",\"iterations\":"<<histogram(m.iterations)<<",\"fieldPracticalRatio\":{";{bool first=true;for(auto &[k,v]:m.field_ratio){if(v<1e-3)continue;if(!first)cases<<",";first=false;cases<<"\""<<k<<"\":"<<v;}}cases<<"}}";
    total.paths+=m.paths;total.sampled+=m.sampled;total.integers+=m.integers;total.floats+=m.floats;total.strict+=m.strict;total.dispatches+=m.dispatches;
    if(m.maximum>total.maximum){total.maximum=m.maximum;total.maximum_field=m.maximum_field;}
  }
  exchange::finish();
  if(reference_policy::prepare_only()){std::cout<<"{\"passed\":true,\"sceneExchangeOnly\":\"dump\"}\n";TaskScheduler::exit();return 0;}
  // CODEBRIX_MNEE_GATE_DIAGNOSE reports every difference instead of stopping at the first; it still fails.
  require(diagnosed==0,"MNEE gate differences: "+std::to_string(diagnosed));
  require(total.sampled>=20,"Missing MNEE sampled-path coverage");
  require(stats.mem_used==0,"MNEE gate leaked native allocations");std::ofstream(prefix+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(prefix+".library.txt")<<d.gpu.library_path;
  std::ostringstream result;result<<std::setprecision(17)<<"{\"passed\":true,";
  if(exchange::tape.mode==exchange::Judge)result<<"\"stageJudge\":true,\"stageChecks\":"<<exchange::tape.checks<<",";
  if(exchange::tape.mode==exchange::Record)result<<"\"stageRecord\":true,\"stageChecks\":"<<exchange::tape.checks<<",\"localDifferencesDiagnostic\":"<<exchange::tape.local_differences
    <<",\"firstLocalDifference\":\""<<reference_policy::escape(exchange::tape.first_local_difference)<<"\",";
  result<<"\"originalCpuIntersectMnee\":true,\"paths\":"<<total.paths<<",\"sampled\":"<<total.sampled<<",\"dispatches\":"<<total.dispatches
    <<",\"integerChecks\":"<<total.integers<<",\"floatChecks\":"<<total.floats<<",\"strictFloatFailures\":"<<total.strict<<",\"maximumAbsoluteError\":"<<total.maximum<<",\"maximumErrorField\":\""<<total.maximum_field<<"\",\"cases\":["<<cases.str()<<"]}\n";
  std::ofstream(prefix+".result.json")<<result.str();std::cout<<result.str();
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
