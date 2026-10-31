// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Diagnostic tooling only (LD_PRELOAD): sets the x86 MXCSR flush-to-zero (bit 15) and
// denormals-are-zero (bit 6) bits in the process before main, so every thread the
// process creates afterwards (TBB workers, driver threads) inherits them. Used to run a
// gate's in-process CPU oracle in a flush-to-zero mode; never part of the default oracle.
#include <stdio.h>
#include <stdlib.h>
#include <xmmintrin.h>
__attribute__((constructor)) static void cbc_ftz_daz(void){
  _mm_setcsr(_mm_getcsr()|0x8040u);
  if(getenv("CBC_FTZ_DAZ_VERBOSE"))fprintf(stderr,"ftz_daz: MXCSR=0x%04x\n",_mm_getcsr());
}
