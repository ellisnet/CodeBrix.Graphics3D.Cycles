# Lane Q checkpoint status (written 16:41:15, last step: complete)
Worktree /home/jeremy/ClaudeHome/cycles-worktrees/q-s20-sweep (base 3392df3d). Chain: native/work/q/tools/chain_s20.sh [STEP ...];
steps in order: stage hostgate canary triangle emitter lighting regression transparent terminator specrefusal featurecomb motion precision volume hostcap app materials. A step whose runs/s20/<corpus>-<label>/status.json (or device/android-host-gate, android-app/webgpu-s20) exists is skipped on re-run.
Builds: Linux native/work/cycles-build; Android gates native/work/q/build-android-arm64 (staged-binaries-android-arm64.json); APK native/work/q/android-app/validation-q.
Lock: not held (s20 lock dir absent)
S20 remote folders/apps of lane Q: none

## Completed (timeline END lines)
- [13:44:42] staged exit=0
- [13:46:58] END android-host-gate exit=0 136s
- [13:47:58] staged exit=0
- [13:49:53] s20 canary-q: END passed=True wall=108s/900s failures=0
- [13:49:53] END canary exit=0 115s
- [14:15:02] s20 triangle-q: END passed=True wall=1503s/6000s failures=0
- [14:15:02] END triangle exit=0 1509s
- [14:15:46] s20 emitter-q: END passed=True wall=38s/900s failures=0
- [14:15:46] END emitter exit=0 44s
- [14:23:57] s20 lighting-q: END passed=True wall=484s/4200s failures=0
- [14:23:57] END lighting exit=0 491s
- [14:27:59] s20 regression-q: END passed=False wall=236s/2100s failures=1
- [14:27:59] END regression exit=0 242s
- [14:29:20] s20 transparent-q: END passed=False wall=75s/7800s failures=12
- [14:29:20] END transparent exit=0 81s
- [14:54:53] s20 terminator-q: END passed=True wall=1526s/7200s failures=0
- [14:54:53] END terminator exit=0 1533s
- [14:56:52] s20 spec-refusal-q: END passed=True wall=112s/1500s failures=0
- [14:56:52] END spec-refusal exit=0 118s
- [15:01:15] s20 lighting-fc: END passed=True wall=256s/2100s failures=0
- [15:01:15] END feature_combination exit=0 263s
- [15:07:03] s20 motion-q: END passed=False wall=342s/4200s failures=4
- [15:07:03] END motion exit=0 348s
- [15:08:08] s20 precision-q: END passed=True wall=58s/2700s failures=0
- [15:08:08] END precision exit=0 65s
- [15:26:42] s20 volume-q: END passed=True wall=1108s/7200s failures=0
- [15:26:42] END volume exit=0 1114s
- [15:30:56] s20 volume-cs: END passed=True wall=248s/1600s failures=0
- [15:30:56] END closed-single exit=0 254s
- [15:37:21] s20 volume-co: END passed=True wall=378s/2100s failures=0
- [15:37:21] END closed-overlap exit=0 384s
- [15:43:31] s20 volume-ci: END passed=True wall=364s/2100s failures=0
- [15:43:31] END closed-inside exit=0 370s
- [15:43:41] s20 hostcap-linux-triangle: END passed=False wall=5s/7000s
- [15:43:41] END hostcap-linux-triangle exit=1 10s
- [15:43:51] s20 hostcap-linux-terminator: END passed=False wall=5s/7000s
- [15:43:51] END hostcap-linux-terminator exit=1 10s
- [15:50:11] END app-webgpu exit=0 379s
- [16:01:31] s20 materials-pr: END passed=False wall=672s/2700s failures=4
- [16:02:21] staged exit=0
- [16:15:51] s20 hostcap-linux-triangle-q2: END passed=True wall=806s/7000s
- [16:15:51] END hostcap-linux-triangle-q2 exit=0 810s
- [16:35:54] s20 hostcap-linux-terminator-q2: END passed=True wall=1198s/7000s
- [16:35:54] END hostcap-linux-terminator-q2 exit=0 1202s
- [16:36:28] s20 materials-nsvm: END passed=False wall=28s/2900s failures=1
- [16:36:28] END nodes_svm exit=0 34s
- [16:36:50] s20 materials-nw: END passed=False wall=15s/1800s failures=1
- [16:36:50] END nodes_world exit=0 21s
- [16:37:19] s20 materials-nr: END passed=False wall=22s/1500s failures=2
- [16:37:19] END nodes_render exit=0 29s
- [16:38:14] staged exit=0
- [16:39:39] s20 transparent-iso: END passed=False wall=18s/1500s failures=3
- [16:39:39] END transparent_shadow-iso exit=0
- [16:40:59] s20 materials-nwiso: END passed=False wall=14s/1800s failures=1
- [16:40:59] END nodes_world-iso exit=0

## Jobs so far: 105 rows, 76 pass, 29 fail (table: checkpoint/sweep-table.md)

## Remaining
Every step after the last END line above; then: DEVICE.md section, report, handoff (see the lane brief).
