# Implementation Plan: F-f tail hardening bundle

**Branch**: `039-ff-tail-hardening` | **Date**: 2026-06-15 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/039-ff-tail-hardening/spec.md`

## Summary

Five independent, separately-witnessed concern groups from the Fable F-f release-gate tail (the
residual LOW items after 038). Only **US1** changes production behavior: a tag-accumulation overflow
guard on the live inbound field-decode path (two twin sites) closing a forged-tag aliasing vector
(reclassified LOW→MED on a reachability check). **US2–US5** make no production behavior change —
US2 pins a user-ratified frozen-ABI behavior with a regression test + comment (the Fable "reject the
sentinel" ask was found to contradict the 2026-05-12 ABI decision and is out of scope), US3 covers
three reachable "untestable"-waived lines, US4 extends the §XV.9 no-`std::mutex` corpus gate to the
uncovered session-side awaitable headers, US5 resolves the L-033-3 doc wording + absent-`1137`-ack
case.

Implementation discipline: **one implementer invocation per user story** (the
`phase-implementer-sonnet` runaway-scope guard). Each story is independently testable and shippable.

## Technical Context

**Language/Version**: C++23 (libstdc++ / clang + gcc), CMake + Conan presets
**Primary Dependencies**: Boost.Asio (awaitable corpus), OpenSSL (TLS, not touched here), GoogleTest
**Storage**: N/A (US3(b) touches `OsFile` test coverage only; no store format change)
**Testing**: GoogleTest via ctest; `tools/check_no_std_mutex_in_awaitable_headers.sh` corpus gate
(US4); lcov DA/BRDA per `[const §IX.1]` (US3)
**Target Platform**: Linux (primary); POSIX + Windows `OsFile` variants exist (US3(b) covers both
move-ctors at `file_store.cpp:401`/`:503`)
**Project Type**: Library (FIX engine) — wire codec (`fixpp::wire`), C-ABI (`fixpp::capi`), session
**Performance Goals**: US1 guard adds at most one comparison per tag digit on an already-O(digits)
scan — no measurable hot-path regression; SC-002 requires byte-identical decode for conforming tags
**Constraints**: No new error codes (reuse `wire_tag_out_of_range`); no new config; no codegen
regeneration; frozen C-ABI (`[const §X.1]`, MAJOR=1) MUST NOT change (US2)
**Scale/Scope**: ~1-line guard ×2 sites (US1) + 5 test/doc deltas; no new public surface

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- **§ Gate A trigger (Article XVII / pipeline step 4)**: US1 touches the **wire-codec surface**
  (inbound field decode) → Gate A is REQUIRED. (US2 no longer touches C-ABI behavior, so the C-ABI
  trigger is moot; US1 alone triggers Gate A.) → **Gate A scheduled after this plan.**
- **§X.1 frozen C-ABI (MAJOR=1)**: US2 explicitly makes NO ABI behavior change — it pins existing
  behavior with a test + comment. The Fable "reject the sentinel" change is OUT OF SCOPE (would
  require an ABI decision / 2a amendment). → **PASS (compliant by non-modification).**
- **§IX.1 coverage (lcov DA/BRDA)**: US3 *improves* coverage (covers three previously-waived lines)
  and re-measures the 033 deferral. No new uncovered production lines are introduced (US1's new
  reject branch is witnessed by the adversarial forged-tag test). → **PASS.**
- **§XV.9 no-`std::mutex`-in-awaitable corpus gate**: US4 *extends* the gate (T066) — strictly
  additive hardening, no production change; the newly-listed headers are clean today (SC-005). →
  **PASS.**
- **§VI.4 glob-free explicit corpus lists**: US4 MUST add headers explicitly (no glob) to the
  `check_no_std_mutex_corpus` list, matching the existing style. → **Constraint noted.**
- **Default-real on parse/analyzer findings (project norm)**: US1 was reclassified LOW→MED after a
  reachability check disproved the "latent" premise; severity framing is honest. US2's Fable premise
  was disproven against primary sources (ratified ABI decision) and downgraded — not silently
  dropped. → **PASS.**

No constitutional violations requiring Complexity Tracking.

## Project Structure

### Documentation (this feature)

```text
specs/039-ff-tail-hardening/
├── plan.md              # This file
├── research.md          # Phase 0 — decisions per user story
├── data-model.md        # Phase 1 — (no new entities; decode-disposition invariants)
├── quickstart.md        # Phase 1 — how to run each story's witness
├── contracts/           # Phase 1 — wire-decode behavior contract (US1)
├── checklists/
│   └── requirements.md  # spec quality checklist (passing)
└── tasks.md             # Phase 2 (/speckit-tasks — not created here)
```

### Source Code (repository root)

```text
src/wire/offset_table.cpp                 # US1 site A — Index-mode eager build (loop :168, post-check :176)
include/fixpp/wire/parser.hpp             # US1 site B — Scan-mode field_iterator::advance (loop :340)
include/fixpp/wire/errors.hpp             # US1 — err_tag_out_of_range() (reuse; :55)
src/capi/decimal.cpp                      # US2 — _checked entry points (:145-168); comment cross-ref only
include/fixpp/session/seqnum_manager.hpp  # US3(a) — mutex_test_access seam
src/session/seqnum_manager.cpp            # US3(a) — set_next_outbound lock-fail branch (:188)
src/session/file_store.cpp                # US3(b) — OsFile move-ctor (:401 posix / :503 windows)
tests/sync/CMakeLists.txt                 # US4 — T066 check_no_std_mutex_corpus header list (:140)
spec/behaviors-and-limitations.md         # US1 (new B&L row, FR-003a) + US5 (L-033-3 wording, :1096)

tests/
├── wire/        # US1 — adversarial forged-tag aliasing witness (Index + Scan)
├── capi/ or core/  # US2 — sentinel-pinning regression test (locate existing _checked test home)
├── session/     # US3(a) — set_next_outbound lock-fail witness via mutex_test_access
│                # US3(b) — OsFile move-ctor witness
└── sync/        # US4 — corpus-gate extension validated here
```

**Structure Decision**: Library layout (existing). No new modules. US1 is the only production code
change (wire decode); everything else is tests, a build-gate list, and docs. Each user story maps to
a distinct directory/file set, satisfying independent-implementability.

## Complexity Tracking

> No Constitution Check violations — section intentionally empty.
