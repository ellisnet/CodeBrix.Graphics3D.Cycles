# Cycles compatibility inventory

Copyright (c) 2026 Jeremy Ellis and contributors

This is the initial P00 inventory for Cycles revision
`a456b761034dda42c32eef9f4aae0fa5a5c9f604`. Full Cycles functionality, all nine
native RIDs, the unchanged shipped WebGPU runtime and thin .NET delivery remain
required. `inventoryComplete` and `fullCyclesParity` are both false.

The ledger currently covers 1,011 source declarations: 111 SVM symbols (including
`NODE_NUM`), 45 closures, 78 device kernels, 62 passes, 621 other enum values,
41 feature flags/masks and 53 native scene tables. It also records all nine RIDs,
18 reviewed dependency snapshots and eight unresolved dependency tracks.
These counts include sentinels, masks, aliases and virtual closures. They are
not a count of independent user features or a completion percentage.

## Generate and check

From the repository root, with Python 3 and host Clang available:

```bash
python3 tools/compatibility/inventory.py generate
python3 tools/compatibility/inventory.py check
python3 -m unittest discover -s tools/compatibility -p 'test_*.py' -v
python3 tools/licensing/audit.py
python3 tools/licensing/notices.py --check
```

No implementation sources or dependencies are fetched. The tool verifies the
filtered Cycles snapshot, extracts source declarations, checks their exact order
and membership against Clang's AST, then compiles and executes a native metadata
program. Enum values and table element sizes come from that executable. Unknown
declaration syntax fails rather than silently reducing coverage.

The captured numeric/layout profile is Linux x64 (`x86_64-pc-linux-gnu`, 8-byte
pointers), with optional build features disabled. Other target ABIs require
separate metadata and native execution; this host probe is not their proof.
Compiler identity, executable/source hashes, raw metadata and validation are
working results in `native/work/compatibility/`. The metadata executable does
not render or establish runtime GPU support. No renderer regression is implied
by a successful inventory check.

## Sources of authority

- `baseline.json` pins the selected declaration files, runtime requirement,
  metadata profile, numerical proof limits and remaining inventory work.
- `reviews.json` contains deliberate symbol classifications and bounded coverage
  observations. Generic classifications are provisional; only rows with
  `classificationReviewed: true` have an explicit classification review.
- `evidence.json` links 51 immutable reports from `native-normals`: new normal/
  tangent captures and full render, plus fresh image, file-image, attribute and
  earlier renderer regressions. Every native source pin refers to the current
  tested implementation. The earlier checkpoints remain unchanged. Report hashes,
  explicit assertions and current source hashes are checked; regeneration cannot
  silently refresh stale evidence.
- The file-image collector accepts a source-matching fresh image/attribute/render
  regression chain after renderer changes. Its original checkpoint's strict
  identity check remains available for reproducing the earlier test-only addition.
- The remaining JSON files are generated. Edit the source/reviews and regenerate
  them. `check` rejects missing, changed or outdated output. Unknown review IDs
  fail so a removed upstream symbol cannot erase its existing review silently.

Each declaration retains its source, stable ID, native value or element size,
work-item assignment, test strategy, status and evidence links. Existing tests
are attached to 16 registered kernel entries, five closure families, four
attribute opcodes, six image/environment opcodes, three normal/tangent opcodes,
fourteen normal/tangent modes and thirteen image storage/alpha
mode entries. Their
coverage remains partial, with restrictions stated on each row. No row claims
`fullRequirementVerified: true`. The initial generator rejects such claims;
a future completion workflow must implement the full requirement audit before
allowing them.

The platform matrix distinguishes native-host gates from captured-command
replay. Linux has a bounded native-host proof. Android has command replay, with
full native hosts and APK integration pending. Windows, macOS, Linux ARM64 and
Linux RISC-V64 remain required. No historical report establishes current device
connectivity. Package and release gates remain separate from rendering evidence.

## Remaining P00 work and next implementation

This first extraction covers `kernel/types.h`, `kernel/svm/types.h`,
`util/types_image.h`, feature macros and scene-array declarations. It does not
yet enumerate native graph registration/socket properties, all scene/session
APIs, state fields, every build-conditional facility, or CPU/Metal/OSL differences.
Most behavior/parameter rows still need individual assessment, detailed work
items, independent tests and feature-combination coverage. Workload budgets and
the public capability-report schema remain open. The full parity denominator
is therefore still incomplete.

Next, extend the inventory with native registration and properties, then
extend the tested static-triangle attributes to remaining domains and resident
image transport to production decoding/color, updates and tiled/sparse paths
under P02/P04. The image checkpoint tests eight resident formats, 115,650
sampling cases, 128 native graphs and 176 typed projection/derivative programs.
The sampling corpus, native graph capture and complete generated-image render
have four-GPU replay gates; typed projections/derivatives currently execute on
Linux only. Preserve
the clay regression and require a native-prepared textured ForestCommunity
asset with independent CPU film evidence. General closures, direct lighting,
shadows and MIS follow; native Android hosts and hard dependency/OSL feasibility
should progress early as described in the main plan.

The identifiers and compiler metadata refer to the already reviewed Cycles
snapshot. Upstream implementation and notices remain in `third_party/cycles`;
no new third-party implementation or dependency was imported for this ledger.
