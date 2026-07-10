# Quickstart: FR-015a-lite — Codegen Writer-Emitter

Practical guide for working on / verifying feature 067. All paths relative to the library submodule; run every command with cwd inside it:

```bash
cd research/G19-fix-fpml-iso20022/library
```

## What this feature adds

A generated typed writer per OFFICIAL FIX application message. Instead of hand-writing 33 builders, `fixpp-codegen` emits them from the dictionary, matching the frozen 061 shape-oracle byte-for-byte.

## Using a generated builder (consumer view)

```cpp
#include <fixpp/v44/Builders.hpp>   // generated

std::array<std::byte, 512> buf;
fixpp::v44::NewOrderSingleArgs args;
args.cl_ord_id   = "ORD-001";
args.symbol      = "MSFT";
args.side        = '1';
args.order_qty   = fixpp::decimal_t::parse("100").value();
args.ord_type    = '2';
args.price       = fixpp::decimal_t::parse("190.5").value();
args.transact_time = "20240101-10:00:00";

// Optional fail-closed check (separate from build — see below):
if (auto v = fixpp::v44::validate_NewOrderSingle(args); !v) return v.error();

auto body = fixpp::v44::build_NewOrderSingle(buf, args);   // pure serialize -> body-only bytes
// body.value() == the 061 hand-exemplar bytes == the QuickFIX golden
```

**Key rule**: `build_*` is a **pure serializer** (byte-identical to the 061 exemplars). Required-presence is a **separate** `validate_*` step — call it first if you want the fail-closed guarantee. `build_*` emits present (`std::optional` engaged) fields in canonical FIX order regardless of the order you set `args` members.

## Adding / regenerating builders (developer view)

The emitter is `tools/codegen/fixpp-codegen/emit_builders.cpp`. To regenerate after an emitter change:

```bash
# forced full regen (structural change) — configure+build the codegen tool, then the graph regenerates
cmake --build build/linux-clang-debug --target fixpp-codegen
# the codegen build-graph regenerates Builders.hpp into build/<preset>/_codegen/include/fixpp/v44/
# then re-index CodeGraph:
cd research/G19-fix-fpml-iso20022/library && codegraph sync   # or: codegraph index --force after structural change
```

Verify regeneration is clean (the codegen build-graph cleanliness gate — no source-tree writes, deterministic):

```bash
git status --porcelain   # must be clean w.r.t. tracked files after regen (generated output is build-tree only)
```

## Verifying the feature

```bash
# headline pin — generated == hand == golden for D/8/9/E/AS
ctest --test-dir build/linux-clang-debug -R 067_builder_shape_oracle --output-on-failure

# round-trip + byte-structural (all 33)
ctest --test-dir build/linux-clang-debug -R 067_builder_roundtrip --output-on-failure

# required-presence: top-level + group-entry depth
ctest --test-dir build/linux-clang-debug -R 067_builder_validate --output-on-failure

# exact-set completeness over the 33 OFFICIAL MsgTypes
ctest --test-dir build/linux-clang-debug -R 067_completeness --output-on-failure

# emitter unit tests (order rule, header-exclusion set, group tables)
ctest --test-dir build/linux-clang-debug -R 067_emit_builders_unit --output-on-failure
```

Then the full local mirror before Gate B:

```bash
/speckit-verify        # Tier-1 sanitizer/coverage/static-analysis mirror -> .specify/decisions/067-*-verify.md
```

## The 33 OFFICIAL MsgTypes (completeness set)

`D E F G H 8 9 q r AF AC t u` · `V W X Y c d e f g h i b S R AG Z a` · `J P AS`
(4 multi-char: `AF AC AG AS`; `b` = MassQuoteAcknowledgement, shared M-008/M-009.)

## Guardrails (do not regress)

- `build_*` output MUST stay byte-identical to the 061 exemplars — the hand builders in `src/session/business_messages.cpp` are the frozen oracle; do NOT edit them to match the emitter, fix the emitter.
- Field order is emitter-imposed (top-level tag-ascending; group-entry member order), NOT caller-controlled — see `contracts/generated-builder.md` G3.
- No new `fixpp_error_t`; reuse `wire_required_field_missing`. No C-ABI / read-path / Python change (FR-009).
- Non-debug (sanitizer/coverage) build dirs compile a fresh `_codegen` — regenerate before those runs or you get stale-emitter false-greens (`project_codegen_emitter_staleness`).
