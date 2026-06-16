# Checklist: Non-Functional Requirements Quality — 041 Validation Gate Wiring

**Purpose**: Unit-test the non-functional requirements (default-path zero cost, allocation discipline, awaitable-closure constraints, thread-safety, fuzz obligation) before implementation.
**Created**: 2026-06-16
**Audience**: Gate B reviewer
**Feature**: [spec.md](../spec.md)

## Performance / Allocation

- [x] CHK021 Is the default-path zero-cost requirement stated as objectively verifiable (no validator construction AND no early `MessageView` parse on the disabled path), not merely "no added cost"? [Measurability, Spec §SC-005, FR-002] — PASS: SC-005 states "The default (validation-disabled) inbound path performs no validator construction or invocation — verifiable by inspection/instrumentation"; FR-002 states "no construction or invocation of the validator on the message path"; tasks T016 adds "Add an instrumentation/inspection assertion or a structural test that the default path does not enter the validate block." Objectively verifiable, not merely prose. Complete.
- [x] CHK022 Is the allocation discipline of the *enabled* path specified — dictionary→`table_view` tables built once at config time, not per message ([const §XV.1])? [Completeness, plan R-1a, Gap] — PASS: research R-1 states "All table construction happens once at as_table_view() time (config/session setup), never per message → §XV.1-clean"; data-model E-2 states "Lifetime: constructed at validator/session setup; immutable thereafter; no per-message mutation/allocation"; tasks T008 says "built once at construction ([const §XV.1] — config-time, not per-message)"; plan §Performance Goals confirms "the table_view is built once at validator construction (config-time alloc, not hot path)". Fully specified. Complete.
- [x] CHK023 Is the per-message cost of the enabled MVP path (the opt-in double-parse) acknowledged and bounded, with unify-the-parse explicitly deferred? [Clarity, plan R-2] — PASS: research R-2 §Decision(double-parse) states "the MVP accepts that the enabled path parses the frame twice (once for validate, once in parse_and_dispatch_ for dispatch)" and explicitly records "Threading the already-built MessageView through…is recorded as a follow-up optimization, not MVP scope"; plan §Constitution Check §VIII row confirms "Enabled path adds one parse + one O(1) validate before the seqnum gate; an opt-in mode, measured but not on the default budget"; plan §Performance Goals bounds the cost. Acknowledged and deferred. Complete.
- [x] CHK024 Is the byte-identical default-output requirement (SC-001) expressed measurably (existing corpus outcomes + emitted bytes unchanged)? [Measurability, Spec §SC-001] — PASS: SC-001 states "100% of the existing inbound-handling and interop test corpus produces outcomes identical to the prior release — zero new rejects, zero dispatch-outcome changes"; tasks T001 establishes the pre-change baseline GREEN, tasks T015/T016 verify the no-op at default config against that baseline, and tasks T001's "byte-identical no-op claim" is explicitly named. Measurable via corpus run comparison. Complete.

## Concurrency / Awaitable Closure

- [x] CHK025 Is the §XV.9 constraint addressed for the new headers pulled into the `on_inbound_frame` awaitable closure — i.e., a requirement that `field_type.hpp`/`table_view.hpp`/`validator.hpp` drag no `std::mutex`/`std::shared_mutex` into the co_await frame? [Coverage, Gap, [const §XV.9]] — PASS: plan §Constitution Check §XV.9 row states "inbound path stays on fixpp::sync primitives; the new flag is a plain bool; no new mutex. The new headers must be run through the §XV.9 corpus gate (extended in 039 US4)"; tasks T023 is a dedicated task to run the §XV.9 no-std-mutex awaitable-corpus gate over any new session-side header pulled into the inbound awaitable closure. Requirement is stated and a verification task is assigned. Complete.
- [x] CHK026 Is the thread-confinement expectation of the validate gate (runs on the session's inbound execution context, same as the existing checks) specified so the new code introduces no cross-thread access? [Coverage, Gap] — PASS: plan §Constitution Check §XV.9 row confirms "inbound path stays on fixpp::sync primitives; the new flag is a plain bool; no new mutex"; `[2b §6.6]` threading paragraph states "Parser, Writer, OffsetTable, MessageView, Validator are value types…safe on a single thread; not synchronized for concurrent access" — these run inside the session's inbound strand as per the existing inbound FSM (the validate gate is inserted within `on_inbound_frame` which is already strand-confined). No cross-thread access is introduced. Specified. Complete.

## Robustness / Hostile Input

- [x] CHK027 Is the fuzz obligation for the now-production validator path dispositioned (harness OR documented waiver), per [const §VII.7] parser-touching gate? [Coverage, tasks T026] — PASS: tasks T026 explicitly dispositions the fuzz obligation: "EITHER add tests/fuzz/fuzz_wire_validator.cpp…asserting no crash/UB/exception-escape and every rejection is a defined wire_* error…AND register it in tests/fuzz/CMakeLists.txt; OR, if /speckit-verify/Gate-B judges the validator non-parser-touching (it consumes an already-parsed MessageView; the byte-parse is already covered by fuzz_wire_parser/fuzz_wire_framer), record an explicit waiver." The obligation is dispositioned as a Gate-B blocker to be resolved at implement time (harness-or-waiver, not silently deferred). Addressed. Complete.
- [x] CHK028 Are the validator's failure modes specified to always surface a defined `wire_*` error (never silent UB / never an escaped exception across the noexcept inbound boundary) under arbitrary inbound bytes? [Completeness, Gap] — SPEC-FIXED: The noexcept contract holds, but the failure-mode set was INCOMPLETE: the validator's Float type arm (`validator.hpp:307-313`) can emit `decimal_invalid_input` (slot 10) or `decimal_overflow` (slot 11) — non-`wire_*` errors outside the enumerated {38–42} set — when `decimal_t::parse` returns an error other than `decimal_precision_loss`. These are not `wire_*` errors and are not defined as valid validator outputs. Fixed: FR-004 (spec.md) and data-model E-4 now explicitly require the Float arm to remap all non-`decimal_precision_loss` parse errors to `wire_field_value_out_of_range` (40), so `validate()` always surfaces a `wire_*` error. Task T009a added to implement the remap. Affected: `spec.md §FR-004`, `data-model.md §E-4`, `tasks.md §T009a`.
- [x] CHK029 Is the behaviour under a malformed establishing Logon specified for every Logon-arm overlap row (dict-invalid vs dict-clean-but-CompID/SendingTime-failing), so no hostile-Logon path is left undefined? [Coverage, Spec §FR-003, FR-010, Edge Cases, data-model Logon-arm overlap precedence] — PASS: data-model Logon-arm overlap precedence table provides seven rows (a–f incl. the c-i/c-ii split) covering: dict-invalid+BeginString mismatch, missing FIXT 1137 (dict-valid), absent vs present-but-malformed `52` SendingTime, CompID-authz failure (dict-valid), well-formed non-Logon first message, malformed `35=3`/`35=5` exemption; contracts C-2 repeats all seven rows; Edge Cases paragraph covers them in prose. Every hostile-Logon overlap has a specified disposition. Complete.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 8 |
| SPEC-FIXED | 1 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **9** |

### SPEC-FIXED items
- CHK028 — validator failure-mode completeness gap: `decimal_invalid_input`/`decimal_overflow` from the Float arm were not `wire_*` errors; added remap requirement to FR-004 + data-model E-4 + task T009a; affected: `spec.md §FR-004`, `data-model.md §E-4`, `tasks.md §T009a`.

### DD-DECIDED items
None.

### WAIVED items
None.

Anchors spot-verified:
- `[const §XV.1]` — resolves at "Article XV §1 (Banned Patterns: per-message hot-path heap alloc)" in `.specify/constitution.md`.
- `[const §XV.9]` — resolves at "Article XV §9 (std::mutex banned in awaitable context)" in `.specify/constitution.md`.
- `[const §VII.7]` — resolves at "Article VII §7 (fuzz obligation for parser-touching features)" in `.specify/constitution.md` (Article VII is the Testing Requirements article; §7 is the fuzz gate).
- `[2b §6.6]` — resolves at `### 6.6 Allocation, exceptions, threading` in `.specify/2b-wire.md`.
- All resolve in the signed-off revision.
