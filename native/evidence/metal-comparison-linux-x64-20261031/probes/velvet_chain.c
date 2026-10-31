// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Diagnostic check: the pinned velvet value chain (bsdf_ashikhmin_velvet.h, sample) in binary32
// for one path's inputs and every fixture sigma; run normally and with the FTZ+DAZ shim.
#include <math.h>
#include <stdio.h>
typedef struct{float x,y,z;}v3;
static float dot(v3 a,v3 b){float s=a.x*b.x+a.y*b.y;return s+a.z*b.z;}
static v3 norm(v3 a){float l=sqrtf(dot(a,a));v3 r={a.x/l,a.y/l,a.z/l};return r;}
int main(int c,char**v){
  v3 N={-0.174013555f,0.4241581261f,0.8887121677f},wi={-0.4319342077f,0.2591605484f,0.863868475f},wo={-0.2314945757f,0.8879356384f,0.397467941f};
  float sig[]={.4f,1,.01f,.6f,.5f,.2f,.3f,.9f,.45f};
  for(int k=0;k<9;k++){float sigma=sig[k];float s=fmaxf(sigma,0.01f);float inv=1.0f/(s*s);
    float ni=dot(N,wi),no=dot(N,wo);v3 H=norm((v3){wi.x+wo.x,wi.y+wo.y,wi.z+wo.z});
    float nh=dot(N,H),hi=fabsf(dot(wi,H));float r=fmaxf(nh/hi,1e-5f);float f1=2*fabsf(r*ni),f2=2*fabsf(r*no);
    float sin2=1-nh*nh,sin4=sin2*sin2,cot2=(nh*nh)/sin2;
    volatile float e=expf(-cot2*inv);float t1=e*inv;float t2=t1*(float)M_1_PI;float D=t2/sin4;float G=fminf(1,fminf(f1,f2));
    float out=0.25f*(D*G)/ni;
    printf("sigma %.2f arg %.9g expf %.9g e*a %.9g D %.9g out %.9g\n",sigma,-cot2*inv,e,t1,D,out);}
}
