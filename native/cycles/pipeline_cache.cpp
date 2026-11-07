// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "pipeline_cache.h"
#include "device_tier.h"
#include "../oracle/sha256.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <locale>
#include <mutex>
#include <sstream>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ccl {
namespace {
namespace fs=std::filesystem;
using clock_type=std::chrono::steady_clock;
double ms_since(clock_type::time_point t0){return std::chrono::duration<double,std::milli>(clock_type::now()-t0).count();}
std::mutex config_mutex;
PipelineCacheConfig host_config;
bool default_allowed=false;
const char *env(const char *name){const char *value=std::getenv(name);return value&&*value?value:nullptr;}
#ifdef _WIN32
fs::path native_path(const std::string &utf8){return fs::u8path(utf8);}
long process_id(){return long(GetCurrentProcessId());}
#else
fs::path native_path(const std::string &utf8){return fs::path(utf8);}
long process_id(){return long(getpid());}
#endif
std::string json_text(const std::string &text){
  std::string out;for(char c:text){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(uint8_t(c)<0x20)out+=' ';else out+=c;}return out;
}
std::string quoted_or_null(const std::string &text){return text.empty()?std::string("null"):'"'+json_text(text)+'"';}
// The user's cache directory (see pipeline_cache.h), empty where there is none.
// The XDG rule: an unset, empty or relative XDG_CACHE_HOME means ~/.cache.
std::string user_cache_directory(){
#if defined(__linux__)&&!defined(__ANDROID__)
  std::string root;
  if(const char *xdg=env("XDG_CACHE_HOME");xdg&&xdg[0]=='/')root=xdg;
  else if(const char *home=env("HOME"))root=std::string(home)+"/.cache";
  else if(const passwd *user=getpwuid(getuid());user&&user->pw_dir&&*user->pw_dir)root=std::string(user->pw_dir)+"/.cache";
  return root.empty()?root:root+"/cycles/webgpu-pipelines";
#else
  return {};
#endif
}
struct Choice {std::string directory,source,reason;};
Choice choose(){
  // Fingerprint mode hands back stub pipelines and compiles only the scene-preparation kernels (fingerprint.h): it neither reads nor writes a cache, whatever is asked below.
  if(env("CODEBRIX_CYCLES_FINGERPRINT"))return {{},{},"fingerprint-mode"};
  if(const char *mode=env("CODEBRIX_CYCLES_PIPELINE_CACHE");mode&&std::strcmp(mode,"off")==0)return {{},{},"off"};
  if(const char *directory=env("CODEBRIX_CYCLES_PIPELINE_CACHE_DIR"))return {directory,"environment",{}};
  PipelineCacheConfig config;bool allowed=false;
  {std::lock_guard lock(config_mutex);config=host_config;allowed=default_allowed;}
  if(config.off)return {{},{},"off"};
  if(!config.directory.empty())return {config.directory,"consumer",{}};
  const std::string record=device_tier_config().record_directory;
  if(!record.empty())return {record,"record-directory",{}};
  if(!allowed)return {{},{},"not-requested"};
  const std::string user=user_cache_directory();
  if(user.empty())return {{},{},"no-directory"};
  return {user,"user-cache",{}};
}
bool make_directory(const std::string &directory){
#ifdef _WIN32
  std::error_code failed;fs::create_directories(native_path(directory),failed);
  return fs::is_directory(native_path(directory),failed);
#else
  for(size_t at=directory.find('/',1);;at=directory.find('/',at+1)){
    (void)mkdir(directory.substr(0,at).c_str(),0700);
    if(at==std::string::npos)break;
  }
  struct stat info{};
  return stat(directory.c_str(),&info)==0&&S_ISDIR(info.st_mode)&&access(directory.c_str(),W_OK|X_OK)==0;
#endif
}
// File: magic, key bytes, runtime bytes, data bytes, SHA-256 of the data (hex),
// key, runtime, data. Native byte order: a cache file never leaves its machine.
constexpr char file_magic[8]={'C','B','C','P','L','C','0','1'};
constexpr size_t hash_chars=64,header_bytes=sizeof(file_magic)+4+4+8+hash_chars;
// The default limit on one adapter's folder (native/DEVICE.md "Pipeline cache").
constexpr size_t default_max_bytes=size_t(1)<<30;
std::string folder_name(const std::string &key){
  std::string out;
  for(char c:key)out+=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'?c:'_';
  return out;
}
bool has_suffix(const std::string &text,const char *suffix){
  const size_t n=std::strlen(suffix);return text.size()>=n&&text.compare(text.size()-n,n,suffix)==0;
}
// Temporary files a process left when it died: older than an hour, so never one in use.
void remove_stale_temporaries(const std::string &folder){
  std::error_code failed;const auto now=fs::file_time_type::clock::now();
  fs::directory_iterator it(native_path(folder),failed);
  for(const fs::directory_iterator end;!failed&&it!=end;it.increment(failed)){
    if(!has_suffix(it->path().filename().string(),".tmp"))continue;
    std::error_code ignored;const auto written=fs::last_write_time(it->path(),ignored);
    if(!ignored&&now-written>std::chrono::hours(1))fs::remove(it->path(),ignored);
  }
}
// Reads and checks a file: "absent", "damaged", "other-adapter", "other-runtime" or
// "loaded" (data then holds the runtime's bytes).
std::string read_cache_file(const std::string &path,const std::string &key,const std::string &runtime,std::string &data){
  std::ifstream in(native_path(path),std::ios::binary);if(!in)return "absent";
  std::error_code failed;const uintmax_t size=fs::file_size(native_path(path),failed);
  if(failed||size<header_bytes)return "damaged";
  std::string bytes(size_t(size),'\0');
  if(!in.read(bytes.data(),std::streamsize(size)))return "damaged";
  const char *at=bytes.data();
  if(std::memcmp(at,file_magic,sizeof(file_magic))!=0)return "damaged";
  uint32_t key_bytes=0,runtime_bytes=0;uint64_t data_bytes=0;
  std::memcpy(&key_bytes,at+8,4);std::memcpy(&runtime_bytes,at+12,4);std::memcpy(&data_bytes,at+16,8);
  if(uint64_t(key_bytes)+runtime_bytes+data_bytes+header_bytes!=size)return "damaged";
  const char *key_at=at+header_bytes,*runtime_at=key_at+key_bytes,*data_at=runtime_at+runtime_bytes;
  if(std::string(at+24,hash_chars)!=oracle::sha256(data_at,size_t(data_bytes)))return "damaged";
  if(std::string(key_at,key_bytes)!=key)return "other-adapter";
  if(std::string(runtime_at,runtime_bytes)!=runtime)return "other-runtime";
  data.assign(data_at,size_t(data_bytes));return "loaded";
}
bool write_cache_file(const std::string &path,const std::string &key,const std::string &runtime,const std::vector<char> &data,std::string &failure){
  static std::atomic<unsigned> serial{0};
  const std::string temporary=path+"."+std::to_string(process_id())+"."+std::to_string(serial++)+".tmp";
  const std::string hash=oracle::sha256(data.data(),data.size());
  const uint32_t key_bytes=uint32_t(key.size()),runtime_bytes=uint32_t(runtime.size());const uint64_t data_bytes=data.size();
  bool written=false;
  {std::ofstream out(native_path(temporary),std::ios::binary|std::ios::trunc);
   out.write(file_magic,sizeof(file_magic));out.write(reinterpret_cast<const char*>(&key_bytes),4);out.write(reinterpret_cast<const char*>(&runtime_bytes),4);
   out.write(reinterpret_cast<const char*>(&data_bytes),8);out.write(hash.data(),std::streamsize(hash.size()));out.write(key.data(),std::streamsize(key.size()));
   out.write(runtime.data(),std::streamsize(runtime.size()));out.write(data.data(),std::streamsize(data.size()));out.close();written=!out.fail();}
  std::error_code failed;
  if(!written){failure="cannot write "+temporary;fs::remove(native_path(temporary),failed);return false;}
  // The rename is what other processes see: the old file or the whole new one.
  // Power loss may leave a short file; the hash then makes the next run rebuild it.
  fs::rename(native_path(temporary),native_path(path),failed);
  if(failed){failure="cannot replace "+path+": "+failed.message();std::error_code ignored;fs::remove(native_path(temporary),ignored);return false;}
  return true;
}
}
void set_pipeline_cache_config(const PipelineCacheConfig &config){std::lock_guard lock(config_mutex);host_config=config;}
void allow_default_pipeline_cache(){std::lock_guard lock(config_mutex);default_allowed=true;}
bool pipeline_cache_wanted(){return choose().reason.empty();}
void PipelineDiskCache::open(cbc::Device &gpu)try{
  const Choice choice=choose();reason=choice.reason;
  if(!reason.empty())return;
  directory=choice.directory;source=choice.source;
  if(!gpu.pipeline_cache_offered){reason="not-offered";return;}
  key=gpu.pipeline_cache_key;runtime=webgpu_runtime_version();
  folder=directory+"/"+folder_name(key);
  if(!make_directory(folder)){reason="directory-unavailable";return;}
  max_bytes=default_max_bytes;
  if(const char *limit=env("CODEBRIX_CYCLES_PIPELINE_CACHE_MAX_BYTES"))max_bytes=size_t(std::strtoull(limit,nullptr,10));
  remove_stale_temporaries(folder);
  device=&gpu;active=true;
}catch(const std::exception &e){reason="create-failed";failure=e.what();}
PipelineDiskCache::Use::Use(PipelineDiskCache &cache_owner,const std::string &code,const char *entry):owner(cache_owner){
  if(!owner.active||owner.device->pipeline_cache_faults)return;
  try{
    cbc::Device &gpu=*owner.device;const auto t0=clock_type::now();
    std::string name=code;name+='\0';name+=entry;
    path=owner.folder+"/"+oracle::sha256(name.data(),name.size()).substr(0,32)+".bin";
    std::string data;const std::string load=read_cache_file(path,owner.key,owner.runtime,data);
    if(load=="damaged")owner.damaged++;else if(load=="other-adapter")owner.other_adapter++;else if(load=="other-runtime")owner.other_runtime++;
    const auto create=[&](const void *bytes,size_t count,bool fallback,bool keep_invalid){
      WGPUCodebrixPipelineCacheDescriptor descriptor{};descriptor.data=bytes;descriptor.dataSize=count;descriptor.fallback=fallback;
      gpu.push_scope();
      WGPUCodebrixPipelineCache made=wgpuCodebrixDeviceCreatePipelineCache(gpu.get(),&descriptor);
      try{gpu.pop_scope();}catch(const std::exception&){if(made&&!keep_invalid){wgpuCodebrixPipelineCacheRelease(made);made=nullptr;}}
      return made;
    };
    // Gate hook: a cache the runtime created invalid, so chaining it fails the
    // pipeline, proving that a failure through the cache is not the consumer's.
    if(const char *test=env("CODEBRIX_CYCLES_TEST_PIPELINE_CACHE");test&&std::strcmp(test,"invalid")==0)handle=create("x",1,false,true);
    else if(load=="loaded"){
      // The runtime refuses another adapter's data even with fallback, as a validation error.
      handle=create(data.data(),data.size(),true,false);
      if(handle)loaded=true;else owner.rejected++;
    }
    if(!handle)handle=create(nullptr,0,true,false);
    if(!handle){owner.load_ms+=ms_since(t0);return;}
    known_bytes=wgpuCodebrixPipelineCacheGetData(handle,nullptr,0);
    // With fallback, data from another driver version gives an empty cache.
    if(loaded&&known_bytes<data.size()){loaded=false;owner.outdated++;}
    if(loaded)owner.loaded_bytes+=data.size();
    owner.load_ms+=ms_since(t0);
  }catch(const std::exception &e){owner.failure=e.what();}
}
PipelineDiskCache::Use::~Use(){if(handle)wgpuCodebrixPipelineCacheRelease(handle);}
void PipelineDiskCache::Use::compiled()try{
  if(!handle||owner.device->pipeline_cache_faults)return;
  const size_t bytes=wgpuCodebrixPipelineCacheGetData(handle,nullptr,0);
  // Nothing new: the driver took the pipeline from what was loaded. Its file is now
  // the most recently used, for the size limit.
  if(bytes==known_bytes&&loaded){hit=true;owner.hits++;std::error_code ignored;fs::last_write_time(native_path(path),fs::file_time_type::clock::now(),ignored);return;}
  owner.misses++;
  // Some drivers keep nothing; then there is nothing to save.
  if(!bytes||bytes==known_bytes)return;
  const auto t0=clock_type::now();
  if(bytes>owner.max_bytes){std::error_code ignored;fs::remove(native_path(path),ignored);return;}
  std::vector<char> data(bytes);
  if(wgpuCodebrixPipelineCacheGetData(handle,data.data(),data.size())!=bytes)return;
  if(write_cache_file(path,owner.key,owner.runtime,data,owner.failure)){owner.saves++;owner.saved_bytes+=bytes;owner.enforce_limit();}else owner.save_failures++;
  owner.save_ms+=ms_since(t0);
}catch(const std::exception &e){owner.save_failures++;owner.failure=e.what();}
// The oldest pipelines go first when a save takes the adapter's folder over the limit.
void PipelineDiskCache::enforce_limit(){
  struct Row {fs::path path;uintmax_t bytes;fs::file_time_type written;};
  std::vector<Row> rows;uintmax_t total=0;std::error_code failed;
  fs::directory_iterator it(native_path(folder),failed);
  for(const fs::directory_iterator end;!failed&&it!=end;it.increment(failed)){
    if(!has_suffix(it->path().filename().string(),".bin"))continue;
    std::error_code ignored;const uintmax_t bytes=fs::file_size(it->path(),ignored);if(ignored)continue;
    const auto written=fs::last_write_time(it->path(),ignored);if(ignored)continue;
    rows.push_back({it->path(),bytes,written});total+=bytes;
  }
  if(total<=max_bytes)return;
  std::sort(rows.begin(),rows.end(),[](const Row &a,const Row &b){return a.written<b.written;});
  for(const Row &row:rows){
    if(total<=max_bytes)break;
    std::error_code ignored;if(fs::remove(row.path,ignored))total-=row.bytes;
  }
}
std::string PipelineDiskCache::report_json() const{
  const bool faulted=active&&device&&device->pipeline_cache_faults;
  std::ostringstream o;o.imbue(std::locale::classic());
  o<<"{\"active\":"<<(active&&!faulted?"true":"false")<<",\"reason\":\""<<(faulted?"faulted":active?"active":json_text(reason))<<'"'
   <<",\"directory\":"<<quoted_or_null(directory)<<",\"directorySource\":"<<quoted_or_null(source)<<",\"folder\":"<<quoted_or_null(folder)<<",\"key\":"<<quoted_or_null(key)
   <<",\"hits\":"<<hits<<",\"misses\":"<<misses<<",\"ignored\":{\"damaged\":"<<damaged<<",\"otherAdapter\":"<<other_adapter<<",\"otherRuntime\":"<<other_runtime
   <<",\"rejected\":"<<rejected<<",\"outdated\":"<<outdated<<"},\"loadedBytes\":"<<loaded_bytes<<",\"loadMs\":"<<load_ms
   <<",\"saves\":"<<saves<<",\"savedBytes\":"<<saved_bytes<<",\"saveMs\":"<<save_ms<<",\"saveFailures\":"<<save_failures
   <<",\"maxBytes\":"<<max_bytes<<",\"failure\":"<<quoted_or_null(failure)<<'}';
  return o.str();
}
}
