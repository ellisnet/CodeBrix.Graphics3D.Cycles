// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "scene_data.h"
#include "webgpu_device.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <functional>
#include <set>
namespace cbc {
namespace {
void require(bool ok,const char *message){if(!ok)throw Error(CBC_BACKEND_INVALID,message);}
void supported(bool ok,const char *message){if(!ok)throw Error(CBC_BACKEND_UNSUPPORTED,message);}
struct Table {
  std::vector<uint32_t> &words;
  size_t rows()const{return words.size()/4;}
  uint32_t u(size_t row,size_t c)const{return words.at(row*4+c);}
  float f(size_t row,size_t c)const{return std::bit_cast<float>(u(row,c));}
  int32_t i(size_t row,size_t c)const{return std::bit_cast<int32_t>(u(row,c));}
  uint32_t number(size_t row,size_t c)const{float v=f(row,c);require(std::isfinite(v)&&v>=0&&v<=4194304&&v==std::floor(v),"Invalid integer-valued metadata");return uint32_t(v);}
  void finite(size_t first,size_t last)const{for(size_t r=first;r<last;r++)for(unsigned c=0;c<4;c++)require(std::isfinite(f(r,c)),"Nonfinite scene data");}
};
std::vector<uint32_t> copy(const void *p,size_t bytes){
  require(p&&bytes>=16&&bytes%16==0&&bytes<=64*1024*1024,"Invalid scene buffer size");
  std::vector<uint32_t> out(bytes/4);std::memcpy(out.data(),p,bytes);return out;
}
void svm(const Table &t,size_t base,size_t count,uint32_t shader){
  require(shader<count/4,"Shader index outside SVM table");
  size_t offset=shader*4;std::set<size_t> visited;std::array<bool,255> initialized{};unsigned closures=0;
  auto word=[&](size_t i){require(i<count,"Truncated SVM instruction");return t.words[base+i];};
  auto scalar=[&](uint32_t bits){
    if((bits>>8)==0x7fc000){unsigned s=bits&255;require(s<255&&initialized[s],"Uninitialized SVM stack input");}
    else require(std::isfinite(std::bit_cast<float>(bits)),"Nonfinite SVM scalar");
  };
  for(unsigned step=0;step<256;step++){
    require(visited.insert(offset).second,"Cyclic SVM control flow");uint32_t op=word(offset++);
    if(op==0)return;
    if(op==1){word(offset+2);offset=word(offset);continue;}
    if(op==11){uint32_t bits=word(offset);word(offset+1);offset+=2;
      supported((bits&0xffffff)==1,"Only geometry normal SVM nodes supported");unsigned s=bits>>24;
      require(s<=252,"SVM normal stack overflow");initialized[s]=initialized[s+1]=initialized[s+2]=true;continue;}
    if(op==5){for(unsigned c=0;c<3;c++)require(std::isfinite(std::bit_cast<float>(word(offset+c))),"Nonfinite closure weight");offset+=3;continue;}
    if(op==2){uint32_t kind=word(offset),mix=word(offset+1)&255;offset+=2;
      supported(mix==255,"Closure mixing is not implemented");supported(++closures==1,"Multiple closures are not implemented");uint32_t normal;
      if(kind==2){uint32_t color=word(offset);
        if((color>>8)==0x7fc000){unsigned s=color&255;require(s<=252,"SVM color stack overflow");for(unsigned c=0;c<3;c++)require(initialized[s+c],"Uninitialized SVM color");}
        else for(unsigned c=0;c<3;c++)require(std::isfinite(std::bit_cast<float>(word(offset+c))),"Nonfinite BSDF color");
        scalar(word(offset+3));normal=word(offset+4)&255;offset+=5;
      }else if(kind==9){word(offset);normal=word(offset+1)&255;offset+=2;}
      else throw Error(CBC_BACKEND_UNSUPPORTED,"Unsupported BSDF closure");
      if(normal!=255){require(normal<=252,"SVM normal stack overflow");for(unsigned c=0;c<3;c++)require(initialized[normal+c],"Uninitialized BSDF normal");}
      continue;
    }
    throw Error(CBC_BACKEND_UNSUPPORTED,"Unsupported SVM opcode");
  }
  throw Error(CBC_BACKEND_INVALID,"SVM instruction budget exceeded");
}
}
SceneData::SceneData(const CBCBackendScene &source){
  require(source.struct_size==sizeof(source)&&source.format_version==1,"Unsupported scene descriptor version");
  geometry=copy(source.geometry,source.geometry_bytes);camera=copy(source.camera,source.camera_bytes);lights=copy(source.lights,source.light_bytes);
  Table c{camera};require(c.rows()>=13,"Truncated camera header");width=c.u(0,0);height=c.u(0,1);samples=c.u(0,2);bounces=c.u(0,3);
  require(width&&height&&width<=4096&&height<=4096&&samples&&samples<=256,"Invalid camera dimensions/sample count");
  supported(bounces==4,"This kernel build supports four surface bounces");
  require(uint64_t(width)*height*samples<=0xffffffffu,"Path index overflow");
  uint32_t sequence=c.u(1,1);supported(sequence>=256&&sequence<=8192&&(sequence&(sequence-1))==0&&c.u(1,2)==256&&c.u(1,3)==4,"Unsupported Sobol table dimensions");
  const uint32_t lut=sequence*256*4;
  require(c.u(4,0)==52&&c.u(4,1)==52+lut&&c.u(4,2)==lut&&c.u(4,3)==1024&&camera.size()==52+lut+1024,"Invalid camera table layout");
  supported(c.u(3,0)==0&&c.u(3,1)==16&&c.u(3,2)==3&&c.u(3,3)==1024,"Unsupported sampling dimensions");
  c.finite(2,3);c.finite(5,13);
  require(c.f(2,0)>=0&&c.f(2,0)<=1&&c.f(2,1)>0&&c.f(2,2)>0,"Invalid camera clipping/scrambling configuration");
  supported(c.u(12,0)==0x7f7fffff&&c.u(12,1)==0x7f7fffff,"Pixel jitter overrides not supported");
  supported(c.f(8,0)==0&&c.f(8,1)==0&&c.f(8,3)>0&&c.f(7,0)==0&&c.f(7,1)==0&&c.f(7,3)>0,"Unsupported perspective projection");
  const double determinant=double(c.f(9,0))*(double(c.f(10,1))*c.f(11,2)-double(c.f(10,2))*c.f(11,1))-
    double(c.f(9,1))*(double(c.f(10,0))*c.f(11,2)-double(c.f(10,2))*c.f(11,0))+
    double(c.f(9,2))*(double(c.f(10,0))*c.f(11,1)-double(c.f(10,1))*c.f(11,0));
  require(std::isfinite(determinant)&&std::abs(determinant)>1e-12,"Degenerate camera transform");
  for(size_t i=52;i<52+lut;i++){float f=std::bit_cast<float>(camera[i]);require(std::isfinite(f)&&f>=0&&f<1,"Invalid Sobol sample");}
  for(size_t i=52+lut;i<camera.size();i++)require(std::isfinite(std::bit_cast<float>(camera[i])),"Invalid pixel filter");

  Table g{geometry};const size_t triangles=g.number(0,0),leaf=g.number(0,1),indices=g.number(0,2),vertices=g.number(0,3);
  require(triangles&&leaf>=5&&(leaf-1)%4==0&&indices>leaf&&vertices>indices&&vertices<g.rows()&&triangles<=(g.rows()-vertices)/4,"Invalid geometry layout");
  const size_t metadata=vertices+4*triangles;require(metadata+2<g.rows(),"Missing SVM metadata");
  const size_t count=g.u(metadata,0),code=g.u(metadata,1);
  require(count&&code==(metadata+2)*4&&code+(count+3)/4*4==geometry.size(),"Invalid SVM table extent");
  supported(g.u(metadata,2)==bounces,"Scene/camera bounce mismatch");g.finite(vertices,metadata);g.finite(metadata+1,metadata+2);
  std::vector<uint8_t> visited(leaf);std::function<void(int32_t,unsigned)> walk=[&](int32_t node,unsigned depth){
    supported(depth<190,"BVH exceeds kernel stack capacity");
    if(node>=0){size_t n=node;require(n>=1&&(n-1)%4==0&&n+3<leaf,"Invalid BVH node");require(!visited[n],"Cyclic/shared BVH node");visited[n]=1;
      g.finite(n+1,n+4);walk(g.i(n,2),depth+1);walk(g.i(n,3),depth+1);
    }else{int64_t n=-int64_t(node)-1;require(n<int64_t(indices-leaf),"Invalid BVH leaf");size_t row=leaf+size_t(n);
      int32_t first=g.i(row,0),last=g.i(row,1);require(first>=0&&last>=first&&uint64_t(last)<=vertices-indices,"Invalid primitive range");
      supported((g.u(row,3)&0xffff)==1,"Only static triangle leaves are supported");
      for(int32_t j=first;j<last;j++)require(g.number(indices+j,0)<triangles,"Invalid triangle reference");}
  };walk(1,0);
  std::set<uint32_t> shaders;for(size_t i=0;i<triangles;i++)shaders.insert(g.number(vertices+triangles*3+i,3));
  for(auto shader:shaders)svm(g,code,count,shader);

  Table l{lights};require(l.rows()>=4,"Missing light header");uint32_t n=l.u(0,0),base=l.u(0,2),dist=l.u(0,3);
  supported(n&&n<=64&&l.u(0,1)==n,"Only finite lamp distributions are supported");
  require(base==3&&dist==3+n*4&&l.rows()==dist+n+1,"Invalid light table extent");l.finite(1,2);
  require(std::abs(l.f(1,0)-1.f/n)<1e-7&&l.f(1,1)==0&&l.f(1,2)>=1&&l.f(1,2)<=1024,"Invalid light PDF/termination settings");
  supported(l.u(2,0)==1,"Unsupported light sampling dimension");std::set<uint32_t> selected;
  for(uint32_t i=0;i<n;i++){uint32_t row=base+4*i;l.finite(row,row+2);
    supported(l.u(row+2,0)==1&&l.u(row+2,2)==1&&l.u(row+2,3)==1,"Only spherical MIS lights with shadows are supported");
    require(l.f(row,3)>0&&l.f(row+1,3)>0,"Invalid spherical emitter");
    require(l.f(row+1,0)>=0&&l.f(row+1,1)>=0&&l.f(row+1,2)>=0,"Negative emission");
    require(std::isfinite(l.f(dist+i,0))&&std::abs(l.f(dist+i,0)-float(i)/n)<1e-7,"Nonuniform lamp CDF unsupported");
    uint32_t id=l.u(dist+i,1);require(id<n&&selected.insert(id).second,"Invalid light CDF selection");
    supported(l.u(dist+i,3)==0,"Light visibility overrides unsupported");
  }
  require(l.f(dist+n,0)==1,"Missing CDF terminator");
}
}
