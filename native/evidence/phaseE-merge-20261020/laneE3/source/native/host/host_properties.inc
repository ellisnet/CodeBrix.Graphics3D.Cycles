// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Included in the implementation TU to keep opaque native ownership internal.
namespace {
bool float3_array(SocketType::Type type) {
  return type == SocketType::COLOR_ARRAY || type == SocketType::VECTOR_ARRAY ||
         type == SocketType::POINT_ARRAY || type == SocketType::NORMAL_ARRAY;
}
void stored_property(const SocketType &input) {
  require(input.struct_offset >= 0 && input.type != SocketType::CLOSURE && input.type != SocketType::UNDEFINED,
          "Socket has no accessible stored value", CBC_UNSUPPORTED);
}
Transform read_transform(const float *values) {
  Transform value;
  value.x=make_float4(values[0],values[1],values[2],values[3]);
  value.y=make_float4(values[4],values[5],values[6],values[7]);
  value.z=make_float4(values[8],values[9],values[10],values[11]); return value;
}
template<class T> void copy_result(const std::vector<T> &source,T *target,uint64_t capacity,uint64_t *required) {
  require(required && (!capacity || target),"Invalid result buffer or size pointer");
  *required=source.size(); require(capacity>=source.size(),"Result buffer is too small",CBC_SMALL_BUFFER);
  for (size_t i=0;i<source.size();++i) target[i]=source[i];
}
void copy_text(const std::string &source,char *target,uint64_t capacity,uint64_t *required) {
  require(required && (!capacity || target),"Invalid text buffer or size pointer");
  *required=source.size()+1;require(capacity>=*required,"Text buffer is too small",CBC_SMALL_BUFFER);
  std::memcpy(target,source.c_str(),size_t(*required));
}
uint32_t value_type(SocketType::Type type) {
  switch(type) {
    case SocketType::BOOLEAN:case SocketType::BOOLEAN_ARRAY:return CBC_VALUE_BOOLEAN;
    case SocketType::INT:case SocketType::INT_ARRAY:return CBC_VALUE_INT;
    case SocketType::UINT:return CBC_VALUE_UINT;
    case SocketType::UINT64:return CBC_VALUE_UINT64;
    case SocketType::FLOAT:case SocketType::FLOAT_ARRAY:return CBC_VALUE_FLOAT;
    case SocketType::COLOR:case SocketType::COLOR_ARRAY:return CBC_VALUE_COLOR;
    case SocketType::VECTOR:case SocketType::VECTOR_ARRAY:return CBC_VALUE_VECTOR;
    case SocketType::POINT:case SocketType::POINT_ARRAY:return CBC_VALUE_POINT;
    case SocketType::NORMAL:case SocketType::NORMAL_ARRAY:return CBC_VALUE_NORMAL;
    case SocketType::POINT2:case SocketType::POINT2_ARRAY:return CBC_VALUE_POINT2;
    case SocketType::STRING:case SocketType::STRING_ARRAY:return CBC_VALUE_STRING;
    case SocketType::ENUM:return CBC_VALUE_ENUM;
    case SocketType::TRANSFORM:case SocketType::TRANSFORM_ARRAY:return CBC_VALUE_TRANSFORM;
    case SocketType::NODE:case SocketType::NODE_ARRAY:return CBC_VALUE_NODE;
    case SocketType::CLOSURE:return CBC_VALUE_CLOSURE;
    default:return CBC_VALUE_UNSUPPORTED;
  }
}
uint32_t components(SocketType::Type type) {
  const auto kind=value_type(type);
  if (kind==CBC_VALUE_TRANSFORM) return 12;
  if (kind>=CBC_VALUE_COLOR && kind<=CBC_VALUE_NORMAL) return 3;
  if (kind==CBC_VALUE_POINT2) return 2;
  if (kind==CBC_VALUE_UNSUPPORTED || kind==CBC_VALUE_CLOSURE) return 0;
  return 1;
}
uint32_t property_flags(const SocketType &input) {
  uint32_t flags=input.is_array()?CBC_PROPERTY_ARRAY:0;
  if (input.flags&SocketType::LINKABLE) flags|=CBC_PROPERTY_LINKABLE;
  if (input.flags&SocketType::INTERNAL) flags|=CBC_PROPERTY_INTERNAL;
  if (input.struct_offset>=0 && components(input.type)) flags|=CBC_PROPERTY_READABLE|CBC_PROPERTY_WRITABLE;
  return flags;
}
uint64_t element_count(const Node &node,const SocketType &input) {
  switch(input.type) {
    case SocketType::BOOLEAN_ARRAY:return node.get_bool_array(input).size();
    case SocketType::INT_ARRAY:return node.get_int_array(input).size();
    case SocketType::FLOAT_ARRAY:return node.get_float_array(input).size();
    case SocketType::POINT2_ARRAY:return node.get_float2_array(input).size();
    case SocketType::COLOR_ARRAY:case SocketType::VECTOR_ARRAY:case SocketType::POINT_ARRAY:case SocketType::NORMAL_ARRAY:return node.get_float3_array(input).size();
    case SocketType::STRING_ARRAY:return node.get_string_array(input).size();
    case SocketType::TRANSFORM_ARRAY:return node.get_transform_array(input).size();
    case SocketType::NODE_ARRAY:return node.get_node_array(input).size();
    default:return components(input.type)?1:0;
  }
}
std::vector<float> float_values(const Node &node,const SocketType &input) {
  std::vector<float> result;
  auto pair=[&](auto v){result.insert(result.end(),{v.x,v.y});};
  auto triple=[&](auto v){result.insert(result.end(),{v.x,v.y,v.z});};
  auto transform=[&](const Transform &v){result.insert(result.end(),{v.x.x,v.x.y,v.x.z,v.x.w,v.y.x,v.y.y,v.y.z,v.y.w,v.z.x,v.z.y,v.z.z,v.z.w});};
  switch(input.type) {
    case SocketType::FLOAT:result.push_back(node.get_float(input));break;
    case SocketType::POINT2:pair(node.get_float2(input));break;
    case SocketType::COLOR:case SocketType::VECTOR:case SocketType::POINT:case SocketType::NORMAL:triple(node.get_float3(input));break;
    case SocketType::TRANSFORM:transform(node.get_transform(input));break;
    case SocketType::FLOAT_ARRAY:for(auto value:node.get_float_array(input))result.push_back(value);break;
    case SocketType::POINT2_ARRAY:for(auto value:node.get_float2_array(input))pair(value);break;
    case SocketType::COLOR_ARRAY:case SocketType::VECTOR_ARRAY:case SocketType::POINT_ARRAY:case SocketType::NORMAL_ARRAY:for(auto value:node.get_float3_array(input))triple(value);break;
    case SocketType::TRANSFORM_ARRAY:for(auto value:node.get_transform_array(input))transform(value);break;
    default:throw Failure(CBC_INVALID,"Socket does not contain floating-point values");
  }
  return result;
}
void json_text(std::ostream &out,const char *value) {
  out<<'"';
  for(auto p=reinterpret_cast<const unsigned char *>(value);*p;++p) {
    if(*p=='"'||*p=='\\')out<<'\\'<<char(*p);
    else if(*p<32)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(*p)<<std::dec;
    else out<<char(*p);
  }
  out<<'"';
}
}
extern "C" int cbc_host_property_info(CBCHost handle,CBCNode id,const char *name,CBCPropertyInfo *info) {
  return edit(handle,[&](Host &host) {
    require(info && info->struct_size==sizeof(*info),"Invalid property information structure");
    auto &entry=host.entry(id);auto &input=socket(entry,name);
    CBCPropertyInfo result{};result.struct_size=sizeof(result);result.value_type=value_type(input.type);
    result.flags=property_flags(input);result.components=components(input.type);
    result.count=(result.flags&CBC_PROPERTY_READABLE)?element_count(*entry.node,input):0;*info=result;
  });
}
extern "C" int cbc_host_get_floats(CBCHost handle,CBCNode id,const char *name,float *values,uint64_t capacity,uint64_t *required) {
  return edit(handle,[&](Host &host) {auto &entry=host.entry(id);auto &input=socket(entry,name);stored_property(input);copy_result(float_values(*entry.node,input),values,capacity,required);});
}
extern "C" int cbc_host_get_ints(CBCHost handle,CBCNode id,const char *name,int64_t *values,uint64_t capacity,uint64_t *required) {
  return edit(handle,[&](Host &host) {
    auto &entry=host.entry(id);auto &input=socket(entry,name);stored_property(input);auto &node=*entry.node;std::vector<int64_t> result;
    switch(input.type) {
      case SocketType::BOOLEAN:result.push_back(node.get_bool(input));break;
      case SocketType::INT:case SocketType::ENUM:result.push_back(node.get_int(input));break;
      case SocketType::UINT:result.push_back(node.get_uint(input));break;
      case SocketType::UINT64:{auto value=node.get_uint64(input);require(value<=INT64_MAX,"Unsigned value exceeds signed result range");result.push_back(int64_t(value));break;}
      case SocketType::BOOLEAN_ARRAY:for(auto value:node.get_bool_array(input))result.push_back(value);break;
      case SocketType::INT_ARRAY:for(auto value:node.get_int_array(input))result.push_back(value);break;
      default:throw Failure(CBC_INVALID,"Socket does not contain integer values");
    }
    copy_result(result,values,capacity,required);
  });
}
extern "C" int cbc_host_set_uints(CBCHost handle,CBCNode id,const char *name,const uint64_t *values,uint64_t count) {
  return edit(handle,[&](Host &host) {
    count_ok(values,count);require(count==1,"Expected one unsigned integer");auto &entry=host.entry(id);auto &input=socket(entry,name);stored_property(input);before_edit(host,entry,name);
    if(input.type==SocketType::UINT64)entry.node->set(input,values[0]);
    else if(input.type==SocketType::UINT){require(values[0]<=UINT_MAX,"Unsigned socket out of range");entry.node->set(input,uint(values[0]));}
    else throw Failure(CBC_INVALID,"Socket is not an unsigned integer");
    host.changed(entry);
  });
}
extern "C" int cbc_host_get_uints(CBCHost handle,CBCNode id,const char *name,uint64_t *values,uint64_t capacity,uint64_t *required) {
  return edit(handle,[&](Host &host) {
    auto &entry=host.entry(id);auto &input=socket(entry,name);stored_property(input);std::vector<uint64_t> result;
    if(input.type==SocketType::UINT64)result.push_back(entry.node->get_uint64(input));
    else if(input.type==SocketType::UINT)result.push_back(entry.node->get_uint(input));
    else throw Failure(CBC_INVALID,"Socket is not an unsigned integer");
    copy_result(result,values,capacity,required);
  });
}
extern "C" int cbc_host_set_strings(CBCHost handle,CBCNode id,const char *name,const char *const *values,uint64_t count) {
  return edit(handle,[&](Host &host) {
    count_ok(values,count);auto &entry=host.entry(id);auto &input=socket(entry,name);stored_property(input);before_edit(host,entry,name);
    require(input.type==SocketType::STRING_ARRAY,"Expected a string array socket");
    for(size_t i=0;i<count;++i)require(values[i],"Null string array element");
    array<ustring> copy;copy.resize(size_t(count));for(size_t i=0;i<count;++i)copy[i]=*values[i]?ustring(values[i]):ustring();entry.node->set(input,copy);host.changed(entry);
  });
}
extern "C" int cbc_host_get_string(CBCHost handle,CBCNode id,const char *name,uint64_t index,char *value,uint64_t capacity,uint64_t *required) {
  return edit(handle,[&](Host &host) {
    auto &entry=host.entry(id);auto &input=socket(entry,name);stored_property(input);ustring result;
    if(input.type==SocketType::STRING_ARRAY){auto &items=entry.node->get_string_array(input);require(index<items.size(),"String index out of range");result=items[size_t(index)];}
    else {require((input.type==SocketType::STRING||input.type==SocketType::ENUM)&&index==0,"Expected a string or enumeration at index zero");result=entry.node->get_string(input);}
    copy_text(result.string(),value,capacity,required);
  });
}
extern "C" int cbc_host_get_nodes(CBCHost handle,CBCNode id,const char *name,CBCNode *values,uint64_t capacity,uint64_t *required) {
  return edit(handle,[&](Host &host) {
    auto &entry=host.entry(id);auto &input=socket(entry,name);stored_property(input);std::vector<Node *> nodes;
    if(input.type==SocketType::NODE)nodes.push_back(entry.node->get_node(input));
    else if(input.type==SocketType::NODE_ARRAY)for(auto node:entry.node->get_node_array(input))nodes.push_back(node);
    else throw Failure(CBC_INVALID,"Socket does not contain node references");
    require(required&&(!capacity||values),"Invalid result buffer or size pointer");*required=nodes.size();require(capacity>=nodes.size(),"Result buffer is too small",CBC_SMALL_BUFFER);
    std::vector<CBCNode> result;for(auto node:nodes)result.push_back(node?host.remember(node):0);copy_result(result,values,capacity,required);
  });
}
extern "C" int cbc_host_node_type(CBCHost handle,CBCNode id,char *name,uint64_t capacity,uint64_t *required) {
  return edit(handle,[&](Host &host){copy_text(host.entry(id).node->type->name.string(),name,capacity,required);});
}
extern "C" int cbc_host_registry_json(char *json,uint64_t capacity,uint64_t *required) {
  return boundary([&] {
    auto names=NodeType::type_names();std::sort(names.begin(),names.end(),[](auto a,auto b){return a.string()<b.string();});
    std::ostringstream out;out.imbue(std::locale::classic());out<<"{\"abiVersion\":"<<CBC_HOST_ABI<<",\"nodeTypes\":[";bool first=true;
    for(auto name:names) {
      auto type=NodeType::find(name);require(type,"Missing registered type",CBC_FAILED);
      if(!first)out<<',';
      first=false;out<<"{\"name\":";json_text(out,name.c_str());out<<",\"kind\":"<<int(type->type)<<",\"base\":";
      if(type->base)json_text(out,type->base->name.c_str());else out<<"null";
      out<<",\"hasFactory\":"<<(type->create?"true":"false");
      auto sockets=[&](const auto &items,bool inputs) {
        out<<(inputs?",\"inputs\":[":",\"outputs\":[");bool first_socket=true;
        for(const auto &input:items) {
          if(!first_socket)out<<',';
          first_socket=false;out<<"{\"name\":";json_text(out,input.name.c_str());out<<",\"uiName\":";json_text(out,input.ui_name.c_str());
          out<<",\"type\":";json_text(out,SocketType::type_name(input.type).c_str());
          out<<",\"valueType\":"<<value_type(input.type)<<",\"components\":"<<components(input.type)<<",\"flags\":"<<(inputs?property_flags(input):0)<<",\"nodeType\":";
          if(input.node_type)json_text(out,input.node_type->name.c_str());else out<<"null";
          out<<",\"enumValues\":[";std::map<std::string,int> enums;
          if(input.enum_values)for(const auto &[key,value]:*input.enum_values)enums.emplace(key.c_str(),value);
          bool first_enum=true;for(const auto &[key,value]:enums){if(!first_enum)out<<',';first_enum=false;out<<"{\"name\":";json_text(out,key.c_str());out<<",\"value\":"<<value<<'}';}
          out<<"]}";
        }
        out<<']';
      };
      sockets(type->inputs,true);sockets(type->outputs,false);out<<'}';
    }
    out<<"]}";copy_text(out.str(),json,capacity,required);
  });
}
