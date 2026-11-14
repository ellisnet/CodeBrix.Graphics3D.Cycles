# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""The source paths of the Cycles-with-Metal profile (osx-arm64), defined once.

Payload staleness is judged per build profile. A staged payload records the commit
it was built from (provenance.json sourceCommit); tools/licensing/package_check.py
reports the payload as stale when one of its profile's paths changed after that
commit. osx-arm64 is judged by METAL_SOURCES; every other RID (Cycles-with-WGSL)
by package_check.NATIVE_SOURCES.

METAL_SOURCES is what the osx-arm64 recipe reads (tools/build_macos.py and what it
runs and imports), with the same scope as the WGSL list: the build inputs, the build
environment's recipes and the oracle runner. It is also the pathspec whose git status
native/tools/build_cycles_macos.py records with each build and
native/tools/stage_macos_binaries.py requires to be clean for a real stage.
"""

METAL_SOURCES=[
 'native/host',                                # cycles_host.cpp, host_*.inc, cycles_host.h, metal_recovery.mm, exports.macos, targets.cmake
 'native/cycles/cpu_arithmetic.cpp',           # compiled into cycles_kernel_cpu (inject_cycles_macos.cmake); the only native/cycles file read
 'native/oracle/runner.cpp',                   # the oracle runner (tools/build_macos.py), used by the stager's standalone render
 'native/tools/build_cycles_macos.py',         # the host recipe
 'native/tools/cycles_overlay.py',             # the registration overlay
 'native/tools/inject_cycles_macos.cmake',     # CMAKE_PROJECT_Cycles_INCLUDE
 'tools/build_macos.py',                       # the driver: dependencies, host, runner
 'tools/dependencies/build_macos.py',          # the dependency recipe
 'tools/dependencies/build.py',                # the shared dependency profile
 'tools/dependencies/macos_checks.py',         # isolation and Mach-O checks of both recipes
 'tools/dependencies/boost',                   # the boost project (build.py)
 'tools/dependencies/pystring',                # the pystring project (build.py)
 'tools/android_toolchain.py',                 # environment() and sha() of the dependency recipe
 'tools/cycles_source.py',                     # verify() and remove_stale_copy()
 'tools/licensing/dependency_sources.py',      # verify() of the dependency snapshots
 'third_party/cycles',                         # the copied source tree and the Metal kernel closure
 'third_party/cycles-import.json',             # the pinned import; its revision is compiled into the host
 'third_party/deps',                           # the dependency snapshots
 'third_party/dependency-imports.json',        # their manifest
]
