/* Copyright (c) 2026 Jeremy Ellis and contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Scratch probe for docs/TESTING-LINUX-ARM64-ORACLE.md section 10, items 3 and 4.
 * Valid C and C++; compile it with the CPU kernel's own compiler and flags.
 * Every input comes from argv so that the compiler cannot fold the operations.
 *
 *   ./probe inf 3.40282347e38 1e37 2147483648 4294967296 nan -- 0 0 -1
 *
 * Item 3: (int)roundf(x) for each value before "--", and the kernel's clamp of it to
 *         1..VOLUME_MAX_STEPS (1024), as in kernel/integrator/shade_volume.h.
 * Item 4: the bits of a/b and of sqrtf(c) for the three values after "--".
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t bits_of(float f)
{
  uint32_t u;
  memcpy(&u, &f, sizeof u);
  return u;
}

static int clamp_steps(int k)
{
  return k < 1 ? 1 : (k > 1024 ? 1024 : k);
}

int main(int argc, char **argv)
{
  int i = 1;
  for (; i < argc && strcmp(argv[i], "--") != 0; i++) {
    volatile float x = strtof(argv[i], NULL);
    volatile float r = roundf(x);
    int k = (int)r;
    printf("roundf-to-int input=%s bits=0x%08x roundf=%.9g int=%d (0x%08x) clamped=%d\n",
           argv[i], bits_of(x), (double)r, k, (unsigned)k, clamp_steps(k));
  }
  if (i + 3 < argc) {
    volatile float a = strtof(argv[i + 1], NULL);
    volatile float b = strtof(argv[i + 2], NULL);
    volatile float c = strtof(argv[i + 3], NULL);
    volatile float q = a / b;
    volatile float s = sqrtf(c);
    printf("nan-bits %s/%s=0x%08x sqrtf(%s)=0x%08x\n", argv[i + 1], argv[i + 2], bits_of(q), argv[i + 3],
           bits_of(s));
  }
  return 0;
}
