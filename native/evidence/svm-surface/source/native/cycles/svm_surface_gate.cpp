// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "wavefront_test.h"
#include "wavefront.h"
#include "svm_surface_reference.h"
#include "kernel/svm/node_types.h"
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
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
using namespace ccl;
using namespace wavefront_test;
static_assert(sizeof(SurfaceClosureRecord)==56);
struct OutputField {std::string name,expression;size_t offset,size;bool floating;};
static std::vector<OutputField> output_fields(){
  std::vector<OutputField> out;
#define F(name,fp) out.push_back({#name,"r." #name,offsetof(SurfaceSvmRecord,name),sizeof(SurfaceSvmRecord::name),fp});
  F(count,false) F(remaining,false) F(runtime_flag,false) F(lcg_state,false) F(N,true) F(emission,true) F(transparency,true)
#undef F
  for(unsigned i=0;i<MAX_CLOSURE;i++){
    std::string prefix="closures["+std::to_string(i)+"]";
#define F(name,fp) out.push_back({prefix+"." #name,"r."+prefix+"." #name,offsetof(SurfaceSvmRecord,closures)+i*sizeof(SurfaceClosureRecord)+offsetof(SurfaceClosureRecord,name),sizeof(SurfaceClosureRecord::name),fp});
    F(weight,true) F(sample_weight,true) F(N,true) F(kind,false) F(roughness,true) F(a,true) F(b,true) F(multiscatter,true)
#undef F
  }return out;
}
static std::string probe_source(){
  std::ostringstream s;s<<svm_surface_source();
  s<<"@compute @workgroup_size(64) fn surface_svm_probe(@builtin(global_invocation_id) id:vec3<u32>){\n"
      "if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}\n"
      "let sd=surface_from_ray(index);if(sd.error!=0u){return;}let r=svm_evaluate(index,sd,data_word(D_max_closures),p(23u)+id.x*SVM_CLOSURE_CAPACITY*SVM_CLOSURE_BYTES);if(r.error!=0u){return;}\n"
      "let base=p(4u)+id.x*"<<sizeof(SurfaceSvmRecord)<<"u;\n";
  const char *components[]={".x",".y",".z"};
  auto emit=[&](const OutputField &f,const std::string &base,const std::string &expression,size_t offset){
    for(size_t i=0;i<f.size/4;i++)s<<"store_word("<<base<<"+"<<offset+4*i<<"u,"<<(f.floating?"bitcast<u32>(":"")<<expression<<(f.size>4?components[i]:"")<<(f.floating?")":"")<<");\n";
  };
  auto fs=output_fields();for(auto &f:fs)if(f.offset<offsetof(SurfaceSvmRecord,closures))emit(f,"base",f.expression,f.offset);
  s<<"for(var ci=0u;ci<SVM_CLOSURE_CAPACITY;ci++){let cb=base+"<<offsetof(SurfaceSvmRecord,closures)<<"u+ci*"<<sizeof(SurfaceClosureRecord)<<"u;let c=svm_closure_read(r,ci);\n";
  for(auto &f:fs)if(f.name.starts_with("closures[0]."))emit(f,"cb","c."+f.name.substr(12),f.offset - offsetof(SurfaceSvmRecord,closures));
  s<<"}\n";
  s<<"}\n";return s.str();
}
static cbc::Pipeline pipeline(WebGPUDevice &d,const std::string &source){
  WGPUShaderSourceWGSL wgsl{};wgsl.chain.sType=WGPUSType_ShaderSourceWGSL;wgsl.code={source.data(),source.size()};
  WGPUShaderModuleDescriptor desc{};desc.nextInChain=&wgsl.chain;d.gpu.push_scope();
  cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> module(wgpuDeviceCreateShaderModule(d.gpu.get(),&desc));d.gpu.pop_scope();
  return d.gpu.pipeline(module,d.pipeline_layout,"surface_svm_probe");
}
static constexpr unsigned GRAPH_COUNT=20;
static void prepare(Scene &scene){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);scene.integrator->set_aa_samples(16);
  scene.film->set_use_sample_count(false);scene.camera->set_full_width(64);scene.camera->set_full_height(48);scene.camera->set_screen_size(64,48);scene.camera->compute_auto_viewplane();
  std::vector<Node*> shaders;
  for(unsigned test=0;test<GRAPH_COUNT;test++){
    auto g=make_unique<ShaderGraph>();auto diffuse=g->create_node<DiffuseBsdfNode>();diffuse->set_color(make_float3(.2,.6,.9));diffuse->set_roughness(test==0?0:.43f);
    auto trans=g->create_node<TranslucentBsdfNode>();trans->set_color(make_float3(.6,.2,.1));
    auto transparent=g->create_node<TransparentBsdfNode>();transparent->set_color(make_float3(.7,.8,.9));
    auto emission=g->create_node<EmissionNode>();emission->set_color(make_float3(.1,.4,.7));emission->set_strength(2.3f);
    auto holdout=g->create_node<HoldoutNode>();ShaderOutput *result=diffuse->output("BSDF");
    if(test==2)result=trans->output("BSDF");
    if(test==3)result=transparent->output("BSDF");
    if(test==4)result=emission->output("Emission");
    if(test==5)result=holdout->output("Holdout");
    if(test>=6&&test<=9){
      auto mix=g->create_node<MixClosureNode>();mix->set_fac(.37f);g->connect(diffuse->output("BSDF"),mix->input("Closure1"));g->connect(transparent->output("BSDF"),mix->input("Closure2"));result=mix->output("Closure");
      if(test>=7){auto geo=g->create_node<GeometryNode>();auto checker=g->create_node<CheckerTextureNode>();checker->set_scale(3.7f);g->connect(geo->output("Position"),checker->input("Vector"));g->connect(checker->output("Fac"),mix->input("Fac"));g->connect(checker->output("Color"),diffuse->input("Color"));}
      if(test>=8){auto add=g->create_node<AddClosureNode>();g->connect(result,add->input("Closure1"));g->connect(test==8?emission->output("Emission"):holdout->output("Holdout"),add->input("Closure2"));result=add->output("Closure");}
    }
    if(test>=10&&test<16){auto tc=g->create_node<TextureCoordinateNode>();const char *names[]={"Object","Normal","Reflection","Camera","Window","Object"};
      if(test==15){tc->set_use_transform(true);tc->set_ob_tfm(transform_translate(make_float3(.3,.2,.1))*transform_scale(make_float3(.4,.5,.6)));}
      g->connect(tc->output(names[test-10]),emission->input("Color"));auto add=g->create_node<AddClosureNode>();g->connect(emission->output("Emission"),add->input("Closure1"));g->connect(diffuse->output("BSDF"),add->input("Closure2"));result=add->output("Closure");}
    if(test>=16){const float rough[]={1e-5f,.0001f,.001f,.02f};diffuse->set_roughness(rough[test-16]);}
    g->connect(result,g->output()->input("Surface"));auto sh=scene.create_node<Shader>();sh->set_graph(std::move(g));sh->tag_update(&scene);shaders.push_back(sh);
  }
  auto mesh=[&](){auto m=scene.create_node<Mesh>();array<Node*> material_slots;for(auto sh:shaders)material_slots.push_back_slow(sh);m->set_used_shaders(material_slots);m->resize_mesh(GRAPH_COUNT*3,GRAPH_COUNT);
    for(unsigned t=0;t<GRAPH_COUNT;t++){const float x=float(t%4)*2.3f,y=float(t/4)*2.1f;auto v=m->get_position_for_write();v[3*t]=make_float3(x,y,.2f);v[3*t+1]=make_float3(x+1.3f,y,.4f);v[3*t+2]=make_float3(x,y+1.1f,-.1f);
      for(unsigned j=0;j<3;j++)m->get_triangles()[3*t+j]=3*t+j;
      m->get_shader()[t]=t;m->get_smooth()[t]=t%2;}
    m->tag_triangles_modified();auto n=m->attributes.add(ATTR_STD_VERTEX_NORMAL)->data_for_write<packed_normal>();for(unsigned i=0;i<GRAPH_COUNT*3;i++)n[i]=packed_normal(normalize(make_float3(-.25f+.03f*(i%7),.3f+.02f*(i%5),1)));
    return m;};auto shared=mesh();
  for(unsigned i=0;i<3;i++){auto ob=scene.create_node<Object>();ob->set_geometry(i<2?shared:mesh());ob->set_tfm(transform_translate(make_float3(-4,-4,float(i)*2))*transform_rotate(.17f*i,make_float3(0,1,0))*transform_scale(make_float3(i==1?-1.2f:1.1f,.8f,1.4f)));}
}
static std::vector<int> states_for(Scene &scene,std::vector<IntegratorStateCPU> &states){
  std::vector<int> ids;for(auto ob:scene.objects)for(unsigned t=0;t<GRAPH_COUNT;t++)for(unsigned j=0;j<17;j++){
    unsigned i=ids.size()*3;require(i<states.size(),"SVM fixture capacity");ids.push_back(i);auto &s=states[i];s.isect.object=ob->get_device_index();s.isect.prim=ob->get_geometry()->prim_offset+t;s.isect.type=PRIMITIVE_TRIANGLE;s.isect.t=.1f*j;
    s.isect.u=.05f*(j%9);s.isect.v=.04f*(j%11);s.ray.P=make_float3(.1,.2,-2);s.ray.D=normalize(make_float3(.13f*j-.8f,.4f-.07f*j,j%2?1.f:-1.f));s.ray.time=float(j)/16;s.ray.dP=.0001f*j;s.ray.dD=.0003f*j;
    s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE;s.path.visibility=PATH_RAY_VISIBILITY_CAMERA;s.path.rng_pixel=0x12345678u+i*0x9e3779b9u;s.path.rng_offset=uint16_t(65500u+i*79u);s.path.sample=j*137u;
  }return ids;
}
struct Metrics {unsigned cases=0;uint64_t paths=0,integers=0,floats=0,state_checks=0,guards=0,strict=0;double maximum=0;std::set<uint> closures;};
static void evaluate(WebGPUDevice &d,Scene &scene,cbc::Pipeline &shader,const std::vector<Field> &layout,
    const std::vector<IntegratorStateCPU> &states,const std::vector<int> &ids,const std::string &label,Metrics &m,const std::string &capture="",bool dense=false){
  auto expected=original_surface_svm(scene.dscene,ids,states);upload(d,layout,states);
  device_vector<int> selected(&d,"svm_surface_indices",MEM_READ_WRITE);selected.alloc(ids.size());std::copy(ids.begin(),ids.end(),selected.data());selected.copy_to_device();
  device_vector<uint> output(&d,"svm_surface_output",MEM_READ_WRITE);output.alloc(16+ids.size()*sizeof(SurfaceSvmRecord)/4);std::fill_n(output.data(),output.size(),0xa5a5a5a5u);output.copy_to_device();
  device_vector<uint> scratch(&d,"svm_surface_closures",MEM_READ_WRITE);scratch.alloc(16+ids.size()*MAX_CLOSURE*sizeof(SurfaceClosureRecord)/4);std::fill_n(scratch.data(),scratch.size(),0xa5a5a5a5u);std::fill(scratch.data()+8,scratch.data()+scratch.size()-8,0);scratch.copy_to_device();
  std::array<uint32_t,32> p{};p[1]=ids.size();p[2]=dense?0:selected.device_pointer;p[4]=output.device_pointer+32;p[6]=d.constants.at("data");p[7]=d.constants.at("integrator_state");p[10]=states.size();p[14]=d.scene_tables();p[17]=d.error_word;p[21]=scene.dscene.svm_nodes.device_pointer;p[22]=scene.dscene.svm_nodes.size();p[23]=scratch.device_pointer+32;
  if(!capture.empty()){
    auto staging=d.gpu.buffer(d.arena_bytes,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);wgpuCommandEncoderCopyBufferToBuffer(d.commands(),d.arena,0,staging,0,d.arena_bytes);d.flush();std::vector<uint8_t> arena(d.arena_bytes);d.gpu.read(staging,arena.data(),arena.size());
    std::ofstream(capture+".arena.bin",std::ios::binary).write(reinterpret_cast<const char*>(arena.data()),arena.size());std::ofstream(capture+".params.bin",std::ios::binary).write(reinterpret_cast<const char*>(p.data()),sizeof(p));std::ofstream(capture+".reference.bin",std::ios::binary).write(reinterpret_cast<const char*>(expected.data()),expected.size()*sizeof(SurfaceSvmRecord));
    std::ofstream info(capture+".capture.json");info<<"{\"entry\":\"surface_svm_probe\",\"recordSize\":"<<sizeof(SurfaceSvmRecord)<<",\"count\":"<<ids.size()<<",\"outputOffset\":"<<p[4]<<",\"errorOffset\":"<<p[17]<<",\"closureScratch\":{\"offset\":"<<p[23]<<",\"stride\":"<<MAX_CLOSURE*sizeof(SurfaceClosureRecord)<<",\"recordOffset\":"<<offsetof(SurfaceSvmRecord,closures)<<"},\"fields\":[";bool first=true;
    for(auto &f:output_fields()){if(!first)info<<',';first=false;info<<"{\"name\":\""<<f.name<<"\",\"offset\":"<<f.offset<<",\"size\":"<<f.size<<",\"floating\":"<<(f.floating?"true":"false")<<"}";}info<<"]}\n";
  }
  d.dispatch(p,ids.size()+63,shader);uint error_word=0;d.download(d.error_word,&error_word,4);output.copy_from_device();healthy(d);auto bytes=reinterpret_cast<const uint8_t*>(output.data());
  if(error_word){for(size_t i=0;i<ids.size();i++){uint marker;std::memcpy(&marker,bytes+32+i*sizeof(SurfaceSvmRecord),4);if(marker==0xa5a5a5a5u){throw std::runtime_error(label+" SVM error mask="+std::to_string(error_word)+" first incomplete path="+std::to_string(i)+" shader="+std::to_string(scene.dscene.tri_shader[states[ids[i]].isect.prim]&SHADER_MASK));}}throw std::runtime_error("SVM error word set");}
  for(size_t i=0;i<ids.size();i++){
    for(auto &f:output_fields())for(size_t k=0;k<f.size;k+=4){uint a,e;std::memcpy(&a,bytes+32+i*sizeof(SurfaceSvmRecord)+f.offset+k,4);std::memcpy(&e,reinterpret_cast<const char*>(&expected[i])+f.offset+k,4);
      if(!f.floating){m.integers++;require(a==e,label+" integer "+f.name+" path "+std::to_string(i)+" actual="+std::to_string(a)+" expected="+std::to_string(e));}
      else{m.floats++;float av=std::bit_cast<float>(a),ev=std::bit_cast<float>(e);double error=std::abs(double(av)-ev);m.maximum=std::max(m.maximum,error);m.strict+=error>1e-7+1e-7*std::abs(double(ev));
        if(!(std::isfinite(av)&&std::isfinite(ev)&&error<=5e-5+2e-4*std::abs(double(ev)))){SurfaceSvmRecord got{};std::memcpy(&got,bytes+32+i*sizeof(got),sizeof(got));auto &want=expected[i];std::cerr<<std::hex<<"Oren diagnostic a="<<std::bit_cast<uint>(got.closures[0].a)<<"/"<<std::bit_cast<uint>(want.closures[0].a)<<" b="<<std::bit_cast<uint>(got.closures[0].b)<<"/"<<std::bit_cast<uint>(want.closures[0].b)<<std::dec<<'\n';}
        require(std::isfinite(av)&&std::isfinite(ev)&&error<=5e-5+2e-4*std::abs(double(ev)),label+" float "+f.name+" path "+std::to_string(i)+" component "+std::to_string(k/4)+" actual="+std::to_string(av)+" expected="+std::to_string(ev));}}
    for(unsigned j=0;j<expected[i].count;j++)m.closures.insert(expected[i].closures[j].kind);
  }
  for(unsigned i=0;i<32;i++){require(bytes[i]==0xa5&&bytes[output.size()*4-1-i]==0xa5,label+" output guard");m.guards+=2;}
  scratch.copy_from_device();healthy(d);auto sb=reinterpret_cast<const uint8_t*>(scratch.data());
  for(unsigned i=0;i<32;i++){require(sb[i]==0xa5&&sb[scratch.size()*4-1-i]==0xa5,label+" closure scratch guard");m.guards+=2;}
  for(size_t i=0;i<ids.size();i++)require(!std::memcmp(sb+32+i*MAX_CLOSURE*sizeof(SurfaceClosureRecord),bytes+32+i*sizeof(SurfaceSvmRecord)+offsetof(SurfaceSvmRecord,closures),MAX_CLOSURE*sizeof(SurfaceClosureRecord)),label+" closure scratch mismatch");
  auto unchanged=download(d,layout,states.size());for(auto &f:layout)for(size_t i=0;i<states.size();i++){require(!std::memcmp(reinterpret_cast<const char*>(&unchanged[i])+f.offset,reinterpret_cast<const char*>(&states[i])+f.offset,f.stride),label+" changed path state "+f.name);m.state_checks++;}
  m.cases++;m.paths+=ids.size();std::cerr<<"Passed "<<label<<" ("<<ids.size()<<" paths)\n";
}
static SVMInputFloat value(float f){return {std::bit_cast<uint>(f)};}
static SVMInputFloat3 color(float x,float y,float z){return {value(x),value(y),value(z)};}
struct Program {
  std::vector<uint> words;
  Program(DeviceScene &scene):words(scene.data.max_shaders*4,uint(NODE_END)){for(int i=0;i<scene.data.max_shaders;i++){words[i*4]=NODE_SHADER_JUMP;words[i*4+1]=words.size();}}
  template<class T>void add(uint op,const T &n){words.push_back(op);auto b=std::bit_cast<std::array<uint,sizeof(T)/4>>(n);words.insert(words.end(),b.begin(),b.end());}
  void weight(float x,float y,float z){add(NODE_CLOSURE_SET_WEIGHT,SVMNodeClosureSetWeight{.rgb=make_float3(x,y,z)});}
  void diffuse(float roughness,SVMStackOffset normal=SVM_STACK_INVALID){add(NODE_CLOSURE_BSDF,SVMNodeClosureBsdf{.closure_type=CLOSURE_BSDF_DIFFUSE_ID,.mix_weight_offset=SVM_STACK_INVALID});auto n=SVMNodeDiffuseBsdfData{.color=color(.2,.5,.9),.roughness=value(roughness),.normal_offset=normal};auto b=std::bit_cast<std::array<uint,sizeof(n)/4>>(n);words.insert(words.end(),b.begin(),b.end());}
  void simple(ClosureType type,SVMStackOffset normal=SVM_STACK_INVALID){add(NODE_CLOSURE_BSDF,SVMNodeClosureBsdf{.closure_type=type,.mix_weight_offset=SVM_STACK_INVALID});auto n=SVMNodeSimpleBsdfData{.normal_offset=normal};auto b=std::bit_cast<std::array<uint,sizeof(n)/4>>(n);words.insert(words.end(),b.begin(),b.end());}
  void install(DeviceScene &s){words.push_back(NODE_END);s.svm_nodes.alloc(words.size());std::copy(words.begin(),words.end(),s.svm_nodes.data());s.svm_nodes.copy_to_device();}
};
static unsigned rejections(WebGPUDevice &d,Scene &scene,cbc::Pipeline &shader){
  auto &ds=scene.dscene;
  for(unsigned test=0;test<11;test++){
    Program program(ds);program.weight(.2,.4,.6);
    if(test==0)program.simple(CLOSURE_BSDF_PRINCIPLED_ID);
    if(test==1)program.words.push_back(NODE_TEX_IMAGE);
    if(test==2)program.diffuse(0,253);
    if(test==3)program.diffuse(0);
    if(test==4)program.diffuse(.4f);
    if(test==5)program.add(NODE_GEOMETRY,SVMNodeGeometry{.geom_type=NODE_GEOM_T,.out_offset=0});
    if(test==6)program.add(NODE_TEX_COORD,SVMNodeTexCoord{.texco_type=NODE_TEXCO_VOLUME_GENERATED,.out_offset=0});
    if(test==7)program.add(NODE_CLOSURE_HOLDOUT,SVMNodeClosureHoldout{.mix_weight_offset=7});
    program.install(ds);ds.data.max_closures=test==3?MAX_CLOSURE+1:MAX_CLOSURE;d.const_copy_to("data",&ds.data,sizeof(KernelData));
    device_vector<uint> out(&d,"invalid_surface_svm",MEM_READ_WRITE);out.alloc(sizeof(SurfaceSvmRecord)/4);std::fill_n(out.data(),out.size(),0xa5a5a5a5u);out.copy_to_device();
    device_vector<uint> scratch(&d,"invalid_surface_closures",MEM_READ_WRITE);scratch.alloc(MAX_CLOSURE*sizeof(SurfaceClosureRecord)/4);scratch.zero_to_device();
    std::array<uint32_t,32> p{};p[1]=1;p[4]=out.device_pointer;p[6]=d.constants.at("data");p[7]=d.constants.at("integrator_state");p[10]=1;p[14]=d.scene_tables();p[17]=d.error_word;p[21]=ds.svm_nodes.device_pointer;p[22]=ds.svm_nodes.size()-(test==4?2:0);p[23]=scratch.device_pointer;
    if(test==8)p[23]=0;
    if(test==9)p[23]++;
    if(test==10)p[23]=d.arena_bytes-4;
    d.dispatch(p,1,shader);uint error=0;d.download(d.error_word,&error,4);const uint expected[]={16,2,8,16,4,16,16,8,1,1,1};require(error==expected[test],"Surface SVM rejection mask "+std::to_string(test)+" actual="+std::to_string(error));
    out.copy_from_device();healthy(d);require(std::all_of(out.data(),out.data()+out.size(),[](uint v){return v==0xa5a5a5a5u;}),"Rejected SVM wrote output");d.zero(d.error_word,4);
  }return 11;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2,"Usage: svm_surface_gate OUTPUT_PREFIX");std::string prefix=argv[1];auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU device");Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=*dynamic_cast<WebGPUDevice*>(owner.get());Metrics m;unsigned invalid=0;
  {SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;Scene scene(params,&d);prepare(scene);Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Surface SVM scene update failed");
    auto &ds=scene.dscene;ds.data.integrator.use_light_mis=0;ds.data.integrator.use_caustics=0;
    auto limit=[&](int n){ds.data.max_closures=n;d.const_copy_to("data",&ds.data,sizeof(KernelData));};limit(MAX_CLOSURE);
    bool cancelled=false;Scheduler scheduler(&d,scene.film,&ds,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();auto layout=fields(d);std::vector<IntegratorStateCPU> initial(scheduler.capacity());auto ids=states_for(scene,initial);
    auto source=probe_source();std::ofstream(prefix+".wgsl")<<source;auto shader=pipeline(d,source);
    evaluate(d,scene,shader,layout,initial,ids,"Native compiled graphs",m,prefix);
    for(int cap:{0,1,2}){limit(cap);evaluate(d,scene,shader,layout,initial,ids,"Closure limit "+std::to_string(cap),m);}limit(MAX_CLOSURE);
    for(unsigned mode=0;mode<3;mode++){auto states=initial;for(int i:ids){if(mode==0)states[i].path.flag=PATH_RAY_TERMINATE;if(mode==1)states[i].path.flag=PATH_RAY_EMISSION;if(mode==2)states[i].path.visibility=PATH_RAY_VISIBILITY_SHADOW;}evaluate(d,scene,shader,layout,states,ids,"Evaluation mode "+std::to_string(mode),m);}
    auto dense=initial;std::vector<int> dense_ids;for(size_t i=0;i<ids.size();i++){dense[i]=initial[ids[i]];dense_ids.push_back(i);}evaluate(d,scene,shader,layout,dense,dense_ids,"Dense indices",m,"",true);evaluate(d,scene,shader,layout,initial,{},"Zero work",m);
    std::vector<int> small(ids.begin(),ids.begin()+51);
    for(unsigned test=0;test<14;test++){
      Program p(ds);p.weight(.2,.5,.9);
      if(test==0){for(unsigned j=0;j<70;j++)p.diffuse(0);p.add(NODE_CLOSURE_HOLDOUT,SVMNodeClosureHoldout{.mix_weight_offset=SVM_STACK_INVALID});p.simple(CLOSURE_BSDF_TRANSPARENT_ID);}
      if(test==1){p.simple(CLOSURE_BSDF_TRANSPARENT_ID);p.weight(-.5,-.6,-.7);p.simple(CLOSURE_BSDF_TRANSPARENT_ID);p.diffuse(0);}
      if(test==2){for(float f:{0.f,1e-7f,1e-5f,-1.f}){p.weight(f,f,f);p.diffuse(0);p.simple(CLOSURE_BSDF_TRANSPARENT_ID);}p.add(NODE_CLOSURE_HOLDOUT,SVMNodeClosureHoldout{.mix_weight_offset=SVM_STACK_INVALID});}
      if(test==3){p.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=0,.value=make_float3(.3,.4,.5)});p.add(NODE_CLOSURE_SET_NORMAL,SVMNodeClosureSetNormal{.direction_offset=0,.normal_offset=3});p.diffuse(0,3);p.simple(CLOSURE_BSDF_TRANSLUCENT_ID,3);}
      if(test==4){p.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=0,.value=make_float3(0,0,0)});p.diffuse(0,0);p.simple(CLOSURE_BSDF_TRANSLUCENT_ID,0);}
      if(test>=5){const float rough[]={.001f,.02f,1.f,2.f,-1.f,9.999e-6f,1e-5f,1.0001e-5f,.0001f};p.diffuse(rough[test-5]);}
      p.install(ds);evaluate(d,scene,shader,layout,initial,small,"Typed program "+std::to_string(test),m);
    }
    invalid=rejections(d,scene,shader);
  }
  require(stats.mem_used==0,"Surface SVM allocation leak");require(m.closures.size()==5,"Missing closure families");std::ofstream(prefix+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(prefix+".library.txt")<<d.gpu.library_path;
  std::cout<<"{\"passed\":true,\"originalCpuSurfaceSvm\":true,\"compiledGraphs\":"<<GRAPH_COUNT<<",\"cases\":"<<m.cases<<",\"paths\":"<<m.paths<<",\"closureFamilies\":"<<m.closures.size()<<",\"integerChecks\":"<<m.integers<<",\"floatChecks\":"<<m.floats<<",\"stateChecks\":"<<m.state_checks<<",\"untouchedGuardBytes\":"<<m.guards<<",\"strictFloatFailures\":"<<m.strict<<",\"maximumAbsoluteError\":"<<m.maximum<<",\"invalidCasesRejected\":"<<invalid<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
