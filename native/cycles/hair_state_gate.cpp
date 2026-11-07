// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Evaluate, sample and pdf state gate of the hair closures (CBC_BSDF_FIXTURE 4, hair_fixture.h), against the original
// CPU kernels: bsdf_eval of every closure in 24 directions per path (half of them where the sampled lobes are, half
// a scatter over the sphere) and surface_shader_bsdf_bssrdf_pick, bsdf_sample and the MIS of the sampled direction for
// 24 random triples per path, on triangles and on all three curve shapes. Labels and the sampled closure are exact;
// floats by the practical criterion, strict reported. Reports per fixture graph (hair_fixture.h).
#include "wavefront_test.h"
#include "wavefront.h"
#include "surface_reference.h"
#include "hair_reference.h"
#include "surface_closure.h"
#include "scene/scene.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/camera.h"
#include "scene/integrator.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "util/progress.h"
#include "util/task.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <map>
#include <random>
#include <sstream>
using namespace ccl;
using namespace wavefront_test;
#include "surface_fixture.h"
static const char *probe_entries=R"WGSL(
// p(0): per path, 24 directions then 24 random triples (vec3 floats). p(4): the output records.
const HP_DIRECTIONS:u32=24u;const HP_SAMPLES:u32=24u;const HP_CLOSURES:u32=3u;
@compute @workgroup_size(64) fn hair_probe_evaluate(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);if(id.x>=p(1u)){return;}let index=load_word(p(2u)+id.x*4u);
    let sd=surface_cached(id.x);if(sd.error!=0u){return;}
    let r=svm_evaluate(index,sd,data_word(D_max_closures),p(23u)+id.x*SVM_CLOSURE_CAPACITY*SVM_CLOSURE_BYTES);if(r.error!=0u){return;}
    let table=p(0u)+id.x*(HP_DIRECTIONS+HP_SAMPLES)*12u;let base=p(4u)+id.x*HP_DIRECTIONS*HP_CLOSURES*16u;
    for(var j=0u;j<HP_DIRECTIONS;j++){let wo=heap_vec3(table+j*12u);
        for(var k=0u;k<min(r.count,HP_CLOSURES);k++){
            let e=surf_evaluate(sd,svm_closure_read(r,k),wo);let at=base+(j*HP_CLOSURES+k)*16u;
            store_word(at,bitcast<u32>(e.x));store_word(at+4u,bitcast<u32>(e.y));store_word(at+8u,bitcast<u32>(e.z));store_word(at+12u,bitcast<u32>(e.w));
        }}
}
@compute @workgroup_size(64) fn hair_probe_sample(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);if(id.x>=p(1u)){return;}let index=load_word(p(2u)+id.x*4u);
    let sd=surface_cached(id.x);if(sd.error!=0u){return;}
    let r=svm_evaluate(index,sd,data_word(D_max_closures),p(23u)+id.x*SVM_CLOSURE_CAPACITY*SVM_CLOSURE_BYTES);if(r.error!=0u){return;}
    let table=p(0u)+id.x*(HP_DIRECTIONS+HP_SAMPLES)*12u+HP_DIRECTIONS*12u;let base=p(4u)+id.x*HP_SAMPLES*64u;
    for(var j=0u;j<HP_SAMPLES;j++){
        let random=heap_vec3(table+j*12u);surf_sampled_closure=0u;
        let sampled=surf_sample(sd,r,random);surf_sample_store(base+j*64u,sampled,surf_sampled_closure);
    }
}
// The MIS of the stored samples, in its own entry like shade_surface.
@compute @workgroup_size(64) fn hair_probe_mis(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);if(id.x>=p(1u)){return;}let index=load_word(p(2u)+id.x*4u);
    let sd=surface_cached(id.x);if(sd.error!=0u){return;}
    let r=svm_evaluate(index,sd,data_word(D_max_closures),p(23u)+id.x*SVM_CLOSURE_CAPACITY*SVM_CLOSURE_BYTES);if(r.error!=0u){return;}
    let base=p(4u)+id.x*HP_SAMPLES*64u;
    for(var j=0u;j<HP_SAMPLES;j++){
        let sampled=surf_sample_load(base+j*64u);let selected=surf_sampled_closure;let mis=surf_sample_mis(sd,r,sampled);surf_sample_store(base+j*64u,mis,selected);
    }
}
// The libm adapters the hair code uses, against glibc (p(0) inputs as pairs, p(1) count, p(4) seven results each).
@compute @workgroup_size(64) fn hair_probe_math(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}let x=heap_float(p(0u)+id.x*8u);let y=heap_float(p(0u)+id.x*8u+4u);let at=p(4u)+id.x*28u;
    let sc=hair_sincos(x);
    let values=array<f32,7>(family_exp(x),hair_logf(abs(x)),sc.x,sc.y,trig_asin(clamp(y,-1.0,1.0)),hair_atan2(y,x),hair_tan(x));
    for(var k=0u;k<7u;k++){store_word(at+k*4u,bitcast<u32>(values[k]));}
}
)WGSL";
struct Row {uint64_t paths=0,evals=0,nonzero_evals=0,samples=0,sampled=0,labels=0,float_checks=0,bits=0,strict=0,practical=0;double maximum=0;std::string worst;std::map<uint,uint64_t> label_counts;};
static bool judge(float a,float e,Row &row,const std::string &where){
  row.float_checks++;if(std::bit_cast<uint>(a)!=std::bit_cast<uint>(e))row.bits++;
  const double error=std::abs(double(a)-double(e));
  if(error>1e-7+1e-7*std::abs(double(e)))row.strict++;
  if(error>row.maximum||!std::isfinite(error)){row.maximum=error;row.worst=where+" actual="+std::to_string(a)+" expected="+std::to_string(e);}
  if(!(std::isfinite(a)&&std::isfinite(e)&&error<=5e-5+2e-4*std::abs(double(e)))){row.practical++;return false;}
  return true;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2,"Usage: hair_state_gate OUTPUT_PREFIX");auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");
  Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=require_webgpu_device(owner.get(),"hair_state_gate device creation");
  std::map<unsigned,Row> rows;uint64_t failures=0,integer_failures=0;std::string math_report;
  {
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;
    Scene scene(params,&d);prepare(scene);for(auto shader:scene.shaders)shader->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
    Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Hair scene preparation failed");
    auto &data=scene.dscene.data;data.max_closures=MAX_CLOSURE;d.const_copy_to("data",&data,sizeof(data));
    bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);auto layout=fields(d);
    std::vector<IntegratorStateCPU> states(scheduler.capacity());auto ids=states_for(scene,states);
    for(int i:ids){auto &s=states[i];s.path.render_pixel_index=i%193;s.path.throughput=make_float3(.6,.8,.4);s.path.continuation_probability=1;s.path.flag=PATH_RAY_MIS_SKIP|PATH_RAY_TRANSPARENT_BACKGROUND;s.path.min_ray_pdf=FLT_MAX;s.ray.tmax=FLT_MAX;}
    std::vector<HairProbePath> paths(ids.size());std::mt19937 rng(20261107);std::uniform_real_distribution<float> unit(0.f,1.f);
    for(auto &path:paths)for(auto &r:path.rand)for(auto &x:r)x=unit(rng);
    // Corners of the random cube, for a few samples of every path.
    for(auto &path:paths){const float corners[][3]={{0,0,0},{.999999f,.999999f,.999999f},{.5f,.5f,.5f},{0,.999999f,.25f}};for(unsigned c=0;c<4;c++)for(unsigned k=0;k<3;k++)path.rand[c][k]=corners[c][k];}
    original_hair_probe(scene.dscene,ids,states,paths);
    upload(d,layout,states);
    device_vector<int> selected(&d,"hair_indices",MEM_READ_WRITE);selected.alloc(ids.size());std::copy(ids.begin(),ids.end(),selected.data());selected.copy_to_device();
    device_vector<float> table(&d,"hair_table",MEM_READ_WRITE);table.alloc(ids.size()*(hair_probe_directions+hair_probe_samples)*3);
    for(size_t n=0;n<ids.size();n++){float *at=table.data()+n*(hair_probe_directions+hair_probe_samples)*3;std::memcpy(at,paths[n].directions,sizeof(paths[n].directions));std::memcpy(at+hair_probe_directions*3,paths[n].rand,sizeof(paths[n].rand));}
    table.copy_to_device();
    device_vector<uint> eval_output(&d,"hair_eval_output",MEM_READ_WRITE);eval_output.alloc(ids.size()*hair_probe_directions*hair_probe_closures*4);eval_output.zero_to_device();
    device_vector<uint> sample_output(&d,"hair_sample_output",MEM_READ_WRITE);sample_output.alloc(ids.size()*hair_probe_samples*16);sample_output.zero_to_device();
    device_vector<uint> scratch(&d,"hair_closures",MEM_READ_WRITE);scratch.alloc(ids.size()*MAX_CLOSURE*sizeof(SurfaceClosureRecord)/4);scratch.zero_to_device();
    device_vector<uint> cache(&d,"surface_data_cache",MEM_READ_WRITE);cache.alloc(ids.size()*surface_data_cache_bytes/4);cache.zero_to_device();
    const std::string source=d.module_wgsl(surface_source())+probe_entries;std::ofstream(std::string(argv[1])+".wgsl")<<source;
    WGPUShaderSourceWGSL wgsl{};wgsl.chain.sType=WGPUSType_ShaderSourceWGSL;wgsl.code={source.data(),source.size()};
    WGPUShaderModuleDescriptor desc{};desc.nextInChain=&wgsl.chain;d.gpu.push_scope();
    cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> module(wgpuDeviceCreateShaderModule(d.gpu.get(),&desc));d.gpu.pop_scope();
    d.surface_geometry=d.gpu.pipeline(module,d.pipeline_layout,"prepare_shader_data");auto evaluate=d.gpu.pipeline(module,d.pipeline_layout,"hair_probe_evaluate");auto sample=d.gpu.pipeline(module,d.pipeline_layout,"hair_probe_sample");auto mis=d.gpu.pipeline(module,d.pipeline_layout,"hair_probe_mis");
    std::array<uint32_t,32> p{};p[0]=table.device_pointer;p[1]=ids.size();p[2]=selected.device_pointer;p[6]=d.constants.at("data");p[7]=d.constants.at("integrator_state");p[10]=states.size();p[14]=d.scene_tables();p[17]=d.error_word;
    p[21]=scene.dscene.svm_nodes.device_pointer;p[22]=scene.dscene.svm_nodes.size();p[23]=scratch.device_pointer;p[26]=cache.device_pointer;p[27]=surface_data_cache_bytes;
    // libm adapter census (diagnostic, reported, not a pass condition).
    {
      auto mathp=d.gpu.pipeline(module,d.pipeline_layout,"hair_probe_math");const unsigned count=1u<<18;std::mt19937 mrng(7);
      std::uniform_real_distribution<float> wide(-7.5f,7.5f),unitr(-1.f,1.f);
      device_vector<float> in(&d,"hair_math_in",MEM_READ_WRITE);in.alloc(count*2);for(unsigned i=0;i<count;i++){in[2*i]=i%4==0?wide(mrng)*.05f:wide(mrng);in[2*i+1]=unitr(mrng);}in.copy_to_device();
      device_vector<float> res(&d,"hair_math_out",MEM_READ_WRITE);res.alloc(count*7);res.zero_to_device();
      std::array<uint32_t,32> m{};m[0]=in.device_pointer;m[1]=count;m[4]=res.device_pointer;m[6]=p[6];m[7]=p[7];m[14]=p[14];m[17]=p[17];
      for(unsigned start=0;start<count;start+=8192){auto q=m;q[0]+=start*8;q[1]=std::min(8192u,count-start);q[4]+=start*28;d.dispatch(q,q[1],mathp);healthy(d);}
      res.copy_from_device();const char *names[]={"expf","logf","sinf","cosf","asinf","atan2f","tanf"};uint64_t diff[7]={},ulp2[7]={};
      for(unsigned i=0;i<count;i++){const float x=in[2*i],y=in[2*i+1];
        const float want[7]={std::exp(x),std::log(std::fabs(x)),std::sin(x),std::cos(x),std::asin(y),std::atan2(y,x),std::tan(x)};
        for(unsigned k=0;k<7;k++){const float a=res[i*7+k];if(std::bit_cast<uint>(a)!=std::bit_cast<uint>(want[k])&&!(std::isnan(a)&&std::isnan(want[k]))){diff[k]++;if(std::abs(int64_t(std::bit_cast<int>(a))-int64_t(std::bit_cast<int>(want[k])))>1)ulp2[k]++;}}}
      std::ostringstream s;s<<"{";for(unsigned k=0;k<7;k++)s<<(k?",":"")<<"\""<<names[k]<<"\":{\"differ\":"<<diff[k]<<",\"overOneUlp\":"<<ulp2[k]<<"}";s<<",\"inputs\":"<<count<<"}";math_report=s.str();std::cerr<<"libm census "<<math_report<<"\n";
    }
    std::cerr<<"Pipelines ready\n";const auto started=std::chrono::steady_clock::now();
    // Small dispatches: one long one trips the driver's hang check on a shared GPU.
    uint error=0;const size_t chunk=std::getenv("CODEBRIX_HAIR_GATE_CHUNK")?std::atoi(std::getenv("CODEBRIX_HAIR_GATE_CHUNK")):8;
    for(size_t start=0;start<ids.size();start+=chunk){const uint32_t n=uint32_t(std::min(chunk,ids.size()-start));auto q=p;
      q[0]=table.device_pointer+start*(hair_probe_directions+hair_probe_samples)*12;q[1]=n;q[2]=selected.device_pointer+start*4;q[23]=scratch.device_pointer+start*MAX_CLOSURE*sizeof(SurfaceClosureRecord);q[26]=cache.device_pointer+start*surface_data_cache_bytes;
      d.dispatch(q,n,d.surface_geometry,64);d.download(d.error_word,&error,4);healthy(d);
      q[4]=eval_output.device_pointer+start*hair_probe_directions*hair_probe_closures*16;d.dispatch(q,n,evaluate);d.download(d.error_word,&error,4);require(!error,"Hair evaluate probe error mask="+std::to_string(error));healthy(d);
      q[4]=sample_output.device_pointer+start*hair_probe_samples*64;d.dispatch(q,n,sample);d.download(d.error_word,&error,4);require(!error,"Hair sample probe error mask="+std::to_string(error));healthy(d);
      d.dispatch(q,n,mis);d.download(d.error_word,&error,4);require(!error,"Hair MIS probe error mask="+std::to_string(error));healthy(d);
    }
    std::cerr<<"Probes ran in "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<" s, "<<ids.size()/chunk<<" dispatches of each\n";
    eval_output.copy_from_device();sample_output.copy_from_device();healthy(d);
    const auto *evals=reinterpret_cast<const HairEvalRecord*>(eval_output.data());const auto *samples=reinterpret_cast<const HairSampleRecord*>(sample_output.data());
    std::ofstream lines(std::string(argv[1])+".mismatches.txt");
    auto report=[&](bool ok,const std::string &where){if(!ok){failures++;if(failures<=2000)lines<<where<<'\n';}};
    // Row: fixture graph, split by geometry (states_for: 5 triangle rays, then curves).
    for(size_t n=0;n<ids.size();n++){
      const unsigned per_object=GRAPH_COUNT*17;const unsigned graph=(n%per_object)/17,ray=n%17;const unsigned geometry=ray<5?0:1+ray%3;
      auto &row=rows[graph*10+geometry];auto &path=paths[n];row.paths++;const std::string tag="graph="+std::to_string(graph)+" geometry="+std::to_string(geometry)+" path="+std::to_string(n);
      for(unsigned j=0;j<hair_probe_directions;j++)for(unsigned k=0;k<path.closure_count;k++){
        auto &e=path.evals[j][k];auto &a=evals[n*hair_probe_directions*hair_probe_closures+j*hair_probe_closures+k];row.evals++;row.nonzero_evals+=e.pdf!=0;
        const std::string where=tag+" eval dir="+std::to_string(j)+" closure="+std::to_string(k)+" kind="+std::to_string(path.kinds[k]);
        for(unsigned c=0;c<3;c++)report(judge(a.value[c],e.value[c],row,where+" value"+std::to_string(c)),where+" value"+std::to_string(c)+" actual="+std::to_string(a.value[c])+" expected="+std::to_string(e.value[c]));
        report(judge(a.pdf,e.pdf,row,where+" pdf"),where+" pdf actual="+std::to_string(a.pdf)+" expected="+std::to_string(e.pdf));
      }
      for(unsigned j=0;j<hair_probe_samples;j++){
        auto &e=path.samples[j];auto &a=samples[n*hair_probe_samples+j];row.samples++;row.sampled+=e.pdf!=0;const std::string where=tag+" sample="+std::to_string(j);
        row.labels++;row.label_counts[e.label]++;
        if(a.label!=e.label){integer_failures++;report(false,where+" label actual="+std::to_string(a.label)+" expected="+std::to_string(e.label));}
        report(judge(a.pdf,e.pdf,row,where+" pdf"),where+" pdf actual="+std::to_string(a.pdf)+" expected="+std::to_string(e.pdf));
        if(e.pdf==0)continue;
        if(a.selected!=e.selected){integer_failures++;report(false,where+" selected actual="+std::to_string(a.selected)+" expected="+std::to_string(e.selected));}
        for(unsigned c=0;c<3;c++){
          report(judge(a.wo[c],e.wo[c],row,where+" wo"),where+" wo"+std::to_string(c)+" actual="+std::to_string(a.wo[c])+" expected="+std::to_string(e.wo[c]));
          report(judge(a.eval[c],e.eval[c],row,where+" eval"),where+" eval"+std::to_string(c)+" actual="+std::to_string(a.eval[c])+" expected="+std::to_string(e.eval[c]));
          report(judge(a.diffuse[c],e.diffuse[c],row,where+" diffuse"),where+" diffuse"+std::to_string(c)+" actual="+std::to_string(a.diffuse[c])+" expected="+std::to_string(e.diffuse[c]));
          report(judge(a.glossy[c],e.glossy[c],row,where+" glossy"),where+" glossy"+std::to_string(c)+" actual="+std::to_string(a.glossy[c])+" expected="+std::to_string(e.glossy[c]));
        }
        report(judge(a.roughness,e.roughness,row,where+" roughness"),where+" roughness actual="+std::to_string(a.roughness)+" expected="+std::to_string(e.roughness));
      }
    }
  }
  require(stats.mem_used==0,"Hair Cycles allocation leak");
  const char *geometry_names[]={"triangle","ribbon","thick","linear"};
  std::cout<<std::setprecision(9)<<"{\"libmCensus\":"<<math_report<<",\"passed\":"<<((failures||integer_failures)?"false":"true")<<",\"practicalFailures\":"<<failures<<",\"integerFailures\":"<<integer_failures<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<",\"rows\":[";
  bool first=true;for(auto &[key,row]:rows){if(!first)std::cout<<',';first=false;
    std::cout<<"\n{\"graph\":"<<key/10<<",\"geometry\":\""<<geometry_names[key%10]<<"\",\"paths\":"<<row.paths<<",\"evals\":"<<row.evals<<",\"nonzeroEvals\":"<<row.nonzero_evals<<",\"samples\":"<<row.samples<<",\"sampledWithDensity\":"<<row.sampled
      <<",\"floatChecks\":"<<row.float_checks<<",\"bitDifferences\":"<<row.bits<<",\"strict\":"<<row.strict<<",\"practical\":"<<row.practical<<",\"maximum\":"<<row.maximum<<",\"worst\":\""<<row.worst<<"\"}";}
  std::cout<<"]}\n";
  if(failures||integer_failures)status=1;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
