---
description: "Task list for feature 077-builder-args-dedup (typed builder tier for all FIX versions via structural-plan Args deduplication)"
---

# Tasks: Typed builder tier for all FIX versions via group-Args deduplication

**Input**: Design documents from `specs/077-builder-args-dedup/`
**Prerequisites**: plan.md ✓, spec.md ✓ (US1 = P1; US2/US3 = P2; US4 = P3), research.md ✓ (R1–R6), data-model.md ✓ (Entities 1–6), contracts/generated-builder-dedup.md ✓ (G1–G5), contracts/builder-completeness.md ✓ (C1–C4), quickstart.md ✓ (V1–V6)

**Tests**: REQUIRED (Constitution Article VII — TDD). New tests are authored to FAIL (RED) before the emitter/CMake change that makes them GREEN: the dedup-soundness discriminating witness, the vlatest deep-group round-trip, the dedup struct-count assertion, the per-version round-trips, the frozen-external-corpus byte differential, the raw-XML completeness census, and the committed mutation witness.

**Organization**: Grouped by user story (US1 vlatest MVP = P1; US2 all-version widening = P2; US3 v44 behavior verification = P2; US4 completeness = P3). All paths are relative to the library submodule root (`research/G19-fix-fpml-iso20022/library/`); run every command with cwd inside it.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no incomplete dependency).
- **[Story]**: US1 / US2 / US3 / US4 label — ONLY on user-story-phase tasks (not Setup / Foundational / Polish).
- Every task carries an exact file path.

## Path Conventions

Single-project C++ library + host codegen tool (the library submodule). Emitter source under `tools/codegen/fixpp-codegen/`; CMake codegen driver at `cmake/Codegen.cmake`; generated output into the build tree `build/<preset>/_codegen/include/fixpp/<ns>/Builders.hpp`; tests under `tests/session/` and `tests/codegen/`; frozen external byte corpus under `tests/session/golden/`; goldens under `specs/077-builder-args-dedup/contracts/golden/` (+ the v44 official golden regenerated in place at `specs/069-v44-all-families/contracts/golden/`); catalogue/limitations under `spec/`; the folded amendment in `.specify/constitution.md`.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Capture the pre-077 byte baselines the no-regression gates (FR-008/FR-009/SC-004/SC-005) compare against, and land the folded constitution amendment (amend-then-proceed, Article XX §1) before touching emitter code.

- [ ] T001 [P] Capture the pre-077 **legacy read-tier byte baseline**: build `fixpp-codegen` and copy every generated legacy read artifact (`Messages.hpp` / `Fields.hpp` / `Validator.hpp` / `Reify.hpp` for `v42`/`v44`/`v50sp2`/`vt11`) from `build/linux-clang-debug/_codegen/include/fixpp/<ns>/` into a scratch baseline dir for the FR-009/SC-005 recursive byte-diff (T022). Also record the 067/069 GREEN baseline (`ctest --preset linux-clang-debug -L "067|069" --output-on-failure`). No code edits.
- [ ] T002 [P] Confirm the **frozen pre-077 external byte corpus** used by the FR-008/SC-004 differential is present and unchanged: the QuickFIX-authored `.fix` goldens under `tests/session/golden/*.fix` (incl. `069_*.fix` family exemplars) + the 061 hand exemplars in `tests/session/exemplar_seeds.hpp`. These are fixtures NOT regenerated from the emitter, so they survive the nested-Args rename (T011) — this task only records the inventory + provenance the differential (T020) anchors on. No edits.
- [ ] T003 Land the folded **constitution amendment** into `.specify/constitution.md`: (a) **Article I §1** — remove the "typed `build_<Msg>` builder codegen … remain post-1.0" carve-out **for FIX Latest** (077 delivers the component-identity Args-dedup redesign the 076 close-out named); ApplExtID(1156)=303 + session negotiation stay post-1.0. (b) **Article XVIII §7** — reclassify v42/v50sp2 app-message builder widening from "v1.x-deferred" to "v1.0-delivered-by-077". (c) **Article XVIII §2** — narrow the "only their typed `build_<Msg>` builders … remain post-1.0" annotation to ApplExtID/EP-back-port only. Bump `v0.8 → v0.9`, prepend the Sync Impact Report line. **No copy-ready payload exists in plan.md** — draft the replacement text from the plan.md `## Constitution Check` table (Gate A converged 3 rounds, 2026-07-16; reviews under parent `research/reviews/*077*`); this lands the reviewed intent (amend-then-proceed).

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The single version-agnostic dedup mechanism (FR-005) that ALL stories consume, plus the minimum needed to keep the tree compilable. **⚠️ CRITICAL — tree-green ordering:** the BREAK decision (plan.md §Decision) means v44 flows through the deduped path with `groups::G_…Args` names the instant the mechanism lands (message-rooted naming removed at `emit_builders.cpp:415-416`). Because 068 grouped tests into whole-binary executables, one un-rewritten `test_067_*`/`test_069_*` source kills the entire `tests/session` binary and blocks the US1 MVP. So the v44 test rewrite (T011) and both v44 golden regens (T010) live HERE, immediately after the mechanism — not in US3. US3 then owns only the behavior *verification* it uniquely needs.

- [ ] T004 [P] Confirm the IR already carries every **structural-signature input** the dedup key needs, in `tools/codegen/fixpp-codegen/ir.hpp` / `ir.cpp` (data-model Entity 1): per `GroupOrderEntry` — `no_tag`, `delimiter_tag`, ordered `members` in **declaration order** (`(tag, is_group)`), and per-member required-ness reachable via `MessageIR.fields[tag].ref.rule`; child groups recursively. plan.md marks `ir.hpp`/`ir.cpp` **no-change** — this task VERIFIES that (source-read confirm), and if any input is missing, flags it as a scope change before T008. No edits expected.
- [ ] T005 [P] **v44 count reconciliation (spec-assigned `/tasks` deliverable, research.md R2 scope note + Assumptions)**: with an independent raw-`FIX44.xml` walk (NOT the emitter's `ir(V).messages`), reconcile the app-only builder group count (census 58 distinct `no_tag`s) vs the read-tier 59 (all-messages incl. 8 admin), and the app-message count (83 = `is_application` 85 − `{BE,BF}`) vs vt11-split versions. Pin the per-version **expected counts** the goldens + completeness census (T010/T013/T023) assert against: app msgs 39/83/156/173 and distinct `(no_tag, signature)` plans 29/89/558/578 (v42/v44/v50sp2/vlatest). Record in `research.md` (append a reconciliation note) — this closes the "not yet verified against source" caveat before any golden expectation is frozen.
- [ ] T006 [P] **FR-009 scope decision (name the choice; SC-005 is graded on it)**: decide and record whether the read-tier no-regression gate is the **full** 4-artifact recursive byte-diff (`Messages`/`Fields`/`Validator`/`Reify` × 4 versions vs the T001 baseline) OR **narrowed** to the goldened `Messages` artifact with the ungoldened `Fields`/`Validator`/`Reify` residual recorded explicitly (per FR-009). Record the decision + rationale in `.specify/decisions/077-builder-args-dedup-verify.md` (create the skeleton); T022 implements whichever is chosen.
- [ ] T007 [P] **(TDD — RED first)** Add the **dedup-soundness discriminating witness** to `tests/codegen/test_067_emit_builders_unit.cpp`: synthetic IR where (a) two occurrences of one `no_tag` carry **different** ordered-member/required-ness signatures → emitter yields TWO ordinaled `G_<no_tag>_1Args`/`G_<no_tag>_2Args` (no bare name, G1a); (b) two occurrences with **byte-identical** recursive signatures → ONE bare `G_<no_tag>Args` (collapse); (c) two **different** `no_tag`s with coincidentally-identical bodies stay **separate** plans. Mutation-grade (a wrong `no_tag`-only key would fail (a)/(c)). Must FAIL before T008. Inherits this TU's existing grouping/label.
- [ ] T008 **[core mechanism]** In `tools/codegen/fixpp-codegen/emit_builders.cpp`: replace the message-rooted `type_prefix`/`child_prefix` naming (`resolve_level`, lines ~367–421) with **structural-plan deduplication** keyed by `(no_tag, recursive structural signature)` (data-model Entity 1; research.md R3): compute the recursive signature (delimiter + ordered `(tag, required, {child-signature}?)`), intern distinct plans in a per-version map, and emit each plan **exactly once** into `namespace fixpp::<ns>::groups` as `G_<no_tag>Args` (one signature) or ordinaled `G_<no_tag>_1..kArgs` (≥2 signatures, NO bare name — G1a), ordinal by first-encounter over the bytewise-sorted `ir.messages` × declaration-order `group_order`. Per-message bodies reference the shared plan by qualified name (FR-002, G2). Emit `writer_traits<groups::G_…Args>` + `_required_`/`_count_`/`_validate_entry_` helpers **once per plan**, post-order (children before parents), no ODR conflict (FR-003, G3). Preserve the cycle/over-deep bound (`kMaxGroupDepth`-style) — never reference an undefined Args type (FR-011, G1c). Makes T007 GREEN. (FR-001)
- [ ] T009 Remove the `if (ir.ns != "v44") return {}` builder gate (`tools/codegen/fixpp-codegen/emit_builders.cpp:658`) so the deduped path is the **single, version-agnostic** emitter for every application-bearing version (FR-005). Per-version scope (which app set each version emits) is applied in US1/US2 via `is_application` + per-version in-scope predicate (data-model Entity 4), NOT by re-introducing a namespace gate.
- [ ] T010 Regenerate + create the **v44 builder goldens** to the deduped output (FR-007/FR-007a): regenerate `official` (33) **in place** at `specs/069-v44-all-families/contracts/golden/v44_Builders_official.golden.hpp`, and **newly create** `all` (83) at `specs/077-builder-args-dedup/contracts/golden/v44_Builders_all.golden.hpp` (no prior `all` golden exists). Retain the `FIXPP_CODEGEN_V44_FAMILIES` knob (both modes flow through the deduped path). Struct count must equal the T005-pinned 89 distinct plans.
- [ ] T011 **Rewrite the v44 builder-test nested-Args references → `groups::G_…Args`** across the 7 builder test sources (measured `/tasks` surface — see re-count below): `tests/session/test_067_builder_roundtrip.cpp`, `test_067_builder_validate.cpp`, `test_067_builder_failclosed.cpp`, `test_067_builder_shape_oracle.cpp`, `support/test_067_seeds.hpp`, and the 069 `test_069_all_families_roundtrip.cpp` / `test_069_family_exemplar_golden.cpp`. **Names are derived from the T010-regenerated golden, NOT hand-mapped** — the bare-vs-ordinaled form (`G_555Args` vs `G_555_1Args`) depends on the per-version signature census, only knowable after regen. Top-level `<Msg>Args` names are UNCHANGED (per-message, not deduped); only the ~19 nested-group Args names change. NO `using`-alias shim (BREAK, user-decided 2026-07-16). Keeps `tests/session` compilable at this checkpoint (068 whole-binary grouping). (FR-008 source-API break, SC-004)

> **`/tasks` re-count of the v44 rename surface (plan.md mandated — supersedes the unverified "46 refs / 7 files"):** the renamed surface is the **nested-group** Args only (top-level `<Msg>Args` stay). Measured: **~19 distinct nested-group Args type-names, ~130 occurrences** across the 7 files above — e.g. `NewOrderListOrdersArgs` (16), `TradeCaptureReportSidesArgs` (15), `MarketDataSnapshot/IncrementalRefreshMDEntriesArgs` (13+13), `NewOrderListOrdersPartyIDs[PartySubIDs]Args` (12+9), `AllocationReportPartyIDs[PartySubIDs]Args` (9+8), `TradeCaptureReportSidesPartyIDsArgs`/`TradeCaptureReportLegsArgs`/`SecurityListRelatedSymArgs`/`RegistrationInstructionsRegistDtlsArgs`/`PositionReportPositionsArgs`/`MassQuoteQuoteSets[QuoteEntries]Args`/`CollateralInquiryCollInquiryQualifierArgs` (4 each), `ListStatusOrdersArgs`/`ConfirmationCapacitiesArgs` (3 each), `QuotePartyIDsArgs` (1). The true surface is **larger** than the "46 refs / 7 files" estimate — size the rewrite from the measured ~130.

**Checkpoint**: The deduped emitter is the sole builder path; v44 goldens regenerated; `tests/session` compiles against `groups::G_…Args`. v44 *behavior* verification (byte differential, determinism) is US3.

---

## Phase 3: User Story 1 - Compilable typed vlatest builders (Priority: P1) 🎯 MVP

**Goal**: Re-enable the FIX Latest (`vlatest`) `build_<Msg>`/`validate_<Msg>` tier descoped by 076 (L-076-1) — deduped to the read-tier order so it compiles as one TU in low-single-digit-GB RSS.

**Independent Test**: With `FIXPP_CODEGEN_FIX_LATEST=ON`, generate `vlatest/Builders.hpp`, compile a TU that includes it and calls `build_<Msg>` for a deep-group message (Instrument + Legs + Underlyings), read the frame back field-for-field, assert equality; confirm the header compiles in low single-digit GB (not >21 GB).

- [ ] T012 [US1] **(TDD — RED first)** Add a **vlatest deep-group round-trip** smoke in `tests/session/` (new `test_077_vlatest_builder_roundtrip.cpp`, labelled `builder-roundtrip`): populate a `vlatest` `<Msg>Args` for a message carrying Instrument + Legs + Underlyings, `build_<Msg>` → read-back → assert field-for-field equality with zero skips. This is the US1 MVP independent-test smoke (one deep-group message); the **exhaustive** 100%-of-messages SC-003 coverage for vlatest lands in T016. RED before T014 (no vlatest builders yet). (SC-003 partial, quickstart V2)
- [ ] T013 [US1] **(TDD — RED first)** Add a **dedup struct-count assertion** in `tests/codegen/` (new `test_077_builder_dedup_count.cpp`): `grep`/parse `vlatest/Builders.hpp` for `struct G_` count ≈ **578** (the T005-pinned plan count; read-tier order, NOT ~26k), and file size ~10 MB order (SC-002). RED before T014.
- [ ] T014 [US1] Wire `vlatest` through the deduped emitter, gated by `FIXPP_CODEGEN_FIX_LATEST` (data-model Entity 4): emit its full `is_application` set (173) when ON. In `cmake/Codegen.cmake`, **remove 076's unconditional `vlatest/Builders.hpp` deletion** (the `if(NOT FIXPP_CODEGEN_FIX_LATEST) file(REMOVE_RECURSE …/vlatest) else() file(REMOVE …/vlatest/Builders.hpp) endif()` block at lines ~330–342 — NOT the regen-guard cache-tracking at ~264–298) and replace it with a **conditional OFF-clean**: when the option is OFF, the configure-time regen-guard deletes any previously-generated `vlatest/Builders.hpp` (no stale file); when ON, it emits and participates in the determinism/regen-guard discipline (FR-012). Makes T012/T013 GREEN. (FR-004, G4a)
- [ ] T015 [US1] Add the **compile-resource bench** (003 T046 decision-record convention — NOT an Article VIII mechanism), labelled `compile-budget`: a target that `clang++ -fsyntax-only` includes `vlatest/Builders.hpp` and captures peak RSS + wall time, asserting peak RSS in low single-digit GB (was >21 GB / OOM). Record the measured RSS/wall + the expected v50sp2/vlatest **KNOWN_OVERAGE** against the ≤3 s single-version syntax-only ceiling in `.specify/decisions/077-builder-args-dedup-verify.md`. (SC-001/SC-002, quickstart V1)

**Checkpoint**: vlatest builders emit, dedup to ~578 plans, compile within budget, and round-trip — the MVP is usable.

---

## Phase 4: User Story 2 - Typed builders for all application-bearing versions (Priority: P2)

**Goal**: Emit `build_<Msg>`/`validate_<Msg>` for v42 and v50sp2 (v44/vlatest already delivered), each version's genuine `is_application` set; vt11 (admin-only) emits none.

**Independent Test**: For v42 and v50sp2, generate the tier, compile a TU including that version's `Builders.hpp`, round-trip a representative app message (≥1 repeating group).

- [ ] T016 [US2] **(TDD — RED first) — SC-003 100% coverage.** Add an **exhaustive differential round-trip** in `tests/session/` (new `test_077_allversions_builder_roundtrip.cpp`, label `builder-roundtrip`) covering the **FULL in-scope application set of v42 (39), v50sp2 (156), AND vlatest (173)** — NOT one representative per version (v44's exhaustive coverage is T020's frozen-corpus differential). Mirror 069's `test_069_all_families_roundtrip.cpp` pattern: for every in-scope message, seed its `<Msg>Args`, `build_<Msg>` → read the frame back via the runtime `as_table_view` path → assert field-for-field equality with **zero skips** (set-equality over the in-scope set). This is what SC-003 ("100% ... zero skips") literally requires; the completeness census (T023) proves only entry-point existence and determinism (T021) only byte-stability — neither proves per-message round-trip correctness. RED before T017. (SC-003)
- [ ] T017 [US2] Emit builders for **v42 and v50sp2** through the deduped path — full `is_application` set (39 / 156), NO v44 `{BE,BF,BW,BX,BY}` exclusion inherited (BW/BX/BY are genuine FIX 5.0 SP2 application messages) (FR-006, R4, data-model Entity 4). Create their goldens `specs/077-builder-args-dedup/contracts/golden/v42_Builders.golden.hpp` and `v50sp2_Builders.golden.hpp`; plus the `vlatest` golden `vlatest_Builders.golden.hpp`. Struct counts match T005 pins (29/558/578). Makes T016 GREEN.
- [ ] T018 [US2] Assert **vt11 emits no builders** in `tests/codegen/` (extend T013's TU or a sibling): vt11 (FIXT, admin-only, 0 application messages) produces NO `vt11/Builders.hpp` (empty-skip, `write_file` empty), and this is not an error (FR-006, edge case, G4a).
- [ ] T019 [US2] Add the **OFF-path stale-file** check (quickstart V3 / FR-012) as a ctest: after a prior ON build, reconfigure `-DFIXPP_CODEGEN_FIX_LATEST=OFF` and assert `vlatest/Builders.hpp` is absent (the conditional OFF-clean from T014 removed it) while other versions' builders are unaffected.

**Checkpoint**: Every application-bearing version (v42/v44/v50sp2/vlatest) emits a compilable builder tier; vt11 does not.

---

## Phase 5: User Story 3 - v44 builder behavior unchanged after dedup (Priority: P2)

**Goal**: Prove the dedup changed only struct identities/layout — never v44's serialized bytes or validation outcomes — and that the regenerated golden is deterministic. (Goldens regenerated + tests rewritten in Foundational; this phase is the *verification* US3 uniquely owns.)

**Independent Test**: Drive each v44 `build_<Msg>` and differential its bytes against the frozen pre-077 external corpus (NOT a regenerated golden); run the 067/069 validation suite + determinism.

- [ ] T020 [US3] **(TDD — extend the differential)** Extend `tests/session/test_067_builder_shape_oracle.cpp` (label `v44-byte-invariance`) from its **5** exemplars (D/8/9/E/AS) to a **frozen pre-077 external-byte differential over every distinct structural plan / multi-plan `no_tag`** — `NoLegs`/555 (8 plans), `NoOrders`/73, `NoRelatedSym`/146 (where a mis-share is most likely) — or at minimum all 83 in-scope messages: build the message, byte-compare against the QuickFIX `.fix` goldens + 061 exemplars (T002 corpus). **Round-trip is NOT a sufficient differential** (reads scan by tag; a reordered/dropped member of a collapsed variant round-trips clean). (FR-008, SC-004)
- [ ] T021 [US3] **Determinism**: assert every builder-bearing version's `Builders.hpp` is byte-identical across two separate-directory generations; wire the new/regenerated builder goldens into `tests/codegen/determinism_test.cpp` (or `codegen_determinism_test`) so the CI determinism gate covers them (FR-007, SC-004, G4b). Also run the 067/069 validation suite against the regenerated golden (`ctest -L "067|069" -L v44-builder`).
- [ ] T022 [US3] Implement the **read-tier no-regression gate** per the T006 FR-009 decision (label `read-tier-byte-diff`): recursive byte-diff of the generated legacy read artifacts vs the T001 baseline — full 4-artifact × 4-version, OR the narrowed goldened-`Messages` gate with the ungoldened `Fields`/`Validator`/`Reify` residual recorded in the verify doc. This is confinement-gated, not trusted (the change is `emit_builders.cpp`-only, but per the stale-object / narrow-verify lessons it must be asserted). (FR-009, SC-005)

**Checkpoint**: v44 serialized bytes + validation outcomes are provably unchanged; all builder goldens deterministic; read tiers byte-identical.

---

## Phase 6: User Story 4 - Per-version builder completeness is provable (Priority: P3)

**Goal**: Non-circular, exact-set, red-provable completeness per builder-bearing version — re-instating 076's descoped V-2/V-2b legs at raw-XML strength.

**Independent Test**: Each version's census asserts emitted `build_<Msg>` set == independently-derived in-scope application set; a dropped message makes it go RED.

- [ ] T023 [US4] **(TDD — RED first)** Add the **builder-completeness census** in `tests/codegen/` (new `test_077_builder_completeness.cpp`, label `builder-completeness`): (a) **expected(V)** from a **standalone raw-XML/Orchestra walk INDEPENDENT of `emit_builders`** — parse each message's `msgtype` + `msgcat`/`category` directly from `FIX42/44/50SP2.xml` and the `<fixr:repository>`, apply `is_application` + per-version `in_scope` (data-model Entity 6, C1); NOT `ir(V).messages`. (b) **actual(V)** by an **address-of census TU** taking `&fixpp::<ns>::build_<Msg>` and `&validate_<Msg>` for every expected entry point (compile-time ODR-use ⇒ existence; C2) — pins `expected ⊆ actual`. (c) secondary `builder_registry` text-parse pins `actual ⊆ expected`. Assert **exact-set equality** (C3a, SC-006). **Record the co-emission invariant (Gate A round-3 residual P3):** the `actual(V) ⊆ expected(V)` leg currently rests on `build_<Msg>`/registry-entry/`validate_<Msg>` all being emitted from ONE loop (`emit_builders.cpp:715/717/718`) — an unproven co-emission invariant, not an independent check. Note this dependency here; optionally close it cheaply with a header-symbol census over the emitted `Builders.hpp` asserting the parsed entry-point symbol set `== expected(V)` (the round-3 reviewer's suggested closure). RED until T017's emission is complete for all versions.
- [ ] T024 [US4] Implement THE **committed mutation seam** (C3b): a build-time `FIXPP_CODEGEN_DROP_BUILDER_MSGTYPE=<msgtype>` compile-define in `emit_builders.cpp`/`main.cpp`, compiled ONLY in the census mutation-witness target, that drops exactly one known in-scope message from the emitted builders + registry. A real mechanism, NOT prose.
- [ ] T025 [US4] Add `builder_completeness_mutation_witness` in `tests/codegen/` (standalone, §8-exempt — do NOT fold into a whole-binary target): invoke the T024 drop path in a subprocess and pass ONLY if the census goes **RED** with the expected missing msgtype. **Prove it RED** (run it, capture the failure) — a canary never observed red proves nothing. (C3b, SC-006, quickstart V6)
- [ ] T026 [US4] Assert **vt11 completeness** in the census: `expected(vt11) = ∅` and no `vt11/Builders.hpp` is emitted (C3c). Record the structural-key safety pin (C4): a future dictionary bump adding a new `no_tag` variant surfaces as a new `G_<no_tag>_<ordinal>Args` in the golden diff, never a silent mis-share.

**Checkpoint**: Every builder-bearing version has an exact-set, raw-XML-independent, proven-red-capable completeness gate.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: CI wiring, docs, limitation updates, and the mandatory close-out.

- [ ] T027 [P] In `cmake/Codegen.cmake`, wire the four new + one regenerated per-version builder goldens (`v42`/`v44_all`/`v50sp2`/`vlatest` new; `v44_official` regenerated) into the golden/determinism CI gates so the determinism + golden-diff gates cover them (FR-013). **Boundary vs T021:** T021 wires the goldens into the run-to-run *determinism* check; this task adds the *checked-in-golden-diff* gate (CI fails if a regenerated header drifts from its committed golden) and confirms all five goldens are actually referenced by a gate. If T021 already performed the CMake edit, this task only adds the golden-diff coverage + verifies it fires — state which in the /implement notes to avoid a double-edit.
- [ ] T028 [P] Update `docs/src/dictionary/codegen.md`: builders are now emitted for all application-bearing versions (v42/v44/v50sp2/vlatest) and deduped into `fixpp::<ns>::groups::G_<no_tag>[_ord]Args`; note the v44 nested-Args source-API break (no aliases).
- [ ] T029 [P] Update `spec/behaviors-and-limitations.md`: flip **L-076-1** (vlatest builder tier descoped) → **resolved-by-077**; record the structural-key safety pin (C4) as a behavior.
- [ ] T030 Run `quickstart.md` V1–V6 end-to-end; capture results into `.specify/decisions/077-builder-args-dedup-verify.md`.

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [ ] T031 [P] **Catalogue close-out**: flip every 077-owned OFFICIAL row in `spec/feature-catalogue.md` from `in-progress`/`backlog` → `done` (with the PR / evidence ref) AND add/update its matching `spec/coverage-index.md` entry (v42/v50sp2/vlatest builder tiers + v44 dedup). (T057 analog.)
- [ ] T032 **Feature-completeness audit (MUST be the FINAL task)**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale; (ii) every spec FR-001..013 and SC-001..006 maps to a landed test AND a landed implementation (use the FR/SC → task map below); (iii) every 077-owned OFFICIAL catalogue row is `done` with a matching `coverage-index.md` entry. Record the verdict (100% or fully-waived) in `.specify/decisions/077-builder-args-dedup-verify.md` (`## Completeness` section). Hard `/gate-b` precondition (Article XVII §8 / pre-flight 4d). (T058 analog.)

---

## Requirement → Task Traceability (for `/analyze` coverage pass + T032 audit)

| Req | Tasks |
|---|---|
| FR-001 (dedup key `(no_tag, signature)` + G1a naming) | T007, T008 |
| FR-002 (per-message bodies reference shared plans) | T008 |
| FR-003 (validation metadata once per plan, no ODR) | T008 |
| FR-004 (vlatest compiles single TU) | T014, T015 |
| FR-005 (single version-agnostic path) | T008, T009 |
| FR-006 (all app-bearing versions, per-version set, vt11 none) | T005, T017, T018, T026 |
| FR-007 / FR-007a (v44 goldens regen, deterministic, families knob) | T010, T021 |
| FR-008 (v44 behavior — frozen external corpus differential) | T011, T020 |
| FR-009 (legacy read tiers byte-identical) | T006, T022 (baseline T001) |
| FR-010 (non-circular completeness + mutation seam) | T023, T024, T025, T026 |
| FR-011 (dependency order / cycle bound) | T008 |
| FR-012 (vlatest gating + conditional OFF-clean) | T014, T019 |
| FR-013 (goldens wired to CI gates) | T010, T017, T027 |
| SC-001 (compile RSS low GB) | T015 |
| SC-002 (source shrink ~10 MB / ~578) | T013, T015 |
| SC-003 (100% round-trip, 0 skips) | T012, T016 |
| SC-004 (v44 renamed tests + byte differential + determinism) | T011, T020, T021 |
| SC-005 (read tiers byte-identical) | T022 |
| SC-006 (completeness exact-set + proven-red) | T023, T025 |

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: no dependencies — T001/T002 parallel; T003 (amendment) lands before emitter edits.
- **Foundational (Phase 2)**: T004/T005/T006/T007 parallel (verification/decision/RED-test); T008 depends on T004+T007; T009 after T008; T010 after T008+T009; **T011 after T010** (names derived from the regenerated golden). BLOCKS all user stories — and specifically the tree must compile at the T011 checkpoint before US1 tests can run.
- **US1 (Phase 3)**: after Foundational. T012/T013 RED before T014; T015 after T014.
- **US2 (Phase 4)**: after Foundational (US1's mechanism proven). T016 RED before T017; T018/T019 after T017.
- **US3 (Phase 5)**: after Foundational (goldens/rewrite already landed there). T020 after T010; T021 after T017 (covers all versions); T022 after T006 decision.
- **US4 (Phase 6)**: after US1+US2 emission complete. T023 RED → T024 → T025 (proven red) → T026.
- **Polish (Phase 7)**: after all desired stories. T032 is the FINAL task.

### Within Each User Story

- Tests (RED) before the emitter/CMake change that makes them GREEN.
- Mechanism (T008) before any per-version wiring.
- Golden regen (T010) before test rewrite (T011).

### Parallel Opportunities

- T001 ‖ T002 (Setup).
- T004 ‖ T005 ‖ T006 ‖ T007 (Foundational — different files/artifacts).
- T027 ‖ T028 ‖ T029 (Polish docs/CMake — different files); T031 ‖ others.

---

## Implementation Strategy

### MVP First (User Story 1)

1. Phase 1 Setup → baselines captured, amendment landed (v0.9).
2. Phase 2 Foundational (CRITICAL) → deduped emitter is the sole path; v44 goldens regenerated; `tests/session` compiles against `groups::G_…Args`.
3. Phase 3 US1 → vlatest builders emit, dedup to ~578, compile in low-GB RSS, round-trip.
4. **STOP and VALIDATE**: US1 independently (T012/T013/T015 green) — the descoped 076 tier is restored.

### Incremental Delivery

1. Setup + Foundational → mechanism + v44 green.
2. US1 → compilable vlatest builders (MVP).
3. US2 → v42/v50sp2 builders; vt11 none; OFF-path clean.
4. US3 → v44 behavior provably unchanged (frozen-corpus differential + determinism + read-tier byte-identity).
5. US4 → exact-set, raw-XML-independent, proven-red completeness per version.
6. Polish → CI wiring + docs + L-076-1 resolved + close-out.

---

## Notes

- [P] = different files, no incomplete dependency.
- **The v44 nested-Args rename is a source-breaking change with NO aliases** (BREAK, user-decided 2026-07-16). Top-level `<Msg>Args` names are unchanged; only ~19 nested-group Args names / ~130 occurrences change (T011 re-count) — derived from the regenerated golden, never hand-mapped.
- `no_tag` alone is NOT a sound builder key (research.md R2 census: up to 8 plans/`no_tag`) — the key is `(no_tag, recursive structural signature)`. The read tier compiling proves nothing about builder-key soundness (it unions).
- Expected set for the completeness census AND the golden struct counts MUST come from an independent raw-XML walk (T005/T023), never the emitter's own `ir(V).messages` (non-circular — the 075/076 blind-corpus lesson).
- The mutation witness (T025) MUST be run and observed RED — a canary never proven red proves nothing.
- No new runtime / C-ABI / Python / link-ABI surface (C-ABI frozen 1.5.0). Only the build-only codegen host tool, generated headers, goldens, and tests change.
- v50sp2/vlatest builder-TU compile overage is recorded via the 003 compile-bench decision-record convention (T015), NOT Article VIII (runtime-only).
- New standalone TUs that need isolation (the mutation witness) stay standalone; whole-binary-grouped TUs select by `ctest -L <label>`, never `-R <exe-name>` (§8).
