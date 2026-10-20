================================================================================
EXTRAS-README: CodeBrix.Graphics3D.Cycles
Samples, tools and other content in this repository that is not part of a NuGet
package
================================================================================

This repository ships no sample applications or demos. Exactly one .NET project
is packable - src/CodeBrix.Graphics3D.Cycles - and it is a thin wrapper over a
native renderer whose source, build tooling, validation gates and evidence also
live here. None of the content listed below is included in the
CodeBrix.Graphics3D.Cycles.ApacheLicenseForever package.

For runnable, compilable usage of the library, read the test project: the
"WORKING EXAMPLES ON GITHUB" section of AGENT-README.txt maps each feature area
to the test file that exercises it.


TEST PROJECT
============
    tests/CodeBrix.Graphics3D.Cycles.Tests/

The only non-package project in the solution. xUnit v3; run it as described in
MAINTAINER-README.txt (TESTING). Its native-host tests skip with a reason when
the native host has not been built in the working tree.


NATIVE RENDERER SOURCE
======================
    native/

The native Cycles scene host (native/host, whose cycles_host.h is the ABI the
.NET wrapper binds to), the WebGPU device and WGSL kernels, the oracle runner,
the native test drivers, every build and validation gate script
(native/tools), the reviewed RID binaries (native/binaries/<rid>/) and the
immutable gate evidence (native/evidence). native/work/ is ignored build output.


VENDORED THIRD-PARTY SOURCE
===========================
    third_party/

Pinned, license-reviewed source snapshots: the filtered Cycles snapshot and
every native dependency, with their import manifests, per-file hashes and
license texts. This is vendored upstream content, not project source: never
reformat or edit it by hand. THIRD-PARTY-NOTICES.txt and NOTICE are generated
from these manifests.


BUILD AND LICENSING TOOLS
=========================
    tools/

    build.py, build_android.py,         native build entry points for Linux x64,
    build_linux_arm64.py, build_macos.py Android, Linux ARM64 and osx-arm64
    cycles_source.py, arm_math_source.py, native_dependencies.py,
    android_toolchain.py                source and toolchain verifiers
    dependencies/                       dependency build recipes
    licensing/                          the source audit (audit.py), the
                                        notices generator (notices.py), the
                                        import, offline-build, direct-build and
                                        binary audits, and their guard tests
    compatibility/                      the compatibility inventory generator
                                        and its guard tests

HOST-GATE FIXTURES
------------------
    tools/hosttests/HostLifecycle/   tools/hosttests/ImageModes/
    tools/hosttests/ColorReload/     tools/hosttests/AndroidHost/
    tools/hosttests/NuGet.Config

Console fixtures and an Android app template that consume the .NET wrapper and
the native host. They are driven by native/tools/host_gate.py,
macos_host_gate.py and build_android_app.py, are not in the solution, and are
not family tests. MAINTAINER-README.txt (HOST-GATE FIXTURES) describes them.


COMPATIBILITY, PARITY AND RESEARCH
==================================
    compatibility/      the machine-readable Cycles compatibility inventory
    parity/             the material, real-model and integrator parity harness
                        and its evidence
    prototype/          the original native scene-to-image prototype and its
                        evidence
    research/           kernel feasibility research and its evidence
    docs/               PARITY.md (the parity ledger) and
                        RELEASE-LICENSE-PLAN.md (publication and license
                        readiness)

MAINTAINER-README.txt (MAP OF THE NATIVE DOCUMENTATION) lists the document that
describes each of these areas.


================================================================================
END OF EXTRAS-README
