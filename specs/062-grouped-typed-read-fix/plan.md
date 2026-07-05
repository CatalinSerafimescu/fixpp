# Implementation Plan: Grouped Typed-Read Path Fix (062)

**Branch**: `062-grouped-typed-read-fix` | **Date**: 2026-07-05 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification + [research.md](./research.md) (Phase-0)

## Summary

Make typed field access on repeating-group **entries** compile and return correct per-instance values — scalars AND nested groups — closing the `group_view`/codegen contract mismatch that currently makes `group[i].field()` ill-formed. Delivers QuickFIX C++/J parity for nested groups (MassQuote quote-entry prices). Chosen mechanism (research §Mechanism): a **hybrid** — the entry span-scans its own slice for scalar fields (no per-entry sub-index, protecting the one-level MarketData hot path per FR-004a), and builds a **dict-aware sub-view lazily, cached once in the root OffsetTable's single flat arena cache, only on nested descent** (satisfying FR-002 nested + FR-004 zero one-level alloc). Prerequisite for feature 061.

## Technical Context

**Language/Version**: C++23. **Primary surface**: `include/fixpp/wire/{group_view,parser,offset_table,view,framer}.hpp` + `src/wire/offset_table.cpp` (wire); `tools/codegen/fixpp-codegen/emit_messages.cpp` + regenerated `_codegen` headers + golden `specs/003-dictionary-codegen/contracts/golden/v44_Messages.golden.hpp` (codegen). **Testing**: GoogleTest under `tests/wire/` + `tests/codegen/`; sanitizer matrix (ASan/UBSan/TSan) + alloc gate. **Target Platform**: Linux Tier-1 + Windows Tier-2. **Project Type**: single library. **Scale/Scope**: focused wire+codegen fix; no new message families, no builders, no C-ABI change.

## Constitution Check

*GATE: must pass before Phase 0 (passed — research complete) and re-checked post-design.*

- **Appendix A mandatory triggers**: **Wire format/parser** (group entry read path, `OffsetTable`/`group_view` changes, `frame_view`-over-slice seam) AND **Codegen layout** (entry-class emission + forced regen) → run ALL four mandatory controls: `/clarify` (done — 1 session, 2 Q), `/analyze` (done — cross-artifact pass 2026-07-05; 0 CRITICAL, findings remediated into tasks.md/plan.md), Codex **Gate A** (done — CONVERGED round 3, `gate-a-done`, converged commit `628e35fd`, per `.specify/decisions/062-grouped-typed-read-fix-gatea.md`), user **`/plan` sign-off** (done 2026-07-05). Full **Gate B** before merge.
- **Error semantics**: no new `fixpp_error_t` value; entry accessors reuse existing field-not-found/decode errors (FR-007).
- **C-ABI**: unchanged (FR-007).
- **Zero-alloc discipline** (`[const §VIII.5]` "Arena/PMR is the default", `[const §XV.1]` "arena/PMR for the rare materialise cases"): honored on the one-level scalar path (FR-004/FR-004a — zero heap alloc); nested descent materializes a bounded, cached-once **arena** sub-view (FR-004b, research §Cost reconciliation) — constitutionally permitted read-path arena/PMR materialisation (no global `new`/`delete` between parse and `fromApp`), surfaced at `/plan` sign-off as the intended nested cost.
- **Sanitizers as real defects**: lifetime correctness (entry borrows parent; nested sub-view arena-owned) validated under the full sanitizer matrix, not asserted.

**Post-design re-check**: no new violations; the eager-materialization hot-path risk that would have breached zero-alloc discipline is designed out by the hybrid (one-level never builds a sub-index). No Complexity-Tracking waiver required.

## Project Structure

### Source Code (repository root)

```text
include/fixpp/wire/group_view.hpp     # carry entry_context {span,mr,dict,group_member_fn,gen,parent_cache_owner,outer_occurrence_id}; operator[]/iterator hand entry ctx + occurrence identity (same i -> seam-#8)
include/fixpp/wire/parser.hpp         # MessageView::group<>() threads ctx (incl. generation token) into group_view; expose dict/group_member_fn/token; NEW span-scan helper get(span,tag,token)->expected_t<field_view> (token-bearing, one-level scalar reads, N1)
include/fixpp/wire/offset_table.hpp   # single flat nested sub-view cache keyed (unique_slice_identity=outer slice data ptr, nested_no_tag) in the ROOT OffsetTable per-message arena; parent_cache_owner handle (points at root, threaded unchanged at every depth); API to build/fetch a DICT-AWARE sub-view over a slice
src/wire/offset_table.cpp             # dict-aware sub-view materialization over a slice-scoped len+1 (whole-frame build() guard + group_slice.len UNCHANGED — RC1); whole-frame-parse regression guard
include/fixpp/wire/framer.hpp / view.hpp   # frame_view-over-slice friend-seam (mirror src/capi/message_write.cpp:63-74 precedent); group_slice/context struct shape if needed
tools/codegen/fixpp-codegen/emit_messages.cpp  # entry class: store entry_context ctx_; scalar accessors -> span-scan helper; field_value -> span-scan (N3 limitation); nested accessor -> lazy dict-aware sub-view keyed (unique_slice_identity=outer slice data ptr, nested_no_tag) in the root flat cache
# regenerated: build/<preset>/_codegen/include/fixpp/**/Messages.hpp ; golden: specs/003-dictionary-codegen/contracts/golden/v44_Messages.golden.hpp
tests/wire/  tests/codegen/          # discriminating witnesses over GENERATED flyweights: one-level scalar+decimal, nested (MassQuote NoQuoteSets->NoQuoteEntries), empty group, operator[]==iter(); + a regression guard (instantiate operator[] on a generated entry)
```

**Structure Decision**: wire-layer feature with a confined codegen entry-class change; no new modules. Design artifacts: [data-model.md](./data-model.md), [contracts/](./contracts/), [quickstart.md](./quickstart.md).

## Implementation Sequencing

1. **Enabling seams (wire) FIRST**: `frame_view`-over-slice friend-seam + a **span-scan → token-bearing `field_view`** helper + a **dict-aware sub-view-over-slice builder** reading a **slice-scoped `len+1`** (whole-frame `build()` guard + `group_slice.len` UNCHANGED — RC1). Verify: whole-frame-parse regression test proves no top-level regression AND a nested-slice build over a counted-last-field entry succeeds; an internal test builds a working typed reader over a hand-cut slice.
2. **group_view context threading**: `group_view` carries the full `entry_context` (incl. generation token + `parent_cache_owner`); `MessageView::group<>()` passes it; `operator[]`/`iter()` hand the entry its ctx + `outer_occurrence_id` (same `i` from both — seam-#8). Verify: `repeating_group_equivalence_test` extended to a GENERATED flyweight compiles + passes.
3. **Codegen entry emission**: entry stores `entry_context ctx_`; scalar accessors span-scan; nested accessor lazy dict-aware sub-view keyed `(outer_occurrence_id, nested_no_tag)`. Rebuild tool, clear `_codegen` markers, regen, update golden. Verify: golden diff is intentional; `typed_accessor_test` reads real entry fields.
4. **Discriminating witnesses + regression guard** (FR-006, mapped in §Acceptance → Witness Map): one-level (scalar+decimal), **absent-field typed error** (US1 AC2 / FR-001), **absent-vs-present-but-empty** edge, nested (MassQuote), empty group, `operator[]`==`iter()` equivalence over a GENERATED flyweight; a guard that re-breaks on revert.
5. **Sanitizer matrix + alloc gate** (FR-004/FR-004a/FR-004b): prove one-level scalar read = zero sub-index/zero alloc; nested first descent = one bounded arena build per stable occurrence; repeat descent = zero; ASan/UBSan/TSan clean.

## Acceptance → Witness Map

Every AC/FR/SC maps to a named witness (Codex P2#4). Test **filenames are provisional** — they are finalised at `/tasks` (Gate A precedes `/tasks`); the AC→witness *mapping* is the binding artifact here (see `## Gate A → Round 1 — disagreements`).

| AC / FR / SC | Witness file | Test name |
|--------------|--------------|-----------|
| US1 AC1 / FR-001 / FR-005 / SC-001 (one-level scalar+decimal, generated flyweight) | `tests/codegen/group_entry_read_test.cpp` | `OneLevelScalarAndDecimalReadExactValues` |
| US1 AC2 / FR-001 (absent field → typed not-found error) | `tests/codegen/group_entry_read_test.cpp` | `AbsentEntryFieldReturnsTypedError` |
| Edge (absent vs present-but-empty) | `tests/codegen/group_entry_read_test.cpp` | `AbsentVsPresentButEmptyField` |
| US2 AC1 / FR-002 / SC-001 (nested MassQuote NoQuoteSets→NoQuoteEntries) | `tests/codegen/nested_group_read_test.cpp` | `NestedQuoteEntriesPerInstancePrices` |
| FR-002 / INV-G3 (depth-3+ no cross-occurrence collision — slice-identity key) | `tests/codegen/nested_group_read_test.cpp` | `Depth3NonFirstOuterOccurrenceNoCollision` |
| Edge (empty group) | `tests/codegen/group_entry_read_test.cpp` | `EmptyGroupSizeZeroNoDeref` |
| Edge (single-entry / last-entry delimiter extent, spec.md:71) | `tests/codegen/group_entry_read_test.cpp` | `LastEntryDelimiterExtentExact` |
| Edge (group cap / oversized count — no regression, spec.md:73) | `tests/wire/group_slice_trailing_soh_test.cpp` | `OversizedCountPerInstanceCapPreserved` |
| US1 AC3 / FR-003 (operator[] == iter() over a GENERATED flyweight) | `tests/wire/repeating_group_equivalence_test.cpp` (extended) | `GeneratedFlyweightOperatorEqualsIter` |
| FR-006 / SC-003 (generated-flyweight compile/regression guard) | `tests/codegen/typed_accessor_test.cpp` (extended) | `GeneratedEntryOperatorSubscriptInstantiates` |
| RC1 / FR-007 (trailing counted-data slice build + whole-frame parse regression) | `tests/wire/group_slice_trailing_soh_test.cpp` | `NestedSliceBuildCountedLastField` + `WholeFrameParseUnchanged` |
| FR-007 / SC-004 (NO C-ABI symbol change + NO error-enum change + top-level non-group reads unchanged + Tier-1 green) | `tests/capi/abi_symbol_golden_test.cpp` (symbol-golden / `nm`/abidiff) + `tests/wire/toplevel_read_regression_test.cpp` | `CabiSymbolSetUnchanged` + `ErrorEnumUnchanged` + `TopLevelNonGroupReadUnchanged` (+ Tier-1 suite green post-regen) |
| US3 AC2 / INV-G1 (parent destroyed → documented-undefined entry lifetime) | doc-witness: `data-model.md` §Invariants INV-G1 + `contracts/group-entry-read.md` §Lifetime & stability | (documentation obligation, not a runtime UAF test) |
| US3 AC1 / SC-002 (sanitizer lifetime) | above witnesses under the ASan/UBSan/TSan matrix | (matrix run, not a distinct test) |
| FR-004 / FR-004a / FR-004b / SC-002 (alloc gate: one-level zero-alloc + nested cached-once, repeat zero) | `tests/codegen/group_entry_alloc_gate_test.cpp` | `OneLevelScalarZeroAlloc` + `NestedFirstDescentBoundedRepeatZero` |
| INV-G7 (dict-aware nested slicer MANDATORY — never the dict-free fallback; added /analyze C1) | `tests/codegen/nested_group_read_test.cpp` | `NonLastNestedGroupTrailingFieldNotSwallowed` (outer field AFTER the nested group — fails under the dict-free fallback) |
| INV-G6 (entry read never under a default `{}` generation token; added /analyze C2) | `tests/codegen/group_entry_alloc_gate_test.cpp` (or sibling `group_entry_generation_trap_test.cpp`) | `GenerationTokenTrapOnStaleEntryRead` (debug-mode; sanitizer matrix cannot catch — `pool_id==0` is untracked) |

## Complexity Tracking

| Decision | Why | Simpler alternative rejected because |
|----------|-----|--------------------------------------|
| Hybrid (span-scan scalars + lazy nested sub-view) | Satisfies FR-002 (nested) and FR-004a (one-level cheap) simultaneously | Pure (b) builds sub-indices for one-level groups -> hot-path regression (FR-004a); pure (c) has no nested slicer -> fails FR-002 |
| `frame_view`-over-slice friend-seam | No public span->frame_view path; sub-view build needs a frame_view | Making `frame_view`'s ctor public widens surface for no benefit; the friend-seam precedent (`message_write.cpp:63-74`) is the established pattern |
| Nested sub-view cache = a single flat cache in the **root** OffsetTable arena (nested sub-tables own no cache), keyed by globally-unique slice identity | Entries are by-value temporaries; cache must outlive them and dedupe repeat descents; a root-owned single flat cache keyed by the outer slice's `data` pointer is collision-free at every nesting depth | Per-entry-instance cache impossible (temporary); rebuild-per-call violates FR-004 for nested; a per-sub-table cache keyed by `(no_tag + ordinal i)` collides across repeated outer occurrences (silent wrong values) |
| Slice-scoped nested build over `len+1` (whole-frame `build()` guard UNTOUCHED — RC1) | The terminal SOH is provably present in the parent buffer at `data+len` (slice interior to a checksum-terminated frame), so `len+1` makes the counted last field pass the intentional guard (`end < n && buf[end]==SOH`) with no global relaxation | Relaxing the whole-frame guard weakens top-level parser correctness (it rejects `end==n` to protect the checksum tail — PR #166 F1/B-004-6); widening the shared `group_slice.len` corrupts the C-ABI group cursor + `TestLeg`; the one-level scalar path needs neither (`field_iterator` tolerates the absent SOH) |

## Gate A

- Round 1 applied 2026-07-05: Codex P1=2 P2=3 P3=1; Opus post-judging P1=2 P2=5 P3=2; rewrite addresses root causes RC1 (slice-scoped SOH, drop whole-frame guard edit), RC2 (entry_context: parent-cache handle + occurrence identity + generation token + recursive ctx + span-scan field_view primitive), RC3 (split FR-004/SC-002 alloc contract), RC4 (Normative References + named witnesses + FR-003 mis-cite). Reviews: research/reviews/codex_062-grouped-typed-read-fix_gate_a_review.md, research/reviews/opus_062-grouped-typed-read-fix_gate_a_adversarial_review.md.
- Round 2 applied 2026-07-05: Codex P1=0 P2=2 P3=0; Opus post-judging P1=0 P2=2 P3=2; rewrite addresses the depth-3/4 cache-keying collision (globally-unique slice-identity key, ordinal alternative deleted), the witness-map RC4 gaps (US3 AC2/INV-G1, FR-007/SC-004, alloc row names FR-004/004a/004b), the last-entry/group-cap edge witnesses, and the Normative-References 057-precedent clause. Reviews: research/reviews/codex_062-grouped-typed-read-fix_gate_a_2_review.md, research/reviews/opus_062-grouped-typed-read-fix_gate_a_2_adversarial_review.md.
- Round 3 (2026-07-05): Codex P1=0 P2=0 P3=0 (zero findings); Opus post-judging P1=0 P2=0 P3=2 — **CONVERGED**. Opus independently re-certified the round-2 slice-identity keying fix against source (`offset_table.cpp:193-195,495-505`). `gate-a-done`; user-signed-off 2026-07-05; converged commit `628e35fd` (submodule) / `4172ad3` (parent). Reviews: research/reviews/codex_062-grouped-typed-read-fix_gate_a_3_review.md, research/reviews/opus_062-grouped-typed-read-fix_gate_a_3_adversarial_review.md. Evidence record: `.specify/decisions/062-grouped-typed-read-fix-gatea.md`.

### Round 1 — disagreements

- **Codex P2#4 (named test files)** — applied *with a scope caveat*, not verbatim. The task directive requests named test files + test names; the Opus adversarial review judged the concrete-filename sliver a deferred **P3** because Gate A runs *before* `/tasks`. Resolution: the binding artifact is the **AC→witness mapping** (§Acceptance → Witness Map, now complete for every AC/FR/SC); the concrete `.cpp` **filenames + test names are provisional** and finalised at `/tasks`. No substantive coverage is deferred — the previously-missing absent-field-error and absent-vs-present-but-empty witnesses ARE added to FR-006 and the map. No other finding is skipped. **(Round 2: this disagreement stands — the Opus round-2 review confirms the concrete-filenames-provisional posture is legitimate.)**

### Round 2 — disagreements

- **Codex round-2 P2#2 (Normative References format — "add `[2b §…]`/`[2c §…]` FIX-doc catalogue rows")** — NOT applied as Codex counter-proposed. The Opus round-2 adversarial review downgraded this to **P3** and refuted the counter-proposal: adding `[2b §…]`/`[2c §…]` FIX-message catalogue titles to 062 would **misassign ownership** — the A-*/M-*/P-* catalogue rows are closed by feature **061**, not by this enabling/mechanism feature. The merged-057 precedent (a mechanism feature) satisfies `[const §VI.5]` with architecture/constitution/`[impl]` anchors and no new FIX-spec normative section; 062 mirrors it. Applied instead: the explicit 057-precedent clause ("062 introduces NO new FIX-spec normative section") at the head of the section (P3 polish), per the Opus downgrade. The FIX-doc catalogue rows remain under "It unblocks:", not as 062's own normative refs.

## Simplify (post-implement `/simplify` pass — 2026-07-05)

Four-angle cleanup pass (Reuse / Simplification / Efficiency / Altitude). **Applied** (no codegen regen; low-risk, re-verified across debug witnesses + the ASan/UBSan/TSan matrix + Tier-1):
- **Altitude** — `entry_context::parent_cache_owner` retyped `OffsetTable* → const OffsetTable*`; dropped the `const_cast` at the single mint site (`parser.hpp`). The only method reached through it is the `const nested_group_slices()`; the cast guarded a hypothetical mutating overload that does not exist — a real const-correctness hole closed.
- **Efficiency / Simplification** — `OffsetTable::nested_group_slices` collapsed from two O(n) `nested_cache_` scans into a single pass. Kept **FIRST-wins** on the same-slice fallback (a failed `build_nested_subview` pushes a `table==nullptr` row, so the first same-slice row must win to keep the build count identical). Behavior + build count unchanged.
- **Reuse (comment accuracy)** — corrected the `frame_view_slice_access` header comment: the second seam exists because `frame_view_access`'s body is defined per-TU in `message_write.cpp` (not visible/callable here), NOT because a shared header seam would cause an "ODR clash" (token-identical seam defs across TUs are ODR-legal).

**Deferred** (each needs either a codegen regen, a spec/data-model reconciliation, or is spec-observable — not surgical `/simplify` work; recorded here so they are tracked, not lost):
- **Altitude — nested-descent orchestration in generated code (half-restructure, per [[feedback_half_restructure_symmetric_api]]).** The scalar path was moved to a wire free-helper (`wire::get`), but the nested accessor still bakes the full descent recipe (null-check + 6-arg `nested_group_slices` call + `ctx_` re-thread) into the emitter string. The symmetric fix — a wire-layer `entry_context::nested_group<GroupT>(no_tag) const` member *function* template (keeps `entry_context` trivially copyable), collapsing the generated accessor to `return ctx_.template nested_group<G>(NO_TAG);` — requires an `emit_messages.cpp` change → forced regen + golden update + full re-verify. Deferred to a focused follow-up / Gate B (whose remit *is* design-altitude); the asymmetry is recorded here so it is a tracked deferral, not an accident.
- **Reuse — `nested_cache_` per-`no_tag` rows shadow `OffsetTable::group_slices(no_tag)`'s own per-`no_tag` cache.** Keying the flat cache by `slice_data` only (dropping `nested_no_tag` from the row) and delegating per-`no_tag` resolution to the sub-table's existing cache is cleaner, but it is **spec-observable**: it contradicts the literal INV-G3 / data-model.md key `(unique_slice_identity, nested_no_tag)` and changes the FR-004b alloc-gate profile on a 2nd distinct `no_tag`/same slice (`NestedFirstDescentBoundedRepeatZero` pins specific build counts). Needs a spec-aware pass + data-model reconciliation → 063 or a dedicated cleanup.
- **Simplification — `entry_context` vestigial/derivable fields.** `mr` is write-only dead state (set in `MessageView::group<>()`, never read — nested build uses `resource()`, decimal decode takes an explicit `mr` param; derivable as `parent_cache_owner->resource()`); `outer_occurrence_id` is always `== span.data()`. Both are enumerated in `data-model.md` §"entry read context" (which over-documents `mr` as used for "nested build + decimal decode"). Removing them needs a data-model reconciliation, and `outer_occurrence_id` removal needs an emitter change → regen. Bundle with the nested→wire-helper follow-up so the data-model is updated once.
- **Low-value** — a 3-call-site codegen DRY helper (`emit_entry_get`) and the dead back-compat `group_view(instances, gen)` ctor (public-surface change) — not worth the regen / API churn now.
