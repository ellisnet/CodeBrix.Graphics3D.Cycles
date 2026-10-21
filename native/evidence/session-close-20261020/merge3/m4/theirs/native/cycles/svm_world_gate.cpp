// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "wavefront_test.h"
#include "background_reference.h"
#include "kernel/svm/node_types.h"
#include "scene/scene.h"
#include "scene/background.h"
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
#include <map>
#include <set>
using namespace ccl;
using namespace wavefront_test;
struct Metrics {uint64_t paths=0,state_checks=0,float_checks=0,strict_failures=0;unsigned cases=0,compiled_worlds=0;double maximum_error=0;std::set<uint> opcodes;};
static unsigned node_size(uint op){switch(op){
#define NODE(op,type) case op:return sizeof(SVMNode##type)/4
  NODE(NODE_SHADER_JUMP,ShaderJump);NODE(NODE_JUMP_IF_ZERO,JumpIfZero);NODE(NODE_JUMP_IF_ONE,JumpIfOne);NODE(NODE_MATH,Math);NODE(NODE_VECTOR_MATH,VectorMath);
  NODE(NODE_CLAMP,Clamp);NODE(NODE_VALUE_F,ValueF);NODE(NODE_VALUE_V,ValueV);NODE(NODE_CONVERT,Convert);NODE(NODE_GEOMETRY,Geometry);NODE(NODE_TEX_COORD,TexCoord);
  NODE(NODE_SEPARATE_VECTOR,SeparateVector);NODE(NODE_COMBINE_VECTOR,CombineVector);NODE(NODE_CLOSURE_SET_WEIGHT,ClosureSetWeight);NODE(NODE_CLOSURE_WEIGHT,ClosureWeight);
  NODE(NODE_CLOSURE_BACKGROUND,ClosureBackground);NODE(NODE_CLOSURE_EMISSION,ClosureEmission);NODE(NODE_EMISSION_WEIGHT,EmissionWeight);NODE(NODE_MIX_CLOSURE,MixClosure);
  NODE(NODE_TEX_CHECKER,TexChecker);NODE(NODE_TEX_GRADIENT,TexGradient);NODE(NODE_GAMMA,Gamma);NODE(NODE_INVERT,Invert);NODE(NODE_BRIGHTCONTRAST,BrightContrast);
  // Texture, colour and utility nodes (ramp/curve tables are added by inspect_program).
  NODE(NODE_TEX_NOISE,TexNoise);NODE(NODE_TEX_VORONOI,TexVoronoi);NODE(NODE_TEX_WHITE_NOISE,TexWhiteNoise);NODE(NODE_TEX_WAVE,TexWave);NODE(NODE_TEX_MAGIC,TexMagic);NODE(NODE_TEX_BRICK,TexBrick);
  NODE(NODE_MAPPING,Mapping);NODE(NODE_TEXTURE_MAPPING,TextureMapping);NODE(NODE_MIN_MAX,MinMax);NODE(NODE_MAP_RANGE,MapRange);NODE(NODE_VECTOR_MAP_RANGE,VectorMapRange);
  NODE(NODE_MIX,Mix);NODE(NODE_MIX_COLOR,MixColor);NODE(NODE_MIX_FLOAT,MixFloat);NODE(NODE_MIX_VECTOR,MixVector);NODE(NODE_MIX_VECTOR_NON_UNIFORM,MixVectorNonUniform);
  NODE(NODE_HSV,HSV);NODE(NODE_SEPARATE_COLOR,SeparateColor);NODE(NODE_COMBINE_COLOR,CombineColor);NODE(NODE_RGB_RAMP,RGBRamp);NODE(NODE_CURVES,Curves);NODE(NODE_FLOAT_CURVE,FloatCurve);
  NODE(NODE_FRESNEL,Fresnel);NODE(NODE_LAYER_WEIGHT,LayerWeight);NODE(NODE_VECTOR_ROTATE,VectorRotate);NODE(NODE_BLACKBODY,Blackbody);NODE(NODE_NORMAL,Normal);
  NODE(NODE_OBJECT_INFO,ObjectInfo);NODE(NODE_CAMERA,Camera);NODE(NODE_SCENE_TIME,SceneTime);NODE(NODE_VECTOR_TRANSFORM,VectorTransform);NODE(NODE_LIGHT_PATH,LightPath);
  NODE(NODE_WAVELENGTH,Wavelength);NODE(NODE_WIREFRAME,Wireframe);
  // Bump stage and derivative variants (world 22).
  NODE(NODE_SET_BUMP,SetBump);NODE(NODE_GEOMETRY_DERIVATIVE,Geometry);NODE(NODE_TEX_COORD_DERIVATIVE,TexCoord);NODE(NODE_VALUE_F_DERIVATIVE,ValueF);
  NODE(NODE_VALUE_V_DERIVATIVE,ValueV);NODE(NODE_CONVERT_DERIVATIVE,Convert);NODE(NODE_VECTOR_MATH_DERIVATIVE,VectorMath);NODE(NODE_MAPPING_DERIVATIVE,Mapping);
  NODE(NODE_TEXTURE_MAPPING_DERIVATIVE,TextureMapping);NODE(NODE_SEPARATE_VECTOR_DERIVATIVE,SeparateVector);NODE(NODE_COMBINE_VECTOR_DERIVATIVE,CombineVector);
#undef NODE
  case NODE_END:return 0;default:throw std::runtime_error("Fixture contains unsupported opcode "+std::to_string(op));}}
static SVMInputFloat value(float f){return {std::bit_cast<uint>(f)};}
static SVMInputFloat slot(unsigned i){return {SVM_INPUT_STACK_OFFSET_MASK|i};}
static SVMInputFloat3 value3(float x,float y,float z){return {value(x),value(y),value(z)};}
static SVMInputFloat3 slot3(unsigned i){return {slot(i),{}, {}};}
struct Program {
  std::vector<uint> words;unsigned shader;
  Program(DeviceScene &scene):words(scene.data.max_shaders*(1+sizeof(SVMNodeShaderJump)/4),uint(NODE_END)),shader(scene.data.background.surface_shader&SHADER_MASK){
    words[shader*4]=NODE_SHADER_JUMP;words[shader*4+1]=words.size();
  }
  template<class T>unsigned add(uint opcode,const T &node){static_assert(sizeof(T)%4==0);unsigned at=words.size();words.push_back(opcode);const auto bytes=std::bit_cast<std::array<uint,sizeof(T)/4>>(node);words.insert(words.end(),bytes.begin(),bytes.end());return at;}
  void geometry(){add(NODE_GEOMETRY,SVMNodeGeometry{.geom_type=NODE_GEOM_P,.out_offset=0});}
  void result(unsigned offset=3){add(NODE_CLOSURE_WEIGHT,SVMNodeClosureWeight{.weight_offset=SVMStackOffset(offset)});add(NODE_CLOSURE_BACKGROUND,SVMNodeClosureBackground{.mix_weight_offset=SVM_STACK_INVALID});words.push_back(NODE_END);}
  void install(DeviceScene &scene){scene.svm_nodes.alloc(words.size());std::copy(words.begin(),words.end(),scene.svm_nodes.data());scene.svm_nodes.copy_to_device();scene.shaders[shader].flags&=~SD_HAS_CONSTANT_EMISSION;scene.shaders.copy_to_device();}
};
static void base_scene(Scene &scene){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);scene.integrator->set_aa_samples(16);
  scene.film->set_use_sample_count(false);scene.camera->set_full_width(64);scene.camera->set_full_height(48);scene.camera->set_screen_size(64,48);scene.camera->set_fov(1.1f);
  scene.camera->set_matrix(transform_translate(make_float3(2,-1,3))*transform_rotate(.23f,make_float3(0,1,0)));scene.camera->compute_auto_viewplane();
}
static void graph_scene(Scene &scene,unsigned test){
  base_scene(scene);auto graph=make_unique<ShaderGraph>();auto geo=graph->create_node<GeometryNode>();auto bg=graph->create_node<BackgroundNode>();bg->set_strength(.7f);
  ShaderOutput *color=geo->output("Position");
  if(test==1){auto m=graph->create_node<VectorMathNode>();m->set_math_type(NODE_VECTOR_MATH_MULTIPLY_ADD);m->set_vector2(make_float3(.7,.5,.3));m->set_vector3(make_float3(.2,.1,.4));graph->connect(geo->output("Incoming"),m->input("Vector1"));color=m->output("Vector");}
  if(test==2||test==3){
    auto separate=graph->create_node<SeparateXYZNode>();auto math=graph->create_node<MathNode>();math->set_math_type(NODE_MATH_ADD);math->set_value2(.5f);graph->connect(color,separate->input("Vector"));graph->connect(separate->output("X"),math->input("Value1"));
    auto clamp=graph->create_node<ClampNode>();clamp->set_clamp_type(test==2?NODE_CLAMP_MINMAX:NODE_CLAMP_RANGE);clamp->set_min(test==2?.2f:.8f);clamp->set_max(test==2?.8f:.2f);graph->connect(math->output("Value"),clamp->input("Value"));color=clamp->output("Result");
  }
  if(test==4||test==5){auto checker=graph->create_node<CheckerTextureNode>();checker->set_scale(8.0f);checker->set_color1(make_float3(.8,.2,.1));checker->set_color2(make_float3(.1,.4,.9));graph->connect(color,checker->input("Vector"));color=checker->output("Color");
    if(test==5){auto gamma=graph->create_node<GammaNode>();gamma->set_gamma(.7f);graph->connect(color,gamma->input("Color"));auto invert=graph->create_node<InvertNode>();invert->set_fac(.3f);graph->connect(gamma->output("Color"),invert->input("Color"));auto bright=graph->create_node<BrightContrastNode>();bright->set_bright(.1f);bright->set_contrast(.2f);graph->connect(invert->output("Color"),bright->input("Color"));color=bright->output("Color");}}
  if(test>=6&&test<=12){auto scale=graph->create_node<VectorMathNode>();scale->set_math_type(NODE_VECTOR_MATH_SCALE);scale->set_scale(.65f);graph->connect(color,scale->input("Vector1"));auto gradient=graph->create_node<GradientTextureNode>();gradient->set_gradient_type(NodeGradientType(test-6));graph->connect(scale->output("Vector"),gradient->input("Vector"));color=gradient->output("Color");}
  if(test>=13&&test<=18){auto tc=graph->create_node<TextureCoordinateNode>();const char *outputs[]={"Normal","Object","Reflection","Camera","Window","Object"};if(test==18){tc->set_use_transform(true);tc->set_ob_tfm(transform_translate(make_float3(.2,.1,.3))*transform_scale(make_float3(.5,.6,.7)));}color=tc->output(outputs[test-13]);}
  if(test==19){
    auto sep=graph->create_node<SeparateXYZNode>();graph->connect(color,sep->input("Vector"));auto math=graph->create_node<MathNode>();math->set_math_type(NODE_MATH_MULTIPLY_ADD);math->set_value2(2);math->set_value3(.5f);graph->connect(sep->output("X"),math->input("Value1"));
    auto other=graph->create_node<BackgroundNode>();other->set_color(make_float3(.8,.1,.2));bg->set_color(make_float3(.1,.3,.9));auto mix=graph->create_node<MixClosureNode>();graph->connect(math->output("Value"),mix->input("Fac"));graph->connect(bg->output("Background"),mix->input("Closure1"));graph->connect(other->output("Background"),mix->input("Closure2"));graph->connect(mix->output("Closure"),graph->output()->input("Surface"));
  }else{graph->connect(color,bg->input("Color"));graph->connect(bg->output("Background"),graph->output()->input("Surface"));}
  auto shader=scene.create_node<Shader>();shader->set_graph(std::move(graph));shader->tag_update(&scene);scene.background->set_shader(shader);
}
#ifdef CBC_NODES_FIXTURE
// Compiled worlds for the texture, colour and utility nodes. World inputs are
// the GPU camera directions read by both sides, so they are bit-identical.
static constexpr unsigned WORLD_COUNT=23;
static void nodes_world(Scene &scene,unsigned test){
  base_scene(scene);auto graph=make_unique<ShaderGraph>();auto &g=*graph;auto geo=g.create_node<GeometryNode>();auto bg=g.create_node<BackgroundNode>();bg->set_strength(.7f);
  ShaderOutput *dir=geo->output("Position");ShaderOutput *color=dir;
  auto vmath=[&](NodeVectorMathType type,ShaderOutput *a,float3 b){auto n=g.create_node<VectorMathNode>();n->set_math_type(type);n->set_vector2(b);g.connect(a,n->input("Vector1"));return n->output("Vector");};
  auto component=[&](ShaderOutput *v,const char *name){auto n=g.create_node<SeparateXYZNode>();g.connect(v,n->input("Vector"));return n->output(name);};
  auto noise=[&](unsigned dims,NodeNoiseType type,float detail,float distortion){auto n=g.create_node<NoiseTextureNode>();n->set_dimensions(dims);n->set_type(type);n->set_detail(detail);n->set_distortion(distortion);
    n->set_scale(2.3f);n->set_roughness(.6f);g.connect(dir,n->input("Vector"));g.connect(component(dir,"Z"),n->input("W"));return n;};
  if(test==0)color=noise(3,NODE_NOISE_FBM,6.5f,.3f)->output("Color");
  if(test==1)color=noise(4,NODE_NOISE_RIDGED_MULTIFRACTAL,4,0)->output("Color");
  if(test==2){auto m=g.create_node<MappingNode>();m->set_mapping_type(NODE_MAPPING_TYPE_POINT);m->set_rotation(make_float3(.3f,.2f,.9f));m->set_scale(make_float3(2,1,1.5f));g.connect(dir,m->input("Vector"));
    auto w=g.create_node<WaveTextureNode>();w->set_wave_type(NODE_WAVE_RINGS);w->set_profile(NODE_WAVE_PROFILE_SAW);w->set_distortion(1.5f);w->set_detail(2);g.connect(m->output("Vector"),w->input("Vector"));color=w->output("Color");}
  if(test==3){auto n=g.create_node<MagicTextureNode>();n->set_depth(3);n->set_scale(3.1f);g.connect(dir,n->input("Vector"));color=n->output("Color");}
  if(test==4){auto mix=g.create_node<MixColorNode>();mix->set_blend_type(NODE_MIX_OVERLAY);g.connect(noise(2,NODE_NOISE_FBM,2,0)->output("Color"),mix->input("A"));
    auto grad=g.create_node<GradientTextureNode>();g.connect(dir,grad->input("Vector"));g.connect(grad->output("Color"),mix->input("B"));g.connect(component(dir,"Y"),mix->input("Factor"));color=mix->output("Result");}
  if(test==5){auto r=g.create_node<RGBRampNode>();array<packed_float3> ramp;array<float> alpha;for(unsigned i=0;i<RAMP_TABLE_SIZE;i++){const float t=float(i)/(RAMP_TABLE_SIZE-1);ramp.push_back_slow(make_float3(t,t*t,1-t));alpha.push_back_slow(t);}
    r->set_ramp(ramp);r->set_ramp_alpha(alpha);r->set_interpolate(true);g.connect(noise(1,NODE_NOISE_HETERO_TERRAIN,3,0)->output("Fac"),r->input("Fac"));color=r->output("Color");}
  if(test==6){auto checker=g.create_node<CheckerTextureNode>();checker->set_scale(6);g.connect(dir,checker->input("Vector"));auto h=g.create_node<HSVNode>();h->set_hue(.2f);h->set_saturation(1.3f);
    g.connect(checker->output("Color"),h->input("Color"));g.connect(component(dir,"X"),h->input("Fac"));color=h->output("Color");}
  if(test==8){auto lp=g.create_node<LightPathNode>();auto comb=g.create_node<CombineXYZNode>();g.connect(lp->output("Ray Depth"),comb->input("X"));g.connect(lp->output("Is Camera Ray"),comb->input("Y"));
    g.connect(lp->output("Ray Length"),comb->input("Z"));color=vmath(NODE_VECTOR_MATH_MINIMUM,comb->output("Vector"),make_float3(3,3,3));}
  if(test==9){auto m=g.create_node<MapRangeNode>();m->set_from_min(-1);m->set_from_max(1);m->set_to_min(600);m->set_to_max(15000);g.connect(component(dir,"Z"),m->input("Value"));
    auto b=g.create_node<BlackbodyNode>();g.connect(m->output("Result"),b->input("Temperature"));color=b->output("Color");}
  if(test==10){auto sep=g.create_node<SeparateColorNode>();sep->set_color_type(NODE_COMBSEP_COLOR_HSL);g.connect(vmath(NODE_VECTOR_MATH_ABSOLUTE,dir,zero_float3()),sep->input("Color"));
    auto comb=g.create_node<CombineColorNode>();comb->set_color_type(NODE_COMBSEP_COLOR_HSV);g.connect(sep->output("Blue"),comb->input("Red"));g.connect(sep->output("Red"),comb->input("Green"));g.connect(sep->output("Green"),comb->input("Blue"));color=comb->output("Color");}
  if(test==11){auto r=g.create_node<VectorRotateNode>();r->set_rotate_type(NODE_VECTOR_ROTATE_TYPE_EULER_XYZ);r->set_rotation(make_float3(.4f,.1f,-.7f));g.connect(dir,r->input("Vector"));
    // White noise hashes float bits, so it reads the exact direction (a rotated
    // direction differs in the last bits through sin/cos).
    auto w=g.create_node<WhiteNoiseTextureNode>();w->set_dimensions(3);g.connect(dir,w->input("Vector"));
    auto mix=g.create_node<MixVectorNode>();mix->set_fac(.5f);g.connect(r->output("Vector"),mix->input("A"));g.connect(w->output("Color"),mix->input("B"));color=mix->output("Result");}
  if(test==12){auto b=g.create_node<BrickTextureNode>();b->set_scale(3);b->set_mortar_smooth(.3f);g.connect(dir,b->input("Vector"));color=b->output("Color");}
  if(test==13){auto f=g.create_node<FresnelNode>();f->set_IOR(1.45f);auto lw=g.create_node<LayerWeightNode>();lw->set_blend(.3f);auto comb=g.create_node<CombineXYZNode>();
    g.connect(f->output("Fac"),comb->input("X"));g.connect(lw->output("Fresnel"),comb->input("Y"));g.connect(lw->output("Facing"),comb->input("Z"));color=comb->output("Vector");}
  if(test==14){auto cam=g.create_node<CameraNode>();auto add=g.create_node<VectorMathNode>();add->set_math_type(NODE_VECTOR_MATH_SCALE);g.connect(cam->output("View Vector"),add->input("Vector1"));
    g.connect(cam->output("View Distance"),add->input("Scale"));color=vmath(NODE_VECTOR_MATH_ABSOLUTE,add->output("Vector"),zero_float3());}
  if(test==15){auto info=g.create_node<ObjectInfoNode>();auto comb=g.create_node<CombineXYZNode>();g.connect(info->output("Material Index"),comb->input("X"));g.connect(info->output("Random"),comb->input("Y"));
    g.connect(component(dir,"Z"),comb->input("Z"));auto add=g.create_node<VectorMathNode>();add->set_math_type(NODE_VECTOR_MATH_ADD);g.connect(comb->output("Vector"),add->input("Vector1"));g.connect(info->output("Location"),add->input("Vector2"));color=add->output("Vector");}
  if(test==16){auto vt=g.create_node<VectorTransformNode>();vt->set_transform_type(NODE_VECTOR_TRANSFORM_TYPE_POINT);vt->set_convert_from(NODE_VECTOR_TRANSFORM_CONVERT_SPACE_WORLD);
    vt->set_convert_to(NODE_VECTOR_TRANSFORM_CONVERT_SPACE_CAMERA);g.connect(dir,vt->input("Vector"));color=vmath(NODE_VECTOR_MATH_FRACTION,vt->output("Vector"),zero_float3());}
  if(test==17){auto vm=g.create_node<VectorMapRangeNode>();vm->set_range_type(NODE_MAP_RANGE_SMOOTHERSTEP);vm->set_from_min(make_float3(-1,-1,-1));vm->set_from_max(make_float3(1,1,1));g.connect(dir,vm->input("Vector"));
    auto f=g.create_node<FloatCurveNode>();array<float> curve;for(unsigned i=0;i<RAMP_TABLE_SIZE;i++){const float t=float(i)/(RAMP_TABLE_SIZE-1);curve.push_back_slow(t*t);}f->set_curve(curve);f->set_min_x(-1);f->set_max_x(1);
    g.connect(component(dir,"X"),f->input("Value"));auto mix=g.create_node<MixVectorNode>();g.connect(vm->output("Vector"),mix->input("A"));g.connect(f->output("Value"),mix->input("Factor"));color=mix->output("Result");}
  if(test==18||test==19){auto v=g.create_node<VoronoiTextureNode>();v->set_dimensions(test==18?3:4);v->set_feature(test==18?NODE_VORONOI_SMOOTH_F1:NODE_VORONOI_DISTANCE_TO_EDGE);
    v->set_scale(4.f);v->set_detail(test==18?1.5f:2.f);v->set_use_normalize(true);g.connect(dir,v->input("Vector"));g.connect(component(dir,"Y"),v->input("W"));
    if(test==18)color=v->output("Color");else{auto c=g.create_node<CombineXYZNode>();g.connect(v->output("Distance"),c->input("X"));g.connect(v->output("Distance"),c->input("Z"));color=c->output("Vector");}}
  if(test==20){auto m=g.create_node<MapRangeNode>();m->set_from_min(-1);m->set_from_max(1);m->set_to_min(385);m->set_to_max(775);g.connect(component(dir,"Z"),m->input("Value"));
    auto w=g.create_node<WavelengthNode>();g.connect(m->output("Result"),w->input("Wavelength"));color=w->output("Color");}
  if(test==21){
    // Wireframe has no primitive in a world evaluation (0, as on the host).
    auto wire=g.create_node<WireframeNode>();wire->set_use_pixel_size(true);wire->set_size(3.f);auto n=noise(3,NODE_NOISE_FBM,1.5f,0);
    auto comb=g.create_node<CombineXYZNode>();g.connect(wire->output("Fac"),comb->input("X"));g.connect(n->output("Fac"),comb->input("Y"));g.connect(component(dir,"Z"),comb->input("Z"));color=comb->output("Vector");}
  if(test==22){
    // Bump in a world: the derivative variants of the copied height sub-graph run,
    // and the background node mask has no BUMP feature, so the host bump normal is
    // zero; the texture-coordinate tap reads the world direction.
    auto bump=g.create_node<BumpNode>();bump->set_filter_width(20.f);auto tc=g.create_node<TextureCoordinateNode>();auto sum=g.create_node<VectorMathNode>();sum->set_math_type(NODE_VECTOR_MATH_ADD);
    g.connect(dir,sum->input("Vector1"));g.connect(tc->output("Object"),sum->input("Vector2"));auto dot=g.create_node<VectorMathNode>();dot->set_math_type(NODE_VECTOR_MATH_DOT_PRODUCT);dot->set_vector2(make_float3(.6f,-.9f,1.2f));
    g.connect(sum->output("Vector"),dot->input("Vector1"));g.connect(dot->output("Value"),bump->input("Height"));
    auto add=g.create_node<VectorMathNode>();add->set_math_type(NODE_VECTOR_MATH_ADD);g.connect(bump->output("Normal"),add->input("Vector1"));g.connect(vmath(NODE_VECTOR_MATH_ABSOLUTE,dir,zero_float3()),add->input("Vector2"));
    color=add->output("Vector");}
  if(test==7){
    // Is Camera Ray selects between two backgrounds (camera rays at bounce 0 here).
    auto lp=g.create_node<LightPathNode>();auto other=g.create_node<BackgroundNode>();other->set_color(make_float3(.8,.1,.2));g.connect(noise(3,NODE_NOISE_MULTIFRACTAL,2,0)->output("Color"),bg->input("Color"));
    auto mix=g.create_node<MixClosureNode>();g.connect(lp->output("Is Camera Ray"),mix->input("Fac"));g.connect(bg->output("Background"),mix->input("Closure1"));g.connect(other->output("Background"),mix->input("Closure2"));
    g.connect(mix->output("Closure"),g.output()->input("Surface"));
  }else{g.connect(color,bg->input("Color"));g.connect(bg->output("Background"),g.output()->input("Surface"));}
  auto shader=scene.create_node<Shader>();shader->set_graph(std::move(graph));shader->tag_update(&scene);scene.background->set_shader(shader);
}
#endif
static void prepare_device(Scene &scene,WebGPUDevice &d){
  Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"SVM scene preparation failed");
  auto &data=scene.dscene.data;data.background.use_mis=0;data.integrator.num_lights=0;data.integrator.use_light_mis=0;data.integrator.use_caustics=0;
  d.const_copy_to("data",&data,sizeof(data));healthy(d);
}
static void inspect_program(DeviceScene &scene,Metrics &m){
  auto &words=scene.svm_nodes;unsigned shader=scene.data.background.surface_shader&SHADER_MASK;unsigned at=shader*(1+sizeof(SVMNodeShaderJump)/4);
  require(words[at]==NODE_SHADER_JUMP,"Missing shader jump entry");m.opcodes.insert(NODE_SHADER_JUMP);at=words[at+1];
  for(unsigned n=0;n<65536;n++){
    require(at<words.size(),"Fixture program outside table");uint op=words[at++];m.opcodes.insert(op);if(op==NODE_END)return;
    unsigned extra=0;if(op==NODE_TEX_COORD){auto *node=reinterpret_cast<const SVMNodeTexCoord*>(words.data()+at);if(node->texco_type==NODE_TEXCO_OBJECT_WITH_TRANSFORM)extra=sizeof(PackedTransform)/4;}
    if(op==NODE_RGB_RAMP)extra=reinterpret_cast<const SVMNodeRGBRamp*>(words.data()+at)->table_size*4;
    if(op==NODE_CURVES)extra=reinterpret_cast<const SVMNodeCurves*>(words.data()+at)->table_size*4;
    if(op==NODE_FLOAT_CURVE)extra=reinterpret_cast<const SVMNodeFloatCurve*>(words.data()+at)->table_size;
    at+=node_size(op)+extra;
  }throw std::runtime_error("Fixture program missing END");
}
static void compare(WebGPUDevice &d,const std::vector<Field> &layout,const std::vector<IntegratorStateCPU> &wanted,device_vector<float> &film,
                    const std::vector<float> &expected,unsigned paths,const std::string &label,Metrics &m){
  auto actual=download(d,layout,wanted.size());for(auto &f:layout)for(size_t i=0;i<wanted.size();i++){
    require(!std::memcmp(reinterpret_cast<const char*>(&actual[i])+f.offset,reinterpret_cast<const char*>(&wanted[i])+f.offset,f.stride),label+" changed native state "+f.name);m.state_checks++;
  }
  film.copy_from_device();healthy(d);for(size_t i=0;i<expected.size();i++){
    const double error=std::abs(double(film[i])-expected[i]);m.maximum_error=std::max(m.maximum_error,error);m.float_checks++;
    if(error>1e-7+1e-7*std::abs(double(expected[i])))m.strict_failures++;
    require(std::isfinite(film[i])&&error<=5e-5+2e-4*std::abs(double(expected[i])),label+" film mismatch at "+std::to_string(i)+" actual="+std::to_string(film[i])+" expected="+std::to_string(expected[i]));
  }
  IntegratorQueueCounter counts{};d.download(uintptr_t(d.state.queue_counter),&counts,sizeof(counts));for(int n:counts.num_queued)require(n==0,label+" did not terminate paths");require(!counts.cache_miss,"Unexpected shader cache miss");m.cases++;m.paths+=paths;
}
static void image(const std::string &path,DeviceScene &scene,device_vector<float> &film){
  std::ofstream out(path,std::ios::binary);out<<"P6\n64 48\n255\n";for(unsigned i=0;i<3072;i++)for(unsigned c=0;c<3;c++){
    const float v=film[i*scene.data.film.pass_stride+scene.data.film.pass_combined+c];const unsigned char b=unsigned(std::lround(std::clamp(v,0.f,1.f)*255));out.write(reinterpret_cast<const char*>(&b),1);
  }
}
static void execute_program(Scene &scene,WebGPUDevice &d,Scheduler &scheduler,const std::vector<Field> &layout,const std::string &label,Metrics &m){
  auto buffers=scheduler.get_render_buffers();buffers->zero();scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"SVM reset failed");
  std::vector<IntegratorStateCPU> states(scheduler.capacity());std::vector<int> indices;
  for(unsigned i=0;i<257;i++){int index=i*7;indices.push_back(index);auto &s=states[index];s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND;s.path.render_pixel_index=i;
    s.path.visibility=PATH_RAY_VISIBILITY_CAMERA;s.path.throughput=one_float3();s.path.sample=i;s.path.bounce=i%4;s.isect.prim=PRIM_NONE;s.isect.object=OBJECT_NONE;
    s.ray.D=normalize(make_float3((i%2?-1:1)*(.03f+.017f*(i%53)),-.7f+.029f*(i%47),.2f+.013f*(i%37)));
    // Exact and near inverse-trigonometric endpoints, both signed zeros, and
    // both sides of the half-angle reduction threshold are real ray directions.
    constexpr float edge[]={-1.f,-.99999994f,-.50000006f,-.5f,-.49999997f,-0.f,0.f,.49999997f,.5f,.50000006f,.99999994f,1.f};
    if(i<std::size(edge))s.ray.D=make_float3(edge[i],std::sqrt(std::max(0.f,1-edge[i]*edge[i])),0);
    s.ray.P=make_float3(.1,.2,.3);s.ray.time=.5f;s.ray.dD=.001f;
  }
  upload(d,layout,states);IntegratorQueueCounter counts{};counts.num_queued[DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND]=indices.size();d.upload(uintptr_t(d.state.queue_counter),&counts,sizeof(counts));
  scheduler.set_active(1793);require(scheduler.update_queue_counter_and_cache(),"SVM counter update failed");std::vector<float> expected(buffers->buffer.size());
  original_background(scene.dscene,indices,states,expected);scheduler.enqueue_path_iteration(DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND);require(scheduler.update_queue_counter_and_cache(),label+": "+d.error_message());
  compare(d,layout,states,buffers->buffer,expected,indices.size(),label,m);
}
static unsigned rejections(const DeviceInfo &info){unsigned rejected=0;
  for(unsigned test=0;test<10;test++){
    Stats stats;Profiler profiler;auto owner=Device::create(info,stats,profiler,true);auto &d=*dynamic_cast<WebGPUDevice*>(owner.get());SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.use_texture_cache=false;Scene scene(params,&d);graph_scene(scene,0);prepare_device(scene,d);
    Program code(scene.dscene);uint mask=0;
    if(test==0){code.words.push_back(NODE_NUM);mask=2;}
    if(test==1){code.words.push_back(NODE_MATH);mask=4;}
    if(test==2){code.add(NODE_EMISSION_WEIGHT,SVMNodeEmissionWeight{.color=slot3(255),.strength=value(1)});mask=8;}
    if(test==3){code.add(NODE_CLOSURE_WEIGHT,SVMNodeClosureWeight{.weight_offset=7});mask=8;}
    if(test==4){code.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=254,.value=make_float3(1,2,3)});mask=8;}
    if(test==5){code.add(NODE_MATH,SVMNodeMath{.math_type=NodeMathType(0xffffffffu),.value1=value(1),.value2=value(2),.value3=value(3),.result_offset=0});mask=16;}
    if(test==6){code.words[code.shader*4+1]=0x7fffffffu;mask=4;}
    if(test==7){code.words[code.shader*4+1]=code.shader*4;mask=32;}
    if(test==8){code.add(NODE_SEPARATE_VECTOR,SVMNodeSeparateVector{.vector=value3(1,2,3),.vector_index=3,.out_offset=0});mask=16;}
    if(test==9){code.add(NODE_JUMP_IF_ZERO,SVMNodeJumpIfZero{.jump_offset=1,.stack_offset=253});mask=8;}
    if(test!=1)code.words.push_back(NODE_END);
    code.install(scene.dscene);healthy(d);
    bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();auto layout=fields(d);
    std::vector<IntegratorStateCPU> states(scheduler.capacity());auto &s=states[0];s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND;s.path.throughput=one_float3();s.path.visibility=PATH_RAY_VISIBILITY_CAMERA;s.ray.D=make_float3(0,0,1);s.isect.prim=PRIM_NONE;upload(d,layout,states);
    IntegratorQueueCounter counts{};counts.num_queued[DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND]=1;d.upload(uintptr_t(d.state.queue_counter),&counts,sizeof(counts));
    device_vector<float> film(&d,"negative_svm_film",MEM_READ_WRITE);film.alloc(scene.dscene.data.film.pass_stride);std::fill_n(film.data(),film.size(),.25f);film.copy_to_device();
    device_ptr null=0;int count=1;DeviceKernelArguments args;args.add(&null);args.add(&film.device_pointer);args.add(&count);auto queue=d.gpu_queue_create();bool accepted=queue->enqueue(DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND,1,args);if(accepted)accepted=queue->synchronize();require(!accepted,"Malformed SVM was accepted");
    uint error=0;d.download(d.error_word,&error,4);require(error==mask,"Wrong SVM diagnostic for "+std::to_string(test)+": "+std::to_string(error));
    require(d.error_message().find("mask "+std::to_string(mask))!=std::string::npos,"Missing native SVM error diagnostic");
    std::vector<float> after(film.size());d.download(film.device_pointer,after.data(),after.size()*4);require(std::all_of(after.begin(),after.end(),[](float v){return v==.25f;}),"Malformed SVM changed film");
    IntegratorQueueCounter final{};d.download(uintptr_t(d.state.queue_counter),&final,sizeof(final));require(!std::memcmp(&counts,&final,sizeof(final)),"Malformed SVM changed queue counts");rejected++;
  }return rejected;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2,"Usage: svm_world_gate OUTPUT_PREFIX");const std::string output=argv[1];auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=*dynamic_cast<WebGPUDevice*>(owner.get());Metrics m;
#ifdef CBC_NODES_FIXTURE
  for(unsigned test=0;test<WORLD_COUNT;test++){
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.use_texture_cache=false;Scene scene(params,&d);nodes_world(scene,test);
#else
  for(unsigned test=0;test<20;test++){
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.use_texture_cache=false;Scene scene(params,&d);graph_scene(scene,test);
#endif
    prepare_device(scene,d);
    require(!(scene.dscene.shaders[scene.dscene.data.background.surface_shader&SHADER_MASK].flags&SD_HAS_CONSTANT_EMISSION),"World unexpectedly constant-folded");inspect_program(scene.dscene,m);
    bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();auto layout=fields(d);
    BufferParams bp;bp.width=64;bp.height=48;bp.pass_stride=scene.dscene.data.film.pass_stride;auto buffers=scheduler.get_render_buffers();buffers->reset(bp);buffers->zero();scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Compiled-world reset failed");
    KernelWorkTile tile{};tile.w=64;tile.h=48;tile.stride=64;tile.num_samples=1;tile.start_sample=3;scheduler.enqueue_work_tiles(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA,&tile,1,0,0);require(scheduler.update_queue_counter_and_cache(),"SVM camera failed");scheduler.enqueue_path_iteration(DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST);require(scheduler.update_queue_counter_and_cache(),"SVM closest failed");
    auto states=download(d,layout,scheduler.capacity());std::vector<int> indices;for(unsigned i=0;i<states.size();i++)if(states[i].path.queued_kernel)indices.push_back(i);require(indices.size()==3072,"Compiled world path count differs");std::vector<float> expected(buffers->buffer.size());original_background(scene.dscene,indices,states,expected);
    scheduler.enqueue_path_iteration(DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND);require(scheduler.update_queue_counter_and_cache(),"Compiled world "+std::to_string(test)+": "+d.error_message());compare(d,layout,states,buffers->buffer,expected,indices.size(),"Compiled world "+std::to_string(test),m);
    image(output+".world-"+std::to_string(test)+".ppm",scene.dscene,buffers->buffer);m.compiled_worlds++;
    std::ofstream bytes(output+".world-"+std::to_string(test)+".svm.bin",std::ios::binary);bytes.write(reinterpret_cast<const char*>(scene.dscene.svm_nodes.data()),scene.dscene.svm_nodes.size()*4);
  }
  {
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.use_texture_cache=false;Scene scene(params,&d);graph_scene(scene,0);prepare_device(scene,d);bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();auto layout=fields(d);
    BufferParams bp;bp.width=64;bp.height=48;bp.pass_stride=scene.dscene.data.film.pass_stride;scheduler.get_render_buffers()->reset(bp);
    for(unsigned kind=0;kind<=NODE_MATH_FLOORED_MODULO;kind++)for(unsigned mode=0;mode<2;mode++){
      Program code(scene.dscene);code.geometry();code.add(NODE_MATH,SVMNodeMath{.math_type=NodeMathType(kind),.value1=slot(0),.value2=value(mode?0:2.25f),.value3=value(.3f),.result_offset=3});code.add(NODE_CONVERT,SVMNodeConvert{.convert_type=NODE_CONVERT_FV,.from_offset=3,.to_offset=4});code.result(4);code.install(scene.dscene);inspect_program(scene.dscene,m);execute_program(scene,d,scheduler,layout,"Scalar "+std::to_string(kind)+" mode "+std::to_string(mode),m);
    }
    for(unsigned kind=0;kind<=NODE_VECTOR_MATH_ROUND;kind++)for(unsigned mode=0;mode<2;mode++){
      Program code(scene.dscene);code.geometry();code.add(NODE_VECTOR_MATH,SVMNodeVectorMath{.math_type=NodeVectorMathType(kind),.a=slot3(0),.b=mode?value3(0,0,0):value3(.3f,-.7f,1.1f),.c=value3(-.2f,.4f,-.1f),.param1=value(.7f),.value_offset=3,.vector_offset=4});
      code.add(NODE_CONVERT,SVMNodeConvert{.convert_type=NODE_CONVERT_FV,.from_offset=3,.to_offset=7});code.add(NODE_VECTOR_MATH,SVMNodeVectorMath{.math_type=NODE_VECTOR_MATH_ADD,.a=slot3(4),.b=slot3(7),.c=value3(0,0,0),.param1=value(1),.value_offset=SVM_STACK_INVALID,.vector_offset=10});code.result(10);code.install(scene.dscene);inspect_program(scene.dscene,m);execute_program(scene,d,scheduler,layout,"Vector "+std::to_string(kind)+" mode "+std::to_string(mode),m);
    }
    for(unsigned kind=0;kind<NODE_CONVERT_NONE;kind++){
      Program code(scene.dscene);code.add(NODE_VALUE_V,SVMNodeValueV{.out_offset=0,.value=make_float3(-2.75f,3.25f,.75f)});
      if(kind==NODE_CONVERT_IF||kind==NODE_CONVERT_IV)code.add(NODE_VALUE_F,SVMNodeValueF{.value=std::bit_cast<float>(int(-7)),.out_offset=0});
      code.add(NODE_CONVERT,SVMNodeConvert{.convert_type=NodeConvert(kind),.from_offset=0,.to_offset=3});unsigned result=3;
      if(kind==NODE_CONVERT_FI||kind==NODE_CONVERT_CI||kind==NODE_CONVERT_VI){code.add(NODE_CONVERT,SVMNodeConvert{.convert_type=NODE_CONVERT_IV,.from_offset=3,.to_offset=4});result=4;}
      else if(kind==NODE_CONVERT_CF||kind==NODE_CONVERT_VF||kind==NODE_CONVERT_IF){code.add(NODE_CONVERT,SVMNodeConvert{.convert_type=NODE_CONVERT_FV,.from_offset=3,.to_offset=4});result=4;}
      code.result(result);code.install(scene.dscene);inspect_program(scene.dscene,m);execute_program(scene,d,scheduler,layout,"Convert "+std::to_string(kind),m);
    }
    for(unsigned kind=0;kind<6;kind++){
      Program code(scene.dscene);code.add(NODE_GEOMETRY,SVMNodeGeometry{.geom_type=NodeGeometry(kind),.out_offset=0});code.result(0);code.install(scene.dscene);inspect_program(scene.dscene,m);execute_program(scene,d,scheduler,layout,"Geometry "+std::to_string(kind),m);
    }
    // Both conditional jump outcomes; END, immediate/stack closure weights,
    // mix weights and accumulation of background plus emission closures.
    for(unsigned mode=0;mode<4;mode++){
      Program code(scene.dscene);code.add(NODE_VALUE_F,SVMNodeValueF{.value=mode%2?1.f:0.f,.out_offset=0});
      unsigned jump=mode<2?code.add(NODE_JUMP_IF_ZERO,SVMNodeJumpIfZero{.stack_offset=0}):code.add(NODE_JUMP_IF_ONE,SVMNodeJumpIfOne{.stack_offset=0});
      code.add(NODE_CLOSURE_SET_WEIGHT,SVMNodeClosureSetWeight{.rgb=make_float3(.2f,.4f,.6f)});code.add(NODE_CLOSURE_BACKGROUND,SVMNodeClosureBackground{.mix_weight_offset=SVM_STACK_INVALID});
      code.words[jump+1]=code.words.size()-(jump+1+sizeof(SVMNodeJumpIfZero)/4);
      code.add(NODE_MIX_CLOSURE,SVMNodeMixClosure{.fac=value(.3f),.in_weight_offset=SVM_STACK_INVALID,.weight1_offset=1,.weight2_offset=2});
      code.add(NODE_EMISSION_WEIGHT,SVMNodeEmissionWeight{.color=value3(.7f,.3f,.1f),.strength=value(2)});code.add(NODE_CLOSURE_EMISSION,SVMNodeClosureEmission{.mix_weight_offset=2});code.words.push_back(NODE_END);
      code.install(scene.dscene);inspect_program(scene.dscene,m);execute_program(scene,d,scheduler,layout,"Control "+std::to_string(mode),m);
    }
    // Typed vector components are independent stores, then assembled as RGB.
    Program code(scene.dscene);code.geometry();for(unsigned i=0;i<3;i++)code.add(NODE_COMBINE_VECTOR,SVMNodeCombineVector{.in=slot(i),.vector_index=uint8_t(2-i),.out_offset=3});code.result(3);code.install(scene.dscene);inspect_program(scene.dscene,m);execute_program(scene,d,scheduler,layout,"Combine components",m);
  }
  require(stats.mem_used==0,"SVM gate leaked Cycles allocations");unsigned rejected=rejections(infos[0]);std::ofstream(output+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(output+".library.txt")<<d.gpu.library_path;
  std::cout<<"{\"passed\":true,\"nativeSvmBytecode\":true,\"originalCameraClosestBackgroundScheduler\":true,\"compiledWorlds\":"<<m.compiled_worlds<<",\"cases\":"<<m.cases<<",\"paths\":"<<m.paths<<",\"opcodesCovered\":"<<m.opcodes.size()<<",\"scalarMathOperations\":"<<NODE_MATH_FLOORED_MODULO+1<<",\"vectorMathOperations\":"<<NODE_VECTOR_MATH_ROUND+1<<",\"conversionKinds\":"<<NODE_CONVERT_NONE<<",\"exactStateChecks\":"<<m.state_checks<<",\"filmFloatChecks\":"<<m.float_checks<<",\"strictFloatFailures\":"<<m.strict_failures<<",\"maximumAbsoluteError\":"<<m.maximum_error<<",\"invalidCasesRejected\":"<<rejected<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
