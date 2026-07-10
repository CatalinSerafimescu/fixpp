# ABI Checklist: FR-009 Freeze & Article X Boundary (Requirements Quality)

**Purpose**: Validate that the requirements asserting "no collateral surface change" — the C-ABI 1.5.0 freeze, the read-path/codegen-read-layout/Python no-change boundary, the host-tool-only `MessageIR.group_order` + pugixml re-parse, and the reuse-38 error-model constraint — are stated with a concrete, checkable verification basis and are conflict-free BEFORE implementation. Unit tests for the REQUIREMENTS, not the code.
**Created**: 2026-07-10
**Feature**: [spec.md](../spec.md) · **Focus**: FR-009 freeze, Article X ABI policy, error-model reuse, codegen-tool-local boundary (RC-B/RC#7)

**CHK numbering**: restarts at CHK001 in each of the three 067 checklists (api / abi / nfr).

## Requirement Completeness

- [x] CHK001 Is "no C-ABI change" specified with a CONCRETE verification basis — `nm`, `abidiff`, `check_capi_occupancy.sh`, `tools/abi_history/error_codes_v1.txt`, C-ABI 1.5.0 byte-identical — rather than prose "the C-ABI is unchanged"? [Completeness, Spec §FR-009] — PASS: plan.md Constitution Check row "X — ABI policy" names the concrete instruments verbatim ("Verify `nm`, abidiff, `check_capi_occupancy.sh`, `error_codes_v1.txt` unchanged; C-ABI 1.5.0 byte-identical"); tasks.md T026 operationalizes the same list.
- [x] CHK002 Is the "no-change" boundary explicitly enumerated as a set of named surfaces that MUST stay byte-identical — read path, wire-format semantics, codegen READ layout (`Messages.hpp`/`Fields.hpp`/`Reify.hpp`/`Validator.hpp`), C-ABI, and Python bindings — not a vague "read side"? [Completeness, Spec §FR-009] — PASS: contract §G7 enumerates the named surfaces verbatim — "Read path, wire semantics, codegen READ layout (`Messages.hpp`/`Fields.hpp`/`Reify.hpp`/`Validator.hpp`), C-ABI, and Python bindings".
- [x] CHK003 Is the claim that `MessageIR.group_order` + the codegen-tool-local pugixml re-parse touch ZERO runtime/ABI/Python surface stated as a checkable requirement (host-tool-only in `ir.hpp`/`ir.cpp`/`emit_builders.cpp`; no runtime `Dictionary`/`GroupRef`/C-ABI/Python symbol)? [Completeness, Spec §Art-X / plan Constitution Check] — PASS: plan.md Constitution Check row X + the "RC#7 IR-addition note" state this as a checkable claim ("host-build-tool-only, adds no runtime type/symbol"); research R9 + data-model §3 repeat "NO runtime `Dictionary`/`GroupRef`/C-ABI/Python change" identically.
- [x] CHK004 Is the pugixml codegen build-home requirement complete — PRIVATE link, host-tool-only, NEVER installed, no version pin (inherit project-resolved), and a bootstrap configure/link verification? [Completeness, plan §RC-B] — PASS: plan.md Project Structure CMakeLists.txt bullet + tasks.md T003 state PRIVATE link, host-tool-only, never-installed, no version pin (inherit project-resolved), and a bootstrap configure/link verification step.

## Clarity

- [x] CHK005 Is it unambiguous that `MessageIR.group_order` is a codegen-LOCAL IR-shape field (not a runtime `Dictionary`/`GroupRef` addition), so no reader mistakes it for an ABI/runtime-dictionary event? [Clarity, Spec §Art-X / research R9] — PASS: research R9, plan.md RC#7 note, and data-model §3 all label `MessageIR.group_order` "codegen-tool-local" / "NEW codegen-tool IR field" explicitly, distinct from runtime `Dictionary`/`GroupRef`.
- [x] CHK006 Is the pugixml dependency scoped unambiguously — a NEW codegen-tool TU dependency (re-parse of `xml_path` inside `ir.cpp`), explicitly NOT a new runtime `XmlLoader`/`Dictionary` accessor and NOT a link that propagates from `fixpp_dictionary` (whose pugixml link is PRIVATE)? [Clarity, plan §RC-B / research R9] — PASS: research R9 states verbatim "a NEW pugixml dependency in the codegen tool TU... explicitly NOT a new runtime `Dictionary`/`XmlLoader` accessor"; plan.md CMakeLists.txt bullet states `fixpp_dictionary`'s pugixml link is PRIVATE and does NOT propagate to `fixpp-codegen` — confirmed by source: `fixpp-codegen` links only `fixpp::dictionary` today per plan text.
- [x] CHK007 Is the codegen-time-vs-runtime edge boundary clarified — a wire→dict edge AT CODEGEN TIME (delimiter baked into generated tables) is permitted, while `include/fixpp/wire/builder_validate.hpp` MUST introduce no runtime `wire→codegen`/`wire→dict` include edge (check_layers.py)? [Clarity, plan §check_layers / research R7] — PASS: research R7 states "the author-supplied delimiter contract of `body_builder` (no wire→dict edge at runtime; the edge is at codegen time, which is correct)"; plan.md Open Items lists the New #4 `check_layers.py` verification task on `builder_validate.hpp` explicitly.

## Consistency

- [x] CHK008 Is the "no new `fixpp_error_t`; reuse `wire_required_field_missing` (=38)" constraint stated consistently across spec §FR-009, §FR-006, the Clarifications, and research R8 — with no residual mention of a new error value? [Consistency, Spec §FR-009] — PASS: consistent across spec Clarifications Q3, FR-006, FR-009, and research R8 ("NO new `fixpp_error_t`"); value confirmed =38 at `include/fixpp/core/error.hpp:69` (`wire_required_field_missing = 38, // [2b §6.5.4]`).
- [x] CHK009 Is the round-1 "no IR addition" premise reconciled with the RC#7 `group_order` addition — the addition explicitly falsifies the premise ONLY for the delimiter/order axis while the required-presence tables remain order-independent (no IR add) — so the two statements do not conflict? [Consistency, Conflict] — PASS: plan.md's dedicated "RC#7 IR-addition note" reconciles this explicitly — "the round-1 'no IR addition' premise is CORRECTED — a codegen-tool-local `MessageIR.group_order`... IS added... The required-presence tables are unaffected (still order-independent from `m.fields`, R2/R3)"; research R9 closing paragraph repeats "This FALSIFIES the round-1 'no IR addition' premise for the delimiter/order... corrected across research R2/R7/R9".

## Acceptance Criteria Quality

- [x] CHK010 Is SC-005 stated with objective witnesses — existing read-path + Python suites green, C-ABI 1.5.0 freeze byte-identical, no new error enum value — rather than "unchanged"? [Acceptance Criteria, Spec §SC-005] — PASS: SC-005 states verbatim "Read path, wire semantics, C-ABI, and Python surface are unchanged (existing suites and the C-ABI 1.5.0 freeze byte-identical); no new error enum value".
- [x] CHK011 Is the FR-009/G7 collateral-surface check operationalized as a named measurable instrument set (the T026 gate: `nm`/abidiff/occupancy/`error_codes_v1.txt` + read-emitter determinism golden)? [Acceptance Criteria, contract §G7] — PASS: tasks.md T026 names the instrument set verbatim (`nm`, abidiff, `check_capi_occupancy.sh`, `error_codes_v1.txt`, C-ABI 1.5.0 byte-identical, NO new `fixpp_error_t`, read-emitter determinism golden, existing read-path + Python suites).

## Dependencies & Assumptions

- [x] CHK012 Is the assumption that `fixpp_dictionary`'s pugixml link is PRIVATE and does NOT propagate to `fixpp-codegen` stated explicitly — so the explicit `find_package`/`target_link_libraries` add is REQUIRED, not redundant? [Assumption, plan §RC-B] — PASS: plan.md Project Structure CMakeLists.txt bullet states verbatim "`fixpp-codegen` currently links only `fixpp::dictionary`... and `fixpp_dictionary`'s pugixml link is PRIVATE so it does NOT propagate", explicitly grounding why the explicit add is required.
- [x] CHK013 Is the bootstrap-build dependency stated as a requirement — the codegen build must still configure and link cleanly with the new pugixml dep (`ir.cpp`'s `#include <pugixml.hpp>` compiles, `fixpp-codegen` links) before any generated row closes? [Dependencies, plan §RC-B] — PASS: tasks.md T003 states verbatim "VERIFY the bootstrap codegen build configures + links cleanly (`ir.cpp`'s `#include <pugixml.hpp>` compiles, `fixpp-codegen` links)" as a precondition before any generated row closes.

## Ambiguities & Conflicts

- [x] CHK014 Is the error-model reuse conflict-free — does any acceptance scenario, edge case, or table imply a NEW disposition value that would contradict FR-009's "no new `fixpp_error_t`" (e.g. a distinct code for required-group-zero vs missing-scalar)? [Conflict, Spec §FR-006] — PASS: contract §G5 and data-model §1.3 both route EVERY required-presence violation (missing scalar AND required-group `size()==0`) through the single pre-existing `error::wire_required_field_missing` (=38) — no distinct disposition value proposed anywhere in spec/data-model/contract for the group-zero case.
- [x] CHK015 Is the "codegen-tool-local, zero-ABI-surface" claim free of a hidden runtime leak — does the spec confirm the group_order data never reaches a runtime dict/C-ABI/Python accessor, only the generated tables baked at codegen time? [Ambiguity, Spec §Art-X] — PASS: research R9 states verbatim "entirely inside the codegen tool: NO change to the runtime `Dictionary`/`GroupRef`/ABI"; data-model §3 states "Codegen-tool-local only: no runtime `Dictionary`/`GroupRef`/C-ABI change (FR-009 intact)" — the group_order data reaches only the generated constexpr tables baked into `Builders.hpp` at codegen time, never a runtime accessor.

## Notes

- Requirements-quality checklist (probes the SPEC/design bundle, not the emitted code). Dispositioned by the pipeline step-9 checklist audit before `/speckit-implement`.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 15 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 15 |

### SPEC-FIXED items

None.

### DD-DECIDED items

None.

### WAIVED items

None.

Anchors spot-verified: `include/fixpp/core/error.hpp:69` (`wire_required_field_missing = 38`, confirmed against abi CHK008/CHK014); `plan.md` Constitution Check row "X — ABI policy" + the dedicated "RC#7 IR-addition note" (reconciles round-1 "no IR addition" premise against the RC#7 `MessageIR.group_order` addition, CHK009); `plan.md` Project Structure CMakeLists.txt bullet (`fixpp_dictionary`'s pugixml link is PRIVATE, does not propagate to `fixpp-codegen`, CHK006/CHK012); `tasks.md` T003/T026 (concrete instrument lists, CHK001/CHK004/CHK011/CHK013) — all resolve in the Gate-A-converged bundle (converged commit `a4cb5624`).
