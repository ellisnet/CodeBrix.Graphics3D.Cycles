# WGSL release license work

Copyright (c) 2026 Jeremy Ellis and contributors

SPDX-License-Identifier: Apache-2.0

Jeremy's `/home/jeremy/ClaudeHome/Apache-2.0 License Plan for Cycles Build.md`
(October 18, 2026) was read for this work. This is a tracking note, not a
license clearance or a copy of that plan. The source of the detailed policy
remains Jeremy's document. Current provenance and unresolved findings are in
[THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt) and
[third_party/provenance.json](third_party/provenance.json).

The policy applies to source and tools proposed for GitHub, compiled native
libraries and dependencies, generated WGSL, assets, and NuGet contents.
Third-party permissive code keeps its own license and required notices.
An unresolved license is not an approved dependency.

The following work remains required before release:

- Resolve the CPU/host atomic-header blocker. Jeremy is handling this issue.
  Any replacement must be written from required signatures and behavior,
  without editing, translating, or paraphrasing the excluded implementation.
  Rebuild and verify the CPU reference under multithreaded execution afterward.
- Produce a filtered source import and build setup that excludes prohibited
  or unresolved files, including the identified build/maintenance scripts.
  Do not copy excluded files into source snapshots or evidence bundles.
- Build and inventory pinned dependencies with unwanted optional components
  explicitly disabled; reconcile actual binary linkage and runtime files
  with the manifest for each shipping RID. Development dependency inventories
  do not establish release clearance for all platforms.
- Complete root NOTICE and generated package notices, including required
  upstream notices and modified-file statements for translated kernels.
- Add source/license checks with reviewed exceptions for legitimate notice
  text, historical findings, permissive dual-license choices and permitted
  runtime exceptions. A keyword occurrence alone does not identify the license
  governing a shipped implementation.
- Inspect Git history and the actual proposed GitHub contents, including
  evidence and generated files. Any history rewrite is Jeremy's operation;
  no commit, push, or history rewrite is performed by this session.
- Unpack and scan each candidate NuGet package, compare every payload with
  the manifest, and archive the source/dependency/binary/package reports.

The existing development host still includes the flagged upstream atomic
headers. Passing rendering tests does not clear that host for distribution.
Historical evidence stays intact; it is not automatically approved for a
public source or package payload.

This session implements WGSL only. Apple Silicon Metal development remains
outside its scope. The unchanged binaries shipped by CodeBrix.WebGpu.Native
remain the WebGPU runtime requirement, and Android API 33 remains the floor.
The requested Windows ARM64 and Linux RISC-V64 targets remain in scope even
though the plan's initial platform table omits them. Its minimal glTF build
configuration is an initial dependency profile, not a reduction of the
ongoing full Cycles parity objective. Additional features require their own
dependency review before being enabled.
