#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Copy the closing-run records into native/evidence/device-macos-arm64-20261115/closing/ (re-runnable).
cd /Volumes/VideoWork/GitHome/CodeBrix.Graphics3D.Cycles || exit 99
R=native/work/macos-arm64-closing-20261115; D=native/evidence/device-macos-arm64-20261115/closing
mkdir -p $D/consumer/logs $D/consumer/app $D/scripts
H='Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0'
cp $R/steps.md $R/phase1-summary.md $R/walls.txt $D/
for f in rule9.json metal-practical-info.json corpus-find-count.txt corpus-head.txt corpus-git-status-before.txt corpus-git-status-after.txt \
  gate-status.json; do cp $R/$f $D/$f; done
for n in corpus gate bundle stage final-pack package-check consumer; do cp $R/$n.output.txt $D/$n.output.txt; done
cp $R/pack-nugets-before.txt $D/pack-nugets-before.txt
cp $R/consumer-results.json $D/consumer/results.json; cp $R/consumer-logs/*.output.txt $D/consumer/logs/
cp native/work/consumer-macos-arm64/app/Consumer.csproj native/work/consumer-macos-arm64/app/Program.cs $D/consumer/app/
cp native/work/consumer-macos-arm64/run_matrix.py $D/consumer/run_matrix.py
for f in rule9_closing.py hash_build.py build_check.py repro_compare.py linux_compare.py; do cp $R/$f $D/scripts/$f; done
for f in run_step.sh corpus.sh step7.sh copy_closing.sh; do
  { head -1 $R/$f; printf '# %s\n' "Copyright (c) 2026 Jeremy Ellis and contributors" "SPDX-License-Identifier: Apache-2.0"; tail -n +2 $R/$f; } > $D/scripts/$f
done
