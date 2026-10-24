// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device.h"
#include "wavefront.h"
#include <cmath>
#include <sstream>
#include <stdexcept>
namespace ccl {
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
std::string lighting_layout(){
  std::ostringstream out;auto constant=[&](const char *name,size_t value){out<<"const "<<name<<":u32="<<value<<"u;\n";};
  constant("TRIANGLE_LIGHT_NATIVE_SSE",reference_oracle_sse()?1:0);
#define DATA(member) constant("D_integrator_" #member,offsetof(KernelData,integrator.member))
  DATA(use_direct_light);DATA(use_light_mis);DATA(num_lights);DATA(num_distribution);DATA(distribution_pdf_lights);DATA(distribution_pdf_triangles);DATA(light_inv_rr_threshold);
#undef DATA
#define LIGHT(member) constant("L_" #member,offsetof(KernelLight,member))
  LIGHT(type);LIGHT(co);LIGHT(shader_id);LIGHT(object_id);LIGHT(max_bounces);LIGHT(strength);
#undef LIGHT
  constant("L_radius",offsetof(KernelLight,spot.radius));constant("L_point_eval",offsetof(KernelLight,spot.eval_fac));
  constant("L_is_sphere",offsetof(KernelLight,spot.is_sphere));constant("L_sun_angle",offsetof(KernelLight,sun.one_minus_cosangle));
  constant("L_spot_dir",offsetof(KernelLight,spot.dir));constant("L_spot_cos",offsetof(KernelLight,spot.cos_half_spot_angle));
  constant("L_spot_smooth",offsetof(KernelLight,spot.spot_smooth));constant("L_spot_spread_cos",offsetof(KernelLight,spot.cos_half_larger_spread));
#define AREA(member) constant("L_area_" #member,offsetof(KernelLight,area.member))
  AREA(axis_u);AREA(axis_v);AREA(len_u);AREA(len_v);AREA(dir);AREA(invarea);AREA(tan_half_spread);AREA(normalize_spread);
#undef AREA
  constant("L_sun_eval",offsetof(KernelLight,sun.eval_fac));
  constant("L_sun_half_angle",offsetof(KernelLight,sun.angle));constant("L_sun_pdf",offsetof(KernelLight,sun.pdf));
  constant("LD_totarea",offsetof(KernelLightDistribution,totarea));constant("LD_prim",offsetof(KernelLightDistribution,prim));
  constant("LD_object",offsetof(KernelLightDistribution,object_id));constant("LD_visibility",offsetof(KernelLightDistribution,visibility_flag));
  constant("O_shadow_terminator_geometry_offset",offsetof(KernelObject,shadow_terminator_geometry_offset));
#define ENUM(name) constant(#name,uint32_t(name))
  ENUM(LIGHT_POINT);ENUM(LIGHT_SUN);ENUM(LIGHT_SPOT);ENUM(LIGHT_AREA);ENUM(SHADER_CAST_SHADOW);ENUM(SHADER_USE_MIS);ENUM(PRNG_LIGHT);ENUM(PRNG_LIGHT_TERMINATE);
#undef ENUM
  constant("K_LIGHT_NEE",DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_NEE);constant("O_primitive_type",offsetof(KernelObject,primitive_type));
  constant("LIGHT_EXCLUDE_CAMERA",SHADER_EXCLUDE_CAMERA);constant("LIGHT_EXCLUDE_SHADOW_CATCHER",SHADER_EXCLUDE_SHADOW_CATCHER);
  // Light tree and light linking (wavefront_light_tree.wgsl, wavefront_light_link.wgsl).
  constant("LT_D_use_light_tree",offsetof(KernelData,integrator.use_light_tree));constant("LT_D_link_sets",offsetof(KernelData,light_link_sets));
  constant("LT_D_background_object",offsetof(KernelData,background.object_index));
  constant("LT_LINK_SET_STRIDE",sizeof(KernelLightLinkSet));constant("LT_LINK_SET_MAX",LIGHT_LINK_SET_MAX);
  constant("LT_FEATURE_LIGHT_TREE",KERNEL_FEATURE_LIGHT_TREE);constant("LT_FEATURE_LIGHT_LINKING",KERNEL_FEATURE_LIGHT_LINKING);
  constant("LT_O_receiver_light_set",offsetof(KernelObject,receiver_light_set));constant("LT_O_light_set_membership",offsetof(KernelObject,light_set_membership));
  constant("LT_O_tfm",offsetof(KernelObject,tfm));constant("LT_O_itfm",offsetof(KernelObject,itfm));
#define NODE(name,member) constant("LTN_" #name,offsetof(KernelLightTreeNode,member))
  NODE(bbox_min,bbox.min);NODE(bbox_max,bbox.max);NODE(axis,bcone.axis);NODE(theta_o,bcone.theta_o);NODE(theta_e,bcone.theta_e);
  NODE(energy,energy);NODE(type,type);NODE(num_emitters,num_emitters);NODE(first_emitter,leaf.first_emitter);
  NODE(left_child,inner.left_child);NODE(right_child,inner.right_child);NODE(reference,instance.reference);NODE(bit_trail,bit_trail);NODE(bit_skip,bit_skip);
#undef NODE
#define EMITTER(name,member) constant("LTE_" #name,offsetof(KernelLightTreeEmitter,member))
  EMITTER(theta_o,theta_o);EMITTER(theta_e,theta_e);EMITTER(energy,energy);EMITTER(id,light.id);EMITTER(emission_sampling,triangle.emission_sampling);
  EMITTER(mesh_object,mesh.object_id);EMITTER(mesh_node,mesh.node_id);EMITTER(object,object_id);EMITTER(visibility,visibility_flag);EMITTER(bit_trail,bit_trail);
#undef EMITTER
  static_assert(offsetof(KernelLightTreeEmitter,light.id)==offsetof(KernelLightTreeEmitter,triangle.id)&&offsetof(KernelLightTreeEmitter,light.id)==offsetof(KernelLightTreeEmitter,mesh.object_id));
  static_assert(sizeof(LightTreeNodeType)==1&&sizeof(KernelLightTreeNode{}.bit_skip)==1&&sizeof(EmissionSampling)==4);
#define ENUM(name) constant("LT_" #name,uint32_t(name))
  ENUM(LIGHT_TREE_INSTANCE);ENUM(LIGHT_TREE_INNER);ENUM(LIGHT_TREE_LEAF);ENUM(LIGHT_TREE_DISTANT);ENUM(LIGHT_BACKGROUND);ENUM(LIGHT_POINT);ENUM(LIGHT_SPOT);ENUM(LIGHT_AREA);ENUM(LIGHT_SUN);
  ENUM(EMISSION_SAMPLING_FRONT);ENUM(EMISSION_SAMPLING_BACK);ENUM(SD_OBJECT_TRANSFORM_APPLIED);ENUM(SD_OBJECT_NEGATIVE_SCALE);ENUM(SD_OBJECT_MOTION);
  ENUM(PATH_RAY_MIS_HAD_TRANSMISSION);ENUM(PATH_RAY_VISIBILITY_CAMERA);ENUM(PATH_RAY_VISIBILITY_VOLUME_SCATTER);ENUM(SR_BSDF_HAS_TRANSMISSION);
#undef ENUM
  return out.str();
}
// Light tree tables uploaded by the unmodified LightManager::device_update_tree:
// counts, node kinds, child/emitter/instance indices and the per-object lookups
// are checked here, so the WGSL traversal reads only in-range records.
void WebGPUDevice::validate_light_tree() const{
  auto count=[&](const char *name,size_t size)->size_t{auto it=globals.find(name);if(it==globals.end())return 0;
    require(remaining(it->second)%size==0,"Invalid native light tree table size");return remaining(it->second)/size;};
  const size_t nodes=count("light_tree_nodes",sizeof(KernelLightTreeNode)),emitters=count("light_tree_emitters",sizeof(KernelLightTreeEmitter));
  const size_t to_tree=count("light_to_tree",4),lookup=count("object_lookup_offset",4),triangles=count("triangle_to_tree",4),prim_offsets=count("object_prim_offset",4);
  require(light_tree_nodes.size()==nodes&&light_tree_emitters.size()==emitters,"Invalid native light tree host copy");
  if(!data.integrator.use_direct_light)return;
  require(nodes>0&&emitters>0,"Missing native light tree");
  require(to_tree>=object_records.size()&&lookup>=object_records.size()&&prim_offsets>=object_records.size(),"Invalid native light tree object lookup");
  for(const KernelLightTreeNode &node:light_tree_nodes){
    const unsigned type=node.type;
    require(type==LIGHT_TREE_INSTANCE||type==LIGHT_TREE_INNER||type==LIGHT_TREE_LEAF||type==LIGHT_TREE_DISTANT,"Invalid native light tree node type");
    for(float value:{node.bbox.min.x,node.bbox.min.y,node.bbox.min.z,node.bbox.max.x,node.bbox.max.y,node.bbox.max.z,node.bcone.axis.x,node.bcone.axis.y,node.bcone.axis.z,
        node.bcone.theta_o,node.bcone.theta_e,node.energy})require(!std::isnan(value),"Invalid native light tree node bounds");
    if(type==LIGHT_TREE_INNER)require(node.inner.left_child>=0&&size_t(node.inner.left_child)<nodes&&node.inner.right_child>=0&&size_t(node.inner.right_child)<nodes,"Invalid native light tree child");
    else if(type==LIGHT_TREE_INSTANCE)require(node.instance.reference>=0&&size_t(node.instance.reference)<nodes&&light_tree_nodes[node.instance.reference].type!=LIGHT_TREE_INSTANCE,"Invalid native light tree instance");
    else require(node.num_emitters>=0&&node.num_emitters<=32&&node.leaf.first_emitter>=0&&size_t(node.leaf.first_emitter)+size_t(node.num_emitters)<=emitters,"Invalid native light tree leaf");
  }
  for(int set=0;set<LIGHT_LINK_SET_MAX;set++)require(data.light_link_sets[set].light_tree_root<nodes,"Invalid native light link tree root");
  for(const KernelLightTreeEmitter &emitter:light_tree_emitters){
    for(float value:{emitter.theta_o,emitter.theta_e,emitter.energy})require(std::isfinite(value),"Invalid native light tree emitter");
    if(emitter.light.id<0){
      require(size_t(~emitter.light.id)<light_records.size()&&emitter.object_id>=0&&size_t(emitter.object_id)<object_records.size(),"Invalid native light tree lamp");
      const auto type=light_records[~emitter.light.id].type;
      require(type==LIGHT_POINT||type==LIGHT_SPOT||type==LIGHT_AREA||type==LIGHT_SUN||type==LIGHT_BACKGROUND,"WebGPU direct lighting emitter type not implemented");
    }
    else if(emitter.object_id==OBJECT_NONE)require(emitter.mesh.object_id>=0&&size_t(emitter.mesh.object_id)<object_records.size()&&emitter.mesh.node_id>=0&&size_t(emitter.mesh.node_id)<nodes,"Invalid native light tree mesh");
    else{
      auto it=globals.find("tri_vindex");const size_t triangle_count=it==globals.end()?0:remaining(it->second)/sizeof(packed_uint3);
      require(emitter.object_id>=0&&size_t(emitter.object_id)<object_records.size()&&size_t(emitter.triangle.id)<triangle_count&&
        (object_records[emitter.object_id].primitive_type==PRIMITIVE_TRIANGLE||object_records[emitter.object_id].primitive_type==PRIMITIVE_MOTION_TRIANGLE),"Invalid native light tree triangle");
    }
  }
  (void)triangles;
}
void WebGPUDevice::validate_lighting(bool sampling) const {
  if(data.integrator.use_light_tree)validate_light_tree();
  require(data.integrator.num_lights>=0&&size_t(data.integrator.num_lights)==light_records.size(),"Invalid native light table");
  // Background (world) light: importance-map sampling, MIS and non-constant
  // worlds (shade_light_nee). Portals and sky-texture sun guiding are refused.
  require(data.integrator.num_portals==0&&data.background.portal_weight==0,"WebGPU background light portals not implemented");
  require(data.background.sun_weight==0,"WebGPU background sun guiding (sky texture sun disc) not implemented");
  unsigned background_lights=0;
  for(const auto &light:light_records){
    require(light.type==LIGHT_POINT||light.type==LIGHT_SUN||light.type==LIGHT_SPOT||light.type==LIGHT_AREA||light.type==LIGHT_BACKGROUND,"WebGPU direct lighting emitter type not implemented");
    const auto id=uint(light.shader_id)&SHADER_MASK;
    require(id<shader_records.size(),"Invalid native light shader");
    if(light.type==LIGHT_BACKGROUND){
      require(++background_lights==1,"WebGPU multiple background lights not implemented");
      require(id==(uint(data.background.surface_shader)&SHADER_MASK),"Invalid native background light shader");
      for(float strength:light.strength)require(std::isfinite(strength),"Invalid native light strength");
      require(!data.background.use_mis||(data.background.map_res_x>0&&data.background.map_res_y>0),"Invalid native background importance map");
      continue;
    }
    require(shader_records[id].flags&SD_HAS_CONSTANT_EMISSION,"WebGPU nonconstant point, spot, area or sun light emission not implemented");
    require(std::isfinite(light.co.x)&&std::isfinite(light.co.y)&&std::isfinite(light.co.z),"Invalid native light position/direction");
    for(float strength:light.strength)require(std::isfinite(strength),"Invalid native light strength");
    if(light.type==LIGHT_POINT||light.type==LIGHT_SPOT){require(std::isfinite(light.spot.radius)&&light.spot.radius>=0&&
      std::isfinite(light.spot.eval_fac)&&light.spot.eval_fac>=0,"Invalid native point light");}
    else if(light.type==LIGHT_AREA){
      require(std::isfinite(light.area.len_u)&&light.area.len_u>0&&std::isfinite(light.area.len_v)&&light.area.len_v>0,"WebGPU degenerate area light not implemented");
      require(std::isfinite(light.area.invarea)&&std::isfinite(light.area.tan_half_spread)&&light.area.tan_half_spread>=0&&
        std::isfinite(light.area.normalize_spread)&&light.area.normalize_spread>=0,"Invalid native area light");
      for(const float3 value:{light.area.axis_u,light.area.axis_v,light.area.dir})require(std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z),"Invalid native area basis");
    }
    else require(std::isfinite(light.sun.one_minus_cosangle)&&light.sun.one_minus_cosangle>=0&&light.sun.one_minus_cosangle<=2&&
      std::isfinite(light.sun.eval_fac)&&light.sun.eval_fac>=0&&
      std::isfinite(light.sun.angle)&&light.sun.angle>=0&&std::isfinite(light.sun.pdf)&&light.sun.pdf>=0,"Invalid native sun light");
    if(light.type==LIGHT_SPOT)require(std::isfinite(light.spot.cos_half_spot_angle)&&std::isfinite(light.spot.cos_half_larger_spread)&&
      light.spot.cos_half_larger_spread>=-1&&light.spot.cos_half_larger_spread<=1&&!std::isnan(light.spot.spot_smooth),"Invalid native spot light");
  }
  if(data.integrator.use_light_mis)require(std::isfinite(data.integrator.distribution_pdf_lights)&&data.integrator.distribution_pdf_lights>=0,"Invalid native MIS selection probability");
  require(std::isfinite(data.integrator.distribution_pdf_triangles)&&data.integrator.distribution_pdf_triangles>=0,"Invalid native triangle selection probability");
  if(!sampling)return;
  if(data.integrator.use_light_tree)return;
  const int n=data.integrator.num_distribution;
  require(n>0&&light_distribution_records.size()==size_t(n)+1,"Invalid native light distribution");
  require(std::isfinite(data.integrator.distribution_pdf_lights)&&data.integrator.distribution_pdf_lights>=0&&
    std::isfinite(data.integrator.light_inv_rr_threshold)&&data.integrator.light_inv_rr_threshold>=0,"Invalid native light sampling parameters");
  // Flat-CDF mesh lights: the GPU evaluates their sampled vertices, PDFs and MIS;
  // a triangle whose shader has no constant emission is shaded at the sampled
  // point by shade_light_nee (textured and node emission), as in the pinned kernel.
  auto triangles=globals.find("tri_vindex");const size_t triangle_count=triangles==globals.end()?0:remaining(triangles->second)/sizeof(packed_uint3);
  float previous=0;
  for(int i=0;i<=n;i++){const auto &entry=light_distribution_records[i];
    require(std::isfinite(entry.totarea)&&entry.totarea>=previous&&entry.totarea<=1,"Invalid native light CDF");previous=entry.totarea;
    if(i<n){
      if(entry.prim<0)require(size_t(~entry.prim)<light_records.size()&&data.integrator.distribution_pdf_lights>0,"Invalid native light distribution lamp");
      else {
        require(size_t(entry.prim)<triangle_count&&entry.object_id>=0&&size_t(entry.object_id)<object_records.size()&&
          (object_records[entry.object_id].primitive_type==PRIMITIVE_TRIANGLE||object_records[entry.object_id].primitive_type==PRIMITIVE_MOTION_TRIANGLE)&&
          data.integrator.distribution_pdf_triangles>0,"Invalid native triangle light distribution");
        require(size_t(entry.object_id)<object_flag_records.size(),"Invalid native triangle object flags");
        const auto flags=object_flag_records[entry.object_id];
        // Static instanced emitters pass their independent image since the
        // closest/shadow traversal reproduces the CPU oracle's arithmetic
        // exactly (contraction-proof sums, SSE grouping, RCPSS model; X2).
        (void)flags;
      }
    }
  }
  require(light_distribution_records.front().totarea==0&&previous==1,"Invalid native light CDF endpoints");
}
}
