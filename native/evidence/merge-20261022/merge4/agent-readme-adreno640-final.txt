NOT SUPPORTED ON ADRENO 640 (PIXEL 4 XL CLASS DEVICES)
======================================================
The Adreno 640 class is below this package's performance floor. The lists
below are what a triage on one such device found; anything not listed is
untested there, not supported.
Android devices with a Qualcomm Adreno 640 GPU (for example the Pixel 4 XL) are
not reference devices for this package. The ARM64 Android reference is an
Adreno 730 device. The policy:
- functionality that works on the reference device but fails on an Adreno 640
  is not supported there;
- functionality that takes more than three times as long as on the reference
  device is treated as a failure and is not supported there either.
The package does not block these features on an Adreno 640. They may render
wrongly, stop with a WebGPU error or a lost device, or take too long. Do not
build a product that depends on them on this GPU. Maintainers: the evidence is
in native/DEVICE.md, "Pixel 4 XL triage under the device reference policy".
This list says nothing about other GPU families (Mali, other Adreno
generations).

Not supported (fails on the Adreno 640):
- Transparent shadows and Transparent BSDF lighting. The transparent-shadow
  stage reports invalid data, and renders with transparent surfaces lose the
  device.
- Object motion blur (moving objects). It either fails or takes more than three
  times the reference device's time.
- Deformation motion blur (vertex motion). The render reports an invalid SVM
  stack.
- World (background) shaders built from texture, colour or utility nodes. The
  world evaluation reports an invalid SVM stack.
- Texture, colour and utility nodes in surface shaders. Renders report an
  invalid SVM stack, and evaluating their shader programs takes more than three
  times the reference device's time.

Not supported (more than three times the reference device's time):
- Emissive mesh (triangle) lights, for rendering and for light sampling.
- Point, sun and mesh lighting mixed with transmission and forward-hit MIS in
  one scene.
- Shadow terminator geometry and shading offsets.
- Principled BSDF materials: shader evaluation, bounces and renders each take
  more than three times the reference device's time.
- Scenes with motion-blur-capable objects or deforming meshes, even when
  rendered without motion.
- Managed .NET WebGPU rendering of simple scenes (glass, diffuse sun, textured
  plane) is more than three times slower than on the reference device.
  Pipeline compilation accounts for most of the difference.

Supported on the Adreno 640 within the policy:
- perspective cameras, static and with camera motion;
- closest-hit and shadow ray traversal;
- glass;
- point, sun, spot and area lighting with MIS;
- point-lamp emission;
- homogeneous volumes: absorption, isotropic, forward and backward scattering,
  closed and overlapping volumes, and a camera inside a volume.
Pipeline compilation is still much slower than on the reference device. Expect
compile-time warnings over the 20 s mobile budget.


WHAT THIS PACKAGE DOES NOT DO
