# Implementation Plan: Fail-loud on nested-read sub-table allocation failure

**Branch**: `073-nested-read-arena-failloud` | **Date**: 2026-07-13 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/073-nested-read-arena-failloud/spec.md`

## Summary

Close limitation **L-065-2** (GitHub #184): a nested repeating-group read whose sub-`OffsetTable` allocation fails under fixed-parse-arena exhaustion currently degrades to an empty result, indistinguishable from a legitimately absent / count-0 group, and the C-ABI reports it as `OK`/nc=0 — a silent truncation. Fix by widening the shared primitive `OffsetTable::nested_group_slices` (both overloads) to a **status-bearing return type** (`nested_slices_result { span slices; bool alloc_failed; }`), setting `alloc_failed` at **every** empty-returning exit (cache-hit early return + final return) as the **OR** of both arena-exhaustion sub-modes — the sub-table build failing (`build_nested_subview → nullptr`) and the built sub-table's own `group_slices()` slice-materialization catching `bad_alloc` (surfaced via an internal `group_slices_status`, D2) — and consuming it symmetrically on **both** read paths: the C-ABI (`message_read.cpp` → `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` before the presence probe) and the typed path (a new `group_view::alloc_failed()` status bit threaded by the codegen emitter). Fail-loud only — no arena-sizing change. See [research.md](./research.md) for the verified seam and the eight design decisions (D1–D8).

## Technical Context

**Language/Version**: C++23 (per Article II / `.specify/constitution.md:51`). The facilities this feature uses (`std::span`, `std::pmr`, a trivially-copyable result struct + `bool`) are ordinary, standard-era C++ — no C++23-only feature is required.

**Primary Dependencies**: standard library only (`<span>`, `<memory_resource>`); the codegen tool (`tools/codegen/fixpp-codegen`).

**Storage**: N/A (in-memory parse arena; `std::pmr` bounded `monotonic_buffer_resource` over `null_memory_resource`).

**Testing**: GoogleTest (`ctest`), sanitizer matrix (ASan/UBSan/TSan), coverage (lcov), fuzz (existing `fuzz_wire_nested_slice`), per Article IX. Witnesses mutation-proven RED-first.

**Target Platform**: Linux (clang + gcc) + MSVC, per Article II / CI tiers.

**Project Type**: C++ library (wire codec + C-ABI + codegen), single-project layout.

**Performance Goals**: no regression on the success/absence read path (SC-004); the new flag is a by-value `bool` on an already-returned value — zero added allocation.

**Constraints**: C-ABI GA-frozen at 1.5.0 (Article X) — no symbol/struct/signature/macro change (reuse existing `WIRE_LIMIT_EXCEEDED`); both primitive overloads and `build_nested_subview` are `noexcept` (status must be a value, never a throw); wire-read layer is zero-alloc by-value (result struct stays trivially copyable).

**Scale/Scope**: ~5 production files + ~20 mechanical test-call-site adaptations + golden regen + 2 new witnesses. Extreme-edge reachability (arena exhaustion), not shipped-traffic-reachable.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Article | Gate | Disposition |
|---------|------|-------------|
| II — Language/Compilers | C++23, clang+gcc+MSVC | PASS — no new language features; trivially-copyable struct + bool (ordinary standard-era facilities). |
| VI — Spec Coverage | 100% FIX rule | N/A — no dictionary/message-coverage change; read-behavior hardening only. |
| VII — Testing (TDD) | RED-first, mutation-proven | PASS — D6 witnesses authored failing-first; repeated-read discriminator for the cache exit. |
| IX — Coverage/Sanitizers/Static | full matrix on diff | PASS — `bad_alloc`/arena path is ASan/UBSan-relevant; `/speckit-verify` + Tier-1 matrix planned. |
| **X — ABI (C-ABI frozen 1.5.0)** | no ABI break | PASS-with-note — returns an **existing** error code in a formerly-silent edge; no symbol/struct/signature/macro change. Behavioral bug-fix (silent-loss→fail-loud) on a normal-traffic-unreachable path. **Flagged for explicit Gate A confirmation** (see Complexity Tracking). |
| XI — Concurrency | co_await/noexcept discipline | PASS — no concurrency surface; preserves `noexcept` on the primitive (value-return status). |
| XV — Banned Patterns | no silent success / fail-closed | PASS — this feature *removes* a silent-success (the whole point); return struct makes status un-ignorable (D1). |
| XVI — Spec Kit Workflow | artifacts consistent | PASS — shape oracle `contracts/group_view.hpp` updated for the new accessor. |
| XVII — Codex Review Gates | own Gate A + Gate B | PASS — this feature carries its own Gate A (reopens 065 Decision 6); Gate B before merge. |

**Initial gate: PASS** (one PASS-with-note on Article X, tracked below). **Post-design re-check (after artifacts): PASS** — design introduces no new violations; the Article X note is unchanged and is a Gate-A confirmation item, not a blocker.

## Project Structure

### Documentation (this feature)

```text
specs/073-nested-read-arena-failloud/
├── plan.md              # This file
├── research.md          # Phase 0 — seam + decisions D1–D8
├── data-model.md        # Phase 1 — nested_slices_result + group_view status
├── quickstart.md        # Phase 1 — witness run guide
├── contracts/
│   └── nested_slices_result.md   # widened primitive signature + group_view accessor contract
├── checklists/
│   └── requirements.md  # spec-quality checklist (16/16)
└── tasks.md             # /speckit-tasks output (NOT created here)
```

### Source Code (repository root = library submodule)

```text
include/fixpp/wire/
├── offset_table.hpp     # + nested_slices_result; both nested_group_slices decls return it
└── group_view.hpp       # + alloc_failed_ member, alloc_failed() accessor, defaulted ctor param

src/wire/
└── offset_table.cpp     # return the struct; set alloc_failed at cache-hit exit (:748) + final exit (:768)

src/capi/
└── message_read.cpp     # consume result.alloc_failed → FIXPP_ERR_WIRE_LIMIT_EXCEEDED before presence probe

tools/codegen/fixpp-codegen/
└── emit_messages.cpp    # generated nested accessor threads result.slices / result.alloc_failed

tests/
├── capi/                # NEW: C-ABI arena-exhaustion witness (+ repeated-read, controls)
├── wire/                # NEW: typed group_view.alloc_failed() witness; adapt existing span callers
├── fuzz/                # adapt fuzz_wire_nested_slice callers (.slices)
└── alloc_guard/         # adapt nested_group_slices caller (.slices)

specs/003-dictionary-codegen/contracts/golden/
└── v{44,50sp2,42,vt11}_Messages.golden.hpp   # REGENERATE (D7)

specs/004-wire-codec/contracts/
└── group_view.hpp       # shape oracle — add alloc_failed()
```

**Structure Decision**: Single-project C++ library. The change is confined to the wire nested-read primitive, its two consumers (C-ABI + codegen-emitted typed accessor), and their tests/goldens. No new module or directory.

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|--------------------------------------|
| Return-type change on a widely-called primitive (~20 test-site edits) | A status-bearing **return** is the only shape that makes the failure un-ignorable by default (D1) — an out-param defaulting to `nullptr` keeps silent-drop as the default path and re-inherits the exact bug this feature kills. | Out-param `bool* = nullptr` rejected: default-silent, fails a hostile Gate A on "fixed silent truncation with an opt-in flag that defaults to silent". |
| Public `group_view::alloc_failed()` addition | The typed nested accessor's only observable is the returned `group_view`; there is no error channel on the typed path (FR-004: value/status, no throw across `noexcept`). | No alternative — a throw terminates; an out-param on a generated value-returning accessor is not observable by a typed caller. |
| Article X PASS-with-note (C-ABI behavior change) | Returning `WIRE_LIMIT_EXCEEDED` where the read formerly returned `OK`/nc=0 is a *behavioral* change on the frozen C-ABI, even though no symbol/layout changes. | Leaving it silent is the defect; a new dedicated code would add macro/completeness churn for no correctness gain (clarified FR-008). Confirm at Gate A. |

## Gate A

- Round 1 applied 2026-07-13: Codex P1=1 P2=0 P3=1; Opus post-judging P1=1 P2=0 P3=3; rewrite addresses root cause "status origin too narrow — group_slices() second bad_alloc path" (widen alloc_failed to OR the sub-table group_slices status at both nested_group_slices exits) + C++23 cite + L-073-1 scope note + second-loss read-twice witness. Reviews: research/reviews/codex_073-nested-read-arena-failloud_gate_a_review.md, research/reviews/opus_073-nested-read-arena-failloud_gate_a_adversarial_review.md.
