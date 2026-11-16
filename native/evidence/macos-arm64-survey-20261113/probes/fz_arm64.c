// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Diagnostic tooling only, the AArch64 counterpart of probes/ftz_daz.c: sets the FPCR flush-to-zero
// bit (FZ, bit 24) before main, so single- and double-precision subnormal inputs and results are
// flushed to zero for the rest of the process. Linked into a copy of probes/velvet_chain.c to run the
// velvet chain on the Mac CPU in a flush-to-zero mode; never part of the default oracle.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
__attribute__((constructor)) static void cbc_fz_arm64(void){
  uint64_t fpcr;
  __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
  fpcr |= (uint64_t)1 << 24;
  __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
  __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
  volatile float m = 1.17549435e-38f, h = 0.5f;
  fprintf(stderr, "fz_arm64: FPCR=0x%08llx, FLT_MIN*0.5 = %.9g\n", (unsigned long long)fpcr, (double)(m * h));
}
