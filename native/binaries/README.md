# Versioned native RID deliverables

Copyright (c) 2026 Jeremy Ellis and contributors

Jeremy requires intentional native platform binaries to be checked into Git
when the .NET 10 assembly/NuGet delivery work begins. Retain applicable
unstripped builds and symbols alongside the runtime binaries:

```
native/binaries/<rid>/runtime/
native/binaries/<rid>/unstripped/
native/binaries/<rid>/symbols/
```

The nine RIDs remain win-x64, win-arm64, osx-x64, osx-arm64, linux-x64,
linux-arm64, linux-riscv64, android-arm64 and android-x64. An empty directory
does not imply a completed port. `osx-arm64` holds the first reviewed payload
(Cycles with its Metal device; see [MACOS_ARM64.md](../host/MACOS_ARM64.md));
`win-x64` and `win-arm64` hold the Windows payloads (the WGSL/WebGPU device on
Direct3D 12 with the pinned DXC compiler, or Vulkan; see
[WINDOWS_X64.md](../host/WINDOWS_X64.md)); `osx-x64` holds the Intel Mac
payload (the WGSL/WebGPU device through the runtime's Metal route; see
[MACOS_X64.md](../host/MACOS_X64.md)), validated on an Intel UHD 630 on
2026-10-26. Both Windows payloads are built on
an x64 machine: `win-arm64` is cross-compiled and carries the gate's test
executables (`tests/`) so an ARM64 device validates it without building. Windows
binaries have no symbol tables, so there is no `unstripped/` copy; their line
tables are in `symbols/*.pdb`. No NuGet release has been made.

Each delivery must carry source/dependency manifest hashes, toolchain and build
options, binary hashes, a passing dependency/license audit and the applicable
notices. Record which stripped runtime binary corresponds to each unstripped
binary or symbol file. Configure source/binary audit approval for that exact
reviewed payload before adding it; arbitrary compiled files remain rejected.
The approval is `<rid>/provenance.json`: `approvedPayload` lists the SHA-256 of
every payload file, and `snapshotCopies` names payload sources that must equal
the pinned snapshot. `tools/licensing/audit.py` rejects anything else.

This location is deliberately unignored. Build work stays in ignored `work/`,
`bin/` and `obj/` directories. NuGet payloads should include the selected runtime
artifacts; retaining unstripped/symbol artifacts in Git does not require bundling
them in every runtime package. Jeremy handles commits and pushes.
