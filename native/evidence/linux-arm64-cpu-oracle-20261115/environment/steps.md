Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

# Linux ARM64 oracle re-render, commit 24b74e095f5f1f4526e095e7ac4ad6dfff62bb4c
- step0 12:36 PST: HEAD + empty status; host moved to host-0b19d875 (exit 0). See step0.txt
- step1-3: step1-3.txt (image, tools, ISPC)
- build.sh via docker exec: deps exit 0 wall 54s; cycles --device-set exit 0 wall 2564s (logs build-*.log). RUNNER find excluded host-* trees.
- render.sh: attempt1 (render-attempt1-unexported-vars.log) exit 1: RUN/RUNNER/HOSTLIB not exported into inner bash -c (my invocation error, 163s);
  attempt2 (render-attempt2-notempty.log) exit 1: output dir not empty (empty dir from attempt1), 1s;
  attempt3 (render.log) exit 0 wall 290s.
- status checks: step0.txt, step5-status.txt
