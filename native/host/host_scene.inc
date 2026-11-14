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
    default:throw Failure(CBC_INVALID,"Unsupported attribute value type");
  }
}
unsigned attribute_components(const TypeDesc &type,AttributeElement element) {
  if(element&ATTR_ELEMENT_IS_BYTE)return 4;
  if(type==TypeFloat)return 1;
  if(type==TypeFloat2)return 2;
  if(type==TypeFloat4||type==TypeRGBA)return 4;
  if(type==TypeMatrix)return 12;
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
  if(key=="tangent" && mesh)return ATTR_STD_UV_TANGENT;
  if(key=="tangent_sign" && mesh)return ATTR_STD_UV_TANGENT_SIGN;
  // The Geometry node's Pointiness and Random Per Island outputs: values the caller computes, as Cycles reads them.
  if(key=="pointiness" && mesh)return ATTR_STD_POINTINESS;
  if(key=="random_per_island" && mesh)return ATTR_STD_RANDOM_PER_ISLAND;
  throw Failure(CBC_UNSUPPORTED,"Standard attribute is not exposed for this geometry");
}
struct AttributeTarget {Geometry &geom;AttributeSet &set;AttributeElement element;TypeDesc type;AttributeStandard standard;uint64_t count;};
AttributeTarget attribute_target(Host &host,CBCNode id,const char *name,const CBCAttributeConfig *config,uint64_t count) {
  require(name&&*name&&config&&config->struct_size==sizeof(*config),"Invalid attribute configuration");
  require(std::all_of(std::begin(config->reserved),std::end(config->reserved),[](auto x){return x==0;}),"Unknown attribute options");
  auto &geom=geometry(host,id);auto &set=attribute_set(geom,config->flags);
  auto element=attribute_element(geom,config->domain);auto type=attribute_type(config->value_type);
  require(!(element&ATTR_ELEMENT_IS_NORMAL)||config->value_type==CBC_VALUE_NORMAL,"Normal storage requires a normal value");
  require(bool(element&ATTR_ELEMENT_IS_BYTE)==(config->value_type==CBC_VALUE_BYTE4),"Byte storage requires a byte corner domain");
  auto standard=config->flags&CBC_ATTR_STANDARD?standard_attribute(geom,name,element):ATTR_STD_NONE;
  auto existing=set.find(ustring(name));require(!existing||existing->std==ATTR_STD_NONE||standard!=ATTR_STD_NONE,"Use standard mode to replace a standard attribute");
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
// Requests the pinned Cycles accepts but this library cannot honour: refused by name, never ignored (on every backend
// unless the test names one).
void validate_unavailable(Host &host) {
  // Dispersion renders on the CPU device and on Cycles' Metal device. The WebGPU device does not implement it and is
  // refused here by the device's own words (pipelines.cpp refuses a referenced shader flagged SD_REQUIRES_WAVELENGTH with
  // the same text). The test is the pinned node's own (PrincipledBsdfNode::has_dispersion): the dispersion scale and
  // the transmission weight are each linked or not zero; with a constant zero transmission weight nothing disperses
  // and the scene renders, as in Cycles.
  if(host.backend==CBC_HOST_WEBGPU)
    for(auto &[shader,stored]:host.graphs)for(auto node:stored->authoring->nodes)if(auto principled=dynamic_cast<PrincipledBsdfNode *>(node))
      require(!principled->has_dispersion(),"WebGPU dispersion not implemented",CBC_UNSUPPORTED);
  // Gaussian splats: a point cloud asked to render as splats needs the radiance, scale and rotation attributes, which this
  // host cannot author; the pinned Cycles would render it as plain points with no message (PointCloud::primitive_type).
  for(auto geom:host.session->scene->geometry)if(auto points=dynamic_cast<PointCloud *>(geom))
    require(points->get_render_as()!=PointCloud::RENDER_AS_GSPLATS,
            "Unsupported feature: Gaussian splats are not available in this library (the point cloud input render_as must be points)",CBC_UNSUPPORTED);
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
}
extern "C" int cbc_host_attribute_floats(CBCHost handle,CBCNode id,const char *name,const CBCAttributeConfig *config,const float *values,uint64_t count) {
  return edit(handle,[&](Host &host) {
    count_ok(values,count);for(size_t i=0;i<count;++i)require(std::isfinite(values[i]),"Attribute contains a nonfinite value");
    before_edit(host,host.entry(id),nullptr);auto target=attribute_target(host,id,name,config,count);require(!(target.element&ATTR_ELEMENT_IS_BYTE),"Use byte attribute input");
    AttributeSet staged(&target.geom,target.set.prim);auto value=stage_attribute(staged,target,name);
    const unsigned components=attribute_components(target.type,target.element);
    for(size_t i=0;i<target.count;++i) {
      const float *v=values+i*components;
      if(target.element&ATTR_ELEMENT_IS_NORMAL)value->data_for_write<packed_normal>()[i]=packed_normal(make_float3(v[0],v[1],v[2]));
      else if(target.type==TypeFloat)value->data_for_write<float>()[i]=v[0];
      else if(target.type==TypeFloat2)value->data_for_write<float2>()[i]=make_float2(v[0],v[1]);
      else if(target.type==TypeFloat4||target.type==TypeRGBA)value->data_for_write<float4>()[i]=make_float4(v[0],v[1],v[2],v[3]);
      else if(target.type==TypeMatrix)value->data_for_write<Transform>()[i]=read_transform(v);
      else value->data_for_write<packed_float3>()[i]=make_float3(v[0],v[1],v[2]);
    }
    target.set.add_from(std::move(*value));target.geom.tag_update(host.session->scene.get(),true);
  });
}
extern "C" int cbc_host_attribute_bytes(CBCHost handle,CBCNode id,const char *name,const CBCAttributeConfig *config,const uint8_t *values,uint64_t count) {
  return edit(handle,[&](Host &host) {
    count_ok(values,count);before_edit(host,host.entry(id),nullptr);auto target=attribute_target(host,id,name,config,count);require(target.element&ATTR_ELEMENT_IS_BYTE,"Expected byte corner colors");
    AttributeSet staged(&target.geom,target.set.prim);auto value=stage_attribute(staged,target,name);
    if(count)std::memcpy(value->data_for_write<uchar4>(),values,size_t(count));
    target.set.add_from(std::move(*value));target.geom.tag_update(host.session->scene.get(),true);
  });
}
// Reads return authored values: a geometry baked by the static BVH path
// answers from the attributes captured before the bake (host_edits.inc).
Attribute &authored_attribute(Host &host,CBCNode id,const char *name,uint32_t flags) {
  auto &attr=find_attribute(host,id,name,flags);auto &geom=geometry(host,id);
  auto record=host.bakes.find(&geom);
  if((flags&CBC_ATTR_SUBDIVISION)||!geom.transform_applied||record==host.bakes.end())return attr;
  auto authored=record->second.attributes->find(ustring(name));require(authored,"Unknown attribute");return *authored;
}
extern "C" int cbc_host_attribute_get_floats(CBCHost handle,CBCNode id,const char *name,uint32_t flags,float *values,uint64_t capacity,uint64_t *required) {
  return edit(handle,[&](Host &host) {
    auto &attr=authored_attribute(host,id,name,flags);require(!(attr.element&(ATTR_ELEMENT_IS_BYTE|ATTR_ELEMENT_VOXEL)),"Attribute does not contain float input");
    std::vector<float> result;result.reserve(size_t(attr.size)*attribute_components(attr.type,attr.element));
    for(int i=0;i<attr.size;++i) {
      if(attr.element&ATTR_ELEMENT_IS_NORMAL){auto v=attr.data<packed_normal>()[i].decode();result.insert(result.end(),{v.x,v.y,v.z});}
      else if(attr.type==TypeFloat)result.push_back(attr.data<float>()[i]);
      else if(attr.type==TypeFloat2){auto v=attr.data<float2>()[i];result.insert(result.end(),{v.x,v.y});}
      else if(attr.type==TypeFloat4||attr.type==TypeRGBA){auto v=attr.data<float4>()[i];result.insert(result.end(),{v.x,v.y,v.z,v.w});}
      else if(attr.type==TypeMatrix){auto v=attr.data<Transform>()[i];result.insert(result.end(),{v.x.x,v.x.y,v.x.z,v.x.w,v.y.x,v.y.y,v.y.z,v.y.w,v.z.x,v.z.y,v.z.z,v.z.w});}
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
    before_edit(host,host.entry(id),nullptr);auto &attr=find_attribute(host,id,name,flags);require(attr.std!=ATTR_STD_POSITION&&attr.std!=ATTR_STD_RADIUS,"Cannot remove required geometry storage");
    auto &geom=geometry(host,id);attribute_set(geom,flags).remove(&attr);geom.tag_update(host.session->scene.get(),true);
  });
}
// Validates and copies caller pixels; shared by image creation and update.
static std::shared_ptr<MemoryImageData> image_data(const CBCImageConfig *config,const void *pixels,uint64_t bytes) {
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
    if(image){auto it=host.images.find(image);require(it!=host.images.end(),"Invalid or foreign image handle");entry.graph->images[node]=it->second;}
    else entry.graph->images.erase(node);
    node->handle.clear();entry.graph->dirty=true;
  });
}
extern "C" int cbc_host_image_release(CBCHost handle,CBCImage image) {
  return edit(handle,[&](Host &host){require(host.images.erase(image)==1,"Invalid or released image handle");});
}
