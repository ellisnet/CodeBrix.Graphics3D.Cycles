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
  if(key=="tangent" && mesh)return ATTR_STD_UV_TANGENT;
  if(key=="tangent_sign" && mesh)return ATTR_STD_UV_TANGENT_SIGN;
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
  require(name&&*name,"Attribute name is required");auto &set=attribute_set(geometry(host,id),flags);
  auto value=set.find(ustring(name));require(value,"Unknown attribute");
  require(!(flags&CBC_ATTR_STANDARD)||value->std!=ATTR_STD_NONE,"Attribute is not standard");return *value;
}
void validate_geometry(Host &host) {
  auto &scene=*host.session->scene;
  for(auto object:scene.objects)require(object->get_geometry(),"Object has no geometry");
  for(auto geom:scene.geometry) {
    if(auto mesh=dynamic_cast<Mesh *>(geom)) {
      const auto &triangles=mesh->get_triangles();require(triangles.size()%3==0,"Incomplete triangle index array");
      for(auto index:triangles)require(index>=0 && size_t(index)<mesh->num_verts(),"Triangle index is outside the position array");
      const auto faces=triangles.size()/3;
      require(mesh->get_shader().empty()||mesh->get_shader().size()>=faces,"Triangle shader array is too short");
      require(mesh->get_smooth().empty()||mesh->get_smooth().size()>=faces,"Triangle smoothing array is too short");
    } else if(auto hair=dynamic_cast<Hair *>(geom)) {
      const auto &first=hair->get_curve_first_key();require(hair->get_curve_shader().size()>=first.size(),"Curve shader array is too short");
      if(!first.empty())require(first[0]==0,"First curve must start at key zero");
      for(size_t i=0;i<first.size();++i){const size_t end=i+1<first.size()?size_t(first[i+1]):hair->num_keys();require(first[i]>=0&&size_t(first[i])<end&&end<=hair->num_keys(),"Invalid curve key range");}
    }
    for(const auto &attr:geom->attributes.attributes) {
      require(size_t(attr.size)==Attribute::element_size(geom,attr.element,geom->attributes.prim),"Attribute no longer matches geometry topology");
    }
  }
}
}
extern "C" int cbc_host_attribute_floats(CBCHost handle,CBCNode id,const char *name,const CBCAttributeConfig *config,const float *values,uint64_t count) {
  return edit(handle,[&](Host &host) {
    count_ok(values,count);for(size_t i=0;i<count;++i)require(std::isfinite(values[i]),"Attribute contains a nonfinite value");
    auto target=attribute_target(host,id,name,config,count);require(!(target.element&ATTR_ELEMENT_IS_BYTE),"Use byte attribute input");
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
    count_ok(values,count);auto target=attribute_target(host,id,name,config,count);require(target.element&ATTR_ELEMENT_IS_BYTE,"Expected byte corner colors");
    AttributeSet staged(&target.geom,target.set.prim);auto value=stage_attribute(staged,target,name);
    if(count)std::memcpy(value->data_for_write<uchar4>(),values,size_t(count));
    target.set.add_from(std::move(*value));target.geom.tag_update(host.session->scene.get(),true);
  });
}
extern "C" int cbc_host_attribute_get_floats(CBCHost handle,CBCNode id,const char *name,uint32_t flags,float *values,uint64_t capacity,uint64_t *required) {
  return edit(handle,[&](Host &host) {
    auto &attr=find_attribute(host,id,name,flags);require(!(attr.element&(ATTR_ELEMENT_IS_BYTE|ATTR_ELEMENT_VOXEL)),"Attribute does not contain float input");
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
    auto &attr=find_attribute(host,id,name,flags);require(attr.element&ATTR_ELEMENT_IS_BYTE,"Attribute is not byte storage");
    const size_t count=size_t(attr.size)*4;std::vector<uint8_t> result(count);
    if(count)std::memcpy(result.data(),attr.data<uchar4>(),count);
    copy_result(result,values,capacity,required);
  });
}
extern "C" int cbc_host_attribute_remove(CBCHost handle,CBCNode id,const char *name,uint32_t flags) {
  return edit(handle,[&](Host &host) {
    auto &attr=find_attribute(host,id,name,flags);require(attr.std!=ATTR_STD_POSITION&&attr.std!=ATTR_STD_RADIUS,"Cannot remove required geometry storage");
    auto &geom=geometry(host,id);attribute_set(geom,flags).remove(&attr);geom.tag_update(host.session->scene.get(),true);
  });
}
extern "C" int cbc_host_image_create(CBCHost handle,const CBCImageConfig *config,const void *pixels,uint64_t bytes,CBCImage *image) {
  if(image)*image=0;
  return edit(handle,[&](Host &host) {
    require(image&&config&&config->struct_size==sizeof(*config),"Invalid image configuration");
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
    copy->pixels.assign(data,data+size_t(bytes));host.images.emplace(copy->identity,copy);*image=copy->identity;
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
