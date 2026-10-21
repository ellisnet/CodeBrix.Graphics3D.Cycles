// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "wavefront_test.h"
#include "closest_reference.h"
#include "background_reference.h"
#include "light_reference.h"
#include "wavefront.h"
#include "reference_policy.h"
#include "scene/scene.h"
#include "scene/background.h"
#include "scene/camera.h"
#include "scene/integrator.h"
#include "scene/light.h"
#include "scene/mesh.h"
#ifdef CODEBRIX_TRIANGLE_LIGHT_GATE
#include "object_motion_fixture.h"
#include "deforming_motion_fixture.h"
#endif
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
#include <numeric>
using namespace ccl;
using namespace wavefront_test;
struct Metrics {uint64_t cases=0,guards=0,paths=0,lamp_hits=0,continued=0,terminated=0,exact=0,numeric=0,film=0,strict=0;double maximum=0;};
static void numeric(float actual,float expected,Metrics &m,const std::string &where){
  const double error=std::abs(double(actual)-expected);m.numeric++;m.maximum=std::max(m.maximum,error);m.strict+=error>1e-7+1e-7*std::abs(double(expected));
  require(std::isfinite(actual)&&std::isfinite(expected)&&error<=5e-5+2e-4*std::abs(double(expected)),where+" got="+std::to_string(actual)+" expected="+std::to_string(expected));
}
static void fixture(Scene &scene){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);scene.film->set_use_sample_count(false);
  scene.camera->set_full_width(64);scene.camera->set_full_height(1);scene.camera->set_screen_size(64,1);scene.camera->compute_auto_viewplane();
  auto world_graph=make_unique<ShaderGraph>();auto bg=world_graph->create_node<BackgroundNode>();bg->set_color(zero_float3());world_graph->connect(bg->output("Background"),world_graph->output()->input("Surface"));
  auto world=scene.create_node<Shader>();world->set_graph(std::move(world_graph));world->tag_update(&scene);scene.background->set_shader(world);
  auto graph=make_unique<ShaderGraph>();auto emission=graph->create_node<EmissionNode>();emission->set_color(make_float3(.8,.5,.25));emission->set_strength(1);
  graph->connect(emission->output("Emission"),graph->output()->input("Surface"));auto shader=scene.create_node<Shader>();shader->set_graph(std::move(graph));shader->tag_update(&scene);
#ifdef CODEBRIX_TRIANGLE_LIGHT_GATE
  scene.integrator->set_motion_blur(true);scene.camera->set_shuttertime(.6f);
  for(int i=0;i<6;i++){
    auto mesh=scene.create_node<Mesh>();array<Node*> slots;slots.push_back_slow(shader);mesh->set_used_shaders(slots);mesh->resize_mesh(3,1);
    const float3 vertices[]={make_float3(-1.1f,-.8f,0),make_float3(1.3f,-.7f,0),make_float3(-.2f,1.2f,0)};
    std::copy_n(vertices,3,mesh->get_position_for_write());for(int k=0;k<3;k++)mesh->get_triangles()[k]=k;
    mesh->get_shader()[0]=0;mesh->get_smooth()[0]=false;mesh->tag_triangles_modified();
    auto object=scene.create_node<Object>();object->set_geometry(mesh);
    object->set_tfm(transform_translate(make_float3(i*2.8f-6,0,4))*transform_scale(make_float3(i%2?-1.2f:1.f,.7f+.2f*i,1.f)));
    if(i==4)object_motion_fixture(scene,*object,1);
    if(i==5)deforming_motion_fixture(scene,*object,1);
    if(i==2){auto instance=scene.create_node<Object>();instance->set_geometry(mesh);instance->set_tfm(transform_translate(make_float3(0,2,6))*transform_scale(make_float3(.8,1.4,.6)));}
  }
  shader->set_emission_sampling_method(EMISSION_SAMPLING_FRONT_BACK);
#else
  for(int i=0;i<4;i++){
#ifdef CODEBRIX_BROAD_LIGHT_GATE
    Light *light;if(i<2){auto spot=scene.create_node<SpotLight>();spot->set_radius(i==0?1.25f:.8f);spot->set_is_sphere(i==0);spot->set_angle(.9f);spot->set_smooth(.2f);light=spot;}
    else{auto area=scene.create_node<AreaLight>();area->set_sizeu(1.5f);area->set_sizev(i==2?.8f:1.5f);area->set_ellipse(i==3);area->set_spread(i==2?M_PI_F:.8f);area->set_normalize(i==3);light=area;}
#else
    Light *light;if(i<2){auto point=scene.create_node<PointLight>();point->set_radius(i==0?1.25f:.8f);point->set_is_sphere(i==0);light=point;}
    else{auto sun=scene.create_node<SunLight>();sun->set_angle(i==2?.6f:0.f);sun->set_normalize(false);light=sun;}
#endif
    light->set_use_mis(true);light->set_strength(make_float3(2,3,4));array<Node*> slots;slots.push_back_slow(shader);light->set_used_shaders(slots);
    auto object=scene.create_node<Object>();object->set_geometry(light);object->set_tfm(transform_translate(make_float3(i*3.f,0,4)));
  }
  for(auto shader:scene.shaders)shader->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
#endif
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2,"Usage: emitter_sample_gate OUTPUT_PREFIX");auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");
  Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=*dynamic_cast<WebGPUDevice*>(owner.get());Metrics metrics;bool exchange_only=false;
  {
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.use_texture_cache=false;Scene scene(params,&d);fixture(scene);Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Emitter scene preparation failed");
    bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);const auto layout=fields(d);
    const unsigned count=4096;std::vector<IntegratorStateCPU> states(count);
    for(unsigned i=0;i<count;i++){auto &s=states[i];s.path.sample=i%64;s.path.rng_pixel=i*2654435761u;s.path.rng_offset=32*(i%4);s.path.bounce=i%3;
      s.path.flag=(i%2)?PATH_RAY_MIS_HAD_TRANSMISSION:0;s.ray.time=float(i%17)/16.f;
      s.ray.P=make_float3(float(i%23)-5.f,float((i/23)%17)*.5f-4.f,4.f-(.1f+float(i%31)*.2f));
      s.ray.D=normalize(make_float3(.3f*(int(i%5)-2),.2f*(int(i%7)-3),i%11?1.f:-1.f));}
    // Scene exchange (reference_policy.h): dump or load the prepared tables and the path-state
    // fields set above (the directions carry this host's float3 normalization).
    struct PathInput {uint32_t sample,rng_pixel,rng_offset,bounce,flag;float time,P[3],D[3];};
    std::vector<PathInput> inputs(count);
    for(unsigned i=0;i<count;i++){const auto &s=states[i];inputs[i]={s.path.sample,s.path.rng_pixel,s.path.rng_offset,uint32_t(s.path.bounce),s.path.flag,s.ray.time,{s.ray.P.x,s.ray.P.y,s.ray.P.z},{s.ray.D.x,s.ray.D.y,s.ray.D.z}};}
    exchange_only=reference_policy::exchange_scene(scene.dscene,"samples",{{"paths",inputs.data(),inputs.size()*sizeof(PathInput)}});
    if(reference_policy::reference_only())for(unsigned i=0;i<count;i++){auto &s=states[i];const auto &in=inputs[i];
      s.path.sample=in.sample;s.path.rng_pixel=in.rng_pixel;s.path.rng_offset=in.rng_offset;s.path.bounce=in.bounce;s.path.flag=in.flag;s.ray.time=in.time;
      s.ray.P=make_float3(in.P[0],in.P[1],in.P[2]);s.ray.D=make_float3(in.D[0],in.D[1],in.D[2]);}
    if(!exchange_only)upload(d,layout,states);const auto local=original_light_samples(scene.dscene,states);reference_policy::record_local("samples",local);
    // Pass/fail reference: local, or the imported correctly rounded one (reference_policy.h).
    const auto expected=reference_policy::resolve("samples",local,std::string(argv[1])+".reference-diagnostic.jsonl");
    if(!exchange_only){
    device_vector<float> output(&d,"emitter_samples",MEM_READ_WRITE);output.alloc(expected.size()+16);std::fill_n(output.data(),output.size(),-1234.f);output.copy_to_device();
#ifdef CODEBRIX_TRIANGLE_LIGHT_GATE
    constexpr unsigned probe_group_size=1;
#else
    constexpr unsigned probe_group_size=64;
#endif
    // The probe compiles the surface module specialized to this scene, exactly as
    // the device does (pipelines.h): the unspecialized module with emissive-triangle
    // sampling crashed the Intel Mesa 25.0.7 compiler on every cold compile.
    const std::string source=d.module_wgsl(surface_source())+"const EMITTER_PROBE_GROUP_SIZE:u32="+std::to_string(probe_group_size)+"u;\n"+R"WGSL(
@compute @workgroup_size(EMITTER_PROBE_GROUP_SIZE) fn emitter_sample_probe(@builtin(global_invocation_id) id:vec3<u32>){
 if(id.x>=p(1u)){return;}var sd:SurfaceData;sd.time=heap_float(state_address(S_ray_time,id.x,4u));sd.P=heap_vec3(state_address(S_ray_P,id.x,12u));sd.N=heap_vec3(state_address(S_ray_D,id.x,12u));var r:SvmResult;
 if((state_word(S_path_flag,id.x)&PATH_RAY_MIS_HAD_TRANSMISSION)!=0u){r.runtime_flag=SR_BSDF_HAS_TRANSMISSION;}
 let ls=light_sample_native(id.x,sd,r);let at=p(4u)+id.x*48u;let valid=ls.pdf>0.0;store_word(at,bitcast<u32>(select(0.0,1.0,valid)));
 if(valid){let values=array<f32,11>(ls.P.x,ls.P.y,ls.P.z,ls.D.x,ls.D.y,ls.D.z,ls.t,ls.pdf,ls.eval_fac,f32(ls.prim),f32(ls.object_id));
 for(var j=0u;j<11u;j++){store_word(at+4u+j*4u,bitcast<u32>(values[j]));}}
})WGSL";
    std::ofstream(std::string(argv[1])+".wgsl")<<source;
    WGPUShaderSourceWGSL code{};code.chain.sType=WGPUSType_ShaderSourceWGSL;code.code={source.data(),source.size()};WGPUShaderModuleDescriptor descriptor{};descriptor.nextInChain=&code.chain;d.gpu.push_scope();cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> module(wgpuDeviceCreateShaderModule(d.gpu.get(),&descriptor));d.gpu.pop_scope();auto shader=d.gpu.pipeline(module,d.pipeline_layout,"emitter_sample_probe");
    std::array<uint32_t,32> p{};p[1]=count;p[4]=output.device_pointer+32;p[6]=d.constants.at("data");p[7]=d.constants.at("integrator_state");p[9]=scene.dscene.sample_pattern_lut.device_pointer;p[14]=d.scene_tables();p[17]=d.error_word;
    d.dispatch(p,count,shader,probe_group_size);d.sync();output.copy_from_device();healthy(d);uint error=0;d.download(d.error_word,&error,4);require(error==0,"Emitter shader error");
    auto save=[&](const char *suffix,const float *data,size_t count){std::ofstream f(std::string(argv[1])+suffix,std::ios::binary);f.write(reinterpret_cast<const char*>(data),count*4);};
    save(".reference.bin",local.data(),local.size());save(".actual.bin",output.data()+8,expected.size());
    unsigned valid=0;for(unsigned i=0;i<count;i++){const auto at=i*12;require(output[at+8]==expected[at],"Emitter validity differs index="+std::to_string(i));metrics.exact++;if(!expected[at])continue;valid++;
      for(unsigned j=1;j<12;j++)numeric(output[at+j+8],expected[at+j],metrics,"Emitter sample="+std::to_string(i)+" field="+std::to_string(j));}
    for(unsigned i=0;i<8;i++)require(output[i]==-1234.f&&output[output.size()-1-i]==-1234.f,"Emitter sample output guard overwritten");require(valid>100,"Insufficient valid emitter samples");metrics.paths=count;metrics.lamp_hits=valid;
#ifdef CODEBRIX_TRIANGLE_LIGHT_GATE
    const auto saved=d.light_distribution_records;const auto data=d.data;
    // Pure sampler math includes configurations that the renderer deliberately
    // rejects until their independent images pass. Verify that boundary too.
    bool unsupported=false;try{d.validate_lighting(true);}catch(const std::exception &e){unsupported=std::string(e.what()).find("static instanced triangle emitters")!=std::string::npos;}
    require(unsupported,"Missing renderer static-instance emitter guard");metrics.guards++;
    const char *reasons[]={"Invalid native triangle light distribution","Invalid native triangle light distribution","Invalid native triangle selection probability","Invalid native light CDF"};
    for(unsigned bad=0;bad<4;bad++){
      d.light_distribution_records=saved;d.data=data;
      if(bad==0)d.light_distribution_records[0].object_id=int(d.object_records.size());
      if(bad==1)d.light_distribution_records[0].prim=0x7fffffff;
      if(bad==2)d.data.integrator.distribution_pdf_triangles=-1;
      if(bad==3)d.light_distribution_records[0].totarea=-1;
      bool rejected=false;try{d.validate_lighting(true);}catch(const std::exception &e){rejected=std::string(e.what()).find(reasons[bad])!=std::string::npos;}
      require(rejected,"Triangle light guard did not reject case="+std::to_string(bad));metrics.guards++;
    }d.light_distribution_records=saved;d.data=data;
#else
    const auto saved=d.light_records;
    for(unsigned bad=0;bad<6;bad++){d.light_records=saved;std::string reason;
      if(bad==0){d.light_records[2].area.len_u=0;reason="degenerate area";}
      if(bad==1){d.light_records[2].area.len_v=0;reason="degenerate area";}
      if(bad==2){d.light_records[2].area.tan_half_spread=-1;reason="Invalid native area";}
      if(bad==3){d.light_records[2].area.invarea=std::numeric_limits<float>::quiet_NaN();reason="Invalid native area";}
      if(bad==4){d.light_records[0].spot.cos_half_larger_spread=2;reason="Invalid native spot";}
      if(bad==5){d.light_records[0].type=LIGHT_BACKGROUND;reason="emitter type not implemented";}
      bool rejected=false;try{d.validate_lighting(true);}catch(const std::exception &error){rejected=std::string(error.what()).find(reason)!=std::string::npos;}
      require(rejected,"Emitter guard did not reject case="+std::to_string(bad));metrics.guards++;
    }d.light_records=saved;
#endif
    }
  }
  require(stats.mem_used==0,"Emitter test leaked allocations");std::ofstream(std::string(argv[1])+".library.txt")<<d.gpu.library_path;
  if(exchange_only)std::cout<<"{\"passed\":true,\"sceneExchangeOnly\":\""<<(reference_policy::prepare_only()?"dump":"reference")<<"\",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
  else std::cout<<std::setprecision(17)<<"{\"passed\":true,\"samples\":"<<metrics.paths<<",\"guardCases\":"<<metrics.guards<<",\"valid\":"<<metrics.lamp_hits<<",\"numericChecks\":"<<metrics.numeric<<",\"strictDifferences\":"<<metrics.strict<<",\"maximumAbsoluteError\":"<<metrics.maximum<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
