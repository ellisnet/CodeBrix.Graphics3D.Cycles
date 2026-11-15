Copyright (c) 2026 Jeremy Ellis and contributors
SPDX-License-Identifier: Apache-2.0

# Closing build: reproducibility, information only

The C27 tables and the deleted build were made from other source states (the code changed since C27, and the deleted tree was built from 49cd3a4a plus uncommitted changes), so a difference here is expected wherever a source changed and is not a verdict.

## Against C27 scratch table F-a1 (build-tree keys)

| Artifacts | Identical / compared | Differ | Only in one |
| --- | --- | --- | --- |
| lifecycle/reference executables | 4/7 | codebrix_host_image_reference, codebrix_host_session64_reference, codebrix_host_session_reference | - |
| host library | 0/1 | libcodebrix_cycles_host.dylib | - |
| Cycles static libraries | 9/10 | libcycles_integrator.a | - |
| staged kernel closure | 292/292 | - | - |
| dependency prefix record | 0/1 | prefix-marker | - |
| dependency dylibs | 27/28 | libjpeg.62.3.0.dylib | - |
| dependency static libraries | 11/11 | - | - |
| oracle runner | 1/1 | - | - |

## Against C27 scratch table F-bb (build-tree keys)

| Artifacts | Identical / compared | Differ | Only in one |
| --- | --- | --- | --- |
| lifecycle/reference executables | 4/7 | codebrix_host_image_reference, codebrix_host_session64_reference, codebrix_host_session_reference | - |
| host library | 0/1 | libcodebrix_cycles_host.dylib | - |
| Cycles static libraries | 9/10 | libcycles_integrator.a | - |
| staged kernel closure | 292/292 | - | - |
| dependency prefix record | 0/1 | prefix-marker | - |
| dependency dylibs | 27/28 | libjpeg.62.3.0.dylib | - |
| dependency static libraries | 11/11 | - | - |
| oracle runner | 1/1 | - | - |

## Against the batch-final build (native/work/partD-20261114/final/hashes.txt)

- build/host/lib/libcodebrix_cycles_host.dylib: closing `b804223deae0afce07ad33440b312d3ea907b1f528839b375db628e3a1916e5e`, batch-final `b804223deae0afce07ad33440b312d3ea907b1f528839b375db628e3a1916e5e`: IDENTICAL
- build/runner: closing `1f90b7df1bac4910b08ddae6a78de82246df3d87f07a75f9ff248f4899e692d2`, batch-final `1f90b7df1bac4910b08ddae6a78de82246df3d87f07a75f9ff248f4899e692d2`: IDENTICAL

## Against the build tree deleted at the start of phase 1

| Artifacts | Identical / compared | Differ | Only in one |
| --- | --- | --- | --- |
| lifecycle/reference executables | 7/7 | - | - |
| host library | 1/1 | - | - |
| Cycles static libraries | 10/10 | - | - |
| staged kernel closure | 292/292 | - | - |
| dependency prefix record | 0/1 | prefix-marker | - |
| dependency dylibs | 27/28 | libjpeg.62.3.0.dylib | - |
| dependency static libraries | 11/11 | - | - |
| oracle runner | 1/1 | - | - |
