// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "surface_svm_test.h"
#include "object_motion_fixture.h"
#include "deforming_motion_fixture.h"
#include "scene/attribute.h"
#include "scene/shader.h"
#include <filesystem>
static constexpr unsigned normal_graphs=40, graph_count=48;
static void prepare_normals(Scene &scene){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);
  scene.integrator->set_use_adaptive_sampling(false);scene.integrator->set_aa_samples(16);
  scene.film->set_use_sample_count(false);scene.camera->set_full_width(64);scene.camera->set_full_height(48);
  scene.camera->set_screen_size(64,48);scene.camera->compute_auto_viewplane();array<Node*> shaders;
  for(unsigned i=0;i<graph_count;i++){
    auto g=make_unique<ShaderGraph>();ShaderOutput *output=nullptr;
    if(i<normal_graphs){auto n=g->create_node<NormalMapNode>();n->set_space(NodeNormalMapSpace(i/8));
      n->set_convention(NodeNormalMapConvention((i/4)%2));n->set_base(NodeNormalMapBase((i/2)%2));
      n->set_strength(i%2?1.7f:.35f);n->set_color(make_float3(.76f,.31f,.91f));
      if(i%2)n->set_attribute(ustring("named_uv"));
      output=n->output("Normal");
    }else if(i<46){auto n=g->create_node<TangentNode>();unsigned j=i-normal_graphs;
      n->set_direction_type(j<3?NODE_TANGENT_RADIAL:NODE_TANGENT_UVMAP);n->set_axis(NodeTangentAxis(j%3));
      if(j>=4)n->set_attribute(ustring(j==4?"named_uv":"missing_uv"));
      output=n->output("Tangent");
    }else{auto a=g->create_node<AttributeNode>();a->set_attribute(ustring(i==46?"named_uv":"bad_scalar"));
      output=a->output("Color");}
    auto e=g->create_node<EmissionNode>();g->connect(output,e->input("Color"));g->connect(e->output("Emission"),g->output()->input("Surface"));
    auto sh=scene.create_node<Shader>();sh->set_graph(std::move(g));sh->tag_update(&scene);shaders.push_back_slow(sh);
  }
  auto mesh=[&](unsigned variant){auto m=scene.create_node<Mesh>();array<Node*> slots;for(auto sh:shaders)slots.push_back_slow(sh);m->set_used_shaders(slots);m->resize_mesh(graph_count*4,graph_count*2);
    for(unsigned i=0;i<graph_count;i++){float x=float(i%8)*2,y=float(i/8)*2;auto v=m->get_position_for_write();
      v[4*i]=make_float3(x,y,.1f);v[4*i+1]=make_float3(x+1,y,.2f);v[4*i+2]=make_float3(x,y+1,-.1f);v[4*i+3]=make_float3(x+1,y+1,.3f);
      const int indices[]={0,1,2,2,1,3};for(unsigned j=0;j<6;j++)m->get_triangles()[6*i+j]=4*i+indices[j];
      m->get_shader()[2*i]=i;m->get_shader()[2*i+1]=i;m->get_smooth()[2*i]=false;m->get_smooth()[2*i+1]=true;
    }m->tag_triangles_modified();
    if(variant==2){auto normals=m->attributes.add(ATTR_STD_CORNER_NORMAL)->data_for_write<packed_normal>();
      for(unsigned i=0;i<graph_count*6;i++)normals[i]=packed_normal(normalize(make_float3(.05f*(i%5)-.2f,.03f*(i%7),1)));}
    // Separate original/displaced normals exercise the original-base interpolation.
    if(variant!=1){auto original=m->attributes.add(ATTR_STD_NORMAL_UNDISPLACED)->data_for_write<packed_normal>();
      for(int i=0;i<m->attributes.find(ATTR_STD_NORMAL_UNDISPLACED)->size;i++)original[i]=packed_normal(normalize(make_float3(.1f+.03f*(i%3),-.2f,1)));}
    auto generated=m->attributes.add(ATTR_STD_GENERATED)->data_for_write<packed_float3>();
    for(unsigned i=0;i<graph_count*4;i++)generated[i]=make_float3(.1f+.12f*(i%5),.2f+.08f*(i%7),.3f+.1f*(i%3));
    auto uv=m->attributes.add(ATTR_STD_UV)->data_for_write<float2>();
    auto named=m->attributes.add(ustring("named_uv"),TypeFloat2,ATTR_ELEMENT_CORNER)->data_for_write<float2>();
    const float2 corners[]={make_float2(0,0),make_float2(1,0),make_float2(0,1),make_float2(0,1),make_float2(1,0),make_float2(1,1)};
    for(unsigned i=0;i<graph_count*6;i++){uv[i]=corners[i%6];if((i/6)%2)uv[i].x=-uv[i].x;named[i]=make_float2(.2f+.7f*uv[i].y,-.1f+1.2f*uv[i].x);}
    m->attributes.add(ustring("bad_scalar"),TypeFloat,ATTR_ELEMENT_MESH)->data_for_write<float>()[0]=.4f;
    return m;
  };auto shared=mesh(0);
  for(unsigned i=0;i<4;i++){auto ob=scene.create_node<Object>();ob->set_geometry(i<2?shared:mesh(i-1));
    ob->set_tfm(transform_translate(make_float3(-3,-2,i*3))*transform_rotate(.17f*i,make_float3(0,1,0))*transform_scale(make_float3(i%2?-1.2f:1.1f,.8f,1.4f)));}
}
static std::vector<int> normal_states(Scene &scene,std::vector<IntegratorStateCPU> &states){std::vector<int> ids;
  for(auto ob:scene.objects)for(unsigned t=0;t<graph_count*2;t++)for(unsigned j=0;j<3;j++){
    unsigned i=ids.size()*3;require(i<states.size(),"Normal fixture capacity");ids.push_back(i);auto &s=states[i];
    s.isect.object=ob->get_device_index();s.isect.prim=ob->get_geometry()->prim_offset+t;s.isect.type=scene.dscene.objects[s.isect.object].primitive_type;
    const float u[]={0,.23f,1},v[]={0,.37f,0};s.isect.u=u[j];s.isect.v=v[j];s.isect.t=.25f*j;
    s.ray.P=make_float3(.1,.2,-2);s.ray.D=normalize(make_float3(.13f*j-.8f,.4f-.07f*j,j%2?1.f:-1.f));
    s.ray.time=.2f*j;s.ray.dP=.007f*(j+1);s.ray.dD=.013f*j;
    s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE;s.path.visibility=PATH_RAY_VISIBILITY_CAMERA;
    s.path.rng_pixel=0x12345678u+i*0x9e3779b9u;s.path.rng_offset=uint16_t(i*79u);s.path.sample=j*137u;
  }return ids;
}
static void emit_stack(Program &p,unsigned offset){p.add(NODE_CLOSURE_WEIGHT,SVMNodeClosureWeight{.weight_offset=SVMStackOffset(offset)});p.add(NODE_CLOSURE_EMISSION,SVMNodeClosureEmission{.mix_weight_offset=SVM_STACK_INVALID});}
static unsigned rejections(WebGPUDevice &d,Scene &scene,cbc::Pipeline &shader){auto &ds=scene.dscene;
  int scalar=int(scene.shader_manager->get_attribute_id(ustring("bad_scalar"))),uv=int(scene.shader_manager->get_attribute_id(ustring("named_uv")));
  std::vector<float2> saved_uv(ds.attributes_float2.data(),ds.attributes_float2.data()+ds.attributes_float2.size());
  for(unsigned test=0;test<10;test++){
    Program program(ds);
    if(test<5){SVMNodeNormalMap n{.space=NODE_NORMAL_MAP_WORLD,.color=color(.8,.4,.9),.strength=value(.5),.normal_offset=0};
      if(test==0)n.space=NodeNormalMapSpace(99);
      if(test==1)n.invert_green=2;
      if(test==2)n.use_original_base=2;
      if(test==3)n.normal_offset=253;
      if(test==4){n.space=NODE_NORMAL_MAP_TANGENT;n.attr=scalar;n.attr_sign=scalar;}program.add(NODE_NORMAL_MAP,n);
    }else{SVMNodeTangent n{.direction_type=NODE_TANGENT_UVMAP,.axis=NODE_TANGENT_AXIS_X,.attr=uv,.tangent_offset=0};
      if(test==5)n.direction_type=NodeTangentDirectionType(99);
      if(test==6)n.axis=NodeTangentAxis(99);
      if(test==7)n.attr=scalar;
      if(test==8)n.tangent_offset=250;
      program.add(test==8?NODE_TANGENT_DERIVATIVE:NODE_TANGENT,n);
    }emit_stack(program,0);program.install(ds);
    if(test==9){std::fill_n(ds.attributes_float2.data(),ds.attributes_float2.size(),make_float2(0,0));ds.attributes_float2.copy_to_device();}
    device_vector<uint> output(&d,"normal_rejection_output",MEM_READ_WRITE);output.alloc(sizeof(SurfaceSvmRecord)/4);std::fill_n(output.data(),output.size(),0xa5a5a5a5u);output.copy_to_device();
    device_vector<uint> scratch(&d,"normal_rejection_scratch",MEM_READ_WRITE);scratch.alloc(MAX_CLOSURE*sizeof(SurfaceClosureRecord)/4);scratch.zero_to_device();
    std::array<uint32_t,32> p{};p[1]=1;p[4]=output.device_pointer;p[6]=d.constants.at("data");p[7]=d.constants.at("integrator_state");p[10]=d.remaining(uintptr_t(d.state.path.render_pixel_index))/4;p[14]=d.scene_tables();p[17]=d.error_word;p[21]=ds.svm_nodes.device_pointer;p[22]=ds.svm_nodes.size();p[23]=scratch.device_pointer;
    PreparedSurfaceCache cache(d,p);
    d.dispatch(p,1,shader);uint error=0;d.download(d.error_word,&error,4);output.copy_from_device();healthy(d);
    require(error==((test==3||test==8)?8u:16u),"Normal rejection "+std::to_string(test)+" error="+std::to_string(error));
    require(std::all_of(output.data(),output.data()+output.size(),[](uint v){return v==0xa5a5a5a5u;}),"Invalid normal wrote output");cache.verify();d.zero(d.error_word,4);
    std::copy(saved_uv.begin(),saved_uv.end(),ds.attributes_float2.data());ds.attributes_float2.copy_to_device();
  }return 10;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2||(argc==3&&(std::string(argv[2])=="--motion"||std::string(argv[2])=="--deform"||std::string(argv[2])=="--deform-motion")),"Usage: normal_gate OUTPUT_PREFIX [--motion|--deform|--deform-motion]");const bool deform=argc==3&&std::string(argv[2])!="--motion";const bool motion=argc==3&&std::string(argv[2])!="--deform";std::string prefix=argv[1];auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU device");
  Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=require_webgpu_device(owner.get(),"normal_gate device creation");Metrics m;unsigned typed=0,rejected=0;
  {SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;Scene scene(params,&d);prepare_normals(scene);if(motion)for(size_t i=0;i<scene.objects.size();i++)object_motion_fixture(scene,*scene.objects[i],unsigned(i));if(deform)for(size_t i=0;i<scene.objects.size();i++)deforming_motion_fixture(scene,*scene.objects[i],unsigned(i));
    Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Normal scene update failed");auto &ds=scene.dscene;
    ds.data.integrator.use_light_mis=0;ds.data.integrator.use_caustics=0;ds.data.max_closures=MAX_CLOSURE;d.const_copy_to("data",&ds.data,sizeof(KernelData));
    bool cancelled=false;Scheduler scheduler(&d,scene.film,&ds,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();auto layout=fields(d);std::vector<IntegratorStateCPU> initial(scheduler.capacity());auto ids=normal_states(scene,initial);if(motion||deform)for(int i:ids)initial[i].ray.time*=2.5f;
    auto source=probe_source();std::ofstream(prefix+".wgsl")<<source;auto shader=pipeline(d,source);
    evaluate(d,scene,shader,layout,initial,ids,"Native normal/tangent graphs",m,prefix);
    std::vector<int> small;for(unsigned ob=0;ob<4;ob++)for(unsigned j=0;j<6;j++)small.push_back(ids[ob*graph_count*6+j]);
    auto id=[&](AttributeStandard attr){return int(scene.shader_manager->get_attribute_id(attr));};
    for(unsigned space=0;space<5;space++)for(unsigned invert=0;invert<2;invert++)for(unsigned original=0;original<2;original++)for(float strength:{-1.f,0.f,.35f,1.f,2.f})for(unsigned color_case=0;color_case<3;color_case++){
      Program p(ds);SVMNodeNormalMap n{.space=NodeNormalMapSpace(space),.invert_green=int(invert),.use_original_base=int(original),.attr=id(original?ATTR_STD_UV_TANGENT_UNDISPLACED:ATTR_STD_UV_TANGENT),.attr_sign=id(original?ATTR_STD_UV_TANGENT_SIGN_UNDISPLACED:ATTR_STD_UV_TANGENT_SIGN),.color=color_case==0?color(.5,.5,.5):color_case==1?color(.9,.1,.8):color(-.2,.7,1.3),.strength=value(strength),.normal_offset=0};
      p.add(NODE_NORMAL_MAP,n);emit_stack(p,0);p.diffuse(.3,0);p.install(ds);
      evaluate(d,scene,shader,layout,initial,small,"Normal space "+std::to_string(space)+" strength "+std::to_string(strength)+" color "+std::to_string(color_case)+" original "+std::to_string(original),m);typed++;
    }
    for(unsigned missing=0;missing<2;missing++){
      Program p(ds);p.add(NODE_NORMAL_MAP,SVMNodeNormalMap{.space=NODE_NORMAL_MAP_TANGENT,
        .attr=missing?id(ATTR_STD_UV_TANGENT):0x1234567,.attr_sign=missing?0x1234567:id(ATTR_STD_UV_TANGENT_SIGN),
        .color=color(.8,.3,.9),.strength=value(.7),.normal_offset=0});emit_stack(p,0);p.install(ds);
      evaluate(d,scene,shader,layout,initial,small,"Missing normal-map attribute "+std::to_string(missing),m);typed++;
    }
    const int attrs[]={id(ATTR_STD_UV_TANGENT),id(ATTR_STD_GENERATED),int(scene.shader_manager->get_attribute_id(ustring("named_uv"))),int(scene.shader_manager->get_attribute_id(ustring("missing_uv")))};
    for(int attr:attrs)for(unsigned kind=0;kind<2;kind++)for(unsigned axis=0;axis<3;axis++)for(unsigned output=0;output<4;output++){
      Program p(ds);p.add(output?NODE_TANGENT_DERIVATIVE:NODE_TANGENT,SVMNodeTangent{.direction_type=NodeTangentDirectionType(kind),.axis=NodeTangentAxis(axis),.attr=attr,.tangent_offset=0});emit_stack(p,output>=2?3*(output-1):0);p.install(ds);
      std::string capture;
      // Keep a representative set of derivative captures for device replay.
      if(attr==attrs[0]&&kind==NODE_TANGENT_UVMAP&&axis==0&&output>0){auto folder=std::filesystem::path(prefix).parent_path()/ ("dual-"+std::to_string(output));std::filesystem::create_directories(folder);capture=(folder/"run").string();std::ofstream(capture+".wgsl")<<source;}
      if(!capture.empty()&&output>=2){auto ref=original_surface_svm(ds,small,initial);require(std::any_of(ref.begin(),ref.end(),[](const auto &r){return r.emission.x!=0||r.emission.y!=0||r.emission.z!=0;}),"Derivative capture must contain nonzero original-CPU derivatives");}
      evaluate(d,scene,shader,layout,initial,small,"Tangent attr "+std::to_string(attr)+" kind "+std::to_string(kind)+" axis "+std::to_string(axis)+" output "+std::to_string(output),m,capture);typed++;
    }
    rejected=rejections(d,scene,shader);
  }
  require(stats.mem_used==0,"Normal allocation leak");std::ofstream(prefix+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(prefix+".library.txt")<<d.gpu.library_path;
  std::cout<<"{\"passed\":true,\"originalCpuSurfaceSvm\":true,\"nativeGraphs\":"<<graph_count<<",\"typedPrograms\":"<<typed<<",\"cases\":"<<m.cases<<",\"paths\":"<<m.paths<<",\"integerChecks\":"<<m.integers<<",\"floatChecks\":"<<m.floats<<",\"stateChecks\":"<<m.state_checks<<",\"strictFloatFailures\":"<<m.strict<<",\"maximumAbsoluteError\":"<<m.maximum<<",\"invalidCasesRejected\":"<<rejected<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
