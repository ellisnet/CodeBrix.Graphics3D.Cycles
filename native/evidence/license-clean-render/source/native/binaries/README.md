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
does not imply a completed port. There are no release binaries here yet.

Each delivery must carry source/dependency manifest hashes, toolchain and build
options, binary hashes, a passing dependency/license audit and the applicable
notices. Record which stripped runtime binary corresponds to each unstripped
binary or symbol file. Configure source/binary audit approval for that exact
reviewed payload before adding it; arbitrary compiled files remain rejected.

This location is deliberately unignored. Build work stays in ignored `work/`,
`bin/` and `obj/` directories. NuGet payloads should include the selected runtime
artifacts; retaining unstripped/symbol artifacts in Git does not require bundling
them in every runtime package. Jeremy handles commits and pushes.
