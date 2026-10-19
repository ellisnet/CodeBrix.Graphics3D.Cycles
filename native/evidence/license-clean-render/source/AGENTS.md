# Standing instructions

- Never create a Git commit or perform `git push`. Leave changes uncommitted
  for Jeremy to review.
- Use exactly `Copyright (c) 2026 Jeremy Ellis and contributors` for original
  contributions; preserve required third-party copyright and license notices.
- Maintain the root `THIRD-PARTY-NOTICES.txt` when adding or upgrading imported
  code or dependencies. Record origins, pinned revisions, license/notices and
  how the code is used. Tell Jeremy immediately about licensing that conflicts
  with, or is not established as compatible with, the intended Apache 2.0 release.
- Jeremy explicitly authorized independent replacements for excluded atomic
  headers and build helpers. Implement from required APIs and behavior, never
  by editing, translating or paraphrasing excluded implementation bodies.
  Preserve the existing rendering functionality and immutable historical evidence.
- All renderer, dependency and WGSL translation implementation sources needed
  for a build must be pinned, license-reviewed snapshots in this repository.
  Compilers, platform SDKs/toolchains and the unchanged shipped
  CodeBrix.WebGpu.Native package are agreed external inputs. Do not make builds
  depend on personal ClaudeHome checkouts or fetch source during configuration.
  Verify a fresh-checkout build and preserve the full nine-RID objective.
- When native RID deliverables are produced for .NET 10 assemblies/NuGet,
  preserve the intentional platform binaries in Git, including applicable
  unstripped binaries and debug symbols. Use `native/binaries/<rid>/` with
  runtime, unstripped and symbols subdirectories and reviewed provenance/hashes.
  Ordinary build intermediates remain ignored. Jeremy performs every commit/push.
