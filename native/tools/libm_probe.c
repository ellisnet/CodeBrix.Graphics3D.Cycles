/* Copyright (c) 2026 Jeremy Ellis and contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * libm deviation probe (diagnostic tool, never linked into the product or a gate).
 *
 * Measures how often the host C library's binary32 functions that the pinned Cycles
 * kernel code calls differ from the correctly rounded result, and prints one JSON
 * object. The correctly rounded value is the host's binary64 function rounded once to
 * binary32; inputs whose binary64 result lies within 2^-50 (relative) of a binary32
 * rounding midpoint cannot be decided that way and are counted as "ambiguous" instead
 * of as differences. Each function also gets a digest of the host's raw results, in a
 * fixed order, so two hosts can be compared bit for bit.
 *
 * Domains: acosf and asinf every binary32 in [-1, 1] (the arguments Cycles' safe_acosf
 * and safe_asinf pass); the other unary functions every 16th binary32 bit pattern over
 * their finite domain; atan2f and powf 2^24 deterministic pseudo-random pairs.
 *
 * Usage: libm_probe [THREADS]   (writes JSON to stdout)
 */
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__GLIBC__)
#include <gnu/libc-version.h>
#endif

#define CHUNKS 256u
static float bits_to_float(uint32_t u){float x;memcpy(&x,&u,4);return x;}
static uint32_t float_to_bits(float x){uint32_t u;memcpy(&u,&x,4);return u;}
static int64_t ordered(float x){int32_t b;memcpy(&b,&x,4);return b<0?(int64_t)INT32_MIN-b:b;}

typedef float (*unary_f)(float);typedef double (*unary_d)(double);
typedef float (*binary_f)(float,float);typedef double (*binary_d)(double,double);

/* Correctly rounded candidate from the binary64 result; *ambiguous set near a midpoint. */
static float rounded(double y,int *ambiguous){
  float r=(float)y;*ambiguous=0;
  if(!isfinite(y)||y==0.0||!isfinite(r))return r;
  float n=nextafterf(r,y>(double)r?INFINITY:-INFINITY);
  if(!isfinite(n))return r;
  double mid=((double)r+(double)n)*0.5;
  if(fabs(y-mid)<=fabs(y)*0x1p-50)*ambiguous=1;
  return r;
}
static int same(float a,float b){return float_to_bits(a)==float_to_bits(b)||(isnan(a)&&isnan(b));}

typedef struct {
  const char *name,*domain;unary_f f;unary_d d;binary_f bf;binary_d bd;
  uint32_t low,high,stride;int sign_both;uint32_t pairs;float a_low,a_high,b_low,b_high;
  uint64_t total[CHUNKS],differ[CHUNKS],ambiguous[CHUNKS],hash[CHUNKS],max_ulps[CHUNKS];
  uint32_t first_input[CHUNKS],first_input_b[CHUNKS],first_host[CHUNKS],first_cr[CHUNKS];int has_first[CHUNKS];
} Probe;

static float f_acos(float x){return acosf(x);} static double d_acos(double x){return acos(x);}
static float f_asin(float x){return asinf(x);} static double d_asin(double x){return asin(x);}
static float f_atan(float x){return atanf(x);} static double d_atan(double x){return atan(x);}
static float f_sin(float x){return sinf(x);} static double d_sin(double x){return sin(x);}
static float f_cos(float x){return cosf(x);} static double d_cos(double x){return cos(x);}
static float f_tan(float x){return tanf(x);} static double d_tan(double x){return tan(x);}
static float f_exp(float x){return expf(x);} static double d_exp(double x){return exp(x);}
static float f_log(float x){return logf(x);} static double d_log(double x){return log(x);}
static float f_sqrt(float x){return sqrtf(x);} static double d_sqrt(double x){return sqrt(x);}
static float f_atan2(float y,float x){return atan2f(y,x);} static double d_atan2(double y,double x){return atan2(y,x);}
static float f_pow(float x,float y){return powf(x,y);} static double d_pow(double x,double y){return pow(x,y);}

static Probe probes[]={
  {"acosf","every binary32 in [-1,1]",f_acos,d_acos,0,0,0u,0x3f800000u,1u,1},
  {"asinf","every binary32 in [-1,1]",f_asin,d_asin,0,0,0u,0x3f800000u,1u,1},
  {"atanf","every 16th finite binary32",f_atan,d_atan,0,0,0u,0x7f7fffffu,16u,1},
  {"sinf","every 16th finite binary32",f_sin,d_sin,0,0,0u,0x7f7fffffu,16u,1},
  {"cosf","every 16th finite binary32",f_cos,d_cos,0,0,0u,0x7f7fffffu,16u,1},
  {"tanf","every 16th finite binary32",f_tan,d_tan,0,0,0u,0x7f7fffffu,16u,1},
  {"expf","every 16th finite binary32",f_exp,d_exp,0,0,0u,0x7f7fffffu,16u,1},
  {"logf","every 16th positive finite binary32",f_log,d_log,0,0,0u,0x7f7fffffu,16u,0},
  {"sqrtf","every 16th positive finite binary32",f_sqrt,d_sqrt,0,0,0u,0x7f7fffffu,16u,0},
  {"atan2f","2^24 pseudo-random (y,x) pairs in [-8,8]^2",0,0,f_atan2,d_atan2,0,0,0,0,1u<<24,-8.f,8.f,-8.f,8.f},
  {"powf","2^24 pseudo-random (x,y) pairs, x in (0,4], y in [-8,8]",0,0,f_pow,d_pow,0,0,0,0,1u<<24,0.f,4.f,-8.f,8.f},
};
#define PROBES (sizeof(probes)/sizeof(probes[0]))

static void note(Probe *p,unsigned c,float a,float b,float host,float cr){
  int64_t d=ordered(host)-ordered(cr);uint64_t u=(uint64_t)(d<0?-d:d);
  if(isfinite(host)&&isfinite(cr)&&u>p->max_ulps[c])p->max_ulps[c]=u;
  if(!p->has_first[c]){p->has_first[c]=1;p->first_input[c]=float_to_bits(a);p->first_input_b[c]=float_to_bits(b);p->first_host[c]=float_to_bits(host);p->first_cr[c]=float_to_bits(cr);}
}
static uint32_t lcg(uint64_t *s){*s=*s*6364136223846793005ull+1442695040888963407ull;return (uint32_t)(*s>>32);}
static void run_chunk(Probe *p,unsigned c){
  uint64_t total=0,differ=0,amb=0,hash=1469598103934665603ull;
  if(p->f){
    const uint64_t count=((uint64_t)p->high-p->low)/p->stride+1,begin=count*c/CHUNKS,end=count*(c+1)/CHUNKS;
    for(int sign=0;sign<(p->sign_both?2:1);sign++)for(uint64_t i=begin;i<end;i++){
      const uint32_t bits=(uint32_t)(p->low+i*p->stride)|(sign?0x80000000u:0u);const float x=bits_to_float(bits);
      const float host=p->f(x);int a;const float cr=rounded(p->d((double)x),&a);total++;
      hash=(hash^float_to_bits(host))*1099511628211ull;
      if(a){amb++;continue;}
      if(!same(host,cr)){differ++;note(p,c,x,0.f,host,cr);}
    }
  }else{
    const uint64_t begin=(uint64_t)p->pairs*c/CHUNKS,end=(uint64_t)p->pairs*(c+1)/CHUNKS;
    uint64_t state=0x9e3779b97f4a7c15ull^(uint64_t)c;
    for(uint64_t i=begin;i<end;i++){
      const float a=p->a_low+(p->a_high-p->a_low)*(float)(lcg(&state)>>8)*0x1p-24f;
      const float b=p->b_low+(p->b_high-p->b_low)*(float)(lcg(&state)>>8)*0x1p-24f;
      const float host=p->bf(a,b);int am;const float cr=rounded(p->bd((double)a,(double)b),&am);total++;
      hash=(hash^float_to_bits(host))*1099511628211ull;
      if(am){amb++;continue;}
      if(!same(host,cr)){differ++;note(p,c,a,b,host,cr);}
    }
  }
  p->total[c]=total;p->differ[c]=differ;p->ambiguous[c]=amb;p->hash[c]=hash;
}
static unsigned next_job=0;static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static void *worker(void *unused){(void)unused;
  for(;;){pthread_mutex_lock(&lock);unsigned job=next_job++;pthread_mutex_unlock(&lock);
    if(job>=PROBES*CHUNKS)return 0;run_chunk(&probes[job/CHUNKS],job%CHUNKS);}
}
int main(int argc,char **argv){
  unsigned threads=argc>1?(unsigned)atoi(argv[1]):4;if(threads<1)threads=1;if(threads>64)threads=64;
  pthread_t ids[64];for(unsigned i=0;i<threads;i++)pthread_create(&ids[i],0,worker,0);for(unsigned i=0;i<threads;i++)pthread_join(ids[i],0);
#if defined(__GLIBC__)
  const char *libc="glibc";const char *version=gnu_get_libc_version();
#elif defined(__BIONIC__)
  const char *libc="bionic";char version[32];snprintf(version,sizeof version,"API %d",__ANDROID_API__);
#else
  const char *libc="other";const char *version="";
#endif
#if defined(__aarch64__)
  const char *arch="aarch64";
#elif defined(__x86_64__)
  const char *arch="x86_64";
#else
  const char *arch="other";
#endif
  printf("{\"tool\":\"native/tools/libm_probe.c\",\"libc\":\"%s\",\"libcVersion\":\"%s\",\"architecture\":\"%s\",\"threads\":%u,"
         "\"correctlyRoundedReference\":\"host binary64 function rounded once to binary32; inputs within 2^-50 relative of a binary32 midpoint counted as ambiguous\",\"functions\":[",libc,version,arch,threads);
  for(unsigned i=0;i<PROBES;i++){Probe *p=&probes[i];uint64_t total=0,differ=0,amb=0,max_ulps=0,digest=1469598103934665603ull;int shown=0;
    for(unsigned c=0;c<CHUNKS;c++){total+=p->total[c];differ+=p->differ[c];amb+=p->ambiguous[c];if(p->max_ulps[c]>max_ulps)max_ulps=p->max_ulps[c];digest=(digest^p->hash[c])*1099511628211ull;}
    printf("%s{\"function\":\"%s\",\"domain\":\"%s\",\"inputs\":%llu,\"differFromCorrectlyRounded\":%llu,\"ambiguous\":%llu,\"maximumUlps\":%llu,\"resultDigest\":\"%016llx\",\"firstDifferences\":[",
           i?",":"",p->name,p->domain,(unsigned long long)total,(unsigned long long)differ,(unsigned long long)amb,(unsigned long long)max_ulps,(unsigned long long)digest);
    for(unsigned c=0;c<CHUNKS&&shown<4;c++)if(p->has_first[c]){
      if(p->f)printf("%s{\"x\":\"%08x\",\"host\":\"%08x\",\"correctlyRounded\":\"%08x\"}",shown?",":"",p->first_input[c],p->first_host[c],p->first_cr[c]);
      else printf("%s{\"a\":\"%08x\",\"b\":\"%08x\",\"host\":\"%08x\",\"correctlyRounded\":\"%08x\"}",shown?",":"",p->first_input[c],p->first_input_b[c],p->first_host[c],p->first_cr[c]);
      shown++;}
    printf("]}");}
  printf("]}\n");return 0;
}
