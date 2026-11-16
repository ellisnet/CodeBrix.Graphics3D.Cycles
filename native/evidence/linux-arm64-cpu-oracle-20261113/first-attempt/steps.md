Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

# Steps record (Part B, Linux ARM64 oracle 20261113)
## Step 1 start Fri Nov 13 20:51:24 PST 2026
- step1: docker pull debian:trixie (01-pull.txt), inspect (01-inspect.txt), mac (01-mac.txt): Mac mini Mac14,12 Apple M2 Pro 16GB, macOS 27.2.1
- step2: docker run -d --name cbc-arm64-oracle-20261113 ... (02-run.txt)
- step2/3 done: 02-apt.txt 02-versions.txt 03-*.txt
- step2/3 done: 02-apt.txt 02-versions.txt 03-*.txt
- step4a start Fri Nov 13 20:52:36 PST 2026: python3 tools/dependencies/build_linux_arm64.py --jobs 8 (04a-deps.txt, run-deps.sh)
- step4a done: exit 0, wall real 7m4.052s (04a-deps.txt). step4b start Fri Nov 13 20:59:45 PST 2026: build_cycles_linux_arm64.py --jobs 8 --device-set (04b-cycles.txt)
- step4b: FAILED exit=1 wall real 2m6.211s (04b-cycles.txt), clang++ AST dump in research/translate.py failed on half.h NEON types. Not retried (not a memory kill). Steps 4 RUNNER/HOSTLIB, 5, 7 NOT RUN.
