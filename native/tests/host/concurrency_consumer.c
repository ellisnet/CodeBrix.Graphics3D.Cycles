// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#define main baseline_lifecycle_main
#include "consumer.c"
#undef main

#ifdef __APPLE__
// macOS omits the optional POSIX barriers; this equivalent covers the uses below.
typedef struct { pthread_mutex_t mutex; pthread_cond_t ready; unsigned count, waiting, cycle; } cbc_barrier_t;
static int cbc_barrier_init(cbc_barrier_t *barrier, const void *attributes, unsigned count) {
  (void)attributes; if (!count) return -1;
  barrier->count = count; barrier->waiting = 0; barrier->cycle = 0;
  if (pthread_mutex_init(&barrier->mutex, NULL) != 0) return -1;
  if (pthread_cond_init(&barrier->ready, NULL) != 0) { pthread_mutex_destroy(&barrier->mutex); return -1; }
  return 0;
}
static int cbc_barrier_wait(cbc_barrier_t *barrier) {
  pthread_mutex_lock(&barrier->mutex);
  const unsigned cycle = barrier->cycle;
  if (++barrier->waiting == barrier->count) {
    barrier->waiting = 0; ++barrier->cycle; pthread_cond_broadcast(&barrier->ready);
    pthread_mutex_unlock(&barrier->mutex); return -1;
  }
  while (cycle == barrier->cycle) pthread_cond_wait(&barrier->ready, &barrier->mutex);
  pthread_mutex_unlock(&barrier->mutex); return 0;
}
static int cbc_barrier_destroy(cbc_barrier_t *barrier) {
  pthread_cond_destroy(&barrier->ready); return pthread_mutex_destroy(&barrier->mutex);
}
#  define pthread_barrier_t cbc_barrier_t
#  define pthread_barrier_init cbc_barrier_init
#  define pthread_barrier_wait cbc_barrier_wait
#  define pthread_barrier_destroy cbc_barrier_destroy
#  define PTHREAD_BARRIER_SERIAL_THREAD (-1)
#endif

enum { WORKERS=4, CONFIG_ROUNDS=64, HOST_ROUNDS=24, DESTROY_ROUNDS=12 };
static const CBCHostConfig host_config={sizeof(CBCHostConfig),CBC_HOST_ABI,CBC_HOST_CPU_REFERENCE,1,{0}};
static char config_a[4096],config_b[4096];
typedef struct Worker { pthread_barrier_t *barrier; int guarded; unsigned accepted,rejected,hosts; } Worker;
static void start(Worker *worker) {
  const int result=pthread_barrier_wait(worker->barrier);
  REQUIRE(result==0 || result==PTHREAD_BARRIER_SERIAL_THREAD);
}
static void *config_worker(void *pointer) {
  Worker *worker=pointer;start(worker);
  for(unsigned i=0;i<CONFIG_ROUNDS;++i) {
    const int status=cbc_host_color_config_path(i%2?config_a:config_b);
    if(worker->guarded)REQUIRE(status==CBC_NOT_READY);
    REQUIRE(status==CBC_OK || status==CBC_NOT_READY);
    worker->accepted+=status==CBC_OK;worker->rejected+=status==CBC_NOT_READY;
  }
  return NULL;
}
static void *host_worker(void *pointer) {
  Worker *worker=pointer;start(worker);
  for(unsigned i=0;i<HOST_ROUNDS;++i) {
    CBCHost host=0;CHECK(cbc_host_create(&host_config,&host));
    REQUIRE(cbc_host_color_config_path(config_b)==CBC_NOT_READY);
    CBCNode camera=singleton(host,"camera");scalar(host,camera,"fov",1.1f);
    float value=0;uint64_t count=0;CHECK(cbc_host_get_floats(host,camera,"fov",&value,1,&count));
    REQUIRE(count==1 && value==1.1f);CHECK(cbc_host_destroy(host));++worker->hosts;
  }
  return NULL;
}
static void run_race(int guarded,unsigned *accepted,unsigned *rejected,unsigned *hosts) {
  pthread_barrier_t barrier;REQUIRE(pthread_barrier_init(&barrier,NULL,WORKERS*2)==0);
  Worker workers[WORKERS*2];pthread_t threads[WORKERS*2];
  for(unsigned i=0;i<WORKERS*2;++i) {
    workers[i]=(Worker){&barrier,guarded,0,0,0};
    REQUIRE(pthread_create(&threads[i],NULL,i<WORKERS?config_worker:host_worker,&workers[i])==0);
  }
  for(unsigned i=0;i<WORKERS*2;++i) {
    REQUIRE(pthread_join(threads[i],NULL)==0);
    *accepted+=workers[i].accepted;*rejected+=workers[i].rejected;*hosts+=workers[i].hosts;
  }
  REQUIRE(pthread_barrier_destroy(&barrier)==0);
}
static CBCHost textured(const char *root) {
  CBCHost host=0;CHECK(cbc_host_create(&host_config,&host));
  CBCNode mesh=0,material=0,output=0;uint64_t count=0;fixture(host,&mesh);
  CHECK(cbc_host_get_nodes(host,mesh,"used_shaders",&material,1,&count));REQUIRE(count==1);
  CBCNode diffuse=shader_node(host,material,"diffuse_bsdf"),texture=shader_node(host,material,"image_texture"),uv=shader_node(host,material,"uvmap");
  CHECK(cbc_host_shader_output(host,material,&output));scalar(host,diffuse,"roughness",.3f);
  CHECK(cbc_host_connect(host,uv,"UV",texture,"Vector"));CHECK(cbc_host_connect(host,texture,"Color",diffuse,"Color"));CHECK(cbc_host_connect(host,diffuse,"BSDF",output,"Surface"));
  CHECK(cbc_host_set_string(host,texture,"interpolation","linear"));CHECK(cbc_host_set_string(host,texture,"extension","periodic"));
  CHECK(cbc_host_set_string(host,texture,"colorspace","cbc_scaled"));CHECK(cbc_host_set_string(host,texture,"alpha_type","channel_packed"));
  CBCAttributeConfig attr={sizeof(attr),CBC_VALUE_POINT2,CBC_ATTR_CORNER,CBC_ATTR_STANDARD,{0}};
  const float coords[]={-.3f,-.2f,1.7f,.1f,.1f,1.4f};CHECK(cbc_host_attribute_floats(host,mesh,"uv",&attr,coords,6));
  char path[4096];REQUIRE(snprintf(path,sizeof(path),"%s/input.5.bin",root)>0);
  unsigned char pixels[7*5*4];FILE *file=fopen(path,"rb");REQUIRE(file);
  REQUIRE(fread(pixels,1,sizeof(pixels),file)==sizeof(pixels));REQUIRE(fclose(file)==0);
  CBCImageConfig config={sizeof(config),7,5,CBC_IMAGE_BYTE4,{0}};CBCImage image=0;
  CHECK(cbc_host_image_create(host,&config,pixels,sizeof(pixels),&image));
  CHECK(cbc_host_image_bind(host,texture,image));CHECK(cbc_host_image_release(host,image));return host;
}
static void write_film(CBCHost host,const char *out,const char *name) {
  CBCRenderConfig render={sizeof(render),32,24,8,{0}};float pixels[FLOAT_COUNT];
  CHECK(cbc_host_render(host,&render,pixels,FLOAT_COUNT));
  for(unsigned i=0;i<FLOAT_COUNT;++i)REQUIRE(isfinite(pixels[i]));
  char path[4096];REQUIRE(snprintf(path,sizeof(path),"%s/%s.rgba.bin",out,name)>0);
  FILE *file=fopen(path,"wb");REQUIRE(file);
  REQUIRE(fwrite(pixels,sizeof(float),FLOAT_COUNT,file)==FLOAT_COUNT);REQUIRE(fclose(file)==0);
}
typedef struct Closing { CBCHost host; CBCNode camera; int status; } Closing;
static void *destroy_worker(void *pointer) { Closing *worker=pointer;worker->status=cbc_host_destroy(worker->host);return NULL; }
static void *read_worker(void *pointer) {
  Closing *worker=pointer;float value=-99;uint64_t count=0;
  worker->status=cbc_host_get_floats(worker->host,worker->camera,"fov",&value,1,&count);
  REQUIRE(worker->status==CBC_OK || worker->status==CBC_INVALID || worker->status==CBC_NOT_READY);
  REQUIRE(worker->status==CBC_OK ? (value==1.1f && count==1) : value==-99);return NULL;
}
int main(int argc,char **argv) {
  REQUIRE(argc==3);
  REQUIRE(snprintf(config_a,sizeof(config_a),"%s/a.ocio",argv[1])>0);
  REQUIRE(snprintf(config_b,sizeof(config_b),"%s/b.ocio",argv[1])>0);
  CHECK(cbc_host_color_config_path(config_a));CBCHost guard=textured(argv[1]);
  unsigned guarded_accepted=0,guarded_rejected=0,hosts=0,race_accepted=0,race_rejected=0;
  run_race(1,&guarded_accepted,&guarded_rejected,&hosts);
  REQUIRE(guarded_accepted==0 && guarded_rejected==WORKERS*CONFIG_ROUNDS);
  write_film(guard,argv[2],"guarded");CHECK(cbc_host_destroy(guard));
  run_race(0,&race_accepted,&race_rejected,&hosts);
  REQUIRE(hosts==2*WORKERS*HOST_ROUNDS);
  unsigned closing_rejections=0,removed_rejections=0,readers=0;
  for(unsigned i=0;i<DESTROY_ROUNDS;++i) {
    CHECK(cbc_host_color_config_path(config_a));CBCHost host=textured(argv[1]);
    CBCNode camera=singleton(host,"camera");
    RenderThread render={host,{sizeof(CBCRenderConfig),32,24,1000000,{0}},{0},-1};
    for(unsigned j=0;j<FLOAT_COUNT;++j)render.pixels[j]=-99;
    pthread_t rendering;REQUIRE(pthread_create(&rendering,NULL,render_worker,&render)==0);
    wait_active(host);REQUIRE(cbc_host_color_config_path(config_b)==CBC_NOT_READY);
    Closing waiting[WORKERS];pthread_t reads[WORKERS];
    for(unsigned j=0;j<WORKERS;++j) {
      waiting[j]=(Closing){host,camera,-1};REQUIRE(pthread_create(&reads[j],NULL,read_worker,&waiting[j])==0);
    }
    Closing closing={host,camera,-1};pthread_t destroying;
    REQUIRE(pthread_create(&destroying,NULL,destroy_worker,&closing)==0);
    const struct timespec delay={0,100000};int configured=0;
    for(unsigned attempt=0;attempt<300000;++attempt) {
      uint32_t active=0;const int visible=cbc_host_is_rendering(host,&active);
      REQUIRE(visible==CBC_OK || visible==CBC_INVALID);
      const int status=cbc_host_color_config_path(config_b);
      REQUIRE(status==CBC_OK || status==CBC_NOT_READY);
      if(status==CBC_OK) { REQUIRE(cbc_host_is_rendering(host,&active)==CBC_INVALID);configured=1;break; }
      ++closing_rejections;removed_rejections+=visible==CBC_INVALID;nanosleep(&delay,NULL);
    }
    REQUIRE(configured);REQUIRE(pthread_join(destroying,NULL)==0);CHECK(closing.status);
    REQUIRE(pthread_join(rendering,NULL)==0);REQUIRE(render.status==CBC_CANCELLED);
    for(unsigned j=0;j<FLOAT_COUNT;++j)REQUIRE(render.pixels[j]==-99);
    for(unsigned j=0;j<WORKERS;++j) { REQUIRE(pthread_join(reads[j],NULL)==0);++readers; }
  }
  CHECK(cbc_host_color_config_path(config_a));
  char missing[4096];REQUIRE(snprintf(missing,sizeof(missing),"%s/missing.ocio",argv[1])>0);
  REQUIRE(cbc_host_color_config_path(missing)==CBC_FAILED);
  guard=textured(argv[1]);write_film(guard,argv[2],"after-invalid");CHECK(cbc_host_destroy(guard));
  CHECK(cbc_host_color_config_path(config_a));
  printf("{\"passed\":true,\"guardedConfigurationRejections\":%u,\"racingConfigurationsAccepted\":%u,\"racingConfigurationsRejected\":%u,\"concurrentHostLifetimes\":%u,\"hostLiveConfigurationRejections\":%u,\"destroyDuringRender\":%u,\"racingReaders\":%u,\"closingConfigurationRejections\":%u,\"postRemovalConfigurationRejections\":%u,\"cancelledOutputUntouched\":true,\"films\":2}\n",guarded_rejected,race_accepted,race_rejected,hosts,hosts,DESTROY_ROUNDS,readers,closing_rejections,removed_rejections);
  return 0;
}
