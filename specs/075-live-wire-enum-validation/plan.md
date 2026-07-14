# Implementation Plan: Live-Wire Enum-Value Validation

**Branch**: `075-live-wire-enum-validation` | **Date**: 2026-07-14 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/075-live-wire-enum-validation/spec.md`

## Summary

Turn `dict::table_view::enum_valid()` from a `return true` stub into a real, dictionary-backed domain check, so an inbound message carrying an out-of-domain value for a codeset-backed field is rejected with **SessionRejectReason 5** — firing, for the first time, a reject arm that `reject_reason_map.hpp:20-23` already documents as dead.

Three moving parts, in dependency order:

1. **`XmlLoader` learns code sets** (FR-001). It currently never reads `<value enum= description=>` at all, so the nine QuickFIX dictionaries carry an **empty** enum store. It populates 074's existing `enum_values_`/`enum_runs_` — no new storage type.
2. **`as_table_view()` projects an owned enum-domain table** into `table_view` (FR-002/FR-005), carrying the per-tag **multi-value bit** that the collapsed `field_type` cannot express.
3. **`enum_valid()` consults it** (FR-003/FR-004) — absent tag ⇒ accept; single-value ⇒ binary-search; multi-value ⇒ tokenize on a single space and require every token.

**Zero change at the two validator call sites** (`validator.hpp:148`, `:325`) — the behavior lands entirely inside the dictionary layer, so nested groups, header fields, and admin messages all inherit it for free.

Parity with QuickFIX is proven by a **checked-in golden table generated from the real, already-built reference engine** (FR-018), with QuickFIX's non-enum switches explicitly pinned (FR-019) — because an unpinned switch is exactly what produced this spec's one false parity claim (research R-6).

## Technical Context

**Language/Version**: C++20 (as the rest of fixpp)

**Primary Dependencies**: none new. pugixml (already used by both loaders); QuickFIX v1.16.0 **build-time-only, local-only** for golden generation (`reference-engines/` is gitignored — CI never sees it).

**Storage**: N/A (in-memory dictionary tables only)

**Testing**: GoogleTest + ctest, per the 068 whole-binary grouping convention (author tests grouped, select by `ctest -L`, never by exe name)

**Target Platform**: Linux (gcc/clang, Tier 1), Windows/MSVC (Tier 2), libc++ (Tier 3)

**Performance Goals**: no measurable throughput regression on the validated-message hot path (SC-006). `enum_valid` is per-field, per-message: **O(1)** tag lookup + **O(log C)** code lookup, **zero allocation**. Sorted codes matter — `MsgType(35)` has 92 codes in FIX44 and is present on *every* message.

**Constraints**: `enum_valid` stays `noexcept` + allocation-free. Table built once at config time (`[const §XV.1]`), never per message. Zero C-ABI change (frozen `1.5.0`). Empty code set ⇒ **accept** (the anti-reject-everything floor, FR-003).

**Scale/Scope**: 10 dictionaries. Largest is FIX50SP2 — 668 enum-backed fields / 5565 codes. FIX44 (the real-world case) — 245 enum-backed fields / 1708 codes / 8 multi-value tags.

## Constitution Check

*GATE: checked before Phase 0, re-checked after Phase 1 design.*

| Article | Requirement | Status |
|---|---|---|
| **I §1** (supported version set) | FIX Latest's *"live wire validation"* is listed as **post-1.0** | ⚠️ **AMENDMENT REQUIRED — v0.6 → v0.7 (MINOR)**. See research **R-9** and Complexity Tracking. Not a violation to be waived: an amendment to be ratified at Gate A. |
| **XVIII §5** (no early-ship of deferred post-1.0 scope) | — | ✅ No residual conflict **once I §1 is narrowed** — the scope ceases to be deferred. Ordering matters: the amendment is what discharges this, not a waiver. |
| **VII §8** (068: author tests grouped; select via `ctest -L`) | — | ✅ New tests join existing grouped binaries; selected by label, never by exe name. |
| **VIII §5 / XV.1** (no per-message heap; config-time tables) | — | ✅ Enforced by design: owned table built once in `as_table_view()`; `enum_valid` allocation-free + `noexcept` (research R-2). Pinned by the existing alloc-guard suite. |
| **XV.9** (no `std::mutex`/`std::shared_mutex` on these paths) | — | ✅ None introduced; the table is immutable after construction. |
| **C-ABI freeze (`1.5.0`)** | — | ✅ Zero change to `include/fix/c_api*` or the exported symbol set (FR-011). Asserted by the existing ABI-golden gate. |
| **Testing rule** (sanitizer/analyzer findings are real until disproven) | — | ✅ Standing; no waiver anticipated. |
| **Dependency rule** (never propagate a pinned version verbatim) | — | ✅ No new dependency, no new version pin. |
| **Appendix A** (mandatory Codex Gate A triggers) | Constitution amendment + dictionary/version semantics | ✅ **Gate A is mandatory** for this feature (it was regardless; the amendment reinforces it). |

**Post-Phase-1 re-check**: unchanged. The design adds no new external dependency, no new public API beyond an internal `table_view` table, no threading primitive, and no C-ABI surface. The **only** constitutional item is the I §1 amendment, tracked as an explicit deliverable rather than an exception.

## Project Structure

### Documentation (this feature)

```text
specs/075-live-wire-enum-validation/
├── spec.md              # 19 FRs, 11 SCs, 5 clarifications
├── plan.md              # This file
├── research.md          # Phase 0 — R-1..R-9 + open items O-1..O-3
├── data-model.md        # Phase 1
├── quickstart.md        # Phase 1
├── contracts/
│   └── enum-domain.md   # Phase 1 — the enum_valid behavioral contract
├── checklists/
│   └── requirements.md  # spec quality + design landmines
└── tasks.md             # /speckit-tasks — NOT created here
```

### Source Code (repository root)

```text
include/fixpp/dict/
├── table_view.hpp        # enum_domain store + real enum_valid() + tokenizer   [CORE]
└── dictionary.hpp        # as_table_view() doc: enum table now populated

src/dictionary/
├── xml_loader.cpp        # parse <value enum= description=> → enum_values_/enum_runs_  [CORE]
└── dictionary.cpp        # as_table_view(): project code sets + multi-value bit  [CORE]

tools/quickfix_enum_golden/    # NEW — local-only generator (links reference-engines QuickFIX)
                              # + the checked-in golden table that CI consumes

tests/
├── dictionary/           # loader code-set population; census pins (SC-002/010/011)
└── wire/                 # enum_valid unit + validator/session reject witnesses,
                          # multi-value, header enums, Logon, golden-parity (SC-009)

spec/behaviors-and-limitations.md   # B-row (behavior change) + L-075-1 (no reason-4 slot)
spec/feature-catalogue.md           # D-011 carve-out correction (rides the amendment)
.specify/constitution.md            # Article I §1 → v0.7 + Sync Impact Report
```

**Structure Decision**: single-project C++ library layout, unchanged. The feature is deliberately **narrow in blast radius but deep in verification** — three production files carry the entire behavior change; the bulk of the work is proving it (golden parity, mutation-discriminating witnesses, censuses).

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|---|---|---|
| **Constitution amendment (Article I §1, v0.6 → v0.7)** | The enum check is **dictionary-generic**: a FIX Latest dictionary in a validating session gets it with *zero* FIX-Latest-specific code. The ratified baseline lists FIX Latest's "live wire validation" as post-1.0, so shipping it silently would leave a shipped capability contradicting the constitution. | *Version-keyed carve-out* (suppress the check for `vlatest` so the carve-out stays intact) — rejected: it means writing deliberate, artificial code to make a **correct** check not run on one version, which we would then justify and later delete. *Argue the carve-out never meant this* — rejected: leans on a reading the text does not support. Amending is the honest option; 035/043/068/069 are exactly this precedent. |
| **`table_view` owns copies of the code bytes** (rather than aliasing the Dictionary pool) | Preserves the documented *"the returned `table_view` owns its tables"* contract (`dictionary.hpp:193-205`) and avoids silently coupling validator lifetime to Dictionary lifetime. | *Alias `string_view`s into `name_pool_`* — zero-copy, but would be the **first** external aliasing in a fully self-owning type, converting a currently-legal usage (a `table_view` outliving its Dictionary) into a use-after-free no existing test would catch. The copy is one config-time allocation, <64 KB worst case, off the hot path. Not worth the lifetime hazard. |

**Note**: neither row is a shortcut being waived — both are deliberate, argued choices with the cheaper alternative rejected on correctness grounds, not effort.

## Risks (ranked)

1. **Multi-value false-reject (FR-004/FR-005).** If the multi-value bit is not carried into `table_view`, `ExecInst(18)=1 G 6` — conformant, shipped, 8 such tags in FIX44 — is rejected. This does not under-validate; it **breaks working traffic**. Mitigated by R-3 + a table-driven witness over the full multi-value census.
2. **Reject-everything regression (FR-003).** If an absent/empty code set mapped to *reject* instead of *accept*, every legacy dictionary would reject nearly every message. Mitigated by making absent-tag ⇒ `true` the first branch, plus a direct pin (not merely a green suite). This is also what keeps **FIXT11** working at all — its `MsgType` has zero codes.
3. **Behavior change for existing strict-validating sessions.** By design (FR-010, no sub-flag). Must land as a B-row + release note, not as a surprise.
4. **Logon lockout (FR-013).** A peer with an out-of-domain admin enum can no longer establish a session. Deliberate (QuickFIX parity), bounded by the opt-in flag, pinned by SC-008.
5. **Dangling views in `XmlLoader`.** Enum views MUST be bound in the existing post-`shrink_to_fit()` pass (research R-4). Binding during the parse dangles on the next pool reallocation.
6. **Golden measures the wrong thing (FR-019).** An unpinned QuickFIX switch conflates unrelated validation differences with enum divergence. This already bit us once (R-6).

## Next

`/speckit-tasks` → **Gate A is mandatory before `/tasks`** per `.specify/pipeline.md` (Gate A runs after `/plan`, before `/tasks`), and doubly so here: the constitution amendment must be ratified at Gate A, not discovered at Gate B.
