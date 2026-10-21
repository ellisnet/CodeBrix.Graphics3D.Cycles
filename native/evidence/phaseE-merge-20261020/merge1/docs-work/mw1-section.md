## Merged tree on the devices (Phase E merge, 2026-10-20)

The Phase E changes above (emissive-triangle compile load, Pixel function-scope arrays, host edits and
shadow terminator, validation reference policy, exact traversal) were merged and validated together. The
runtime stayed the unchanged 1.0.290.1433 package; the shipped `libcodebrix_wgpu.so` was verified by hash
on each device before every corpus. Every sampler and image gate was judged with the device-scene flow of
the validation reference policy (glibc reference computed on Linux x64 from the device's own prepared
scene; the device's bionic CPU result is a diagnostic). Lockstep stage checks and state gates (surface,
closest, shadow, camera, volume, volume_boundary, transparent_shadow) still compare against the device's
own CPU. Every device ran the full tier, decided by the known-adapter table, with no tier warning.

**Linux x64.** Clean build; the maintained all-lanes corpus, the host gate, `dotnet test`, the
triangle gate with six images (static instanced emitter included), the closest/shadow
precision fixtures, the shadow-terminator and scene-edit gates and the motion, volume and
feature-combination gates all pass. Every number equals the lanes' own final runs, apart from 1-2
strict differences in atomic-accumulation cases whose own repeat films differ within the run. The
glass_render film is bit-identical with specialization on and off
(`a072683ce5d76309bdd93f81cc18a8c924c7a71260dd44c51c43871f7b0f851a`; the exact traversal changed it
from the earlier `a8e69a34...`).

**Samsung SM-X700 (Adreno 730).**
- Triangle image mode 2 now passes. The Linux mode-2 capture that failed at command 574 replays
  within tolerance (415 readbacks, max 6.41e-7), and the native mode-2 image passes against the glibc
  reference (0 practical, max 1.69e-5). The shadow-ray decision now follows the CPU reference: its
  bounce-0 barycentric is integer-exact (see "Exact traversal and intersection arithmetic").
- Static instanced triangle emitters (image mode 4) fail: 9 practical differences against the glibc
  reference (max 0.0127). The device's own ARM64 CPU result also differs from glibc there (6
  practical). The Linux mode-4 capture diverges at command 438, the queue count after surface
  shading (`shade_surface_light`/`connect_surface_light`/`shade_surface`). Modes 0-3 pass. Mode 5
  was not reached natively (the gate stops at the first failure); its Linux capture replays.
- The triangle sampler still fails on the deforming emitter only (41 practical; ARM64 float3
  arithmetic), as before.
- These pass against the glibc reference:
  - the emitter probe;
  - broad lighting, feature combination, lighting MIS and transparent lighting (0 practical in each);
  - the shadow-terminator gate (10 cases, 195 strict) and its 10 device replays;
  - E3's 10 Linux shadow-terminator captures replayed on the device;
  - glass_render (97 image strict);
  - object and deforming motion images.
- Finite and closed volumes, the volume-boundary state test, camera static/motion and the
  transparent-shadow gate pass against the device CPU.
- The closest/shadow precision fixtures reproduce the device CPU bit for bit (0 bit mismatches, 0
  shadow decision mismatches).
- The managed app (glass, diffuse+sun with the default shadow-terminator offset, textured plane)
  passes against the Linux films: 0 practical, max 1.38e-5.

**HP Android x64 (Intel Xe, Mesa 24.0.8).**
- The emissive-triangle corpus passes completely against the glibc reference. The sampler passes
  (0 practical, 865 strict; the bionic CPU differs from glibc by 32 practical, the earlier failure).
  All six images pass, including the static instanced emitter and the moving/deforming emitter (0
  practical), with six device replays.
- Mode 5 and the sampler probe now compile within the Scudo limit.
- The emitter probe, the shadow-terminator gate and its replays, glass_render, lighting MIS,
  transparent lighting, broad lighting, feature combination, volumes and motion pass.
- Precision fixtures: 0 bit mismatches, 0 shadow decision mismatches.
- The six re-recorded Linux triangle captures replay with the same strict counts as Linux's own replays,
  and the ten shadow-terminator captures replay.
- The managed app passes against the Linux films: 0 practical, max 2.55e-5, no capability warning.

**Pixel 4 XL (Adreno 640, old-hardware gauge; 20 s compile budget, warnings only).** One confirmation
run per corpus.
- The formerly failing E2 corpora pass against the glibc reference with their replays: feature
  combination (0 practical), lighting MIS, the four object/deforming motion images, the
  volume-boundary state test, the camera-inside closed volume, and camera static/motion.
- E3's 10 Linux shadow-terminator captures replay within tolerance.
- Triangle images 0-3 pass, and Linux captures 0-3 and 5 replay. Mode 4 fails exactly as on the
  Samsung (same component, same values), and the mode-4 Linux capture diverges at the same command
  438.
- The triangle sampler gate fails with a garbage GPU error mask: a float value is written into the
  error word, which is an out-of-bounds store. The sampler module holds E1's per-invocation
  `triangle_light_cache`, a module-scope array of structs. Function- and private-scope arrays of
  structs are the Adreno 640 defect class described in "Pixel 4 XL (Adreno 640) function-scope array
  fix". The sampler had not run on the Pixel before.
- Compile-budget warnings (over 20 s; warnings only, no tier change): `intersect_closest` 86-87 s and
  `intersect_shadow` 74-83 s in the motion images, `prepare_surface` 48-50 s, emissive-triangle
  `shade_surface_light` 65 s, `init_camera` 27 s, `shade_volume` 27 s, and `shade_surface` 21-23 s.
- The closest/shadow precision fixtures reproduce the device CPU bit for bit.
- The managed app passes against the Linux films (0 practical, max 1.38e-5) and reports the over-budget
  compiles as capability warnings. App time was 421 s (128 s on the Samsung, 197 s on the HP).

**Gate binaries and failed device creation.** A gate whose WebGPU device cannot be created now exits
with status 1. It prints `<gate> device creation: WebGPU device creation failed: <reason>` instead of
crashing on the failed cast (`require_webgpu_device`, `native/cycles/device.h`). Verified on both
Android devices: the specialization-off refusal, which previously ended the gate with exit 139, now
exits 1 with that message.

