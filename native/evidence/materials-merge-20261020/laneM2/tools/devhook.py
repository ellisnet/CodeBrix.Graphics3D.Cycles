# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# M2 dev only: apply/revert a non-fatal comparison hook (M2DEV=1) in the gate sources.
import sys,shutil
from pathlib import Path
C=Path('/home/jeremy/ClaudeHome/cycles-worktrees/m2-svm-nodes/native/cycles');B=Path('/home/jeremy/ClaudeHome/cycles-worktrees/m2-svm-nodes/native/work/m2/dev/hook-backup');B.mkdir(parents=True,exist_ok=True)
if sys.argv[1]=='apply':
    for n in ['surface_svm_test.h','render_gate.cpp']:shutil.copy2(C/n,B/n)
    t=(C/'surface_svm_test.h').read_text();a='''        require(std::isfinite(av)&&std::isfinite(ev)&&error<=5e-5+2e-4*std::abs(double(ev)),label+" float "+f.name'''
    assert t.count(a)==1;t=t.replace(a,'''        if(std::getenv("M2DEV")){if(!(std::isfinite(av)&&std::isfinite(ev)&&error<=5e-5+2e-4*std::abs(double(ev))))std::cerr<<"M2FAIL "<<label<<" "<<f.name<<" path "<<i<<" c"<<k/4<<" actual="<<av<<" expected="<<ev<<"\\n";}else\n'''+a);(C/'surface_svm_test.h').write_text(t)
    t=(C/'render_gate.cpp').read_text();a='''  require(std::isfinite(a)&&std::isfinite(e)&&error<=5e-5+2e-4*std::abs(double(e)),where'''
    assert t.count(a)==1;t=t.replace(a,'''  if(std::getenv("M2DEV")){if(!(std::isfinite(a)&&std::isfinite(e)&&error<=5e-5+2e-4*std::abs(double(e))))std::cerr<<"M2FAIL "<<where<<" actual="<<a<<" expected="<<e<<"\\n";return;}\n'''+a);(C/'render_gate.cpp').write_text(t)
else:
    for n in ['surface_svm_test.h','render_gate.cpp']:shutil.copy2(B/n,C/n)
