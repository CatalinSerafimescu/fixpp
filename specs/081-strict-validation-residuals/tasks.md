# Tasks: Strict-Validation-Path Residual Closeout

**Input**: Design documents from `specs/081-strict-validation-residuals/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/{validation-acceptance,census-and-parity}.md, quickstart.md
**Branch**: `081-strict-validation-residuals` | **Repo root** = library submodule (`research/G19-fix-fpml-iso20022/library/`)

**Tests**: INCLUDED. The spec/quickstart mandate TDD RED→GREEN behavior pins + a non-circular census + a quickfix-cpp parity golden (the 079 tooling, updated). Write each RED test first and confirm it FAILS for the stated reason before the implementing task.

**Organization**: two independent user stories — **US1 = Concern A** (#203 / L-041-2, FIXT header/trailer acceptance, P1) and **US2 = Concern B** (#205, QuickFIX group-gating parity, P2). They touch different regions (US1 = validator view + framing surface; US2 = loaders + emitter + oracle) and can proceed in parallel; the only shared files (`dictionary.cpp`, `table_view.hpp`) are touched in disjoint regions.

## Format: `[ID] [P?] [Story?] Description with file path`

- **[P]** = parallelizable (different file, no incomplete dependency).
- Paths are repo-root-relative (submodule root).

---

## N3 — Divergent-context enumeration & tier-coverage split (research.md D-7 N3)

> **Best-effort LEGACY enumeration — pending oracle reconciliation (T021), NOT "the authoritative 24".** These 23 contexts are computed by an independent raw-XML walk of the QuickFIX-format dicts (FIX44/50/50SP1/50SP2): a context is a `(dict, message, group)` where a `required='Y'` **direct** member sits inside an **optional** group (`required='N'`) — exactly where fixpp's 079 "required-once-present" store over-requires and QuickFIX group-gating drops it. The authoritative divergent set (the "24", 24→0 at SC-002) is an **output** of the reworked census oracle vs the loaded store and does not exist until /implement; T021 reconciles this table against it. The ~1-context delta (23 here vs 24 measured) is expected: this walk cannot parse the Orchestra/`vlatest` schema (its divergent contexts are typed-tier-covered and enumerated by the oracle at /implement, D-2/D-5), and the 24 was an empirical store-vs-QuickFIX count across **all** dicts including vlatest.
>
> **The load-bearing column is `tier`; the load-bearing row is FIX50.** Typed-tier contexts (v44, v50sp2, and vlatest-by-oracle) get a two-tier agreement pin (T013) + regenerated typed-validator golden (T019). The sole **runtime-only-by-scope** context — **FIX50 `ListStrikePrice / NoUnderlyings / Price`** (FIX50 has no typed builder tier per L-077-1) — has no typed tier to compare and is pinned by the census (T015) + parity golden (T020) **alone**. FIX50SP1 contributes 0 in this legacy walk; T021 confirms against the oracle.

| Dict | Message | Group | `required='Y'` member(s) over-required | Tier |
|---|---|---|---|---|
| FIX44 | ListStrikePrice | NoUnderlyings | Price | typed |
| FIX44 | PositionReport | NoUnderlyings | UnderlyingSettlPrice, UnderlyingSettlPriceType | typed |
| **FIX50** | **ListStrikePrice** | **NoUnderlyings** | **Price** | **RUNTIME-ONLY** |
| FIX50SP2 | Confirmation | NoCapacities | OrderCapacity | typed |
| FIX50SP2 | CrossOrderCancelReplaceRequest | NoSides | ClOrdID, Side | typed |
| FIX50SP2 | CrossOrderCancelRequest | NoSides | ClOrdID, Side | typed |
| FIX50SP2 | Email | NoLinesOfText | Text | typed |
| FIX50SP2 | ListStatus | NoOrders | AvgPx, CumQty, CxlQty, LeavesQty, OrdStatus | typed |
| FIX50SP2 | MarketDataIncrementalRefresh | NoMDEntries | MDUpdateAction | typed |
| FIX50SP2 | MarketDataRequest | NoMDEntryTypes | MDEntryType | typed |
| FIX50SP2 | MarketDataSnapshotFullRefresh | NoMDEntries | MDEntryType | typed |
| FIX50SP2 | MarketDataStatisticsReport | NoMDStatistics | MDStatisticIntervalType, MDStatisticScope, MDStatisticType | typed |
| FIX50SP2 | MarketDataStatisticsRequest | NoMDStatistics | MDStatisticIntervalType, MDStatisticScope, MDStatisticType | typed |
| FIX50SP2 | MassQuote | NoQuoteEntries | QuoteEntryID | typed |
| FIX50SP2 | MassQuote | NoQuoteSets | QuoteSetID, TotNoQuoteEntries | typed |
| FIX50SP2 | NetworkCounterpartySystemStatusResponse | NoCompIDs | RefCompID, StatusValue | typed |
| FIX50SP2 | NewOrderCross | NoSides | ClOrdID, Side | typed |
| FIX50SP2 | NewOrderList | NoOrders | ClOrdID, ListSeqNo, Side | typed |
| FIX50SP2 | News | NoLinesOfText | Text | typed |
| FIX50SP2 | TradeCaptureReport | NoSides | Side | typed |
| FIX50SP2 | TradeCaptureReportAck | NoSides | Side | typed |
| FIX50SP2 | TradingSessionList | NoTradingSessions | TradSesStatus, TradingSessionID | typed |
| FIX50SP2 | TradingSessionListUpdateReport | NoTradingSessions | TradSesStatus, TradingSessionID | typed |

_Coverage split: 22 typed-tier contexts (FIX44 ×2, FIX50SP2 ×20) + vlatest contexts (oracle-enumerated) → two-tier pin + typed golden; **1 runtime-only context (FIX50 ListStrikePrice/NoUnderlyings/Price)** → census + parity golden only._

> **T021 — /implement reconciliation (2026-07-19, AUTHORITATIVE).** The reworked census oracle (T012) vs the loaded store (T015) confirmed **0 divergences both directions on the optional-group axis across all in-scope dicts including vlatest** — the census IS the authoritative enumeration; the 23-row legacy table above is exactly the raw-XML-enumerable subset (legacy dicts), and it is complete for those dicts. **vlatest contexts** are census-enumerated, not hand-listed: the typed-golden regen (T019) forked exactly **one** genuinely mixed-usage vlatest group (`NoLinesOfText`(33), Email/News required vs MarketDataAck optional) into `G_33_1/G_33_2`; every other vlatest divergent context is optional-only and was corrected in-place in `vlatest/validators/traits.hpp` (no fork). v44/v50sp2 divergent contexts are likewise all optional-only (traits.hpp-only, Δ0 plans) — so **every typed-tier divergent context is two-tier-pinned** (T013) and **golden-regenerated** (T019), and the sole **runtime-only-by-scope** context (FIX50 `ListStrikePrice/NoUnderlyings/Price`, no typed builder tier per L-077-1) is covered by the census (T015) + parity golden (T020) alone — **none silently dropped**. A stale 079-era typed-tier pin for the FIX44 `PositionReport/NoUnderlyings` context (row 2) was found in `validator_type_check_test.cpp:898` during T022's full sweep and flipped ACCEPT (see T022). **Separate axis (NOT in this optional-group N3 set):** the 3 `MassQuote/NoQuoteSets/295` parity residuals (T020) are a component-nested-group-in-a-**required**-group divergence D-3 never modeled — waived as L-081-1, pinned by the parity carve-out; they are orthogonal to this table and correctly absent from it.

---

## Phase 1: Setup & Prerequisites

**Purpose**: parity-golden prerequisite (N2) and build preset. No source change.

- [X] T001 [P] **(N2 prereq)** Clone + build `reference-engines/quickfix-cpp` at tag **1.16.0** (gitignored / absent on a fresh tree per [[project_reference_engines_setup]]); it is the source for the T020 parity golden and is **offline / never linked in CI**. Without it the parity oracle silently SKIPS and false-greens (research.md N2). Verify the built `DataDictionary` is loadable by `tools/quickfix_required_golden/main.cpp`.
- [X] T002 [P] Confirm the `clang-debug` preset configures for TDD (sanitizer + gcc-release + MSVC matrix is deferred to `/speckit-verify`); confirm `dictionaries/FIXT11.xml`, `dictionaries/FIX50{,SP1,SP2}.xml` are present as the Concern A source/targets.

**Foundational (Phase 2)**: none. US1 (Concern A) and US2 (Concern B) share no blocking prerequisite — they can start in parallel immediately after Setup.

---

## Phase 3: User Story 1 — Concern A: FIXT header/trailer acceptance (Priority: P1) 🎯 MVP

**Goal**: On the strict path, full-frame `validate()` of a FIX50/FIX50SP1/FIX50SP2 application frame ACCEPTS the FIXT.1.1-owned standard header (8/9/34/49/52/56 + full session-header set) and trailer (10) instead of rejecting on tag 8 — accept-only, no new required-presence enforcement, no false-accept of a malformed numeric header field. Validator-private framing surface only; shared `valid_` store byte-identical (zero golden change, no parser behavior change).

**Independent Test**: Load only `dictionaries/FIX50SP2.xml`, enable `validate_inbound_messages`, feed a well-formed TradeCaptureReport carrying 8/9/34/49/52/56/10 → **accepted** (was rejected `wire_unexpected_tag`, `ref_tag==8`).

### Tests for US1 (write FIRST, confirm RED) ⚠️

- [X] T003 [P] [US1] **RED — standalone accept (SC-001)**: new `tests/wire/fixt_header_validate_test.cpp` — load `FIX50SP2.xml` only, strict on, feed a well-formed app frame (TradeCaptureReport / NewOrderSingle) with standard header+trailer. Assert **RED** = reject `wire_unexpected_tag` with `ref_tag == 8` (cf. mechanism `tests/wire/validator_production_table_view_test.cpp:270`); target GREEN = accept. Repeat for `FIX50.xml` and `FIX50SP1.xml` (SC-001 parity). Isolation-safe → existing grouped `wire` bucket.
- [X] T004 [P] [US1] **RED — accept-only guard + F2 no-false-accept (FR-003a/FR-011)**: in `tests/wire/fixt_header_validate_test.cpp` — (a) frame omitting a genuinely-required **application** field still **rejects**; (b) frame omitting a session-owned header field (e.g. 52) is **NOT** rejected by `validate()`; (c) **malformed numeric header** `34=abc` and (separately) `1156=abc` → **RED** (accepted before `fixt_framing_types_` — String default, the F2 false-accept) → **GREEN** rejected `wire_field_value_out_of_range` with **exact** `ref_tag == 34` / `ref_tag == 1156` (not a generic "rejects"); (d) `52=notatime` stays **accepted** — documented limitation (UtcTimestamp→String, structurally undetectable), NOT a reject pin.
- [X] T005 [P] [US1] **RED — FIXT framing census (D-2)**: new standalone `tests/dictionary/fixt_header_merge_test.cpp` (own `add_test`, exact-set gate) — assert the baked FIXT framing `tag→field_type` table == `dictionaries/FIXT11.xml` `<header>`+`<trailer>` field tags **AND** their datatypes (through the canonical `field_data_type → field_type` reduction), **exact-set both directions**. Must include 8/9/10 (they reach Step-1) AND the flat-recursed nested-`<header>` `NoHops` hop tags (627→Int, 628→String, 629→String, 630→Int; data-model.md E-1 disposition — census recurses one level into `<header>`/`<trailer>` groups, avoids the SC-003 routed-FIXT residual false-reject). RED before the constant exists.
- [X] T006 [P] [US1] **RED — parser-containment pin, asserted DIRECTLY (RC#1/FR-009)**: in `tests/dictionary/fixt_header_merge_test.cpp` — on the `as_table_view()` output for FIX50/FIX50SP1/FIX50SP2, assert `table_view::field_valid_for(msg_type, T)` **and** `valid_tags_for(msg_type).contains(T)` stay **false** for each framing tag `T ∈ {8,9,10,34,49,52,56,1128,1156,…}` not genuinely message-declared, WHILE `dictionary_driven_validator::validate()` **accepts** those tags via `is_fixt_framing_tag`. A `valid_` re-widening flips `field_valid_for`→true → RED. Do **NOT** use a blind `unknown_fields()` strict-on-vs-off compare (near-vacuous — `inbound_tv_` is flag-independent at `session.cpp:992`).

### Implementation for US1

- [X] T007 [US1] Add the baked **FIXT.1.1 standard framing `tag → field_type` constant** in the dictionary layer (static, alongside `kVersionTable`/`kFieldTypeTable` precedent) — full `<header>`+`<trailer>` set with each tag's real datatype (34=SEQNUM→Int, 1156=INT→Int, 52=UTCTIMESTAMP→String, plus the flat nested-`NoHops` hop tags 627→Int/628→String/629→String/630→Int, …), single source `dictionaries/FIXT11.xml` (E-1). Makes T005 GREEN.
- [X] T008 [US1] Add validator-private members + accessors to `include/fixpp/dict/table_view.hpp`: `fixt_framing_tags_` set + `fixt_framing_types_` map + `is_fixt_framing_tag(tag)` and a framing-type lookup accessor (E-2). Leave `valid_`, `field_valid_for` (:259–263), `valid_tags_for` (:273–276), `types_`, `field_type_of` (:398–401) **byte-identical**.
- [X] T009 [US1] Populate the framing surface in `src/dictionary/dictionary.cpp` `as_table_view()` (~:376–382) **only** for versions `v50`/`v50sp1`/`v50sp2` (read `version_` from `dict_metadata_handle` per `kVersionTable`); do NOT widen the shared `valid_` store (D-1). Makes T006 GREEN.
- [X] T010 [US1] Wire the validator in `include/fixpp/wire/validator.hpp`: Step-1 gate (:170) `… && !dict_.is_fixt_framing_tag(fld.tag)` (accept framing tag); type-check arm (`check_field_type`/`field_type_of`, :183/:467) resolves a framing tag from `fixt_framing_types_` **before** `field_type_of` (malformed numeric → Int arm). No `builder_validate.hpp` / hot-path enforcement change; `consume_group` untouched. Makes T003/T004 GREEN.
- [X] T011 [US1] **No-regression pins** (SC-004): FIX40/41/42/43/44 + FIXT.1.1 full-frame `validate()` behavior byte-for-byte unchanged; run the read/reify golden + codegen-determinism tests to confirm **byte-identical** (Concern A changes no golden). `vlatest` untouched (FR-004).

**Checkpoint US1**: FIX50SPx app frames validate under strict validation; malformed numeric header rejected; parser + goldens unchanged.

---

## Phase 4: User Story 2 — Concern B: QuickFIX group-gating parity (Priority: P2)

**Goal**: A `required='Y'` direct member of an **optional** group is no longer required per-instance (gated on the immediate enclosing group's own `required=`, QuickFIX `addXMLGroup`); required-group members still enforced. Runtime store + both loaders + codegen emitter + oracle move in lockstep; the 24 (23-legacy) divergent contexts collapse to 0; two-tier agreement; typed-validator goldens regenerate; read/reify goldens byte-identical.

**Independent Test**: FIX44 PositionReport/AP `NoUnderlyings(711)` omitting 732/733 → **accepted** (was rejected `wire_required_field_missing`); a present-but-incomplete **required**-group instance → still **rejected**.

### Tests for US2 (write FIRST, confirm RED) ⚠️

- [X] T012 [P] [US2] **Rework the census oracle** `tests/dictionary/required_scope_oracle.hpp` (:91–100,:154): replace the 079 `group_scope_and` (required-once-present) with **immediate-enclosing group-gating** — a direct `required='Y'` member is recorded iff its immediate enclosing group's own `required='Y'` (D-3), computed **independently from raw XML** (non-circular; the parity golden corroborates, does not define). This is the shared oracle reused by census + parity.
- [X] T013 [P] [US2] **RED — two-tier optional-flip + required-hold (contract clauses 1–3)**: `tests/wire/required_scope_two_tier_test.cpp` (~:255–299) — optional-group present-but-incomplete instance flips REJECT→**ACCEPT** (both runtime + typed `validate_<Msg>` tiers agree); add/keep a **required**-group present-but-incomplete instance that still **REJECTs** (`wire_required_field_missing`). RED before loader/emitter fixes.
- [X] T014 [P] [US2] **RED — dictionary pin flip**: `tests/dictionary/required_scope_test.cpp` (:38–39,:86–91) — `group_required_members("AP",…,711)` flips **contains 732/733 → NOT-contains** (NoUnderlyings optional); re-check `AR`/NoSides (:185–186).
- [X] T015 [US2] **RED — per-context census exact-set (SC-002)**: `tests/dictionary/required_scope_census_test.cpp` — loaded per-group store == reworked oracle **exact-set both directions**, all in-scope dicts, 24→0; recount shrunk baselines: RC5 max-required-member (was 6, :331) and `total_contexts` (:279). Depends on T012.

### Implementation for US2

- [X] T016 [US2] Thread the **enclosing group's own `required=`** into the member-record gate in `src/dictionary/xml_loader.cpp` (`expand_field_list` :522; member record :557–559; recurse drops `greq` at :658–660) — a direct `required='Y'` member enters the per-group required store **only if its immediate enclosing group is required** (mirrors QuickFIX `addXMLGroup`).
- [X] T017 [US2] **Symmetric fix (same pass)** in `src/dictionary/orchestra_loader.cpp` (`expand_field_list` :509; member record :545–547; `greq` :592; recurse :634–636). Half-restructure risk — both loaders + oracle must move together or census/two-tier diverge ([[feedback_half_restructure_symmetric_api]]).
- [X] T018 [US2] **Fork group-plan identity by enclosing-group-required** in `tools/codegen/fixpp-codegen/emit_builders.cpp` — include the enclosing group's own `required=` in `compute_signature` (:251–282) / intern identity (:648–649) so a mixed-usage structural group forks into a `required` plan (populated `required_checks`, `group_check.required=true`) and an `optional` plan (empty `required_checks`, `false`); `emit_writer_traits_for_level` (:723) then emits per-fork checks (E-4/D-4). **No `builder_validate.hpp` change.**
- [X] T019 [US2] **Regenerate the typed-VALIDATOR goldens** under `specs/078-precompiled-builder-libs/contracts/golden/{v44,v50sp2,vlatest}/` (messages/*.validator.{cpp,inl}, validators/traits.hpp) — only the affected group-gating sites change; measure + record the plan-count fork delta (D-4). Makes T013 typed tier GREEN. **NOTE (flagged for orchestrator review, not silently resolved):** v44/v50sp2 are validator-only as anticipated (0 plan-count delta each — confined to `validators/traits.hpp`). vlatest has a **+1 plan-count delta**: `NoLinesOfText`(33) is used REQUIRED in `Email`/`News` but OPTIONAL in `MarketDataAck`, with `Text`(58) declared `required='Y'` — a genuinely mixed-usage group per D-4's own "bounded dedup impact" note. Since the same shared `writer_traits<T>` cannot carry two conflicting explicit specializations (ODR), this forks `G_33Args` into `G_33_1Args`(required, `required_checks={58}`)/`G_33_2Args`(optional, `required_checks={}`), which ripples a rename into `groups.hpp` + `groups/G_33_1Args.hpp`+`G_33_2Args.hpp` (replacing `G_33Args.hpp`) + `messages/{Email,News,MarketDataAck}.hpp` — outside the literal "validator files only" framing in D-5. Confirmed correct (pre-fix, MarketDataAck's optional NoLinesOfText entries would have wrongly enforced Text(58) required) and semantically necessary (leaving it un-regenerated breaks `codegen_determinism_test`'s `VlatestGeneratedMatchesGolden`/`VlatestBuildersMatchesGolden`), so committed; read/reify goldens (003/076) confirmed byte-identical via `cmp`. `codegen_determinism_test` full suite (21/21) GREEN post-regen.
- [X] T020 [US2] **Regenerate the quickfix-cpp parity golden** (`tools/quickfix_required_golden/`, offline against the T001-built QuickFIX 1.16.0) and assert `tests/wire/required_scope_parity_test.cpp` **exact both directions** at the divergent contexts. Depends on T001 + T012. (Body-only surface per the test header; StandardHeader/Trailer stays census-pinned.) **NOTE (orchestrator/user-approved, 2026-07-19):** exhaustive across 29,301 real body group contexts, 3 (FIX44/FIX50/FIX50SP1 MassQuote/NoQuoteSets(296)/295) are a named, source-verified WAIVED stricter-superset residual — W-204-1 lineage, safe direction (no false-accept), root cause = QuickFIX `DataDictionary.cpp:563` hardcoded `componentRequired=false` for a component nested directly in a group (D-3 never modeled this axis); pinned via `kKnownSupersetContexts` in the test (`oracle == golden ∪ {295}` exactly at those 3), NOT a blanket skip. Loaders/oracle left untouched (fixpp stays spec-faithful). SC-002/N3 doc reconciliation deferred to T021/T024 (orchestrator-owned).
- [X] T021 [US2] **Reconcile the N3 enumeration (this file, top) against the reworked oracle's authoritative divergent-context set** — DONE, see the "T021 — /implement reconciliation" note under the N3 table above. — confirm the census (T015) yields the same `(version, message, group)` set, update the legacy 23-context table to the authoritative set (incl. vlatest/`v50sp1` contexts the raw-XML walk could not enumerate), and re-confirm the **FIX50 `ListStrikePrice/NoUnderlyings/Price`** runtime-only-by-scope context is covered by census + parity **alone** (no typed tier). Records the completeness argument for N3.
- [X] T022 [US2] **Invariant pins (FR-008/FR-009)**: read/reify goldens (v42/v44/v50sp2/vt11/vlatest) **byte-identical** (required-ness independent of read membership); `abidiff` 0-diff + `nm` symbol golden unchanged (no C-ABI change); `validate_inbound_messages=off` byte-identical no-op. **DONE:** zero `capi/`/`error.h`/`version.h` diff; `capi_pure_tests` `AbiSymbolGolden.{CabiSymbolSetUnchanged,ErrorEnumUnchanged}` 2/2 PASS (the `nm`-symbol-golden IS the repo's current abidiff-equivalent gate — libabigail retired 2026-06-22); no `*_Messages.golden.hpp` changed + `codegen_determinism_test` 21/21; `session_validate_gate_default_off` 4/4 incl. `T016_ValidatorNotConstructed_SC005` (structural opt-in-no-op proof); FIX40–44/FIXT11 validate() unchanged (`validator_legacy_char_type_test` 6/6, framing surface gated v50/50sp1/50sp2-only). **Full-sweep miss caught + fixed:** a stale 079-era pin `validator_type_check_test.cpp:898` (`...GroupInstanceMissingRequiredMemberRejected`) still asserted REJECT for the FIX44 PositionReport/NoUnderlyings(711) optional-group context Concern B flips to ACCEPT (US2-runtime flipped its 2 census siblings but missed this one); flipped to ACCEPT + renamed, citing #205/W-204-1 supersession → `wire_pure_tests` GREEN.

**Checkpoint US2**: group-gating == QuickFIX exact (24→0); both tiers agree; typed goldens regenerated; read/reify + ABI unchanged.

---

## Phase 5: Polish & Cross-Cutting Concerns

- [X] T023 Run all `quickstart.md` scenarios (A1–A3, B1–B3) end-to-end on `clang-debug`; confirm the cross-cutting gates list (default-off no-op, abidiff, determinism, `validator_bench` ±5%) before handing to `/speckit-verify`. **DONE:** A1–A3 pinned by `wire_dict_tests`/`fixt_header_merge`; B1–B3 by `required_scope_{two_tier,census,parity}` + `required_scope_test` (in `dictionary_pure_tests`) + `codegen_determinism_test`; full `wire_pure_tests` GREEN post stale-pin fix. `validator_bench` exists but has NO FIX50SPx-accept-path case — the D1 note below stands: /speckit-verify must add `BM_Validate_FixtHeaderAccept` OR record the no-characterization rationale. **(D1 — perf, /verify decision)** Concern A enables a **new** steady-state path (a FIX50SPx frame that previously fast-rejected on tag 8 now runs full `validate()` Step-1/2/3); the existing `validator_bench` cases do not exercise it. At `/speckit-verify` either add a `BM_Validate_FixtHeaderAccept` case to `bench/wire/validator_bench.cpp` **or** record an explicit rationale for why the new accept path needs no characterization (pre-empts the 075-class Gate B P1 per [[feedback_gateb_perf_change_needs_bench_not_a_metpartial_note]]). Not a blocker; existing-path baseline is unchanged (the `is_fixt_framing_tag` term short-circuits when `valid_tags.contains` is already true).
- [X] T024 [P] **Catalogue close-out — adapted to B&L update (NO new OFFICIAL rows; D-6 / Normative References)** — **DONE:** L-041-2 #203 sub-part marked RESOLVED (accept-only named-intent recorded); new B-081-1/B-081-2/L-081-1 rows (Concern A acceptance; Concern B group-gating flip **superseding W-204-1**; 3 MassQuote waived residuals); W-014 amendments in `coverage-index.md` + `feature-catalogue.md` (no new OFFICIAL row); L-063-1/L-066-1 confirmed untouched: this feature introduces **no** new `spec/feature-catalogue.md` rows (both concerns are correctness of existing versions). Instead: in `spec/behaviors-and-limitations.md` — mark **L-041-2 → resolved** (Concern A; explicitly record the accept-only-vs-QuickFIX header-required divergence as **named intent**, so a future interop pass sees recorded intent not a re-filed bug); record the Concern B flip that **supersedes W-204-1** / #205 (fixpp now group-gated == QuickFIX at all divergent contexts) via an L-row; **confirm L-063-1/L-066-1 (FIX40/41/42 INT-group carve-out, #196) untouched**. Touch `spec/coverage-index.md` only for the behavior-record refs (row W-014 clauses), no structural row add.
- [X] T025 **Feature-completeness audit (FINAL — hard `/gate-b` precondition, Article XVII §8 / pre-flight 4d)** — **DONE: verdict 100% COMPLETE**, 2 named user-approved waivers (L-081-1 MassQuote residual; Concern A accept-only named-intent) + 1 `/verify`-deferred perf decision (D1); all FR-001..011 + SC-001..006 map to landed test+impl; no silent drops. Recorded in `.specify/decisions/081-strict-validation-residuals-completeness.md`: assert against the tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale; (ii) every FR-001..011 and SC-001..006 maps to a landed test AND landed implementation; (iii) N3 coverage-split reconciled (T021) — every divergent context is typed-tier-pinned or explicitly runtime-only-by-scope, none silently dropped; (iv) no new OFFICIAL catalogue row was required (D-6) and the B&L updates (T024) landed. Record the verdict (100%-or-fully-waived) in `.specify/decisions/081-strict-validation-residuals-verify.md` `## Completeness` (or a sibling `-completeness.md`).

---

## Dependencies & Execution Order

- **Setup (T001–T002)** → no source dep; T001 gates the parity golden (T020).
- **US1 (T003–T011)** and **US2 (T012–T022)** are independent — parallelizable after Setup. Shared files (`dictionary.cpp`, `table_view.hpp`) touched in disjoint regions.
- **Within US1**: T003–T006 (RED) before T007–T010; T007→T005 GREEN, T008/T009→T006 GREEN, T010→T003/T004 GREEN; T011 last.
- **Within US2**: T012 (oracle) before T015/T020; T013/T014 (RED) before T016–T018; T016+T017 (loaders, same pass) → runtime tier; T018→T019 (typed golden); T020 needs T001+T012; T021 needs T015; T022 last.
- **Polish (T023–T025)** after both stories; T025 is the FINAL task.

## Parallel Opportunities

- T001 ‖ T002 (Setup).
- All four US1 RED tests (T003–T006) ‖; T012/T013/T014 ‖ (US2 RED + oracle).
- Entire US1 phase ‖ entire US2 phase (different developers / different file regions).

## Implementation Strategy

**MVP = US1 (Concern A)** — it is the complete functional break (no FIX50SPx app traffic accepted under strict validation today). Land Setup → US1 → validate independently (standalone accept + malformed-header reject + parser/goldens unchanged) before starting US2. US2 (parity relaxation) is safe-direction and additive; land it, regenerate goldens, reconcile N3, then Polish.
