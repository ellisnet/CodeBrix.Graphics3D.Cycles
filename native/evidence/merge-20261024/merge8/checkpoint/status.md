# Merge wave 8 checkpoint 2026-10-24 10:00:24

SEALED parts 1a+1b. wave8.patch 7acb8f49 (96 files vs 139b24f0).

changes.patch sha256 7acb8f49fde01ed6729816803af5a3252eef34325c1f7cafa4a29874827c71cc (96 files)

## timeline (tail)
[09:50:40] START b8f-compare-shadow-catcher_catcher-approximate
[09:50:40] END b8f-compare-shadow-catcher_catcher-approximate exit=0 0s
[09:50:40] START b8f-compare-ao_fast-gi-additive
[09:50:40] END b8f-compare-ao_fast-gi-additive exit=0 0s
[09:50:40] START b8f-compare-ao_ao-node
[09:50:40] END b8f-compare-ao_ao-node exit=0 0s
[09:50:40] START b8f-compare-mnee_glass-sphere-caustic
[09:50:40] END b8f-compare-mnee_glass-sphere-caustic exit=0 0s
[09:50:40] slot1b_F2 DONE
[09:51:55] START z-audit
[09:52:55] END z-audit exit=0 60s
[09:52:55] START z-notices
[09:52:55] END z-notices exit=0 0s
[09:52:55] START z-test-inventory
[09:52:55] END z-test-inventory exit=0 0s
[09:52:55] START z-inventory-check
[09:52:55] END z-inventory-check exit=1 0s
[09:52:56] android gate build android-arm64 run 3 exit=0 65s
[09:53:22] review android-arm64 run 3 exit=0
[09:54:27] android gate build android-x64 run 3 exit=0 65s
[09:54:52] review android-x64 run 3 exit=0
[09:55:52] android host build android-x64 run 3 exit=0 60s
[09:56:51] android host build android-arm64 run 3 exit=0 59s
[09:56:51] android builds run 3 DONE
[09:59:15] APK build exit=0 130s
