// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Compiled into cycles_kernel_cpu, so with exactly the flags of the Cycles CPU
// kernels. It states the CPU device's arithmetic for the capability report and
// refuses to build an oracle that is not at the fixed baseline (native/DEVICE.md,
// "Fixed-baseline CPU oracle"). Contraction is not visible to the preprocessor:
// CODEBRIX_CPU_FP_CONTRACT comes from the same CMake setting that passes
// -ffp-contract (inject_cycles*.cmake), and the build scripts check the compile
// database. Android's CPU is a diagnostic and keeps Cycles' own flags.
#ifndef CODEBRIX_CPU_FP_CONTRACT
#error "CODEBRIX_CPU_FP_CONTRACT must be defined by the CodeBrix CMake injection"
#endif
#if defined(__RECIPROCAL_MATH__) || defined(__FAST_MATH__)
#define CODEBRIX_CPU_RECIPROCAL "on"
#else
#define CODEBRIX_CPU_RECIPROCAL "off"
#endif
#if defined(__x86_64__) || defined(_M_X64)
#if defined(__AVX__) || defined(__FMA__) || defined(__AVX2__)
#define CODEBRIX_CPU_ISA "x86-64 with AVX/FMA"
#elif defined(__SSE4_2__)
#define CODEBRIX_CPU_ISA "x86-64 SSE4.2"
#else
#define CODEBRIX_CPU_ISA "x86-64 below SSE4.2"
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
#define CODEBRIX_CPU_ISA "ARM64"
#else
#define CODEBRIX_CPU_ISA "other"
#endif
#if !defined(__ANDROID__)
#if defined(__x86_64__) && (defined(__AVX__) || defined(__FMA__) || !defined(__SSE4_2__))
#error "The x86-64 CPU oracle must be built at the SSE4.2 baseline (no AVX, no FMA)"
#endif
#if defined(__RECIPROCAL_MATH__) || defined(__FAST_MATH__)
#error "The CPU oracle must be built without reciprocal math"
#endif
#endif
extern "C" const char codebrix_cpu_arithmetic_json[] =
    "{\"isa\":\"" CODEBRIX_CPU_ISA "\",\"fpContract\":\"" CODEBRIX_CPU_FP_CONTRACT
    "\",\"reciprocalMath\":\"" CODEBRIX_CPU_RECIPROCAL "\"}";
