/* Copyright (c) 2026 Jeremy Ellis and contributors
 * SPDX-License-Identifier: Apache-2.0
 * Phase E1 diagnostic only: the rectangle solid angle of the emitter probe's
 * failing sample 4093 (area light 2) with the host asinf and with a correctly
 * rounded arcsine, to attribute the ARM64-oracle pdf difference. */
#include <math.h>
#include <stdio.h>
static float cr_asinf(float x){return (float)asin((double)x);}
int main(void){
  volatile float px=17.f,py=-.5f,pz=4.f-(.1f+1*.2f);
  const float dx=6.f-px,dy=0.f-py,dz=4.f-pz;float z0=dz;  /* z=(0,0,1) */
  if(z0>0.f)z0=-z0;
  const float xc=dx,yc=dy,x0=xc-.5f*1.5f,x1=xc+.5f*1.5f,y0=yc-.5f*.8f,y1=yc+.5f*.8f;
  float nz[4]={-y0,x1,y1,-x0};for(int i=0;i<4;i++)nz[i]/=sqrtf(nz[i]*nz[i]+z0*z0);
  for(int mode=0;mode<2;mode++){
    float (*as)(float)=mode?cr_asinf:asinf;
    const float g0=as(-nz[0]*nz[1]),g1=as(-nz[1]*nz[2]),g2=as(-nz[2]*nz[3]),g3=as(-nz[3]*nz[0]);
    const float S=-(g0+g1+g2+g3);
    printf("%s S=%.9g pdf=%.9g g=%a %a %a %a\n",mode?"cr   ":"host ",S,1.f/S,g0,g1,g2,g3);
  }
  return 0;
}
