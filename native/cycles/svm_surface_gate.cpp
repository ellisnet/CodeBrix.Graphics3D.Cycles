// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "pipelines.h"
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
    if(test==1)program.words.push_back(NODE_LIGHT_FALLOFF);
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
    refresh_probe(d,shader);PreparedSurfaceCache cache(d,p);
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
#ifdef CBC_NODES_FIXTURE
// Deferred opcodes report unsupported (2); invalid node parameters report 16 and
// truncated ramp tables report a bytecode bound (4). Nothing is approximated.
static SVMInputFloat stack_in(unsigned i){return {SVM_INPUT_STACK_OFFSET_MASK|i};}
static SVMInputFloat3 stack_in3(unsigned i){return {stack_in(i),{},{}};}
static unsigned node_rejections(WebGPUDevice &d,Scene &scene,cbc::Pipeline &shader){
  auto &ds=scene.dscene;const auto original=ds.data;constexpr unsigned count=21;
  const uint expected[count]={2,2,2,2,16,16,16,4,16,16,16,16,16,2,2,2,2,2,2,2,16};
  for(unsigned test=0;test<count;test++){
    Program program(ds);program.weight(.2,.4,.6);program.add(NODE_GEOMETRY,SVMNodeGeometry{.geom_type=NODE_GEOM_P,.out_offset=0});
    const uint deferred[]={NODE_RAYCAST,NODE_LIGHT_FALLOFF,NODE_IES,NODE_AOV_START};
    if(test<4)program.words.push_back(deferred[test]);
    if(test==4||test==5)program.add(NODE_TEX_NOISE,SVMNodeTexNoise{.dimensions=test==4?5u:3u,.noise_type=NodeNoiseType(test==4?1:7),.normalize=1,.w=value(0),.scale=value(1),.detail=value(2),
      .roughness=value(.5f),.lacunarity=value(2),.offset=value(0),.gain=value(1),.distortion=value(0),.vector=0,.value_offset=3,.color_offset=SVM_STACK_INVALID});
    if(test==6||test==7){program.add(NODE_RGB_RAMP,SVMNodeRGBRamp{.table_size=test==6?1u:1000u,.fac=value(.5f),.interpolate=1,.color_offset=3,.alpha_offset=SVM_STACK_INVALID});for(unsigned i=0;i<8;i++)program.words.push_back(0);}
    if(test==8)program.add(NODE_TEX_WAVE,SVMNodeTexWave{.wave_type=NODE_WAVE_BANDS,.bands_direction=NODE_WAVE_BANDS_DIRECTION_X,.rings_direction=NODE_WAVE_RINGS_DIRECTION_X,.profile=NODE_WAVE_PROFILE_SIN,
      .scale=value(1),.distortion=value(1),.detail=value(1e9f),.dscale=value(1),.droughness=value(.5f),.phase=value(0),.co=0,.color_offset=3,.fac_offset=SVM_STACK_INVALID});
    if(test==9)program.add(NODE_MAPPING,SVMNodeMapping{.mapping_type=NodeMappingType(9),.vector=stack_in3(0),.location=color(0,0,0),.rotation=color(0,0,0),.scale=color(1,1,1),.result_offset=3});
    if(test==10)program.add(NODE_MIX_COLOR,SVMNodeMixColor{.blend_type=NodeMix(77),.a=stack_in3(0),.b=color(1,1,1),.fac=value(.5f),.use_clamp=0,.use_clamp_result=0,.result_offset=3});
    if(test==11)program.add(NODE_TEX_WHITE_NOISE,SVMNodeTexWhiteNoise{.dimensions=0,.vector=stack_in3(0),.w=value(0),.value_offset=3,.color_offset=SVM_STACK_INVALID});
    if(test==12)program.add(NODE_MAP_RANGE,SVMNodeMapRange{.range_type=NodeMapRangeType(9),.value=stack_in(0),.from_min=value(0),.from_max=value(1),.to_min=value(0),.to_max=value(1),.steps=value(4),.result_offset=3});
    const uint more[]={NODE_VECTOR_DISPLACEMENT,NODE_BEVEL,NODE_PARTICLE_INFO,NODE_HAIR_INFO,NODE_TEX_SKY,NODE_RADIAL_TILING,NODE_POINT_INFO};
    if(test>=13&&test<20)program.words.push_back(more[test-13]);
    // Gabor is implemented: an invalid Gabor type reports 16.
    if(test==20)program.add(NODE_TEX_GABOR,SVMNodeTexGabor{.gabor_type=NodeGaborType(2),.orientation_3d=color(1,0,0),.scale=value(1),.frequency=value(2),.anisotropy=value(1),
      .orientation_2d=value(0),.coordinates=0,.value_offset=3,.phase_offset=SVM_STACK_INVALID,.intensity_offset=SVM_STACK_INVALID});
    program.diffuse(0);program.install(ds);ds.data.max_closures=MAX_CLOSURE;d.const_copy_to("data",&ds.data,sizeof(KernelData));
    device_vector<uint> out(&d,"invalid_node_svm",MEM_READ_WRITE);out.alloc(sizeof(SurfaceSvmRecord)/4);std::fill_n(out.data(),out.size(),0xa5a5a5a5u);out.copy_to_device();
    device_vector<uint> scratch(&d,"invalid_node_closures",MEM_READ_WRITE);scratch.alloc(MAX_CLOSURE*sizeof(SurfaceClosureRecord)/4);scratch.zero_to_device();
    std::array<uint32_t,32> p{};p[1]=1;p[4]=out.device_pointer;p[6]=d.constants.at("data");p[7]=d.constants.at("integrator_state");p[10]=1;p[14]=d.scene_tables();p[17]=d.error_word;p[21]=ds.svm_nodes.device_pointer;p[22]=ds.svm_nodes.size();p[23]=scratch.device_pointer;
    refresh_probe(d,shader);PreparedSurfaceCache cache(d,p);
    d.dispatch(p,1,shader);uint error=0;d.download(d.error_word,&error,4);
    require(error==expected[test],"Node SVM rejection mask "+std::to_string(test)+" actual="+std::to_string(error)+" expected="+std::to_string(expected[test]));
    out.copy_from_device();healthy(d);require(std::all_of(out.data(),out.data()+out.size(),[](uint v){return v==0xa5a5a5a5u;}),"Rejected node SVM wrote output");cache.verify();d.zero(d.error_word,4);
  }ds.data=original;return count;
}
#endif
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2,"Usage: svm_surface_gate OUTPUT_PREFIX");std::string prefix=argv[1];auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU device");Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=require_webgpu_device(owner.get(),"svm_surface_gate device creation");Metrics m;unsigned invalid=0,host_rejections=0;
  {SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;Scene scene(params,&d);prepare(scene);Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Surface SVM scene update failed");
    auto &ds=scene.dscene;ds.data.integrator.use_light_mis=0;ds.data.integrator.use_caustics=0;
    auto limit=[&](int n){ds.data.max_closures=n;d.const_copy_to("data",&ds.data,sizeof(KernelData));};limit(MAX_CLOSURE);
    bool cancelled=false;Scheduler scheduler(&d,scene.film,&ds,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();auto layout=fields(d);std::vector<IntegratorStateCPU> initial(scheduler.capacity());auto ids=states_for(scene,initial);
    auto source=probe_source();std::ofstream(prefix+".wgsl")<<gate_probe_wgsl(d,source);auto shader=pipeline(d,source);
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
#ifdef CBC_NODES_FIXTURE
    // Typed programs: every implemented opcode and mode with parameter sweeps.
    // Slot 0 = P, slot 3 = P quantized to 1/64 (exact on both sides) then scaled:
    // hashes of float bits and high-octave noise are ill-conditioned in the input
    // ulp, so inputs are made bit-identical before the node under test. Outputs
    // color 10..12 and value 13 become closure weights (diffuse and translucent).
    unsigned node_typed=0;
    auto begin=[&](Program &p,float3 k=make_float3(1.3f,.9f,1.7f)){p.add(NODE_GEOMETRY,SVMNodeGeometry{.geom_type=NODE_GEOM_P,.out_offset=0});
      p.add(NODE_VECTOR_MATH,SVMNodeVectorMath{.math_type=NODE_VECTOR_MATH_SCALE,.a=stack_in3(0),.b=color(0,0,0),.c=color(0,0,0),.param1=value(64),.value_offset=SVM_STACK_INVALID,.vector_offset=3});
      p.add(NODE_VECTOR_MATH,SVMNodeVectorMath{.math_type=NODE_VECTOR_MATH_FLOOR,.a=stack_in3(3),.b=color(0,0,0),.c=color(0,0,0),.param1=value(1),.value_offset=SVM_STACK_INVALID,.vector_offset=3});
      p.add(NODE_VECTOR_MATH,SVMNodeVectorMath{.math_type=NODE_VECTOR_MATH_SCALE,.a=stack_in3(3),.b=color(0,0,0),.c=color(0,0,0),.param1=value(1.f/64),.value_offset=SVM_STACK_INVALID,.vector_offset=3});
      p.add(NODE_VECTOR_MATH,SVMNodeVectorMath{.math_type=NODE_VECTOR_MATH_MULTIPLY,.a=stack_in3(3),.b=color(k.x,k.y,k.z),.c=color(0,0,0),.param1=value(1),.value_offset=SVM_STACK_INVALID,.vector_offset=3});
      p.add(NODE_VECTOR_MATH,SVMNodeVectorMath{.math_type=NODE_VECTOR_MATH_FRACTION,.a=stack_in3(3),.b=color(0,0,0),.c=color(0,0,0),.param1=value(1),.value_offset=SVM_STACK_INVALID,.vector_offset=6});
      p.add(NODE_SEPARATE_VECTOR,SVMNodeSeparateVector{.vector=stack_in3(6),.vector_index=0,.out_offset=9});};
    auto finish=[&](Program &p,const std::string &name,bool value_out=true){
      p.add(NODE_CLOSURE_WEIGHT,SVMNodeClosureWeight{.weight_offset=10});p.diffuse(0);
      if(value_out){p.add(NODE_CONVERT,SVMNodeConvert{.convert_type=NODE_CONVERT_FV,.from_offset=13,.to_offset=14});p.add(NODE_CLOSURE_WEIGHT,SVMNodeClosureWeight{.weight_offset=14});p.simple(CLOSURE_BSDF_TRANSLUCENT_ID);}
      p.install(ds);evaluate(d,scene,shader,layout,initial,small,"Typed node "+name+" "+std::to_string(node_typed++),m);};
    for(unsigned dims=1;dims<=4;dims++)for(unsigned kind=0;kind<=NODE_NOISE_HETERO_TERRAIN;kind++)for(unsigned variant=0;variant<4;variant++){
      Program p(ds);begin(p,variant==3?make_float3(131072.f,-1048576.f,32768.f):make_float3(1.3f,.9f,1.7f));const float detail[]={0,2.7f,15,1.5f};
      p.add(NODE_TEX_NOISE,SVMNodeTexNoise{.dimensions=dims,.noise_type=NodeNoiseType(kind),.normalize=uint(variant%2),.w=variant==2?stack_in(9):value(.37f),.scale=value(variant==1?4.3f:1.1f),
        .detail=value(detail[variant]),.roughness=variant==2?stack_in(9):value(.55f),.lacunarity=value(variant==1?2.5f:1.9f),.offset=value(variant==2?.4f:1.f),.gain=value(variant==1?1.7f:1.f),
        .distortion=value(variant>=2?.45f:0.f),.vector=3,.value_offset=13,.color_offset=variant==3?SVMStackOffset(SVM_STACK_INVALID):SVMStackOffset(10)});
      if(variant==3)p.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=10,.value=make_float3(.4f,.5f,.6f)});
      finish(p,"noise "+std::to_string(dims)+"d type "+std::to_string(kind)+" variant "+std::to_string(variant));
    }
    for(unsigned dims=1;dims<=4;dims++)for(unsigned feature=0;feature<=NODE_VORONOI_N_SPHERE_RADIUS;feature++)for(unsigned variant=0;variant<(feature<3?4u:2u);variant++){
      // Metric variants for F1/F2/smooth F1; detail/normalize for the others.
      Program p(ds);begin(p,make_float3(1.3f,.9f,1.7f));const unsigned metric=feature<3?variant:0;const float detail[]={0,2.5f,1,15};
      p.add(NODE_TEX_VORONOI,SVMNodeTexVoronoi{.dimensions=dims,.feature=NodeVoronoiFeature(feature),.metric=NodeVoronoiDistanceMetric(metric),.w=stack_in(9),.scale=value(variant%2?3.3f:1.7f),
        .detail=value(feature<3?(dims==4&&feature==2?0.f:detail[(variant+dims)%3]):(variant?2.5f:0.f)),.roughness=value(.65f),.lacunarity=value(2.f),.smoothness=value(feature==2&&variant==1?0.f:.9f),
        .exponent=value(variant==3?.7f:1.6f),.randomness=value(dims==2&&variant==0?.4f:1.f),.normalize=uint8_t(variant%2),.coord=3,.distance_offset=13,.color_offset=10,
        .position_offset=SVM_STACK_INVALID,.w_out_offset=SVM_STACK_INVALID,.radius_offset=SVM_STACK_INVALID});
      if(feature>=3){p.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=10,.value=make_float3(.4f,.5f,.6f)});}
      if(feature==4){p.add(NODE_TEX_VORONOI,SVMNodeTexVoronoi{.dimensions=dims,.feature=NODE_VORONOI_N_SPHERE_RADIUS,.metric=NODE_VORONOI_EUCLIDEAN,.w=stack_in(9),.scale=value(2.1f),.detail=value(0),
        .roughness=value(.5f),.lacunarity=value(2),.smoothness=value(1),.exponent=value(1),.randomness=value(variant?.6f:1.f),.normalize=0,.coord=3,.distance_offset=SVM_STACK_INVALID,
        .color_offset=SVM_STACK_INVALID,.position_offset=SVM_STACK_INVALID,.w_out_offset=SVM_STACK_INVALID,.radius_offset=13});}
      if(feature<3){p.add(NODE_TEX_VORONOI,SVMNodeTexVoronoi{.dimensions=dims,.feature=NodeVoronoiFeature(feature),.metric=NodeVoronoiDistanceMetric(metric),.w=stack_in(9),.scale=value(1.1f),.detail=value(1.5f),
        .roughness=value(.5f),.lacunarity=value(2.f),.smoothness=value(.6f),.exponent=value(2.5f),.randomness=value(.8f),.normalize=1,.coord=3,.distance_offset=SVM_STACK_INVALID,
        .color_offset=SVM_STACK_INVALID,.position_offset=16,.w_out_offset=14,.radius_offset=SVM_STACK_INVALID});
        p.add(NODE_VECTOR_MATH,SVMNodeVectorMath{.math_type=NODE_VECTOR_MATH_ADD,.a=stack_in3(10),.b=stack_in3(16),.c=color(0,0,0),.param1=value(1),.value_offset=SVM_STACK_INVALID,.vector_offset=10});
        p.add(NODE_MATH,SVMNodeMath{.math_type=NODE_MATH_ADD,.value1=stack_in(13),.value2=stack_in(14),.value3=value(0),.result_offset=13});}
      finish(p,"voronoi "+std::to_string(dims)+"d feature "+std::to_string(feature)+" variant "+std::to_string(variant));
    }
    for(unsigned dims=1;dims<=4;dims++)for(unsigned outputs=0;outputs<3;outputs++){
      Program p(ds);begin(p);p.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=10,.value=make_float3(.4f,.5f,.6f)});
      p.add(NODE_TEX_WHITE_NOISE,SVMNodeTexWhiteNoise{.dimensions=dims,.vector=outputs==2?color(.3f,-.4f,.5f):stack_in3(3),.w=outputs==2?value(-.37f):stack_in(9),.value_offset=13,.color_offset=outputs?SVMStackOffset(10):SVMStackOffset(SVM_STACK_INVALID)});
      finish(p,"white noise "+std::to_string(dims)+"d");
    }
    for(unsigned kind=0;kind<2;kind++)for(unsigned dir=0;dir<4;dir++)for(unsigned profile=0;profile<3;profile++){
      Program p(ds);begin(p);const bool distort=(dir+profile)%2;
      p.add(NODE_TEX_WAVE,SVMNodeTexWave{.wave_type=NodeWaveType(kind),.bands_direction=NodeWaveBandsDirection(dir),.rings_direction=NodeWaveRingsDirection(dir),.profile=NodeWaveProfile(profile),
        .scale=value(1.7f),.distortion=value(distort?3.1f:0.f),.detail=value(dir==3?0.f:2.6f),.dscale=value(1.2f),.droughness=value(.6f),.phase=stack_in(9),.co=3,.color_offset=10,.fac_offset=13});
      finish(p,"wave "+std::to_string(kind)+"/"+std::to_string(dir)+"/"+std::to_string(profile));
    }
    for(unsigned depth=0;depth<=10;depth++){
      Program p(ds);begin(p,depth==5?make_float3(256.f,-64.f,128.f):make_float3(1.3f,.9f,1.7f));
      p.add(NODE_TEX_MAGIC,SVMNodeTexMagic{.scale=value(2.3f),.distortion=value(depth==4?0.f:depth>=8?1.f:1.6f),.depth=uint8_t(depth),.co=3,.color_offset=10,.fac_offset=13});finish(p,"magic depth "+std::to_string(depth));
    }
    for(unsigned variant=0;variant<4;variant++){
      Program p(ds);begin(p);
      p.add(NODE_TEX_BRICK,SVMNodeTexBrick{.color1=color(.8f,.3f,.2f),.color2=stack_in3(6),.mortar=color(.1f,.1f,.1f),.scale=value(3.3f),.mortar_size=value(variant==3?0.f:.05f),.bias=value(variant==2?-.3f:.1f),
        .brick_width=value(.55f),.row_height=value(.27f),.mortar_smooth=value(variant%2?.35f:0.f),.offset_amount=.5f,.squash_amount=variant==1?.6f:1.f,.offset_frequency=uint8_t(variant==0?0:2),
        .squash_frequency=uint8_t(variant==0?0:3),.co=3,.color_offset=10,.fac_offset=13});finish(p,"brick "+std::to_string(variant));
    }
    for(unsigned kind=0;kind<6;kind++){
      Program p(ds);begin(p);
      if(kind<4)p.add(NODE_MAPPING,SVMNodeMapping{.mapping_type=NodeMappingType(kind),.vector=stack_in3(3),.location=color(.3f,-.2f,.5f),.rotation=color(.4f,-.7f,1.3f),.scale=kind==3?color(1.5f,0,.5f):color(1.5f,-.8f,.5f),.result_offset=10});
      if(kind==4){SVMNodeTextureMapping n{.vec_offset=3,.out_offset=10};Transform t=transform_translate(make_float3(.2f,-.4f,.3f))*transform_rotate(.7f,normalize(make_float3(.2f,.9f,.4f)))*transform_scale(make_float3(1.4f,.6f,-.8f));
        n.tfm=PackedTransform(t);p.add(NODE_TEXTURE_MAPPING,n);}
      if(kind==5)p.add(NODE_MIN_MAX,SVMNodeMinMax{.vec_offset=3,.out_offset=10,.mn=make_float3(-1.f,-.5f,0.f),.mx=make_float3(2.f,.5f,3.f)});
      p.add(NODE_SEPARATE_VECTOR,SVMNodeSeparateVector{.vector=stack_in3(10),.vector_index=1,.out_offset=13});finish(p,"mapping "+std::to_string(kind));
    }
    for(unsigned kind=0;kind<4;kind++)for(unsigned variant=0;variant<3;variant++){
      Program p(ds);begin(p);const float lo[]={.1f,.8f,.4f},hi[]={.9f,.2f,.4f};
      p.add(NODE_MAP_RANGE,SVMNodeMapRange{.range_type=NodeMapRangeType(kind),.value=stack_in(9),.from_min=value(lo[variant]),.from_max=value(hi[variant]),.to_min=value(-.5f),.to_max=value(1.5f),.steps=value(variant==1?0.f:4.f),.result_offset=13});
      p.add(NODE_VECTOR_MAP_RANGE,SVMNodeVectorMapRange{.range_type=NodeMapRangeType(kind),.use_clamp=uint8_t(variant!=1),.value=stack_in3(6),.from_min=color(.1f,.9f,.2f),.from_max=color(.9f,.1f,.2f),
        .to_min=color(0,1,-1),.to_max=color(1,0,1),.steps=color(3,0,5),.result_offset=10});finish(p,"map range "+std::to_string(kind)+" variant "+std::to_string(variant));
    }
    for(unsigned kind=0;kind<=NODE_MIX_CLAMP;kind++)for(unsigned variant=0;variant<3;variant++){
      Program p(ds);begin(p);
      if(variant<2)p.add(NODE_MIX_COLOR,SVMNodeMixColor{.blend_type=NodeMix(kind),.a=stack_in3(6),.b=color(.3f,variant?0.f:.7f,1.2f),.fac=variant?value(1.4f):stack_in(9),.use_clamp=uint8_t(variant==0),.use_clamp_result=uint8_t(variant==1),.result_offset=10});
      else p.add(NODE_MIX,SVMNodeMix{.mix_type=NodeMix(kind),.c1=color(.6f,.0f,.25f),.c2=stack_in3(6),.fac=stack_in(9),.result_offset=10});
      p.add(NODE_MIX_FLOAT,SVMNodeMixFloat{.fac=value(variant?1.3f:-.2f),.a=stack_in(9),.b=value(.8f),.use_clamp=uint8_t(variant==1),.result_offset=13});
      finish(p,"mix "+std::to_string(kind)+" variant "+std::to_string(variant));
    }
    for(unsigned variant=0;variant<2;variant++){
      Program p(ds);begin(p);p.add(NODE_MIX_VECTOR,SVMNodeMixVector{.a=stack_in3(6),.b=color(.9f,-.4f,.1f),.fac=value(variant?1.6f:.3f),.use_clamp=uint8_t(variant),.result_offset=10});
      p.add(NODE_MIX_VECTOR_NON_UNIFORM,SVMNodeMixVectorNonUniform{.a=stack_in3(10),.b=color(.2f,.5f,.8f),.fac=stack_in3(3),.use_clamp=uint8_t(variant),.result_offset=10});
      p.add(NODE_VALUE_F,SVMNodeValueF{.value=.5f,.out_offset=13});finish(p,"mix vector "+std::to_string(variant));
    }
    for(unsigned variant=0;variant<4;variant++){
      Program p(ds);begin(p);const float hue[]={.3f,-.7f,0,.5f},sat[]={1.4f,.5f,0,2},val[]={.8f,1.2f,1,0};
      p.add(NODE_HSV,SVMNodeHSV{.color=variant==3?color(.5f,.5f,.5f):stack_in3(6),.hue=value(hue[variant]),.sat=value(sat[variant]),.val=value(val[variant]),.fac=variant==1?value(1.f):stack_in(9),.out_color_offset=10});
      p.add(NODE_VALUE_F,SVMNodeValueF{.value=.5f,.out_offset=13});finish(p,"hsv "+std::to_string(variant));
    }
    for(unsigned kind=0;kind<3;kind++){
      Program p(ds);begin(p);
      p.add(NODE_SEPARATE_COLOR,SVMNodeSeparateColor{.color_type=NodeCombSepColorType(kind),.color=stack_in3(6),.red_offset=13,.green_offset=15,.blue_offset=16});
      p.add(NODE_COMBINE_COLOR,SVMNodeCombineColor{.color_type=NodeCombSepColorType((kind+1)%3),.red=stack_in(15),.green=stack_in(16),.blue=stack_in(9),.color_offset=10});finish(p,"color sepcomb "+std::to_string(kind));
    }
    for(unsigned kind=0;kind<6;kind++){
      Program p(ds);begin(p,make_float3(.7f,.4f,1.1f));const uint size=kind<2?256u:kind<4?5u:2u;
      auto table=[&](uint stride){for(uint i=0;i<size;i++){const float t=float(i)/(size-1);const float v[4]={t*t,1-t,.5f+.4f*std::sin(9*t),1-t*t};for(uint c=0;c<stride;c++)p.words.push_back(std::bit_cast<uint>(v[c]));}};
      if(kind%2==0){p.add(NODE_RGB_RAMP,SVMNodeRGBRamp{.table_size=size,.fac=stack_in(9),.interpolate=uint8_t(kind!=2),.color_offset=10,.alpha_offset=13});table(4);}
      else{p.add(NODE_CURVES,SVMNodeCurves{.color=stack_in3(3),.fac=value(.85f),.min_x=-.2f,.max_x=.9f,.table_size=size,.extrapolate=uint8_t(kind!=3),.out_offset=10});table(4);
        p.add(NODE_FLOAT_CURVE,SVMNodeFloatCurve{.fac=value(.9f),.value_in=stack_in(9),.min_x=.1f,.max_x=.7f,.table_size=size,.extrapolate=uint8_t(kind!=3),.out_offset=13});table(1);}
      finish(p,"ramp "+std::to_string(kind));
    }
    for(unsigned variant=0;variant<8;variant++){
      Program p(ds);begin(p);if(variant%2)p.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=16,.value=make_float3(.2f,.3f,.9f)});
      const float blend[]={.2f,.5f,.9f,0,1};
      p.add(NODE_FRESNEL,SVMNodeFresnel{.ior=variant<3?value(variant==0?1.45f:variant==1?.6f:1e-7f):stack_in(9),.normal_offset=variant%2?SVMStackOffset(16):SVMStackOffset(SVM_STACK_INVALID),.out_offset=10});
      p.add(NODE_LAYER_WEIGHT,SVMNodeLayerWeight{.weight_type=NODE_LAYER_WEIGHT_FRESNEL,.blend=value(blend[variant%5]),.normal_offset=variant%2?SVMStackOffset(16):SVMStackOffset(SVM_STACK_INVALID),.out_offset=11});
      p.add(NODE_LAYER_WEIGHT,SVMNodeLayerWeight{.weight_type=NODE_LAYER_WEIGHT_FACING,.blend=variant==7?stack_in(9):value(blend[variant%5]),.normal_offset=variant%2?SVMStackOffset(16):SVMStackOffset(SVM_STACK_INVALID),.out_offset=12});
      p.add(NODE_VALUE_F,SVMNodeValueF{.value=.5f,.out_offset=13});finish(p,"fresnel "+std::to_string(variant));
    }
    for(unsigned kind=0;kind<=NODE_VECTOR_ROTATE_TYPE_EULER_XYZ;kind++)for(unsigned invert=0;invert<2;invert++){
      Program p(ds);begin(p);
      p.add(NODE_VECTOR_ROTATE,SVMNodeVectorRotate{.rotate_type=NodeVectorRotateType(kind),.vector=stack_in3(3),.center=color(.2f,-.1f,.3f),.axis=invert?color(0,0,0):color(.3f,.8f,-.5f),.rotation=color(.5f,-.3f,.9f),
        .angle=stack_in(9),.invert=uint8_t(invert),.result_offset=10});p.add(NODE_SEPARATE_VECTOR,SVMNodeSeparateVector{.vector=stack_in3(10),.vector_index=2,.out_offset=13});
      finish(p,"vector rotate "+std::to_string(kind)+" invert "+std::to_string(invert));
    }
    // Wavelength: per-path sweep across both table ends, constants at the row
    // boundaries, beyond the table, extrapolation below 380 nm and NaN.
    for(unsigned variant=0;variant<11;variant++){
      Program p(ds);begin(p);const float lambda[]={0,379,380,382.5f,555,779.9f,780,805,-1e9f,1e30f,NAN};
      if(variant==0){p.add(NODE_MATH,SVMNodeMath{.math_type=NODE_MATH_MULTIPLY_ADD,.value1=stack_in(9),.value2=value(500),.value3=value(330),.result_offset=13});
        p.add(NODE_WAVELENGTH,SVMNodeWavelength{.wavelength=stack_in(13),.color_offset=10});}
      else p.add(NODE_WAVELENGTH,SVMNodeWavelength{.wavelength=value(lambda[variant]),.color_offset=10});
      p.add(NODE_VALUE_F,SVMNodeValueF{.value=.5f,.out_offset=13});finish(p,"wavelength "+std::to_string(variant));
    }
    // Gabor: 2D and 3D, anisotropic, partly isotropic and isotropic, constant and
    // per-path inputs (scale, frequency, anisotropy, orientations), unused outputs.
    for(unsigned three=0;three<2;three++)for(unsigned variant=0;variant<6;variant++){
      Program p(ds);begin(p);const float anisotropy[]={1,.5f,0,1,.25f,0};
      p.add(NODE_TEX_GABOR,SVMNodeTexGabor{.gabor_type=three?NODE_GABOR_TYPE_3D:NODE_GABOR_TYPE_2D,.orientation_3d=variant==4?stack_in3(6):color(.3f,-.8f,variant==5?0.f:.5f),
        .scale=variant==1?stack_in(9):value(variant<3?1.7f:4.1f),.frequency=value(variant==3?.0004f:variant==2?3.3f:1.9f),.anisotropy=variant==4?stack_in(9):value(anisotropy[variant]),
        .orientation_2d=variant==0?stack_in(9):value(variant==3?-2.4f:.8f),.coordinates=3,.value_offset=13,.phase_offset=variant==5?SVMStackOffset(SVM_STACK_INVALID):SVMStackOffset(10),
        .intensity_offset=11});
      if(variant==5)p.add(NODE_VALUE_F,SVMNodeValueF{.value=.25f,.out_offset=10});
      p.add(NODE_VALUE_F,SVMNodeValueF{.value=.5f,.out_offset=12});finish(p,"gabor "+std::to_string(three?3:2)+"d variant "+std::to_string(variant));
    }
    // Wireframe: object/pixel units, bump offsets (centre, dx, dy), constant and
    // per-path sizes, unused output.
    for(unsigned pixel=0;pixel<2;pixel++)for(unsigned bump=0;bump<3;bump++)for(unsigned variant=0;variant<2;variant++){
      Program p(ds);begin(p);p.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=10,.value=make_float3(.4f,.5f,.6f)});
      if(variant==1)p.add(NODE_MATH,SVMNodeMath{.math_type=NODE_MATH_MULTIPLY_ADD,.value1=stack_in(9),.value2=value(pixel?50.f:.5f),.value3=value(pixel?1.f:.02f),.result_offset=12});
      p.add(NODE_WIREFRAME,SVMNodeWireframe{.in_size=variant?stack_in(12):value(pixel?40.f:.3f),.bump_filter_width=bump?25.f:.1f,.use_pixel_size=uint8_t(pixel),.bump_offset=NodeBumpOffset(bump),
        .out_fac_offset=uint8_t(pixel&&bump==2&&variant?SVM_STACK_INVALID:10)});
      p.add(NODE_VALUE_F,SVMNodeValueF{.value=.5f,.out_offset=13});finish(p,"wireframe "+std::to_string(pixel)+"/"+std::to_string(bump)+"/"+std::to_string(variant));
    }
    for(unsigned variant=0;variant<12;variant++){
      Program p(ds);begin(p);const float temperature[]={500,800,965,1167,1449,1902,3315,6365,11999,12000,20000,-5};
      if(variant==0){p.add(NODE_MATH,SVMNodeMath{.math_type=NODE_MATH_MULTIPLY_ADD,.value1=stack_in(9),.value2=value(14000),.value3=value(300),.result_offset=13});
        p.add(NODE_BLACKBODY,SVMNodeBlackbody{.temperature=stack_in(13),.color_offset=10});}
      else p.add(NODE_BLACKBODY,SVMNodeBlackbody{.temperature=value(temperature[variant]),.color_offset=10});
      p.add(NODE_VALUE_F,SVMNodeValueF{.value=.5f,.out_offset=13});finish(p,"blackbody "+std::to_string(variant));
    }
    for(unsigned kind=0;kind<=NODE_INFO_OB_RANDOM;kind++){
      Program p(ds);begin(p);p.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=10,.value=make_float3(.4f,.5f,.6f)});
      p.add(NODE_OBJECT_INFO,SVMNodeObjectInfo{.info_type=NodeObjectInfo(kind),.out_offset=uint8_t(kind<=NODE_INFO_OB_COLOR?10:13)});
      if(kind<=NODE_INFO_OB_COLOR)p.add(NODE_SEPARATE_VECTOR,SVMNodeSeparateVector{.vector=stack_in3(10),.vector_index=0,.out_offset=13});finish(p,"object info "+std::to_string(kind));
    }
    {Program p(ds);begin(p);p.add(NODE_CAMERA,SVMNodeCamera{.vector_offset=10,.zdepth_offset=13,.distance_offset=14});
      p.add(NODE_MATH,SVMNodeMath{.math_type=NODE_MATH_ADD,.value1=stack_in(13),.value2=stack_in(14),.value3=value(0),.result_offset=13});finish(p,"camera");}
    {Program p(ds);begin(p);p.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=10,.value=make_float3(.4f,.5f,.6f)});p.add(NODE_SCENE_TIME,SVMNodeSceneTime{.seconds_out=13,.frame_out=14});
      p.add(NODE_MATH,SVMNodeMath{.math_type=NODE_MATH_MULTIPLY_ADD,.value1=stack_in(13),.value2=value(.1f),.value3=stack_in(14),.result_offset=13});finish(p,"scene time");}
    for(unsigned kind=0;kind<3;kind++)for(unsigned from=0;from<3;from++)for(unsigned to=0;to<3;to++){
      Program p(ds);begin(p);
      p.add(NODE_VECTOR_TRANSFORM,SVMNodeVectorTransform{.transform_type=NodeVectorTransformType(kind),.convert_from=NodeVectorTransformConvertSpace(from),.convert_to=NodeVectorTransformConvertSpace(to),.vector_in=stack_in3(3),.vector_out_offset=10});
      p.add(NODE_SEPARATE_VECTOR,SVMNodeSeparateVector{.vector=stack_in3(10),.vector_index=2,.out_offset=13});finish(p,"vector transform "+std::to_string(kind)+"/"+std::to_string(from)+"/"+std::to_string(to));
    }
    {
      // Light path: path flag/visibility and bounce counters vary per path.
      auto states=initial;unsigned n=0;const uint vis[]={PATH_RAY_VISIBILITY_CAMERA,PATH_RAY_VISIBILITY_DIFFUSE,PATH_RAY_VISIBILITY_GLOSSY,PATH_RAY_VISIBILITY_TRANSMIT,PATH_RAY_VISIBILITY_VOLUME_SCATTER};
      for(int i:small){auto &s=states[i];s.path.visibility=vis[n%5];s.path.flag=(n%3==1?PATH_RAY_SINGULAR:0u)|(n%4==2?PATH_RAY_REFLECT:0u)|(n%7==3?PATH_RAY_EMISSION:0u);
        s.path.bounce=n%6;s.path.diffuse_bounce=n%3;s.path.glossy_bounce=n%4;s.path.transparent_bounce=n%5;s.path.transmission_bounce=n%2;n++;}
      for(unsigned kind=0;kind<=NODE_LP_ray_portal;kind++){
        Program p(ds);begin(p);p.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=10,.value=make_float3(.4f,.5f,.6f)});
        p.add(NODE_LIGHT_PATH,SVMNodeLightPath{.path_type=NodeLightPath(kind),.out_offset=13});
        p.add(NODE_CLOSURE_WEIGHT,SVMNodeClosureWeight{.weight_offset=10});p.diffuse(0);
        p.add(NODE_CONVERT,SVMNodeConvert{.convert_type=NODE_CONVERT_FV,.from_offset=13,.to_offset=14});p.add(NODE_CLOSURE_WEIGHT,SVMNodeClosureWeight{.weight_offset=14});p.simple(CLOSURE_BSDF_TRANSLUCENT_ID);
        p.install(ds);evaluate(d,scene,shader,layout,states,small,"Typed node light path "+std::to_string(kind)+" "+std::to_string(node_typed++),m);
      }
    }
    for(unsigned variant=0;variant<2;variant++){
      Program p(ds);begin(p);
      p.add(NODE_NORMAL,SVMNodeNormal{.in_normal=variant?color(.1f,.2f,.3f):stack_in3(3),.out_normal_offset=10,.out_dot_offset=13,.direction_x=.3f,.direction_y=-.5f,.direction_z=variant?0.f:.8f});
      finish(p,"normal "+std::to_string(variant));
    }
    // Bump stages and derivative variants. Slot 20 holds a dual point: derivatives
    // from GEOMETRY_DERIVATIVE, value replaced by the bit-identical slot 3 (so
    // discontinuous operations see exact inputs). acc3/acc1 accumulate |value|
    // and 1000*|derivative| into the colour (10..12) and value (13) weights.
    auto dbegin=[&](Program &p){begin(p);
      p.add(NODE_GEOMETRY_DERIVATIVE,SVMNodeGeometry{.geom_type=NODE_GEOM_P,.bump_offset=NODE_BUMP_OFFSET_CENTER,.store_derivatives=1,.out_offset=20,.bump_filter_width=1});
      p.add(NODE_VECTOR_MATH,SVMNodeVectorMath{.math_type=NODE_VECTOR_MATH_ADD,.a=stack_in3(3),.b=color(0,0,0),.c=color(0,0,0),.param1=value(1),.value_offset=SVM_STACK_INVALID,.vector_offset=20});
      p.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=10,.value=make_float3(.1f,.1f,.1f)});p.add(NODE_VALUE_F,SVMNodeValueF{.value=.1f,.out_offset=13});};
    auto acc3=[&](Program &p,unsigned slot,float k){
      p.add(NODE_VECTOR_MATH,SVMNodeVectorMath{.math_type=NODE_VECTOR_MATH_ABSOLUTE,.a=stack_in3(slot),.b=color(0,0,0),.c=color(0,0,0),.param1=value(1),.value_offset=SVM_STACK_INVALID,.vector_offset=200});
      p.add(NODE_VECTOR_MATH,SVMNodeVectorMath{.math_type=NODE_VECTOR_MATH_MULTIPLY_ADD,.a=stack_in3(200),.b=color(k,k,k),.c=stack_in3(10),.param1=value(1),.value_offset=SVM_STACK_INVALID,.vector_offset=10});};
    auto acc1=[&](Program &p,unsigned slot,float k){
      p.add(NODE_MATH,SVMNodeMath{.math_type=NODE_MATH_ABSOLUTE,.value1=stack_in(slot),.value2=value(0),.value3=value(0),.result_offset=203});
      p.add(NODE_MATH,SVMNodeMath{.math_type=NODE_MATH_MULTIPLY_ADD,.value1=stack_in(203),.value2=value(k),.value3=stack_in(13),.result_offset=13});};
    auto dual3=[&](Program &p,unsigned slot){acc3(p,slot,.1f);acc3(p,slot+3,1000);acc3(p,slot+6,1000);};
    auto dual1=[&](Program &p,unsigned slot){acc1(p,slot,.1f);acc1(p,slot+1,1000);acc1(p,slot+2,1000);};
    for(NodeGeometry kind:{NODE_GEOM_P,NODE_GEOM_N,NODE_GEOM_Ng,NODE_GEOM_I,NODE_GEOM_uv})for(unsigned bump=0;bump<3;bump++)for(uint8_t store=0;store<2;store++){
      Program p(ds);dbegin(p);
      p.add(NODE_GEOMETRY_DERIVATIVE,SVMNodeGeometry{.geom_type=kind,.bump_offset=NodeBumpOffset(bump),.store_derivatives=store,.out_offset=30,.bump_filter_width=37.5f});
      if(store)dual3(p,30);else acc3(p,30,.5f);
      finish(p,"geometry derivative "+std::to_string(kind)+"/"+std::to_string(bump)+"/"+std::to_string(store));
    }
    for(NodeTexCoord kind:{NODE_TEXCO_OBJECT,NODE_TEXCO_NORMAL,NODE_TEXCO_REFLECTION,NODE_TEXCO_CAMERA,NODE_TEXCO_WINDOW,NODE_TEXCO_OBJECT_WITH_TRANSFORM,NODE_TEXCO_DUPLI_GENERATED,NODE_TEXCO_DUPLI_UV})
      for(unsigned bump=0;bump<4;bump++){
        Program p(ds);dbegin(p);const uint8_t store=bump<3;
        p.add(NODE_TEX_COORD_DERIVATIVE,SVMNodeTexCoord{.texco_type=kind,.bump_offset=NodeBumpOffset(bump%3),.store_derivatives=store,.out_offset=30,.bump_filter_width=37.5f});
        if(kind==NODE_TEXCO_OBJECT_WITH_TRANSFORM){auto b=std::bit_cast<std::array<uint,sizeof(PackedTransform)/4>>(PackedTransform(transform_translate(make_float3(.3,.2,.1))*transform_rotate(.4f,make_float3(.2,.9,.1))*transform_scale(make_float3(.4,.5,.6))));
          p.words.insert(p.words.end(),b.begin(),b.end());}
        if(store)dual3(p,30);else acc3(p,30,.5f);
        finish(p,"tex coord derivative "+std::to_string(kind)+"/"+std::to_string(bump));
      }
    for(unsigned component=0;component<3;component++){
      // Value, conversion and separate/combine derivative variants.
      Program p(ds);dbegin(p);
      p.add(NODE_SEPARATE_VECTOR_DERIVATIVE,SVMNodeSeparateVector{.vector=stack_in3(20),.vector_index=uint8_t(component),.out_offset=30});
      for(unsigned k=0;k<3;k++)p.add(NODE_COMBINE_VECTOR_DERIVATIVE,SVMNodeCombineVector{.in=k==component?stack_in(30):value(.25f*k-.3f),.vector_index=uint8_t(k),.out_offset=40});
      p.add(NODE_CONVERT_DERIVATIVE,SVMNodeConvert{.convert_type=NODE_CONVERT_VF,.from_offset=20,.to_offset=50});
      p.add(NODE_CONVERT_DERIVATIVE,SVMNodeConvert{.convert_type=NODE_CONVERT_FV,.from_offset=30,.to_offset=53});
      p.add(NODE_CONVERT_DERIVATIVE,SVMNodeConvert{.convert_type=NODE_CONVERT_CF,.from_offset=20,.to_offset=62});
      p.add(NODE_VALUE_F_DERIVATIVE,SVMNodeValueF{.value=.35f+component,.out_offset=65});
      p.add(NODE_VALUE_V_DERIVATIVE,SVMNodeValueV{.out_offset=68,.value=make_float3(.2f,-.4f,.6f*component)});
      p.add(NODE_CONVERT_DERIVATIVE,SVMNodeConvert{.convert_type=NODE_CONVERT_FI,.from_offset=30,.to_offset=77});
      p.add(NODE_CONVERT_DERIVATIVE,SVMNodeConvert{.convert_type=NODE_CONVERT_IF,.from_offset=77,.to_offset=78});
      p.add(NODE_CONVERT_DERIVATIVE,SVMNodeConvert{.convert_type=NODE_CONVERT_IV,.from_offset=77,.to_offset=81});
      p.add(NODE_CONVERT_DERIVATIVE,SVMNodeConvert{.convert_type=NODE_CONVERT_CI,.from_offset=20,.to_offset=90});
      p.add(NODE_CONVERT_DERIVATIVE,SVMNodeConvert{.convert_type=NODE_CONVERT_VI,.from_offset=20,.to_offset=91});
      p.add(NODE_CONVERT,SVMNodeConvert{.convert_type=NODE_CONVERT_IF,.from_offset=90,.to_offset=92});p.add(NODE_CONVERT,SVMNodeConvert{.convert_type=NODE_CONVERT_IF,.from_offset=91,.to_offset=93});
      dual1(p,30);dual3(p,40);dual1(p,50);dual3(p,53);dual1(p,62);dual1(p,65);dual3(p,68);dual1(p,78);dual3(p,81);acc1(p,92,.1f);acc1(p,93,.1f);
      finish(p,"value/convert/sepcomb derivative "+std::to_string(component));
    }
    for(unsigned kind=0;kind<=NODE_VECTOR_MATH_ROUND;kind++){
      // Smooth operations read duals with derivatives; discontinuous ones read
      // exact constant b/c (their derivatives are still carried through a).
      Program p(ds);dbegin(p);const NodeVectorMathType type=NodeVectorMathType(kind);
      const bool step=type==NODE_VECTOR_MATH_SNAP||type==NODE_VECTOR_MATH_FLOOR||type==NODE_VECTOR_MATH_CEIL||type==NODE_VECTOR_MATH_MODULO||type==NODE_VECTOR_MATH_FRACTION||
        type==NODE_VECTOR_MATH_ABSOLUTE||type==NODE_VECTOR_MATH_MINIMUM||type==NODE_VECTOR_MATH_MAXIMUM||type==NODE_VECTOR_MATH_WRAP||type==NODE_VECTOR_MATH_FACEFORWARD||
        type==NODE_VECTOR_MATH_SIGN||type==NODE_VECTOR_MATH_ROUND;
      const bool small_a=type==NODE_VECTOR_MATH_SINE||type==NODE_VECTOR_MATH_COSINE||type==NODE_VECTOR_MATH_TANGENT||type==NODE_VECTOR_MATH_POWER;
      p.add(NODE_VECTOR_MATH_DERIVATIVE,SVMNodeVectorMath{.math_type=NODE_VECTOR_MATH_MULTIPLY_ADD,.a=stack_in3(20),.b=color(.3f,-.7f,1.1f),.c=color(.5f,.2f,-.4f),.param1=value(1),.value_offset=SVM_STACK_INVALID,.vector_offset=40});
      p.add(NODE_VECTOR_MATH_DERIVATIVE,SVMNodeVectorMath{.math_type=NODE_VECTOR_MATH_SCALE,.a=stack_in3(20),.b=color(0,0,0),.c=color(0,0,0),.param1=value(-.6f),.value_offset=SVM_STACK_INVALID,.vector_offset=50});
      p.add(NODE_SEPARATE_VECTOR_DERIVATIVE,SVMNodeSeparateVector{.vector=stack_in3(40),.vector_index=1,.out_offset=60});
      p.add(NODE_VALUE_V_DERIVATIVE,SVMNodeValueV{.out_offset=70,.value=make_float3(.75f,-1.25f,2.5f)});
      p.add(NODE_VALUE_V_DERIVATIVE,SVMNodeValueV{.out_offset=80,.value=make_float3(-.5f,.25f,-1.5f)});
      unsigned a=20;
      if(small_a){p.add(NODE_VECTOR_MATH_DERIVATIVE,SVMNodeVectorMath{.math_type=NODE_VECTOR_MATH_SCALE,.a=stack_in3(20),.b=color(0,0,0),.c=color(0,0,0),.param1=value(.05f),.value_offset=SVM_STACK_INVALID,.vector_offset=150});a=150;}
      if(type==NODE_VECTOR_MATH_POWER){
        p.add(NODE_VECTOR_MATH_DERIVATIVE,SVMNodeVectorMath{.math_type=NODE_VECTOR_MATH_ABSOLUTE,.a=stack_in3(150),.b=color(0,0,0),.c=color(0,0,0),.param1=value(1),.value_offset=SVM_STACK_INVALID,.vector_offset=160});
        p.add(NODE_VECTOR_MATH_DERIVATIVE,SVMNodeVectorMath{.math_type=NODE_VECTOR_MATH_MULTIPLY_ADD,.a=stack_in3(160),.b=color(1,1,1),.c=color(.5f,.5f,.5f),.param1=value(1),.value_offset=SVM_STACK_INVALID,.vector_offset=150});}
      p.add(NODE_VECTOR_MATH_DERIVATIVE,SVMNodeVectorMath{.math_type=type,.a=stack_in3(a),.b=stack_in3(step||type==NODE_VECTOR_MATH_POWER?70:40),.c=stack_in3(step?80:50),
        .param1=type==NODE_VECTOR_MATH_REFRACT?value(.7f):stack_in(60),.value_offset=110,.vector_offset=120});
      const bool scalar=type==NODE_VECTOR_MATH_DOT_PRODUCT||type==NODE_VECTOR_MATH_DISTANCE||type==NODE_VECTOR_MATH_LENGTH;
      if(scalar)dual1(p,110);else dual3(p,120);
      finish(p,"vector math derivative "+std::to_string(kind));
    }
    for(unsigned kind=0;kind<6;kind++){
      Program p(ds);dbegin(p);
      if(kind<5){const float3 scale=kind==4?make_float3(1.5f,0,.5f):make_float3(1.5f,-.8f,.5f);
        p.add(NODE_MAPPING_DERIVATIVE,SVMNodeMapping{.mapping_type=NodeMappingType(kind%4),.vector=stack_in3(20),.location=color(.3f,-.2f,.5f),.rotation=color(.4f,-.7f,1.3f),
          .scale=color(scale.x,scale.y,scale.z),.result_offset=30});}
      else{SVMNodeTextureMapping n{.vec_offset=20,.out_offset=30};n.tfm=PackedTransform(transform_translate(make_float3(.2,-.3,.1))*transform_rotate(.7f,make_float3(.3,.1,.9))*transform_scale(make_float3(1.5,.7,1.1)));
        p.add(NODE_TEXTURE_MAPPING_DERIVATIVE,n);}
      dual3(p,30);finish(p,"mapping derivative "+std::to_string(kind));
    }
    for(unsigned variant=0;variant<10;variant++){
      // Bump: heights of three point taps (centre, +dx and +dy offsets).
      Program p(ds);dbegin(p);const float strength[]={.3f,1,2,-.5f};const float width=variant==9?0.f:2000.f;
      for(unsigned tap=0;tap<3;tap++){p.add(NODE_GEOMETRY_DERIVATIVE,SVMNodeGeometry{.geom_type=NODE_GEOM_P,.bump_offset=NodeBumpOffset(tap),.store_derivatives=0,.out_offset=SVMStackOffset(110+3*tap),.bump_filter_width=width});
        p.add(NODE_VECTOR_MATH,SVMNodeVectorMath{.math_type=NODE_VECTOR_MATH_DOT_PRODUCT,.a=stack_in3(110+3*tap),.b=color(.7f,-1.3f,.4f),.c=color(0,0,0),.param1=value(1),.value_offset=SVMStackOffset(119+tap),.vector_offset=SVM_STACK_INVALID});}
      p.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=30,.value=normalize(make_float3(.2f,-.3f,.93f))});
      p.add(NODE_SET_BUMP,SVMNodeSetBump{.scale=value(variant==8?0.f:.7f),.strength=value(strength[variant%4]),.bump_filter_width=width,.normal_offset=SVMStackOffset(variant%2?30:SVM_STACK_INVALID),
        .invert=uint8_t(variant/2%2),.use_object_space=uint8_t(variant/4%2),.center_offset=119,.dx_offset=120,.dy_offset=121,.out_offset=122,.bump_state_offset=SVM_STACK_INVALID});
      acc3(p,122,1);p.add(NODE_CLOSURE_WEIGHT,SVMNodeClosureWeight{.weight_offset=10});p.diffuse(0,122);
      p.add(NODE_CONVERT,SVMNodeConvert{.convert_type=NODE_CONVERT_FV,.from_offset=13,.to_offset=14});p.add(NODE_CLOSURE_WEIGHT,SVMNodeClosureWeight{.weight_offset=14});p.simple(CLOSURE_BSDF_TRANSLUCENT_ID,122);
      p.install(ds);evaluate(d,scene,shader,layout,initial,small,"Typed node set bump "+std::to_string(variant)+" "+std::to_string(node_typed++),m);
    }
    {
      // Bump state: enter saves P and dP, leave restores them.
      Program p(ds);dbegin(p);p.add(NODE_ENTER_BUMP_EVAL,SVMNodeEnterBumpEval{.state_offset=130});acc3(p,130,.1f);acc1(p,133,1000);
      p.add(NODE_LEAVE_BUMP_EVAL,SVMNodeLeaveBumpEval{.state_offset=130});p.add(NODE_GEOMETRY,SVMNodeGeometry{.geom_type=NODE_GEOM_P,.out_offset=140});acc3(p,140,.1f);
      finish(p,"bump state");
    }
#endif
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
    // The scene-exchange passes (reference_policy.h) do no GPU work; the rejections run in the import pass.
    if(!svm_exchange_mode()){
    invalid=rejections(d,scene,shader);
#ifdef CBC_NODES_FIXTURE
    invalid+=node_rejections(d,scene,shader);
#endif
    }
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
  if(svm_exchange_mode()){std::cout<<"{\"passed\":true,\"sceneExchangeOnly\":\""<<(reference_policy::prepare_only()?"dump":"reference")<<"\",\"cases\":"<<m.cases<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";}
  else std::cout<<"{\"passed\":true,\"originalCpuSurfaceSvm\":true,\"compiledGraphs\":"<<GRAPH_COUNT<<",\"cases\":"<<m.cases<<",\"paths\":"<<m.paths<<",\"closureFamilies\":"<<m.closures.size()<<",\"integerChecks\":"<<m.integers<<",\"floatChecks\":"<<m.floats<<",\"stateChecks\":"<<m.state_checks<<",\"untouchedGuardBytes\":"<<m.guards<<",\"strictFloatFailures\":"<<m.strict<<",\"maximumAbsoluteError\":"<<m.maximum<<",\"invalidCasesRejected\":"<<invalid<<",\"nativeUnsupportedFeatureRejections\":"<<host_rejections<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<",\"pipelineReport\":"<<ccl::pipeline_process_report_json()<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
