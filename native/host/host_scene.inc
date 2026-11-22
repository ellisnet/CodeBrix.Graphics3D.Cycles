// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
namespace {
Geometry &geometry(Host &host,CBCNode id) {
  auto value=dynamic_cast<Geometry *>(host.entry(id).node);require(value,"Handle is not geometry");return *value;
}
AttributeSet &attribute_set(Geometry &geometry,uint32_t flags) {
  require(!(flags&~uint32_t(CBC_ATTR_STANDARD|CBC_ATTR_SUBDIVISION)),"Unknown attribute flags");
  if(flags&CBC_ATTR_SUBDIVISION){require(geometry.is_mesh(),"Subdivision attributes require a mesh");return static_cast<Mesh &>(geometry).subd_attributes;}
  return geometry.attributes;
}
AttributeElement attribute_element(Geometry &geometry,uint32_t domain) {
  const bool mesh=geometry.is_mesh(),volume=geometry.is_volume(),points=geometry.is_pointcloud(),hair=geometry.is_hair();
  switch(domain) {
    case CBC_ATTR_MESH:return ATTR_ELEMENT_MESH;
    case CBC_ATTR_FACE:require(mesh||volume,"Face attributes require mesh topology");return ATTR_ELEMENT_FACE;
    case CBC_ATTR_VERTEX:require(mesh||volume||points,"Vertex attributes require mesh or point topology");return ATTR_ELEMENT_VERTEX;
    case CBC_ATTR_CORNER:require(mesh,"Corner attributes require a mesh");return ATTR_ELEMENT_CORNER;
    case CBC_ATTR_CURVE:require(hair,"Curve attributes require hair");return ATTR_ELEMENT_CURVE;
    case CBC_ATTR_CURVE_KEY:require(hair,"Curve-key attributes require hair");return ATTR_ELEMENT_CURVE_KEY;
    case CBC_ATTR_VERTEX_NORMAL:require(mesh||volume,"Vertex normals require mesh topology");return ATTR_ELEMENT_VERTEX_NORMAL;
    case CBC_ATTR_CORNER_NORMAL:require(mesh,"Corner normals require a mesh");return ATTR_ELEMENT_CORNER_NORMAL;
    case CBC_ATTR_CORNER_BYTE:require(mesh,"Byte corner colors require a mesh");return ATTR_ELEMENT_CORNER_BYTE;
    case CBC_ATTR_CURVE_KEY_NORMAL:require(hair,"Curve-key normals require hair");return ATTR_ELEMENT_CURVE_KEY_NORMAL;
    default:throw Failure(CBC_INVALID,"Unknown attribute domain");
  }
}
TypeDesc attribute_type(uint32_t type) {
  switch(type) {
    case CBC_VALUE_FLOAT:return TypeFloat;
    case CBC_VALUE_POINT2:return TypeFloat2;
    case CBC_VALUE_COLOR:return TypeColor;
    case CBC_VALUE_VECTOR:return TypeVector;
    case CBC_VALUE_POINT:return TypePoint;
    case CBC_VALUE_NORMAL:return TypeNormal;
    case CBC_VALUE_FLOAT4:return TypeFloat4;
    case CBC_VALUE_BYTE4:return TypeRGBA;
    case CBC_VALUE_TRANSFORM:return TypeMatrix;
    // PROVISIONAL (ABI.md): Cycles' own storage types, written and read through Cycles' own functions below.
    case CBC_VALUE_QUATERNION:return TypeQuaternion;
    case CBC_VALUE_SPHERICAL_HARMONICS_REST:return TypePackedSphericalHarmonicsRest;
    default:throw Failure(CBC_INVALID,"Unsupported attribute value type");
  }
}
unsigned attribute_components(const TypeDesc &type,AttributeElement element) {
  if(element&ATTR_ELEMENT_IS_BYTE)return 4;
  if(type==TypeFloat)return 1;
  if(type==TypeFloat2)return 2;
  if(type==TypeFloat4||type==TypeRGBA||type==TypeQuaternion)return 4;
  if(type==TypeMatrix)return 12;
  if(type==TypePackedSphericalHarmonicsRest)return 3*PackedSphericalHarmonicsRest::MAX_COEFFICIENTS;
  return 3;
}
AttributeStandard standard_attribute(Geometry &geometry,const char *name,AttributeElement element) {
  const bool mesh=geometry.is_mesh(),points=geometry.is_pointcloud(),hair=geometry.is_hair(),volume=geometry.is_volume();
  const std::string key=name;
  if(key=="P" && (mesh||points||hair||volume))return ATTR_STD_POSITION;
  if(key=="N" && (mesh||hair||volume)) {
    if(element==ATTR_ELEMENT_CORNER_NORMAL && (mesh||volume))return ATTR_STD_CORNER_NORMAL;
    require(element==(hair?ATTR_ELEMENT_CURVE_KEY_NORMAL:ATTR_ELEMENT_VERTEX_NORMAL),"Wrong normal attribute domain");return ATTR_STD_VERTEX_NORMAL;
  }
  if(key=="radius" && (points||hair))return ATTR_STD_RADIUS;
  if(key=="uv" && (mesh||points||hair))return ATTR_STD_UV;
  if(key=="generated" && (mesh||points||hair))return ATTR_STD_GENERATED;
  if(key=="curve_intercept" && hair)return ATTR_STD_CURVE_INTERCEPT;
  if(key=="curve_length" && hair)return ATTR_STD_CURVE_LENGTH;
  if(key=="curve_random" && hair)return ATTR_STD_CURVE_RANDOM;
  if(key=="point_random" && points)return ATTR_STD_POINT_RANDOM;
  // Gaussian splats (ABI.md): Cycles finds a splat's data by these standards alone (PointCloud::primitive_type,
  // the device upload, KernelObject.gsplat). radiance:base is float4 (band-0 colour and opacity), scale a vector, rotation
  // a quaternion and radiance:sh the spherical-harmonics rest; stage_attribute requires the matching value kind.
  if(key=="radiance:base" && points)return ATTR_STD_GSPLAT_RADIANCE_BASE;
  if(key=="scale" && points)return ATTR_STD_GSPLAT_SCALE;
  if(key=="rotation" && points)return ATTR_STD_GSPLAT_ROTATION;
  if(key=="radiance:sh" && points)return ATTR_STD_GSPLAT_RADIANCE_SPHERICAL_HARMONICS_REST;
  if(key=="tangent" && mesh)return ATTR_STD_UV_TANGENT;
  if(key=="tangent_sign" && mesh)return ATTR_STD_UV_TANGENT_SIGN;
  // The Geometry node's Pointiness and Random Per Island outputs: values the caller computes, as Cycles reads them.
  if(key=="pointiness" && mesh)return ATTR_STD_POINTINESS;
  if(key=="random_per_island" && mesh)return ATTR_STD_RANDOM_PER_ISLAND;
  throw Failure(CBC_UNSUPPORTED,"Standard attribute is not exposed for this geometry");
}
// The attribute of that name an edit of the geometry applies to. Every edit restores the authored attributes first
// (before_edit, host_edits.inc): of a displaced mesh (undisplace) or of a geometry the static BVH path baked (unbake; a
// displaced copy, taken after it, wins). An attribute call checks its arguments against those, before the edit
// preparation, so that a refused call changes nothing (no restore, no tag; a splat cloud keeps its bounding radius).
const Attribute *edited_attribute(Host &host,Geometry &geom,uint32_t flags,const char *name){
  const AttributeSet &set=attribute_set(geom,flags);
  if(!(flags&CBC_ATTR_SUBDIVISION)){
    if(auto kept=host.displaced.find(&geom);kept!=host.displaced.end())return kept->second.attributes->find(ustring(name));
    if(geom.transform_applied&&bake_restorable(&geom))if(auto record=host.bakes.find(&geom);record!=host.bakes.end())return record->second.attributes->find(ustring(name));
  }
  return set.find(ustring(name));
}
struct AttributeTarget {Geometry &geom;AttributeSet &set;AttributeElement element;TypeDesc type;AttributeStandard standard;uint64_t count;};
AttributeTarget attribute_target(Host &host,CBCNode id,const char *name,const CBCAttributeConfig *config,uint64_t count) {
  require(name&&*name&&config&&config->struct_size==sizeof(*config),"Invalid attribute configuration");
  require(std::all_of(std::begin(config->reserved),std::end(config->reserved),[](auto x){return x==0;}),"Unknown attribute options");
  auto &geom=geometry(host,id);auto &set=attribute_set(geom,config->flags);
  auto element=attribute_element(geom,config->domain);auto type=attribute_type(config->value_type);
  require(!(element&ATTR_ELEMENT_IS_NORMAL)||config->value_type==CBC_VALUE_NORMAL,"Normal storage requires a normal value");
  require(bool(element&ATTR_ELEMENT_IS_BYTE)==(config->value_type==CBC_VALUE_BYTE4),"Byte storage requires a byte corner domain");
  // Quaternion and spherical-harmonics attributes (ABI.md): quaternions on meshes, point clouds and curves,
  // spherical harmonics on meshes and point clouds. On curves the pinned kernels fetch a quaternion wherever they fetch
  // any attribute: per curve key, interpolated along the segment, and per curve (curve_attribute, kernel/geom/curve.h),
  // and on the mesh domain (primitive_surface_attribute, kernel/geom/primitive.h), and the device upload packs it for any
  // geometry (scene/geometry_attributes.cpp); the curve-key normal domain takes normals only. Spherical harmonics they
  // fetch only for Gaussian splats (attributes_spherical_harmonics_rest in kernel/geom/gsplat.h; the Attribute node
  // returns no value for them on any geometry, svm_node_attr_surface_eval in kernel/svm/attribute.h), so on curves they
  // stay refused.
  // Cycles does not interpolate spherical-harmonics data over subdivided faces (subd/interpolation.cpp has no case for
  // it), so a subdivision attribute of that kind would leave the diced mesh with values nobody set: refused.
  const bool quaternion=config->value_type==CBC_VALUE_QUATERNION,harmonics=config->value_type==CBC_VALUE_SPHERICAL_HARMONICS_REST;
  require(!quaternion||geom.is_mesh()||geom.is_pointcloud()||geom.is_hair(),
          "Unsupported feature: quaternion attributes are accepted on meshes, point clouds and curves only",CBC_UNSUPPORTED);
  require(!harmonics||geom.is_mesh()||geom.is_pointcloud(),
          "Unsupported feature: spherical-harmonics attributes are accepted on meshes and point clouds only (the pinned kernels read them "
          "only for Gaussian splats, kernel/geom/gsplat.h, and never on curves)",CBC_UNSUPPORTED);
  require(!harmonics||!(config->flags&CBC_ATTR_SUBDIVISION),
          "Unsupported feature: spherical-harmonics attributes cannot be subdivision attributes (Cycles does not interpolate them over subdivided faces)",CBC_UNSUPPORTED);
  auto standard=config->flags&CBC_ATTR_STANDARD?standard_attribute(geom,name,element):ATTR_STD_NONE;
  auto existing=edited_attribute(host,geom,config->flags,name);require(!existing||existing->std==ATTR_STD_NONE||standard!=ATTR_STD_NONE,"Use standard mode to replace a standard attribute");
  const auto elements=Attribute::element_size(&geom,element,set.prim);
  require(elements<=INT_MAX && count==uint64_t(elements)*attribute_components(type,element),"Attribute count does not match geometry");
  return {geom,set,element,type,standard,elements};
}
Attribute *stage_attribute(AttributeSet &staged,const AttributeTarget &target,const char *name) {
  auto value=target.standard==ATTR_STD_NONE?staged.add(ustring(name),target.type,target.element):staged.add(target.standard,ustring(name));
  require(value->type==target.type && value->element==target.element && uint64_t(value->size)==target.count,"Standard attribute type/domain does not match configuration");
  return value;
}
Attribute &find_attribute(Host &host,CBCNode id,const char *name,uint32_t flags) {
  require(name&&*name,"Attribute name is required");auto &geom=geometry(host,id);auto &set=attribute_set(geom,flags);
  auto value=set.find(ustring(name));require(value,"Unknown attribute");
  require(!(flags&CBC_ATTR_STANDARD)||value->std!=ATTR_STD_NONE,"Attribute is not standard");return *value;
}
// The largest image side Cycles' CPU kernels address (PROVISIONAL; the capability report's maxImageDimension of the cpu and
// cpu-embree backends): ImageInterpolator's wrap_mirror (kernel/util/image.h), the Mirror extension, computes 2 * width and
// 2 * height in int, defined only up to INT_MAX / 2, 1073741823 (also the WebGPU device's limit). The CPU device itself
// checks no image size.
constexpr int64_t cpu_max_image_dimension=INT32_MAX/2;
// The size at which Cycles loads an image bound whole: ImageManager::device_load_image (scene/image.cpp) reduces an image
// whose larger side exceeds the texture limit (cbc_host_texture_limit) by texture_resolution = min(SceneParams'
// texture_resolution, limit / side), ImageCache::load_full (scene/image_cache.cpp) scales it by that factor rounded down to
// a power of two when below 1, and util_image_resize_pixels (util/image_impl.h) makes each side max(1, int64(side * factor));
// in binary32 as there (a limit of 1073741823 is 2^30 as a float, so it leaves an image of 2^30 whole).
std::pair<int64_t,int64_t> loaded_image_size(const SceneParams &params,int64_t width,int64_t height){
  const int max_dim=int(std::max(width,height));
  float texture_resolution=params.texture_resolution;
  if(params.texture_limit>0&&max_dim>params.texture_limit)texture_resolution=std::min(texture_resolution,float(params.texture_limit)/float(max_dim));
  float scale_factor=1.0f;
  if(texture_resolution<1.0f)scale_factor=powf(2.0f,floorf(log2f(texture_resolution)));
  if(!(scale_factor>0.0f&&scale_factor<1.0f))return {width,height};
  return {std::max(int64_t(float(width)*scale_factor),int64_t(1)),std::max(int64_t(float(height)*scale_factor),int64_t(1))};
}
// Every image bound whole in an authoring graph (each UDIM tile too) loads within cpu_max_image_dimension; a tiled image
// (cbc_host_image_create_tiled) is loaded in tiles through Cycles' texture cache and is not bound by it.
void images_within_cpu_limit(Host &host){
  const SceneParams &params=host.session->scene->params;
  for(auto &[shader,stored]:host.graphs)for(auto &[node,binding]:stored->images)for(auto &[tile,data]:binding){
    if(data->tiled)continue;
    const auto [width,height]=loaded_image_size(params,data->width,data->height);
    require(width<=cpu_max_image_dimension&&height<=cpu_max_image_dimension,
            ("Unsupported feature: an image wider or taller than 1073741823 pixels on the CPU backends (Cycles' CPU kernels compute twice its "
             "width and height in int for the Mirror extension, kernel/util/image.h); this image loads at "+std::to_string(width)+" x "+
             std::to_string(height)+(params.texture_limit?" with the texture limit":"")+": pass a smaller image or set a texture limit that reduces it").c_str(),
            CBC_UNSUPPORTED);
  }
}
// Requests the pinned Cycles accepts but this library cannot honour: refused by name, never ignored (on every backend
// unless the test names one).
void validate_unavailable(Host &host) {
  // Gaussian splats render on every backend (validate_gsplats below; on the WGSL device native/shaders/wavefront_gsplat.wgsl).
  // UDIM tiles (cbc_host_image_bind_tiles, PROVISIONAL) render on every backend: the WGSL device reads Cycles' UDIM table
  // (image_sample in native/shaders/wavefront_images.wgsl; oracle scenes images/udim-*).
  // Tiled images (cbc_host_image_create_tiled, PROVISIONAL) render through Cycles' texture cache on every backend; the
  // WGSL device maps a lookup to its mip level and tile (with the dual-number projections Cycles uses for it) and runs a
  // kernel again once the tiles it requested are loaded (native/cycles/image.cpp tile_transaction).
  // BVH spatial splits (cbc_host_bvh_spatial_split, PROVISIONAL) apply to Cycles' BVH2 builder on every backend that
  // traverses it and to Embree. A split BVH2 references one primitive from several leaves; the WGSL traversal reads
  // Cycles' packed tables as they are and drops a primitive met twice where the pinned kernel does (a recorded
  // transparent shadow hit, a local hit at the same distance).
  // Tiled rendering (cbc_host_tile_size, PROVISIONAL) on every backend: Cycles renders tile by tile, with tile offsets, a
  // sample count pass and, with adaptive sampling, an overscan, and the library keeps the finished tiles in memory
  // (oracle scenes render-settings/tiles-*).
  // The texture limit (cbc_host_texture_limit, PROVISIONAL) reduces images on the host before any device sees them, so it
  // applies to every backend (oracle scenes images/texture-limit-*).
  // Progressive results (CBC_HOST_PROGRESSIVE_RESULTS, PROVISIONAL) are delivered on every backend: a display update
  // reads the render buffers of the device between batches of samples, as the final read does.
  // A Both displacement shader on curves or points: its bump from displacement starts with svm_node_enter_bump_eval,
  // which finds no undisplaced position there (only meshes have one) and saves no differential, so svm_node_set_bump
  // reads stack entries nothing wrote; the pinned kernels have no defined result on any device (native/DEVICE.md 87).
  // Refused when a hair or point cloud geometry lists a caller's shader whose displacement method is Both and whose
  // output's Displacement input is linked (Cycles adds the bump state only for Both, and the bump only with a displacement).
  for(auto geom:host.session->scene->geometry)if(dynamic_cast<Hair *>(geom)||dynamic_cast<PointCloud *>(geom))
    for(auto used:geom->get_used_shaders()){
      auto shader=static_cast<Shader *>(used);auto stored=host.graphs.find(shader);
      require(stored==host.graphs.end()||shader->get_displacement_method()!=DISPLACE_BOTH||!stored->second->authoring->output()->input("Displacement")->link,
              "Unsupported feature: curves and points with a Both displacement shader (Cycles' bump from displacement reads values it never sets there; "
              "use displacement method bump or true)",CBC_UNSUPPORTED);
    }
  // An area light with an axis of infinities (PROVISIONAL), on every backend. AreaLight::copy_to_kernel (scene/light.cpp)
  // gives the kernels each axis as normalize_len (util/math_float3.h) of the object transform's column times the light's
  // size: the vector times one over its length. A vector with no zero component whose squared length underflows to zero
  // becomes an axis of infinities without a NaN, whose comparisons in the kernels depend on the signs of each ray's
  // components (native/DEVICE.md item 148): the CPU and Metal films of such a light differ in 879 practical values, up to
  // 1.1e3 (the survey's lamps/area-degenerate-infinite-axis). A vector with a zero component gets a NaN there instead, and
  // that degenerate light stays accepted (no sample; a strip or a plane for camera rays), as do portals of zero extent.
  // Computed with Cycles' own functions from the light's own values. A light that Light::has_contribution disables by its
  // strength, its size or a zero transform column never reaches the kernels and is not refused (its test of the emission
  // shader needs the compiled shader, which the render compiles later, and is not made here).
  for(auto object:host.session->scene->objects){
    auto light=dynamic_cast<AreaLight *>(object->get_geometry());
    if(!light||light->is_portal_light()||light->get_strength()==zero_float3()||light->get_sizeu()*light->get_sizev()==0.0f)continue;
    const Transform &tfm=object->get_tfm();
    const float3 u=transform_get_column(&tfm,0),v=transform_get_column(&tfm,1);
    if(is_zero(u)||is_zero(v))continue;
    for(const auto &[extent,axis_name]:{std::pair{u*light->get_sizeu(),"u axis (its object transform's first column times sizeu)"},
                                        std::pair{v*light->get_sizev(),"v axis (its object transform's second column times sizev)"}}){
      float length;const float3 axis=normalize_len(extent,&length);
      const bool nan=std::isnan(axis.x)||std::isnan(axis.y)||std::isnan(axis.z);
      require(nan||(std::isfinite(axis.x)&&std::isfinite(axis.y)&&std::isfinite(axis.z)),
              (std::string("Unsupported feature: area lights with an axis of infinities (the light's ")+axis_name+" has no zero component "
               "but a length that underflows to zero, so Cycles divides it by zero; give the light a larger size or object scale)").c_str(),CBC_UNSUPPORTED);
    }
  }
  // The largest image side of the CPU backends (PROVISIONAL, the capability report's maxImageDimension of cpu and
  // cpu-embree): an image bound whole and loaded wider or taller than that is refused by name (images_within_cpu_limit).
  if(host.backend==CBC_HOST_CPU_REFERENCE||host.backend==CBC_HOST_CPU)images_within_cpu_limit(host);
  // The DLSS denoiser is not part of this library; the pinned Cycles would fall back to another denoiser with no message.
  auto integrator=host.session->scene->integrator;
  require(!integrator->get_use_denoise()||integrator->get_denoiser_type()!=DENOISER_DLSS,
          "Unsupported feature: the dlss denoiser is not available in this library (use openimagedenoise)",CBC_UNSUPPORTED);
  // denoiser_passes is Cycles' DenoiserPass bit set (device/denoise.h). A bit outside it is read by nothing; a caller
  // built against a different numbering of the guides is told instead of rendering without the guide it asked for.
  const int denoiser_guides=DENOISER_PASS_ALBEDO|DENOISER_PASS_SPECULAR_ALBEDO|DENOISER_PASS_NORMAL|DENOISER_PASS_ROUGHNESS|DENOISER_PASS_DEPTH|
                            DENOISER_PASS_BACKWARD_MOTION|DENOISER_PASS_SPECULAR_MOTION;
  require(!integrator->get_use_denoise()||!(integrator->get_denoiser_passes()&~denoiser_guides),
          "Invalid denoiser_passes: a bit that is not a denoiser guide pass (albedo 1, specular albedo 2, normal 4, roughness 8, depth 16, backward motion 32, specular motion 64)");
}
// A point cloud rendered as Gaussian splats (every backend). The pinned Cycles renders the cloud as splats only
// when it finds the standard attributes radiance:base, scale and rotation (PointCloud::primitive_type) and draws plain
// points otherwise, with no message; create_missing_gsplat_attributes, which would supply defaults, has no caller in the
// pinned source. So a splat cloud without them is refused, naming what is missing. quaternion_to_rotation
// (util/math_float3x3.h) and gsplat_normal (kernel/geom/gsplat.h) divide by the rotation's squared length
// w*w + x*x + y*y + z*z: a rotation none of whose components has a normal binary32 square (its squared length is then
// zero or subnormal, and GPUs flush subnormals to zero) or whose squared length overflows has no defined splat and is
// refused too.
void validate_gsplats(const PointCloud &points) {
  std::string missing;
  for(const auto &[standard,name]:{std::pair{ATTR_STD_GSPLAT_RADIANCE_BASE,"radiance:base"},std::pair{ATTR_STD_GSPLAT_SCALE,"scale"},
                                   std::pair{ATTR_STD_GSPLAT_ROTATION,"rotation"}})
    if(!points.attributes.find(standard))missing+=(missing.empty()?"":", ")+std::string(name);
  require(missing.empty(),("Gaussian splats need the standard point attributes radiance:base, scale and rotation, set with CBC_ATTR_STANDARD (missing: "+
                           missing+")").c_str());
  const Attribute &rotation=*points.attributes.find(ATTR_STD_GSPLAT_ROTATION);
  const double smallest=double(std::numeric_limits<float>::min()),largest=double(std::numeric_limits<float>::max());
  for(int step=0;step<=int(rotation.motion.size());++step) {
    const Quaternion *q=rotation.data<Quaternion>(step);
    for(int i=0;i<rotation.size;++i) {
      const double w=double(q[i].w)*q[i].w,x=double(q[i].x)*q[i].x,y=double(q[i].y)*q[i].y,z=double(q[i].z)*q[i].z;
      require(std::max({w,x,y,z})>=smallest,("Gaussian splat rotation of point "+std::to_string(i)+" has zero length (Cycles divides by its "
              "squared length w*w + x*x + y*y + z*z, and no component's square is a normal binary32 number)").c_str());
      require(w+x+y+z<=largest,("Gaussian splat rotation of point "+std::to_string(i)+" is too long (Cycles divides by its squared length "
              "w*w + x*x + y*y + z*z, which overflows binary32)").c_str());
    }
  }
}
void validate_geometry(Host &host) {
  validate_unavailable(host);
  auto &scene=*host.session->scene;
  for(auto object:scene.objects)require(object->get_geometry(),"Object has no geometry");
  for(auto geom:scene.geometry) {
    if(auto mesh=dynamic_cast<Mesh *>(geom)) {
      const auto &triangles=mesh->get_triangles();require(triangles.size()%3==0,"Incomplete triangle index array");
      for(auto index:triangles)require(index>=0 && size_t(index)<mesh->num_verts(),"Triangle index is outside the position array");
      const auto faces=triangles.size()/3;
      require(mesh->get_shader().empty()||mesh->get_shader().size()>=faces,"Triangle shader array is too short");
      require(mesh->get_smooth().empty()||mesh->get_smooth().size()>=faces,"Triangle smoothing array is too short");
      // Without OpenSubdiv (outside this library) the pinned Cycles turns Catmull-Clark into linear subdivision with no
      // message (scene/mesh_subdivision.cpp); the scene is refused instead of being approximated.
      require(mesh->get_subdivision_type()!=Mesh::SUBDIVISION_CATMULL_CLARK,
              "Unsupported feature: Catmull-Clark subdivision needs OpenSubdiv, which this library does not include",CBC_UNSUPPORTED);
      // Subdivision faces (cbc_host_subd_mesh) render only through a subdivision type; without one Cycles would skip them.
      require(!mesh->get_num_subd_faces()||mesh->get_subdivision_type()!=Mesh::SUBDIVISION_NONE,
              "Subdivision faces need subdivision_type linear");
    } else if(auto hair=dynamic_cast<Hair *>(geom)) {
      const auto &first=hair->get_curve_first_key();require(hair->get_curve_shader().size()>=first.size(),"Curve shader array is too short");
      if(!first.empty())require(first[0]==0,"First curve must start at key zero");
      for(size_t i=0;i<first.size();++i){const size_t end=i+1<first.size()?size_t(first[i+1]):hair->num_keys();require(first[i]>=0&&size_t(first[i])<end&&end<=hair->num_keys(),"Invalid curve key range");}
    } else if(auto points=dynamic_cast<PointCloud *>(geom)) {
      require(points->get_shader().size()>=points->num_points(),"Point shader array is too short");
      if(points->get_render_as()==PointCloud::RENDER_AS_GSPLATS)validate_gsplats(*points);
    }
    for(const auto &attr:geom->attributes.attributes) {
      require(size_t(attr.size)==Attribute::element_size(geom,attr.element,geom->attributes.prim),"Attribute no longer matches geometry topology");
    }
  }
}
// A world with a volume needs a background light object that uses the world shader. Pinned Cycles
// builds the world's volume octree only from such a light (scene/volume.cpp,
// VolumeManager::initialize_octree), and the world's volume stack entry names
// background.object_index, the background light's object (scene/object.cpp; 0 when there is none).
// Without one, the octree root search (kernel/integrator/shade_volume.h, volume_find_octree_root)
// finds no root for the world shader and reads outside the root array once a path meets the world
// volume together with a heterogeneous one: the CPU kernel faults and the WebGPU device is lost.
// Cycles' own hosts give the background light the world shader (hydra/session.cpp,
// app/cycles_xml.cpp). When the caller created no background light, the host adds one for the
// render, without MIS (LightManager::test_enabled_lights leaves it unsampled unless the scene has a
// portal), and removes it afterwards. A caller's background light that does not list the world
// shader gets it appended for the render (its own shader, the first listed or the default light
// shader when it lists none, is unchanged), and its list back afterwards.
struct WorldVolumeLight {
  Scene &scene; Object *object=nullptr; Light *light=nullptr;
  std::vector<std::pair<Light *,array<Node *>>> listed;
  ~WorldVolumeLight(){
    for(auto &[caller,shaders]:listed){caller->set_used_shaders(shaders);caller->tag_update(&scene);}
    if(object){scene.delete_node(object);}
    if(light){scene.delete_node(light);}
  }
};
void world_volume_light(Host &host,WorldVolumeLight &added) {
  auto &scene=*host.session->scene; Shader *world=scene.background->get_shader(&scene);
  auto graph=host.graphs.find(world); if(graph==host.graphs.end())return;
  ShaderInput *volume=graph->second->authoring->output()->input("Volume"); if(!volume||!volume->link)return;
  bool background_light=false;std::vector<Light *> unlisted;
  for(auto object:scene.objects) {
    auto geom=object->get_geometry();
    if(!geom||!geom->is_light()||!static_cast<Light *>(geom)->is_background_light())continue;
    background_light=true;
    const auto &shaders=geom->get_used_shaders();
    if(std::find(shaders.begin(),shaders.end(),static_cast<Node *>(world))==shaders.end())unlisted.push_back(static_cast<Light *>(geom));
  }
  for(Light *caller:unlisted){
    const array<Node *> &original=caller->get_used_shaders();array<Node *> shaders;
    if(original.empty())shaders.push_back_slow(scene.default_light);
    for(auto shader:original)shaders.push_back_slow(shader);
    shaders.push_back_slow(world);added.listed.emplace_back(caller,original);caller->set_used_shaders(shaders);caller->tag_update(&scene);
  }
  if(background_light)return;
  auto light=scene.create_node<BackgroundLight>(); light->set_use_mis(false);
  array<Node *> shaders; shaders.push_back_slow(world); light->set_used_shaders(shaders); added.light=light;
  auto object=scene.create_node<Object>(); object->set_geometry(light); added.object=object;
}
// Subdivision attributes reach the diced mesh only through Cycles' tessellation, which runs when the cage's positions or
// its dicing settings changed (Mesh::need_tessellation, scene/mesh.cpp), not when an attribute did: an edit of one tags
// the positions, so the next render or bake dices the cage again with it, as for a new scene.
void subdivision_attribute_edited(Geometry &geom,uint32_t flags){if(flags&CBC_ATTR_SUBDIVISION)static_cast<Mesh &>(geom).tag_position_modified();}
}
extern "C" int cbc_host_attribute_floats(CBCHost handle,CBCNode id,const char *name,const CBCAttributeConfig *config,const float *values,uint64_t count) {
  return edit(handle,[&](Host &host) {
    // Every argument is checked and the value staged before the edit preparation (edited_attribute), so a refused call
    // changes nothing; the target's geometry, set, domain and count are those the edit applies to.
    count_ok(values,count);for(size_t i=0;i<count;++i)require(std::isfinite(values[i]),"Attribute contains a nonfinite value");
    auto &entry=host.entry(id);auto target=attribute_target(host,id,name,config,count);require(!(target.element&ATTR_ELEMENT_IS_BYTE),"Use byte attribute input");
    // Cycles quantizes spherical-harmonics coefficients from [-1, 1] to 8 bits and clips anything outside
    // (quantize_spherical_harmonics); a clipped value would be an approximation, so it is refused instead.
    if(target.type==TypePackedSphericalHarmonicsRest)for(size_t i=0;i<count;++i)
      require(values[i]>=-1.0f&&values[i]<=1.0f,"Spherical-harmonics coefficients must lie in [-1, 1] (Cycles stores them at 8 bits over that range and would clip the rest)");
    AttributeSet staged(&target.geom,target.set.prim);auto value=stage_attribute(staged,target,name);
    const unsigned components=attribute_components(target.type,target.element);
    for(size_t i=0;i<target.count;++i) {
      const float *v=values+i*components;
      if(target.element&ATTR_ELEMENT_IS_NORMAL)value->data_for_write<packed_normal>()[i]=packed_normal(make_float3(v[0],v[1],v[2]));
      else if(target.type==TypeFloat)value->data_for_write<float>()[i]=v[0];
      else if(target.type==TypeFloat2)value->data_for_write<float2>()[i]=make_float2(v[0],v[1]);
      else if(target.type==TypeFloat4||target.type==TypeRGBA)value->data_for_write<float4>()[i]=make_float4(v[0],v[1],v[2],v[3]);
      else if(target.type==TypeMatrix)value->data_for_write<Transform>()[i]=read_transform(v);
      else if(target.type==TypeQuaternion)value->data_for_write<Quaternion>()[i]=make_quaternion(v[0],v[1],v[2],v[3]);
      else if(target.type==TypePackedSphericalHarmonicsRest){
        auto &packed=value->data_for_write<PackedSphericalHarmonicsRest>()[i];
        for(int k=0;k<PackedSphericalHarmonicsRest::MAX_COEFFICIENTS;++k)spherical_harmonics_rest_set_coefficient(packed,k,make_float3(v[3*k],v[3*k+1],v[3*k+2]));
      }
      else value->data_for_write<packed_float3>()[i]=make_float3(v[0],v[1],v[2]);
    }
    before_edit(host,entry,nullptr);
    target.set.add_from(std::move(*value));target.geom.tag_update(host.session->scene.get(),true);subdivision_attribute_edited(target.geom,config->flags);
  });
}
extern "C" int cbc_host_attribute_bytes(CBCHost handle,CBCNode id,const char *name,const CBCAttributeConfig *config,const uint8_t *values,uint64_t count) {
  return edit(handle,[&](Host &host) {
    // Checked and staged before the edit preparation, as cbc_host_attribute_floats.
    count_ok(values,count);auto &entry=host.entry(id);auto target=attribute_target(host,id,name,config,count);require(target.element&ATTR_ELEMENT_IS_BYTE,"Expected byte corner colors");
    AttributeSet staged(&target.geom,target.set.prim);auto value=stage_attribute(staged,target,name);
    if(count)std::memcpy(value->data_for_write<uchar4>(),values,size_t(count));
    before_edit(host,entry,nullptr);
    target.set.add_from(std::move(*value));target.geom.tag_update(host.session->scene.get(),true);subdivision_attribute_edited(target.geom,config->flags);
  });
}
// Reads return authored values: a geometry baked by the static BVH path
// answers from the attributes captured before the bake, a displaced mesh from
// those captured before its displacement, a splat cloud's radius from the
// radius kept before Cycles replaced it (host_edits.inc).
Attribute &authored_attribute(Host &host,CBCNode id,const char *name,uint32_t flags) {
  auto &attr=find_attribute(host,id,name,flags);auto &geom=geometry(host,id);
  // The radius of a splat cloud as authored: Cycles replaced it at the last render (host_edits.inc, keep_splat_radii).
  if(attr.std==ATTR_STD_RADIUS)if(auto kept=host.splat_radii.find(&geom);kept!=host.splat_radii.end())return *kept->second->find(ATTR_STD_RADIUS);
  // A mesh with true displacement as authored: the last render displaced it (host_edits.inc, reconcile_displacement).
  if(!(flags&CBC_ATTR_SUBDIVISION))if(auto kept=host.displaced.find(&geom);kept!=host.displaced.end()){
    auto authored=kept->second.attributes->find(ustring(name));require(authored,"Unknown attribute");return *authored;}
  auto record=host.bakes.find(&geom);
  if((flags&CBC_ATTR_SUBDIVISION)||!geom.transform_applied||record==host.bakes.end())return attr;
  auto authored=record->second.attributes->find(ustring(name));require(authored,"Unknown attribute");return *authored;
}
// The bounding radius of each splat (CBC_ATTR_SPLAT_BOUNDING_RADIUS): what Cycles' device update computed
// for a modified splat cloud from each splat's opacity (radiance:base) and scale (PointCloud::update_gsplat_radii,
// scene/pointcloud.cpp, called by GeometryManager::device_update_preprocess) and bounds the splats with in the BVH. It
// is the cloud's radius attribute from then on (the centre step, as a read without the flag returns the authored one),
// until the next edit of the cloud writes the authored radius back (keep_splat_radii and restore_splat_radius,
// host_edits.inc). The host holds the authored radius from the start of a render (keep_splat_radii), and Cycles clears
// the cloud's modified flags only once its device update has finished (GeometryManager::device_update), so the computed
// radius is there exactly when the host holds the authored one and the cloud is no longer modified: not before the first
// render, nor after an edit, nor after a render that stopped before its device update finished.
std::vector<float> splat_bounding_radius(Host &host,CBCNode id,const char *name,uint32_t flags) {
  require(name&&std::strcmp(name,"radius")==0,"CBC_ATTR_SPLAT_BOUNDING_RADIUS reads the standard attribute radius only");
  require(!(flags&CBC_ATTR_SUBDIVISION),"CBC_ATTR_SPLAT_BOUNDING_RADIUS reads the radius of a point cloud, not a subdivision attribute");
  auto &geom=geometry(host,id);auto points=dynamic_cast<PointCloud *>(&geom);
  require(points&&points->get_render_as()==PointCloud::RENDER_AS_GSPLATS,
          "CBC_ATTR_SPLAT_BOUNDING_RADIUS reads the bounding radius of a Gaussian splat cloud only (a point cloud with render_as gaussian_splats)");
  require(host.splat_radii.count(&geom)&&!geom.is_modified(),
          "The bounding radius of a Gaussian splat cloud is Cycles' and is read after a render or bake has computed it for the cloud as it is "
          "(not before the first render, nor after an edit of the cloud)",CBC_NOT_READY);
  const Attribute *radius=geom.attributes.find(ATTR_STD_RADIUS);require(radius,"Unknown attribute");
  return std::vector<float>(radius->data<float>(),radius->data<float>()+radius->size);
}
extern "C" int cbc_host_attribute_get_floats(CBCHost handle,CBCNode id,const char *name,uint32_t flags,float *values,uint64_t capacity,uint64_t *required) {
  return edit(handle,[&](Host &host) {
    if(flags&CBC_ATTR_SPLAT_BOUNDING_RADIUS){
      require(!(flags&~uint32_t(CBC_ATTR_STANDARD|CBC_ATTR_SUBDIVISION|CBC_ATTR_SPLAT_BOUNDING_RADIUS)),"Unknown attribute flags");
      copy_result(splat_bounding_radius(host,id,name,flags),values,capacity,required);return;
    }
    auto &attr=authored_attribute(host,id,name,flags);require(!(attr.element&(ATTR_ELEMENT_IS_BYTE|ATTR_ELEMENT_VOXEL)),"Attribute does not contain float input");
    std::vector<float> result;result.reserve(size_t(attr.size)*attribute_components(attr.type,attr.element));
    for(int i=0;i<attr.size;++i) {
      if(attr.element&ATTR_ELEMENT_IS_NORMAL){auto v=attr.data<packed_normal>()[i].decode();result.insert(result.end(),{v.x,v.y,v.z});}
      else if(attr.type==TypeFloat)result.push_back(attr.data<float>()[i]);
      else if(attr.type==TypeFloat2){auto v=attr.data<float2>()[i];result.insert(result.end(),{v.x,v.y});}
      else if(attr.type==TypeFloat4||attr.type==TypeRGBA){auto v=attr.data<float4>()[i];result.insert(result.end(),{v.x,v.y,v.z,v.w});}
      else if(attr.type==TypeMatrix){auto v=attr.data<Transform>()[i];result.insert(result.end(),{v.x.x,v.x.y,v.x.z,v.x.w,v.y.x,v.y.y,v.y.z,v.y.w,v.z.x,v.z.y,v.z.z,v.z.w});}
      else if(attr.type==TypeQuaternion){auto q=attr.data<Quaternion>()[i];result.insert(result.end(),{q.w,q.x,q.y,q.z});}
      else if(attr.type==TypePackedSphericalHarmonicsRest){
        // The stored value, as for packed normals: each coefficient as Cycles unquantizes it.
        const auto &packed=attr.data<PackedSphericalHarmonicsRest>()[i];
        for(int k=0;k<PackedSphericalHarmonicsRest::MAX_COEFFICIENTS;++k){auto c=spherical_harmonics_rest_get_coefficient(packed,k);result.insert(result.end(),{c.x,c.y,c.z});}
      }
      else {auto v=attr.data<packed_float3>()[i];result.insert(result.end(),{v.x,v.y,v.z});}
    }
    copy_result(result,values,capacity,required);
  });
}
extern "C" int cbc_host_attribute_get_bytes(CBCHost handle,CBCNode id,const char *name,uint32_t flags,uint8_t *values,uint64_t capacity,uint64_t *required) {
  return edit(handle,[&](Host &host) {
    auto &attr=authored_attribute(host,id,name,flags);require(attr.element&ATTR_ELEMENT_IS_BYTE,"Attribute is not byte storage");
    const size_t count=size_t(attr.size)*4;std::vector<uint8_t> result(count);
    if(count)std::memcpy(result.data(),attr.data<uchar4>(),count);
    copy_result(result,values,capacity,required);
  });
}
extern "C" int cbc_host_attribute_remove(CBCHost handle,CBCNode id,const char *name,uint32_t flags) {
  return edit(handle,[&](Host &host) {
    // Checked against the attribute the removal applies to before the edit preparation (edited_attribute), so a refused
    // call changes nothing; the attribute is looked up again after it, in the restored set.
    require(name&&*name,"Attribute name is required");auto &entry=host.entry(id);auto &geom=geometry(host,id);
    const Attribute *edited=edited_attribute(host,geom,flags,name);require(edited,"Unknown attribute");
    require(!(flags&CBC_ATTR_STANDARD)||edited->std!=ATTR_STD_NONE,"Attribute is not standard");
    require(edited->std!=ATTR_STD_POSITION&&edited->std!=ATTR_STD_RADIUS,"Cannot remove required geometry storage");
    before_edit(host,entry,nullptr);auto &attr=find_attribute(host,id,name,flags);
    attribute_set(geom,flags).remove(&attr);geom.tag_update(host.session->scene.get(),true);subdivision_attribute_edited(geom,flags);
  });
}
// Validates and copies caller pixels; shared by image creation and update.
static std::shared_ptr<MemoryImageData> image_data(const CBCImageConfig *config,const void *pixels,uint64_t bytes) {
    require(config&&config->struct_size==sizeof(*config),"Invalid image configuration");
    require(std::all_of(std::begin(config->reserved),std::end(config->reserved),[](auto x){return x==0;}),"Unknown image options");
    require(config->tile_size==0,"A tile size applies to tiled images only (cbc_host_image_create_tiled); it must be 0 here");
    require(config->width&&config->height&&config->width<=INT_MAX&&config->height<=INT_MAX,"Invalid image dimensions");
    ImageDataType type;unsigned channels,component_bytes;
    switch(config->format) {
      case CBC_IMAGE_FLOAT:type=IMAGE_DATA_TYPE_FLOAT;channels=1;component_bytes=4;break;
      case CBC_IMAGE_FLOAT4:type=IMAGE_DATA_TYPE_FLOAT4;channels=4;component_bytes=4;break;
      case CBC_IMAGE_HALF:type=IMAGE_DATA_TYPE_HALF;channels=1;component_bytes=2;break;
      case CBC_IMAGE_HALF4:type=IMAGE_DATA_TYPE_HALF4;channels=4;component_bytes=2;break;
      case CBC_IMAGE_BYTE:type=IMAGE_DATA_TYPE_BYTE;channels=1;component_bytes=1;break;
      case CBC_IMAGE_BYTE4:type=IMAGE_DATA_TYPE_BYTE4;channels=4;component_bytes=1;break;
      case CBC_IMAGE_USHORT:type=IMAGE_DATA_TYPE_USHORT;channels=1;component_bytes=2;break;
      case CBC_IMAGE_USHORT4:type=IMAGE_DATA_TYPE_USHORT4;channels=4;component_bytes=2;break;
      default:throw Failure(CBC_INVALID,"Unknown image format");
    }
    const uint64_t area=uint64_t(config->width)*config->height;
    require(area<=SIZE_MAX/(channels*component_bytes)&&pixels&&bytes==area*channels*component_bytes,"Incorrect image byte count");
    const auto data=static_cast<const uint8_t *>(pixels);
    for(uint64_t offset=0;offset<bytes;offset+=component_bytes) {
      if(type==IMAGE_DATA_TYPE_FLOAT||type==IMAGE_DATA_TYPE_FLOAT4){float value;std::memcpy(&value,data+offset,4);require(std::isfinite(value),"Nonfinite image value");}
      else if(type==IMAGE_DATA_TYPE_HALF||type==IMAGE_DATA_TYPE_HALF4){uint16_t value;std::memcpy(&value,data+offset,2);require((value&0x7c00)!=0x7c00,"Nonfinite half image value");}
    }
    auto copy=std::make_shared<MemoryImageData>();copy->identity=new_id();copy->width=config->width;copy->height=config->height;copy->type=type;copy->channels=channels;
    copy->pixels.assign(data,data+size_t(bytes));return copy;
}
extern "C" int cbc_host_image_create(CBCHost handle,const CBCImageConfig *config,const void *pixels,uint64_t bytes,CBCImage *image) {
  if(image)*image=0;
  return edit(handle,[&](Host &host) {
    require(image,"Invalid image configuration");
    auto copy=image_data(config,pixels,bytes);host.images.emplace(copy->identity,copy);*image=copy->identity;
  });
}
extern "C" int cbc_host_image_bind(CBCHost handle,CBCNode id,CBCImage image) {
  return edit(handle,[&](Host &host) {
    auto &entry=host.entry(id);require(entry.graph,"Texture must belong to an authoring graph");
    auto node=dynamic_cast<ImageSlotTextureNode *>(entry.node);
    require(node&&(dynamic_cast<ImageTextureNode *>(node)||dynamic_cast<EnvironmentTextureNode *>(node)),"Expected image or environment texture node");
    if(image){auto it=host.images.find(image);require(it!=host.images.end(),"Invalid or foreign image handle");entry.graph->images[node]=ImageBinding{{0,it->second}};}
    else entry.graph->images.erase(node);
    node->handle.clear();entry.graph->dirty=true;
  });
}
// UDIM tiles (PROVISIONAL, ABI.md "Images"): count images bound to one Image Texture node, images[i] at tile tiles[i], for
// Cycles' multi-loader ImageManager::add_image (prepare_graphs). Cycles builds UDIM handles for Image Texture nodes only,
// and ImageTextureNode::compile decodes every tile with the first tile's metadata ("All tiles have the same metadata"),
// so every image must have one format. Validated completely before the binding changes.
extern "C" int cbc_host_image_bind_tiles(CBCHost handle,CBCNode id,const uint32_t *tiles,const CBCImage *images,uint64_t count) {
  return edit(handle,[&](Host &host) {
    require(count>0,"UDIM tiles need at least one tile and its image");
    require(count<=INT_MAX&&tiles&&images,"Invalid UDIM tile count or pointer");
    auto &entry=host.entry(id);
    require(!dynamic_cast<EnvironmentTextureNode *>(entry.node),"Unsupported feature: UDIM tiles bind to Image Texture nodes only, as in Cycles",CBC_UNSUPPORTED);
    auto node=dynamic_cast<ImageTextureNode *>(entry.node);
    require(node&&entry.graph,"UDIM tiles bind to an Image Texture node of a shader graph");
    ImageBinding binding;std::set<uint32_t> seen;
    for(uint64_t i=0;i<count;++i) {
      const std::string tile=std::to_string(tiles[i]);
      require(tiles[i]>=1001&&tiles[i]<=uint32_t(INT_MAX),("UDIM tile number "+tile+" is outside 1001 to 2147483647 (the tile covering "
              "UV [u, u + 1) x [v, v + 1) is 1001 + 10 * v + u, u 0 to 9)").c_str());
      require(seen.insert(tiles[i]).second,("UDIM tile "+tile+" is listed twice").c_str());
      auto it=host.images.find(images[i]);require(it!=host.images.end(),("Invalid, released or foreign image handle for UDIM tile "+tile).c_str());
      require(binding.empty()||it->second->type==binding.front().second->type,("The image of UDIM tile "+tile+" has another format than the "
              "image of tile "+std::to_string(binding.empty()?0:binding.front().first)+"; every tile needs the same format (Cycles decodes "
              "every tile with the first tile's settings)").c_str());
      // Tiled images (PROVISIONAL): Cycles asks for ray derivatives, which select a tiled image's mip level, from the
      // first tile's metadata only (ImageTextureNode::update_images), so a binding is all tiled images or none.
      require(binding.empty()||bool(it->second->tiled)==bool(binding.front().second->tiled),("The image of UDIM tile "+tile+" is "+
              (it->second->tiled?"a tiled image":"not a tiled image")+" and the image of tile "+std::to_string(binding.empty()?0:binding.front().first)+
              (binding.empty()||binding.front().second->tiled?" is":" is not")+"; the tiles of a binding are all tiled images or none (Cycles "
              "selects mip levels by the first tile)").c_str());
      binding.emplace_back(int(tiles[i]),it->second);
    }
    entry.graph->images[node]=std::move(binding);node->handle.clear();entry.graph->dirty=true;
  });
}
// Tiled images (PROVISIONAL, ABI.md "Tiled images"): Cycles' texture cache, fed by the caller's read callback
// (TiledImageLoader, host_scene_types.inc). Validated as cbc_host_image_create; the handle names a TiledImageSource that
// owns user_data and runs release once, when the last of the handle, the bindings and Cycles' image lets go of it. When
// the call fails, release runs before it returns (also when the call is refused, from inside a callback).
// The tile sizes of a tiled image (CBCImageConfig::tile_size, PROVISIONAL). Cycles' texture cache takes square tiles whose
// side is a power of two (ImageCache::load_image_tiled asserts it, and the kernel maps texels to tiles by shifts,
// kernel_image_tile_map in kernel/util/image_2d.h) of at least DebugFlags().texture_cache.min_tile_size, 64: a file's
// smaller tiles are loaded in blocks of 64 (ImageMetaData::detect_tiles, util/image_metadata.cpp), so Cycles never uses a
// smaller one. The largest is the device's. The tiles of one format and interpolation share pool images of 64 tiles of
// (size + 4) x (size + 4) pixels in a row (ImageCache::alloc_tile, scene/image_cache.cpp: a border of
// KERNEL_IMAGE_TEX_PADDING on each side), allocated whole at the first tile load. Cycles' Metal device refuses an image
// wider than 16384 (MetalDevice::image_alloc), so 128 there. On the CPU device the bound is the memory of the pool: Cycles
// allocates each pool whole at its first tile load and reads and stages every tile whole, so the pool of 64 tiles of 1024
// takes 271 MB for byte4 and 1.1 GB for float4, that of 2048 four times as much, and that of 65536 1.1 TB of address space
// for byte4 (which Linux and Windows would refuse, ending the process through Cycles' unhandled std::bad_alloc from
// device_memory::host_alloc). So 1024 there (and on the WebGPU backend, whose pool lives in the device arena), the
// library's cap: Cycles' CPU kernel would address tiles up to 65536 (it finds a texel of the pool by a float pixel
// coordinate, kernel_image_tile_map, which ImageInterpolator floors, kernel/util/image.h, and binary32 holds every texel
// centre only up to 2^23, a pool of 64 * 65540 pixels), and its int arithmetic up to 2^24 (ImageCache::alloc_tile). The
// largest size per backend is also the capability report's backendScope.<backend>.maxImageTileSize (PROVISIONAL).
constexpr uint32_t tiled_image_default_tile_size=64;
uint32_t tiled_image_largest_tile_size(uint32_t backend){return backend==CBC_HOST_METAL?128u:1024u;}
uint32_t tiled_image_tile_size(uint32_t backend,uint32_t requested) {
  const uint32_t largest=tiled_image_largest_tile_size(backend);
  if(requested==0)return tiled_image_default_tile_size;
  require(requested>=tiled_image_default_tile_size&&requested<=largest&&(requested&(requested-1))==0,
          ("Invalid tiled image tile size "+std::to_string(requested)+": 0 (the default, 64) or a power of two from 64 to "+std::to_string(largest)+
           (backend==CBC_HOST_METAL?" on the Metal backend (Cycles' Metal tile pool of 64 tiles of size + 4 pixels may be at most 16384 wide)"
                                    :" (Cycles' texture cache takes square power-of-two tiles of at least 64 and allocates a pool of 64 tiles whole: "
                                     "1.1 GB for float4 tiles of 1024)")).c_str());
  return requested;
}
extern "C" int cbc_host_image_create_tiled(CBCHost handle,const CBCImageConfig *config,CBCImageTileReadCallback read,
                                           CBCReleaseCallback release,void *user_data,CBCImage *image) {
  if(image)*image=0;
  bool owned=false;
  const int status=edit(handle,[&](Host &host) {
    require(image&&read,"A tiled image needs a read callback and an image handle pointer");
    require(config&&config->struct_size==sizeof(*config),"Invalid image configuration");
    require(std::all_of(std::begin(config->reserved),std::end(config->reserved),[](auto x){return x==0;}),"Unknown image options");
    require(config->width&&config->height&&config->width<=INT_MAX&&config->height<=INT_MAX,"Invalid image dimensions");
    ImageDataType type;unsigned channels,component_bytes;
    switch(config->format) {
      case CBC_IMAGE_FLOAT:type=IMAGE_DATA_TYPE_FLOAT;channels=1;component_bytes=4;break;
      case CBC_IMAGE_FLOAT4:type=IMAGE_DATA_TYPE_FLOAT4;channels=4;component_bytes=4;break;
      case CBC_IMAGE_HALF:type=IMAGE_DATA_TYPE_HALF;channels=1;component_bytes=2;break;
      case CBC_IMAGE_HALF4:type=IMAGE_DATA_TYPE_HALF4;channels=4;component_bytes=2;break;
      case CBC_IMAGE_BYTE:type=IMAGE_DATA_TYPE_BYTE;channels=1;component_bytes=1;break;
      case CBC_IMAGE_BYTE4:type=IMAGE_DATA_TYPE_BYTE4;channels=4;component_bytes=1;break;
      case CBC_IMAGE_USHORT:type=IMAGE_DATA_TYPE_USHORT;channels=1;component_bytes=2;break;
      case CBC_IMAGE_USHORT4:type=IMAGE_DATA_TYPE_USHORT4;channels=4;component_bytes=2;break;
      default:throw Failure(CBC_INVALID,"Unknown image format");
    }
    const uint32_t tile_size=tiled_image_tile_size(host.backend,config->tile_size);
    auto data=std::make_shared<MemoryImageData>();data->identity=new_id();data->width=config->width;data->height=config->height;
    data->type=type;data->channels=channels;
    auto source=std::make_shared<TiledImageSource>(host.tiled_reads);
    source->identity=data->identity;source->width=config->width;source->height=config->height;source->type=type;
    source->channels=channels;source->pixel_bytes=channels*component_bytes;source->tile_size=tile_size;
    source->read=read;source->user_data=user_data;
    data->tiled=std::move(source);
    host.images.emplace(data->identity,data);
    // From here the source owns user_data: its destructor runs release.
    data->tiled->release=release;owned=true;
    *image=data->identity;
  });
  if(!owned&&release)caller_callback("a tiled image's release callback",[&]{release(user_data);return 0;});
  return status;
}
extern "C" int cbc_host_image_release(CBCHost handle,CBCImage image) {
  return edit(handle,[&](Host &host){require(host.images.erase(image)==1,"Invalid or released image handle");});
}
