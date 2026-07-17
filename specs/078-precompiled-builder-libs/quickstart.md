# Quickstart / Validation Guide: Precompiled per-version builder/validator libraries

**Feature**: 078-precompiled-builder-libs · Phase 1

Runnable validation scenarios proving the feature end-to-end. Detail lives in
[contracts/](./contracts/) and [data-model.md](./data-model.md); this is the
"how to verify" guide. Run from the library submodule
(`research/G19-fix-fpml-iso20022/library`) on `linux-clang-debug` unless noted.

## Prerequisites

- Conan install + CMake configure (codegen runs at configure time) on
  `linux-clang-debug` — surfaces the generated file set under
  `build/<preset>/_codegen/include/fixpp/<ns>/` and builds the new lib targets.
- The six new targets exist: `fixpp_builders_{v44,v50sp2,vlatest}` and
  `fixpp_validators_{v44,v50sp2,vlatest}`.

## Scenario 1 — Cheap consumer compile against the slim header (US1 / SC-001)

1. Compile a TU that `#include <fixpp/vlatest/messages/NewOrderSingle.hpp>` and
   calls one `build_NewOrderSingle`, linking `fixpp::builders::vlatest`.
2. **Expect:** compiles + links; peak compiler RSS an order of magnitude below the
   077 monolith baseline (~3.6 GiB just to `#include`). Record RSS/wall in the
   **compile-bench decision record** (003/T046 convention) — not a ctest gate.
3. **Expect:** the object contains machine code only for the called builder (not
   all 173) — `nm` on the object / linked binary (Scenario 3).

## Scenario 2 — Send-only consumer carries zero validator code (US2 / SC-003)

1. Build a consumer linking **only** `fixpp::builders::vlatest` (no validators),
   calling one `build_<Msg>`.
2. `nm --defined-only <binary> | grep -c validate_` → **0**; also zero
   `writer_traits` symbols. No link dependency on `fixpp_validators_vlatest`.
3. Build a second consumer linking `fixpp::validators::vlatest`, call
   `validate_<Msg>` on an `Args` missing a required field → reports the missing
   field consistent with the 077 typed validator.

## Scenario 3 — Link only what you use (SC-002)

1. Build a consumer calling a **subset** (say 3) of a version's builders, linking
   `fixpp::builders::<ver>`.
2. `nm --defined-only <binary>` → only the 3 called `build_` symbols present; the
   remaining messages' machine code absent (per-message `.o` archive granularity).

## Scenario 4 — Force-inline a few, link the rest, ODR-safe (US3 / FR-007 / R2a)

1. Build a consumer that force-inlines one `validate_<Msg>` (header-only, via the
   per-message macro) which **shares a plan** with another, linked validator, and
   also links `fixpp::validators::<ver>`.
2. **Expect:** compiles + links with **no duplicate-symbol** error (shared traits
   are the single `inline` definition in `groups.hpp`).
3. Exercise the inlined message and a linked message with identical inputs →
   **byte-identical** wire output (SC-004).

## Scenario 5 — Aggregator = full set, byte-identical, stays slim (US4 / SC-005 / R5)

1. Build a consumer that `#include <fixpp/<ns>/all.hpp>` (default/link mode) and
   exercises every `build_<Msg>` → output **byte-identical** to the 077 monolith
   (0 diffs vs the golden set).
2. Confirm `all.hpp` in default mode did **not** resurrect the ~3.6 GiB include
   cost (compile-bench record, R5 guard).
3. Confirm `fixpp/<ns>/Builders.hpp` **no longer resolves** (US4 AC2, breaking
   change).

## Scenario 6 — Deterministic multi-file regeneration (SC-007 / FR-010)

1. `ctest -L codegen -R codegen_determinism_test` → PASS: tool run twice yields a
   byte-identical **file set**, a stable file **name-set + count**, and the set
   matches the golden set.
2. `ctest -R codegen-build-graph-check` → PASS (`git status --porcelain` clean).

## Scenario 7 — Heavy tests compile cheap; #197 stopgap retired (US5 / SC-006 / R8)

1. Build the (formerly heavy) builder completeness + round-trip TUs — now
   `#include` slim `all.hpp` + **link** the prebuilt libs.
2. **Expect:** their peak compile RSS is well under the 16 GB runner limit; the
   giant compile happened once at the lib target.
3. **In CI (gating step, R8):** every tier1 leg (incl. `linux-gcc-release`) + tier2
   MSVC green without `FIXPP_BUILD_HEAVY_BUILDER_TESTS` and without the job pool.
   Only after that is green does the option/pool deletion land.

## Full verification

`/speckit-verify 078-precompiled-builder-libs` runs the Tier-1 mirror (sanitizer
matrix, determinism/git-clean gates, `nm` witnesses, round-trip/golden diff, the
compile-bench record for SC-001/006) and writes
`.specify/decisions/078-precompiled-builder-libs-verify.md` — required evidence
for `/gate-b`.
