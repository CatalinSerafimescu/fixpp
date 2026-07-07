# Implementation Plan: Nested Group-Parse Correctness (063)

**Branch**: `063-nested-group-parse-correctness` | **Date**: 2026-07-07 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification + [research.md](./research.md) (Phase-0, incl. census + codegen-union discovery)

## Summary

Fix two pre-existing, `main`-resident defects that break multi-instance / tag-reused nested repeating-group reads against the **real** dictionary, so grouped typed reads (and feature 061) are correct via `Dictionary::as_table_view()`:

- **Defect A — loader group-membership collision.** The XML loader registers repeating-group membership globally, first-XML-occurrence-wins, keyed by the `NumInGroup` tag (`xml_loader.cpp:486`). A reused tag (e.g. FIX44 295) resolves to the wrong variant → `group_member_fn(295, delim=299)` fails `group()`'s acceptance check (`offset_table.cpp:433`) → group reads empty. **Census (research §Pivotal): systemic — 12 (FIX44) … 22 (FIX50SP2) colliding tags/dict; parent-group key insufficient.**
- **Defect B — non-nesting-aware extent.** `OffsetTable::group()`'s flat `seen_in_instance` boundary heuristic (`offset_table.cpp:450-459`) truncates an outer instance at the 2nd entry of a nested group → multi-entry nested reads return too few entries.

**Chosen approach (recommended to Gate A):** **Defect A = Option B (union-per-no_tag membership in the loader), mirroring the proven codegen union model** (`emit_messages.cpp:126-139` emits one superset `G_<no_tag>`); **Defect B = an allocation-free nesting-aware boundary walk.** 062's nested-read mechanism and codegen accessors are untouched (FR-005; superset flyweight composes with the runtime fix). Both are Wire-format + dictionary-layer changes → all four mandatory controls + Gate A/B.

## Technical Context

**Language/Version**: C++23. **Primary surface**: `src/dictionary/xml_loader.cpp` + `include/fixpp/dict/table_view.hpp` + `src/dictionary/dictionary.cpp` (Defect A); `src/wire/offset_table.cpp` (`OffsetTable::group()`) + `include/fixpp/wire/offset_table.hpp` (Defect B). **Read-only consumers to keep consistent**: `include/fixpp/wire/parser.hpp:484-494` (bound `group_member_fn`), `include/fixpp/wire/validator.hpp:219`. **Untouched by contract (FR-005)**: 062's `build_nested_subview`/`nested_group_slices`/`entry_context` + `tools/codegen/fixpp-codegen/emit_messages.cpp` + generated accessors. **Testing**: GoogleTest under `tests/dictionary/`, `tests/wire/`, `tests/codegen/`; sanitizer matrix (ASan/UBSan/TSan) + alloc gate (`tests/alloc_guard/`, `tests/codegen/group_entry_alloc_gate_test.cpp`) + fuzz (`tests/fuzz/fuzz_wire_nested_slice.cpp`). **Census tool**: a loader-faithful (component-expanding) reused-tag census across all nine runtime XMLs. **Target Platform**: Linux Tier-1 + Windows Tier-2 + libc++ Tier-3. **Project Type**: single library. **Scale/Scope**: focused wire + dictionary-loader fix; NO new message families, NO builders, NO codegen change, NO C-ABI change.

**Performance Goals / Constraints**: `OffsetTable::group()` is on the read/metadata path — Defect B's nesting-aware walk MUST be **allocation-free** (stack-only, depth-bounded index recursion over `entries_`; guarded by the alloc gates). Defect A's loader change is load-time only (no hot-path cost).

## Constitution Check

*GATE: must pass before Phase 0 (passed — research complete) and re-checked post-design.*

- **Appendix A mandatory triggers**: **Wire format/parser** (`OffsetTable::group()` boundary semantics) AND **Codegen layout / dictionary loader** (loader group registration) → run ALL four mandatory controls: `/clarify` (done — 1 session, 3 Q on scope/surface/alloc), `/analyze` (pending — after `/tasks`), Codex **Gate A** (pending — this plan is the input; the **Option A vs B fork is the primary Gate-A question**), user **`/plan` sign-off** (pending). Full **Gate B** before merge.
- **Error semantics**: no new `fixpp_error_t` value — the fix changes *which* membership/extent is computed, reusing existing `required_field_missing` / group errors (FR-005/SC-005).
- **C-ABI** (`[const §X.1]`, GA-frozen 1.5.0): **byte-identical** — `fixpp_msg_get_group`/`fixpp_group_*` sit atop `group_slices` and their OUTPUTS legitimately correct, but no exported symbol / header changes (`tests/abi/golden/fixpp_capi_symbols.txt` + `tools/capi_freeze.sha256` unchanged; SC-005).
- **Zero-alloc discipline** (`[const §VIII.5]`/`[const §XV.1]`): Defect B's boundary walk adds **zero heap allocation** (FR-003; alloc-gate-proven). 062's bounded nested sub-view arena is unchanged.
- **TDD / discriminating witnesses** (`[const Art IX]`): both defects land red-green — each guard **mutation-proven RED** on the pre-fix code (SC-003), incl. the NET-NEW real-`as_table_view()` MassQuote multi-entry witness (SC-001) and the un-skipped `nested_group_read_test.cpp:353`.
- **Fuzzing** (`[const Art IX §7]`, parser-touching): extend `fuzz_wire_nested_slice.cpp` to cover the nesting-aware walk.
- **Sanitizers as real defects**: extent/slice lifetime validated under the full matrix, not asserted.

**Post-design re-check**: performed after Phase 1 (below). Under Option B no signature/ABI change → no Complexity-Tracking waiver. Under Option A, the `group_member_fn_t` context-param widening would be surfaced at `/plan` sign-off as an intended public-C++ (non-ABI) surface change (clarify-sanctioned) — recorded in Complexity Tracking if selected.

## Project Structure

### Source Code (repository root)

```text
# Defect A (Option B — recommended): union-per-no_tag membership
src/dictionary/xml_loader.cpp          # :486 first-seen-wins guard -> UNION-accumulate members per (group_no_tag,no_tag), mirroring emit_messages.cpp:390-406 keying; group_first stays first-seen (delim) but membership superset
include/fixpp/dict/table_view.hpp      # group_members_ becomes a superset per no_tag (no key change under Option B); accessor semantics documented
src/dictionary/dictionary.cpp          # as_table_view() (:295-357) + handle group accessors — carry the union through unchanged signatures
#   (Option A fallback ONLY, if over-extension analysis fails): + group_member_fn_t gains a context arg; table_view re-keys (context,no_tag); parser.hpp:484-494 + validator.hpp:219 + entry_context updated

# Defect B: allocation-free nesting-aware extent
src/wire/offset_table.cpp              # group() (:402-482): replace flat seen_in_instance (:450-459) with depth-bounded, stack-only nested-count-aware boundary walk (on a member tag that is itself a NumInGroup count in this dict, consume its extent); NO heap
include/fixpp/wire/offset_table.hpp    # helper decl if the recursion needs a private method; entry_context UNCHANGED (FR-005)

# UNTOUCHED (FR-005): build_nested_subview / nested_group_slices / group_view.hpp entry_context / emit_messages.cpp / generated accessors

tools/ or tests/  census               # loader-faithful reused-tag census (component-expanding) over all 9 XMLs -> the FR-002 collision set + Option-B over-extension check (trailing wire-neighbour ∈ union?) per OFFICIAL message
tests/dictionary/                      # Defect-A guard: real-dict membership for censused colliding tags resolves correctly (295 -> QuotEntryGrp in MassQuote); mutation-proven RED on first-seen-wins
tests/codegen/nested_group_read_test.cpp   # un-skip :353 (hand-built-dict Defect-B); ADD net-new real-as_table_view() MassQuote 2-QuoteEntries witness (SC-001, exercises A+B)
tests/wire/                            # Defect-B guard: multi-entry nested outer-extent; single-entry + count-of-zero + flat + benign-reuse regressions
tests/alloc_guard/ + tests/codegen/group_entry_alloc_gate_test.cpp   # boundary-walk zero-alloc (its header already names Defect B deferred-to-063)
tests/capi/                            # fixpp_msg_get_group nested output now correct; abi_symbol_golden + capi_freeze unchanged
```

**Structure Decision**: single-library layout; changes confined to the dictionary-loader layer (Defect A) and the wire offset-table layer (Defect B), plus tests + a census tool. No new modules.

## Complexity Tracking

> Filled only if Gate A selects **Option A** (per-context membership). Under the recommended **Option B** there is no signature/ABI violation and this table stays empty.

| Violation (Option A only) | Why Needed | Simpler Alternative Rejected Because |
|---|---|---|
| `group_member_fn_t` gains a context param (public C++ surface) | Per-message membership can't be expressed on a `(no_tag)`-only predicate (census: parent-key insufficient) | Option B (union) rejected only if the over-extension analysis finds a real in-scope misslice |

## Phase notes
- **Phase 0** (done): research.md — source map, census, codegen-union oracle, Defect-A fork (A/B), Defect-B alloc-free walk, witness plan.
- **Phase 1** (this command): data-model.md (membership/extent entities), contracts/group-membership-and-extent.md (the observable contracts + the A/B decision surface), quickstart.md (how to run the census + the SC-001 witness). Agent-context marker refresh.
- **Phase 2** (`/tasks`, later): tasks.md — census FIRST (it decides A vs B), then Defect-A fix + guard, Defect-B fix + guard, SC-001 real-dict witness, un-skip, regressions, alloc gate, C-ABI freeze verify, doc/L-062 close-out.
