# Implementation Plan: Structural Repeating-Group Detection for Legacy FIX Dictionaries

**Branch**: `082-structural-group-detection` | **Date**: 2026-07-29 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/082-structural-group-detection/spec.md`

## Summary

Repeating-group detection is gated on the XML-declared field **datatype**
(`FieldRef::type == field_data_type::NumInGroup`) at nine sites across the runtime dictionary
and the codegen emitters. FIX 4.0/4.1/4.2 type every `<group>` count field `INT`, so those
dictionaries register zero groups — `fixpp::v42` has no typed group accessors, legacy inbound
group reads are absent or positionally-wrong, and the 077/078 typed builder tier had to exclude
`v42` outright (issue #196).

The technical approach, established in [research.md](./research.md), is a **re-point onto
structural truth that already exists on both sides** — not new dictionary plumbing:

- **Runtime** — swap the datatype test for **`group_first_field(fr.tag) != 0`** (D-1). That is an
  existing *public* accessor which binary-searches the `groups_` table built solely from the
  `<group>` element. **No new API, public or internal.**
- **Codegen** — `MessageIR::group_order` is *already correctly populated for FIX42 today*
  (`ir.cpp:80` `walk_level` keys on the element name). Add one codegen-tool-local
  `VersionIR::group_tags` set derived from it, and have all six emitter sites consult that one
  value (D-3).
- **Driver** — delete `if (ir.ns != "v42")` (`main.cpp:132`) with **no** replacement version
  predicate; `vt11` continues to self-skip via its genuinely empty application registry (D-8).

`FieldRef::type` is not modified (D-4), which is what makes the byte-identity predictions
falsifiable rather than tautological.

## Technical Context

**Language/Version**: C++23 (library + host codegen tool); Python 3 for the checked-in census oracle

**Primary Dependencies**: pugixml (loader + codegen-tool-local re-parse), GoogleTest. No new dependency.

**Storage**: N/A — in-memory dictionary metadata plus checked-in golden files

**Testing**: GoogleTest via ctest labels (`ctest -L codegen`, `-L dictionary`, `-L wire`, `-L session`); pytest for the SWIG bindings (untouched)

**Target Platform**: Linux (Clang + GCC) Tier 1; Windows/MSVC Tier 2; libc++ Tier 3

**Project Type**: C++ library with a build-time host codegen tool

**Performance Goals**: No hot-path change. `as_table_view()` is session/validator **setup**-time, not per-message; the added `group_first_field` call is an O(log G) binary search per field per message, at setup only. No benchmark obligation under Article VIII §3 — recorded explicitly so Gate B does not read the omission as an oversight.

**Constraints**: C-ABI frozen at `1.5.0`, no symbol/signature change (FR-017). Read goldens for `v44`/`v50sp2`/`vt11`/`vlatest` and builder goldens for `v44`/`v50sp2`/`vlatest` byte-identical (FR-015). Alloc discipline in `as_table_view()` unchanged — tables built once at setup, never per message.

**Scale/Scope**: 9 XML dictionaries + 1 Orchestra; effective registration delta on 4 of them (FIX40 +4, FIX41 +7, FIX42 +18, FIX43 +1). Codegen/golden impact confined to `v42` (46 messages, 39 `app`, 18 group tags). ~9 production sites, ~7 test sites.

## Constitution Check

*GATE: evaluated before Phase 0, re-evaluated after Phase 1 design (below).*

| Article | Requirement | Status | Evidence / how satisfied |
|---|---|---|---|
| **VI** — 100% FIX rule | Every OFFICIAL row traceable; Normative References present | **PASS** | No new OFFICIAL rows — this is correctness of existing FIX 4.0–4.3 coverage. Normative References cite `coverage-index.md:189/239/307` plus the four L-rows being closed. FR-008 is the Article-VI heart: a `required='Y'` group must be *representable* in `Args`, so its omission is detectable rather than silent. |
| **VII** — Testing / TDD | RED→GREEN first; no code without a test; grouped isolation-safe tests selected by label | **PASS** | Every FR carries a pin (data-model.md § Pin map). Two existing descope tests invert (FR-016b) and are the natural RED starts. New tests join existing labelled buckets (`dictionary`, `codegen`, `wire`); no `gtest_discover_tests`. |
| **VIII** — Perf budgets | Hot-path change ⇒ bench in the PR | **PASS (N/A)** | Setup-time only; no hot-path edit. See Performance Goals. |
| **IX** — Coverage / sanitizers / static analysis | ≥95% line, ≥85% branch on touched modules; ASan/UBSan/TSan; clang-tidy/format/cppcheck/IWYU | **PASS (planned)** | Touched modules: `src/dictionary/`, `tools/codegen/`. The predicate **replaces** an existing branch rather than adding an error path, so no uncovered-error-path waiver is anticipated; each replaced branch is directly exercised by its pin. |
| **X** — ABI policy | No silent C-ABI break | **PASS** | FR-017: no `capi/` edit, no symbol-golden or abidiff regeneration; `FIXPP_C_ABI_VERSION` stays `1.5.0`. C-ABI *behavior* for FIX40/41/42 group reads changes — that is the fix — but no surface does. |
| **XI** — Concurrency | — | **PASS (N/A)** | No threading, executor, or cancellation change. |
| **XII** — Security / TLS | — | **PASS (N/A)** | Untouched. |
| **XV** — Banned patterns | No per-message allocation, no hidden global state | **PASS** | `as_table_view()` still builds its tables once at setup; the added predicate is a lookup and allocates nothing. |
| **XVI §3** — `/clarify` mandatory (codegen + wire trigger) | Before `/plan` | **PASS** | Completed 2026-07-29 — 2 questions answered, 2 resolved from source, zero open items. |
| **XVI §4** — `/analyze` mandatory | Same trigger set | **PENDING** | Pipeline step 6, after `/speckit-tasks`. |
| **XVII §1** — Gate A required | Triggers include wire format / parser / **codegen layout** | **REQUIRED** | Triggered by the codegen-layout change. `/gate-a` runs after this plan and **before** `/speckit-tasks`. |

**Result: no violations. Complexity Tracking table omitted — nothing to justify.**

The design actively *reduces* complexity: one predicate replaces nine scattered datatype tests,
and it adds no public API (D-1). The alternative "additive enumeration accessor" was rejected
precisely because it would enlarge the frozen surface for no gain.

### Post-Phase-1 re-evaluation

Re-checked after `data-model.md` / `contracts/` / `quickstart.md` were written. **No gate moved.**
Phase 1 introduced exactly one new data element (`VersionIR::group_tags`, codegen-tool-local,
Article X unaffected) and no new runtime type, interface, allocation, or thread interaction. The
contract in `contracts/group-detection.md` is a *behavioral* contract over existing accessors, not
a new interface. Article VII's obligation strengthened rather than weakened: the pin map now names
a witness for all 24 FRs.

## Project Structure

### Documentation (this feature)

```text
specs/082-structural-group-detection/
├── plan.md                        # This file
├── spec.md                        # /speckit-specify + /speckit-clarify output
├── research.md                    # Phase 0 — decisions D-1..D-11
├── data-model.md                  # Phase 1 — entities, invariants, FR→pin map
├── quickstart.md                  # Phase 1 — runnable validation guide
├── checklists/
│   └── requirements.md            # spec-quality checklist (16/16)
├── contracts/
│   ├── predicate_census.py        # non-circular raw-XML oracle (checked in)
│   └── group-detection.md         # the predicate contract
└── tasks.md                       # /speckit-tasks output — NOT created here
```

### Source Code (repository root)

```text
src/dictionary/
└── dictionary.cpp                 # as_table_view(): 3 detection sites (:398, :441, :446) — D-1

tools/codegen/fixpp-codegen/
├── ir.hpp                         # + VersionIR::group_tags — D-3
├── ir.cpp                         # populate group_tags from MessageIR::group_order
├── emit_messages.cpp              # :166, :234, :337, :347, :425 — D-3
├── emit_reify.cpp                 # :217, :227 — D-3
├── emit_builders.cpp              # :606 top_level_synthetic_members — D-3
└── main.cpp                       # :132 delete the v42 exclusion — D-8

tests/dictionary/
├── reused_tag_census.hpp          # re-point to the raw-XML oracle — D-6 / FR-018
├── reused_tag_census_test.cpp     # carve-out text + FIX40/41/42/43 assertions
├── required_scope_test.cpp        # :107 carve-out comment now false — rewrite
└── required_scope_census_test.cpp # :341 carve-out + COUNT pins move

tests/codegen/
├── test_077_builder_no_emit.cpp              # V42EmitsNoBuilders INVERTS — FR-016b
└── test_077_v42_vt11_completeness_and_c4.cpp # expected(v42) ∅ → real set — FR-016b

tests/wire/ · tests/session/       # ungated parse-correction pins, strict OFF — FR-006a

specs/003-dictionary-codegen/contracts/golden/v42_Messages.golden.hpp   # regenerates
specs/078-precompiled-builder-libs/contracts/golden/                    # gains v42
spec/behaviors-and-limitations.md                                       # closes L-063-1/061-1/066-1/077-1 — FR-019
```

**Structure Decision.** Existing layout; no new module, directory, or build target. The change is
confined to one runtime translation unit (`src/dictionary/dictionary.cpp`), the codegen tool, the
test tree, and the golden corpus. The only structural addition anywhere is a single
codegen-tool-local field on `VersionIR` (D-3) — **no runtime type gains a member**, which is what
keeps FR-017 and the `Fields.hpp` byte-identity prediction (FR-016a) intact.

## Implementation Sequencing (input to `/speckit-tasks`)

Ordered so that each step's evidence exists before the step that depends on it.

1. **Oracle first** — port `contracts/predicate_census.py` into `tests/dictionary/reused_tag_census.hpp`
   as a raw-XML derivation (FR-018/D-6). This must land **before** the predicate change, or the
   census moves in lockstep with the code it checks and witnesses nothing.
2. **RED pins** — assert the target end-state while the code is unchanged: FIX40/41/42 register
   4/7/18 groups, FIX43 registers 576, tag 82 stays unregistered, `v42` emits builders. These go
   RED, including the two inverted 077 descope tests.
3. **Runtime predicate** (D-1) — all three `as_table_view()` sites in one change unit (FR-004).
4. **Codegen predicate** (D-3) — `VersionIR::group_tags` + the six emitter sites.
5. **Regenerate + diff every version** (FR-015/FR-016) — the discriminating check. `v44`/`v50sp2`/
   `vt11`/`vlatest` must diff **clean**; `v42` read artifacts must match D-10's budget.
   Force a clean codegen rebuild first — the known emitter-staleness trap silently compiles a
   stale `Reify.hpp` in non-debug dirs.
6. **Driver exclusion + v42 builder goldens** (D-8, FR-007/009).
7. **US4 exemplar** — `MassQuote` `296 → 295` golden + read round-trip.
8. **Docs** — close L-063-1/L-061-1/L-066-1/L-077-1; add the FR-006c behavior change + release
   note; refresh the now-false carve-out comments in the four dictionary tests.

Because `tools/codegen/**` is touched, `ctest -L codegen` is mandatory locally — a label-filtered
run that omits it has previously missed a subsystem's COUNT pin.

## Gate A framing

`/gate-a` runs **after this plan and before `/speckit-tasks`** (pipeline step 4), not after tasks.
Reviewer attention is best spent on:

- **D-1** — the claim that **no new accessor is needed**: `group_first_field` is already public and
  already answers the structural question.
- **D-2** — the *corrected, weaker* case against a union predicate. A union is **not** wrong today;
  the argument is single-sourcing plus closing a latent trap. Review the argument as stated, not
  the stronger one the issue originally implied.
- **D-3** — `group_order` over a member-derived set (member-independence, I-6/P1).
- **D-5 — read the citation, not the comment.** The ungated-parse claim underpinning FR-006a, and
  the compat posture the user signed off on, rests on the comment at `session.cpp:992` being
  **stale**. The load-bearing evidence is the *consume* site:
  `Parser<access_mode::Index> pd_parser{*inbound_tv_}` at `session.cpp:328`, inside
  `parse_and_dispatch_`, with no `validate_inbound_messages` condition anywhere on that path. A
  reviewer who reads only the `:992` comment will wrongly challenge FR-006a.
- **C2 / D-10** — the reachability-restricted registration counts (**measured** by the oracle, not
  derived from set cardinality) and the per-artifact delta budget.
