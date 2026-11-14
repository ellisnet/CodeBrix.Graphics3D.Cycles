// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

/* Written by tools/licensing/d65_numbers.py (source: tools/licensing/replacements/tables_d65_codebrix.h);
 * included by kernel/tables.h, inside its namespace, and by nothing else.
 *
 * The numbers of cie_d65_spd were copied from the array CIE_Illum_D6500 of spectrum.cpp. The unchanged copy of
 * that file in this repository is third_party/pbrt-v4/src/pbrt/util/spectrum.cpp. Only the numbers were
 * copied. The complete origin record is in THIRD-PARTY-NOTICES.txt at the repository root. */

/* CIE_D65_NORMALIZATION = 80 / sum over the 80 intervals of (2 y0 s0 + y0 s1 + y1 s0 + 2 y1 s1) / 6, with y the
 * second column of cie_color_match and s cie_d65_spd: the mean over 380 to 780 nm of the product of the two
 * linearly interpolated tables, times this constant, is 1. Computed by the same script. */
#define CIE_D65_NORMALIZATION 0.037854508f

/* 81 values, 380 to 780 nm, 5 nm steps. */
ccl_inline_constant float cie_d65_spd[] = {
  49.975498f, 52.311798f, 54.648201f, 68.701500f, 82.754898f, 87.120399f,
  91.486000f, 92.458900f, 93.431801f, 90.056999f, 86.682297f, 95.773598f,
  104.864998f, 110.935997f, 117.008003f, 117.410004f, 117.811996f, 116.335999f,
  114.861000f, 115.391998f, 115.922997f, 112.366997f, 108.810997f, 109.082001f,
  109.353996f, 108.578003f, 107.802002f, 106.295998f, 104.790001f, 106.238998f,
  107.689003f, 106.046997f, 104.404999f, 104.224998f, 104.045998f, 102.023003f,
  100.000000f, 98.167099f, 96.334198f, 96.061096f, 95.788002f, 92.236801f,
  88.685600f, 89.345901f, 90.006203f, 89.802597f, 89.599098f, 88.648903f,
  87.698700f, 85.493599f, 83.288597f, 83.493896f, 83.699203f, 81.862999f,
  80.026802f, 80.120697f, 80.214600f, 81.246201f, 82.277802f, 80.280998f,
  78.284203f, 74.002701f, 69.721298f, 70.665199f, 71.609100f, 72.978996f,
  74.348999f, 67.976501f, 61.604000f, 65.744797f, 69.885597f, 72.486298f,
  75.086998f, 69.339798f, 63.592701f, 55.005402f, 46.418201f, 56.611801f,
  66.805397f, 65.094101f, 63.382801f,
};
