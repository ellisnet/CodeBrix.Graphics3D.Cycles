/* Copyright (c) 2026 Jeremy Ellis and contributors
 * SPDX-License-Identifier: Apache-2.0
 * Phase E1 diagnostic only: for every failing triangle-sampler sample on a
 * static emitter (objects 0-4 of the gate fixture), the spherical-triangle angle
 * alpha = acosf(cos_alpha) with the host C library and correctly rounded. */
#include <math.h>
#include <stdio.h>
typedef struct {float x,y,z;} f3;
static f3 sub(f3 a,f3 b){return (f3){a.x-b.x,a.y-b.y,a.z-b.z};}
static float dot(f3 a,f3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static f3 cross(f3 a,f3 b){return (f3){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static f3 sn(f3 a){float t=sqrtf(dot(a,a));if(t==0.f)return a;float r=1.f/t;return (f3){a.x*r,a.y*r,a.z*r};}
int main(void){
  const int samples[][2]={{127,1},{390,2},{662,0},{687,2},{940,1},{1093,0},{1126,3},{1214,1},{1838,2},{2269,0},{2640,0},{3318,0},{3491,2},{3493,2},{3505,0},{3604,1},{3880,0},{4064,1}};
  int differ=0,n=0;
  for(unsigned k=0;k<sizeof(samples)/sizeof(samples[0]);k++){int i=samples[k][0],o=samples[k][1];
    volatile float px=(float)(i%23)-5.f,py=(float)((i/23)%17)*.5f-4.f,pz=4.f-(.1f+(float)(i%31)*.2f);f3 P={px,py,pz};
    const f3 v[3]={{-1.1f,-.8f,0},{1.3f,-.7f,0},{-.2f,1.2f,0}};
    volatile int m=o==3?2:(o==4?3:o);float sx,sy,sz,tx,ty,tz;
    if(o==3){sx=.8f;sy=1.4f;sz=.6f;tx=0;ty=2;tz=6;}else{sx=(m%2)?-1.2f:1.f;sy=.7f+.2f*m;sz=1.f;tx=m*2.8f-6;ty=0;tz=4;}
    f3 V[3];for(int j=0;j<3;j++)V[j]=(f3){v[j].x*sx+tx,v[j].y*sy+ty,v[j].z*sz+tz};
    const f3 A=sn(sub(V[0],P)),B=sn(sub(V[1],P)),C=sn(sub(V[2],P));
    const float ca=dot(sn(cross(A,B)),sn(cross(A,C)));
    const float host=acosf(ca),cr=(float)acos((double)ca);n++;differ+=host!=cr;
    printf("sample %4d object %d cos_alpha %a host %a cr %a %s\n",i,o,ca,host,cr,host==cr?"same":"DIFFER");
  }
  printf("%d of %d static failing samples: host acosf differs from correctly rounded\n",differ,n);
  return 0;
}
