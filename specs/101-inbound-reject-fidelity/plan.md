# Implementation Plan: Inbound Reject fidelity

**Branch**: `101-inbound-reject-fidelity` | **Date**: 2026-11-07 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/101-inbound-reject-fidelity/spec.md`

## Summary

A validating session answers an undefined MsgType with 373=11 and an undefined tag with 373=0, and every
validation-gate Reject carries QuickFIX C++'s reason text (B85, owner-ruled). A frame has one MsgType, its third
field's, for every reader; a frame repeating 35 with another value is refused the 099 way on every validating session
and disconnected in an establishment arm on any session (B88). A Reject whose MsgType echo does not fit is sent without
372 instead of not at all; the echo stays verbatim otherwise (B82, QuickFIX parity). Decisions D1–D9 in
[research.md](research.md); endings in [contracts/reject-frames.md](contracts/reject-frames.md).

## Technical Context

**Language/Version**: C++23 (clang, gcc, MSVC).

**Primary Dependencies**: none new.

**Storage**: N/A.

**Testing**: GoogleTest, through real `Session` objects for every ending (099's lesson: validator-level probes move a
defect up one layer); RED on `fa778a8e` per cell; a mutation per production change.

**Target Platform**: Linux and Windows.

**Project Type**: C++ library: session, wire validator, dictionary table view.

**Performance Goals**: no regression on the inbound path (D8): one comparison per 35 in the scan, two O(1) lookups per
validated message.

**Constraints**: no per-message allocation (`[const §XV.1]`); `noexcept` preserved; 099's FIXT endings kept (except the
`D…A…D` widening, research D4); the C-ABI code set unchanged.

**Scale/Scope**: production `src/session/scan_frame_header.hpp`, `src/session/admin_messages.cpp` (`interpret_logon`),
`src/session/session.cpp` (gate sites, `refuse_active_frame_`, `establishment_frame_refused_`, Reject emit paths),
`include/fixpp/wire/validator.hpp`, `include/fixpp/wire/reject_reason_map.hpp`, `include/fixpp/dict/table_view.hpp`
(+ its builder), `src/dictionary/dictionary.cpp` (`as_table_view`), `include/fixpp/core/error.hpp`,
`src/capi/error.cpp`. Tests in `tests/session/`, `tests/wire/`, `tests/capi/`, `tests/interop/happy/`.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Article | Gate | Disposition |
|---|---|---|
| I / VI | core tag-value FIX, spec coverage | PASS: FIX-TC 2q, 14a, 14c endings; QuickFIX parity per the owner's rule. |
| VII | RED first, mutation-proven | PASS planned (D9 and tasks). |
| VIII | no regression | D8's paired runs; `validator_bench` CI-paired. |
| IX | coverage, sanitizers, static | `/speckit-verify`. |
| X | C ABI | PASS: two `core::error` slots map to the existing `FIXPP_ERR_WIRE_CONFORMANCE`; no code, symbol or struct added; `expected_error_map.csv` and the completeness pins grow. Gate A confirms no version bump is owed. |
| XV.1 | no per-message heap | PASS: the defined-tag bitset is built with the table; the retry in D7 reuses the stack buffer. |
| XVII.1 | Gate A on public behaviour | Gate A required (session wire endings, public `core::error`, public `table_view`). |

**Initial gate: PASS. Post-design re-check: PASS.**

## Project Structure

### Documentation (this feature)

```text
specs/101-inbound-reject-fidelity/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   └── reject-frames.md
├── checklists/
│   └── requirements.md
└── tasks.md            # /speckit-tasks
```

### Source Code (library root)

```text
include/fixpp/dict/table_view.hpp       # has_msg_type, tag_defined (+ builder)
src/dictionary/dictionary.cpp           # as_table_view fills the defined-tag set from the field store
include/fixpp/wire/validator.hpp        # D2 MsgType check; undefined vs not-in-message split
include/fixpp/wire/reject_reason_map.hpp# 11 and 0 for the new slots
include/fixpp/core/error.hpp            # wire_invalid_msg_type, wire_undefined_tag
src/capi/error.cpp                      # both → FIXPP_ERR_WIRE_CONFORMANCE
src/session/scan_frame_header.hpp       # D4 first-wins + msg_type_conflict
src/session/admin_messages.cpp          # D4 interpret_logon first-wins
src/session/session.cpp                 # D3 texts at the gate sites; D5/D6 refusal conditions; D7 retry
tests/session/                          # US1-US4 through a real Session; rewritten 098/099/092 cells
tests/wire/                             # validator cells per slot
tests/capi/                             # expected_error_map.csv, error_surface_test pins
tests/interop/happy/golden/             # regenerated reject-invalid-admin goldens
```

**Structure Decision**: existing layout; new test files join existing binaries.

## Phases (for /speckit-tasks)

1. **RED** — one Session-level cell per acceptance scenario on today's tree; validator cells for D2.
2. **Dictionary + validator** — D1, D2; slot and C mapping; completeness pins.
3. **Texts** — D3 at the three gate sites; rewrite cells and goldens that pin no 58.
4. **One MsgType** — D4, D5, D6; rewrite 099's validation-off and rep35 rows with their superseding FR.
5. **Echo** — D7 at each inbound-fed Reject/BMR site.
6. **Mutations, benches** — each change reverted alone, its cells RED; D8 paired runs.
7. **Records** — B&L (L-452-2 closed; 099's B-rows for #657/#658 updated; a B-row for the ignored QuickFIX
   validation keys; B-rows for the new endings), catalogue/coverage index.
8. **Verify** — targeted ctest, sanitizers on `session_pure_tests`, MSVC build of touched targets, `/speckit-verify`.

## Complexity Tracking

| Item | Why | Simpler alternative rejected because |
|---|---|---|
| D7's retry without 372 | a long MsgType made the Reject vanish | a fixed bound needs a number derived from the other fields' lengths |
| D4 widens 099's `last ≠ third` to "any later ≠ first" | one MsgType per frame | keeping the last-value comparison leaves `D…A…D` handled as D with a hidden A |

## Open for Gate A

1. D6: the validation-off ending after Logon (handle as the third field, no Reject) against QuickFIX C++'s always-Reject.
2. D2: 373=0 for an undefined tag inside a group (QuickFIX/J: 2; C++: layout-dependent).
3. D7: omit-372-on-overflow versus a fixed bound.
4. X: whether two new `core::error` slots that map to an existing C code owe a C-ABI version step.
