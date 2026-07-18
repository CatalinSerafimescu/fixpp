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
2. **Expect:** compiles + links; peak compiler RSS materially below the 077
   monolith baseline (~3.6 GiB just to `#include`), proportional to
   `NewOrderSingle`'s group-plan closure — for vlatest/v50sp2 (common message,
   233-plan closure) that's ~0.88 GiB (~4.2×), not order-of-magnitude; v44's
   much smaller group set does meet order-of-magnitude (~0.21 GiB, ~17×). See
   SC-001 / L-078-1. Record RSS/wall in the **compile-bench decision record**
   (003/T046 convention) — not a ctest gate.
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

**4a — validator-side mixing (R2a leg i):**
1. Build a consumer that force-inlines one `validate_<Msg>` (header-only, via
   `FIXPP_VALIDATORS_HEADER_ONLY_<Msg>`) which **shares a group-plan** with another,
   linked validator, and also links `fixpp::validators::<ver>`.
2. **Expect:** compiles + links with **no duplicate-symbol** error (the shared
   group-plan trait is the single `inline` definition in `validators/traits.hpp`).
3. Exercise the inlined `validate_<Msg>` and a linked `validate_<Msg>` with identical
   `Args` → **result-identical** (same success/error and same offending tag; the
   validator returns a validation result, not wire bytes — SC-004).

**4b — builder-side mixing (New-4; FR-007's headline case):**
1. Build a consumer that force-inlines one `build_<Msg>` (via
   `FIXPP_BUILDERS_HEADER_ONLY_<Msg>`) and links the rest of `fixpp::builders::<ver>`.
2. **Expect:** compiles + links with **no duplicate-symbol** error; the inlined
   `build_<Msg>` body is available at the call site while every other `build_<Msg>`
   resolves from the lib.
3. `nm` the inlined-message object → the forced-inline `build_<Msg>` is defined
   locally and pulls **no** validator symbol; the message's wire output is
   **byte-identical** to its linked form (SC-004).

**4c — both libs linked (R2a leg iii):** a consumer that links **both**
`fixpp::builders::<ver>` and `fixpp::validators::<ver>` (the US1+US2 common case)
compiles + links with **no duplicate-symbol** for `build_`/`validate_` (disjoint
objects, R1).

**4d — contract: `FIXPP_BUILDERS_HEADER_ONLY[_<Msg>]` / `FIXPP_VALIDATORS_HEADER_ONLY[_<Msg>]`
is a program-wide per-message switch, not a per-TU one (Edge Case, spec.md ~line 128).**
A given message must be **either** force-inlined in *every* TU of the program that
references it, **or** left in link mode (resolved from the archive) in *every* TU —
never both within one program. Scenarios 4a/4b above force-inline exactly one
message per consumer and never reference that same message in link mode elsewhere
in the same binary, so they stay on the safe side of this contract. Mixing modes
for the *same* message across TUs in one program (force-inline in TU-A, link-mode
in TU-B, both linked into the same binary) produces an `inline` (weak/COMDAT)
definition in TU-A's object alongside the archive's non-`inline` (strong external)
definition of the identically-mangled symbol — an ODR violation under
[dcl.inline]/4 (IFNDR: no diagnostic required). In practice mainstream linkers
resolve all references to whichever definition is pulled in first and both
definitions are token-identical (same emitter body, differing only in the
`inline` keyword), so this typically does not manifest as observably divergent
output — but it is undefined behavior and MUST NOT be relied upon. Regression
coverage: `tests/codegen/test_078_builder_inline_all_tus_us3.cpp` (+ validator
twin) demonstrate the SAFE all-inline-across-TUs path; the mixed-same-message
case is intentionally not exercised as a "passing" test (see that file's header
comment) — see also `feedback_named_safety_invariant_needs_direct_pin`.

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
3. **In CI (gating step, R8):** every tier1 C++ leg (incl. `linux-clang-asan`/
   `ubsan`/`tsan` and `linux-gcc-release`), **the python-bindings `asan`/`ubsan`/
   `tsan` legs** (they build the heavy TUs via the reused `linux-clang-debug`
   preset), and tier2 MSVC green without `FIXPP_BUILD_HEAVY_BUILDER_TESTS` and
   without the job pool. Only after that is green does the option/pool deletion land.
   **The python-bindings legs are path-gated (`python_touched`, `tier1.yml:628-631`)
   and SKIP on the 078 PR** (078 matches none of `PY_RE`, `:131`), so this evidence
   must come from a **`workflow_dispatch` (or feature-branch `push`) run** — a
   non-`pull_request` event sets `python_touched=true` unconditionally (`:141`) and
   runs those legs against the post-split tree — **or** the deletion is split into a
   follow-up PR that lands after a `push:main`/dispatch run proved them (Gate A round
   2; do not broaden `PY_RE`).
4. **Caveat:** `/speckit-verify` is **clang-only + local** and does **not** exercise
   the python-bindings sanitizer legs (nor gcc-release / MSVC). The #197 removal is
   gated on **actual CI** (the forced evidence run above) on those legs, not on the
   local verify record — the main-CI OOM findings reproduced exactly there
   (Clarifications — main-CI OOM + Gate A round 2).

## Full verification

`/speckit-verify 078-precompiled-builder-libs` runs the Tier-1 mirror (sanitizer
matrix, determinism/git-clean gates, `nm` witnesses, round-trip/golden diff, the
compile-bench record for SC-001/006) and writes
`.specify/decisions/078-precompiled-builder-libs-verify.md` — required evidence
for `/gate-b`.
