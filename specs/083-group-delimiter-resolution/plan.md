# Implementation Plan: Group Delimiter Resolution

**Branch**: `083-group-delimiter-resolution` | **Date**: 2026-07-30 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/083-group-delimiter-resolution/spec.md`

## Summary

Resolve a repeating group's delimiter **per context** `(msg_type, ancestor-path, no_tag)` instead of dictionary-globally, in both loaders; make the receiver descend when an instance-opening delimiter is itself a nested group's count tag; align the C-ABI construction check and the typed-read instance splitter onto the same rule; and pin the result across all ten dictionaries with a carve-out-free document-order pin.

The central technical finding (research.md **D-1**) is that **no new document-order scan is required**. `LoaderState::expand_field_list` already emits `FieldRef`s in document order, expands components *inline* at the enclosing group's level, and pushes a nested group's own count-tag `FieldRef` at the **outer** level before descending. Therefore *the first `FieldRef` emitted at a group's level is, by construction, that group's document-order first member* — FR-003's component recursion and FR-004's nested-group delimiter both fall out for free. The fix is to capture that first emission per context, not to write a second traversal.

This matters beyond economy: the defect being fixed exists *because* the current `first_field_tag` scan (`xml_loader.cpp:610-641`) is a **separate** traversal that drifted out of step with the member expansion. Adding a third traversal would reproduce the failure mode. D-1 makes delimiter and member set derive from one walk, so they cannot disagree again.

## Technical Context

**Language/Version**: C++23 (Clang 22 primary, GCC Release sanity, MSVC Tier 2)

**Primary Dependencies**: pugixml (dictionary parsing, `src/` only — not exposed in public headers); GoogleTest; Google Benchmark

**Storage**: N/A — in-memory dictionary tables, PMR-allocated on a caller-supplied `memory_resource`

**Testing**: GoogleTest via `ctest -L <label>`; grouped isolation-safe buckets per Article VII §8

**Target Platform**: Linux (Tier 1: Clang Debug+Release, GCC Release), Windows/MSVC (Tier 2)

**Project Type**: C++ library (FIX engine) with a GA-frozen C ABI and SWIG Python bindings

**Performance Goals**: no regression beyond ±5% vs `bench/baselines/` (Article VIII §2). Delimiter resolution is on the inbound validate path — `validator.hpp:276` and `:376` call it per offset-table entry.

**Constraints**: zero `new`/`delete` between parse and `fromApp` (Article VIII §5); `FieldRef` is ABI-pinned at 16 bytes by `static_assert` and **must not grow**; C ABI GA-frozen at 1.5.0 — no exported signature may change; loader runs at init only, so construction-time exceptions are permitted there.

**Scale/Scope**: 10 dictionaries; FIX50SP2 alone has 25,897 group contexts and Orchestra 26,806 — the pin must be O(contexts) and cheap enough to run in Tier 1.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.*

**Appendix A mandatory triggers** — this feature hits **four** trigger rows: *Wire format / parser* (validator changes, offset-table semantics), *Codegen layout* (dictionary loader), *ABI surface change* (behaviour reachable through the frozen C ABI), and arguably *Error semantics* (FR-006's new fail-closed load rejection). All four mandatory controls therefore apply:

| Control | Status |
|---|---|
| `/clarify` | ✅ Done — 4 questions, session 2026-07-30, recorded in spec.md |
| `/analyze` | ⏳ Pipeline step 6, after `/speckit-tasks` |
| Codex Gate A | ⏳ Pipeline step 4, **before** `/speckit-tasks` |
| User `/plan` sign-off | ✅ **Signed off by the user 2026-07-30**, after review of D-1 (first-emission capture), D-7/C-7.1 (fail-closed precondition) and D-6 (splitter characterise-first) |

| Article | Requirement | How this plan satisfies it |
|---|---|---|
| VII §3, §4 | TDD mandatory; no code without a test | Every phase below is RED-first. Phase 1 is *entirely* RED — including the wire repro that closes the spec's one open inference (FR-001 premise) — and is observed failing before any fix. |
| VII §7 | Fuzzing for parser-touching modules | `consume_group` descent changes parser-adjacent control flow → existing wire fuzz harness must cover a nested-delimiter corpus seed. |
| VII §8 | Group isolation-safe tests; select by label | New dictionary tests join the existing `dictionary_pure_tests` bucket; `ctest -L dictionary` and `-L wire`. The census/pin test is an **exact-set completeness gate** → stays standalone per §8. |
| VIII §2, §3 | ±5% budget; **no perf change without a bench in the same PR** | FR-022. Bench added for the inbound validate path in this PR; baseline updated in-PR with rationale if the change is intentional. |
| IX §1 | ≥95% line / ≥85% branch on touched modules; no silent uncovered error path | Touched: `src/dictionary/`, `include/fixpp/dict/`, `include/fixpp/wire/`, `src/wire/`, `src/capi/`. FR-006/006a create new error paths — both dispositions get explicit tests (FR-006b), so neither lands uncovered. |
| IX §2 | ASan/UBSan/TSan Tier 1 | Recursion depth change in `consume_group` → ASan stack coverage matters; depth stays bounded by the existing K=16 cap (FR-009). |
| X §1, §6 | C ABI is a versioned contract; ABI-affecting ⇒ all four controls | FR-018a: **no exported signature changes**. `validate_group_grammar` is a file-static function. Behaviour change is disclosed via FR-019 B&L row + release note. |
| XV | Banned patterns | No new `std::mutex` in awaitable headers; no hot-path allocation added — see D-3 storage decision. |
| XVI §3 | `/clarify` mandatory before `/plan` | ✅ Completed. |
| XVII | Codex Gate A before `/tasks` | Planned as pipeline step 4. |

**Gate result: PASS** — no violations to justify. Complexity Tracking section omitted deliberately (no entries).

**Two risks the gates do not catch, carried forward explicitly:**

1. FR-006's fail-closed default could refuse a dictionary that loads today. D-7 makes "all ten shipped dictionaries still load under the default" a **precondition task that runs before FR-006 is enabled**, not an after-the-fact check.
2. The bare global lookup is an *is-this-a-group* **predicate** at C-ABI construction sites, not only a delimiter source. Deleting the scan that feeds it without repopulating it would make the C ABI reject **all** groups — a total regression through a frozen ABI, in a change whose purpose is to fix rejections. D-10 makes repopulation an explicit decision, not a task-level detail.

## Project Structure

### Documentation (this feature)

```text
specs/083-group-delimiter-resolution/
├── plan.md              # This file
├── research.md          # Phase 0 — D-1..D-9 decisions
├── data-model.md        # Phase 1 — entities + the new side table
├── quickstart.md        # Phase 1 — how to reproduce, verify, and disprove
├── contracts/           # Phase 1 — surface contracts
│   ├── group_ctx_delims.md      # loader → handle → table_view delimiter path
│   ├── consume_group.md         # descend-at-delimiter contract
│   └── loader_tolerant_mode.md  # FR-006/006a load disposition
├── checklists/
│   └── requirements.md  # closed at steps 1–2
└── tasks.md             # Phase 2 — NOT created by /speckit-plan
```

### Source Code (repository root)

```text
include/fixpp/dict/
├── field_ref.hpp              # UNCHANGED — 16-byte ABI static_assert must hold
├── table_view.hpp             # ctx delimiter accessor already exists; set_group_first_ctx loses its member injection (becomes redundant, D-5)
├── xml_loader.hpp             # + tolerant-mode load option (FR-006a)
└── orchestra_loader.hpp       # + same option, symmetric (FR-005)

include/fixpp/wire/
└── validator.hpp              # consume_group: descend-at-delimiter (FR-007..009)

src/dictionary/
├── xml_loader.cpp             # D-1 first-emission capture; D-2 path threading; delete the one-level scan at :610-641 AND repopulate the global from the new table (D-10)
├── orchestra_loader.cpp       # symmetric twin (verified same shape) — first_member_tag replaced by the same capture
├── dictionary_internal.hpp    # + per-context delimiter side table + accessor
└── dictionary.cpp             # as_table_view(): consume the ctx delimiter at :510; correct the false comment at :503-505 (FR-011)

src/wire/
└── offset_table.cpp           # :656-660 splitter — dictionary-sourced delimiter, skip nested extents (FR-021)

src/capi/
└── message_write.cpp          # validate_group_grammar: thread msg_type + path (FR-018)

tests/dictionary/
├── required_scope_oracle.hpp        # ADDITIVE: new group_delims map beside group_members (D-8)
├── delimiter_census_test.cpp        # NEW — the all-ten-dictionary pin (FR-012..015)
└── loader_disposition_test.cpp      # NEW — FR-006/006a both dispositions

tests/wire/
├── consume_group_nested_delim_test.cpp   # NEW — #208 B-2 repro (FR-007)
└── delimiter_divergence_wire_test.cpp    # NEW — the FR-001 premise repro

bench/
└── validate_group_bench.cpp    # inbound validate path (FR-022)
```

**Structure Decision**: existing module layout is unchanged; this feature edits in place across `dict`, `wire`, and `capi` and adds five test files plus one benchmark. No new module, no new library target. The only *new* runtime data structure is the per-context delimiter side table (data-model.md Entity 2), which lives on the existing metadata handle rather than in a new component.

## Implementation Phasing

Ordering is dictated by one hard constraint: **FR-007 (descend-at-delimiter) must land before FR-003 (recursive resolution)**, or the recursive fix converts a wrong-delimiter defect into a false rejection across 232 measured contexts plus the 30 that newly register. Phases 2 and 3 are therefore not interchangeable.

| Phase | Content | Gate to proceed |
|---|---|---|
| **1 — RED** | Wire repro for the FR-001 premise (divergent context rejects, first-seen accepts); #208 B-2 two-instance repro; the all-ten delimiter pin. Baseline the 30 unmeasured ctxMISS contexts (spec's one projected figure). | All observed **failing**, with the measured failure counts recorded. A pin never seen red proves nothing. |
| **2 — Receiver** | FR-007/008/009 descend-at-delimiter. | B-2 repro green; **no** change to the delimiter pin's failure count (this phase must not move it). |
| **3 — Loaders** | D-1 capture + D-2 path threading, both loaders symmetrically; delete the one-level scan; FR-006/006a dispositions. | Delimiter pin green on all ten; FIX50SP2 502→505; all ten still load under the fail-closed default. |
| **4 — Consumers** | `as_table_view()` consumes the ctx delimiter; FR-011 comment fix; C-ABI threading (FR-018); splitter (FR-021, characterise first). | Member-set exactness falls out (FR-015); C-ABI and validator agree; typed-read and validation instance counts agree. |
| **5 — Evidence** | Bench + baseline (FR-022); B&L rows + release note (FR-019); interop-gate observation (FR-020); codegen parity (FR-017); 082 follow-ups. | Article VIII/IX evidence complete for Gate B. |

## Complexity Tracking

No Constitution Check violations — section intentionally empty.
