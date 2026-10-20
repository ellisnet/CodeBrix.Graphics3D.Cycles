# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Phase C close-out Step 2(a): Adreno 640 known-adapter entry -> full tier with a 20 s compile
# budget (Jeremy's decision, applied only after the Pixel full-tier measurement passed).
from pathlib import Path
R = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles')
def sub(path, old, new):
    p = R/path; s = p.read_text(); assert s.count(old) == 1, (path, old[:60]); p.write_text(s.replace(old, new))
sub('native/cycles/device_tier.cpp',
 '''    {"Pixel 4 XL Adreno 640","Adreno (TM) 640","","conservative",0,
     "B3 2026-10-19: full-tier modules compile in minutes and the unsplit surface-light stage lost the device"},''',
 '''    {"Pixel 4 XL Adreno 640","Adreno (TM) 640","","full",20000,
     "Phase C close-out 2026-10-20: full tier passes the practical criterion; old-hardware gauge, tighter 20 s compile budget (warnings only)"},''')
sub('native/tools/tier_gate.py',
 "                ('table-adreno-640', ADRENO_640, 'conservative', 'table', 60000, 'default'),",
 "                ('table-adreno-640', ADRENO_640, 'full', 'table', 20000, 'table'),")
sub('native/tools/tier_gate.py',
 '''        # Adreno 640 renders conservatively by table; a record with full-tier
        # evidence then overrides the table.
        c = records/'c'
        row, *_ = run('table-adreno-640-render', record=c, env={'CODEBRIX_CYCLES_TEST_ADAPTER': ADRENO_640})
        assert row['tier'] == 'conservative' and row['reason'] == 'table' and row['pipelineTiers'] == ['conservative']
        row, *_ = run('table-adreno-640-record-conservative-only', record=c, mode='create',
                      env={'CODEBRIX_CYCLES_TEST_ADAPTER': ADRENO_640})
        assert row['tier'] == 'conservative' and row['reason'] == 'table'
        run('adreno-640-forced-full', record=c, tier='full', env={'CODEBRIX_CYCLES_TEST_ADAPTER': ADRENO_640})
        row, *_ = run('record-overrides-table', record=c, mode='create', env={'CODEBRIX_CYCLES_TEST_ADAPTER': ADRENO_640})
        assert row['tier'] == 'full' and row['reason'] == 'record'
''',
 '''        # Adreno 640 renders at full fidelity by table with its own 20 s budget; the
        # record then decides, and a full-tier crash breadcrumb overrides the table.
        c = records/'c'
        row, *_ = run('table-adreno-640-render', record=c, env={'CODEBRIX_CYCLES_TEST_ADAPTER': ADRENO_640})
        assert row['tier'] == 'full' and row['reason'] == 'table' and row['pipelineTiers'] == ['full']
        assert (row['budgetMs'], row['budgetSource']) == (20000, 'table')
        row, *_ = run('table-adreno-640-record-full', record=c, mode='create',
                      env={'CODEBRIX_CYCLES_TEST_ADAPTER': ADRENO_640})
        assert row['tier'] == 'full' and row['reason'] == 'record'
        run('adreno-640-crash-in-prepare-surface', record=c, expect_crash=True,
            env={'CODEBRIX_CYCLES_TEST_ADAPTER': ADRENO_640, 'CODEBRIX_CYCLES_TEST_CRASH_PIPELINE': 'prepare_surface'})
        row, *_ = run('record-overrides-table', record=c, mode='create', env={'CODEBRIX_CYCLES_TEST_ADAPTER': ADRENO_640})
        assert row['tier'] == 'conservative' and row['reason'] == 'record' and 'prepare_surface' in row['detail']
''')
print('applied')
