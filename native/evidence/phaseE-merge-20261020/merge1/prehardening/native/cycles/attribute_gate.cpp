// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "surface_svm_test.h"
#include "scene/attribute.h"
#include "scene/shader.h"
#include "util/param.h"
#include <span>

struct AttributeSpec {const char *name;TypeDesc type;AttributeElement element;};
static const AttributeSpec specs[]={
  {"vertex_float",TypeFloat,ATTR_ELEMENT_VERTEX},
  {"vertex_float2",TypeFloat2,ATTR_ELEMENT_VERTEX},
  {"vertex_color",TypeColor,ATTR_ELEMENT_VERTEX},
  {"vertex_float4",TypeFloat4,ATTR_ELEMENT_VERTEX},
  {"corner_byte",TypeRGBA,ATTR_ELEMENT_CORNER_BYTE},
  {"mesh_color",TypeColor,ATTR_ELEMENT_MESH},
  {"face_float",TypeFloat,ATTR_ELEMENT_FACE},
  {"corner_uv",TypeFloat2,ATTR_ELEMENT_CORNER},
  {"corner_normal",TypeNormal,ATTR_ELEMENT_CORNER_NORMAL},
  {"missing",TypeFloat,ATTR_ELEMENT_NONE},
  {"object_color",TypeColor,ATTR_ELEMENT_MESH},
  {"vertex_normal",TypeNormal,ATTR_ELEMENT_VERTEX_NORMAL},
  {"face_float4",TypeFloat4,ATTR_ELEMENT_FACE},
  {"corner_float4",TypeFloat4,ATTR_ELEMENT_CORNER},
  {"mesh_float",TypeFloat,ATTR_ELEMENT_MESH},
};
static constexpr unsigned graph_count=std::size(specs)*3+3;

static void prepare_attributes(Scene &scene){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);
  scene.integrator->set_use_adaptive_sampling(false);scene.integrator->set_aa_samples(16);
  scene.film->set_use_sample_count(false);scene.camera->set_full_width(64);scene.camera->set_full_height(48);
  scene.camera->set_screen_size(64,48);scene.camera->compute_auto_viewplane();
  array<Node*> shaders;
  for(unsigned i=0;i<graph_count;i++){
    auto g=make_unique<ShaderGraph>();auto e=g->create_node<EmissionNode>();e->set_strength(.7f);
    ShaderOutput *output=nullptr;
    if(i<std::size(specs)*3){auto a=g->create_node<AttributeNode>();a->set_attribute(ustring(specs[i/3].name));
      const char *outputs[]={"Color","Fac","Alpha"};output=a->output(outputs[i%3]);}
    else if(i<graph_count-1){auto c=g->create_node<VertexColorNode>();c->set_layer_name(ustring(i==graph_count-3?"corner_byte":"vertex_color"));output=c->output("Color");}
    else{auto uv=g->create_node<UVMapNode>();output=uv->output("UV");}
    g->connect(output,e->input("Color"));g->connect(e->output("Emission"),g->output()->input("Surface"));
    auto sh=scene.create_node<Shader>();sh->set_graph(std::move(g));sh->tag_update(&scene);shaders.push_back_slow(sh);
  }
  auto mesh=[&](bool unique=false){
    auto m=scene.create_node<Mesh>();array<Node*> slots;for(auto sh:shaders)slots.push_back_slow(sh);
    m->set_used_shaders(slots);m->resize_mesh(graph_count*4,graph_count*2);
    for(unsigned i=0;i<graph_count;i++){
      float x=float(i%8)*2.f,y=float(i/8)*2.f;auto v=m->get_position_for_write();
      v[4*i]=make_float3(x,y,.1f);v[4*i+1]=make_float3(x+1,y,.2f);
      v[4*i+2]=make_float3(x,y+1,-.1f);v[4*i+3]=make_float3(x+1,y+1,0);
      const int vertices[]={0,1,2,2,1,3};for(unsigned j=0;j<6;j++)m->get_triangles()[6*i+j]=4*i+vertices[j];
      m->get_shader()[2*i]=i;m->get_shader()[2*i+1]=i;m->get_smooth()[2*i]=false;m->get_smooth()[2*i+1]=false;
    }
    m->tag_triangles_modified();
    for(auto &spec:specs){
      if(spec.element==ATTR_ELEMENT_NONE||std::string(spec.name)=="object_color")continue;
      // Only the later mesh owns byte colors: native global corner indices
      // then require a negative descriptor offset into this compact table.
      if(!unique&&std::string(spec.name)=="corner_byte")continue;
      auto a=m->attributes.add(ustring(spec.name),spec.type,spec.element);
      for(int j=0;j<a->size;j++){
        float x=.07f*(j%13),y=.11f*(j%7),z=.03f*(j%17),w=.13f*(j%9);
        if(spec.element&ATTR_ELEMENT_IS_NORMAL)a->data_for_write<packed_normal>()[j]=packed_normal(normalize(make_float3(x-.3f,y-.2f,1)));
        else if(spec.element&ATTR_ELEMENT_IS_BYTE)a->data_for_write<uchar4>()[j]=make_uchar4((j*31)%256,(j*73)%256,(j*101)%256,(j*43)%256);
        else if(spec.type==TypeFloat)a->data_for_write<float>()[j]=x;
        else if(spec.type==TypeFloat2)a->data_for_write<float2>()[j]=make_float2(x,y);
        else if(spec.type==TypeFloat4||spec.type==TypeRGBA)a->data_for_write<float4>()[j]=make_float4(x,y,z,w);
        else a->data_for_write<packed_float3>()[j]=make_float3(x,y,z);
      }
    }
    auto uv=m->attributes.add(ATTR_STD_UV)->data_for_write<float2>();
    for(unsigned j=0;j<graph_count*6;j++)uv[j]=make_float2(.2f*(j%7),.3f*(j%5));
    return m;
  };
  auto shared=mesh();
  for(unsigned i=0;i<3;i++){
    auto ob=scene.create_node<Object>();ob->set_geometry(i<2?shared:mesh(true));
    ob->set_tfm(transform_translate(make_float3(-3,-2,i*3))*transform_rotate(.17f*i,make_float3(0,1,0))*transform_scale(make_float3(i==1?-1.2f:1.1f,.8f,1.4f)));
    float c[]={.2f+.1f*i,.7f-.2f*i,.3f};ob->attributes.emplace_back(ustring("object_color"),TypeColor,1,c);
  }
}
static std::vector<int> attribute_states(Scene &scene,std::vector<IntegratorStateCPU> &states){
  std::vector<int> ids;
  for(auto ob:scene.objects)for(unsigned t=0;t<graph_count*2;t++)for(unsigned j=0;j<4;j++){
    unsigned i=ids.size()*3;require(i<states.size(),"Attribute fixture capacity");ids.push_back(i);auto &s=states[i];
    s.isect.object=ob->get_device_index();s.isect.prim=ob->get_geometry()->prim_offset+t;s.isect.type=PRIMITIVE_TRIANGLE;
    const float u[]={0,.23f,1,.31f},v[]={0,.37f,0,.69f};s.isect.u=u[j];s.isect.v=v[j];s.isect.t=.25f*j;
    s.ray.P=make_float3(.1,.2,-2);s.ray.D=normalize(make_float3(.13f*j-.8f,.4f-.07f*j,j%2?1.f:-1.f));
    s.ray.time=.2f*j;s.ray.dP=.007f*j;s.ray.dD=.013f*j;
    s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE;s.path.visibility=PATH_RAY_VISIBILITY_CAMERA;
    s.path.rng_pixel=0x12345678u+i*0x9e3779b9u;s.path.rng_offset=uint16_t(i*79u);s.path.sample=j*137u;
  }return ids;
}
static void emit_stack(Program &p,unsigned offset,bool vector){
  if(!vector){p.add(NODE_CONVERT,SVMNodeConvert{.convert_type=NODE_CONVERT_FV,.from_offset=SVMStackOffset(offset),.to_offset=30});offset=30;}
  p.add(NODE_CLOSURE_WEIGHT,SVMNodeClosureWeight{.weight_offset=SVMStackOffset(offset)});
  p.add(NODE_CLOSURE_EMISSION,SVMNodeClosureEmission{.mix_weight_offset=SVM_STACK_INVALID});
}
static unsigned attribute_rejections(WebGPUDevice &d,Scene &scene,cbc::Pipeline &shader){
  auto &ds=scene.dscene;auto object=ds.objects[0];auto saved=std::vector<AttributeMap>(ds.attributes_map.data(),ds.attributes_map.data()+ds.attributes_map.size());
  const int attr=int(scene.shader_manager->get_attribute_id(ustring("vertex_float")));
  size_t at=object.attribute_map_offset;
  for(size_t steps=0;steps<saved.size()&&saved.at(at).id!=uint64_t(attr);steps++){
    at=saved.at(at).id==ATTR_STD_NONE?saved.at(at).offset:at+ATTR_PRIM_TYPES;
  }
  require(saved.at(at).id==uint64_t(attr),"Missing native attribute fixture map");
  for(unsigned test=0;test<7;test++){
    Program program(ds);program.add(NODE_ATTR,SVMNodeAttr{.attr=attr,.out_offset=SVMStackOffset(test==5?253:0),.output_type=NodeAttributeOutputType(test==6?255:NODE_ATTR_OUTPUT_FLOAT3)});emit_stack(program,0,true);program.install(ds);
    if(test==0)ds.objects[0].attribute_map_offset=ds.attributes_map.size();
    if(test==1){ds.attributes_map[object.attribute_map_offset]={ATTR_STD_NONE,int(object.attribute_map_offset),1,0,0};}
    if(test==2)ds.attributes_map[at].offset=0x7fffffff;
    if(test==3)ds.attributes_map[at].element=ATTR_ELEMENT_VOXEL;
    if(test==4)ds.attributes_map[at].type=NODE_ATTR_MATRIX;
    ds.objects.copy_to_device();ds.attributes_map.copy_to_device();
    device_vector<uint> output(&d,"attribute_rejection_output",MEM_READ_WRITE);output.alloc(sizeof(SurfaceSvmRecord)/4);std::fill_n(output.data(),output.size(),0xa5a5a5a5u);output.copy_to_device();
    device_vector<uint> scratch(&d,"attribute_rejection_scratch",MEM_READ_WRITE);scratch.alloc(MAX_CLOSURE*sizeof(SurfaceClosureRecord)/4);scratch.zero_to_device();
    std::array<uint32_t,32> p{};p[1]=1;p[4]=output.device_pointer;p[6]=d.constants.at("data");p[7]=d.constants.at("integrator_state");p[10]=d.remaining(uintptr_t(d.state.path.render_pixel_index))/4;p[14]=d.scene_tables();p[17]=d.error_word;p[21]=ds.svm_nodes.device_pointer;p[22]=ds.svm_nodes.size();p[23]=scratch.device_pointer;
    PreparedSurfaceCache cache(d,p);
    d.dispatch(p,1,shader);uint error=0;d.download(d.error_word,&error,4);output.copy_from_device();healthy(d);
    const uint expected[]={64,64,64,16,16,8,16};require(error==expected[test],"Attribute rejection "+std::to_string(test)+" error="+std::to_string(error));
    require(std::all_of(output.data(),output.data()+output.size(),[](uint v){return v==0xa5a5a5a5u;}),"Invalid attribute wrote output");cache.verify();d.zero(d.error_word,4);
    ds.objects[0]=object;std::copy(saved.begin(),saved.end(),ds.attributes_map.data());ds.objects.copy_to_device();ds.attributes_map.copy_to_device();
  }return 7;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2,"Usage: attribute_gate OUTPUT_PREFIX");std::string prefix=argv[1];auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU device");
  Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=*dynamic_cast<WebGPUDevice*>(owner.get());Metrics m;unsigned rejected=0,typed=0,negative_offsets=0,chains=0;
  {SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;Scene scene(params,&d);prepare_attributes(scene);
    Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Attribute scene update failed");auto &ds=scene.dscene;
    ds.data.integrator.use_light_mis=0;ds.data.integrator.use_caustics=0;ds.data.max_closures=MAX_CLOSURE;d.const_copy_to("data",&ds.data,sizeof(KernelData));
    for(auto &a:std::span(ds.attributes_map.data(),ds.attributes_map.size())){negative_offsets+=a.offset<0&&a.offset!=ATTR_STD_NOT_FOUND;chains+=a.id==ATTR_STD_NONE&&a.element!=0;}
    require(negative_offsets>0&&chains>0,"Missing negative offsets or object-to-geometry map chains");
    bool cancelled=false;Scheduler scheduler(&d,scene.film,&ds,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();auto layout=fields(d);std::vector<IntegratorStateCPU> initial(scheduler.capacity());auto ids=attribute_states(scene,initial);
    auto source=probe_source();std::ofstream(prefix+".wgsl")<<source;auto shader=pipeline(d,source);
    evaluate(d,scene,shader,layout,initial,ids,"Native attribute graphs",m,prefix);
    std::vector<int> small;
    for(unsigned ob=0;ob<3;ob++)for(unsigned i=0;i<4;i++)small.push_back(ids[ob*graph_count*8+i]);
    std::vector<int> attributes;for(auto &spec:specs)attributes.push_back(int(scene.shader_manager->get_attribute_id(ustring(spec.name))));
    attributes.push_back(int(scene.shader_manager->get_attribute_id(ATTR_STD_GENERATED)));attributes.push_back(int(scene.shader_manager->get_attribute_id(ATTR_STD_UV)));
    for(int attr:attributes)for(unsigned output=0;output<3;output++)for(unsigned variant=0;variant<6;variant++){
      Program p(ds);bool derivative=variant!=0;bool stored=variant>=3;unsigned bump=variant==1?1:variant==2?2:0;
      p.add(derivative?NODE_ATTR_DERIVATIVE:NODE_ATTR,SVMNodeAttr{.attr=attr,.out_offset=0,.output_type=NodeAttributeOutputType(output),.bump_offset=NodeBumpOffset(bump),.store_derivatives=uint8_t(stored),.bump_filter_width=.73f});
      unsigned offset=variant>=4?(variant-3)*(output==NODE_ATTR_OUTPUT_FLOAT3?3:1):0;emit_stack(p,offset,output==NODE_ATTR_OUTPUT_FLOAT3);p.install(ds);
      evaluate(d,scene,shader,layout,initial,small,"Attribute "+std::to_string(attr)+" output "+std::to_string(output)+" variant "+std::to_string(variant),m);typed++;
    }
    for(const char *name:{"corner_byte","vertex_color","missing"})for(unsigned bump=0;bump<3;bump++)for(unsigned alpha=0;alpha<2;alpha++){
      auto id=scene.shader_manager->get_attribute_id(ustring(name));require(id<256,"Vertex color layer id does not fit native payload");
      Program p(ds);p.add(bump?NODE_VERTEX_COLOR_DERIVATIVE:NODE_VERTEX_COLOR,SVMNodeVertexColor{.layer_id=uint8_t(id),.color_offset=0,.alpha_offset=3,.bump_offset=NodeBumpOffset(bump),.bump_filter_width=.73f});emit_stack(p,alpha?3:0,!alpha);p.install(ds);
      evaluate(d,scene,shader,layout,initial,small,std::string("Vertex color ")+name+" bump "+std::to_string(bump)+" alpha "+std::to_string(alpha),m);typed++;
    }
    auto original_film=ds.data.film;ds.data.film.is_rec709=0;
    ds.data.film.rec709_to_r=make_float4(.8f,.1f,.1f,0);ds.data.film.rec709_to_g=make_float4(.2f,.7f,.1f,0);ds.data.film.rec709_to_b=make_float4(.1f,.2f,.7f,0);
    d.const_copy_to("data",&ds.data,sizeof(KernelData));
    for(unsigned output=0;output<3;output++){
      Program p(ds);p.add(NODE_ATTR_DERIVATIVE,SVMNodeAttr{.attr=int(scene.shader_manager->get_attribute_id(ustring("corner_byte"))),.out_offset=0,.output_type=NodeAttributeOutputType(output),.bump_offset=NODE_BUMP_OFFSET_DX,.store_derivatives=true,.bump_filter_width=.73f});emit_stack(p,0,output==NODE_ATTR_OUTPUT_FLOAT3);p.install(ds);
      evaluate(d,scene,shader,layout,initial,small,"Non-Rec709 byte colors "+std::to_string(output),m);typed++;
    }
    ds.data.film=original_film;d.const_copy_to("data",&ds.data,sizeof(KernelData));
    const int collision_id=attributes[0];
    for(auto &a:std::span(ds.attributes_map.data(),ds.attributes_map.size()))if(a.id==uint64_t(collision_id))a.id|=uint64_t(1)<<32;
    ds.attributes_map.copy_to_device();
    {Program p(ds);p.add(NODE_ATTR,SVMNodeAttr{.attr=collision_id,.out_offset=0,.output_type=NODE_ATTR_OUTPUT_FLOAT3});emit_stack(p,0,true);p.install(ds);evaluate(d,scene,shader,layout,initial,small,"64-bit attribute identity collision",m);typed++;}
    for(auto &a:std::span(ds.attributes_map.data(),ds.attributes_map.size()))if(a.id==(uint64_t(1)<<32|uint64_t(collision_id)))a.id=collision_id;
    ds.attributes_map.copy_to_device();
    rejected=attribute_rejections(d,scene,shader);
  }
  require(stats.mem_used==0,"Attribute allocation leak");std::ofstream(prefix+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(prefix+".library.txt")<<d.gpu.library_path;
  std::cout<<"{\"passed\":true,\"originalCpuSurfaceSvm\":true,\"nativeGraphs\":"<<graph_count<<",\"typedPrograms\":"<<typed<<",\"cases\":"<<m.cases<<",\"paths\":"<<m.paths<<",\"negativeAttributeOffsets\":"<<negative_offsets<<",\"attributeMapChains\":"<<chains<<",\"integerChecks\":"<<m.integers<<",\"floatChecks\":"<<m.floats<<",\"stateChecks\":"<<m.state_checks<<",\"strictFloatFailures\":"<<m.strict<<",\"maximumAbsoluteError\":"<<m.maximum<<",\"invalidCasesRejected\":"<<rejected<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
