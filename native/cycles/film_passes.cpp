// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Film passes beyond Combined on the WebGPU device (pinned Cycles film/data_passes.h,
// film/light_passes.h, film/aov_passes.h, film/cryptomatte_passes.h,
// film/denoising_passes.h; the film layout of scene/film.cpp Film::device_update):
// the data passes the device writes, the explicit refusal of every other one,
// and the WGSL constants of the FILM_DATA_PASSES region family
// (wavefront_film_passes.wgsl). The host reads every pass with the pinned pass
// accessor (cycles_host.cpp HostOutput); nothing on the device converts passes.
#include "device.h"
#include "wavefront.h"
#include "kernel/geom/object.h"
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
namespace ccl {
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static void require(bool ok,const std::string &message){if(!ok)throw std::runtime_error(message);}
// Data passes (KernelFilm::pass_flag, PASS_CATEGORY_DATA) whose writes the device
// implements: the shadow catcher passes (shadow_catcher.cpp) and the
// film_write_data_passes passes of the surface and background stages.
int film_data_pass_mask(){
  return shadow_catcher_pass_mask()|PASSMASK(DEPTH)|PASSMASK(POSITION)|PASSMASK(NORMAL)|PASSMASK(ROUGHNESS)|PASSMASK(UV)|
         PASSMASK(OBJECT_ID)|PASSMASK(MATERIAL_ID)|PASSMASK(DIFFUSE_COLOR)|PASSMASK(GLOSSY_COLOR)|PASSMASK(TRANSMISSION_COLOR)|
         PASSMASK(MIST)|PASSMASK(RENDER_TIME)|PASSMASK(AOV_COLOR)|PASSMASK(AOV_VALUE)|PASSMASK(CRYPTOMATTE)|PASSMASK(MOTION)|PASSMASK(MOTION_WEIGHT)|
         PASSMASK(SAMPLE_COUNT)|PASSMASK(ADAPTIVE_AUX_BUFFER)|PASSMASK(VOLUME_MAJORANT)|PASSMASK(VOLUME_MAJORANT_SAMPLE_COUNT);
}
// Data passes written by the surface stage's film_write_data_passes (the
// shade_surface_passes entry; FILM_DATA_PASSES).
// (The camera stage counts samples, film_write_sample; AOVs are written by shader evaluation.)
// The adaptive sampling buffer is written with the combined pass (film_write_adaptive_buffer) and
// read by the adaptive sampling kernels (enqueue_adaptive_sampling).
static int surface_data_pass_mask(){return film_data_pass_mask()&~shadow_catcher_pass_mask()&~(PASSMASK(RENDER_TIME)|PASSMASK(AOV_COLOR)|PASSMASK(AOV_VALUE)|PASSMASK(SAMPLE_COUNT)|PASSMASK(ADAPTIVE_AUX_BUFFER)|PASSMASK(VOLUME_MAJORANT)|PASSMASK(VOLUME_MAJORANT_SAMPLE_COUNT));}
bool WebGPUDevice::film_surface_passes() const{return (data.film.pass_flag&surface_data_pass_mask())!=0;}
bool WebGPUDevice::film_cryptomatte() const{return data.film.cryptomatte_passes&&(data.film.pass_flag&PASSMASK(CRYPTOMATTE));}
static const char *data_pass_name(int bit){
  switch(PassType(PASS_CATEGORY_LIGHT_END+1+bit)){
    case PASS_DEPTH:return "depth";case PASS_POSITION:return "position";case PASS_NORMAL:return "normal";case PASS_ROUGHNESS:return "roughness";
    case PASS_UV:return "uv";case PASS_OBJECT_ID:return "object_id";case PASS_MATERIAL_ID:return "material_id";case PASS_MOTION:return "motion";
    case PASS_MOTION_WEIGHT:return "motion_weight";case PASS_CRYPTOMATTE:return "cryptomatte";case PASS_AOV_COLOR:return "aov_color";
    case PASS_AOV_VALUE:return "aov_value";case PASS_ADAPTIVE_AUX_BUFFER:return "adaptive_aux_buffer";case PASS_SAMPLE_COUNT:return "sample_count";
    case PASS_DIFFUSE_COLOR:return "diffuse_color";case PASS_GLOSSY_COLOR:return "glossy_color";case PASS_TRANSMISSION_COLOR:return "transmission_color";
    case PASS_MIST:return "mist";case PASS_RENDER_TIME:return "render_time";
    case PASS_GUIDING_COLOR:return "guiding_color";case PASS_GUIDING_PROBABILITY:return "guiding_probability";
    case PASS_GUIDING_AVG_ROUGHNESS:return "guiding_avg_roughness";case PASS_VOLUME_MAJORANT:return "volume_majorant";
    case PASS_VOLUME_MAJORANT_SAMPLE_COUNT:return "volume_majorant_sample_count";default:return nullptr;
  }
}
// Every stage that writes the film calls this: every data pass the scene enables
// is one the device writes, or the render is refused with the pass named (never
// left unwritten or approximated); each written pass lies inside the film record.
// PASS_RENDER_TIME is accepted unwritten: the pinned GPU devices never write it
// (only PathTraceWorkCPU measures it), so it reads zero as on Cycles' Metal device.
void WebGPUDevice::validate_film_passes() const{
  const int refused=data.film.pass_flag&~film_data_pass_mask();
  for(int bit=0;bit<32;bit++)if(refused&(1<<bit)){
    const char *name=data_pass_name(bit);
    require(false,std::string("WebGPU film pass ")+(name?name:"(unknown data pass)")+" not implemented");
  }
  // An enabled pass (its flag set; mask 0: the offset itself, which Film::device_update resets)
  // lies inside the film record. Film::device_update does not reset every offset (for example
  // the AOV, mist and cryptomatte offsets of a removed pass stay), so only enabled passes are checked.
  auto pass=[&](int offset,int length,int mask){
    if(mask&&!(data.film.pass_flag&mask))return;
    require(!mask||offset!=PASS_UNUSED,"Missing film data pass");
    require(offset==PASS_UNUSED||(offset>=0&&offset<=data.film.pass_stride-length),"Invalid film data pass offset");
  };
  pass(data.film.pass_depth,1,PASSMASK(DEPTH));pass(data.film.pass_position,3,PASSMASK(POSITION));
  pass(data.film.pass_normal,3,PASSMASK(NORMAL));pass(data.film.pass_roughness,1,PASSMASK(ROUGHNESS));
  pass(data.film.pass_uv,3,PASSMASK(UV));pass(data.film.pass_object_id,1,PASSMASK(OBJECT_ID));pass(data.film.pass_material_id,1,PASSMASK(MATERIAL_ID));
  pass(data.film.pass_diffuse_color,3,PASSMASK(DIFFUSE_COLOR));pass(data.film.pass_glossy_color,3,PASSMASK(GLOSSY_COLOR));
  pass(data.film.pass_transmission_color,3,PASSMASK(TRANSMISSION_COLOR));pass(data.film.pass_mist,1,PASSMASK(MIST));
  // Cryptomatte: cryptomatte_depth passes of two (id, weight) slots per enabled type. Written
  // only with cryptomatte passes in the film (the film's cryptomatte_passes setting outlives
  // removed passes, and Film::device_update keeps their old offset).
  if(film_cryptomatte()){
    const int types=bool(data.film.cryptomatte_passes&CRYPT_OBJECT)+bool(data.film.cryptomatte_passes&CRYPT_MATERIAL)+bool(data.film.cryptomatte_passes&CRYPT_ASSET);
    require(data.film.cryptomatte_depth>0&&data.film.cryptomatte_depth<=data.film.pass_stride&&types>0,"Invalid cryptomatte settings");
    pass(data.film.pass_cryptomatte,4*data.film.cryptomatte_depth*types,PASSMASK(CRYPTOMATTE));
    require(data.film.pass_cryptomatte!=PASS_UNUSED,"Missing cryptomatte film passes");
  }
  // Motion pass (Scene::MOTION_PASS: the film has the motion pass and there is no motion
  // blur): the object motion pass transforms, the camera's pre/post projections.
  pass(data.film.pass_motion,4,PASSMASK(MOTION));pass(data.film.pass_motion_weight,1,PASSMASK(MOTION));
  if(film_motion_vectors()){
    // Perspective and orthographic cameras project with worldtoraster and perspective_pre/post; panoramic ones through
    // direction_to_panorama (CAMERA_PANORAMA_INVERSE); custom (OSL) cameras are refused with the camera.
    const auto table=globals.find("object_motion_pass");
    require(table!=globals.end()&&remaining(table->second)>=object_records.size()*OBJECT_MOTION_PASS_SIZE*sizeof(Transform),"Missing native object motion pass transforms");
    // Deforming geometry (SD_OBJECT_HAS_VERTEX_MOTION on plain triangles): FILM_MOTION_DEFORM (pipelines.h).
  }
  // AOV passes: the first colour and value AOV (each node's offset is checked when it writes).
  pass(data.film.pass_aov_color,4,PASSMASK(AOV_COLOR));pass(data.film.pass_aov_value,1,PASSMASK(AOV_VALUE));
  require(!(data.film.pass_flag&PASSMASK(MIST))||(std::isfinite(data.film.mist_start)&&std::isfinite(data.film.mist_inv_depth)&&
          std::isfinite(data.film.mist_falloff)),"Invalid film mist settings");
  require(std::isfinite(data.film.pass_alpha_threshold),"Invalid film pass alpha threshold");
  // Denoising features (film/denoising_passes.h): albedo, normal, specular albedo, roughness and depth, and the
  // backward and specular motion features (with motion vectors). These are every pass type of the denoising category
  // (PASS_DENOISING_ALBEDO to PASS_DENOISING_SPECULAR_MOTION; Film::device_update sets denoising_pass_flag from no other
  // type), so any other bit is invalid data, not a pass to implement.
  const int denoising=PASSMASK(DENOISING_ALBEDO)|PASSMASK(DENOISING_NORMAL)|PASSMASK(DENOISING_SPECULAR_ALBEDO)|
                      PASSMASK(DENOISING_ROUGHNESS)|PASSMASK(DENOISING_DEPTH);
  require(!(data.film.denoising_pass_flag&~(denoising|PASSMASK(DENOISING_BACKWARD_MOTION)|PASSMASK(DENOISING_SPECULAR_MOTION))),
          "Invalid film denoising pass flags");
  pass(data.film.pass_denoising_albedo,3,0);pass(data.film.pass_denoising_normal,3,0);pass(data.film.pass_denoising_specular_albedo,3,0);
  pass(data.film.pass_denoising_roughness,1,0);pass(data.film.pass_denoising_depth,1,0);
  // The denoising motion features (Scene::MOTION_PASS through these passes): the motion pass's transforms.
  pass(data.film.pass_denoising_backward_motion,3,0);pass(data.film.pass_denoising_specular_motion,2,0);
}
// Constants of the FILM_DATA_PASSES region (wavefront_film_passes.wgsl); the
// markers make modules of scenes without these passes identical to modules
// without this layout.
std::string film_passes_layout(){
  std::ostringstream out;out<<"//#if FILM_PASSES_ANY\n";
  auto constant=[&](const char *name,size_t value){out<<"const "<<name<<":u32="<<value<<"u;\n";};
#define DATA(parent,member) constant("FP_D_" #member,offsetof(KernelData,parent.member))
  DATA(film,pass_normal);DATA(film,pass_roughness);DATA(film,pass_uv);DATA(film,pass_diffuse_color);DATA(film,pass_glossy_color);
  DATA(film,pass_transmission_color);DATA(film,pass_mist);DATA(film,mist_start);DATA(film,mist_inv_depth);DATA(film,mist_falloff);
  DATA(film,pass_alpha_threshold);DATA(cam,worldtocamera);DATA(cam,cameratoworld);DATA(cam,type);
  DATA(tables,ggx_gen_schlick_s);DATA(tables,ggx_gen_schlick_ior_s);
#undef DATA
  constant("FP_O_pass_id",offsetof(KernelObject,pass_id));constant("FP_SH_pass_id",offsetof(KernelShader,pass_id));
#define ENUM(name) constant("FP_" #name,uint32_t(name))
  ENUM(PATH_RAY_SHADOW_CATCHER_PASS);ENUM(SR_TRANSPARENT);ENUM(SR_RAY_PORTAL);ENUM(SD_HAS_ONLY_VOLUME);ENUM(ATTR_STD_UV);
  ENUM(CAMERA_ORTHOGRAPHIC);ENUM(OBJECT_NONE);
  ENUM(CLOSURE_BSDF_DIFFUSE_ID);ENUM(CLOSURE_BSDF_TRANSLUCENT_ID);ENUM(CLOSURE_BSDF_OREN_NAYAR_ID);ENUM(CLOSURE_BSDF_ROUGH_TRANSLUCENT_ID);
  ENUM(CLOSURE_BSDF_MICROFACET_GGX_ID);ENUM(CLOSURE_BSDF_MICROFACET_BECKMANN_ID);ENUM(CLOSURE_BSDF_HAIR_REFLECTION_ID);ENUM(CLOSURE_BSDF_ASHIKHMIN_SHIRLEY_ID);
  ENUM(CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID);ENUM(CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID);
  ENUM(CLOSURE_BSDF_THIN_GLASS_TRANSMISSION_ID);ENUM(CLOSURE_BSDF_HAIR_TRANSMISSION_ID);
  ENUM(CLOSURE_BSDF_MICROFACET_BECKMANN_GLASS_ID);ENUM(CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID);ENUM(CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID);
  ENUM(CLOSURE_BSDF_HAIR_CHIANG_ID);ENUM(CLOSURE_BSDF_HAIR_HUANG_ID);ENUM(CLOSURE_BSDF_RAY_PORTAL_ID);ENUM(CLOSURE_BSDF_TRANSPARENT_ID);
  ENUM(CLOSURE_BSSRDF_BURLEY_ID);ENUM(CLOSURE_BSSRDF_RANDOM_WALK_SKIN_ID);ENUM(CLOSURE_NONE_ID);
  out<<"//#if CAMERA_PROJECTIONS\n";ENUM(CAMERA_PANORAMA);out<<"//#endif\n";
#undef ENUM
#define PASS(name) constant("FP_MASK_" #name,uint32_t(PASSMASK(name)))
  PASS(NORMAL);PASS(ROUGHNESS);PASS(UV);PASS(DIFFUSE_COLOR);PASS(GLOSSY_COLOR);PASS(TRANSMISSION_COLOR);PASS(MIST);
#undef PASS
  out<<"//#endif\n//#if FILM_CRYPTOMATTE\n";
#define DATA(parent,member) constant("FPC_D_" #member,offsetof(KernelData,parent.member))
  DATA(film,cryptomatte_passes);DATA(film,cryptomatte_depth);DATA(film,pass_cryptomatte);
#undef DATA
  constant("FPC_O_cryptomatte_object",offsetof(KernelObject,cryptomatte_object));constant("FPC_O_cryptomatte_asset",offsetof(KernelObject,cryptomatte_asset));
  constant("FPC_SH_cryptomatte_id",offsetof(KernelShader,cryptomatte_id));
  constant("FPC_MASK_CRYPTOMATTE",PASSMASK(CRYPTOMATTE));constant("FPC_CRYPT_OBJECT",CRYPT_OBJECT);constant("FPC_CRYPT_MATERIAL",CRYPT_MATERIAL);constant("FPC_CRYPT_ASSET",CRYPT_ASSET);
  out<<"//#endif\n";return out.str();
}
// Constants of the FILM_MOTION region (motion pass and denoising motion features: wavefront_film_passes.wgsl and
// the background's camera_motion_vector_direction for the denoising backward motion feature).
std::string film_motion_layout(){
  std::ostringstream out;out<<"//#if FILM_MOTION\n";
  auto constant=[&](const char *name,size_t value){out<<"const "<<name<<":u32="<<value<<"u;\n";};
#define DATA(parent,member) constant("FPM_D_" #member,offsetof(KernelData,parent.member))
  DATA(film,pass_motion);DATA(film,pass_motion_weight);DATA(cam,worldtoraster);DATA(cam,perspective_pre);DATA(cam,perspective_post);
  DATA(film,pass_denoising_backward_motion);DATA(film,pass_denoising_specular_motion);DATA(cam,motion_pass_pre);
#undef DATA
  constant("FPM_MASK_MOTION",PASSMASK(MOTION));constant("FPM_CAMERA_ORTHOGRAPHIC",CAMERA_ORTHOGRAPHIC);
  constant("FPM_TRANSFORMS_PER_OBJECT",OBJECT_MOTION_PASS_SIZE);constant("FPM_PRE",OBJECT_PASS_MOTION_PRE);constant("FPM_POST",OBJECT_PASS_MOTION_POST);
  constant("FPM_TRANSFORM_BYTES",sizeof(Transform));
  out<<"//#endif\n";return out.str();
}
// The object motion pass transforms for the surface stage (push words 5 and 8).
// Motion vectors are written: the motion pass or a denoising motion feature
// (each is only in the film's pass flags under Scene::MOTION_PASS).
// Pass flags only: KernelData can reach the device before Film::device_update has run (for
// example for displacement or the background importance map), and its zeroed offsets are not
// PASS_UNUSED, while the specialization features only grow per device.
bool WebGPUDevice::film_motion_vectors() const{
  return (data.film.pass_flag&PASSMASK(MOTION))||(data.film.denoising_pass_flag&(PASSMASK(DENOISING_BACKWARD_MOTION)|PASSMASK(DENOISING_SPECULAR_MOTION)));
}
void WebGPUDevice::film_motion_words(std::array<uint32_t,32> &p) const{
  if(!film_motion_vectors())return;
  const uint32_t table=globals.at("object_motion_pass");
  p[5]=table;p[8]=uint32_t(remaining(table)/sizeof(Transform));
}
static constexpr char film_post_fragment[]=
#include "film_post_shader.inc"
;
// The film post-processing module (wavefront_film_post.wgsl): the controls module
// header and DEVICE_KERNEL_CRYPTOMATTE_POSTPROCESS.
std::string film_post_wgsl(){
  const std::string controls=controls_wgsl();const auto entry=controls.find("@compute");
  require(entry!=std::string::npos,"Controls module header");
  return controls.substr(0,entry)+film_post_fragment;
}
// DEVICE_KERNEL_CRYPTOMATTE_POSTPROCESS (PathTraceWorkGPU::cryptomatte_postproces):
// the render buffer, the pixel count, and the buffer offset and stride, which the
// pinned GPU kernel does not read (pixel_index * pass_stride from the buffer start).
void WebGPUDevice::enqueue_cryptomatte_postprocess(int work,const DeviceKernelArguments &args){
  require(args.count==4,"Cryptomatte post-processing argument count");
  require(args.types[0]==DeviceKernelArguments::POINTER&&args.sizes[0]==sizeof(device_ptr),"Invalid cryptomatte render buffer");
  require(args.types[1]==DeviceKernelArguments::INT32&&args.sizes[1]==4,"Invalid cryptomatte pixel count");
  int n;std::memcpy(&n,args.values[1],4);require(n>=0&&work>=n,"Incomplete cryptomatte post-processing dispatch");
  device_ptr buffer;std::memcpy(&buffer,args.values[0],sizeof(buffer));require(buffer%4==0,"Unaligned cryptomatte render buffer");
  validate_film_passes();
  if(!n||!film_cryptomatte())return;
  validate(buffer,size_t(n)*data.film.pass_stride*4);
  if(!film_post.value)film_post=std::move(build_pipelines("film_post",film_post_wgsl(),{"cryptomatte_postprocess"})[0]);
  std::array<uint32_t,32> p{};p[1]=uint32_t(n);p[4]=uint32_t(buffer);p[5]=uint32_t(data.film.pass_stride);
  p[6]=uint32_t(data.film.pass_cryptomatte);p[7]=uint32_t(2*data.film.cryptomatte_depth);p[17]=error_word;
  // film_cryptomatte_post sorts 2 * cryptomatte_depth slots from pass_cryptomatte.
  require(size_t(data.film.pass_cryptomatte)+4*size_t(data.film.cryptomatte_depth)<=size_t(data.film.pass_stride),"Invalid cryptomatte post-processing range");
  dispatch(p,uint32_t(n),film_post,workgroup("cryptomatte_postprocess"));
}
static constexpr char film_adaptive_fragment[]=
#include "film_adaptive_shader.inc"
;
// The adaptive sampling module (wavefront_film_adaptive.wgsl): the controls module
// header, the contraction-proof binary32 helpers (wavefront_float.wgsl) and
// DEVICE_KERNEL_ADAPTIVE_SAMPLING_CONVERGENCE_CHECK, _FILTER_X and _FILTER_Y.
std::string film_adaptive_wgsl(){
  const std::string controls=controls_wgsl();const auto entry=controls.find("@compute");
  require(entry!=std::string::npos,"Controls module header");
  return controls.substr(0,entry)+film_adaptive_fragment;
}
static constexpr char film_volume_guiding_fragment[]=
#include "film_volume_guiding_shader.inc"
;
// The volume guiding module (wavefront_film_volume_guiding.wgsl): the controls module header, the
// contraction-proof binary32 helpers (wavefront_float.wgsl) and DEVICE_KERNEL_VOLUME_GUIDING_FILTER_X and _Y.
std::string film_volume_guiding_wgsl(){
  const std::string controls=controls_wgsl();const auto entry=controls.find("@compute");
  require(entry!=std::string::npos,"Controls module header");
  return controls.substr(0,entry)+film_volume_guiding_fragment;
}
// Volume scattering probability guiding (PathTraceWorkGPU::denoise_volume_guiding_buffers): the render buffer,
// the effective buffer's full_x, full_y, width and height, and the buffer offset and stride. FILTER_X filters
// each pixel's scatter and transmit passes along x into the RGBE-encoded denoised passes, FILTER_Y each
// column of those along y in place, as the pinned GPU kernels.
void WebGPUDevice::enqueue_volume_guiding(DeviceKernel kernel,int work,const DeviceKernelArguments &args){
  require(args.count==7u,"Volume guiding argument count");
  auto integer=[&](unsigned i){require(args.types[i]==DeviceKernelArguments::INT32&&args.sizes[i]==4,"Invalid volume guiding integer argument");
    int32_t v;std::memcpy(&v,args.values[i],4);return v;};
  require(args.types[0]==DeviceKernelArguments::POINTER&&args.sizes[0]==sizeof(device_ptr),"Invalid volume guiding pointer argument");
  device_ptr buffer;std::memcpy(&buffer,args.values[0],sizeof(buffer));require(buffer%4==0,"Unaligned volume guiding pointer");
  const int sx=integer(1),sy=integer(2),sw=integer(3),sh=integer(4),offset=integer(5),stride=integer(6);
  const bool x_pass=kernel==DEVICE_KERNEL_VOLUME_GUIDING_FILTER_X;
  require(sx>=0&&sy>=0&&sw>0&&sh>0&&stride>=sx+sw,"Invalid volume guiding region");
  require(work>=(x_pass?sw*sh:sw),"Incomplete volume guiding dispatch");
  auto pass=[&](int at,int length){require(at!=PASS_UNUSED&&at>=0&&at<=data.film.pass_stride-length,"Invalid volume guiding pass");return uint32_t(at);};
  const int64_t first=int64_t(offset)+sx+int64_t(sy)*stride,last=int64_t(offset)+sx+sw-1+int64_t(sy+sh-1)*stride;
  require(first>=0&&last<int64_t(INT32_MAX),"Invalid volume guiding buffer offset");
  validate(buffer,size_t(last+1)*data.film.pass_stride*4);
  if(!film_volume_x.value){
    auto built=build_pipelines("film_volume_guiding",film_volume_guiding_wgsl(),{"volume_guiding_filter_x","volume_guiding_filter_y"});
    film_volume_x=std::move(built[0]);film_volume_y=std::move(built[1]);
  }
  std::array<uint32_t,32> p{};p[1]=uint32_t(work);p[4]=uint32_t(buffer);p[5]=uint32_t(data.film.pass_stride);
  p[6]=uint32_t(sx);p[7]=uint32_t(sy);p[8]=uint32_t(sw);p[9]=uint32_t(sh);p[10]=uint32_t(offset);p[12]=uint32_t(stride);
  p[16]=pass(data.film.pass_volume_scatter,3);p[18]=pass(data.film.pass_volume_transmit,3);p[19]=pass(data.film.pass_sample_count,1);
  p[20]=pass(data.film.pass_volume_scatter_denoised,1);p[21]=pass(data.film.pass_volume_transmit_denoised,1);p[17]=error_word;
  if(x_pass)dispatch(p,uint32_t(work),film_volume_x,workgroup("volume_guiding_filter_x"));
  else dispatch(p,uint32_t(work),film_volume_y,workgroup("volume_guiding_filter_y"));
}
// Adaptive sampling (PathTraceWorkGPU::adaptive_sampling_convergence_check_count_active,
// enqueue_adaptive_sampling_filter_x/_y): the render buffer, the effective buffer's full_x,
// full_y, width and height, (convergence check: the threshold, reset and the active pixel
// counter), and the buffer offset and stride, as the pinned GPU kernels take them.
void WebGPUDevice::enqueue_adaptive_sampling(DeviceKernel kernel,int work,const DeviceKernelArguments &args){
  const bool check=kernel==DEVICE_KERNEL_ADAPTIVE_SAMPLING_CONVERGENCE_CHECK;
  require(args.count==(check?10u:7u),"Adaptive sampling argument count");
  auto integer=[&](unsigned i){require(args.types[i]==DeviceKernelArguments::INT32&&args.sizes[i]==4,"Invalid adaptive sampling integer argument");
    int32_t v;std::memcpy(&v,args.values[i],4);return v;};
  auto pointer=[&](unsigned i){require(args.types[i]==DeviceKernelArguments::POINTER&&args.sizes[i]==sizeof(device_ptr),"Invalid adaptive sampling pointer argument");
    device_ptr v;std::memcpy(&v,args.values[i],sizeof(v));require(v%4==0,"Unaligned adaptive sampling pointer");return v;};
  const device_ptr buffer=pointer(0);
  const int sx=integer(1),sy=integer(2),sw=integer(3),sh=integer(4);
  float threshold=0;int reset=0;device_ptr active=0;unsigned at=5;
  if(check){
    require(args.types[5]==DeviceKernelArguments::FLOAT32&&args.sizes[5]==4,"Invalid adaptive sampling threshold");std::memcpy(&threshold,args.values[5],4);
    reset=integer(6);at=7;active=pointer(9);validate(active,4);
  }
  const int offset=integer(at),stride=integer(at+1);
  require(sx>=0&&sy>=0&&sw>0&&sh>0&&stride>=sx+sw,"Invalid adaptive sampling region");
  require(work>=(check?sw*sh:kernel==DEVICE_KERNEL_ADAPTIVE_SAMPLING_CONVERGENCE_FILTER_X?sh:sw),"Incomplete adaptive sampling dispatch");
  require(data.film.pass_adaptive_aux_buffer!=PASS_UNUSED&&data.film.pass_adaptive_aux_buffer>=0&&data.film.pass_adaptive_aux_buffer<=data.film.pass_stride-4,
          "Invalid adaptive sampling buffer pass");
  if(check)require(data.film.pass_sample_count!=PASS_UNUSED&&data.film.pass_sample_count>=0&&data.film.pass_sample_count<data.film.pass_stride&&
                   data.film.pass_combined!=PASS_UNUSED&&data.film.pass_combined>=0&&data.film.pass_combined<=data.film.pass_stride-4,"Invalid adaptive sampling passes");
  // Every pixel the kernels index (offset + x + y * stride) lies in the render buffer.
  const int64_t first=int64_t(offset)+sx+int64_t(sy)*stride,last=int64_t(offset)+sx+sw-1+int64_t(sy+sh-1)*stride;
  require(first>=0&&last<int64_t(INT32_MAX),"Invalid adaptive sampling buffer offset");
  validate(buffer,size_t(last+1)*data.film.pass_stride*4);
  if(!film_adaptive_check.value){
    auto built=build_pipelines("film_adaptive",film_adaptive_wgsl(),{"adaptive_convergence_check","adaptive_filter_x","adaptive_filter_y"});
    film_adaptive_check=std::move(built[0]);film_adaptive_x=std::move(built[1]);film_adaptive_y=std::move(built[2]);
  }
  std::array<uint32_t,32> p{};p[1]=uint32_t(work);p[4]=uint32_t(buffer);p[5]=uint32_t(data.film.pass_stride);
  p[6]=uint32_t(sx);p[7]=uint32_t(sy);p[8]=uint32_t(sw);p[9]=uint32_t(sh);p[10]=uint32_t(offset);p[12]=uint32_t(stride);
  std::memcpy(&p[13],&threshold,4);p[14]=uint32_t(reset!=0);p[15]=uint32_t(active);p[16]=uint32_t(data.film.pass_adaptive_aux_buffer);
  p[17]=error_word;p[18]=uint32_t(data.film.pass_combined);p[19]=uint32_t(data.film.pass_sample_count);std::memcpy(&p[20],&data.film.exposure,4);
  if(check)dispatch(p,uint32_t(work),film_adaptive_check,workgroup("adaptive_convergence_check"));
  else if(kernel==DEVICE_KERNEL_ADAPTIVE_SAMPLING_CONVERGENCE_FILTER_X)dispatch(p,uint32_t(work),film_adaptive_x,workgroup("adaptive_filter_x"));
  else dispatch(p,uint32_t(work),film_adaptive_y,workgroup("adaptive_filter_y"));
}
}
