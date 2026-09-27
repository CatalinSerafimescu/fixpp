# Implementation Plan: The session never acts on a frame it could not parse

**Branch**: `092-garbled-frame-reject` | **Date**: 2026-09-27 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/092-garbled-frame-reject/spec.md` (clarified 2026-09-27)

## Summary

Today a frame that passes framing but fails the parse is acted on through its scanned header fields.
A SequenceReset is applied; an application message is persisted and consumed, but neither delivered
nor rejected. This feature makes the header scan (`scan_frame_header`, which already runs once per
inbound frame, in every state, with the session's dictionary hooks) record its **first fault** and
stop there. A single new step, `dispose_unparseable_`, runs right after the scan in every state arm
and decides the frame per the owner ruling of 2026-09-27:
- Reject under #423's seqnum rows when both MsgSeqNum and MsgType were read before the fault;
- ignore as garbled when either was not;
- the Logon refusal path while awaiting a Logon;
- ignore in LogoutSent.

Nothing after the fault is read, and nothing except 34/35 is used. The scan/parse agreement is pinned
by a differential test and a fuzz assertion. Residual parse failures at dispatch sites stop reading
as success. `dictionary_driven_validator::validate`'s silent stop becomes a reject. Design decisions:
[research.md](./research.md); the behaviour contract:
[contracts/unparseable-frame-disposition.md](./contracts/unparseable-frame-disposition.md).

## Technical Context

**Language/Version**: C++23 (Clang 22 local and CI; GCC and MSVC lanes per Article II)

**Primary Dependencies**: Standalone Asio (coroutines), GoogleTest/GoogleMock, Google Benchmark,
libFuzzer. No new dependency.

**Storage**: The existing MessageStore. Every Reject is persisted like any outbound session message
(FR-013); an advance is persisted through `consume_rejected_seqnum_` (#423, unchanged).

**Testing**: GoogleTest in the grouped session bucket (`ctest -L 092`); a differential mutation
corpus; a fuzz assertion in `fuzz_session_recovery_admin_parse`; an in-process scripted peer
(SC-007); pytest unchanged.

**Target Platform**: Linux (clang/gcc, sanitizer lanes), Windows (MSVC), as for the library.

**Project Type**: Library (the fixpp FIX engine), session layer plus a small wire-layer addition.

**Performance Goals**: No measurable regression on the clean inbound path. The instrument is a new
`scan_frame_header` micro-bench, baselined before the change (research R-9).

**Constraints**: `[const §VIII.5]` zero allocation between parse and callback: the fault record is
plain members, and the Reject Text is built in the existing 512-byte stack buffer. `noexcept` on the
scan and dispose paths. Private headers stay private.

**Scale/Scope**: One private header (the scan), `session.cpp` (4 state arms, 1 new coroutine, the
dispatch-result handling), `admin_messages.{hpp,cpp}` (one overload), `parser.hpp` + `validator.hpp`
+ `errors`/`reject_reason_map` (FR-012), and the C-ABI doc text. About 6 production files plus tests.

## Constitution Check

*GATE: must pass before Phase 0; re-checked after Phase 1 (below).*

| Article | Requirement | Status |
|---|---|---|
| VII §3–4 TDD | failing test first, no code without a test | Planned: every contract row C-2 D-1…D-9 and every C-3 invariant is a RED-first cell; T076 is the anchor RED |
| VII §5 conformance | the FIX-TC corpus must pass | TC 2d / 3b / 3c stay "ignore" (framing, FR-008). TC 14-class Rejects gain siblings (0 / 5). Check any existing TC cell asserting silence on a parse failure (R-10) |
| VII §7 fuzz | parser-touching code needs a fuzz harness | The scan is parser-touching → equivalence assertion in the existing harness (R-2). #508 caveat disclosed |
| VII §8 test grouping | new isolation-safe tests go into a grouped bucket, selected by label | Planned (`092` label) |
| VIII perf | no hot-path regression without a budget change | A new micro-bench is baselined first (R-9) |
| VIII §5 zero-alloc | no heap between parse and callback | Held: plain members, stack Text buffer |
| IX sanitizers / coverage | per-line coverage assessment | `/speckit-verify` matrix; the fault branches are covered by the corpus |
| X ABI | C-ABI changes versioned; public C++ API additive | No C-ABI surface change. C++ additions only (C-4). Version note is a **Gate A question** (R-8) |
| XI concurrency | — | Not touched: the disposition runs inside the existing per-session strand coroutine |
| XII security | fail closed | This feature *closes* a fail-open (acting on unparsed bytes) |
| XVI §3–4 | `/clarify` and `/analyze` mandatory (session FSM, error semantics, parser) | `/clarify` done (4 answers); `/analyze` due after `/tasks` |
| XVI §6 | the orchestrator does not implement | Implementation is delegated to `phase-implementer`; mutation proofs in a scratch copy |
| XVII §1 Gate A | parser + session FSM + public C++ API → required | **Required before `/tasks`** |
| XVII §7 | local build gate before PR; builds need owner approval | Planned; ASK before each build |

**Result: PASS.** No violation needs justifying. Open items are routed to Gate A (R-8) or already
scheduled (`/analyze`).

## Project Structure

### Documentation (this feature)

```text
specs/092-garbled-frame-reject/
├── spec.md              # clarified 2026-09-27
├── plan.md              # this file
├── research.md          # R-1 … R-11
├── data-model.md        # E-1 fault record, E-2 disposition, E-3 dispatch result, E-4 validator fault
├── quickstart.md        # validation guide (baselines first)
├── contracts/
│   └── unparseable-frame-disposition.md   # C-1 order, C-2 table, C-3 invariants, C-4 API deltas
├── checklists/requirements.md
└── tasks.md             # /speckit-tasks (not created here)
```

### Source Code (repository root)

```text
src/session/
├── scan_frame_header.hpp      # E-1: first-fault record; stop at the first fault (R-1)
├── session.cpp                # dispose_unparseable_ (R-3) at the 4 state arms; dispatch result (R-4);
│                              #   validate_inbound_ comment fix
└── admin_messages.cpp         # build_reject Text overload (R-5)
include/fixpp/session/
├── admin_messages.hpp         # build_reject overload declaration (C-4)
└── session.hpp                # private declarations (dispose_unparseable_, emit_session_reject_ text param)
include/fixpp/wire/
├── parser.hpp                 # iterator stopped_on_fault() + kind (R-7)
├── validator.hpp              # validate: fault → reject (FR-012)
├── errors.hpp / core error    # wire_invalid_tag_number, wire_length_data_mismatch (C-4)
└── reject_reason_map.hpp      # → 0 / 5
include/fix/c_api/session.h    # doc text correction only (FR-014)
tests/session/
├── unparseable_frame_disposition_test.cpp   # NEW: C-2 D-1…D-9, C-3 I-1…I-6, T076 flipped, SC-007 peer
├── scan_frame_header_fault_test.cpp         # NEW: E-1 fault kinds + the differential corpus (I-4)
├── scan_frame_header_overflow_test.cpp      # R-10: + fault assertions, conforming half on its own frame
├── length_data_session_scanner_test.cpp     # R-10: + fault kind / Length tag assertions
└── CMakeLists.txt                           # register in the grouped bucket, label 092
tests/wire/                                  # validator fault cells (FR-012)
tests/fuzz/fuzz_session_recovery_admin_parse.cpp   # equivalence trap (R-2)
bench/session/scan_frame_header_bench.cpp          # NEW, baselined FIRST (R-9)
spec/behaviors-and-limitations.md                  # B-092-* entries; the #423 row revision
brain/components/…                                  # session + wire pages; #423 ruling pointer
```

**Structure Decision**: The single-project library layout above. All changes stay within the
existing session and wire modules. There is no new module, target or dependency, apart from one
test source per bucket, one bench source, and the fuzz arm.

## Implementation phases (for `/speckit-tasks`)

1. **Baselines (before any production edit)**: the scan micro-bench on the unchanged scan; the T076
   RED reproducer as a real test (expected RED against the new assertions).
2. **Scan fault record** (E-1): RED cells per fault kind, then the scan change. Then the differential
   corpus (I-4), proven able to fail with a seeded disagreement.
3. **Disposition** (R-3, contract C-2): RED cells D-1…D-9, then `dispose_unparseable_` wired into
   the 4 state arms. T076 goes green. Mechanism-deletion proofs per arm (SC-006).
4. **Reject Text** (R-5): the overload, with byte-identical output for the old overload (existing
   goldens unchanged).
5. **Dispatch-result residual** (R-4): the app path Rejects with 99, the admin path logs. Cells use a
   forced parse failure (a hook or an arena-too-small path, whichever exists without a public-header
   test hook; see memory "private test access to a public class").
6. **Validator fault** (R-7, FR-012): iterator accessor, new errors, mapping, cells.
7. **SC-007 scripted peer**.
8. **Fuzz arm** (R-2), with a planted disagreement proving the trap.
9. **Docs**: the C-ABI header text, B&L, brain pages, and the #423 reference in code comments (with a
   header comment naming this ruling where a decision is superseded, per the parent CLAUDE.md).
10. **Bench re-run**; `/simplify`; `/speckit-verify`.

## Post-design Constitution re-check

Re-run after Phase 1 artifacts: **PASS**. The design adds no C-ABI surface, no allocation and no new
concurrency. It closes a fail-open. The public C++ changes are additive (C-4). Items still open, and
where they are routed:
- a C-ABI version note or bump for a peer-visible behaviour change → **Gate A** (R-8);
- a fault-free frame whose third field is not 35, still not ignored without validation (R-11) → file
  as a separate issue at close-out;
- the two Length+Data readers stay duplicated; their agreement is pinned by an instrument rather than
  removed (R-2) → Gate A may prefer unification.

## Complexity Tracking

No constitution violations to justify.
