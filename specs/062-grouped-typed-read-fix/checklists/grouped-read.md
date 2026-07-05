# Grouped-Read Requirements-Quality Checklist: Grouped Typed-Read Path Fix (062)

**Purpose**: Validate the QUALITY of the requirements (completeness / clarity / consistency / measurability / coverage) for the repeating-group entry-read contract, allocation-lifetime discipline, and codegen determinism — BEFORE `/speckit-implement`. These are "unit tests for the English", not implementation tests.
**Created**: 2026-07-05
**Feature**: [spec.md](../spec.md)
**Focus areas**: (1) grouped-read contract (scalar / nested / lifetime / alloc); (2) codegen determinism + no-regression (FR-005 / FR-007)
**Depth**: Standard (pre-implementation gate) · **Audience**: PR reviewer / checklist-auditor

## Requirement Completeness

- [x] CHK001 Are typed-read requirements defined for every scalar field type the entry exposes (string / char / int / decimal + the `field_value(tag)` escape hatch)? [Completeness, Spec §FR-001] — PASS: FR-001 lists all four scalar kinds plus `field_value(tag)` explicitly.
- [x] CHK002 Is the recursive nested-group read requirement bounded to a stated depth (or explicitly declared depth-unbounded), so depth-3/4 layouts aren't left to implication? [Completeness, Spec §FR-002, Data-model INV-G3] — PASS: FR-002 requires unconditional recursion; data-model INV-G3 states the cache key is "collision-free at every depth", explicitly declaring depth-unbounded coverage (depth-4 MassQuote cited).
- [x] CHK003 Is the absent-field disposition (typed not-found error) documented as a requirement DISTINCT from the present-but-empty case? [Completeness, Spec §FR-001 + §Edge Cases] — PASS: FR-001 (absent→typed error) and Edge Cases ("absent vs present-but-empty") are distinct; Plan Witness Map carries two distinct tests (`AbsentEntryFieldReturnsTypedError`, `AbsentVsPresentButEmptyField`).
- [x] CHK004 Is the allocation requirement stated for BOTH the one-level scalar path AND the nested-descent path with no silent gap between FR-004 / FR-004a / FR-004b? [Completeness, Spec §FR-004..004b] — PASS: FR-004 (blanket lifetime+one-level zero-alloc) / FR-004a (hot-path posture) / FR-004b (nested-descent contract) partition the allocation space with no gap; SC-002 restates both halves measurably.
- [x] CHK005 Are codegen-regeneration obligations (forced regen + golden update) captured as explicit requirements rather than left implicit? [Completeness, Spec §FR-005 + §Assumptions] — PASS: FR-005 mandates deterministic regen; Assumptions names the force-regen trap explicitly; tasks T013/T017 operationalize it.
- [x] CHK006 Does the spec enumerate every surface that MUST NOT change (C-ABI, error enum, top-level field read, wire framing)? [Completeness, Spec §FR-007] — PASS: FR-007 lists C-ABI, error enum, wire framing of top-level fields, and top-level flyweight read behaviour verbatim.

## Requirement Clarity

- [x] CHK007 Is "lifetime-safe" quantified — does the spec state precisely when an entry is valid (parent-alive) and when its use is undefined (parent-destroyed)? [Clarity, Spec §FR-004 + US3 AC2 + Data-model INV-G1] — PASS: FR-004 ("no dangling when parent is alive"), US3 AC2 ("parent destroyed → documented undefined"), INV-G1 restates both precisely.
- [x] CHK008 Does the spec distinguish "zero heap allocation" (one-level scalar) from "at most one bounded arena build" (nested first descent) rather than a blanket "no allocation"? [Clarity, Spec §FR-004a/004b + §SC-002] — PASS: FR-004a/FR-004b + SC-002(a)/(b) draw this split explicitly and quantitatively.
- [x] CHK009 Is the nested-cache key defined unambiguously as the globally-unique slice `data` identity, not a vague "per occurrence"? [Clarity, Data-model INV-G3] — PASS: INV-G3/data-model §Nested sub-view cache pin the key to the outer slice's `data` pointer and explicitly DELETE the ordinal-`i` alternative.
- [x] CHK010 Is `operator[]` == `iter()` equivalence defined by a concrete criterion (same slice, same `i`), not just "identical"? [Clarity, Spec §FR-003 + Data-model INV-G4] — PASS: INV-G4 states both derive identity "from the same `i`" → same slice `data`; source-verified against `group_view.hpp` (`iter()` delegates to `operator[]`).
- [x] CHK011 Is the dict-aware-slicer obligation stated as a hard MUST (never the dict-free fallback), not an implementation preference? [Clarity, Data-model INV-G7] — PASS: INV-G7 + contract §Read a NESTED group state "MANDATORY... never the dict-free fallback" as a hard invariant, not a preference.

## Requirement Consistency

- [x] CHK012 Do FR-004 (one-level zero-alloc) and FR-004b (nested arena build) reconcile without contradiction — is the carve-out explicit and cited to `[const §VIII.5]`/`[const §XV.1]`? [Consistency, Spec §FR-004/004b] — PASS: FR-004b cites both anchors verbatim; spot-verified against `.specify/constitution.md` Article VIII §5 ("Arena/PMR is the default") and Article XV §1 ("arena/PMR for the rare materialise cases") — both resolve and match the quoted language.
- [x] CHK013 Are the cache-key terms consistent across spec / plan / data-model / contracts (`outer_occurrence_id` ≡ `unique_slice_identity` ≡ outer slice `data` pointer)? [Consistency] — PASS: data-model explicitly declares the equivalence; plan §Complexity Tracking, tasks T002/T006/T016, and contracts/group-entry-read.md all use `unique_slice_identity`/`outer_occurrence_id` interchangeably with the same definition, no drift found.
- [x] CHK014 Is the generation-token requirement (INV-G6, no read under a default `{}` token) consistent with the lifetime model (INV-G1)? [Consistency, Data-model INV-G6/INV-G1] — PASS: INV-G6 (armed generation trap) reinforces rather than contradicts INV-G1 (parent-alive borrow); both describe the same lifetime contract from different angles (trap-detection vs documented-UB).
- [x] CHK015 Does the Out-of-Scope list stay consistent with FR-007 (no builders / no writer / no C-ABI change) with no overlap or contradiction? [Consistency, Spec §Out of Scope + §FR-007] — PASS: Out-of-Scope (061 builders, generated-header install, N/C/R families, non-group field reads) and FR-007 (no C-ABI/error-enum/wire-framing/top-level change) partition cleanly with no contradiction.

## Acceptance Criteria Quality / Measurability

- [x] CHK016 Is SC-001's "≥2 distinct grouped messages including one nested" objectively measurable (named messages + exact-value assertions over generated flyweights)? [Measurability, Spec §SC-001] — PASS: Plan §Acceptance→Witness Map + tasks T009 (pins NewOrderList `orders`/`G_73`) and T015 (MassQuote `NoQuoteSets→NoQuoteEntries`) name two distinct messages with exact-value assertions, discharging the numeral unambiguously (per the Round-1 Gate A resolution, the Witness Map is the binding traceability artifact for this).
- [x] CHK017 Can SC-002's allocation claims be objectively measured (counting resource AND global-malloc interception), not merely asserted? [Measurability, Spec §SC-002] — PASS: T019 explicitly gates with a counting resource AND mallocnesia global-malloc interception, closing the non-PMR-escape false-pass risk.
- [x] CHK018 Is SC-003's regression guard defined by a construction that provably re-breaks on revert (compile-level or executed), not a soft/skippable check? [Measurability, Spec §SC-003] — PASS: FR-006/T011 require a compile-level instantiation of `operator[]`/`iter()` on a generated entry, explicitly "not a silently-skipped path".
- [x] CHK019 Is SC-004's "no C-ABI / error-enum / top-level change" tied to an objective witness (symbol golden / abidiff), not a subjective review? [Measurability, Spec §SC-004] — PASS: T022 creates `abi_symbol_golden_test.cpp` (`nm`/abidiff-based `CabiSymbolSetUnchanged`) + `ErrorEnumUnchanged` + `TopLevelNonGroupReadUnchanged`, all objective witnesses.

## Scenario Coverage

- [x] CHK020 Are requirements defined for the primary one-level scalar read over a GENERATED flyweight (explicitly excluding hand-written stubs)? [Coverage, Spec §US1 + §FR-006] — PASS: US1 Independent Test says "over a generated flyweight (not a hand-written stub)"; FR-006 mandates generated flyweights throughout.
- [x] CHK021 Are requirements defined for the nested-in-entry scenario including a NON-first outer occurrence (cache-collision discrimination)? [Coverage, Spec §US2 + §FR-006] — PASS: FR-006 explicitly requires the depth-3+ non-first-outer-occurrence witness (targeting `[1]`, not `[0]`, to discriminate against a colliding ordinal-key implementation); tasks T015 `Depth3NonFirstOuterOccurrenceNoCollision` lands it.
- [x] CHK022 Are entry-lifetime requirements covered under the ASan/UBSan/TSan matrix (not just prose)? [Coverage, Spec §US3 + §SC-002] — PASS: US3 Independent Test + SC-002 name the sanitizer matrix explicitly; T021 runs the full witness set under ASan/UBSan/TSan + the alloc gate.
- [x] CHK023 Is the dict-aware-path discrimination scenario (an outer field FOLLOWING a nested group) covered by a requirement/witness, or only asserted as an invariant? [Coverage, Data-model INV-G7, Gap] — PASS (gap CLOSED by /analyze C1): data-model INV-G7 states the MANDATORY requirement, and tasks T015 lands the discriminating witness `NonLastNestedGroupTrailingFieldNotSwallowed` (also in Plan §Acceptance→Witness Map's INV-G7 row) — no longer "only asserted", a landed witness now exists. **Observation (not a blocker, surfaced for the orchestrator):** this witness requirement is traceable via data-model.md + the Witness Map but is NOT back-propagated into spec.md's FR-006 enumeration, which lists nearly every other witness category explicitly except this one and the sibling INV-G6 generation-token trap (T019b). No CHK item in this checklist tests "FR-006 enumeration == Witness Map exhaustively", and CHK027's identical pattern (RC1/trailing-SOH living in research+data-model, not an FR) is PASSed on the same basis, so this is not scored as a defect — but the orchestrator may wish to have C1/C2 back-propagated into FR-006 for future traceability hygiene.

## Edge Case Coverage

- [x] CHK024 Are empty-group requirements defined (`size()==0`, `begin()==end()`, no dereference)? [Edge Case, Spec §Edge Cases] — PASS: Edge Cases states exactly this; quickstart.md demonstrates it.
- [x] CHK025 Are single-entry / last-entry delimiter-extent requirements defined? [Edge Case, Spec §Edge Cases] — PASS: Edge Cases names it; Witness Map row `LastEntryDelimiterExtentExact` in T009.
- [x] CHK026 Are group-cap / oversized-count no-regression requirements defined? [Edge Case, Spec §Edge Cases:73] — PASS (landed by /analyze F1): Edge Cases states the existing `OffsetTable::Config` per-instance cap behaviour is preserved; T008 lands `OversizedCountPerInstanceCapPreserved`, confirmed present in the current tasks.md (not a stale reference).
- [x] CHK027 Is the trailing-SOH / counted-last-field edge defined as a requirement (slice-scoped `len+1`; whole-frame `build()` guard + `group_slice.len` untouched)? [Edge Case, Research §RC1 + Data-model group_slice] — PASS: research.md §RC1 and data-model §group_slice define this precisely; source-verified against `offset_table.cpp:264-268` (whole-frame guard, unchanged) and `:495-505` (slice `len` computation, unchanged) — both anchors resolve and match the cited behaviour.
- [x] CHK028 Is the `field_value(tag)` nested-first-occurrence limitation (N3) documented as an explicit limitation (future B&L row), not silently accepted? [Edge Case, Contracts §N3 + Data-model] — PASS (landed): contracts/group-entry-read.md §N3 and data-model both state the limitation explicitly and non-silently; T024 lands the `L-062-*` row in `spec/behaviors-and-limitations.md` as a mandatory close-out task, confirmed present in current tasks.md.

## Dependencies & Assumptions

- [x] CHK029 Is the 057 prerequisite (reify + multi-char dispatch, PR #161 merged) documented as a validated assumption? [Assumption, Spec §Assumptions] — PASS: Assumptions states this; independently confirmed merged (PR #161, squash `5a7a944`, 2026-07-02, per project memory / phase-4 record).
- [x] CHK030 Is the codegen force-regen trap (Codegen.cmake blind to emitter edits) documented so it can't be silently missed? [Assumption, Spec §Assumptions] — PASS: Assumptions states it explicitly; tasks T013/T017 operationalize the rebuild+clear-markers step so it cannot be silently skipped.
- [x] CHK031 Is the assumption that group slices are already lifetime-stable (arena-owned, `.data` into the parent frame) stated and validated against source? [Assumption, Spec §Assumptions] — PASS: Assumptions states it; source-verified against `include/fixpp/wire/offset_table.hpp:164-176` (append-only, reserved-once `group_slices_`/`group_index_`) and `src/wire/offset_table.cpp:456-460` (stable cached span returned) — both anchors resolve and match.

## Ambiguities, Conflicts & Scope Boundary

- [x] CHK032 Is the catalogue-ownership boundary unambiguous — 062 owns NO OFFICIAL rows; the A-/M-/P- rows it unblocks are closed by 061? [Ambiguity/Scope, Spec §Normative References] — PASS: §Normative References states this explicitly ("062 is a prerequisite/unblocking feature... does not itself mark any catalogue row done"); T025 codifies the exemption + coverage-index mechanism entry as a mandatory close-out task.
- [x] CHK033 Are the four Appendix-A mandatory controls (/clarify, /analyze, Gate A, /plan sign-off) unambiguously tracked as done vs pending after the /analyze doc-sync? [Traceability, Plan §Constitution Check] — PASS: Plan §Constitution Check states all four `done` with dates/commit (`/clarify` done, `/analyze` done 2026-07-05 with 0 CRITICAL, Gate A `gate-a-done` converged `628e35fd`, `/plan` sign-off done); cross-verified against `.specify/decisions/062-grouped-typed-read-fix-gatea.md` (round-3 convergence, 0/0/0 Codex findings) and the Gate A review artifacts in `research/reviews/` — all resolve.
- [x] CHK034 Is a requirement & acceptance-criteria ID scheme established and traceable end-to-end (FR- / SC- / INV-G / US → named witness)? [Traceability, Plan §Acceptance → Witness Map] — PASS: the Witness Map table maps every FR/SC/US/INV-G row to a named witness file + test name, including the two /analyze-added rows (INV-G7, INV-G6).

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 34 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 34 |

### SPEC-FIXED items
None. No requirements-quality defect required a bundle edit.

### DD-DECIDED items
None. No item required deferral to a design-doc decision distinct from the bundle itself (062's own Gate A convergence record and constitution anchors serve as the frozen-authority citations already folded into the PASS evidence above).

### WAIVED items
None.

### Observations (non-blocking, surfaced for the orchestrator)
- **CHK023** — the INV-G7 dict-aware-slicer discriminating witness (`NonLastNestedGroupTrailingFieldNotSwallowed`, T015) and the sibling INV-G6 generation-token trap witness (`GenerationTokenTrapOnStaleEntryRead`, T019b) — both added by the prior `/speckit-analyze` pass (C1/C2) — are traceable via data-model.md + the Plan §Acceptance→Witness Map but are NOT back-propagated into spec.md's FR-006 enumeration, which otherwise lists nearly every other witness category explicitly. Not scored as a defect (no CHK item tests FR-006-vs-Witness-Map exhaustiveness; CHK027's structurally identical pattern — RC1/trailing-SOH living in research+data-model, not an FR — is PASSed on the same basis; the Witness Map, not FR-006, is the binding traceability artifact per the Gate A Round-1 disagreement resolution). The orchestrator may wish to have C1/C2 back-propagated into FR-006 for traceability hygiene, but this does not block the gate.

Anchors spot-verified (source-level, not just "looks plausible"):
- `include/fixpp/wire/group_view.hpp` (`operator[]` span-ctor, seam-#8 comment) — resolves, matches research.md/data-model.md description.
- `tools/codegen/fixpp-codegen/emit_messages.cpp:76,102` (`view_->template get<...>`) — resolves, matches the described defect.
- `src/capi/message_write.cpp:63-74,406` + `include/fixpp/wire/framer.hpp:105` (`frame_view_access` friend-seam precedent) — resolves.
- `include/fixpp/wire/parser.hpp:155` (`field_iterator`), `:398-406` (missing-final-SOH tolerance) — resolves, matches data-model §group_slice citation of `parser.hpp:399,405-406`.
- `include/fixpp/wire/view.hpp:33` (`generation_token`) — pre-existing, confirmed.
- `include/fixpp/wire/offset_table.hpp:29,70,77,151` (`group_member_fn_t`) — pre-existing, confirmed.
- `src/wire/offset_table.cpp:264-268` (whole-frame `build()` SOH guard, unchanged), `:436-439` (dict-free fallback `group_end = entries_.size()`), `:495-505` (slice `len` computation) — all resolve, match RC1/INV-G7 citations exactly.
- `include/fixpp/wire/offset_table.hpp:164-176` + `src/wire/offset_table.cpp:456-460` (lazy, reserved-once, stable `group_slices_`) — resolves, matches the lifetime-stability assumption (CHK031).
- `.specify/constitution.md` Article VIII §5 ("Arena/PMR is the default"), Article XV §1 ("arena/PMR for the rare materialise cases"), Appendix A ("Wire format/parser", "Codegen layout" trigger rows) — all resolve verbatim.
- `entry_context` — grep-confirmed **absent** from `include/`/`src/`/`tools/` today, i.e. genuinely NEW as the bundle claims (not a stale "already exists" claim).
- `.specify/decisions/062-grouped-typed-read-fix-gatea.md` + `research/reviews/{codex,opus}_062-grouped-typed-read-fix_gate_a{,_2,_3}_*.md` (parent-repo `research/reviews/`) — all exist, round-3 convergence (0/0/0 Codex findings) confirmed.

**Realizability sub-check**: `entry_context` (NEW, held by value as `G_<no_tag>::ctx_`) — fields are `std::span`, a raw pointer (`std::pmr::memory_resource*`), `void const*`, a function-pointer typedef (`group_member_fn_t`), `detail::generation_token` (pre-existing, trivially copyable, fully defined), `OffsetTable*` (pointer, not by-value). No forward-declared-and-deferred value-typed dependency exists in this struct — **clean, no D-15-class latent completeness defect**.
