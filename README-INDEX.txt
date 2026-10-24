================================================================================
README-INDEX: CodeBrix.Graphics3D.Cycles
Map of the README files in this repository
================================================================================

If you are an AI coding agent: find the NuGet package you are consuming below
and read its AGENT-README file in full. Read MAINTAINER-README.txt only if you
are changing this repository itself.

AGENT-README FILES (consumer documentation, one per NuGet package)
------------------------------------------------------------------
  AGENT-README.txt
      CodeBrix.Graphics3D.Cycles.ApacheLicenseForever - build and render 3D
      scenes with the Cycles path-tracing renderer through a native scene host,
      on a GPU backend (Cycles' Metal device on Apple Silicon Macs, WGSL on
      WebGPU everywhere else, Intel Macs included) or an explicit CPU
      reference backend; raw pixel buffers in and out.

MAINTAINER AND EXTRAS
---------------------
  MAINTAINER-README.txt
      Standing instructions, building (native host and .NET), testing,
      packaging, versioning and provenance notes for maintainers, and the map
      of the native documentation.
  EXTRAS-README.txt
      Native source, tools, host-gate fixtures and other non-package content
      in this repository.
  docs/TESTING-MACOS-ARM64.md
      Test procedure for Apple Silicon Macs: the Metal product binary, the
      macOS host gate and the CPU-arm64 and Metal oracle bundle.
  docs/TESTING-MACOS-X64.md
      Test procedure for Intel Macs: WGSL on the shipped WebGPU runtime's
      Metal route.
  docs/TESTING-LINUX-ARM64.md
      Test procedure for the Raspberry Pi 5: WGSL on the V3D GPU and the glibc
      ARM64 reference.
  docs/TESTING-WINDOWS-X64.md
      Test procedure for Windows x64 and ARM64: WGSL on Direct3D 12 (DXC) and
      Vulkan; win-arm64 is cross-built on x64 and validated on an ARM64 device.

GENERAL
-------
  README.md
      Human-facing overview shown on GitHub and nuget.org.
  README-INDEX.txt
      This file.
  THIRD-PARTY-NOTICES.txt
      What came from where, and under which licences.

================================================================================
