// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "surface_svm_test.h"
#include "surface_fixture.h"
#ifdef CBC_PRINCIPLED_FIXTURE
struct PrincipledArgs {float metallic=0,roughness=.5f,ior=1.45f,transmission=0,coat=0,coat_roughness=.1f,coat_ior=1.5f,sheen=0,sheen_roughness=.5f,
  level=.5f,anisotropic=0,rotation=0,alpha=1,emission=0,diffuse_roughness=0,subsurface=0,thin_film=0,film_ior=1.33f;int thin_wall=0;bool multi=true,tinted=false;
  SVMStackOffset normal=SVM_STACK_INVALID,tangent=SVM_STACK_INVALID,coat_normal=SVM_STACK_INVALID,linked=SVM_STACK_INVALID;};
// One NODE_CLOSURE_BSDF Principled record; `linked` routes roughness and the
// base colour through the SVM stack (the encoding the native compiler emits).
static void principled(Program &p,const PrincipledArgs &a){
  p.add(NODE_CLOSURE_BSDF,SVMNodeClosureBsdf{.closure_type=CLOSURE_BSDF_PRINCIPLED_ID,.mix_weight_offset=SVM_STACK_INVALID});
  SVMNodePrincipledBsdfData n{};n.distribution=a.multi?CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID:CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID;
  n.ior=value(a.ior);n.roughness=value(a.roughness);n.sheen_weight=value(a.sheen);n.coat_weight=value(a.coat);n.metallic=value(a.metallic);
  n.transmission_weight=value(a.transmission);n.subsurface_weight=value(a.subsurface);n.base_color=color(.8f,.5f,.2f);n.alpha=value(a.alpha);
  n.diffuse_roughness=value(a.diffuse_roughness);n.normal_offset=a.normal;n.tangent_offset=a.tangent;n.coat_normal_offset=a.coat_normal;
  n.specular_tint=a.tinted?color(.9f,.7f,.5f):color(1,1,1);n.specular_ior_level=value(a.level);n.anisotropic=value(a.anisotropic);n.anisotropic_rotation=value(a.rotation);
  n.emission_color=color(1,.6f,.3f);n.emission_strength=value(a.emission);n.sheen_tint=a.tinted?color(.9f,.5f,.7f):color(1,1,1);n.sheen_roughness=value(a.sheen_roughness);
  n.coat_tint=a.tinted?color(.6f,.8f,.9f):color(1,1,1);n.coat_roughness=value(a.coat_roughness);n.coat_ior=value(a.coat_ior);
  n.subsurface_method=CLOSURE_BSSRDF_RANDOM_WALK_ID;n.subsurface_radius=color(1,.5f,.2f);n.subsurface_scale=value(.05f);n.subsurface_ior=value(1.4f);n.subsurface_anisotropy=value(0);
  n.thin_film_thickness=value(a.thin_film);n.thin_film_ior=value(a.film_ior);n.thin_wall=SVMInputInt{.value=a.thin_wall,.offset=SVM_STACK_INVALID};
  if(a.linked!=SVM_STACK_INVALID){n.roughness={SVM_INPUT_STACK_OFFSET_MASK|a.linked};n.base_color={{SVM_INPUT_STACK_OFFSET_MASK|uint(a.linked+1)},value(0),value(0)};}
  auto b=std::bit_cast<std::array<uint,sizeof(n)/4>>(n);p.words.insert(p.words.end(),b.begin(),b.end());
}
#endif
static unsigned rejections(WebGPUDevice &d,Scene &scene,cbc::Pipeline &shader){
  auto &ds=scene.dscene;
#ifdef CBC_GLASS_FIXTURE
  constexpr unsigned rejection_count=17;
#elif defined(CBC_PRINCIPLED_FIXTURE)
  constexpr unsigned rejection_count=19;
#elif defined(CBC_MICROFACET_FIXTURE)
  constexpr unsigned rejection_count=16;
#else
  constexpr unsigned rejection_count=11;
#endif
  const auto original=ds.data;
  for(unsigned test=0;test<rejection_count;test++){
    ds.data=original;
    Program program(ds);program.weight(.2,.4,.6);
    if(test==0)program.simple(CLOSURE_BSDF_ASHIKHMIN_VELVET_ID); // unsupported closure (Principled is implemented)
    if(test==1)program.words.push_back(NODE_TEX_NOISE);
    if(test==2)program.diffuse(0,253);
    if(test==3)program.diffuse(0);
    if(test==4)program.diffuse(.4f);
    if(test==5)program.add(NODE_GEOMETRY,SVMNodeGeometry{.geom_type=NODE_GEOM_T,.out_offset=0});
    if(test==6)program.add(NODE_TEX_COORD,SVMNodeTexCoord{.texco_type=NODE_TEXCO_VOLUME_GENERATED,.out_offset=0});
    if(test==7)program.add(NODE_CLOSURE_HOLDOUT,SVMNodeClosureHoldout{.mix_weight_offset=7});
#ifdef CBC_MICROFACET_FIXTURE
    if(test==11)program.glossy(CLOSURE_BSDF_MICROFACET_BECKMANN_ID,.3f);
    if(test==12)program.glossy(CLOSURE_BSDF_MICROFACET_GGX_ID,.3f);
    if(test==13)program.glossy(CLOSURE_BSDF_MICROFACET_GGX_ID,.3f,.5f,0,7);
    if(test==14||test==15){program.glossy(CLOSURE_BSDF_MICROFACET_MULTI_GGX_ID,.3f);
      if(test==14)ds.data.tables.ggx_E=ds.lookup_table.size();
      else ds.data.tables.ggx_Eavg=ds.lookup_table.size()-31;}
#endif
#ifdef CBC_PRINCIPLED_FIXTURE
    // Deferred Principled features are refused, never approximated.
    if(test==11)principled(program,{.subsurface=.5f});
    if(test==12)principled(program,{}); // truncated record (bytecode bounds)
    if(test==13){principled(program,{.metallic=1,.thin_film=300});ds.data.tables.thin_film_table=ds.lookup_table.size()-3000;}
    if(test==14){principled(program,{.sheen=1});ds.data.tables.sheen_ltc=ds.lookup_table.size()-1000;}
    if(test==15){principled(program,{.transmission=1});ds.data.tables.ggx_glass_E=ds.lookup_table.size()-4000;}
    if(test==16){principled(program,{.metallic=1});ds.data.tables.ggx_gen_schlick_s=ds.lookup_table.size();}
    if(test==17)principled(program,{.subsurface=1});
    if(test==18)principled(program,{.subsurface=1e-4f});
#endif
#ifdef CBC_GLASS_FIXTURE
    if(test==11)program.glass(.3f,1.5f,100);
    if(test==12)program.refraction(.3f,1.5f);
    if(test==13)program.glass(.3f,1.5f,0,7);
    if(test==14){program.glass(.3f,1.5f);ds.data.tables.ggx_gen_schlick_ior_s=ds.lookup_table.size()-4095;}
    if(test==15)program.glass(.3f,1.5f,0,SVM_STACK_INVALID,CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID);
    if(test==16)program.refraction(.3f,1.5f,SVM_STACK_INVALID,CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID);
#endif
    program.install(ds);ds.data.max_closures=test==3?MAX_CLOSURE+1:MAX_CLOSURE;d.const_copy_to("data",&ds.data,sizeof(KernelData));
    device_vector<uint> out(&d,"invalid_surface_svm",MEM_READ_WRITE);out.alloc(sizeof(SurfaceSvmRecord)/4);std::fill_n(out.data(),out.size(),0xa5a5a5a5u);out.copy_to_device();
    device_vector<uint> scratch(&d,"invalid_surface_closures",MEM_READ_WRITE);scratch.alloc(MAX_CLOSURE*sizeof(SurfaceClosureRecord)/4);scratch.zero_to_device();
    std::array<uint32_t,32> p{};p[1]=1;p[4]=out.device_pointer;p[6]=d.constants.at("data");p[7]=d.constants.at("integrator_state");p[10]=1;p[14]=d.scene_tables();p[17]=d.error_word;p[21]=ds.svm_nodes.device_pointer;p[22]=ds.svm_nodes.size()-(test==4||test==12?2:0);p[23]=scratch.device_pointer;
    if(test==8)p[23]=0;
    if(test==9)p[23]++;
    if(test==10)p[23]=d.arena_bytes-4;
    PreparedSurfaceCache cache(d,p);
    d.dispatch(p,1,shader);uint error=0;d.download(d.error_word,&error,4);const uint expected[]={16,2,8,16,4,16,16,8,1,1,1,
#ifdef CBC_PRINCIPLED_FIXTURE
      16,4,512,512,512,512,16,16
#else
      16,4,8,512,
#ifdef CBC_GLASS_FIXTURE
      16,16
#else
      512
#endif
#endif
    };require(error==expected[test],"Surface SVM rejection mask "+std::to_string(test)+" actual="+std::to_string(error));
    out.copy_from_device();healthy(d);require(std::all_of(out.data(),out.data()+out.size(),[](uint v){return v==0xa5a5a5a5u;}),"Rejected SVM wrote output");cache.verify();d.zero(d.error_word,4);
  }ds.data=original;return rejection_count;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2,"Usage: svm_surface_gate OUTPUT_PREFIX");std::string prefix=argv[1];auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU device");Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=*dynamic_cast<WebGPUDevice*>(owner.get());Metrics m;unsigned invalid=0,host_rejections=0;
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
#ifdef CBC_MICROFACET_FIXTURE
    unsigned typed=0;
    for(auto kind:{CLOSURE_BSDF_MICROFACET_GGX_ID,CLOSURE_BSDF_MICROFACET_MULTI_GGX_ID})
      for(float rough:{0.f,.0001f,.003f,.004f,.1f,.5f,1.f,2.f,-1.f})
        for(float aniso:{-2.f,-.99f,-.3f,0.f,1e-4f,.5f,.99f,2.f}){
          Program p(ds);p.weight(.2,.5,.9);
          p.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=0,.value=make_float3(.8,.3,.2)});
          p.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=3,.value=typed%2?make_float3(.2,.3,1):zero_float3()});
          p.glossy(kind,rough,aniso,.17f*float(typed%11)-.6f,0,3);p.install(ds);
          evaluate(d,scene,shader,layout,initial,small,"Typed microfacet "+std::to_string(typed++),m);
        }
#endif
#ifdef CBC_PRINCIPLED_FIXTURE
    unsigned typed=0;
    auto run=[&](const PrincipledArgs &a){
      Program p(ds);p.weight(.2,.5,.9);
      p.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=0,.value=make_float3(.8,.3,.2)});
      p.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=3,.value=typed%2?make_float3(.2,.3,1):zero_float3()});
      p.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=6,.value=make_float3(-.1,.4,.9)});
      p.add(NODE_VALUE_F,SVMNodeValueF{.value=.37f,.out_offset=9});
      p.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=10,.value=make_float3(.3,.9,.6)});
      principled(p,a);p.install(ds);evaluate(d,scene,shader,layout,initial,small,"Typed principled "+std::to_string(typed++),m);
    };
    // Lobe-by-lobe parameter extremes.
    for(float rough:{0.f,.003f,.004f,.2f,1.f})for(float ior:{1e-6f,.7f,1.f,1.00001f,1.45f,3.f}){run({.roughness=rough,.ior=ior});run({.metallic=1,.roughness=rough,.ior=ior,.tinted=true});}
    for(float rough:{0.f,.05f,.5f,1.f})for(float ior:{.7f,1.f,1.5f,2.4f})for(bool multi:{false,true})run({.roughness=rough,.ior=ior,.transmission=1,.multi=multi,.tinted=multi});
    for(float coat:{1e-6f,.3f,1.f,2.f})for(float coat_ior:{.5f,1.f,1.5f,4.f})run({.coat=coat,.coat_roughness=coat*.3f,.coat_ior=coat_ior,.tinted=coat>1,.coat_normal=coat>1?SVMStackOffset(6):SVMStackOffset(SVM_STACK_INVALID)});
    for(float sheen:{1e-6f,.5f,1.f,3.f})for(float sr:{0.f,.001f,.3f,1.f})run({.sheen=sheen,.sheen_roughness=sr,.tinted=true,.coat_normal=sr>.5f?SVMStackOffset(6):SVMStackOffset(SVM_STACK_INVALID)});
    for(float level:{0.f,.25f,.5f,1.f,4.f})for(float ior:{.7f,1.45f})run({.ior=ior,.level=level,.tinted=level>1});
    for(float aniso:{1e-6f,.5f,1.f})for(float rotation:{0.f,.3f})run({.metallic=.7f,.roughness=.4f,.anisotropic=aniso,.rotation=rotation,.normal=3,.tangent=0});
    for(float alpha:{0.f,.5f,1.f})for(float emission:{0.f,2.f})run({.coat=.5f,.sheen=.5f,.alpha=alpha,.emission=emission,.tinted=true});
    for(float dr:{0.f,9.999e-6f,1e-5f,.5f,1.f})run({.diffuse_roughness=dr,.subsurface=1e-6f,.thin_film=.1f});
    run({.thin_wall=1,.normal=3}); // thin wall without transmission weight
    // Thin-film iridescence on dielectric, metallic and transmissive lobes, including the sub-1 nm blend.
    for(float film:{.1f,.5f,100.f,400.f,1500.f})for(float film_ior:{1.f,1.33f,2.f}){run({.roughness=.3f,.ior=1.5f,.thin_film=film,.film_ior=film_ior});
      run({.metallic=1,.roughness=.2f,.thin_film=film,.film_ior=film_ior,.tinted=film_ior>1.5f});run({.roughness=.1f,.ior=1.4f,.transmission=1,.thin_film=film,.film_ior=film_ior,.multi=film_ior<1.5f});}
    // Thin-walled transmission: smooth (singular / transparent), rough, colored, with and without film.
    for(float rough:{0.f,.002f,.2f,1.f})for(float ior:{1.f,1.5f,2.4f})run({.roughness=rough,.ior=ior,.transmission=1,.thin_film=rough>.5f?300.f:0.f,.thin_wall=1,.tinted=ior>2});
    run({.metallic=.3f,.ior=1.6f,.transmission=.4f,.coat=.6f,.sheen=.7f,.level=.7f,.anisotropic=.6f,.alpha=.8f,.emission=1,.diffuse_roughness=.4f,.tinted=true,.tangent=0,.coat_normal=6,.linked=9});
    for(int cap:{1,2,3,5}){limit(cap);run({.metallic=.3f,.transmission=.4f,.coat=.6f,.sheen=.7f,.alpha=.8f,.tinted=true});}limit(MAX_CLOSURE);
#endif
#ifdef CBC_GLASS_FIXTURE
    unsigned typed=0;
    for(bool glass:{false,true})for(float rough:{0.f,.003f,.004f,.1f,.5f,1.f,2.f,-1.f})for(float ior:{0.f,.7f,1.f,1.00001f,1.5f,2.4f}){
      Program p(ds);p.weight(.2,.5,.9);p.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=0,.value=typed%2?make_float3(.2,.3,1):zero_float3()});
      if(glass)p.glass(rough,ior,0,0);else p.refraction(rough,ior,0);p.install(ds);
      evaluate(d,scene,shader,layout,initial,small,"Typed glass/refraction "+std::to_string(typed++),m);
    }
#endif
    invalid=rejections(d,scene,shader);
#ifdef CBC_PRINCIPLED_FIXTURE
    // Native-layer rejection before any pipeline or dispatch: a clear
    // unsupported-feature message, never an approximation.
    const std::pair<PrincipledArgs,const char*> host_cases[]={{{.subsurface=.5f},"subsurface"},{{.subsurface=1},"subsurface"},{{.subsurface=1e-4f},"subsurface"},
      {{.subsurface=1e-6f,.thin_film=.1f},nullptr},{{.thin_film=250},nullptr},{{.transmission=1,.thin_wall=1},nullptr},{{.thin_wall=1},nullptr}};
    for(auto &[args,reason]:host_cases){Program p(ds);principled(p,args);p.install(ds);std::string message;
      try{d.shader_features();}catch(const std::exception &e){message=e.what();}
      if(reason)require(message.find("Unsupported feature: Principled BSDF")!=std::string::npos&&message.find(reason)!=std::string::npos,"Missing native Principled rejection: "+message);
      else require(message.empty(),"Supported Principled program rejected: "+message);
      host_rejections+=reason!=nullptr;}
    Program restore(ds);restore.diffuse(0);restore.install(ds);d.shader_features();
#endif
  }
  require(stats.mem_used==0,"Surface SVM allocation leak");
#ifdef CBC_GLASS_FIXTURE
  require(m.closures.size()==7,"Missing glass closure families");
#elif defined(CBC_PRINCIPLED_FIXTURE)
  for(uint kind:{uint(CLOSURE_BSDF_DIFFUSE_ID),uint(CLOSURE_BSDF_OREN_NAYAR_ID),uint(CLOSURE_BSDF_SHEEN_ID),uint(CLOSURE_BSDF_MICROFACET_GGX_ID),
                 uint(CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID),uint(CLOSURE_BSDF_TRANSPARENT_ID)})
    require(m.closures.count(kind),"Missing Principled closure family "+std::to_string(kind));
#elif defined(CBC_MICROFACET_FIXTURE)
  require(m.closures.size()==6,"Missing microfacet closure families");
#else
  require(m.closures.size()==5,"Missing closure families");
#endif
  std::ofstream(prefix+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(prefix+".library.txt")<<d.gpu.library_path;
  std::cout<<"{\"passed\":true,\"originalCpuSurfaceSvm\":true,\"compiledGraphs\":"<<GRAPH_COUNT<<",\"cases\":"<<m.cases<<",\"paths\":"<<m.paths<<",\"closureFamilies\":"<<m.closures.size()<<",\"integerChecks\":"<<m.integers<<",\"floatChecks\":"<<m.floats<<",\"stateChecks\":"<<m.state_checks<<",\"untouchedGuardBytes\":"<<m.guards<<",\"strictFloatFailures\":"<<m.strict<<",\"maximumAbsoluteError\":"<<m.maximum<<",\"invalidCasesRejected\":"<<invalid<<",\"nativeUnsupportedFeatureRejections\":"<<host_rejections<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<",\"pipelineReport\":"<<ccl::pipeline_process_report_json()<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
