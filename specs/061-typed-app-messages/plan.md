# Implementation Plan: Typed Application Messages (061)

**Branch**: `061-typed-app-messages` | **Date**: 2026-07-05 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/061-typed-app-messages/spec.md`

## ⏸ STATUS: PAUSED — blocked on prerequisite feature `062` (grouped typed-read fix)

Phase-0 research ([research.md](./research.md)) surfaced and source-verified a blocker: **typed reads of repeating-group entries do not compile today** (`group_view::operator[]` constructs the entry from `std::span<byte>`, but generated entry classes `G_<n>` have only a `MessageView<Index> const&` ctor — `group_view.hpp:34-37` vs `emit_messages.cpp:209-217`). The majority of the 33 in-scope messages are grouped, so their discriminating read/round-trip witnesses (FR-005/006) cannot be written until this is fixed.

Per user decision 2026-07-05, the fix is carried by a **separate prerequisite feature `062` (grouped typed-read path fix)** — `group_view::operator[]` + the codegen entry-class contract + equivalence tests over *generated* flyweights — sequenced and merged BEFORE this feature, mirroring how `057` unblocked `reify` before the typed rows. **This is a dependency, not a numeric-order inversion mistake: `062` lands before `061`.**

**Resume trigger**: `062` merged to `main`. On resume: rebase `061` onto `main`, re-run `/speckit-plan` to complete Phase 1 (data-model.md, contracts/, quickstart.md) against the now-working group-read API, then continue the pipeline (Gate A → tasks → … → Gate B).

## Summary

Deliver, for each of the **33 distinct messages** across the 28 order-management / market-data / allocation rows (A-001..A-013, M-001..M-012, P-001..P-003), a hand-written typed **builder** (body-only, 020 pattern) + an independent inbound **read witness** + a **round-trip witness**, in one representative version namespace per row (**v44** for A/P, **v42** for M). Fold in a packaging fix installing the generated typed-message flyweight headers (FR-007). The codegen writer-emitter (FR-015a) and all-version coverage (FR-015b) stay deferred; N-002/003 (FSM dispatch) are a separate later feature.

## Technical Context

**Language/Version**: C++23 (fixpp library). **Primary Dependencies**: existing generated flyweights under `fixpp::v{42,44}` (no codegen regen for the read path once `062` lands); `wire::Framer`/`MessageView`/`OffsetTable`; `decimal_t`; `core::fix_time`. **Testing**: GoogleTest suites under `tests/session/`, `tests/wire/`, plus a factored shared read-scaffold header under `tests/support/`. **Target Platform**: Linux (Tier-1) + Windows (Tier-2). **Project Type**: single library. **Scale/Scope**: ~33 messages × 3 artifacts (builder + read + round-trip) + 1 install rule; grouped-message builders are the LoC-heavy portion.

## Constitution Check

- **Appendix A mandatory triggers**: this feature adds ~33 public C++ builder declarations + a packaging change. It does NOT change the C-ABI, error enums, wire framing, or the session FSM (N is out of scope). The grouped-read wire/codegen change lives in the prerequisite `062`, where those triggers (wire-format/parser + codegen-layout) are handled. For `061` itself: run the full pipeline including Gate A (default for feature bundles); `/clarify` done (Session 2026-07-05).
- **§XVIII.7 scope**: A-001..A-013 + M-/P- families are v1.0 typed-message scope; honored. (Doc-hygiene: §XVIII.7's stale "C-/R-" mention to be amended per research.md — tracked as close-out, not this feature.)
- **FR-015a/b deferral honored**: hand-written builders only; one representative namespace.

## Project Structure

### Source Code (repository root)

```text
src/session/business_messages.cpp        # extend: ~33 hand-written body-only builders (grouped + flat)
include/fixpp/session/business_messages.hpp   # extend: builder declarations
include/fixpp/session/business_messages_detail.hpp  # NEW (likely): lift wfield/wchar/wdecimal out of the TU-anon ns for reuse by grouped builders
tests/support/app_message_read_scaffold.hpp   # NEW: factored make_frame/parse_frame (parameterised by BeginString v42/v44)
tests/session/test_<msg>_read.cpp / _roundtrip.cpp   # per-message discriminating witnesses
CMakeLists.txt                            # FR-007: install(DIRECTORY _codegen/include) with _dispatch + vt11 excluded
spec/feature-catalogue.md, spec/coverage-index.md, spec/behaviors-and-limitations.md  # FR-009 status flips + B&L
```

**Structure Decision**: single-library layout; builders extend the existing 020 `business_messages` TU/header; witnesses under `tests/session/` reuse a factored `tests/support/` scaffold. Detailed contracts/data-model/quickstart are **deferred to plan resume** (they depend on the `062` group-read API).

## Complexity Tracking

| Decision | Why | Note |
|----------|-----|------|
| Prerequisite feature `062` before `061` | Typed group-entry reads don't compile (verified) and most in-scope messages are grouped | Mirrors `057`→typed-rows; keeps the wire/codegen change in a focused, separately-gated feature |
| Lift `wfield`/`wchar`/`wdecimal` from anon ns to a shared internal header | Grouped builders in new TUs need them; currently TU-local `static` | Surgical; no behavior change to existing builders |
| Exemplar-first (one grouped row end-to-end) on resume | Grouped body-only builder pattern is unproven | Task 1 on resume, per spec Assumptions |
