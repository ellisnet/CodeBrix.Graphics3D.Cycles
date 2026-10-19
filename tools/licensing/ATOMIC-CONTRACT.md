# Native atomic replacement contract

Copyright (c) 2026 Jeremy Ellis and contributors

SPDX-License-Identifier: Apache-2.0

This is an original implementation, not an edit, translation or paraphrase of
Blender's excluded atomic implementation. Its inputs are Jeremy's license plan,
function names and uses in the pinned Apache-2.0 Cycles `src/util/atomic.h`,
`src/util/stats.h`, `src/scene/image_cache.cpp`, `src/integrator/`, and
`src/kernel/film/`, plus standard C++20 atomic operations. The excluded headers'
implementation bodies were not used for this replacement.

Required behavior:

| Interface | Value returned | Mutation |
| --- | --- | --- |
| `atomic_fetch_and_add_uint32` | previous integer | add, modulo word width |
| `atomic_fetch_and_sub_uint32` | previous integer | subtract, modulo word width |
| `atomic_fetch_and_or_uint32` | previous integer | bitwise OR |
| `atomic_cas_uint32` | observed integer | replace only when expected matches |
| `atomic_add_and_fetch_u` | new unsigned integer | add |
| `atomic_add_and_fetch_z` | new size counter | add |
| `atomic_sub_and_fetch_z` | new size counter | subtract |
| `atomic_fetch_and_update_max_z` | observed prior size counter | increase to candidate if larger |
| `atomic_cas_float` | observed float representation | representation-based conditional replacement |
| `atomic_add_and_fetch_fl` | successfully stored new float | compare/exchange addition loop |
| `atomic_load_z` (new CodeBrix helper) | observed size counter | none |

Fetch/max's return is not consumed by the pinned Cycles callers; the contract
above makes it explicit for future use. Existing float film calls ignore add's
return, while integer allocation and tile-descriptor calls consume prior values.
Float compare/exchange must distinguish signed zero and support NaN payloads;
ordinary float equality is unsuitable. No reinterpret-cast to an integer object
is used. Volatile arguments are accepted for compatibility, but the underlying
objects are ordinary suitably aligned Cycles storage, accessed atomically during
concurrent operations. Storage lifetime must cover all active atomic references.

All operations use sequential consistency. This conservatively preserves
ordering rather than weakening it based on assumptions about the old helper.
The implementation statically checks atomic_ref's alignment requirement against
the existing native type alignment. Platforms requiring stronger alignment fail
the build and need an explicit layout solution; no undefined fallback is allowed.

The Apache-licensed `Stats::mem_alloc` caller is changed to use the returned
atomic sum for its peak update, removing a plain concurrent read of `mem_used`.
Its debug free assertion uses an atomic read. This is separate from the atomic
implementation and is recorded as a modified upstream file.

Tests cover wraparound, old/new values, failed/successful CAS, signed zero, NaN
payloads, infinity, high-contention additions/CAS/OR/max, and native CPU film
updates under multiple threads. Device-specific WGSL atomic code is unchanged.

Specification reference: https://eel.is/c++draft/atomics.ref.ops
and https://eel.is/c++draft/atomics.ref.float . These describe standard semantics;
no standard-library implementation code is copied into the project.
