Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

# Linux ARM64 oracle run 20261113b (records)
Commands were run as scripted in this directory (build1.sh deps, build2.sh cycles, render.sh render; probe in native/work/linux-arm64-probe-20261113/build-probe.sh). Container clock is UTC; Mac clock PST.
- step0 (22:58 PST): docker rm -f old container exit 0; mv host -> host-failed-20261113 exit 0 (step0.txt)
- step1-3 (22:58): pull, run, apt (exit 0), versions (step1-3.txt, step2-apt.txt, step2-3-versions.txt)
- deps: 06:59:08 UTC, exit 0, wall 40s (build-deps.log)
- cycles --device-set --jobs 8: 06:59:53 UTC, exit 0, wall 1764s (build-cycles.log)
- render: 07:29:41 UTC, exit 0, wall 173s (render.log; summary render-summary.txt)
- probe: probe-run.log
