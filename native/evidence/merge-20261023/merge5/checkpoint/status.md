# Merge wave 5 checkpoint 2026-10-23 00:50:59

SEALED. Report written. Nothing in the brief undone (native Debug configuration does not exist: decision 1).

changes.patch sha256 82098bbc78773d17331b75a937f83fbf0f3cf2a2635331c35df366981e9e7223

## timeline (tail)
[00:24:47] END b-probe-attribute exit=0 18s
[00:24:47] START b-probe-image
[00:25:10] END b-probe-image exit=0 23s
[00:25:10] START b-probe-image_file
[00:25:11] END b-probe-image_file exit=0 1s
[00:25:11] START b-probe-shader_data
[00:25:16] END b-probe-shader_data exit=0 5s
[00:25:16] slotB DONE
[00:26:08] checkpoint written (4367cd956d41b37e)
[00:26:23] scratch: removed bounce/*/*/shader-cache (Mesa scratch caches, no file over 5 MB); build trees, reference export, captures and validation outputs KEPT for part 2
[00:27:41] DOC FIX after the gates: on the merged tree node render shade_background = 1,095,718 bytes (z0 measured 1,126,723 before S2/A650), so no gate warns at 1,110,000; my earlier wording (pipelines.h comment, MAINTAINER-README, DEVICE.md) said it warns -> corrected. pipelines.h comment edit keeps the line count; Linux + Android rebuilt to refresh build records, binaries hash-compared
[00:33:38] android gate build android-arm64 exit=0 348s
[00:34:00] review android-arm64 exit=0
[00:36:14] b4 Linux build exit=0 504s
[00:36:42] b4 (after doc fix): 57/57 Linux binaries byte-identical to b3 (validation/linux-bin-sha256-b3.txt vs -b4.txt), so every Linux gate result above stands for the final tree
[00:39:25] android gate build android-x64 exit=0 325s
[00:39:46] review android-x64 exit=0
[00:42:14] android host build android-x64 exit=0 148s
[00:44:43] android host build android-arm64 exit=0 149s
[00:44:43] android builds DONE
[00:45:25] Android rebuild after doc fix: staged arm64 67/67 and x64 67/67 binaries byte-identical to the first build (validation/staged-binaries-*.pre-docfix.json); host builds redone (exit 0)
[00:47:12] APK rebuild exit=0 107s
[00:47:29] APK rebuilt on the final tree: android-app/validation-m5b (apk d6b0b510..., 56 native payloads byte-identical to validation-m5; the APK file differs by packaging/signing); validation-m5b is the one for part 2
[00:48:38] checkpoint written (82098bbc78773d17)
[00:50:57] SEALED: reconciliation.md, post-merge.json (fff21328...), handoff/wave5.patch (82098bbc...), report ~/ClaudeHome/cycles-wgsl-2026-10-22/agent-reports/merge-wave-5-merge.md; git status 68 M / 16 ??
