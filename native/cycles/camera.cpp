// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device.h"
#include "wavefront.h"
#include "kernel/tables.h"
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#if defined(__KERNEL_SSE__) && !defined(__KERNEL_NEON__)
#include <xmmintrin.h>
#endif
namespace ccl {
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static void require(bool ok,const std::string &message){if(!ok)throw std::runtime_error(message);}
#if defined(__KERNEL_SSE__) && !defined(__KERNEL_NEON__)
// Cycles' ray_triangle_reciprocal uses this host's RCPSS estimate. The WGSL
// models it as the 12-bit mantissa nearest to 2^25/(4097+2i), where i is the
// top 11 input mantissa bits, with the exponent 253-e. Check every mantissa
// class plus exponent and special cases on this host before claiming it.
static uint32_t host_rcpss(uint32_t bits){float x;std::memcpy(&x,&bits,4);float r=_mm_cvtss_f32(_mm_rcp_ss(_mm_set_ss(x)));uint32_t out;std::memcpy(&out,&r,4);return out;}
static uint32_t modelled_rcpss(uint32_t bits){
  const uint32_t sign=bits&0x80000000u,e=(bits>>23)&255u,m=bits&0x7fffffu;
  if(e==255u)return m?(bits|0x400000u):sign;
  if(e==0u)return sign|0x7f800000u;
  if(e>=253u)return sign;
  const uint32_t d=4097u+2u*(m>>12),q=((1u<<26)+d)/(2u*d);
  return sign|((253u-e)<<23)|((q-4096u)<<11);
}
static bool host_rcpss_modelled(){
  for(uint32_t i=0;i<2048;i++)for(uint32_t low:{0u,0xfffu,0x5a5u})for(uint32_t e:{1u,2u,100u,126u,127u,128u,200u,252u,253u,254u})
    for(uint32_t sign:{0u,0x80000000u}){const uint32_t bits=sign|(e<<23)|(i<<12)|low;if(host_rcpss(bits)!=modelled_rcpss(bits))return false;}
  for(uint32_t bits:{0u,0x80000000u,1u,0x7fffffu,0x7f800000u,0xff800000u,0x7fc00000u})if(host_rcpss(bits)!=modelled_rcpss(bits))return false;
  return true;
}
#endif
// Test hook (every build): CODEBRIX_CYCLES_WGSL_ARITHMETIC=arm64-scalar makes the device generate the
// scalar ARM64 oracle's adapters on any host, so the Android ARM64 product arithmetic can be rendered
// and bisected on the desktop GPU against the Linux ARM64 CPU oracle (cpu-arm64-linux). Unset, nothing
// changes.
static bool scalar_arithmetic_requested(){const char *set=std::getenv("CODEBRIX_CYCLES_WGSL_ARITHMETIC");return set&&std::strcmp(set,"arm64-scalar")==0;}
bool reference_oracle_sse(){
  if(scalar_arithmetic_requested())return false;
#ifdef __KERNEL_SSE__
  return true;
#elif defined(__ANDROID__)
  const char *oracle=std::getenv("CODEBRIX_CYCLES_REFERENCE_ORACLE");return oracle&&std::strcmp(oracle,"linux-x64-sse4.2")==0;
#else
  return false;
#endif
}
// The arithmetic the generated adapters reproduce, for reports and oracle films (compare.py picks the
// CPU gold standard of the same ISA family from it): "x64-sse4.2" (the Linux x64 SSE4.2 oracle; every
// x86-64 build, and an Android scalar build with CODEBRIX_CYCLES_REFERENCE_ORACLE=linux-x64-sse4.2),
// "arm64-scalar" (the scalar ARM64 oracle: the Android ARM64 product default and Linux ARM64) or
// "arm64-sse2neon" (an SSE2NEON build: SSE grouping, not oracle-exact).
std::string wgsl_arithmetic(){
  if(!reference_oracle_sse())return "arm64-scalar";
#if defined(__KERNEL_SSE__) && defined(__KERNEL_NEON__)
  return "arm64-sse2neon";
#else
  return "x64-sse4.2";
#endif
}
static constexpr char camera_source[]=
#include "camera_shader.inc"
;
// Offsets come from the compiled native ABI, not a second copy of Cycles structs.
std::string wavefront_layout(){
  std::ostringstream source;
  auto constant=[&](const char *name,size_t value){source<<"const "<<name<<":u32="<<value<<"u;\n";};
  const bool oracle_sse=reference_oracle_sse();
  constant("CAMERA_NATIVE_VECTOR_DIVIDE",oracle_sse?1:0);
  // Cycles normalize(float3) divides each component by the length with SSE,
  // while the scalar float3 operator/ (ARM64 without SSE2NEON) multiplies by
  // the reciprocal length. Encode the originating host's rule so ShaderData
  // normals replay that oracle's rounding on any GPU.
  constant("NATIVE_FLOAT3_RECIPROCAL_NORMALIZE",oracle_sse?0:1);
  // Cycles ray_triangle_intersect arithmetic of the originating oracle host:
  // 0 scalar build (dot (x+y)+z, 1/x), 1 SSE4.2 build with the modelled RCPSS
  // (dot x+(y+z), RCPSS + Newton step), 2 SSE build not modelled (SSE2NEON or
  // an x86 RCPSS that differs): SSE grouping with 1/x, not oracle-exact.
  // An Android scalar build judged against the Linux x64 reference emits 1.
  if(!oracle_sse&&scalar_arithmetic_requested())constant("NATIVE_TRIANGLE_ARITHMETIC",0);else
#if defined(__KERNEL_SSE__) && !defined(__KERNEL_NEON__)
  {static const bool rcpss_modelled=host_rcpss_modelled();
  constant("NATIVE_TRIANGLE_ARITHMETIC",rcpss_modelled?1:2);}
#elif defined(__KERNEL_SSE__)
  constant("NATIVE_TRIANGLE_ARITHMETIC",2);
#else
  // Android scalar build judged against the Linux x64 reference: that oracle's
  // RCPSS is the verified model (integer WGSL, any GPU), so mode 1.
  constant("NATIVE_TRIANGLE_ARITHMETIC",oracle_sse?1:0);
#endif
#define DATA(parent,member) constant("D_" #parent "_" #member,offsetof(KernelData,parent.member))
  DATA(cam,rastertocamera);DATA(cam,cameratoworld);DATA(cam,dx);DATA(cam,dy);DATA(cam,differential_scale);
  DATA(cam,nearclip);DATA(cam,cliplength);DATA(cam,width);DATA(cam,height);
  DATA(cam,shuttertime);DATA(cam,shutter_table_offset);DATA(cam,rolling_shutter_type);DATA(cam,rolling_shutter_duration);
  DATA(cam,num_motion_steps);DATA(cam,have_perspective_motion);DATA(cam,perspective_pre);DATA(cam,perspective_post);
  DATA(integrator,seed);DATA(integrator,tabulated_sobol_sequence_size);DATA(integrator,scrambling_distance);
  DATA(integrator,pixel_jitter);DATA(tables,filter_table_offset);
  DATA(film,pass_stride);DATA(film,pass_sample_count);DATA(film,pass_adaptive_aux_buffer);
  DATA(cam,is_inside_volume);
  DATA(background,volume_shader);DATA(background,object_index);
  DATA(film,pass_volume_scatter);DATA(film,pass_volume_transmit);
  DATA(film,pass_volume_majorant);DATA(film,pass_volume_majorant_sample_count);
  DATA(bvh,root);DATA(integrator,min_bounce);DATA(integrator,transparent_min_bounce);
  DATA(integrator,ao_bounces);DATA(integrator,ao_bounces_distance);DATA(integrator,filter_closures);
  constant("D_max_shaders",offsetof(KernelData,max_shaders));
  // Sobol-Burley and the blue-noise patterns (SAMPLING_SOBOL_BURLEY, wavefront_common.wgsl): the integrator's
  // pattern settings and sobol_burley_table (kernel/tables.h).
  source<<"//#if SAMPLING_SOBOL_BURLEY\n";
  constant("SB_D_sampling_pattern",offsetof(KernelData,integrator.sampling_pattern));
  constant("SB_D_sobol_index_mask",offsetof(KernelData,integrator.sobol_index_mask));
  constant("SB_D_blue_noise_sequence_length",offsetof(KernelData,integrator.blue_noise_sequence_length));
  constant("SB_TABULATED_SOBOL",SAMPLING_PATTERN_TABULATED_SOBOL);constant("SB_SOBOL_BURLEY",SAMPLING_PATTERN_SOBOL_BURLEY);
  constant("SB_BLUE_NOISE_PURE",SAMPLING_PATTERN_BLUE_NOISE_PURE);constant("SB_BLUE_NOISE_FIRST",SAMPLING_PATTERN_BLUE_NOISE_FIRST);
  source<<"const SB_TABLE=array<u32,128>(";
  for(int d=0;d<4;d++)for(int i=0;i<32;i++)source<<(d||i?",":"")<<sobol_burley_table[d][i]<<'u';
  source<<");\n//#endif\n";
  // Orthographic and panoramic cameras and depth of field (CAMERA_PROJECTIONS, wavefront_camera.wgsl).
  source<<"//#if CAMERA_PROJECTIONS\n";
#define CAM(member) constant("CP_D_" #member,offsetof(KernelData,cam.member))
  CAM(type);CAM(aperturesize);CAM(blades);CAM(bladesrotation);CAM(focaldistance);CAM(inv_aperture_ratio);CAM(panorama_type);
  CAM(fisheye_fov);CAM(fisheye_lens);CAM(fisheye_lens_polynomial_bias);CAM(equirectangular_range);
  CAM(fisheye_lens_polynomial_coefficients);CAM(central_cylindrical_range);CAM(sensorwidth);CAM(sensorheight);
#undef CAM
  constant("CP_CAMERA_PERSPECTIVE",CAMERA_PERSPECTIVE);constant("CP_CAMERA_ORTHOGRAPHIC",CAMERA_ORTHOGRAPHIC);
  constant("CP_PANORAMA_EQUIRECTANGULAR",PANORAMA_EQUIRECTANGULAR);constant("CP_PANORAMA_FISHEYE_EQUIDISTANT",PANORAMA_FISHEYE_EQUIDISTANT);
  constant("CP_PANORAMA_FISHEYE_EQUISOLID",PANORAMA_FISHEYE_EQUISOLID);constant("CP_PANORAMA_MIRRORBALL",PANORAMA_MIRRORBALL);
  constant("CP_PANORAMA_FISHEYE_LENS_POLYNOMIAL",PANORAMA_FISHEYE_LENS_POLYNOMIAL);constant("CP_PANORAMA_EQUIANGULAR_CUBEMAP_FACE",PANORAMA_EQUIANGULAR_CUBEMAP_FACE);
  constant("CP_PANORAMA_CENTRAL_CYLINDRICAL",PANORAMA_CENTRAL_CYLINDRICAL);
  source<<"//#endif\n";
  // Spherical stereo (CAMERA_STEREO, wavefront_camera.wgsl cp_stereo): kernel/camera/projection.h spherical_stereo_transform.
  source<<"//#if CAMERA_STEREO\n";
  constant("CS_D_interocular_offset",offsetof(KernelData,cam.interocular_offset));constant("CS_D_convergence_distance",offsetof(KernelData,cam.convergence_distance));
  constant("CS_D_pole_merge_angle_from",offsetof(KernelData,cam.pole_merge_angle_from));constant("CS_D_pole_merge_angle_to",offsetof(KernelData,cam.pole_merge_angle_to));
  source<<"//#endif\n";
#undef DATA
#define KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) constant("S_" #parent "_" #name,offsetof(IntegratorStateGPU,parent.name));
#define KERNEL_STRUCT_MEMBER_PACKED KERNEL_STRUCT_MEMBER
#define KERNEL_STRUCT_ARRAY_MEMBER(parent,type,name,feature)
#define KERNEL_STRUCT_END(name)
#define KERNEL_STRUCT_END_ARRAY(name,cpu_size,gpu_size)
#include "kernel/integrator/state_template.h"
#undef KERNEL_STRUCT_BEGIN
#undef KERNEL_STRUCT_BEGIN_PACKED
#undef KERNEL_STRUCT_MEMBER
#undef KERNEL_STRUCT_MEMBER_PACKED
#undef KERNEL_STRUCT_ARRAY_MEMBER
#undef KERNEL_STRUCT_END
#undef KERNEL_STRUCT_END_ARRAY
  // The shadow path's bounce counters for the Light Path node's depths in shadow evaluations (LIGHT_PATH_STATE,
  // wavefront_svm_nodes.wgsl svm_light_path): their own names, since the shadow module also defines the S_shadow_path_* set.
  source<<"//#if LIGHT_PATH_STATE\n";
  constant("LP_shadow_bounce",offsetof(IntegratorStateGPU,shadow_path.bounce));constant("LP_shadow_diffuse_bounce",offsetof(IntegratorStateGPU,shadow_path.diffuse_bounce));
  constant("LP_shadow_glossy_bounce",offsetof(IntegratorStateGPU,shadow_path.glossy_bounce));constant("LP_shadow_transparent_bounce",offsetof(IntegratorStateGPU,shadow_path.transparent_bounce));
  constant("LP_shadow_transmission_bounce",offsetof(IntegratorStateGPU,shadow_path.transmission_bounce));constant("LP_shadow_portal_bounce",offsetof(IntegratorStateGPU,shadow_path.portal_bounce));
  source<<"//#endif\n";
  // Shadow linking (wavefront_shadow_link_*.wgsl, the traversal's blocker filter): one region, so other modules are unchanged.
  source<<"//#if SHADOW_LINKING\n";
  constant("DL_K_INTERSECT",DEVICE_KERNEL_INTEGRATOR_INTERSECT_DEDICATED_LIGHT);constant("DL_K_SHADE",DEVICE_KERNEL_INTEGRATOR_SHADE_DEDICATED_LIGHT);
  constant("DL_O_shadow_set_membership",offsetof(KernelObject,shadow_set_membership));constant("DL_O_blocker_shadow_set",offsetof(KernelObject,blocker_shadow_set));
  constant("DL_SD_HAS_ONLY_VOLUME",SD_HAS_ONLY_VOLUME);constant("DL_VOLUME_BOUNDS_MAX",VOLUME_BOUNDS_MAX);constant("DL_MAX_INTERSECTIONS",1024);
  constant("DL_PATH_RAY_SHADOW_FOR_LIGHT_LINKING",PATH_RAY_SHADOW_FOR_LIGHT_LINKING);constant("DL_RECORD_BYTES",32);constant("DL_D_transparent_max_bounce",offsetof(KernelData,integrator.transparent_max_bounce));
  constant("DL_FEATURE_SHADOW_LINKING",KERNEL_FEATURE_SHADOW_LINKING);
  source<<"//#endif\n";
  constant("VS_HAS_VOLUME",SD_HAS_VOLUME);constant("VS_OBJECT_HAS_VOLUME",SD_OBJECT_HAS_VOLUME);
  constant("VS_MAX",MAX_VOLUME_STACK_SIZE);constant("VS_MAX_HITS",2*MAX_VOLUME_STACK_SIZE);constant("VS_BOUNDS_MAX",VOLUME_BOUNDS_MAX);
  constant("S_volume_stack",offsetof(IntegratorStateGPU,volume_stack));
  constant("VS_stride",sizeof(IntegratorStateGPU{}.volume_stack[0]));
  constant("VS_object",offsetof(std::remove_reference_t<decltype(IntegratorStateGPU{}.volume_stack[0])>,object));
  constant("VS_shader",offsetof(std::remove_reference_t<decltype(IntegratorStateGPU{}.volume_stack[0])>,shader));
  constant("D_volume_stack_size",offsetof(KernelData,volume_stack_size));
  constant("D_kernel_features",offsetof(KernelData,kernel_features));
  constant("S_queue_counter",offsetof(IntegratorStateGPU,queue_counter));
  constant("S_sort_partition_divisor",offsetof(IntegratorStateGPU,sort_partition_divisor));
  constant("S_sort_key_counter",offsetof(IntegratorStateGPU,sort_key_counter));
  constant("POINTER_SIZE",sizeof(void*));
  unsigned table_index=0;
#define TABLE(name,type) constant("G_" #name,table_index++);constant("SIZE_" #name,sizeof(type));
  CBC_SCENE_CORE_TABLES(TABLE) CBC_DEVICE_TABLES(TABLE) CBC_LIGHT_TREE_TABLES(TABLE)
#undef TABLE
  // The IES table follows the others (closest.cpp scene_tables); its index only where the IES node is reachable.
  source<<"//#if SVM_NODE_IES\n";constant("G_ies",table_index);source<<"//#endif\n";
  // The majorant octree of heterogeneous volumes follows the IES table (closest.cpp scene_tables).
  source<<"//#if VOLUME_HETEROGENEOUS\n";
  constant("G_volume_tree_nodes",table_index+1);constant("SIZE_volume_tree_nodes",sizeof(KernelOctreeNode));
  constant("G_volume_tree_roots",table_index+2);constant("SIZE_volume_tree_roots",sizeof(KernelOctreeRoot));
  constant("G_volume_tree_root_ids",table_index+3);constant("VH_ATTR_GENERATED_TRANSFORM",ATTR_STD_GENERATED_TRANSFORM);
  constant("VH_TRANSFORM_APPLIED",SD_OBJECT_TRANSFORM_APPLIED);constant("VH_HETEROGENEOUS",SD_HETEROGENEOUS_VOLUME);
  source<<"//#endif\n";
  // The Light Path extrema estimate (VOLUME_LIGHT_PATH, wavefront_volume_octree.wgsl).
  source<<"//#if VOLUME_LIGHT_PATH\n";constant("VLP_LIGHT_PATH",SD_HAS_LIGHT_PATH_NODE);constant("VLP_VISIBILITY_CAMERA",PATH_RAY_VISIBILITY_CAMERA);source<<"//#endif\n";
  // Biased ray marching: the per-object step sizes follow the octree tables (closest.cpp scene_tables).
  source<<"//#if VOLUME_RAY_MARCHING\n";
  constant("G_volume_step_size",table_index+4);constant("VR_D_volume_max_steps",offsetof(KernelData,integrator.volume_max_steps));
  constant("VR_D_volume_ray_marching",offsetof(KernelData,integrator.volume_ray_marching));
  constant("VR_PRNG_COLOR_CHANNEL",PRNG_VOLUME_COLOR_CHANNEL);constant("VR_PRNG_OFFSET",PRNG_VOLUME_OFFSET);constant("VR_PRNG_SHADE_OFFSET",PRNG_VOLUME_SHADE_OFFSET);
  // The volume kernel the path queues (a run-time choice: modules compiled with every region keep both).
  source<<"fn vr_volume_kernel()->u32{return select("<<uint32_t(DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME)<<"u,"<<uint32_t(DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME_RAY_MARCHING)
        <<"u,load_word(p(6u)+VR_D_volume_ray_marching)!=0u);}\n";
  source<<"//#endif\n";
  // Curves: their tables follow the step sizes (closest.cpp scene_tables), present only in scenes with curves.
  source<<"//#if CURVES\n";
  constant("G_curves",table_index+5);constant("G_curve_keys",table_index+6);constant("G_prim_type",table_index+7);
  constant("D_bvh_curve_subdivisions",offsetof(KernelData,bvh.curve_subdivisions));
  constant("PRIMITIVE_CURVE",PRIMITIVE_CURVE);constant("PRIMITIVE_CURVE_RIBBON",PRIMITIVE_CURVE_RIBBON);constant("PRIMITIVE_CURVE_THICK",PRIMITIVE_CURVE_THICK);
  constant("PRIMITIVE_CURVE_THICK_LINEAR",PRIMITIVE_CURVE_THICK_LINEAR);constant("PRIMITIVE_MOTION",PRIMITIVE_MOTION);constant("PRIMITIVE_NUM_BITS",PRIMITIVE_NUM_BITS);
  constant("PATH_RAY_VISIBILITY_NODE_UNALIGNED",PATH_RAY_VISIBILITY_NODE_UNALIGNED);
  source<<"//#endif\n";
  source<<"//#if CURVE_SHADOW\n";
  constant("CV_ATTR_STD_SHADOW_TRANSPARENCY",ATTR_STD_SHADOW_TRANSPARENCY);
  constant("CV_EVAL_INPUT_BYTES",sizeof(KernelShaderEvalInput));constant("CV_EVAL_INPUT_object",offsetof(KernelShaderEvalInput,object));constant("CV_EVAL_INPUT_prim",offsetof(KernelShaderEvalInput,prim));
  constant("CV_EVAL_INPUT_u",offsetof(KernelShaderEvalInput,u));constant("CV_EVAL_INPUT_v",offsetof(KernelShaderEvalInput,v));
  source<<"//#endif\n";
  // Points: their tables follow the curve tables, particles theirs (closest.cpp scene_tables).
#define TABLE(name,type) +1
  const unsigned point_table_index=table_index+5 CBC_CURVE_TABLES(TABLE);
#undef TABLE
  source<<"//#if POINTS\n";
  constant("G_points",point_table_index);constant("G_points_shader",point_table_index+1);constant("PRIMITIVE_POINT",PRIMITIVE_POINT);constant("PT_PRIMITIVE_MOTION",PRIMITIVE_MOTION);
  source<<"//#endif\n//#if SVM_NODE_PARTICLE_INFO\n";
  constant("G_particles",point_table_index+2);constant("SIZE_particles",sizeof(KernelParticle));
  source<<"//#endif\n";
  constant("O_tfm",offsetof(KernelObject,tfm));constant("O_motion_offset",offsetof(KernelObject,motion_offset));
  constant("O_num_tfm_steps",offsetof(KernelObject,num_tfm_steps));
  constant("O_num_geom_steps",offsetof(KernelObject,num_geom_steps));constant("O_numverts",offsetof(KernelObject,numverts));constant("O_numprims",offsetof(KernelObject,mesh_volume.num_prims));constant("MOTION_SIZE",sizeof(DecomposedTransform));
  constant("O_itfm",offsetof(KernelObject,itfm));constant("O_position_offset",offsetof(KernelObject,position_offset));
  constant("O_ao_distance",offsetof(KernelObject,ao_distance));constant("SH_flags",offsetof(KernelShader,flags));
#define ENUM(name) constant(#name,uint32_t(name))
  ENUM(KERNEL_FEATURE_VOLUME);ENUM(PATH_RAY_VOLUME_PASS);ENUM(PATH_RAY_VOLUME_PRIMARY_TRANSMIT);
  ENUM(PATH_RAY_TERMINATE_IN_NEXT_VOLUME);ENUM(PATH_RAY_TERMINATE_AFTER_VOLUME);
  ENUM(DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME);ENUM(DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK);
  ENUM(SD_OBJECT_MOTION);ENUM(SD_OBJECT_HAS_VERTEX_MOTION);
  ENUM(PRIMITIVE_TRIANGLE);ENUM(PRIMITIVE_MOTION_TRIANGLE);ENUM(PRIMITIVE_ALL);ENUM(PRIMITIVE_LAMP);ENUM(SHADER_MASK);
  ENUM(DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD);
  ENUM(SD_HAS_EMISSION);ENUM(SD_HAS_TRANSPARENT_SHADOW);ENUM(SD_HAS_RAYTRACE);
  ENUM(PATH_RAY_TRANSPARENT);ENUM(PATH_RAY_SHADOW_CATCHER_PASS);
  ENUM(PATH_RAY_TERMINATE_AFTER_TRANSPARENT);ENUM(PATH_RAY_TERMINATE_ON_NEXT_SURFACE);
  ENUM(PATH_RAY_VISIBILITY_TRANSMIT);ENUM(PATH_RAY_VISIBILITY_DIFFUSE);ENUM(PATH_RAY_VISIBILITY_GLOSSY);
  ENUM(PATH_RAY_VISIBILITY_SHADOW_OPAQUE);
  ENUM(FILTER_CLOSURE_DIRECT_LIGHT);ENUM(PRNG_TERMINATE);
  ENUM(DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND);ENUM(DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE);
  ENUM(DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE);
#undef ENUM
#define TILE(member) constant("T_" #member,offsetof(KernelWorkTile,member))
  TILE(x);TILE(y);TILE(w);TILE(h);TILE(start_sample);TILE(num_samples);TILE(sample_offset);
  TILE(offset);TILE(stride);TILE(path_index_offset);TILE(work_size);
#undef TILE
  constant("T_size",sizeof(KernelWorkTile));constant("FILTER_SIZE",FILTER_TABLE_SIZE);
  constant("SHUTTER_SIZE",SHUTTER_TABLE_SIZE);constant("RNG_LENS_TIME",PRNG_LENS_TIME);
  constant("MOTION_X",offsetof(DecomposedTransform,x));constant("MOTION_Y",offsetof(DecomposedTransform,y));
  constant("MOTION_Z",offsetof(DecomposedTransform,z));constant("MOTION_W",offsetof(DecomposedTransform,w));
  constant("SOBOL_PATTERNS",NUM_TAB_SOBOL_PATTERNS);constant("RNG_FILTER",PRNG_FILTER);
  constant("RNG_BOUNCE",PRNG_BOUNCE_NUM);constant("VIS_CAMERA",PATH_RAY_VISIBILITY_CAMERA);constant("VIS_VOLUME",PATH_RAY_VISIBILITY_VOLUME_SCATTER);
  constant("INITIAL_FLAG",PATH_RAY_MIS_SKIP|PATH_RAY_TRANSPARENT_BACKGROUND);
  constant("K_CAMERA",DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA);
  constant("K_CLOSEST",DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST);
  constant("NONE",uint32_t(OBJECT_NONE));constant("PRIM_TYPE_NONE",PRIMITIVE_NONE);
  constant("PASS_NONE",uint32_t(PASS_UNUSED));constant("KF_LIGHT_LINKING",KERNEL_FEATURE_LIGHT_LINKING);
  // Denoising features (FILM_DENOISING; film_passes.cpp, wavefront_film_passes.wgsl).
  source<<"//#if FILM_DENOISING\n";
  constant("DN_KERNEL_FEATURE_DENOISING",KERNEL_FEATURE_DENOISING);constant("DN_PATH_RAY_DENOISING_FEATURES",PATH_RAY_DENOISING_FEATURES);
  constant("DN_D_pass_denoising_albedo",offsetof(KernelData,film.pass_denoising_albedo));constant("DN_D_pass_denoising_normal",offsetof(KernelData,film.pass_denoising_normal));
  constant("DN_D_pass_denoising_specular_albedo",offsetof(KernelData,film.pass_denoising_specular_albedo));
  constant("DN_D_pass_denoising_roughness",offsetof(KernelData,film.pass_denoising_roughness));constant("DN_D_pass_denoising_depth",offsetof(KernelData,film.pass_denoising_depth));
  constant("DN_D_denoising_pass_options_flag",offsetof(KernelData,film.denoising_pass_options_flag));
  constant("DN_FOLLOW_REFLECTIONS",DENOISING_PASS_FOLLOW_REFLECTIONS);constant("DN_USE_ALBEDO_ROUGHNESS_WEIGHTING",DENOISING_PASS_USE_ALBEDO_ROUGHNESS_WEIGHTING);
  constant("DN_PATH_RAY_REFLECT",PATH_RAY_REFLECT);
  source<<"//#endif\n";
  return source.str();
}
std::string camera_wgsl(){return wavefront_layout()+camera_source;}
// The sampling pattern (kernel/sample/pattern.h): tabulated Sobol reads sample_pattern_lut (its
// address, push word 9); Sobol-Burley and the blue-noise patterns compute their samples
// (SAMPLING_SOBOL_BURLEY regions) and need no table (0). Integrator::device_update turns
// blue_noise_round into blue_noise_pure.
uint32_t WebGPUDevice::sample_pattern_table(const char *stage){
  const int pattern=data.integrator.sampling_pattern;
  if(pattern!=SAMPLING_PATTERN_TABULATED_SOBOL){
    require(pattern==SAMPLING_PATTERN_SOBOL_BURLEY||pattern==SAMPLING_PATTERN_BLUE_NOISE_PURE||pattern==SAMPLING_PATTERN_BLUE_NOISE_FIRST,
            std::string("Invalid ")+stage+" sampling pattern");
    return 0;
  }
  const auto sequence=data.integrator.tabulated_sobol_sequence_size;
  require(sequence>1&&(sequence&(sequence-1))==0,std::string("Invalid ")+stage+" Sobol sequence size");
  const uint32_t table=globals.at("sample_pattern_lut");
  validate(table,size_t(sequence)*NUM_TAB_SOBOL_PATTERNS*NUM_TAB_SOBOL_DIMENSIONS*4);
  return table;
}
void WebGPUDevice::enqueue_camera(int work,const DeviceKernelArguments &args){
  require(args.count==6,"Camera argument count");
  auto scalar=[&](unsigned i){require(args.types[i]==DeviceKernelArguments::INT32&&args.sizes[i]==4,"Invalid camera integer argument");
    int32_t n;std::memcpy(&n,args.values[i],4);require(n>=0,"Negative camera argument");return uint32_t(n);};
  auto pointer=[&](unsigned i,bool nullable){require(args.types[i]==DeviceKernelArguments::POINTER&&args.sizes[i]==sizeof(device_ptr),"Invalid camera pointer argument");
    device_ptr address;std::memcpy(&address,args.values[i],sizeof(address));if(address||!nullable)validate(address,4);
    require(address%4==0,"Unaligned camera pointer");return uint32_t(address);};
  std::array<uint32_t,32> p{};
  p[1]=uint32_t(work);p[2]=pointer(0,true);p[3]=scalar(1);p[4]=pointer(2,false);p[5]=scalar(3);
  p[12]=pointer(4,true);p[13]=scalar(5);p[17]=error_word;
  if(p[2]){require(p[3]&&p[5]&&uint64_t(p[3])*p[5]<=uint32_t(work),"Incomplete camera tile dispatch");validate(p[2],size_t(p[3])*sizeof(KernelWorkTile));}
  else{require(p[13]<=uint32_t(work),"Incomplete camera retry dispatch");if(p[12])validate(p[12],size_t(p[13])*4);}
  // Perspective, orthographic and panoramic cameras with depth of field (CAMERA_PROJECTIONS for every case
  // but the pinhole perspective camera); custom (OSL) cameras are outside the package. Spherical stereo (a left or
  // right eye: a non-zero interocular offset) with the perspective and panoramic cameras is CAMERA_STEREO; the
  // orthographic camera ignores the offset, as camera_sample_orthographic does.
  require(data.cam.type==CAMERA_PERSPECTIVE||data.cam.type==CAMERA_ORTHOGRAPHIC||data.cam.type==CAMERA_PANORAMA,
          "WebGPU custom camera (OSL) not implemented");
  require(std::isfinite(data.cam.interocular_offset)&&std::isfinite(data.cam.convergence_distance)&&
          std::isfinite(data.cam.pole_merge_angle_from)&&std::isfinite(data.cam.pole_merge_angle_to),"Invalid stereo camera parameters");
  require(std::isfinite(data.cam.aperturesize)&&data.cam.aperturesize>=0,"Invalid camera aperture size");
  if(data.cam.aperturesize>0){
    require(std::isfinite(data.cam.focaldistance)&&std::isfinite(data.cam.inv_aperture_ratio)&&
            (data.cam.blades==0||(data.cam.blades>=3&&data.cam.blades<=1e6f))&&std::fabs(data.cam.bladesrotation)<=8,"Invalid camera depth of field");
  }
  require(data.cam.type==CAMERA_PERSPECTIVE||!data.cam.have_perspective_motion,"Invalid camera perspective motion");
  if(data.cam.type==CAMERA_PANORAMA){
    require(data.cam.panorama_type>=0&&data.cam.panorama_type<PANORAMA_NUM_TYPES,"Invalid panorama camera type");
    // Angles beyond the sine/cosine adapters' range (equirectangular and central cylindrical ranges).
    const float4 e=data.cam.equirectangular_range,c=data.cam.central_cylindrical_range;
    for(float v:{e.x,e.y,e.z,e.w,c.x,c.y,c.z,c.w,data.cam.fisheye_fov,data.cam.fisheye_lens,data.cam.sensorwidth,data.cam.sensorheight,
                 data.cam.fisheye_lens_polynomial_bias})require(std::isfinite(v)&&std::fabs(v)<=64,"Invalid panorama camera parameters");
  }
  require(data.cam.num_motion_steps==0||data.cam.num_motion_steps>=2,"Invalid camera motion step count");
  if(data.cam.num_motion_steps){
    const auto motion=globals.find("camera_motion");
    require(motion!=globals.end()&&remaining(motion->second)==size_t(data.cam.num_motion_steps)*sizeof(DecomposedTransform),
            "Invalid native camera motion table");
  }
  require(std::isfinite(data.cam.shuttertime)&&(data.cam.shuttertime==-1||data.cam.shuttertime>=0),"Invalid camera shutter time");
  require(std::isfinite(data.cam.height)&&data.cam.height>0,"Invalid camera height");
  require((data.cam.rolling_shutter_type==0||data.cam.rolling_shutter_type==1)&&
          std::isfinite(data.cam.rolling_shutter_duration)&&data.cam.rolling_shutter_duration>=0&&data.cam.rolling_shutter_duration<=1,
          "Invalid camera rolling shutter");
  validate_volume();
  require(!(data.kernel_features&KERNEL_FEATURE_PATH_GUIDING),"WebGPU camera optional path features not implemented");
  // The Ray Portal BSDF (RAY_PORTAL): path_state_init_integrator clears the portal bounce.
  if(data.kernel_features&KERNEL_FEATURE_NODE_PORTAL)validate(uintptr_t(state.path.portal_bounce),remaining(uintptr_t(state.path.render_pixel_index))/4*sizeof(uint16_t));
  // Denoising features (path_state_init_integrator): the flag and the feature throughput.
  validate_film_passes();
  if(data.kernel_features&KERNEL_FEATURE_DENOISING)validate(uintptr_t(state.path.denoising_feature_throughput),
    remaining(uintptr_t(state.path.render_pixel_index))/4*sizeof(PackedSpectrum));
  if(data.kernel_features&KERNEL_FEATURE_LIGHT_LINKING)require(state.path.mis_ray_object,"Missing light linking path state");
  p[6]=constants.at("data");p[7]=constants.at("integrator_state");
  p[8]=globals.at("lookup_table");p[9]=sample_pattern_table("camera");
  require(data.tables.filter_table_offset>=0,"Invalid camera filter offset");
  validate(p[8],(size_t(data.tables.filter_table_offset)+FILTER_TABLE_SIZE)*4);
  if(data.cam.shuttertime!=-1){
    require(data.cam.shutter_table_offset>=0,"Invalid camera shutter table offset");
    validate(p[8],(size_t(data.cam.shutter_table_offset)+SHUTTER_TABLE_SIZE)*4);
  }
  p[14]=scene_tables();
  require(data.film.pass_stride>0,"Invalid camera film stride");
  for(auto [offset,length]:{std::pair{data.film.pass_sample_count,1},std::pair{data.film.pass_adaptive_aux_buffer,4}})
    require(offset==PASS_UNUSED||(offset>=0&&offset<=data.film.pass_stride-length),"Invalid camera film pass offset");
  p[10]=uint32_t(remaining(uintptr_t(state.path.render_pixel_index))/4);p[11]=uint32_t(remaining(p[4])/4);
  // All mandatory fields must cover the same capacity before any shader writes.
#define KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) if(feature==KERNEL_FEATURE_PATH_TRACING)validate(uintptr_t(state.parent.name),size_t(p[10])*sizeof(gpu_state_storage<type>::gpu_type));
#define KERNEL_STRUCT_MEMBER_PACKED KERNEL_STRUCT_MEMBER
#define KERNEL_STRUCT_ARRAY_MEMBER(parent,type,name,feature)
#define KERNEL_STRUCT_END(name)
#define KERNEL_STRUCT_END_ARRAY(name,cpu_size,gpu_size)
#include "kernel/integrator/state_template.h"
#undef KERNEL_STRUCT_BEGIN
#undef KERNEL_STRUCT_BEGIN_PACKED
#undef KERNEL_STRUCT_MEMBER
#undef KERNEL_STRUCT_MEMBER_PACKED
#undef KERNEL_STRUCT_ARRAY_MEMBER
#undef KERNEL_STRUCT_END
#undef KERNEL_STRUCT_END_ARRAY
  validate(uintptr_t(state.queue_counter),sizeof(IntegratorQueueCounter));
  if(!work)return;
  if(!pipeline_current(camera,"init_camera"))camera=std::move(build_pipelines("camera",camera_wgsl(),{"init_camera"})[0]);
  dispatch(p,uint32_t(work),camera,workgroup("init_camera"));
}
}
