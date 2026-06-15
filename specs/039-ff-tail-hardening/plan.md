# Implementation Plan: F-f tail hardening bundle (LOW)

**Branch**: `039-ff-tail-hardening` | **Date**: 2026-06-15 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/039-ff-tail-hardening/spec.md`

## Summary

The residual LOW items from the Fable F-f release-gate tail after 038, as four independent,
separately-witnessed concern groups — **none of which change production behavior**: US2 pins a
user-ratified frozen-ABI behavior with a regression test + comment; US3 covers three reachable
"untestable"-waived lines; US4 extends the §XV.9 no-`std::mutex` corpus gate to the uncovered
session-side awaitable headers; US5 resolves the L-033-3 doc wording + absent-`1137`-ack case.

**US1 (wire/session tag-overflow hardening) was split out** to `040-inbound-tag-overflow-hardening`
after Gate A round 1 found it to be a real 5-site security fix (see spec.md split notice; census in
`research/reviews/opus_039-ff-tail-hardening_gate_a_adversarial_review.md`).

Implementation discipline: **one implementer invocation per user story** (the
`phase-implementer-sonnet` runaway-scope guard).

## Technical Context

**Language/Version**: C++23 (libstdc++ / clang + gcc), CMake + Conan presets
**Primary Dependencies**: Boost.Asio (awaitable corpus), GoogleTest
**Storage**: N/A (US3(b) touches `OsFile` test coverage only; no store format change)
**Testing**: GoogleTest via ctest; `tools/check_no_std_mutex_in_awaitable_headers.sh` corpus gate
(US4); lcov DA/BRDA per `[const §IX.1]` (US3)
**Target Platform**: Linux (primary); POSIX + Windows `OsFile` variants (US3(b) covers both
move-ctors at `file_store.cpp:401`/`:503`)
**Project Type**: Library (FIX engine) — C-ABI (`fixpp::capi`), session, build-gate, docs
**Performance Goals**: N/A (no production code change)
**Constraints**: No new error codes; no new config; no codegen regeneration; **no wire or C-ABI
behavior change** (US2 pins existing behavior, does not modify it)
**Scale/Scope**: 4 test/build-gate/doc deltas; no new public surface; no production code change

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- **§ Gate A trigger (Article XVII / pipeline step 4)**: **NOT triggered.** With US1 split out, this
  feature touches no wire-codec, no C-ABI *behavior* (US2 only adds a test + comment), no codegen,
  no config — only tests, a build-gate header list, and docs. → **Gate A not required for 039.**
  (`/gate-b` pre-flight will see `gate_a_required = no` for this feature; the security fix's Gate A
  lives in 040.)
- **§X.1 frozen C-ABI (MAJOR=1)**: US2 makes NO ABI behavior change — it pins existing behavior with
  a test + comment. → **PASS (compliant by non-modification).**
- **§IX.1 coverage (lcov DA/BRDA)**: US3 *improves* coverage (three previously-waived lines) and
  re-measures the 033 deferral. No new uncovered production lines (no production code added). → **PASS.**
- **§XV.9 no-`std::mutex`-in-awaitable corpus gate**: US4 *extends* the gate — strictly additive; the
  newly-listed headers are clean today (SC-005). → **PASS.**
- **Article VI §5 (Normative References)**: spec.md now carries a Normative References section
  (added per Gate A round 1 P3). → **PASS.**

No constitutional violations requiring Complexity Tracking.

## Project Structure

### Documentation (this feature)

```text
specs/039-ff-tail-hardening/
├── plan.md              # This file
├── research.md          # Phase 0 — decisions per user story (US2–US5)
├── data-model.md        # Phase 1 — (no new entities; US2 pinned-behavior table)
├── quickstart.md        # Phase 1 — how to run each story's witness
├── checklists/
│   └── requirements.md  # spec quality checklist
└── tasks.md             # Phase 2 (/speckit-tasks — not created here)
```

(No `contracts/` — 039 adds no public/behavioral surface; the US1 wire-decode contract moved to 040.)

### Source Code (repository root)

```text
src/capi/decimal.cpp                       # US2 — _checked entry points (:145-168); comment cross-ref only
tests/core/decimal_capi_error_test.cpp     # US2 — sentinel-pinning regression test (existing suite)
include/fixpp/session/seqnum_manager.hpp   # US3(a) — mutex_test_access seam
src/session/seqnum_manager.cpp             # US3(a) — set_next_outbound lock-fail branch (:188)
src/session/file_store.cpp                 # US3(b) — OsFile move-ctor (:401 posix / :503 windows)
tests/session/                             # US3 — witnesses
tests/sync/CMakeLists.txt                  # US4 — check_no_std_mutex_corpus header list (:140)
spec/behaviors-and-limitations.md          # US5 — L-033-3 wording (:1096)
```

**Structure Decision**: Library layout (existing). No new modules. **No production code change** —
US2 is a test + a comment, US3 is tests, US4 is a build-gate list, US5 is docs. Each user story maps
to a distinct directory/file set, satisfying independent-implementability.

## Complexity Tracking

> No Constitution Check violations — section intentionally empty.

## Gate A

Round 1 (on the original 5-US bundle) ran 2026-06-15 and surfaced that US1 was a 5-site security fix
→ **US1 split out to 040**. The residual round-1 findings applicable to 039 were folded in directly:
US4 corpus corrected to the 7 awaitable headers (drop `business_messages`); the invalid
`[const §VI.4]` "glob-free" citation dropped; a Normative References section added to spec.md. With
US1 gone, **039 makes no production change and Gate A is no longer required** — proceed to
`/speckit-tasks`. Reviews archived:
`research/reviews/codex_039-ff-tail-hardening_gate_a_review.md`,
`research/reviews/opus_039-ff-tail-hardening_gate_a_adversarial_review.md`.
