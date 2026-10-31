// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "surface_svm_test.h"
#include "image_fixture.h"
#include "object_motion_fixture.h"
#include "deforming_motion_fixture.h"
#include "image_sampling_reference.h"
#include <span>
#include <filesystem>
#include <iomanip>
static constexpr unsigned IMAGE_COUNT=128;
static std::vector<ImageHandle> prepare_images(Scene &scene){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);scene.integrator->set_aa_samples(8);
  scene.film->set_use_sample_count(false);scene.camera->set_full_width(64);scene.camera->set_full_height(48);scene.camera->set_screen_size(64,48);scene.camera->compute_auto_viewplane();
  array<Node*> shaders;std::vector<ImageHandle> handles;
  for(unsigned i=0;i<IMAGE_COUNT;i++){
    const auto format=ImageDataType(i/16);ImageParams p;p.interpolation=InterpolationType((i/4)%4);p.extension=ExtensionType(i%4);p.alpha_type=IMAGE_ALPHA_ASSOCIATED;
    auto handle=scene.image_manager->add_image(make_unique<FixtureImageLoader>(i,format,i%16==0?1:7,i%16==0?1:5),p,false);handles.push_back(handle);
    auto g=make_unique<ShaderGraph>();auto uv=g->create_node<UVMapNode>();auto tex=g->create_node<ImageTextureNode>();tex->handle=handle;
    tex->set_colorspace(u_colorspace_scene_linear);tex->set_alpha_type(IMAGE_ALPHA_ASSOCIATED);
    auto emission=g->create_node<EmissionNode>();emission->set_strength(.8f);
    g->connect(uv->output("UV"),tex->input("Vector"));g->connect(tex->output("Color"),emission->input("Color"));g->connect(tex->output("Alpha"),emission->input("Strength"));g->connect(emission->output("Emission"),g->output()->input("Surface"));
    auto sh=scene.create_node<Shader>();sh->set_graph(std::move(g));sh->tag_update(&scene);shaders.push_back_slow(sh);
  }
  auto mesh=scene.create_node<Mesh>();mesh->set_used_shaders(shaders);mesh->resize_mesh(IMAGE_COUNT*3,IMAGE_COUNT);
  for(unsigned i=0;i<IMAGE_COUNT;i++){
    float x=(i%16)*2,y=(i/16)*2;auto v=mesh->get_position_for_write();v[3*i]=make_float3(x,y,.1);v[3*i+1]=make_float3(x+1,y,.4);v[3*i+2]=make_float3(x,y+1,-.2);
    for(unsigned j=0;j<3;j++){mesh->get_triangles()[3*i+j]=3*i+j;}mesh->get_shader()[i]=i;mesh->get_smooth()[i]=false;
  }
  mesh->tag_triangles_modified();auto uv=mesh->attributes.add(ATTR_STD_UV)->data_for_write<float2>();
  for(unsigned i=0;i<IMAGE_COUNT;i++){uv[3*i]=make_float2(-.3f,-.2f);uv[3*i+1]=make_float2(1.7f,.1f);uv[3*i+2]=make_float2(.1f,1.4f);}
  auto ob=scene.create_node<Object>();ob->set_geometry(mesh);ob->set_tfm(transform_scale(make_float3(-1.2f,.8f,1.4f)));
  return handles;
}
static uint64_t sample_gate(WebGPUDevice &d,Scene &scene,const std::vector<ImageHandle> &handles,const std::string &prefix,double &maximum,uint64_t &strict,unsigned &rejected){
  std::vector<ImageProbe> probes;
  const float coords[]={-2,-1,-.5f,-1.f/7,-.00001f,0,.5f/7,1.f/7,.21f,.5f,.9f,1,1.00001f,1.5f,2};
  for(auto &h:handles)for(unsigned flags=0;flags<4;flags++)for(float u:coords)for(float v:coords)probes.push_back({h.kernel_id(),flags,u,v});
  // Native UDIM table with a deliberate missing tile; preserve truncation of negative UVs.
  auto &udims=scene.dscene.image_texture_udims;udims.alloc(3);udims[0]={2,0};udims[1]={1001,handles[3].kernel_id()};udims[2]={1012,handles[17].kernel_id()};udims.copy_to_device();
  for(float u:coords)for(float v:coords){probes.push_back({-1,3,u,v});probes.push_back({KERNEL_IMAGE_NONE,0,u,v});}
  const auto expected=original_image_samples(scene.dscene,probes);
  device_vector<ImageProbe> input(&d,"image_probe_input",MEM_READ_ONLY);input.alloc(probes.size());std::copy(probes.begin(),probes.end(),input.data());input.copy_to_device();
  device_vector<uint> output(&d,"image_probe_output",MEM_READ_WRITE);output.alloc(16+probes.size()*4);std::fill_n(output.data(),output.size(),0xa5a5a5a5);output.copy_to_device();
  std::string source=svm_surface_source()+R"WGSL(
@compute @workgroup_size(64) fn image_probe(@builtin(global_invocation_id) id:vec3<u32>){
 if(id.x>=p(1u)){return;}let at=p(2u)+id.x*16u;var s:SvmState;
 let color=svm_image_sample(&s,bitcast<i32>(load_word(at)),vec2<f32>(heap_float(at+8u),heap_float(at+12u)),load_word(at+4u));
 if(s.error!=0u){return;}let out=p(4u)+id.x*16u;for(var i=0u;i<4u;i++){store_word(out+i*4u,bitcast<u32>(color[i]));}
})WGSL";
  source=gate_probe_wgsl(d,source);std::ofstream(prefix+".sampling.wgsl")<<source;
  WGPUShaderSourceWGSL code{};code.chain.sType=WGPUSType_ShaderSourceWGSL;code.code={source.data(),source.size()};WGPUShaderModuleDescriptor md{};md.nextInChain=&code.chain;d.gpu.push_scope();cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> module(wgpuDeviceCreateShaderModule(d.gpu.get(),&md));d.gpu.pop_scope();auto shader=d.gpu.pipeline(module,d.pipeline_layout,"image_probe");
  std::array<uint32_t,32> p{};p[1]=probes.size();p[2]=input.device_pointer;p[4]=output.device_pointer+32;p[14]=d.scene_tables();p[17]=d.error_word;
  const auto capture=std::filesystem::path(prefix).parent_path()/"sampling"/"run";std::filesystem::create_directories(capture.parent_path());
  auto capture_file=[&](const char *suffix,const void *data,size_t bytes){std::ofstream f(capture.string()+suffix,std::ios::binary);f.write(static_cast<const char*>(data),bytes);require(bool(f),"Sampler capture write failed");};
  auto staging=d.gpu.buffer(d.arena_bytes,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);wgpuCommandEncoderCopyBufferToBuffer(d.commands(),d.arena,0,staging,0,d.arena_bytes);d.flush();std::vector<uint8_t> arena(d.arena_bytes);d.gpu.read(staging,arena.data(),arena.size());
  capture_file(".arena.bin",arena.data(),arena.size());capture_file(".params.bin",p.data(),sizeof(p));capture_file(".reference.bin",expected.data(),expected.size()*sizeof(float4));
  std::ofstream(capture.string()+".wgsl")<<source;
  std::ofstream(capture.string()+".capture.json")<<"{\"entry\":\"image_probe\",\"recordSize\":16,\"count\":"<<probes.size()<<",\"outputOffset\":"<<p[4]<<",\"errorOffset\":"<<p[17]<<",\"fields\":[{\"name\":\"rgba\",\"offset\":0,\"size\":16,\"floating\":true}]}\n";
  d.dispatch(p,probes.size()+63,shader);d.sync();output.copy_from_device();healthy(d);
  capture_file(".actual.bin",output.data()+8,expected.size()*sizeof(float4));
  double maximum_ratio=0,worst_actual=0,worst_expected=0;size_t worst_index=0;unsigned worst_channel=0;
  for(size_t i=0;i<probes.size();i++)for(unsigned j=0;j<4;j++){
    float actual=std::bit_cast<float>(output[8+i*4+j]);float wanted=reinterpret_cast<const float*>(&expected[i])[j];double error=std::abs(double(actual)-wanted);if(error>maximum){maximum=error;worst_actual=actual;worst_expected=wanted;worst_index=i;worst_channel=j;}maximum_ratio=std::max(maximum_ratio,error/(5e-5+2e-4*std::abs(double(wanted))));strict+=error>1e-7+1e-7*std::abs(double(wanted));
    require(std::isfinite(actual)&&error<=5e-5+2e-4*std::abs(double(wanted)),"Image sample "+std::to_string(i)+" id="+std::to_string(probes[i].id)+" uv="+std::to_string(probes[i].u)+","+std::to_string(probes[i].v)+" channel="+std::to_string(j)+" got="+std::to_string(actual)+" wanted="+std::to_string(wanted));
  }
  for(unsigned i=0;i<8;i++)require(output[i]==0xa5a5a5a5&&output[output.size()-1-i]==0xa5a5a5a5,"Image sampler guard overwritten");
  std::ofstream(prefix+".sampling-diagnostics.json")<<std::setprecision(17)<<"{\"maximumToleranceRatio\":"<<maximum_ratio<<",\"largestAbsoluteError\":"<<maximum<<",\"actual\":"<<worst_actual<<",\"reference\":"<<worst_expected<<",\"probe\":"<<worst_index<<",\"channel\":"<<worst_channel<<"}\n";
  // Mutate one descriptor at a time; no invalid sample may write output.
  const unsigned tex_id=handles[0].kernel_id();const auto tex_saved=scene.dscene.image_textures[tex_id];const auto info_id=tex_saved.image_info_id;
  const auto info_address=d.globals.at("image_info")+info_id*sizeof(KernelImageInfo);KernelImageInfo original_info{};d.download(info_address,&original_info,sizeof(original_info));
  const auto bounds_address=d.globals.at("image_bounds")+info_id*sizeof(uint2);uint2 original_bounds{};d.download(bounds_address,&original_bounds,sizeof(original_bounds));
  for(unsigned test=0;test<14;test++){
    ImageProbe probe{int(tex_id),0,.3f,.4f};auto info=original_info;auto tex=tex_saved;auto bounds=original_bounds;
    if(test==0)probe.id=scene.dscene.image_textures.size();
    if(test==1)tex.image_info_id=0x7ffffffe;
    if(test==2)tex.tile_descriptor_offset=0;
    if(test==3)info.data_type=IMAGE_DATA_TYPE_NANOVDB_FLOAT;
    if(test==4)info.width=0;
    if(test==5)info.data|=uint64_t(1)<<32;
    if(test==6)bounds.y=0;
    if(test==7)info.interpolation=INTERPOLATION_NUM_TYPES;
    if(test==8)info.extension=EXTENSION_NUM_TYPES;
    if(test==9)info.data+=16;
    if(test==10)probe.flags=4;
    if(test==11)probe.u=std::numeric_limits<float>::quiet_NaN();
    if(test==12){probe.id=-1;udims[0].tile=udims.size();udims.copy_to_device();}
    if(test==13)probe.id=INT_MIN;
    d.upload(input.device_pointer,&probe,sizeof(probe));scene.dscene.image_textures[tex_id]=tex;scene.dscene.image_textures.copy_to_device();
    d.upload(info_address,&info,sizeof(info));d.upload(bounds_address,&bounds,sizeof(bounds));
    uint guard[4]={0xa5a5a5a5,0xa5a5a5a5,0xa5a5a5a5,0xa5a5a5a5};d.upload(p[4],guard,sizeof(guard));p[1]=1;p[14]=d.scene_tables();d.dispatch(p,1,shader);
    uint error=0;d.download(d.error_word,&error,4);d.download(p[4],guard,sizeof(guard));
    require(error==(test==2?256u:test==10?16u:128u),"Image rejection "+std::to_string(test)+" error="+std::to_string(error));
    for(uint v:guard)require(v==0xa5a5a5a5,"Rejected image wrote output");
    d.zero(d.error_word,4);rejected++;
    scene.dscene.image_textures[tex_id]=tex_saved;scene.dscene.image_textures.copy_to_device();d.upload(info_address,&original_info,sizeof(original_info));d.upload(bounds_address,&original_bounds,sizeof(original_bounds));udims[0].tile=2;udims.copy_to_device();
  }
  return probes.size();
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2||(argc==3&&(std::string(argv[2])=="--motion"||std::string(argv[2])=="--deform"||std::string(argv[2])=="--deform-motion")),"Usage: image_gate OUTPUT_PREFIX [--motion|--deform|--deform-motion]");const bool deform=argc==3&&std::string(argv[2])!="--motion";const bool motion=argc==3&&std::string(argv[2])!="--deform";std::string prefix=argv[1];auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU device");Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=require_webgpu_device(owner.get(),"image_gate device creation");Metrics m;uint64_t samples=0,strict=0;double maximum=0;unsigned rejected=0,typed=0;std::set<unsigned> formats;
  {SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;Scene scene(params,&d);auto handles=prepare_images(scene);if(motion)object_motion_fixture(scene,*scene.objects[0],4);if(deform)deforming_motion_fixture(scene,*scene.objects[0],1);Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Image scene preparation failed");
    for(auto &[id,image]:d.images){formats.insert(image->info.data_type);}require(formats.size()==8&&d.images.size()==IMAGE_COUNT,"Native ImageManager format/texture coverage changed");
    samples=sample_gate(d,scene,handles,prefix,maximum,strict,rejected);
    auto &ds=scene.dscene;ds.data.integrator.use_light_mis=0;ds.data.integrator.use_caustics=0;ds.data.max_closures=MAX_CLOSURE;d.const_copy_to("data",&ds.data,sizeof(KernelData));
    bool cancelled=false;Scheduler scheduler(&d,scene.film,&ds,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();auto layout=fields(d);std::vector<IntegratorStateCPU> states(scheduler.capacity());std::vector<int> ids;
    for(unsigned t=0;t<IMAGE_COUNT;t++)for(unsigned j=0;j<6;j++){unsigned i=ids.size()*2;ids.push_back(i);auto &s=states[i];s.isect.object=scene.objects[0]->get_device_index();s.isect.prim=scene.objects[0]->get_geometry()->prim_offset+t;s.isect.type=scene.dscene.objects[s.isect.object].primitive_type;s.isect.u=.17f*j;s.isect.v=.11f*(5-j);s.isect.t=.1f*j;s.ray.P=make_float3(.1,.2,-2);s.ray.D=normalize(make_float3(.13f*j-.8f,.4f-.07f*j,j%2?1.f:-1.f));s.ray.time=((motion||deform)?.2f:.1f)*j;s.ray.dP=.007f*j;s.ray.dD=.013f*j;s.path.visibility=PATH_RAY_VISIBILITY_CAMERA;s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE;}
    auto source=probe_source();std::ofstream(prefix+".wgsl")<<gate_probe_wgsl(d,source);auto shader=pipeline(d,source);evaluate(d,scene,shader,layout,states,ids,"Native image graphs",m,prefix);
    std::vector<int> small(ids.begin(),ids.begin()+6);
    const float3 coordinates[]={make_float3(.13,.47,.61),make_float3(0,0,0),make_float3(.5,.5,.5),make_float3(.5,.5,1),make_float3(1,0,1),make_float3(.99,.01,.57)};
    const float3 normals[]={make_float3(1,0,0),make_float3(0,1,0),make_float3(0,0,1),normalize(make_float3(-1,1,0)),normalize(make_float3(0,-1,1)),normalize(make_float3(1,0,-1)),normalize(make_float3(-1,1,1))};
    for(unsigned mode=0;mode<11;mode++)for(unsigned variant=0;variant<16;variant++){
      const unsigned flags=variant%4;const bool derivative=(variant/4)%2,alpha=variant>=8;Program program(ds);
      program.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=0,.value=coordinates[variant%6]});
      program.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=3,.value=make_float3(.13,.07,-.05)});program.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=6,.value=make_float3(-.17,.11,.03)});
      program.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=9,.value=normals[variant%7]});program.add(NODE_CLOSURE_SET_NORMAL,SVMNodeClosureSetNormal{.direction_offset=9,.normal_offset=12});
      int id=handles[(variant*7+mode*11)%handles.size()].kernel_id();
      if(mode<3){const unsigned projections[]={NODE_IMAGE_PROJ_FLAT,NODE_IMAGE_PROJ_SPHERE,NODE_IMAGE_PROJ_TUBE};program.add(derivative?NODE_TEX_IMAGE_DERIVATIVE:NODE_TEX_IMAGE,SVMNodeTexImage{.id=id,.projection=projections[mode],.flags=uint8_t(flags),.co=0,.out_offset=20,.alpha_offset=23});}
      else if(mode<9){const float blends[]={0,.01f,.2f,.5f,.9f,1};program.add(derivative?NODE_TEX_IMAGE_BOX_DERIVATIVE:NODE_TEX_IMAGE_BOX,SVMNodeTexImageBox{.id=id,.blend=blends[mode-3],.flags=uint8_t(flags),.co=0,.out_offset=20,.alpha_offset=23});}
      else program.add(derivative?NODE_TEX_ENVIRONMENT_DERIVATIVE:NODE_TEX_ENVIRONMENT,SVMNodeTexEnvironment{.id=id,.projection=NodeEnvironmentProjection(mode-9),.flags=uint8_t(flags),.co=0,.out_offset=20,.alpha_offset=23});
      if(alpha)program.add(NODE_CONVERT,SVMNodeConvert{.convert_type=NODE_CONVERT_FV,.from_offset=23,.to_offset=20});
      program.add(NODE_CLOSURE_WEIGHT,SVMNodeClosureWeight{.weight_offset=20});program.add(NODE_CLOSURE_EMISSION,SVMNodeClosureEmission{.mix_weight_offset=SVM_STACK_INVALID});program.install(ds);
      evaluate(d,scene,shader,layout,states,small,"Image projection "+std::to_string(mode)+" variant "+std::to_string(variant),m);typed++;
    }
  }
  require(stats.mem_used==0&&d.images.empty(),"Image cleanup leak");std::ofstream(prefix+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(prefix+".library.txt")<<d.gpu.library_path;
  std::cout<<"{\"passed\":true,\"nativeImageManager\":true,\"formats\":"<<formats.size()<<",\"nativeGraphs\":"<<IMAGE_COUNT<<",\"typedPrograms\":"<<typed<<",\"invalidCasesRejected\":"<<rejected<<",\"samplingCases\":"<<samples<<",\"samplingMaximumError\":"<<maximum<<",\"samplingStrictFailures\":"<<strict<<",\"graphPaths\":"<<m.paths<<",\"graphMaximumError\":"<<m.maximum<<",\"graphStrictFailures\":"<<m.strict<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
