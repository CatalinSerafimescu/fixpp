# Implementation Plan: Live-Wire Enum-Value Validation

**Branch**: `075-live-wire-enum-validation` | **Date**: 2026-07-14 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/075-live-wire-enum-validation/spec.md`

## Summary

Turn `dict::table_view::enum_valid()` from a `return true` stub into a real, dictionary-backed domain check, so an inbound message carrying an out-of-domain value for a codeset-backed field is rejected with **SessionRejectReason 5** — firing, for the first time, a reject arm that `reject_reason_map.hpp:20-23` already documents as dead.

Three moving parts, in dependency order:

1. **`XmlLoader` learns code sets** (FR-001). It currently never reads `<value enum= description=>` at all, so the nine QuickFIX dictionaries carry an **empty** enum store. It populates 074's existing `enum_values_`/`enum_runs_` — no new storage type.
2. **`as_table_view()` projects an owned enum-domain table** into `table_view` (FR-002/FR-005), carrying the per-tag **multi-value bit** that the collapsed `field_type` cannot express. The projection is **store-driven** — iterate `dict_metadata_handle::enum_runs_` / `enum_values_` so every enum-backed tag gets a domain, then use `message_fields()` only to set `multi_value=true` where `FieldRef` data exists.
3. **`enum_valid()` consults it** (FR-003/FR-004) — absent tag ⇒ accept; single-value ⇒ binary-search; multi-value ⇒ tokenize on a single space and require every token. A store-only tag absent from `message_fields()` defaults to **`multi_value=false`**; that is measured-safe on the shipped dictionaries and pinned by SC-011 so the gate goes RED if a future dictionary violates the assumption.

**Zero change at the two validator call sites**, but they are **not** what the first draft said they were *(re-anchored at Gate A round 1 — finding C-3/RC#2)*:

- **`validator.hpp:148`** sits inside the **Step-1 flat walk** (`:139-158`). `msg.begin()`→`msg.end()` builds a **dict-free `field_iterator` over the raw frame bytes** (`parser.hpp:229-233`, `advance()` at `:444`) — a linear `tag=value` scan with **no group awareness** — so it yields **header + trailer + body + group members at every depth** from one loop. That single walk is where group and header coverage come from.
- **`validator.hpp:325`** is **`validate_field(tag, value)`** (`:323-330`) — a context-free public pure-virtual (`:65-66`), **not** the group-member site. It is a **third public surface** and is in scope with its own tests (**FR-020**).

The behavior lands entirely inside the dictionary layer; group members, header fields and admin messages are covered by the Step-1 walk — but "for free" was doing too much work in the first draft, and it hid two real decisions (**FR-021** `add_enum`, **FR-020** `validate_field`).

Parity with QuickFIX is **derived from** a checked-in golden generated from the real, already-built reference engine (**FR-018**) — **built FIRST, as a blocking Phase-0.5 deliverable**, with the four non-enum switches **and** the single-dictionary topology pinned (**FR-019**) and a manifest + CI gate against staleness (**FR-024**). The golden's *measured output* defines the divergence register; no FR or SC asserts parity ahead of it. *(An unpinned switch is exactly what produced this spec's original false parity claim — research R-6 — and a corpus designed from the same partial call-graph read it was meant to check is exactly what hid the group-member divergence — R-11.)*

## Technical Context

**Language/Version**: **C++23** (`[const §II.1]`, `.specify/constitution.md:63`: *"Language standard: C++23. No fallback to earlier standards"*). *(Gate A round 1 — the draft said "C++20 (as the rest of fixpp)", which was false as a factual claim about the repo. Finding C-6.)*

**Primary Dependencies**: none new. pugixml (already used by both loaders); QuickFIX v1.16.0 **build-time-only, local-only** for golden generation.

**QuickFIX location — corrected (finding O-4).** `reference-engines/` is **not in the library repo**. It sits at the **parent** repo root — `<parent>/reference-engines/quickfix-cpp/` (`lib/libquickfix.so.17.0.0`, **built**) — three levels up and **outside the submodule's git boundary**, so the library's own `.gitignore` says nothing about it and the draft's *"`reference-engines/` is gitignored — CI never sees it"* was true only by accident. The protection is made **structural** instead (FR-024): the generator's root is a validated CMake cache variable `FIXPP_QUICKFIX_ROOT` (default `${CMAKE_SOURCE_DIR}/../../../reference-engines/quickfix-cpp`), the target is **guarded off by default**, and it **hard-errors** if the variable is set but `lib/libquickfix.so` is absent.

**Storage**: N/A (in-memory dictionary tables only)

**Testing**: GoogleTest + ctest, per the 068 whole-binary grouping convention (author tests grouped, select by `ctest -L`, never by exe name)

**Target Platform**: Linux (gcc/clang, Tier 1), Windows/MSVC (Tier 2), libc++ (Tier 3)

**Performance Goals**: no measurable throughput regression on the validated-message hot path (SC-006). `enum_valid` is per-field, per-message: **O(1)** tag lookup + **O(log C)** code lookup, **zero allocation**. Sorted codes matter — `MsgType(35)` has **93** codes in FIX44 (*corrected at Gate A round 1; "92" was wrong in three documents — finding O-7*) and is present on *every* message. The conclusion (sort + binary-search) is unchanged; only the constant was wrong. **93** is also exactly FIX44's declared `<message>` count, which is why SC-010's census passes.

**Constraints**: `enum_valid` stays `noexcept` + allocation-free. Table built once at config time (`[const §XV.1]`), never per message. Zero C-ABI change (frozen `1.5.0`). Empty code set ⇒ **accept** (the anti-reject-everything floor, FR-003).

**Scale/Scope**: 10 dictionaries. Largest is FIX50SP2 — 668 enum-backed fields / 5565 codes. FIX44 (the real-world case) — 245 enum-backed fields / 1708 codes / 8 multi-value tags.

## Constitution Check

*GATE: checked before Phase 0, re-checked after Phase 1 design.*

**This table MUST enumerate every article the feature touches. An omitted article is an *unchecked gate*, not a passed one** — a gate that cannot fail is a false-green of the class `[[feedback_ci_gate_observes_not_asserts_witness_skips_into_green]]` names. *(Gate A round 1, finding C-2: the draft table had **no Article VI row at all**, so it could not fail Article VI — while the feature changes catalogue/coverage semantics and shipped **no** Normative References section, which Article VI §5 mandates.)*

| Article | Requirement | Status |
|---|---|---|
| **I §1** (supported version set) | FIX Latest's *"live wire validation"* is listed as **post-1.0** (`.specify/constitution.md:52`, verbatim) | ⚠️ **AMENDMENT REQUIRED — v0.6 → v0.7 (MINOR)**. See research **R-9** and Complexity Tracking. Not a violation to be waived: an amendment to be ratified at Gate A. MINOR is the right class under **XX §4** (none of its enumerated triggers — banned-pattern additions, perf-budget tightening — apply). |
| **II §1** (language standard) | *"Language standard: C++23. No fallback to earlier standards"* (`:63`) | ✅ C++23. *(Was ❌ — the draft's Technical Context said C++20. Corrected; finding C-6.)* |
| **VI §4** (bidirectional traceability — every OFFICIAL row's coverage-index entry lands **before** the row does, `:116`) | 075 delivers the `enum values` clause of the **existing** W-014 row and narrows D-011 | ⚠️ **CONDITIONAL — blocking until the coverage-index / catalogue edits land.** `spec/coverage-index.md:581` still says live-wire validation is **backlog**; `:704` and `spec/feature-catalogue.md:130` (D-011) still say it remains post-1.0; `coverage-index.md:68`'s 041 note still says enum checks are deferred (**L-041-1**, `behaviors-and-limitations.md:1478`). All six edits are enumerated in **spec.md § Normative References** as `/implement` deliverables. Not a PASS until they land. |
| **VI §5** (every `/specify` artifact carries a **Normative References** section, `:117`) | — | ✅ **Added at Gate A round 1** (`spec.md § Normative References`). *(Was a silent ❌: the bundle had none, and this row's absence from the table is what let it pass unnoticed.)* |
| **XIV §2** (≤5 pure-virtuals on `Validator`) | `validate_field()` is a **frozen** pure-virtual (`validator.hpp:65-66`) that begins enum-validating | ✅ No new virtual; the cap is untouched. The *surface* is now specified and tested rather than changed by side effect (**FR-020**). |
| **XVIII §5** (no early-ship of deferred post-1.0 scope) | — | ✅ **Earned, and here is why** (rather than asserted): §5 bars early shipping of post-1.0 ***protocols***, and XVIII §2's roadmap enumerates protocols (SOFH, SBE, FIXP, FAST, JSON, GPB, MMT). **Dictionary-driven wire validation is not a protocol** — so §5 is not engaged on its own terms. The residual question is purely I §1's carve-out, and that is discharged by the **amendment**, not by a waiver. Ordering matters: the amendment is what makes the scope cease to be deferred. |
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
├── spec.md              # 24 FRs, 11 SCs, 3 clarification sessions, Normative References
├── plan.md              # This file
├── research.md          # Phase 0 — R-1..R-12 + open items
├── data-model.md        # Phase 1
├── quickstart.md        # Phase 1
├── contracts/
│   └── enum-domain.md   # Phase 1 — the enum_valid contract + the C-6 divergence register
├── checklists/
│   └── requirements.md  # spec quality + design landmines
└── tasks.md             # /speckit-tasks — NOT created here
```

### Source Code (repository root)

*(Completed at Gate A round 1 — finding O-8/C-2. The draft inventory omitted `reject_reason_map.hpp` (which carries three comments 075 makes **false**, and whose dead-enum-arm comment is the bundle's own **headline**), `spec/coverage-index.md` (which Article VI §4 makes load-bearing), and the release-note artifact (which FR-010 named but which **does not exist** — resolved: the B-row **is** it; there is no `CHANGELOG.md` in this repo).)*

```text
include/fixpp/dict/
├── table_view.hpp        # enum_domain store + real enum_valid() + tokenizer   [CORE]
│                         # + REAL add_enum() + its multi-value companion (FR-021)
└── dictionary.hpp        # :66-67  doc: "Populated only by OrchestraLoader" → FALSE after 075
                          # :184-189 doc (enum_values() itself — ADDED Gate A round 2, O2-4):
                          #          "(all XmlLoader/QuickFIX dictionaries)" carry an empty store
                          #          → FALSE; and "orthogonal to table_view::enum_valid
                          #          (unchanged Phase-1 stub)" → FALSE (enum_valid now READS it)
                          # :195-200 doc: "all validator method calls O(1)" → now O(log C);
                          #          "enum_valid is stubbed to true (FR-005)" → FALSE after 075

include/fixpp/wire/
├── validator.hpp         # NO CODE CHANGE — but :148 (Step-1 walk) and :325 (validate_field,
│                         # FR-020) both go live. Anchored here so /tasks does not miss FR-020.
└── reject_reason_map.hpp # NO CODE CHANGE — but THREE comments become false and MUST be fixed:
                          # :22-23 "type-arm only in Phase-1 since the enum arm is dead",
                          # :60-61 "Reason 5 … type arm; enum arm is dead Phase-1",
                          # :1-19  the same framing in the doc block.        [DOC-CORRECTION]

src/dictionary/
├── xml_loader.cpp        # parse <value enum= description=> → enum_values_/enum_runs_  [CORE]
│                         # + extend pool_estimate (:646-658) with the code bytes (R-10)
└── dictionary.cpp        # as_table_view(): project code sets + multi-value bit  [CORE]

tools/quickfix_enum_golden/    # NEW — local-only generator, guarded by FIXPP_QUICKFIX_ROOT
                              # (parent-relative, validated, OFF by default — FR-024)
                              # + the checked-in golden + its MANIFEST that CI consumes

tests/                        # see the FR/AC/SC → test matrix below

spec/behaviors-and-limitations.md   # B-row (behavior change; operator-facing — it IS the
                                    #   release note, FR-010) + B-rows for DV-3 (group-member
                                    #   strictness) and DV-4 (tag 166 on FIX 4.1/4.2)
                                    # + L-075-1 (no reason-4 slot AND type-arm-dependent
                                    #   empty-value disposition)
                                    # + L-041-1 RETIRED (:1478 — "enum-value conformance is
                                    #   NOT validated"; that is what 075 delivers)
                                    # + L-069-1 RESTATED as still open (typed codegen)
spec/coverage-index.md              # :581 live-wire "backlog" → delivered; :189 W-014 enum
                                    #   clause; :68 §4.5.4 L-041-1 retired; :704 gap registry
spec/feature-catalogue.md           # :130 D-011 carve-out correction (rides the amendment)
                                    # :111 W-014 — 075 as delivering feature for `enum values`
.specify/constitution.md            # Article I §1 → v0.7 + Sync Impact Report
```

**Structure Decision**: single-project C++ library layout, unchanged. The feature is deliberately **narrow in blast radius but deep in verification** — three production files carry the entire behavior change; the bulk of the work is proving it (golden parity, mutation-discriminating witnesses, censuses).

## Phase ordering — the golden is a BLOCKING Phase-0.5 deliverable

*(Gate A round 1, root cause **RC#1**. The draft wrote the parity FRs/SCs/corpus **first** and deferred the golden to a later task — which inverts the dependency: the corpus was then designed from the same partial reading of QuickFIX's call graph it was supposed to check, and so was structurally blind to that reading's errors. Two such errors were found by review, not by the corpus: the group-member divergence and the type-arm-dependent empty value.)*

| Phase | Deliverable | Gate |
|---|---|---|
| **0.5** | **Golden generator + golden + manifest** (FR-018/019/024) over the **whole** fixpp Step-1 surface: top-level, header, trailer, **group member**, **nested group member**, multi-value, degenerate whitespace, empty × {`Char`,`String`}, **strict prefix (`MatchType(574)=A`)**, `SettlLocation(166)=US` — **13 rows** *(12 → 13 at T006)*, **every one a message frame through `validate()`**; **no `validate_field()` row** (QuickFIX has no context-free analogue; the FIX50SP2 `validate_field(1128, "bogus")` store-only witness is the **FR-020 unit test** below, not a corpus row — Gate A round 4, O4-1). Single-DD / **non-FIXT** topology (`sessionDD == appDD`, per the row's own dictionary); four booleans pinned **in generator source**; manifest embedded (topology recorded **per dictionary**). **Every `asserted: true` literal re-measured against the shipped XML first** — a **`reject`**-asserting row that **both engines accept** coincides with the stub and no mutation can redden it: a defect, not a row (Gate A round 2, O2-1). | **BLOCKS the parity leg.** The divergence register (`contracts/enum-domain.md` C-6) is **derived from its measured output**. A divergence it reveals that is not an argued register row is a **defect**. |
| 1 | `XmlLoader` code sets (FR-001) + `as_table_view()` projection (FR-002/005) + real `enum_valid` (FR-003/004) + real `add_enum` (FR-021) | the six FR-021 artifacts flip; censuses (SC-002/010/011) |
| 2 | Witnesses + B-rows + doc corrections + coverage-index/catalogue/constitution edits | SC-009 asserted against the Phase-0.5 golden |

## Test matrix — FR/AC/SC → test file → binary → `ctest` label → mutation witness

*(Added at Gate A round 1 — finding C-4. The draft listed only two **directories** with prose comments, so Gate A could not audit grouped-test discipline against it. `[const §VII.8]` (`.specify/constitution.md:131`) mandates whole-binary grouped executables selected by `ctest -L <label>`, **never** by `-R <exe-name>`, and separately mandates that **isolation-sensitive** tests — global alloc/OOM injection, exact-set completeness gates, own `main()` — stay **standalone**.)*

| SC / FR | Witness | Test file | Binary (bucket) | `ctest -L` | Mutation witness (what must turn it RED) |
|---|---|---|---|---|---|
| **SC-001** / FR-006, AC US1 #1-2 | out-of-domain `54=Z` → reject 5, `RefTagID=54`; `54=1` → accept | `tests/wire/validator_enum_domain_test.cpp` **(new)** | `wire_dict_tests` | `075;wire` | revert `enum_valid` → `return true` |
| **SC-001** / FR-006, AC US1 #4 | **group member** + **nested group member** out-of-domain → reject 5, `RefTagID` = member tag. **Through `validate()` / the Step-1 walk — NOT `validate_field()`** | `tests/wire/validator_enum_domain_test.cpp` **(new)** | `wire_dict_tests` | `075;wire` | restrict the Step-1 walk to top-level fields → must go RED. *(This is the DV-3 pin; a `validate_field()` unit test does **not** discharge it.)* |
| **FR-020** | `validate_field()` in-domain accept / out-of-domain reject / multi-value / empty, **plus the FIX50SP2 store-only witness `validate_field(1128, "bogus")` reject / `validate_field(1128, "9")` accept** | `tests/wire/validator_type_check_test.cpp` **(flip, artifact #2)** | `wire_pure_tests` | `075;wire` | revert `enum_valid` → `return true`, **or** build the enum-domain table only from `message_fields()` so store-only tags stay unconstrained |
| **SC-003** / FR-004, FR-014 | `18=1 G 6` accept; `18=1 ZZ 6` reject; `18=1` accept; `18=1  G` / `18=1 ` reject; table-driven over the **full** multi-value census | `tests/wire/validator_enum_multivalue_test.cpp` **(new)** | `wire_dict_tests` | `075;wire` | replace the tokenizer with a whole-string lookup → the **accept** case must flip to reject |
| **SC-004** / FR-003 | absent-tag ⇒ accept, **directly**; FIXT11's 8 msg types all accept via the empty-set arm | `tests/wire/validator_enum_domain_test.cpp` **(new)** | `wire_dict_tests` | `075;wire` | make the absent-tag branch reject → whole suite goes RED (that is the point) |
| **FR-015** | `PossDupFlag(43)=X` **header** field → reject 5, `RefTagID=43` | `tests/wire/validator_enum_domain_test.cpp` **(new)** | `wire_dict_tests` | `075;wire` | body-only walk → must go RED |
| **SC-002** / FR-001, US3 | per-dictionary enum-backed-field + total-code counts **against the shipped XML**, over the **nine XmlLoader dictionaries** (the Context census table — the tenth is 074's, O2-7); `enum_values(54)` on FIX44 | `tests/dictionary/xml_enum_codeset_test.cpp` **(new)** | `dictionary_pure_tests` | `075;dictionary` | — (exact-count census) |
| **SC-010** | every declared `<message msgtype=X>` ∈ the `MsgType(35)` codeset, **or** the codeset is empty (FIXT11) | `tests/dictionary/dict_enum_census_test.cpp` **(new)** | **standalone** | `075;dictionary;census` | **exact-set completeness gate ⇒ isolation-sensitive ⇒ standalone** per `[const §VII.8]` |
| **SC-011** / FR-022 | zero dup codes / zero missing `enum` / zero missing `description`; the space-bearing declared codes across the ten are **EXACTLY** `{FIX41:166:'ISO Country Code', FIX42:166:'ISO Country Code'}` — **GREEN today**; and every enum-backed tag present in the dictionary store but absent from message expansion has a **non-multi-value** declared type today, so the `multi_value=false` default for store-only tags is measured-safe. Any addition, removal or changed literal — or any store-only tag becoming multi-value — **fails** (**DV-4**; exact-set reformulation at Gate A round 2, C2-1) | `tests/dictionary/dict_enum_census_test.cpp` **(new)** | **standalone** | `075;dictionary;census` | same — exact-set gate |
| **SC-008** / FR-013 | inbound **Logon** w/ out-of-domain `EncryptMethod(98)` → reject 5 + session does **NOT** establish; all-in-domain Logon **does** establish | `tests/session/test_enum_validation_logon.cpp` **(new)** | `session_pure_tests` | `075;session` | revert `enum_valid` → both halves must not both stay green |
| **SC-009** / FR-018, FR-019 | fixpp's verdict matches the golden on **every `asserted: true` row**; `asserted: false` rows are recorded, not asserted. *(Binary: **standalone** — consumes the checked-in golden + manifest; isolation-sensitive data-driven exact gate.)* | `tests/wire/enum_golden_parity_test.cpp` **(new)** | **standalone** | `075;golden;parity` | **revert `enum_valid` → `return true`: EVERY `asserted: true` *reject* row must go RED.** *(Filled at Gate A round 2 — O2-2; this cell previously held a bucket rationale, not a mutation, in the row that arbitrates the entire parity leg.)* **Plus the per-row obligation that catches O2-1: a `reject`-asserting row that BOTH engines ACCEPT is a DEFECT, not a row** — it coincides with the stub, never exercises the rule it names, and no mutation here can redden it. *(The **accept**-asserting rows — 1 (`54=1`) and 3 (`18=1 G 6`) — are the paired positive controls against **over**-rejection: row 3 reddens under the tokenizer mutation, row 1 under any absent-tag⇒reject / reject-everything regression. They are not defects and this obligation does not apply to them.)* |
| **FR-024** | manifest matches the checked-in corpus/config: recomputed dictionary SHA-1s + corpus hash + generator-source hash + topology + 4 flags | `tests/wire/enum_golden_manifest_test.cpp` **(new)** | **standalone** | `075;golden;manifest` | **hand-edit the golden ⇒ must go RED.** This is the anti-false-green gate; it runs **without** `reference-engines/` |
| **SC-006** | zero additional allocations per message; `as_table_view()` build time + `table_view` footprint on FIX50SP2 (5565 codes) — **measured**, not asserted (R-10/O-3). *(Binary: **standalone** — global-alloc interception, per `[const §VII.8]`.)* | existing alloc-guard suite + `bench/dictionary/` | **standalone** (alloc-guard) + bench | `alloc_guard;mallocnesia` / bench | **remove the config-time table build and rebuild the enum table per message: the alloc guard must go RED.** *(Filled at Gate A round 2 — O2-2; this cell also held a bucket rationale rather than a mutation.)* |
| **SC-005** | validation **off** ⇒ byte-identical to `main` | existing session suite | `session_pure_tests` | `075;session` | — |
| **SC-007** | zero C-ABI diff; symbol set unchanged | existing ABI-golden gate | **standalone** | `abi` | — |
| **FR-021** | the **six** artifacts flip (census in the table below) | see FR-021's census | various | — | artifact #5 is a **CSV** — data, not code: it will not fail to compile, it fails at runtime with an unhelpful diff |

**Label discipline note.** The `tests/wire/` buckets carry **no `LABELS`** today (they are name-selected — `tests/wire/CMakeLists.txt:1-7`). That is **pre-existing and not 075's to fix**; 075 sets `LABELS` on the buckets and standalone targets it **adds or joins**, so every *new* row above is selectable by `ctest -L`, per `[const §VII.8]`.

## Discharged-checklist table

*(Added at Gate A round 1 — finding O-6. `checklists/requirements.md` explicitly **hands `/plan` a task** in its "Design risks to carry into `/plan`" section, and one of them — dictionary memory growth — was **never discharged** by `plan.md` or `research.md`. A checklist item that says "Confirm at `/plan`" and is then not confirmed is an unchecked gate.)*

| Checklist item (`checklists/requirements.md`) | Discharged by |
|---|---|
| `:46` — **`table_view` ownership contract is a DECISION** | **research R-1** (owns copies; aliasing rejected on lifetime grounds) + **Complexity Tracking row 2**. ✅ |
| `:47` — **String-pool lifetime in `XmlLoader`** | **research R-4** (bind `EnumValueRef`s in the existing post-`shrink_to_fit()` pass, `xml_loader.cpp:853-865`; binding during the parse dangles). ✅ |
| `:48` — **`table_view` cannot express "multi-value"** | **research R-3** + **FR-005** (carry the bit from `FieldRef::field_data_type`, which `as_table_view()` already has in hand — research O-1 RESOLVED). ✅ |
| `:49` — **Hot-path allocation** | **research R-2** + **FR-007** + the alloc-guard row of the test matrix. ✅ |
| `:50` — **Dictionary memory growth. "Confirm at `/plan`"** | ⚠️ **WAS NOT DISCHARGED — now discharged by research R-10** (Gate A round 1). R-4 addressed only `name_pool_`; it said nothing about `enum_values_` / `enum_runs_`, *the two vectors that actually receive the new data* (`dictionary_internal.hpp:102-103`, both constructed from the handle's `mr`). R-10 names the upstream `memory_resource` each loader is handed, states whether it is bounded, reserves **tight** per `[[feedback_fixed_arena_over_reserve_silent_loss_larger_stl]]`, and extends `pool_estimate` (`xml_loader.cpp:646-658`, which today sums **names only** and will now be short by the full code+description byte count on every dictionary). ✅ |

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|---|---|---|
| **Constitution amendment (Article I §1, v0.6 → v0.7)** | The enum check is **dictionary-generic**: a FIX Latest dictionary in a validating session gets it with *zero* FIX-Latest-specific code. The ratified baseline lists FIX Latest's "live wire validation" as post-1.0, so shipping it silently would leave a shipped capability contradicting the constitution. | *Version-keyed carve-out* (suppress the check for `vlatest` so the carve-out stays intact) — rejected: it means writing deliberate, artificial code to make a **correct** check not run on one version, which we would then justify and later delete. *Argue the carve-out never meant this* — rejected: leans on a reading the text does not support. Amending is the honest option; 035/043/068/069 are exactly this precedent. |
| **`table_view` owns copies of the code bytes** (rather than aliasing the Dictionary pool) | Preserves the documented *"the returned `table_view` owns its tables"* contract (`dictionary.hpp:193-205`) and avoids silently coupling validator lifetime to Dictionary lifetime. | *Alias `string_view`s into `name_pool_`* — zero-copy, but would be the **first** external aliasing in a fully self-owning type, converting a currently-legal usage (a `table_view` outliving its Dictionary) into a use-after-free no existing test would catch. The copy is one config-time allocation, <64 KB worst case, off the hot path. Not worth the lifetime hazard. |

**Note**: neither row is a shortcut being waived — both are deliberate, argued choices with the cheaper alternative rejected on correctness grounds, not effort.

## Risks (ranked)

1. **Multi-value false-reject (FR-004/FR-005).** If the multi-value bit is not carried into `table_view`, `ExecInst(18)=1 G 6` — conformant, shipped, 8 such tags in FIX44 — is rejected. This does not under-validate; it **breaks working traffic**. Mitigated by R-3 + a table-driven witness over the full multi-value census.
2. **Reject-everything regression (FR-003).** If an absent/empty code set mapped to *reject* instead of *accept*, every legacy dictionary would reject nearly every message. Mitigated by making absent-tag ⇒ `true` the first branch, plus a direct pin (not merely a green suite). This is also what keeps **FIXT11** working at all — its `MsgType` has zero codes.
3. **Behavior change for existing strict-validating sessions.** By design (FR-010, no sub-flag). Must land as an operator-facing **B-row** in `spec/behaviors-and-limitations.md`, not as a surprise. *(Corrected at Gate A round 2, C2-2: this read "B-row **+ release note**". Per FR-010 there is no release-note artifact in this repo — **the B-row IS it**.)*
4. **Logon lockout (FR-013).** A peer with an out-of-domain admin enum can no longer establish a session. Deliberate (QuickFIX parity), bounded by the opt-in flag, pinned by SC-008.
5. **Dangling views in `XmlLoader`.** Enum views MUST be bound in the existing post-`shrink_to_fit()` pass (research R-4). Binding during the parse dangles on the next pool reallocation.
6. **Golden measures the wrong thing (FR-019).** An unpinned QuickFIX switch — **or an unpinned dictionary topology** — conflates unrelated validation differences with enum divergence. This already bit us once (R-6). The topology leg is the bigger lever: QuickFIX's two-DD FIXT path checks `MsgType(35)` against **FIXT11** (zero codes ⇒ unconstrained), a validation topology fixpp **does not have** (one `cfg_.dictionary`). Pinned to the single-DD / **non-FIXT** path — `sessionDD == appDD`, applied per the corpus row's own dictionary (R-12; *genericized at Gate A round 2, O2-6 — the pin named "FIX 4.4" while the corpus spans FIX44 and FIX41/FIX42*).
7. **Golden goes stale or is hand-edited, and nothing catches it (FR-024).** `reference-engines/` is outside the repo and CI never runs the generator, so a checked-in golden is a false-green surface of exactly the class that has burned this repo twice (`[[feedback_codegen_golden_exists_narrow_verify_misses_it]]`, `[[feedback_sanitizer_canary_must_be_proven_red]]`). Mitigated by the **manifest + CI manifest gate + local regen-and-diff** (FR-024) — *not* by the (vacuous) gitignore claim. **Division of labour, stated exactly** *(narrowed at Gate A round 2, O2-3)*: the **CI manifest gate** catches **tree drift** + a **hand-edit**; it can **NOT** catch drift against a **newer QuickFIX** (`quickfix_version` is recorded but unverifiable in a CI that has no QuickFIX). That leg belongs solely to the **regen-and-diff target**, which is therefore **bound to the per-release out-of-CI interop gate** (`[[project_release_interop_quickfix_fix8]]`) with a named owner and cadence — an OFF-by-default target with no schedule may never run again after Phase 0.5.
8. **Conformant-traffic regression via a non-literal declared code (FR-022).** `SettlLocation(166)="ISO Country Code"` on FIX41/42 makes `166=US` reject. Same *class* as the multi-value trap (risk 1) — it breaks working traffic — but parity-correct (QuickFIX rejects too). Accepted + B-rowed + gated by SC-011's **exact-exception-set** assertion — the space-bearing codes across the ten dictionaries are pinned to exactly `{FIX41:166, FIX42:166}`, so the gate is **green today** and RED on any addition, removal or edit *(reformulated at Gate A round 2, C2-1 — it previously asserted "no code contains a space" while conceding it "fires today", i.e. a gate defined to be RED)*.
9. **`add_enum()` left a no-op (FR-021).** Four existing suites would stay **green while proving nothing** and `table_view_test.cpp:256-269` would keep *enshrining the stub in a passing test* — `[[feedback_coverage_push_enshrines_bugs]]` verbatim. Mitigated by making it real + the named six-artifact census.

## Next

`/speckit-tasks` → **Gate A is mandatory before `/tasks`** per `.specify/pipeline.md` (Gate A runs after `/plan`, before `/tasks`), and doubly so here: the constitution amendment must be ratified at Gate A, not discovered at Gate B.

## Gate A

- Round 1 applied 2026-07-14: Codex P1=2 P2=3 P3=1; Opus post-judging P1=5 P2=6 P3=4; rewrite addresses root causes RC#1 (parity derived-not-asserted + golden manifest gate), RC#2 (call-site re-anchor + validate_field/add_enum FRs), RC#3 (code-string census), RC#4 (auditable traceability tables). Reviews: research/reviews/codex_075-live-wire-enum-validation_gate_a_review.md, research/reviews/opus_075-live-wire-enum-validation_gate_a_adversarial_review.md.
- Round 2 applied 2026-07-14: Codex P1=0 P2=1 P3=1; Opus post-judging P1=0 P2=2 P3=7; rewrite addresses O2-1 (FR-009 witness was non-discriminating — TradeCondition(277) declares 'A'; replaced with MatchType(574)=A + a full re-audit of every asserted dictionary literal), C2-1 (SC-011 exact exception set), and the RC#4 release-note sweep (6 sites incl. FR-022). Reviews: research/reviews/codex_075-live-wire-enum-validation_gate_a_2_review.md, research/reviews/opus_075-live-wire-enum-validation_gate_a_2_adversarial_review.md.
- Round 3 fixer applied 2026-07-14 (Codex, workspace-write): Codex P1=0 P2=1 P3=0; Opus post-judging P1=0 P2=1 P3=1. Closes C3-1 (enum-domain projection switched from message_fields() expansion to the dictionary enum store, so the 35 message-unreachable enum-backed tags — 10/11/14 in FIX50/SP1/SP2, incl. MsgType(35), EncryptMethod(98), MsgDirection(385), ApplVerID(1128), SessionStatus(1409) — are no longer a silent-accept hole through validate_field(); multi_value=false for store-only tags, measured-safe and now census-pinned) and O3-1 (FR-015 header-in-scope wording). Reviews: research/reviews/codex_075-live-wire-enum-validation_gate_a_3_review.md, research/reviews/opus_075-live-wire-enum-validation_gate_a_3_adversarial_review.md.
- Round 4 applied 2026-07-14 (fresh loop instance, post-Codex-fixer): Codex P1=0 P2=1 P3=1; Opus post-judging P1=0 P2=2 P3=2. Closes O4-1 (the FR-020 validate_field witness was double-booked as FR-018 golden corpus row 12 with asserted:true, but QuickFIX has no context-free validate_field and the two engines' check ordering differs — fixpp field_valid_for→reason 2 before enum_valid, QuickFIX checkValue→reason 5 before checkIsInMessage — so the row would have gone spuriously red; row dropped from the corpus, witness retained as the FR-020 unit test, check-ordering divergence recorded), C4-1 (census corrected to 35 total / 10-11-14 per FIX50-SP1-SP2 incl. MsgDirection(385); store-only type list gains CHAR), C4-2 (empty <header/> blocks, not absent ones; the header_node_ null-guard is not the mechanism), O4-2 (reference-engines fragments). Reviews: research/reviews/codex_075-live-wire-enum-validation_gate_a_4_review.md, research/reviews/opus_075-live-wire-enum-validation_gate_a_4_adversarial_review.md.

### Round 3 — census disagreement, RESOLVED at round 4

- **Settled: the count is 35 — 10 / 11 / 14 on FIX50 / FIX50SP1 / FIX50SP2** (zero on FIX40–FIX44 and FIXT11). The round-3 *review's* narrower **9 / 10 / 13** (aggregate 34) — briefly preserved above as "the adjudicated phrasing" — was **wrong**: it omitted **`MsgDirection(385)`** (`CHAR`, values `R`/`S`; `FIX50SP2.xml:15736-15739`), whose only reference sits inside component **`MsgTypeGrp`** (`FIX50SP2.xml:9530-9536`), which **no** FIX50/SP1/SP2 message references — it belongs to **FIXT11**'s `Logon`. A field referenced inside a `<component>` body is **not** reachable unless a message expansion reaches that component; the round-3 reviewer's predicate treated it as reachable. Three independent measurements (round-3 fixer, round-4 Codex, round-4 Opus adversarial — the last of which authored the 9/10/13 and conceded it) now agree on **35**, and a fourth (this rewrite, `xml.etree` reachability pass over the shipped `dictionaries/*.xml`) reproduces it exactly. The bookkeeping line above and every dependent site (`research.md` O-1, `data-model.md`) now carry **35 / 10-11-14** and the corrected store-only type list **`STRING` / `BOOLEAN` / `INT` / `CHAR`**. **The `multi_value = false` pin still HOLDS** — `MsgDirection(385)` is plain `CHAR`, not `MULTIPLECHARVALUE`; none of the 35 is a `MULTIPLE*` type — so SC-011's new leg is unchanged and remains a real, green-today gate. This is **no longer an open dispute**; it is retained only as the explanation of how the wrong figure entered the bundle. *(Gate A round 4, finding C4-1.)*

### Round 2 — the two root causes that RECURRED, and what closes them

Both P2s were **round-1 root causes re-entering inside text the round-1 rewrite itself wrote** — which is the finding, not the two literals.

- **RC#3 (*the census reads shapes, not what the codes SAY*) → O2-1.** The rewrite diagnosed RC#3, added the code-**string** census (SC-011 / R-11) — and then authored FR-018 corpus **row 7** (`277=A`, *"where only `AX` is declared"*) **without measuring tag 277**. `TradeCondition(277)` **declares `A`**; it does **not** declare `AX`. Both engines accept ⇒ the row was `asserted: true` on a verdict that never happens ⇒ **an accept-accept row that passes green forever and that no mutation can redden** (every mutation the bundle specifies flips *reject* rows to accept). FR-009 had **zero** witnesses. **Closed by**: re-basing row 7 on **`MatchType(574)=A`** (FIX44 codeset `A1`–`A5`, `AQ`, `S1`–`S5`, `M1`, `M2`, `MT`, `M3`–`M6`; **no bare `A`**; `STRING`, single-value, non-header, top-level in `TradeCaptureReport(AE)`) **and** by a **full re-audit of every asserted dictionary literal in the bundle against the shipped XML** — recorded as an audit table under FR-018. **Generalized rule, now normative — and stated NARROWLY on purpose: a `reject`-asserting row that BOTH engines ACCEPT is a defect, not a row.** *(The broad form — "expected verdict equals the stub's behaviour" — is **false**: it would condemn the legitimate **accept**-asserting rows 1 and 3, which are the paired positive controls against **over**-rejection and which the FR-018 audit table shows to be discriminating.)* SC-009's mutation cell (O2-2) carries the per-row obligation that enforces it.
- **RC#4 (*undischargeable artifacts re-enter through prose*) → C2-2.** FR-010 killed the "release notes" artifact (there is no `CHANGELOG.md`; **the B-row IS it**) — and **FR-022, written in the same pass one FR later, re-invented it**. Closed by a **grep-driven sweep of all six sites**, not a patch: `spec.md:192` (FR-022), `:340` (Clarification Q2), `:368` (Clarification Q5); `contracts/enum-domain.md:30`; `plan.md` Risk 3. (`plan.md`'s file-inventory line and `quickstart.md`'s DoD line were already correct and are left as the canonical statement.)

### Round 2 — disagreements

**None.** All nine findings (C2-1, C2-2, O2-1..O2-7) are applied. One **wording** departure, flagged rather than silently taken: O2-7's counter-proposal suggested narrowing SC-002 with the phrase *"the FIX Latest dictionary's counts are 074's and are asserted by **074's existing census**"*. There is **no** exact-count census in 074 — its only codeset test is `tests/dictionary/orchestra_loader_test.cpp`'s `OrchestraCodesets.PreservesValuesAndDescriptions`, an `AdvSide(4)` spot-check. Writing the suggested sentence would have introduced a **fresh false claim** of exactly the class this round exists to remove. SC-002 is therefore narrowed as O2-7 asks (exact-count leg = the **nine** XmlLoader dictionaries; exposure leg = all ten), the Context census table is explicitly scoped to the nine, and the tenth's actual pin is named accurately.

### Round 1 — how the parity leg was closed IN-DOC

The Opus review's closing recommendation held that the parity leg *"cannot be converged in place"* and wanted the golden **built first** (Phase 0.5) before the requirements were written. **The golden being a blocking first deliverable is adopted verbatim** (see § Phase ordering) — but building it is an **`/implement` artifact**, and Gate A reviews the **design**. The leg is closable in-doc because the two blind spots the reviewer feared are no longer blind — they are **named**, with closed source censuses:

- **O-1** — QuickFIX does **not** enum-check repeating-group members (closed call-site census: `isFieldValue` ← `checkValue` ← only `DataDictionary.cpp:172` inside `iterate`; `iterate` never descends into `m_groups`). fixpp's Step-1 flat walk over the raw frame bytes **does**, at every depth (independently re-verified this round against `parser.hpp:229-233` / `:444` — `MessageView::begin()` is a **dict-free byte scan with no group awareness**, which is *why* it reaches members). ⇒ **FR-023**, register row **DV-3**.
- **C-1** — the empty-value disposition is **type-arm dependent** (`Char` → reject/5 via `check_field_type`; `String` → accept), so a single empty-value golden row would pass on Char and fail on String. ⇒ **FR-008** split into **DV-1 / DV-2**, both `asserted: false`.

What made the old structure unconvergeable was that it **asserted** parity and then designed a corpus to confirm it. The rewrite inverts that: the golden's **measured output defines** the divergence register (**FR-018**'s per-row `asserted: true|false` discriminator; **SC-009** rewritten to assert only `asserted: true` rows). A divergence the golden reveals that is not an argued register row is a **defect**. That structure is *robust to the golden finding something we have not predicted* — which is precisely the property the reviewer was asking for, and it does not require the artifact to exist before the design is reviewable.

### Round 1 — disagreements

**None on substance.** All six Codex findings (C-1..C-6) and all nine Opus findings (O-1..O-9) are applied. The single **procedural** departure is the one argued above: the golden is made a **blocking Phase-0.5 deliverable with a derived divergence register**, rather than the bundle being sent back through `/clarify` → `/specify` → `/plan` before Gate A can pass. If round 2 judges that the parity FR/SC text still *asserts* rather than *derives* anywhere, that is a fair re-open — the test to apply is: **can any parity sentence in this bundle be falsified only by the golden, and is every deliberate difference carried as an argued register row?**
