// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device_tier.h"
#include "device.h"
#include "wavefront.h"
#include "webgpu_device.h"
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <locale>
#include <mutex>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <filesystem>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#else
#include <dirent.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ccl {
#ifdef _WIN32
namespace {
// Windows equivalents of the POSIX calls below. Record paths are UTF-8; the
// pending and lock files are opened through the CRT so they stay int descriptors.
std::filesystem::path native_path(const std::string &utf8){return std::filesystem::u8path(utf8);}
int open_record_file(const std::string &path,int flags){
  int fd=-1;_wsopen_s(&fd,native_path(path).c_str(),flags|_O_BINARY|_O_NOINHERIT,_SH_DENYNO,_S_IREAD|_S_IWRITE);return fd;}
void lock_record_file(int fd,bool lock){
  OVERLAPPED at{};const HANDLE h=reinterpret_cast<HANDLE>(_get_osfhandle(fd));
  if(lock)LockFileEx(h,LOCKFILE_EXCLUSIVE_LOCK,0,1,0,&at);else UnlockFileEx(h,0,1,0,&at);}
bool process_running(long pid){
  const HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(pid));
  if(!h)return GetLastError()==ERROR_ACCESS_DENIED;  // Exists but belongs to another user.
  DWORD code=0;const bool running=GetExitCodeProcess(h,&code)&&code==STILL_ACTIVE;CloseHandle(h);return running;}
bool replace_record_file(const std::string &from,const std::string &to){
  return MoveFileExW(native_path(from).c_str(),native_path(to).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;}
}
#endif
namespace {
std::mutex config_mutex;
DeviceTierConfig host_config;
uint64_t fnv1a(const std::string &text,uint64_t hash=1469598103934665603ull){
  for(unsigned char c:text){hash^=c;hash*=1099511628211ull;}return hash;
}
std::string hex64(uint64_t value){char text[17];std::snprintf(text,sizeof(text),"%016llx",(unsigned long long)value);return text;}
// Record fields are single-line and '|'-free.
std::string field(const std::string &text){
  std::string out;for(char c:text)out+=(c=='\n'||c=='\r')?' ':c=='|'?'/':c;return out;
}
std::string json_escape(const std::string &text){
  std::string out;for(char c:text){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(uint8_t(c)<0x20)out+=' ';else out+=c;}return out;
}
std::string quoted(const std::string &text){return '"'+json_escape(text)+'"';}
std::vector<std::string> split(const std::string &text,char separator){
  std::vector<std::string> out(1);for(char c:text){if(c==separator)out.emplace_back();else out.back()+=c;}return out;
}
bool read_file(const std::string &path,std::string &text){
  std::ifstream in(path,std::ios::binary);if(!in)return false;std::ostringstream s;s<<in.rdbuf();text=s.str();return bool(in)||in.eof();
}
const char *env(const char *name){const char *value=std::getenv(name);return value&&*value?value:nullptr;}
bool contains(const std::string &text,const char *part){return !part||!*part||text.find(part)!=std::string::npos;}
}

void set_device_tier_config(const DeviceTierConfig &config){std::lock_guard lock(config_mutex);host_config=config;}
DeviceTierConfig device_tier_config(){std::lock_guard lock(config_mutex);return host_config;}

// Keep this table small and evidence-based (native/DEVICE.md, "Device tiers").
const std::vector<KnownAdapter> &known_adapters(){
  static const std::vector<KnownAdapter> table={
    {"Pixel 4 XL Adreno 640","Adreno (TM) 640","","full",20000,
     "Phase C close-out 2026-10-20: full tier passes the practical criterion; old-hardware gauge, tighter 20 s compile budget (warnings only)"},
    {"Samsung SM-X700 Adreno 730","Adreno (TM) 730","","full",0,"full-fidelity proof device (2026-10-19 policy)"},
    // Not a reference device (fix-or-degrade standard). The EV031.32 compiler
    // miscompiles (wrong branches, error 16 on valid programs) or fails to link
    // (device lost) the texture, colour and utility node interpreters; those
    // scenes are refused instead (native/DEVICE.md, "Adreno 650 lane").
    {"Samsung Galaxy S20 FE Adreno 650","Adreno (TM) 650","","full",20000,
     "Adreno 650 lane 2026-10-22: full tier with a 20 s compile budget (warnings only); texture, colour and utility shader nodes refused",
     "SVM_NODES_ANY","texture, colour and utility shader nodes",
     // Lane m2-ao (2026-10-24): the Ambient Occlusion node's raytrace stage and the AO
     // shadow rays fail with SVM error 8 (invalid stack) on EV031.32.02.10.
     "AO_ANY","ambient occlusion shader nodes and ambient occlusion shadow rays (fast GI additive mode, AO pass)"},
    {"HP Intel Xe (Mesa 24.0.8, Android x64)","Intel(R) Xe Graphics","Mesa 24.0.8","full",0,
     "B3 2026-10-19: every specialized and all-handler stage compiles"},
    {"Linux Intel UHD ADL-S (Mesa 25.0.7)","Intel(R) UHD Graphics (ADL-S GT1)","Mesa 25.0.7","full",0,
     "Phase C1 2026-10-19 Linux validation"},
  };
  return table;
}
const KnownAdapter *match_known_adapter(const AdapterIdentity &identity){
  for(auto &entry:known_adapters())if(contains(identity.device,entry.device)&&contains(identity.description,entry.driver))return &entry;
  return nullptr;
}
bool mobile_adapter(const AdapterIdentity &identity){
#ifdef __ANDROID__
  (void)identity;return true;
#else
  switch(identity.vendor_id){case 0x5143:case 0x13B5:case 0x1010:case 0x144D:case 0x106B:return true;default:break;}
  for(const char *name:{"Adreno","Mali","PowerVR","Xclipse"})if(identity.device.find(name)!=std::string::npos)return true;
  return false;
#endif
}

std::string production_shader_hash(){
  static std::once_flag once;static std::string hash;
  std::call_once(once,[]{
    uint64_t h=1469598103934665603ull;
    for(const std::string &source:{controls_wgsl(),camera_wgsl(),closest_wgsl(),background_wgsl(),shadow_source(),
                                   volume_source(),volume_stack_source(),surface_source()}){h=fnv1a(source,h);h=fnv1a(std::string(1,'\0'),h);}
    hash="fnv1a64:"+hex64(h);
  });
  return hash;
}

bool parse_device_record(const std::string &text,DeviceRecord &record){
  std::istringstream in(text);std::string line;
  if(!std::getline(in,line)||line!="codebrix-cycles-device-record 1")return false;
  DeviceRecord out;
  while(std::getline(in,line)){
    if(line.empty())continue;
    const size_t eq=line.find('=');if(eq==std::string::npos)return false;
    const std::string key=line.substr(0,eq),value=line.substr(eq+1);
    if(key=="vendor")out.vendor=value;else if(key=="architecture")out.architecture=value;
    else if(key=="device")out.device=value;else if(key=="description")out.description=value;
    else if(key=="runtime")out.runtime=value;else if(key=="shaderHash")out.shader_hash=value;
    else if(key=="stage"){auto f=split(value,'|');if(f.size()!=7)return false;
      out.stages.push_back({f[0],f[1],f[2],std::strtoull(f[3].c_str(),nullptr,10),std::strtoull(f[4].c_str(),nullptr,10),
                            double(std::strtoll(f[5].c_str(),nullptr,10)),double(std::strtoll(f[6].c_str(),nullptr,10))});}
    else if(key=="breadcrumb"){auto f=split(value,'|');if(f.size()!=6)return false;
      out.breadcrumbs.push_back({f[0],f[1],f[2],f[3],int(std::strtol(f[4].c_str(),nullptr,10)),std::strtoll(f[5].c_str(),nullptr,10)});}
    else if(key=="failure"){auto f=split(value,'|');if(f.size()!=4)return false;out.failures.push_back({f[0],f[1],f[2],f[3]});}
    // Unknown keys are ignored (later minor additions).
  }
  record=std::move(out);return true;
}
std::string format_device_record(const DeviceRecord &r){
  std::ostringstream o;
  o<<"codebrix-cycles-device-record 1\nvendor="<<field(r.vendor)<<"\narchitecture="<<field(r.architecture)<<"\ndevice="<<field(r.device)
   <<"\ndescription="<<field(r.description)<<"\nruntime="<<field(r.runtime)<<"\nshaderHash="<<field(r.shader_hash)<<'\n';
  for(auto &s:r.stages)o<<"stage="<<field(s.tier)<<'|'<<field(s.module)<<'|'<<field(s.entry)<<'|'<<s.successes<<'|'<<s.failures
                        <<'|'<<(long long)(s.last_ms+0.5)<<'|'<<(long long)(s.max_ms+0.5)<<'\n';
  for(auto &b:r.breadcrumbs)o<<"breadcrumb="<<field(b.tier)<<'|'<<field(b.module)<<'|'<<field(b.entry)<<'|'<<field(b.kind)<<'|'<<b.signal<<'|'<<b.time<<'\n';
  for(auto &f:r.failures)o<<"failure="<<field(f.tier)<<'|'<<field(f.module)<<'|'<<field(f.entry)<<'|'<<field(f.message.substr(0,400))<<'\n';
  return o.str();
}

namespace {
bool full_tier_failure(const DeviceRecord &r,std::string *what){
  for(auto &b:r.breadcrumbs)if(b.tier=="full"){
    if(what)*what="crash breadcrumb while creating '"+b.entry+"' ("+b.module+", "+(b.kind=="signal"?"signal "+std::to_string(b.signal):"process ended during the compile")+")";
    return true;}
  for(auto &f:r.failures)if(f.tier=="full"){if(what)*what="pipeline creation failure in '"+f.entry+"' ("+f.module+"): "+f.message;return true;}
  return false;
}
uint64_t tier_compiles(const DeviceRecord &r,const char *tier){uint64_t n=0;for(auto &s:r.stages)if(s.tier==tier)n+=s.successes;return n;}
}

void DeviceTierPolicy::persist(const std::function<void(DeviceRecord&)> &apply){
  if(!record_writable)return;
  const std::string lock_path=record_path+".lock";
#ifdef _WIN32
  const int lock=open_record_file(lock_path,_O_CREAT|_O_RDWR);
  if(lock<0){warnings.push_back("device record lock '"+lock_path+"' unavailable: "+std::strerror(errno));record_writable=false;return;}
  lock_record_file(lock,true);
#else
  const int lock=open(lock_path.c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
  if(lock<0){warnings.push_back("device record lock '"+lock_path+"' unavailable: "+std::strerror(errno));record_writable=false;return;}
  flock(lock,LOCK_EX);
#endif
  DeviceRecord current;std::string text;
  if(!(read_file(record_path,text)&&parse_device_record(text,current)&&current.vendor==field(identity.vendor)&&current.architecture==field(identity.architecture)
       &&current.device==field(identity.device)&&current.description==field(identity.description)&&current.runtime==field(runtime)&&current.shader_hash==shader_hash)){
    current=DeviceRecord{};current.vendor=field(identity.vendor);current.architecture=field(identity.architecture);current.device=field(identity.device);
    current.description=field(identity.description);current.runtime=field(runtime);current.shader_hash=shader_hash;
  }
  apply(current);
  if(current.breadcrumbs.size()>16)current.breadcrumbs.erase(current.breadcrumbs.begin(),current.breadcrumbs.end()-16);
  if(current.failures.size()>16)current.failures.erase(current.failures.begin(),current.failures.end()-16);
#ifdef _WIN32
  const std::string temporary=record_path+".tmp-"+std::to_string(_getpid());
  bool ok=false;{std::ofstream out(native_path(temporary),std::ios::binary|std::ios::trunc);out<<format_device_record(current);out.flush();ok=bool(out);}
  if(ok&&replace_record_file(temporary,record_path)){record=current;record_status="valid";}
  else{std::error_code ignored;std::filesystem::remove(native_path(temporary),ignored);warnings.push_back("device record '"+record_path+"' could not be written");record_writable=false;}
  lock_record_file(lock,false);_close(lock);
#else
  const std::string temporary=record_path+".tmp-"+std::to_string(getpid());
  bool ok=false;{std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out<<format_device_record(current);out.flush();ok=bool(out);}
  if(ok&&std::rename(temporary.c_str(),record_path.c_str())==0){record=current;record_status="valid";}
  else{std::remove(temporary.c_str());warnings.push_back("device record '"+record_path+"' could not be written");record_writable=false;}
  flock(lock,LOCK_UN);close(lock);
#endif
}

// A pending file left by a process that no longer runs is a breadcrumb: the
// process ended (driver crash, out-of-memory kill or forced stop) while it
// created that pipeline. "signal=<n>" is present when the crash handler ran.
void DeviceTierPolicy::consume_pending(){
#ifdef _WIN32
  const size_t slash=record_path.find_last_of("/\\");
  const std::string directory=record_path.substr(0,slash),prefix=record_path.substr(slash+1)+".pending-";
  std::vector<std::string> names;std::error_code failed;
  for(std::filesystem::directory_iterator it(native_path(directory),failed),end;!failed&&it!=end;it.increment(failed)){
    const auto u8=it->path().filename().u8string();const std::string name(u8.begin(),u8.end());
    if(name.compare(0,prefix.size(),prefix)==0)names.push_back(name);
  }
  for(const std::string &name:names){
    const long pid=std::strtol(name.c_str()+prefix.size(),nullptr,10);
    if(pid<=0||pid==long(_getpid()))continue;
    if(process_running(pid))continue;  // Still running (concurrent process).
#else
  const size_t slash=record_path.rfind('/');
  const std::string directory=record_path.substr(0,slash),prefix=record_path.substr(slash+1)+".pending-";
  DIR *dir=opendir(directory.c_str());if(!dir)return;
  std::vector<std::string> names;while(dirent *e=readdir(dir))if(std::strncmp(e->d_name,prefix.c_str(),prefix.size())==0)names.push_back(e->d_name);
  closedir(dir);
  for(const std::string &name:names){
    const long pid=std::strtol(name.c_str()+prefix.size(),nullptr,10);
    if(pid<=0||pid==long(getpid()))continue;
    if(kill(pid_t(pid),0)==0||errno==EPERM)continue;  // Still running (concurrent process).
#endif
    const std::string path=directory+"/"+name;std::string text;if(!read_file(path,text))continue;
    std::istringstream in(text);std::string line;DeviceRecordBreadcrumb crumb;crumb.kind="interrupted";std::string pending_runtime,pending_hash;bool header=false;
    while(std::getline(in,line)){
      if(line=="codebrix-cycles-pending 1"){header=true;continue;}
      const size_t eq=line.find('=');if(eq==std::string::npos)continue;const std::string key=line.substr(0,eq),value=line.substr(eq+1);
      if(key=="runtime")pending_runtime=value;else if(key=="shaderHash")pending_hash=value;else if(key=="tier")crumb.tier=value;
      else if(key=="module")crumb.module=value;else if(key=="entry")crumb.entry=value;else if(key=="time")crumb.time=std::strtoll(value.c_str(),nullptr,10);
      else if(key=="signal"){crumb.kind="signal";crumb.signal=int(std::strtol(value.c_str(),nullptr,10));}
    }
    const bool applies=header&&pending_runtime==field(runtime)&&pending_hash==shader_hash&&!crumb.entry.empty();
    if(record_mode=="read-write"){
      if(applies)persist([&](DeviceRecord &r){r.breadcrumbs.push_back(crumb);});
      std::remove(path.c_str());
    }else if(applies)record.breadcrumbs.push_back(crumb);
    if(applies)warnings.push_back("breadcrumb from an earlier run: the process ended while creating '"+crumb.entry+"' ("+crumb.module+", "+crumb.tier+" tier"+
                                  (crumb.kind=="signal"?", signal "+std::to_string(crumb.signal):"")+")");
  }
}

void DeviceTierPolicy::decide(const AdapterIdentity &adapter,const std::string &runtime_version,const std::string &hash){
  identity=adapter;runtime=runtime_version;shader_hash=hash;config=device_tier_config();
  if(identity.simulated)overrides.push_back("CODEBRIX_CYCLES_TEST_ADAPTER (simulated adapter identity)");
  if(const char *tier=env("CODEBRIX_CYCLES_PIPELINE_TIER")){
    config.request=std::strcmp(tier,"conservative")==0?TIER_REQUEST_CONSERVATIVE:std::strcmp(tier,"full")==0?TIER_REQUEST_FULL:TIER_REQUEST_AUTO;
    overrides.push_back(std::string("CODEBRIX_CYCLES_PIPELINE_TIER=")+tier);
  }
  if(const char *dir=env("CODEBRIX_CYCLES_DEVICE_RECORD_DIR")){config.record_directory=dir;overrides.push_back(std::string("CODEBRIX_CYCLES_DEVICE_RECORD_DIR=")+dir);}
  bool ignore=false;
  if(const char *mode=env("CODEBRIX_CYCLES_DEVICE_RECORD")){
    overrides.push_back(std::string("CODEBRIX_CYCLES_DEVICE_RECORD=")+mode);
    if(std::strcmp(mode,"off")==0)config.record_off=true;
    else if(std::strcmp(mode,"ignore")==0)ignore=true;
    else if(std::strcmp(mode,"readonly")==0)config.record_read_only=true;
    else if(std::strcmp(mode,"on")==0){config.record_off=false;config.record_read_only=false;}
  }
  if(const char *canary=env("CODEBRIX_CYCLES_PIPELINE_CANARY")){config.canary=std::strcmp(canary,"0")!=0;overrides.push_back(std::string("CODEBRIX_CYCLES_PIPELINE_CANARY=")+canary);}
  canary_enabled=config.canary;
  table=match_known_adapter(identity);

  bool record_applies=false;
  if(config.record_off)record_mode=record_status="off";
  else if(config.record_directory.empty())record_mode=record_status="none";
  else{
    record_mode=ignore?"ignore":config.record_read_only?"read-only":"read-write";
    const std::string directory=config.record_directory;
    // Create missing directories (app data folders normally exist already).
#ifdef _WIN32
    {std::error_code failed;std::filesystem::create_directories(native_path(directory),failed);
     if(failed)warnings.push_back("device record directory '"+directory+"' unavailable: "+failed.message());}
#else
    for(size_t at=directory.find('/',1);;at=directory.find('/',at+1)){
      const std::string part=directory.substr(0,at);
      if(mkdir(part.c_str(),0700)!=0&&errno!=EEXIST&&at==std::string::npos)warnings.push_back("device record directory '"+directory+"' unavailable: "+std::strerror(errno));
      if(at==std::string::npos)break;
    }
#endif
    record_path=directory+"/codebrix-cycles-device-"+hex64(fnv1a(identity.vendor+'\n'+identity.architecture+'\n'+identity.device+'\n'+identity.description+'\n'+
                                                                 std::to_string(identity.vendor_id)+'\n'+std::to_string(identity.device_id)))+".txt";
    record_writable=record_mode=="read-write";
    std::string text;
    if(!read_file(record_path,text))record_status="absent";
    else if(!parse_device_record(text,record))record_status="unreadable";
    else if(record.vendor!=field(identity.vendor)||record.architecture!=field(identity.architecture)||record.device!=field(identity.device)||record.description!=field(identity.description))record_status="identity-mismatch";
    else if(record.runtime!=field(runtime))record_status="stale-runtime";
    else if(record.shader_hash!=shader_hash)record_status="stale-shader-hash";
    else record_status="valid";
    if(record_status!="valid"&&record_status!="absent"){
      if(record_status!="unreadable")warnings.push_back("device record ignored ("+record_status+"); it is replaced by this run's record");
      else warnings.push_back("device record unreadable; it is replaced by this run's record");
      record=DeviceRecord{};
    }
    consume_pending();
    record_applies=record_mode!="ignore"&&(record_status=="valid"||!record.breadcrumbs.empty());
  }

  std::string why;
  if(config.request==TIER_REQUEST_FULL){conservative=false;reason="override";detail="full tier requested";}
  else if(config.request==TIER_REQUEST_CONSERVATIVE){conservative=true;reason="opt-in";detail="conservative tier requested";}
  else if(record_applies&&full_tier_failure(record,&why)){conservative=true;reason="record";detail=why;}
  else if(record_applies&&tier_compiles(record,"full")>0){conservative=false;reason="record";
    detail=std::to_string(tier_compiles(record,"full"))+" full-tier pipeline compiles recorded without a failure or breadcrumb";}
  else if(table){conservative=std::strcmp(table->tier,"conservative")==0;reason="table";detail=std::string(table->name)+": "+table->evidence;}
  else{conservative=false;reason="default";detail=record_status=="absent"?"first run on this device: full fidelity":"no device record: full fidelity";}
  if(table&&table->budget_ms>0){budget_ms=table->budget_ms;budget_source="table";}
  else if(!table&&mobile_adapter(identity)){budget_ms=unknown_mobile_budget_ms;budget_source="unknown-mobile";}
}

void DeviceTierPolicy::begin_pipeline(const char *module,const char *entry,bool tier_conservative){
  pending_tier=tier_conservative?"conservative":"full";pending_module=module;pending_entry=entry;
  if(!record_writable)return;
  // One pending file per device and pipeline: <record>.pending-<pid>-<device>-<serial>.
#ifdef _WIN32
  pending_path=record_path+".pending-"+std::to_string(_getpid())+"-"+std::to_string(reinterpret_cast<uintptr_t>(this))+"-"+std::to_string(pending_serial++);
  pending_fd=open_record_file(pending_path,_O_CREAT|_O_TRUNC|_O_WRONLY);
#else
  pending_path=record_path+".pending-"+std::to_string(getpid())+"-"+std::to_string(reinterpret_cast<uintptr_t>(this))+"-"+std::to_string(pending_serial++);
  pending_fd=open(pending_path.c_str(),O_CREAT|O_TRUNC|O_WRONLY|O_CLOEXEC,0600);
#endif
  if(pending_fd<0)return;
  const std::string text="codebrix-cycles-pending 1\nruntime="+field(runtime)+"\nshaderHash="+shader_hash+"\ntier="+pending_tier+
                         "\nmodule="+field(module)+"\nentry="+field(entry)+"\ntime="+std::to_string((long long)std::time(nullptr))+"\n";
#ifdef _WIN32
  if(_write(pending_fd,text.data(),unsigned(text.size()))!=int(text.size())){_close(pending_fd);pending_fd=-1;std::error_code ignored;std::filesystem::remove(native_path(pending_path),ignored);return;}
#else
  if(write(pending_fd,text.data(),text.size())!=ssize_t(text.size())){close(pending_fd);pending_fd=-1;std::remove(pending_path.c_str());return;}
#endif
}
void DeviceTierPolicy::abandon_pipeline(){
#ifdef _WIN32
  if(pending_fd>=0){_close(pending_fd);pending_fd=-1;std::error_code ignored;std::filesystem::remove(native_path(pending_path),ignored);}
#else
  if(pending_fd>=0){close(pending_fd);pending_fd=-1;std::remove(pending_path.c_str());}
#endif
}
void DeviceTierPolicy::end_pipeline(bool success,double ms,const std::string &failure){
  abandon_pipeline();
  persist([&](DeviceRecord &r){
    DeviceRecordStage *stage=nullptr;
    for(auto &s:r.stages)if(s.tier==pending_tier&&s.module==pending_module&&s.entry==pending_entry)stage=&s;
    if(!stage){r.stages.push_back({pending_tier,pending_module,pending_entry});stage=&r.stages.back();}
    (success?stage->successes:stage->failures)++;stage->last_ms=ms;if(ms>stage->max_ms)stage->max_ms=ms;
    if(!success)r.failures.push_back({pending_tier,pending_module,pending_entry,failure});
  });
}
void DeviceTierPolicy::canary_result(const std::string &result,double ms,const std::string &text){canary=result;canary_ms=ms;canary_detail=text;}
void DeviceTierPolicy::switch_conservative(const std::string &why,const std::string &text){
  if(conservative)return;
  conservative=true;reason=why;detail=text;
}
std::string DeviceTierPolicy::report_json() const{
  std::ostringstream o;o.imbue(std::locale::classic());
  o<<"{\"tier\":\""<<(conservative?"conservative":"full")<<"\",\"reason\":"<<quoted(reason)<<",\"detail\":"<<quoted(detail)
   <<",\"adapter\":{\"vendor\":"<<quoted(identity.vendor)<<",\"architecture\":"<<quoted(identity.architecture)<<",\"device\":"<<quoted(identity.device)
   <<",\"description\":"<<quoted(identity.description)<<",\"vendorId\":"<<identity.vendor_id<<",\"deviceId\":"<<identity.device_id
   <<",\"simulated\":"<<(identity.simulated?"true":"false")<<",\"mobile\":"<<(mobile_adapter(identity)?"true":"false")<<"},\"runtime\":"<<quoted(runtime)
   <<",\"shaderHash\":"<<quoted(shader_hash)<<",\"knownAdapter\":";
  if(table){o<<"{\"name\":"<<quoted(table->name)<<",\"tier\":"<<quoted(table->tier)<<",\"budgetMs\":"<<table->budget_ms<<",\"evidence\":"<<quoted(table->evidence);
    if(table->refused_region)o<<",\"refusedRegion\":"<<quoted(table->refused_region)<<",\"refused\":"<<quoted(table->refused_text);
    if(table->also_refused_region)o<<",\"alsoRefusedRegion\":"<<quoted(table->also_refused_region)<<",\"alsoRefused\":"<<quoted(table->also_refused_text);
    o<<'}';}
  else o<<"null";
  o<<",\"budgetSource\":"<<quoted(budget_source)<<",\"record\":{\"mode\":"<<quoted(record_mode)<<",\"path\":"<<quoted(record_path)
   <<",\"status\":"<<quoted(record_status)<<",\"fullTierCompiles\":"<<tier_compiles(record,"full")<<",\"conservativeTierCompiles\":"<<tier_compiles(record,"conservative")
   <<",\"failures\":"<<record.failures.size()<<",\"breadcrumbs\":[";
  for(size_t i=0;i<record.breadcrumbs.size();i++){const auto &b=record.breadcrumbs[i];if(i)o<<',';
    o<<"{\"tier\":"<<quoted(b.tier)<<",\"module\":"<<quoted(b.module)<<",\"entry\":"<<quoted(b.entry)<<",\"kind\":"<<quoted(b.kind)<<",\"signal\":"<<b.signal<<",\"time\":"<<b.time<<'}';}
  o<<"]},\"canary\":{\"enabled\":"<<(canary_enabled?"true":"false")<<",\"result\":"<<quoted(canary)<<",\"ms\":"<<canary_ms<<",\"detail\":"<<quoted(canary_detail)<<"},\"overrides\":[";
  for(size_t i=0;i<overrides.size();i++){if(i)o<<',';o<<quoted(overrides[i]);}
  o<<"],\"warnings\":[";
  for(size_t i=0;i<warnings.size();i++){if(i)o<<',';o<<quoted(warnings[i]);}
  o<<"]}";return o.str();
}

std::string webgpu_runtime_version(){
  WGPUCodebrixBuildInfo info{};wgpuCodebrixGetBuildInfo(&info);
  auto text=[](WGPUStringView s){return s.data?std::string(s.data,s.length==WGPU_STRLEN?std::strlen(s.data):s.length):std::string();};
  return "codebrix-api="+std::to_string(info.apiVersion)+";wgpu-native="+text(info.wgpuNativeVersion)+";wgpu-core="+text(info.wgpuCoreVersion)+
         ";target="+text(info.targetTriple)+";patches="+text(info.patches);
}
}
