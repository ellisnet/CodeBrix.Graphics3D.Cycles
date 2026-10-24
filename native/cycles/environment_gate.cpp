// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Environment lighting (background light importance sampling and MIS) and
// non-constant emitters (textured/procedural triangle lights, SHADE_LIGHT_NEE):
// importance-map evaluation and light-sample state checks against the original
// CPU kernels, then native-scheduler images against independent original CPU
// images (practical criterion; strict differences reported), with command capture.
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
#include "integrator/shader_eval.h"
#include "scene/camera.h"
#include "scene/film.h"
#include "scene/integrator.h"
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
using namespace wavefront_test;
constexpr unsigned width=32,height=24,samples=8;
struct Case {const char *name;EnvironmentWorld world;unsigned image;int light;int resolution;int emitter;bool point;};
// light: -1 none, 1 background light with MIS. Cycles enables the background
// light only for a spatially varying world with MIS (LightManager::test_enabled_lights),
// so constant worlds and MIS-off background lights do not reach the kernels.
// Resolutions are explicit: with resolution 0 and no sky sun, the pinned
// LightManager picks 4096 x 2048 (8.4 M evaluations), whose evaluation buffers
// exceed one storage binding of the arena (refused with an allocation error).
static const Case cases[]={
  {"procedural-mis",EnvironmentWorld::Procedural,0,1,64,-1,false},
  {"hdri-mis-res128",EnvironmentWorld::Hdri,0,1,128,-1,false},
  {"procedural-forward",EnvironmentWorld::Procedural,0,-1,0,-1,false},
  {"hdri-mis",EnvironmentWorld::Hdri,0,1,64,-1,false},
  {"hdri-mis-point",EnvironmentWorld::Hdri,1,1,32,-1,true},
  {"hdri-forward",EnvironmentWorld::Hdri,0,-1,0,-1,false},
  {"emitter-image",EnvironmentWorld::Constant,0,-1,0,0,false},
  {"emitter-procedural",EnvironmentWorld::Constant,0,-1,0,1,false},
  {"emitter-mixed-hdri-mis",EnvironmentWorld::Hdri,0,1,256,2,false},
  {"emitter-mixed",EnvironmentWorld::Constant,0,-1,0,2,false},
  {"hdri-mis-res256",EnvironmentWorld::Hdri,0,1,256,-1,false},
  {"hdri-sun-mis-res512",EnvironmentWorld::Hdri,3,1,512,-1,false},
};
constexpr unsigned case_count=sizeof(cases)/sizeof(cases[0]);
struct Metrics {uint64_t floats=0,strict=0,repeat_differences=0,nee=0,shadows=0,visible=0,background_hits=0,stages=0,map_checks=0,map_strict=0,sample_checks=0,sample_strict=0,valid_samples=0;
  double maximum=0,repeat_maximum=0,map_maximum=0,sample_maximum=0;};
static Shader *surface_material(Scene &scene,int variant){
  auto graph=make_unique<ShaderGraph>();auto diffuse=graph->create_node<DiffuseBsdfNode>();diffuse->set_color(make_float3(.55,.42,.3));diffuse->set_roughness(.2f);
  ShaderOutput *surface=diffuse->output("BSDF");
  if(variant){auto glossy=graph->create_node<GlossyBsdfNode>();glossy->set_distribution(CLOSURE_BSDF_MICROFACET_GGX_ID);glossy->set_color(make_float3(.3,.55,.8));glossy->set_roughness(variant==2?.12f:.4f);
    auto mix=graph->create_node<MixClosureNode>();mix->set_fac(variant==2?.8f:.35f);graph->connect(surface,mix->input("Closure1"));graph->connect(glossy->output("BSDF"),mix->input("Closure2"));surface=mix->output("Closure");}
  graph->connect(surface,graph->output()->input("Surface"));auto shader=scene.create_node<Shader>();shader->set_graph(std::move(graph));shader->tag_update(&scene);return shader;
}
static Object *quad(Scene &scene,Shader *shader,const std::array<float3,4> &v,bool uv=false){
  auto mesh=scene.create_node<Mesh>();array<Node*> slots;slots.push_back_slow(shader);mesh->set_used_shaders(slots);mesh->resize_mesh(4,2);
  std::copy(v.begin(),v.end(),mesh->get_position_for_write());const int indices[]={0,1,2,0,2,3};std::copy_n(indices,6,mesh->get_triangles().data());
  std::ranges::fill(mesh->get_smooth(),false);std::ranges::fill(mesh->get_shader(),0);mesh->tag_triangles_modified();
  if(uv){auto data=mesh->attributes.add(ATTR_STD_UV)->data_for_write<float2>();const float2 c[]={make_float2(-.2f,-.1f),make_float2(1.3f,-.1f),make_float2(1.3f,1.2f),make_float2(-.2f,1.2f)};
    for(int t=0;t<6;t++)data[t]=c[indices[t]];}
  auto object=scene.create_node<Object>();object->set_geometry(mesh);return object;
}
static void fixture(Scene &scene,const Case &c){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);
  scene.integrator->set_aa_samples(samples);scene.integrator->set_min_bounce(2);scene.integrator->set_max_bounce(3);scene.integrator->set_filter_glossy(0);
  scene.film->set_use_sample_count(false);scene.camera->set_full_width(width);scene.camera->set_full_height(height);scene.camera->set_screen_size(width,height);
  scene.camera->set_fov(.9f);scene.camera->set_matrix(transform_translate(make_float3(0,.4f,-7))*transform_rotate(-.12f,make_float3(1,0,0)));scene.camera->compute_auto_viewplane();
  auto floor=surface_material(scene,1),center=surface_material(scene,0),mirror=surface_material(scene,2);
  quad(scene,floor,{make_float3(-5,-1.5f,-3),make_float3(5,-1.5f,-3),make_float3(5,-1.5f,5),make_float3(-5,-1.5f,5)});
  quad(scene,center,{make_float3(-1,-1.5f,0),make_float3(1,-1.5f,0),make_float3(1,.6f,.3f),make_float3(-1,.6f,.3f)});
  quad(scene,mirror,{make_float3(1.4f,-1.5f,-.5f),make_float3(2.6f,-1.5f,.6f),make_float3(2.6f,.2f,.6f),make_float3(1.4f,.2f,-.5f)});
  Shader *world=environment_world(scene,c.world,c.image);
  if(c.emitter>=0){
    // Dim constant world so the emitter dominates; emitters keep their sampling method.
    if(c.world==EnvironmentWorld::Constant){auto g=make_unique<ShaderGraph>();auto bg=g->create_node<BackgroundNode>();bg->set_color(make_float3(.02f,.025f,.03f));
      g->connect(bg->output("Background"),g->output()->input("Surface"));world->set_graph(std::move(g));world->tag_update(&scene);}
  }
  for(auto shader:scene.shaders)shader->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
  if(c.emitter>=0){auto emitter=environment_emitter(scene,unsigned(c.emitter));
    auto panel=quad(scene,emitter,{make_float3(-.9f,-.5f,0),make_float3(.9f,-.5f,0),make_float3(.9f,.5f,0),make_float3(-.9f,.5f,0)},true);
    panel->set_tfm(transform_translate(make_float3(-1.6f,1.7f,-.8f))*transform_rotate(.25f,make_float3(0,1,0))*transform_rotate(1.1f,make_float3(1,0,0)));
    emitter->set_emission_sampling_method(EMISSION_SAMPLING_FRONT_BACK);}
  if(c.light>=0)environment_light(scene,world,c.light==1,c.resolution);
  if(c.point){auto emission_graph=make_unique<ShaderGraph>();auto emission=emission_graph->create_node<EmissionNode>();emission->set_color(one_float3());emission->set_strength(1);
    emission_graph->connect(emission->output("Emission"),emission_graph->output()->input("Surface"));auto emitter=scene.create_node<Shader>();emitter->set_graph(std::move(emission_graph));
    emitter->set_emission_sampling_method(EMISSION_SAMPLING_NONE);emitter->tag_update(&scene);
    auto point=scene.create_node<PointLight>();point->set_radius(.35f);point->set_is_sphere(true);point->set_use_mis(true);point->set_strength(make_float3(60,50,40));
    array<Node*> slots;slots.push_back_slow(emitter);point->set_used_shaders(slots);auto object=scene.create_node<Object>();object->set_geometry(point);object->set_tfm(transform_translate(make_float3(2,2.2f,-1.5f)));}
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
      require(step<99,"CPU environment paths did not terminate");
      for(auto &[kernel,ids]:queues){
        if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST)original_closest(scene,ids,state,film);
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND){metrics.background_hits+=ids.size();original_background(scene,ids,state,film);}
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
        }else throw std::runtime_error("Unexpected CPU environment kernel");
      }
    }
  }
}
static void compare(double actual,double expected,uint64_t &checks,uint64_t &strict,double &maximum,const std::string &where){
  const double error=std::abs(actual-expected);checks++;maximum=std::max(maximum,error);strict+=error>1e-7+1e-7*std::abs(expected);
  require(std::isfinite(actual)&&std::isfinite(expected)&&error<=5e-5+2e-4*std::abs(expected),where+" actual="+std::to_string(actual)+" expected="+std::to_string(expected));
}
// Importance-map pixels: the device's shader_eval_background against kernel_background_evaluate.
// Importance-map inputs exactly as LightManager's shade_background_pixels builds them.
static std::vector<KernelShaderEvalInput> map_inputs(const DeviceScene &scene){
  const auto &bg=scene.data.background;std::vector<KernelShaderEvalInput> inputs;if(bg.map_res_x<=0||bg.map_res_y<=0)return inputs;
  const int w=bg.map_res_x,h=bg.map_res_y;inputs.resize(size_t(w)*h);
  for(int y=0;y<h;y++)for(int x=0;x<w;x++){KernelShaderEvalInput in;in.object=w;in.prim=h;in.u=(x+.5f)/w;in.v=(y+.5f)/h;inputs[size_t(y)*w+x]=in;}
  return inputs;
}
// CPU references (reference_policy.h keys case-N.map / case-N.samples): local, exported or imported.
static std::vector<float> map_reference(Scene &scene,const std::string &key,const std::filesystem::path &out){
  const auto local=original_background_evaluate(scene.dscene,map_inputs(scene.dscene));reference_policy::record_local(key+".map",local);
  return reference_policy::resolve(key+".map",local,(out/"reference-diagnostic.jsonl").string());
}
static std::vector<float> sample_reference(Scene &scene,const std::vector<IntegratorStateCPU> &states,const std::string &key,const std::filesystem::path &out){
  const auto samples=original_light_samples(scene.dscene,states),evals=original_light_sample_evals(scene.dscene,states);
  std::vector<float> local(states.size()*16);
  for(size_t i=0;i<states.size();i++){std::copy_n(samples.data()+i*12,12,local.data()+i*16);std::copy_n(evals.data()+i*4+1,3,local.data()+i*16+12);local[i*16+15]=evals[i*4];}
  reference_policy::record_local(key+".samples",local);return reference_policy::resolve(key+".samples",local,(out/"reference-diagnostic.jsonl").string());
}
static void map_check(WebGPUDevice &d,Scene &scene,const std::filesystem::path &folder,const std::vector<float> &expected,Metrics &m){
  const auto inputs=map_inputs(scene.dscene);if(inputs.empty())return;std::vector<float> actual;
  Progress progress;ShaderEval eval(&d,progress);
  eval.eval(SHADER_EVAL_BACKGROUND,int(inputs.size()),3,[&](device_vector<KernelShaderEvalInput> &in){std::copy(inputs.begin(),inputs.end(),in.data());return int(inputs.size());},
    [&](device_vector<float> &out){actual.assign(out.data(),out.data()+out.size());});
  healthy(d);require(actual.size()==expected.size(),"Importance map evaluation size");
  RenderCapture::file((folder/"map.gpu.bin").string(),actual.data(),actual.size()*4);RenderCapture::file((folder/"map.cpu.bin").string(),expected.data(),expected.size()*4);
  for(size_t i=0;i<actual.size();i++)compare(actual[i],expected[i],m.map_checks,m.map_strict,m.map_maximum,"Importance map pixel component="+std::to_string(i));
}
// Light samples (direction, PDF, evaluation factor) and the sampled light's shader
// evaluation for the same random numbers.
// Probe path states. The directions carry this host's float3 normalization, so they
// travel with the exchanged scene (PathInput) like the emitter probe's.
constexpr unsigned sample_count=2048;
struct PathInput {uint32_t sample,rng_pixel,rng_offset,bounce,flag;float time,P[3],D[3];};
static std::vector<PathInput> sample_inputs(){
  std::vector<PathInput> inputs(sample_count);
  for(unsigned i=0;i<sample_count;i++){const float3 P=make_float3(float(i%13)*.4f-2.4f,-1.4f+float((i/13)%7)*.3f,float(i%5)-1.f);
    const float3 D=normalize(make_float3(.2f*(int(i%5)-2),i%9?1.f:-1.f,.15f*(int(i%7)-3)));
    inputs[i]={i%64,i*2654435761u,32*(i%4),i%3,(i%2)?uint32_t(PATH_RAY_MIS_HAD_TRANSMISSION):0u,.5f,{P.x,P.y,P.z},{D.x,D.y,D.z}};}
  return inputs;
}
static std::vector<IntegratorStateCPU> sample_states(const std::vector<PathInput> &inputs){
  std::vector<IntegratorStateCPU> states(inputs.size());
  for(size_t i=0;i<inputs.size();i++){auto &s=states[i];const auto &in=inputs[i];s.path.sample=in.sample;s.path.rng_pixel=in.rng_pixel;s.path.rng_offset=in.rng_offset;
    s.path.bounce=in.bounce;s.path.flag=in.flag;s.ray.time=in.time;s.ray.P=make_float3(in.P[0],in.P[1],in.P[2]);s.ray.D=make_float3(in.D[0],in.D[1],in.D[2]);}
  return states;
}
static void sample_check(WebGPUDevice &d,Scene &scene,const std::filesystem::path &folder,const std::vector<IntegratorStateCPU> &states,const std::vector<float> &expected,Metrics &m){
  if(!scene.dscene.data.integrator.use_direct_light)return;
  bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);const auto layout=fields(d);
  const unsigned count=sample_count;require(states.size()==count,"Environment probe input count");
  upload(d,layout,states);
  // Records: 16 floats per sample, then 13 words per sample of the raw light sample
  // (light_sample_write) for the evaluation entry. Sampling and the light shader
  // evaluation are separate entries: one entry with both made the HP's Mesa 24.0.8
  // compiler terminate (emissive triangle + background light + node emission).
  device_vector<float> output(&d,"environment_samples",MEM_READ_WRITE);output.alloc(size_t(count)*(16+13)+16);std::fill_n(output.data(),output.size(),-1234.f);output.copy_to_device();
  const bool nee=(d.shader_features()&SF_LIGHT_NEE).any();
  std::string source=d.module_wgsl(surface_source())+R"WGSL(
@compute @workgroup_size(1) fn environment_sample_probe(@builtin(global_invocation_id) id:vec3<u32>){
 svm_invocation(id.x);if(id.x>=p(1u)){return;}var sd:SurfaceData;sd.time=heap_float(state_address(S_ray_time,id.x,4u));sd.P=heap_vec3(state_address(S_ray_P,id.x,12u));sd.N=heap_vec3(state_address(S_ray_D,id.x,12u));var r:SvmResult;
 if((state_word(S_path_flag,id.x)&PATH_RAY_MIS_HAD_TRANSMISSION)!=0u){r.runtime_flag=SR_BSDF_HAS_TRANSMISSION;}
 triangle_light_prefill(id.x,sd,false,true);let ls=light_sample_native(id.x,sd,r);let at=p(4u)+id.x*64u;let valid=ls.pdf>0.0;store_word(at,bitcast<u32>(select(0.0,1.0,valid)));
 light_sample_write(p(5u)+id.x*52u,ls);
 if(!valid){return;}
 let values=array<f32,11>(ls.P.x,ls.P.y,ls.P.z,ls.D.x,ls.D.y,ls.D.z,ls.t,ls.pdf,ls.eval_fac,f32(ls.prim),f32(ls.object_id));
 for(var j=0u;j<11u;j++){store_word(at+4u+j*4u,bitcast<u32>(values[j]));}
}
@compute @workgroup_size(1) fn environment_eval_probe(@builtin(global_invocation_id) id:vec3<u32>){
 svm_invocation(id.x);if(id.x>=p(1u)){return;}let at=p(4u)+id.x*64u;if(heap_float(at)!=1.0){return;}
 let ls=light_sample_read(p(5u)+id.x*52u);let P=heap_vec3(state_address(S_ray_P,id.x,12u));let time=heap_float(state_address(S_ray_time,id.x,4u));
 let shader=table_at(G_shaders,ls.shader&SHADER_MASK,SIZE_shaders);var e=vec3<f32>(0);
 if((load_word(shader+SH_flags)&SD_HAS_CONSTANT_EMISSION)!=0u){e=heap_vec3(shader+SH_constant_emission);}
 else{PROBE_NEE}
 if(ls.is_light){e*=heap_vec3(table_at(G_lights,ls.prim,SIZE_lights)+L_strength);}
 store_word(at+48u,bitcast<u32>(e.x));store_word(at+52u,bitcast<u32>(e.y));store_word(at+56u,bitcast<u32>(e.z));
})WGSL";
  const std::string probe=nee?"let v=light_nee_eval(ls.object_id,ls.prim,P,ls.D,ls.t,time,0.0,0.0);e=v.xyz;":"atomicOr(&heap[p(17u)/4u],2u);";
  source.replace(source.find("PROBE_NEE"),9,probe);
  std::ofstream(folder/"probe.wgsl")<<source;
  WGPUShaderSourceWGSL code{};code.chain.sType=WGPUSType_ShaderSourceWGSL;code.code={source.data(),source.size()};WGPUShaderModuleDescriptor descriptor{};descriptor.nextInChain=&code.chain;
  d.gpu.push_scope();cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> module(wgpuDeviceCreateShaderModule(d.gpu.get(),&descriptor));d.gpu.pop_scope();
  auto sampler=d.gpu.pipeline(module,d.pipeline_layout,"environment_sample_probe");
  std::array<uint32_t,32> p{};p[1]=count;p[4]=output.device_pointer+32;p[5]=output.device_pointer+32+uint32_t(count)*64;p[6]=d.constants.at("data");p[7]=d.constants.at("integrator_state");p[9]=scene.dscene.sample_pattern_lut.device_pointer;
  p[14]=d.scene_tables();p[17]=d.error_word;p[18]=d.globals.at("shaders");p[19]=uint32_t(d.remaining(p[18])/sizeof(KernelShader));
  if(auto it=d.globals.find("svm_nodes");it!=d.globals.end()){p[21]=it->second;p[22]=uint32_t(d.remaining(p[21])/4);}
  d.dispatch(p,count,sampler,1);d.sync();
  {auto evaluator=d.gpu.pipeline(module,d.pipeline_layout,"environment_eval_probe");d.dispatch(p,count,evaluator,1);d.sync();}
  output.copy_from_device();healthy(d);uint error=0;d.download(d.error_word,&error,4);require(error==0,"Environment probe shader error "+std::to_string(error));
  RenderCapture::file((folder/"samples.gpu.bin").string(),output.data()+8,size_t(count)*16*4);
  for(unsigned i=0;i<count;i++){const float *g=output.data()+8+i*16;const float *c=expected.data()+i*16;
    require(g[0]==c[0]&&c[0]==c[15],"Environment sample validity differs index="+std::to_string(i));if(!c[0])continue;m.valid_samples++;
    for(unsigned j=1;j<15;j++)compare(g[j],c[j],m.sample_checks,m.sample_strict,m.sample_maximum,"Environment sample="+std::to_string(i)+" field="+std::to_string(j));}
  for(unsigned i=0;i<8;i++)require(output[i]==-1234.f&&output[output.size()-1-i]==-1234.f,"Environment probe guard overwritten");
  require(m.valid_samples>100,"Insufficient valid environment samples");
}
// Optional diagnostic (CODEBRIX_ENVIRONMENT_TRIG_PROBE=FILE): the WGSL sine/cosine,
// arccosine and arctangent helpers of the environment light on a fixed input grid,
// written as float32 records (angle, sin, cos, acos input, acos, y, x, atan2).
static void trig_probe(WebGPUDevice &d,Scene &scene,const char *file){
  const unsigned n=1<<16;device_vector<float> buffer(&d,"environment_trig_probe",MEM_READ_WRITE);buffer.alloc(size_t(n)*8);
  for(unsigned i=0;i<n;i++){float *r=buffer.data()+i*8;const float t=float(i)/n;r[0]=-3.14159265f+t*9.42477796f;r[3]=-1.f+2.f*t;
    r[5]=std::sin(float(i)*.7f)*(1.f+t);r[6]=std::cos(float(i)*1.3f)*(1.f+t);}
  buffer.copy_to_device();
  const std::string source=d.module_wgsl(surface_source())+R"WGSL(
@compute @workgroup_size(64) fn environment_trig_probe(@builtin(global_invocation_id) id:vec3<u32>){
 if(id.x>=p(1u)){return;}let at=p(4u)+id.x*32u;let a0=heap_float(at);var sc=trig_sincos_wide(a0);if(abs(a0)<=3.14159265){sc=native_disk_sincos(a0);}store_word(at+4u,bitcast<u32>(sc.x));store_word(at+8u,bitcast<u32>(sc.y));
 store_word(at+16u,bitcast<u32>(trig_acos(heap_float(at+12u))));store_word(at+28u,bitcast<u32>(trig_atan2(heap_float(at+20u),heap_float(at+24u))));
})WGSL";
  WGPUShaderSourceWGSL code{};code.chain.sType=WGPUSType_ShaderSourceWGSL;code.code={source.data(),source.size()};WGPUShaderModuleDescriptor descriptor{};descriptor.nextInChain=&code.chain;
  d.gpu.push_scope();cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> module(wgpuDeviceCreateShaderModule(d.gpu.get(),&descriptor));d.gpu.pop_scope();
  auto pipeline=d.gpu.pipeline(module,d.pipeline_layout,"environment_trig_probe");
  std::array<uint32_t,32> p{};p[1]=n;p[4]=buffer.device_pointer;p[6]=d.constants.at("data");p[14]=d.scene_tables();p[17]=d.error_word;
  d.dispatch(p,n,pipeline,64);d.sync();buffer.copy_from_device();healthy(d);RenderCapture::file(file,buffer.data(),size_t(n)*32);(void)scene;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  reference_policy::use_reference_oracle_arithmetic();
  require(argc>=2,"Usage: environment_gate OUTPUT_DIRECTORY [CASE...]");
  const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
  std::vector<unsigned> selected;for(int a=2;a<argc;a++)selected.push_back(unsigned(std::stoul(argv[a])));
  if(selected.empty())for(unsigned i=0;i<case_count;i++)selected.push_back(i);
  auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");Stats stats;Profiler profiler;
  auto owner=Device::create(infos[0],stats,profiler,true);auto &device=require_webgpu_device(owner.get(),"environment_gate device creation");Metrics total;
  std::ostringstream case_reports;
  for(unsigned image:selected){
    require(image<case_count,"Unknown environment case");const Case &c=cases[image];
    std::cerr<<"environment case="<<image<<" "<<c.name<<'\n';auto folder=out/("case-"+std::to_string(image));std::filesystem::create_directories(folder);const auto prefix=(folder/"run").string();Metrics metrics;
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;
    Scene scene(params,&device);fixture(scene,c);Progress progress;scene.device_update(&device,progress);healthy(device);require(!progress.get_error(),"Environment scene preparation failed");
    const auto &data=scene.dscene.data;
    std::cerr<<"native lights="<<data.integrator.num_lights<<" direct="<<data.integrator.use_direct_light<<" mis="<<data.integrator.use_light_mis<<" background_mis="<<data.background.use_mis
             <<" map="<<data.background.map_res_x<<"x"<<data.background.map_res_y<<" triangles="<<data.integrator.distribution_pdf_triangles<<"\n";
    require(bool(data.background.use_mis)==(c.light==1),"Unexpected native background MIS configuration");
    require(c.light<0||(data.background.map_res_x>0&&data.background.map_res_y>0),"Missing native importance map");
    const std::string key="case-"+std::to_string(image);
    // Scene exchange (reference_policy.h): the device-prepared tables (including the
    // device-built importance map) and the probe path states.
    auto inputs=sample_inputs();std::vector<reference_policy::Input> exchanged;
    if(c.light>=0)exchanged.push_back({"paths",inputs.data(),inputs.size()*sizeof(PathInput)});
    const bool exchange_only=reference_policy::exchange_scene(scene.dscene,key,exchanged);
    const auto states=sample_states(inputs);
    std::vector<float> map_expected,sample_expected;
    if(c.light>=0){map_expected=map_reference(scene,key,out);sample_expected=sample_reference(scene,states,key,out);}
    if(exchange_only){std::vector<float> local(width*height*4);cpu_render(scene.dscene,local,metrics);
      RenderCapture::file(prefix+".cpu.film.bin",local.data(),local.size()*4);reference_policy::record_local(key+".film",local);
      reference_policy::resolve(key+".film",local,(out/"reference-diagnostic.jsonl").string());continue;}
    if(const char *probe=std::getenv("CODEBRIX_ENVIRONMENT_TRIG_PROBE")){trig_probe(device,scene,probe);return 0;}
    if(c.light>=0)map_check(device,scene,folder,map_expected,metrics);
    if(c.light>=0)sample_check(device,scene,folder,states,sample_expected,metrics);
    require(data.film.pass_stride==4,"Environment combined-only film expected");bool cancelled=false;Scheduler scheduler(&device,scene.film,&scene.dscene,&cancelled);
    scheduler.alloc_work_memory();scheduler.init_execution();healthy(device);auto buffers=scheduler.get_render_buffers();BufferParams bp;bp.width=width;bp.height=height;bp.pass_stride=4;buffers->reset(bp);
    std::vector<float> local(width*height*4),first;cpu_render(scene.dscene,local,metrics);reference_policy::record_local(key+".film",local);
    RenderCapture::file(prefix+".cpu.film.bin",local.data(),local.size()*4);
    const auto reference=reference_policy::resolve(key+".film",local,(out/"reference-diagnostic.jsonl").string());
    for(unsigned repeat=0;repeat<2;repeat++){
      buffers->zero();std::unique_ptr<RenderCapture> capture;if(repeat==1)capture=std::make_unique<RenderCapture>(device,prefix);
      for(unsigned sample=0;sample<samples;sample++){
        scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Environment reset failed");KernelWorkTile tile{};tile.w=width;tile.h=height;tile.stride=width;tile.num_samples=1;tile.start_sample=sample;
        scheduler.enqueue_work_tiles(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA,&tile,1,0,0);require(scheduler.update_queue_counter_and_cache(),"Environment camera failed");
        for(unsigned step=0;step<200;step++){const auto kernel=scheduler.get_most_queued_kernel();if(kernel==DEVICE_KERNEL_NUM)break;require(step<199,"GPU environment paths did not terminate");
          require(scheduler.enqueue_path_iteration(),"Native environment scheduler stopped with pending paths");require(scheduler.update_queue_counter_and_cache(),"Environment stage failed: "+device.error_message());metrics.stages++;}
      }
      if(capture)capture->finish(prefix,buffers->buffer.device_pointer,buffers->buffer.memory_size(),uintptr_t(device.state.path.queued_kernel),scheduler.capacity(),width,height,samples);
      buffers->buffer.copy_from_device();healthy(device);std::vector<float> actual(buffers->buffer.data(),buffers->buffer.data()+buffers->buffer.size());
      RenderCapture::file(prefix+(repeat?".repeat.film.bin":".gpu.film.bin"),actual.data(),actual.size()*4);
      for(size_t i=0;i<actual.size();i++){double error=std::abs(double(actual[i])-reference[i]);metrics.maximum=std::max(metrics.maximum,error);metrics.floats++;
        metrics.strict+=error>1e-7+1e-7*std::abs(double(reference[i]));
        require(std::isfinite(actual[i])&&std::isfinite(reference[i])&&error<=5e-5+2e-4*std::abs(double(reference[i])),"Environment film mismatch case="+std::to_string(image)+" component="+std::to_string(i)+" actual="+std::to_string(actual[i])+" expected="+std::to_string(reference[i]));
        if(repeat){metrics.repeat_differences+=std::memcmp(&actual[i],&first[i],4)!=0;metrics.repeat_maximum=std::max(metrics.repeat_maximum,std::abs(double(actual[i])-first[i]));}}
      if(!repeat)first=std::move(actual);
    }
    if(c.light>=0||c.emitter>=0)require(metrics.shadows>100&&metrics.visible>50,"Missing environment direct lighting coverage");
    if((c.light>=0&&c.world!=EnvironmentWorld::Constant)||c.emitter>=0)require(metrics.nee>100,"Missing SHADE_LIGHT_NEE coverage");
    auto report=[&](std::ostream &stream){stream<<std::setprecision(17)<<"{\"passed\":true,\"case\":"<<image<<",\"name\":\""<<c.name<<"\",\"backgroundMis\":"<<data.background.use_mis
      <<",\"mapResolution\":["<<data.background.map_res_x<<','<<data.background.map_res_y<<"],\"mapChecks\":"<<metrics.map_checks<<",\"mapStrictDifferences\":"<<metrics.map_strict<<",\"mapMaximumAbsoluteError\":"<<metrics.map_maximum
      <<",\"validSamples\":"<<metrics.valid_samples<<",\"sampleChecks\":"<<metrics.sample_checks<<",\"sampleStrictDifferences\":"<<metrics.sample_strict<<",\"sampleMaximumAbsoluteError\":"<<metrics.sample_maximum
      <<",\"lightNeePaths\":"<<metrics.nee<<",\"shadows\":"<<metrics.shadows<<",\"visible\":"<<metrics.visible<<",\"backgroundHits\":"<<metrics.background_hits<<",\"stages\":"<<metrics.stages
      <<",\"filmChecks\":"<<metrics.floats<<",\"strictDifferences\":"<<metrics.strict<<",\"maximumAbsoluteError\":"<<metrics.maximum<<",\"repeatBitwiseDifferences\":"<<metrics.repeat_differences<<",\"repeatMaximumError\":"<<metrics.repeat_maximum<<"}";};
    std::ofstream details(folder/"result.json");report(details);details<<'\n';report(std::cerr);std::cerr<<'\n';if(case_reports.tellp()>0)case_reports<<',';report(case_reports);
    total.floats+=metrics.floats;total.strict+=metrics.strict;total.maximum=std::max(total.maximum,metrics.maximum);total.repeat_differences+=metrics.repeat_differences;
    total.nee+=metrics.nee;total.map_checks+=metrics.map_checks;total.map_strict+=metrics.map_strict;total.map_maximum=std::max(total.map_maximum,metrics.map_maximum);
    total.sample_checks+=metrics.sample_checks;total.sample_strict+=metrics.sample_strict;total.sample_maximum=std::max(total.sample_maximum,metrics.sample_maximum);
  }
  require(stats.mem_used==0,"Environment test leaked native allocations");std::ofstream(out/"library.txt")<<device.gpu.library_path;std::ofstream(out/"adapter.txt")<<device.gpu.adapter_name;
  const bool exchange_only=reference_policy::prepare_only()||reference_policy::reference_only();
  if(exchange_only)std::cout<<"{\"passed\":true,\"sceneExchangeOnly\":\""<<(reference_policy::prepare_only()?"dump":"reference")<<"\",\"cases\":"<<selected.size()<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
  else std::cout<<std::setprecision(17)<<"{\"passed\":true,\"nativeSceneAndScheduler\":true,\"independentOriginalCpuImage\":true,\"cases\":"<<selected.size()<<",\"lightNeePaths\":"<<total.nee
    <<",\"mapChecks\":"<<total.map_checks<<",\"mapStrictDifferences\":"<<total.map_strict<<",\"mapMaximumAbsoluteError\":"<<total.map_maximum
    <<",\"sampleChecks\":"<<total.sample_checks<<",\"sampleStrictDifferences\":"<<total.sample_strict<<",\"sampleMaximumAbsoluteError\":"<<total.sample_maximum
    <<",\"filmChecks\":"<<total.floats<<",\"strictDifferences\":"<<total.strict<<",\"maximumAbsoluteError\":"<<total.maximum<<",\"repeatBitwiseDifferences\":"<<total.repeat_differences
    <<",\"caseReports\":["<<case_reports.str()<<"],\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<",\"pipelineReport\":"<<ccl::pipeline_process_report_json()<<"}\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
