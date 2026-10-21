/* Copyright (c) 2026 Jeremy Ellis and contributors
 * SPDX-License-Identifier: Apache-2.0
 * Phase E1 diagnostic only (never linked into the product): counts where the
 * host C library's acosf/asinf differ from the correctly rounded value
 * (float)acos((double)x) over every binary32 input in [-1, 1], and prints a
 * deterministic digest of the host results so two hosts can be compared. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static float f(uint32_t u){float x;memcpy(&x,&u,4);return x;}
static uint32_t u(float x){uint32_t v;memcpy(&v,&x,4);return v;}
typedef float (*fn)(float);typedef double (*dfn)(double);
static void scan(const char *name,fn single,dfn wide){
  uint64_t total=0,miss=0,hash=1469598103934665603ull;uint32_t shown=0;
  for(int sign=0;sign<2;sign++)for(uint32_t b=0;b<=0x3f800000u;b++){
    const uint32_t bits=b|(sign?0x80000000u:0);const float x=f(bits);
    const float got=single(x);const float cr=(float)wide((double)x);total++;
    hash=(hash^u(got))*1099511628211ull;
    if(u(got)!=u(cr)){miss++;if(shown<6){shown++;printf("  %s(%08x) host %08x cr %08x\n",name,bits,u(got),u(cr));}}
  }
  printf("%s total %llu differ_from_cr %llu digest %016llx\n",name,(unsigned long long)total,(unsigned long long)miss,(unsigned long long)hash);
}
int main(void){scan("acosf",acosf,acos);scan("asinf",asinf,asin);return 0;}
