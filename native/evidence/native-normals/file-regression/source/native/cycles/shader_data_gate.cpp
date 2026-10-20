// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "wavefront_test.h"
#include "wavefront.h"
#include "shader_data_reference.h"
#include "scene/scene.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/camera.h"
#include "scene/integrator.h"
#include "util/progress.h"
#include "util/task.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
using namespace ccl;
using namespace wavefront_test;
static std::string capture_prefix;
struct Metrics {
  uint64_t paths=0,float_checks=0,int_checks=0,state_checks=0,guard_bytes=0,strict_failures=0;
  unsigned cases=0,camera_hits=0,backfacing=0,smooth=0,corner=0,instances=0,applied=0,negative=0;
  std::array<unsigned,3> projection_axes{};unsigned negative_position_offsets=0,negative_normal_offsets=0;
  double maximum_error=0;std::set<int> objects;
};
static void prepare(Scene &scene){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);
  scene.integrator->set_aa_samples(16);scene.film->set_use_sample_count(false);
  scene.camera->set_full_width(64);scene.camera->set_full_height(48);scene.camera->set_screen_size(64,48);scene.camera->set_fov(1.2f);
  scene.camera->set_matrix(transform_translate(make_float3(0,0,-10)));scene.camera->compute_auto_viewplane();
  auto mesh=[&](unsigned kind){
    auto m=scene.create_node<Mesh>();array<Node*> shaders;shaders.push_back_slow(scene.default_surface);m->set_used_shaders(shaders);m->resize_mesh(4,2);
    const float v[][3]={{-1,-1,-.3f},{1.1f,-.9f,.1f},{.8f,1,.6f},{-.9f,.7f,-.2f}};
    for(unsigned i=0;i<4;i++)m->get_position_for_write()[i]=make_float3(v[i][0],v[i][1],v[i][2]);
    const int tris[]={0,1,2,0,2,3};std::copy_n(tris,6,m->get_triangles().data());std::ranges::fill(m->get_smooth(),kind==1);std::ranges::fill(m->get_shader(),0);m->tag_triangles_modified();
    if(kind==1){auto attr=m->attributes.add(ATTR_STD_VERTEX_NORMAL);auto n=attr->data_for_write<packed_normal>();
      for(unsigned i=0;i<4;i++)n[i]=packed_normal(normalize(make_float3(-.4f+.23f*i,.3f-.19f*i,1)));}
    if(kind==2){auto attr=m->attributes.add(ATTR_STD_CORNER_NORMAL);auto n=attr->data_for_write<packed_normal>();
      for(unsigned i=0;i<6;i++)n[i]=packed_normal(normalize(make_float3(.2f+.1f*i,-.3f+.11f*i,i<3?1.f:-1.f)));}
    return m;
  };
  for(unsigned kind=0;kind<3;kind++){
    auto shared=mesh(kind);
    for(unsigned column=0;column<3;column++){
      auto ob=scene.create_node<Object>();ob->set_geometry(column==2?mesh(kind):shared);
      auto tfm=transform_translate(make_float3((int(column)-1)*2.9f,(int(kind)-1)*2.5f,.4f*kind))*
        transform_rotate(.19f+.75f*kind+.61f*column,normalize(make_float3(.7f,.9f,.2f)))*
        transform_scale(make_float3(column==1||kind==2?-1.2f:1.1f,.7f+.1f*kind,1.5f));
      ob->set_tfm(tfm);
    }
  }
}
static std::string probe_source(){
  std::ostringstream code;code<<shader_data_source();
  code<<"@compute @workgroup_size(64) fn shader_data_probe(@builtin(global_invocation_id) id:vec3<u32>){\n"
        " if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}\n"
        " let sd=surface_from_ray(index);if(sd.error!=0u){return;}\n"
        " let base=p(4u)+id.x*"<<sizeof(ShaderDataRecord)<<"u;\n";
  auto field=[&](const char *name,size_t offset,size_t size,bool floating){
    const char *component[]={".x",".y",".z"};
    for(size_t i=0;i<size/4;i++)code<<" store_word(base+"<<offset+4*i<<"u,"<<(floating?"bitcast<u32>(":"")<<"sd."<<name<<(size>4?component[i]:"")<<(floating?")":"")<<");\n";
  };
#define FIELD(type,name,wgsl) field(#wgsl,offsetof(ShaderDataRecord,name),sizeof(type),true);
  CBC_SHADER_FLOAT_FIELDS(FIELD)
#undef FIELD
#define FIELD(type,name,wgsl) field(#wgsl,offsetof(ShaderDataRecord,name),sizeof(type),false);
  CBC_SHADER_INT_FIELDS(FIELD)
#undef FIELD
  code<<"}\n";return code.str();
}
static cbc::Pipeline pipeline(WebGPUDevice &d,const std::string &source){
  WGPUShaderSourceWGSL wgsl{};wgsl.chain.sType=WGPUSType_ShaderSourceWGSL;wgsl.code={source.data(),source.size()};
  WGPUShaderModuleDescriptor desc{};desc.nextInChain=&wgsl.chain;d.gpu.push_scope();
  cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> module(wgpuDeviceCreateShaderModule(d.gpu.get(),&desc));d.gpu.pop_scope();
  return d.gpu.pipeline(module,d.pipeline_layout,"shader_data_probe");
}
static void evaluate(WebGPUDevice &d,Scene &scene,cbc::Pipeline &shader,const std::vector<Field> &layout,
                     const std::vector<IntegratorStateCPU> &states,const std::vector<int> &indices,
                     const std::string &label,Metrics &m,bool dense=false){
  auto expected=original_shader_data(scene.dscene,indices,states);upload(d,layout,states);
  device_vector<int> selected(&d,"shader_data_indices",MEM_READ_WRITE);selected.alloc(indices.size());std::copy(indices.begin(),indices.end(),selected.data());selected.copy_to_device();
  device_vector<uint> output(&d,"shader_data_output",MEM_READ_WRITE);output.alloc(16+(indices.size()+3)*sizeof(ShaderDataRecord)/4);std::fill_n(output.data(),output.size(),0xa5a5a5a5u);output.copy_to_device();
  std::array<uint32_t,32> p{};p[1]=indices.size();p[2]=dense?0:selected.device_pointer;p[4]=output.device_pointer+32;
  p[6]=d.constants.at("data");p[7]=d.constants.at("integrator_state");p[10]=states.size();p[14]=d.scene_tables();p[17]=d.error_word;
  if(label=="Signed attribute offsets"){
    auto staging=d.gpu.buffer(d.arena_bytes,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    wgpuCommandEncoderCopyBufferToBuffer(d.commands(),d.arena,0,staging,0,d.arena_bytes);d.flush();
    std::vector<uint8_t> arena(d.arena_bytes);d.gpu.read(staging,arena.data(),arena.size());
    std::ofstream(capture_prefix+".arena.bin",std::ios::binary).write(reinterpret_cast<const char*>(arena.data()),arena.size());
    std::ofstream(capture_prefix+".params.bin",std::ios::binary).write(reinterpret_cast<const char*>(p.data()),sizeof(p));
    std::ofstream(capture_prefix+".reference.bin",std::ios::binary).write(reinterpret_cast<const char*>(expected.data()),expected.size()*sizeof(ShaderDataRecord));
    std::ofstream info(capture_prefix+".capture.json");info<<"{\"recordSize\":"<<sizeof(ShaderDataRecord)<<",\"count\":"<<expected.size()<<",\"outputOffset\":"<<p[4]<<",\"errorOffset\":"<<p[17]<<",\"fields\":[";bool first=true;
    auto field=[&](const char *name,size_t offset,size_t size,bool floating){if(!first)info<<',';first=false;info<<"{\"name\":\""<<name<<"\",\"offset\":"<<offset<<",\"size\":"<<size<<",\"floating\":"<<(floating?"true":"false")<<"}";};
#define FIELD(type,name,wgsl) field(#name,offsetof(ShaderDataRecord,name),sizeof(type),true);
    CBC_SHADER_FLOAT_FIELDS(FIELD)
#undef FIELD
#define FIELD(type,name,wgsl) field(#name,offsetof(ShaderDataRecord,name),sizeof(type),false);
    CBC_SHADER_INT_FIELDS(FIELD)
#undef FIELD
    info<<"]}\n";
  }
  d.dispatch(p,indices.size()+63,shader);d.sync();healthy(d);output.copy_from_device();healthy(d);
  const auto bytes=reinterpret_cast<const uint8_t*>(output.data());std::vector<bool> written(output.size()*4,false);
  for(size_t i=0;i<indices.size();i++){
    ShaderDataRecord actual{};std::memcpy(&actual,bytes+32+i*sizeof(actual),sizeof(actual));auto &wanted=expected[i];
    auto check=[&](const char *name,size_t offset,size_t size,bool floating){
      for(size_t k=0;k<size;k++)written[32+i*sizeof(actual)+offset+k]=true;
      for(size_t k=0;k<size;k+=4){
        uint a,e;std::memcpy(&a,reinterpret_cast<const char*>(&actual)+offset+k,4);std::memcpy(&e,reinterpret_cast<const char*>(&wanted)+offset+k,4);
        if(!floating){require(a==e,label+" integer mismatch "+name);m.int_checks++;continue;}
        const float av=std::bit_cast<float>(a),ev=std::bit_cast<float>(e);const double error=std::abs(double(av)-ev);m.maximum_error=std::max(m.maximum_error,error);m.float_checks++;
        if(error>1e-7+1e-7*std::abs(double(ev)))m.strict_failures++;
        require(std::isfinite(av)&&std::isfinite(ev)&&error<=5e-5+2e-4*std::abs(double(ev)),label+" float mismatch "+name+" path "+std::to_string(i)+" component "+std::to_string(k/4)+" actual="+std::to_string(av)+" expected="+std::to_string(ev));
      }
    };
#define FIELD(type,name,wgsl) check(#name,offsetof(ShaderDataRecord,name),sizeof(type),true);
    CBC_SHADER_FLOAT_FIELDS(FIELD)
#undef FIELD
#define FIELD(type,name,wgsl) check(#name,offsetof(ShaderDataRecord,name),sizeof(type),false);
    CBC_SHADER_INT_FIELDS(FIELD)
#undef FIELD
    m.objects.insert(wanted.object);m.backfacing+=(wanted.runtime_flag&SR_BACKFACING)!=0;m.smooth+=(wanted.shader&SHADER_SMOOTH_NORMAL)!=0;
    m.corner+=(wanted.object_flag&SD_OBJECT_HAS_CORNER_NORMALS)!=0;m.applied+=(wanted.object_flag&SD_OBJECT_TRANSFORM_APPLIED)!=0;
    m.instances+=(wanted.object_flag&SD_OBJECT_TRANSFORM_APPLIED)==0;m.negative+=(wanted.object_flag&SD_OBJECT_NEGATIVE_SCALE)!=0;
    const auto &object=scene.dscene.objects[wanted.object];m.negative_position_offsets+=object.position_offset<0;m.negative_normal_offsets+=object.normal_offset<0;
    unsigned axis=2;auto n=wanted.Ng;if(std::abs(n.z)<std::abs(n.x)||std::abs(n.z)<std::abs(n.y))axis=(std::abs(n.y)<std::abs(n.x)||std::abs(n.y)<std::abs(n.z))?0:1;
    m.projection_axes[axis]++;
  }
  for(size_t i=0;i<written.size();i++)if(!written[i]){require(bytes[i]==0xa5,"Shader-data output guard changed");m.guard_bytes++;}
  auto unchanged=download(d,layout,states.size());for(auto &f:layout)for(size_t i=0;i<states.size();i++){
    require(!std::memcmp(reinterpret_cast<const char*>(&unchanged[i])+f.offset,reinterpret_cast<const char*>(&states[i])+f.offset,f.stride),"Shader-data evaluation changed path state");m.state_checks++;
  }
  m.paths+=indices.size();m.cases++;
}
static std::vector<int> direct_states(Scene &scene,std::vector<IntegratorStateCPU> &states,unsigned mode){
  std::vector<int> indices;
  for(auto ob:scene.objects)for(unsigned triangle=0;triangle<2;triangle++)for(unsigned j=0;j<73;j++){
    unsigned index=indices.size()*3;require(index<states.size(),"Direct shader-data capacity exceeded");indices.push_back(index);auto &s=states[index];
    s.isect.object=ob->get_device_index();s.isect.prim=ob->get_geometry()->prim_offset+triangle;s.isect.type=PRIMITIVE_TRIANGLE;
    s.isect.u=float(j%9)/8;s.isect.v=float(j/9)/9*(1-s.isect.u);s.isect.t=j*.137f;
    s.ray.time=float(j)/72;s.ray.P=make_float3(.1f,.2f,-4);s.ray.dP=mode==0?0:.0003f*(j%7);s.ray.dD=mode==0?0:.0007f*(j%11);
    const float3 directions[]={make_float3(0,0,1),make_float3(0,0,-1),make_float3(1,0,0),make_float3(-1,0,0),make_float3(0,1,0),make_float3(0,-1,0)};
    s.ray.D=mode<6?directions[mode]:normalize(make_float3(.03f+.17f*(j%11),-.8f+.19f*(j%9),-.5f+.13f*(j%13)));
    s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE;
  }
  return indices;
}
static unsigned rejected(WebGPUDevice &d,Scene &scene,cbc::Pipeline &shader,const std::vector<Field> &layout,
                         const std::vector<IntegratorStateCPU> &initial){
  unsigned count=0;
  for(unsigned test=0;test<12;test++){
    auto states=initial;states[0]=initial[0];auto &s=states[0];auto object=scene.dscene.objects[s.isect.object];auto flags=scene.dscene.object_flag[s.isect.object];auto tri=scene.dscene.tri_vindex[s.isect.prim];auto sh=scene.dscene.tri_shader[s.isect.prim];
    if(test==0)s.isect.object=OBJECT_NONE;
    if(test==1)s.isect.prim=PRIM_NONE;
    if(test==2)s.isect.type=PRIMITIVE_MOTION_TRIANGLE;
    if(test==3)scene.dscene.object_flag[initial[0].isect.object]|=SD_OBJECT_MOTION;
    if(test==4)scene.dscene.object_flag[initial[0].isect.object]|=SD_OBJECT_HAS_VERTEX_MOTION;
    if(test==5)scene.dscene.objects[initial[0].isect.object].position_offset=INT_MAX;
    if(test==6)scene.dscene.tri_vindex[initial[0].isect.prim].x=0x7fffffffu;
    if(test==7)scene.dscene.tri_shader[initial[0].isect.prim]=SHADER_MASK;
    if(test==8){scene.dscene.tri_shader[initial[0].isect.prim]|=SHADER_SMOOTH_NORMAL;scene.dscene.objects[initial[0].isect.object].normal_offset=INT_MAX;}
    if(test==10)scene.dscene.tri_vindex[initial[0].isect.prim].x=scene.dscene.tri_vindex[initial[0].isect.prim].y;
    if(test==11){scene.dscene.tri_shader[initial[0].isect.prim]|=SHADER_SMOOTH_NORMAL;scene.dscene.objects[initial[0].isect.object].normal_offset=INT_MIN;}
    scene.dscene.objects.copy_to_device();scene.dscene.object_flag.copy_to_device();scene.dscene.tri_vindex.copy_to_device();scene.dscene.tri_shader.copy_to_device();
    upload(d,layout,states);device_vector<uint> out(&d,"invalid_shader_data",MEM_READ_WRITE);out.alloc(sizeof(ShaderDataRecord)/4);std::fill_n(out.data(),out.size(),0xa5a5a5a5u);out.copy_to_device();
    device_vector<int> selected(&d,"invalid_shader_data_index",MEM_READ_WRITE);selected.alloc(1);selected[0]=test==9?states.size():0;selected.copy_to_device();
    std::array<uint32_t,32> p{};p[1]=1;p[2]=selected.device_pointer;p[4]=out.device_pointer;p[6]=d.constants.at("data");p[7]=d.constants.at("integrator_state");p[10]=states.size();p[14]=d.scene_tables();p[17]=d.error_word;
    d.dispatch(p,1,shader);uint error=0;d.download(d.error_word,&error,4);require(error==1,"Invalid surface data did not report failure "+std::to_string(test));
    out.copy_from_device();healthy(d);require(std::all_of(out.data(),out.data()+out.size(),[](uint x){return x==0xa5a5a5a5u;}),"Invalid surface data wrote output");
    d.zero(d.error_word,4);scene.dscene.objects[initial[0].isect.object]=object;scene.dscene.object_flag[initial[0].isect.object]=flags;scene.dscene.tri_vindex[initial[0].isect.prim]=tri;scene.dscene.tri_shader[initial[0].isect.prim]=sh;count++;
  }
  return count;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2,"Usage: shader_data_gate OUTPUT_PREFIX");std::string prefix=argv[1];capture_prefix=prefix;auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU device");
  Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=*dynamic_cast<WebGPUDevice*>(owner.get());Metrics m;unsigned invalid=0;
  {
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;Scene scene(params,&d);prepare(scene);Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Scene update failed");
    scene.dscene.data.integrator.use_light_mis=0;scene.dscene.data.integrator.use_caustics=0;d.const_copy_to("data",&scene.dscene.data,sizeof(KernelData));
    bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();auto layout=fields(d);
    auto source=probe_source();std::ofstream(prefix+".wgsl")<<source;auto shader=pipeline(d,source);
    BufferParams bp;bp.width=64;bp.height=48;bp.pass_stride=scene.dscene.data.film.pass_stride;auto buffers=scheduler.get_render_buffers();buffers->reset(bp);buffers->zero();scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Reset failed");
    KernelWorkTile tile{};tile.w=64;tile.h=48;tile.stride=64;tile.num_samples=1;tile.start_sample=3;scheduler.enqueue_work_tiles(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA,&tile,1,0,0);require(scheduler.update_queue_counter_and_cache(),"Camera failed");
    scheduler.enqueue_path_iteration(DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST);require(scheduler.update_queue_counter_and_cache(),"Closest failed");auto states=download(d,layout,scheduler.capacity());std::vector<int> indices;
    for(unsigned i=0;i<states.size();i++)if(states[i].path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE)indices.push_back(i);
    require(indices.size()>200,"Camera fixture has too few surface hits");m.camera_hits=indices.size();evaluate(d,scene,shader,layout,states,indices,"Camera hits",m);
    for(unsigned mode=0;mode<7;mode++){states.assign(scheduler.capacity(),{});indices=direct_states(scene,states,mode);evaluate(d,scene,shader,layout,states,indices,"Directions "+std::to_string(mode),m);}
    // Dense dispatch uses the same original inputs and tests the null-index branch.
    auto sparse=states;for(size_t i=0;i<indices.size();i++){states[i]=sparse[indices[i]];indices[i]=i;}evaluate(d,scene,shader,layout,states,indices,"Dense",m,true);
    evaluate(d,scene,shader,layout,states,{},"Zero work",m);
    // Native attribute offsets are signed and can cancel global vertex IDs.
    // Shift those IDs in the original tables, preserving the physical geometry.
    auto &ds=scene.dscene;std::vector<packed_uint3> saved_tri(ds.tri_vindex.data(),ds.tri_vindex.data()+ds.tri_vindex.size());
    std::vector<KernelObject> saved_objects(ds.objects.data(),ds.objects.data()+ds.objects.size());
    for(size_t i=0;i<ds.tri_vindex.size();i++){ds.tri_vindex[i].x+=17;ds.tri_vindex[i].y+=17;ds.tri_vindex[i].z+=17;}
    for(auto ob:scene.objects){unsigned id=ob->get_device_index();ds.objects[id].position_offset-=17;if(!(ds.object_flag[id]&SD_OBJECT_HAS_CORNER_NORMALS))ds.objects[id].normal_offset-=17;}
    ds.tri_vindex.copy_to_device();ds.objects.copy_to_device();evaluate(d,scene,shader,layout,states,indices,"Signed attribute offsets",m,true);
    std::copy(saved_tri.begin(),saved_tri.end(),ds.tri_vindex.data());std::copy(saved_objects.begin(),saved_objects.end(),ds.objects.data());ds.tri_vindex.copy_to_device();ds.objects.copy_to_device();
    invalid=rejected(d,scene,shader,layout,states);
  }
  require(stats.mem_used==0,"Shader-data test leaked Cycles allocations");require(m.objects.size()==9&&m.smooth&&m.corner&&m.applied&&m.instances&&m.negative&&m.backfacing,"Incomplete surface coverage");
  require(std::all_of(m.projection_axes.begin(),m.projection_axes.end(),[](unsigned n){return n!=0;})&&m.negative_position_offsets&&m.negative_normal_offsets,"Missing differential/signed-offset branch coverage");
  std::ofstream(prefix+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(prefix+".library.txt")<<d.gpu.library_path;
  std::cout<<"{\"passed\":true,\"originalCpuShaderSetup\":true,\"nativeSceneTables\":true,\"cases\":"<<m.cases<<",\"paths\":"<<m.paths<<",\"cameraHits\":"<<m.camera_hits<<",\"hitObjects\":"<<m.objects.size()<<",\"backfacing\":"<<m.backfacing<<",\"smooth\":"<<m.smooth<<",\"cornerNormals\":"<<m.corner<<",\"instanceTransforms\":"<<m.instances<<",\"appliedTransforms\":"<<m.applied<<",\"negativeScale\":"<<m.negative<<",\"projectionAxes\":["<<m.projection_axes[0]<<","<<m.projection_axes[1]<<","<<m.projection_axes[2]<<"],\"negativePositionOffsets\":"<<m.negative_position_offsets<<",\"negativeNormalOffsets\":"<<m.negative_normal_offsets<<",\"integerChecks\":"<<m.int_checks<<",\"floatChecks\":"<<m.float_checks<<",\"stateChecks\":"<<m.state_checks<<",\"untouchedGuardBytes\":"<<m.guard_bytes<<",\"strictFloatFailures\":"<<m.strict_failures<<",\"maximumAbsoluteError\":"<<m.maximum_error<<",\"invalidCasesRejected\":"<<invalid<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
