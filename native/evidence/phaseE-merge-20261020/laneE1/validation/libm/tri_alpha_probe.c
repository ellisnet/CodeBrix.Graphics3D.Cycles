/* Copyright (c) 2026 Jeremy Ellis and contributors
 * SPDX-License-Identifier: Apache-2.0
 * Phase E1 diagnostic only: the spherical-triangle angle alpha of the triangle
 * sampler gate's failing samples, with the host acosf and a correctly rounded
 * arccosine (vertices as the fixture places them; inputs as the gate builds them). */
#include <math.h>
#include <stdio.h>
typedef struct {float x,y,z;} f3;
static f3 sub(f3 a,f3 b){return (f3){a.x-b.x,a.y-b.y,a.z-b.z};}
static float dot(f3 a,f3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static f3 cross(f3 a,f3 b){return (f3){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static f3 sn(f3 a){float t=sqrtf(dot(a,a));if(t==0.f)return a;float r=1.f/t;return (f3){a.x*r,a.y*r,a.z*r};}
int main(void){
  const int samples[]={3880,940,93,662,2640};const int objects[]={0,1,5,0,0};
  for(int k=0;k<5;k++){int i=samples[k],o=objects[k];
    volatile float px=(float)(i%23)-5.f,py=(float)((i/23)%17)*.5f-4.f,pz=4.f-(.1f+(float)(i%31)*.2f);f3 P={px,py,pz};
    const f3 v[3]={{-1.1f,-.8f,0},{1.3f,-.7f,0},{-.2f,1.2f,0}};
    const float sx=(o%2)?-1.2f:1.f,sy=.7f+.2f*o,tx=o*2.8f-6;f3 V[3];
    for(int j=0;j<3;j++)V[j]=(f3){v[j].x*sx+tx,v[j].y*sy,4.f};
    const f3 A=sn(sub(V[0],P)),B=sn(sub(V[1],P)),C=sn(sub(V[2],P));
    const float ca=dot(sn(cross(A,B)),sn(cross(A,C)));
    const float host=acosf(ca),cr=(float)acos((double)ca);
    printf("sample %d object %d cos_alpha %a host %a cr %a %s\n",i,o,ca,host,cr,host==cr?"same":"DIFFER");
  }
  return 0;
}
