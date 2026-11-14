// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Diagnostic check: the pinned velvet value chain (bsdf_ashikhmin_velvet.h, sample) as a Metal
// compute kernel, for the inputs of probes/velvet_chain.c and every fixture sigma. The kernel source
// is compiled at run time with MTLCompileOptions as pinned Cycles passes them
// (third_party/cycles/src/device/metal/device_impl.mm: fastMathEnabled = YES, languageVersion 3.2 on
// macOS 15+), and once with fastMathEnabled = NO. Every input comes from a buffer so that nothing is
// folded at compile time. Two chains per sigma:
//   "cycles": the expressions of bsdf_ashikhmin_velvet_sample as they compile for Metal
//             (metal::dot, metal::normalize, fabs, fmax, fmin and expf -> fast::exp, compat.h);
//   "cprobe": the scalar chain of velvet_chain.c line for line (sqrtf -> fast::sqrt, expf -> fast::exp).
// Plus direct checks: exp of the decisive argument in fast::, precise:: and the default namespace,
// and whether subnormal values are produced, read or flushed.
// Build: xcrun --sdk macosx clang -fobjc-arc -O2 metal_velvet_probe.m -framework Metal -framework Foundation
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdint.h>
#include <string.h>

static NSString *const kSource = @
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct In { float N[3]; float wi[3]; float wo[3]; float sigma[9]; float arg; float sub; float fmin; float half_; float zero; float one;\n"
"            float gate_eval[4]; float gate_weight[4]; };\n"
"kernel void velvet(device const In &in [[buffer(0)]], device float *o [[buffer(1)]], uint t [[thread_position_in_grid]]) {\n"
"  if (t != 0) return;\n"
"  float3 N = float3(in.N[0], in.N[1], in.N[2]), wi = float3(in.wi[0], in.wi[1], in.wi[2]), wo = float3(in.wo[0], in.wo[1], in.wo[2]);\n"
"  for (int k = 0; k < 9; k++) {\n"
"    float sigma = in.sigma[k];\n"
"    /* cycles: bsdf_ashikhmin_velvet_setup + _sample (wo given) */\n"
"    { float s = fmax(sigma, 0.01f); float inv = 1.0f / (s * s);\n"
"      float3 H = normalize(wi + wo);\n"
"      float cosNI = dot(N, wi), cosNO = dot(N, wo), cosHI = fabs(dot(wi, H)), cosNH = dot(N, H);\n"
"      float r = cosNH / cosHI; r = fmax(r, 1e-5f);\n"
"      float fac1 = 2 * fabs(r * cosNI), fac2 = 2 * fabs(r * cosNO);\n"
"      float sinNH2 = 1 - cosNH * cosNH, sinNH4 = sinNH2 * sinNH2, cot2 = (cosNH * cosNH) / sinNH2;\n"
"      float a = -cot2 * inv; float e = fast::exp(a); float ei = e * inv;\n"
"      float D = fast::exp(-cot2 * inv) * inv * (0.3183098861837067f) / sinNH4;\n"
"      float G = fmin(1.0f, fmin(fac1, fac2)); float p = 0.25f * (D * G) / cosNI;\n"
"      device float *q = o + k * 12; q[0] = a; q[1] = e; q[2] = ei; q[3] = D; q[4] = p; q[5] = inv; }\n"
"    /* cprobe: velvet_chain.c */\n"
"    { float s = fmax(sigma, 0.01f); float inv = 1.0f / (s * s);\n"
"      float ni = N.x * wi.x + N.y * wi.y; ni = ni + N.z * wi.z;\n"
"      float no = N.x * wo.x + N.y * wo.y; no = no + N.z * wo.z;\n"
"      float3 h = wi + wo; float hh = h.x * h.x + h.y * h.y; hh = hh + h.z * h.z; float l = fast::sqrt(hh);\n"
"      float3 H = float3(h.x / l, h.y / l, h.z / l);\n"
"      float nh = N.x * H.x + N.y * H.y; nh = nh + N.z * H.z;\n"
"      float hi = wi.x * H.x + wi.y * H.y; hi = fabs(hi + wi.z * H.z);\n"
"      float r = fmax(nh / hi, 1e-5f); float f1 = 2 * fabs(r * ni), f2 = 2 * fabs(r * no);\n"
"      float sin2 = 1 - nh * nh, sin4 = sin2 * sin2, cot2 = (nh * nh) / sin2;\n"
"      float a = -cot2 * inv; float e = fast::exp(a); float t1 = e * inv; float t2 = t1 * 0.3183098861837067f;\n"
"      float D = t2 / sin4; float G = fmin(1.0f, fmin(f1, f2)); float p = 0.25f * (D * G) / ni;\n"
"      device float *q = o + k * 12 + 6; q[0] = a; q[1] = e; q[2] = t1; q[3] = D; q[4] = p; q[5] = inv; }\n"
"  }\n"
"  device float *d = o + 9 * 12;\n"
"  d[0] = fast::exp(in.arg); d[1] = precise::exp(in.arg); d[2] = exp(in.arg);\n"
"  d[3] = in.sub * in.one;          /* subnormal input times 1: 0 if inputs are flushed (DAZ) */\n"
"  d[4] = in.sub + in.zero;         /* subnormal input plus 0 */\n"
"  d[5] = in.fmin * in.half_;       /* FLT_MIN * 0.5: subnormal result, or 0 if results are flushed (FTZ) */\n"
"  d[6] = (in.sub != 0.0f) ? 1.0f : 0.0f; /* subnormal compared with zero */\n"
"  d[7] = fast::exp(in.arg - 1.0f); /* argument one lower: exact result 5.05e-39, subnormal */\n"
"  d[8] = precise::exp(in.arg - 1.0f);\n"
"  d[9] = in.sub;                   /* plain copy of a subnormal input, no arithmetic */\n"
"  /* velvet-gate-paths.txt: the normal-CPU BSDF values, times 1 and times their closure weight */\n"
"  for (int i = 0; i < 4; i++) { d[10 + i] = in.gate_eval[i] * in.one; d[14 + i] = in.gate_eval[i] * in.gate_weight[i]; }\n"
"}\n";

static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static const char *kind(float f) {
  if (f == 0.0f) return "zero";
  if (__builtin_isnan(f)) return "nan";
  if (__builtin_isinf(f)) return "inf";
  return (__builtin_fabsf(f) < 1.17549435e-38f) ? "subnormal" : "normal";
}

typedef struct { float N[3], wi[3], wo[3], sigma[9], arg, sub, fmin, half_, zero, one, gate_eval[4], gate_weight[4]; } In;
static const int kGatePath[4] = {534, 549, 1566, 2055};

static int run(id<MTLDevice> dev, BOOL fast) {
  MTLCompileOptions *opt = [[MTLCompileOptions alloc] init];
  opt.fastMathEnabled = fast;
  if (@available(macOS 15.0, *)) opt.languageVersion = MTLLanguageVersion3_2;
  NSString *mm = @"n/a", *fp = @"n/a";
  if (@available(macOS 15.0, *)) {
    mm = opt.mathMode == MTLMathModeFast ? @"MTLMathModeFast" : opt.mathMode == MTLMathModeRelaxed ? @"MTLMathModeRelaxed" : @"MTLMathModeSafe";
    fp = opt.mathFloatingPointFunctions == MTLMathFloatingPointFunctionsFast ? @"MTLMathFloatingPointFunctionsFast" : @"MTLMathFloatingPointFunctionsPrecise";
  }
  printf("== MTLCompileOptions: fastMathEnabled=%s languageVersion=0x%lx (3.2=0x%lx) mathMode=%s mathFloatingPointFunctions=%s\n",
         fast ? "YES" : "NO", (unsigned long)opt.languageVersion, (unsigned long)MTLLanguageVersion3_2, mm.UTF8String, fp.UTF8String);
  NSError *err = nil;
  id<MTLLibrary> lib = [dev newLibraryWithSource:kSource options:opt error:&err];
  if (!lib) { printf("compile failed: %s\n", err.localizedDescription.UTF8String); return 1; }
  if (err) printf("compile messages: %s\n", err.localizedDescription.UTF8String);
  id<MTLComputePipelineState> pso = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"velvet"] error:&err];
  if (!pso) { printf("pipeline failed: %s\n", err.localizedDescription.UTF8String); return 1; }
  In in = {{-0.174013555f, 0.4241581261f, 0.8887121677f}, {-0.4319342077f, 0.2591605484f, 0.863868475f},
           {-0.2314945757f, 0.8879356384f, 0.397467941f}, {.4f, 1, .01f, .6f, .5f, .2f, .3f, .9f, .45f},
           -87.1811905f, 1e-39f, 1.17549435e-38f, 0.5f, 0.0f, 1.0f,
           {1.54958639e-38f, 8.23442718e-38f, 9.84552301e-42f, 1.13863109e-38f}, {0.5f, 0.5f, 0.5f, 0.800000012f}};
  enum { COUNT = 9 * 12 + 18 };
  id<MTLBuffer> bi = [dev newBufferWithBytes:&in length:sizeof in options:MTLResourceStorageModeShared];
  id<MTLBuffer> bo = [dev newBufferWithLength:COUNT * sizeof(float) options:MTLResourceStorageModeShared];
  memset(bo.contents, 0xff, COUNT * sizeof(float));
  id<MTLCommandQueue> queue = [dev newCommandQueue];
  id<MTLCommandBuffer> cb = [queue commandBuffer];
  id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
  [enc setComputePipelineState:pso]; [enc setBuffer:bi offset:0 atIndex:0]; [enc setBuffer:bo offset:0 atIndex:1];
  [enc dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
  [enc endEncoding]; [cb commit]; [cb waitUntilCompleted];
  if (cb.error) { printf("dispatch failed: %s\n", cb.error.localizedDescription.UTF8String); return 1; }
  const float *o = (const float *)bo.contents;
  const char *names[] = {"arg", "expf", "e*inv", "D", "out", "invsigma2"};
  for (int chain = 0; chain < 2; chain++)
    for (int k = 0; k < 9; k++) {
      const float *q = o + k * 12 + chain * 6;
      printf("%s sigma %.2f", chain ? "cprobe" : "cycles", in.sigma[k]);
      for (int i = 0; i < 6; i++) printf(" %s %.9g", names[i], q[i]);
      printf(" | expf 0x%08x %s, out 0x%08x %s\n", bits(q[1]), kind(q[1]), bits(q[4]), kind(q[4]));
    }
  const float *d = o + 9 * 12;
  const char *dn[] = {"fast::exp(-87.1811905)", "precise::exp(-87.1811905)", "exp(-87.1811905) (default namespace)",
                      "1e-39 * 1 (subnormal input)", "1e-39 + 0 (subnormal input)", "FLT_MIN * 0.5 (subnormal result)",
                      "1e-39 != 0", "fast::exp(-88.1811905)", "precise::exp(-88.1811905)"};
  for (int i = 0; i < 9; i++) printf("direct %-40s %.9g  0x%08x %s\n", dn[i], d[i], bits(d[i]), kind(d[i]));
  printf("direct %-40s %.9g  0x%08x %s (input bits 0x%08x)\n", "1e-39 copied (no arithmetic)", d[9], bits(d[9]), kind(d[9]), bits(in.sub));
  for (int i = 0; i < 4; i++) {
    float e = in.gate_eval[i], w = in.gate_weight[i], cpu = e * w;
    printf("gate path %4d: normal-CPU eval %.9g (0x%08x %s) | Metal eval*1 %.9g (0x%08x %s) | weight %.9g: CPU eval*weight %.9g (0x%08x %s), Metal %.9g (0x%08x %s)\n",
           kGatePath[i], e, bits(e), kind(e), d[10 + i], bits(d[10 + i]), kind(d[10 + i]), w, cpu, bits(cpu), kind(cpu),
           d[14 + i], bits(d[14 + i]), kind(d[14 + i]));
  }
  return 0;
}

int main(void) {
  @autoreleasepool {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (!dev) { printf("no Metal device\n"); return 1; }
    NSOperatingSystemVersion v = NSProcessInfo.processInfo.operatingSystemVersion;
    printf("device %s, macOS %ld.%ld.%ld\n", dev.name.UTF8String, (long)v.majorVersion, (long)v.minorVersion, (long)v.patchVersion);
    printf("host reference (binary32, exact): expf(-87.1811905) = 1.37306174e-38 (normal, just above FLT_MIN 1.17549435e-38)\n");
    int rc = run(dev, YES);
    rc |= run(dev, NO);
    return rc;
  }
}
