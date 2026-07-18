---
description: "Task list — Runtime validator required-presence scoping (fixpp#201)"
---

# Tasks: Runtime validator required-presence scoping

**Input**: Design documents from `specs/079-required-presence-scope/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/census-and-agreement.md, quickstart.md

**Tests**: REQUIRED. This is a correctness-fix + verification feature (plan.md Article VII: TDD RED→GREEN, non-circular exact-set census). Test tasks are first-class, not optional.

**Candidate baseline (already committed on this branch — commit `177a0535`)**: the loader group-scope change (`expand_field_list` `in_group` flag in both loaders), the additive per-group required-member store (bare `group_required_members_` + context `group_ctx_` in `table_view`) + accessors, `dictionary.cpp::as_table_view()` population, the validator `consume_group()` per-instance check (**≤64-guarded bitmask — fail-OPEN, to be widened**), and two RED→GREEN pin tests (`tests/dictionary/required_scope_test.cpp`, `tests/wire/validator_type_check_test.cpp`). Tasks below describe the **delta** the spec's independent verification requires on top of this candidate — the candidate is a *hypothesis*, not ratified verbatim (plan.md §Summary; Assumptions).

**Organization**: grouped by user story (US1–US4) per priority (P1 = US1/US4, P2 = US2/US3).

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: US1 / US2 / US3 / US4; Setup / Foundational / Polish carry none

## Path Conventions

Single-project C++ library (plan.md §Project Structure). Source at `include/fixpp/…` + `src/…`; tests at `tests/…`; bench at `bench/…`. Paths are repo-root-relative to the library submodule `research/G19-fix-fpml-iso20022/library/`.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: confirm the build/test scaffolding the verification legs need; no product code.

- [ ] T001 Confirm a clean configured build dir per quickstart.md §Prerequisites (`cmake -S . -B build/debug -G Ninja`, `-j2` WSL2 cap) and that the candidate baseline (`177a0535`) builds + its two pin tests pass GREEN, establishing the pre-delta floor.
- [ ] T002 [P] Inventory the ctest bucket labels the quickstart TODOs defer (`dictionary_pure_tests`, `wire_pure_tests`, `codegen_determinism`, `wire_dict`) in `tests/dictionary/CMakeLists.txt` + `tests/wire/CMakeLists.txt`; record the exact `-L <label>` strings each new grouped test will attach to (068 grouping precedent, Article VII §8 — select by label, never `-R <exe-name>`). Standalone exact-set gates (census, parity) are `-R`-selected per the Article VII §8 carve-out.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: the single candidate-code change every US2/US4 leg depends on, plus a store-contract reconciliation. **BLOCKS US2, US3, US4.**

**⚠️ CRITICAL**: the ≤64 fail-open skip must be removed before the per-instance reject legs (US2) and the per-group census max-count leg (US4) can be honest.

- [ ] T003 [Foundational] RED test for the dynamic-width per-instance required-member check: add a case to `tests/wire/validator_type_check_test.cpp` that drives a **synthetic** group carrying >64 required members and asserts an instance omitting one is REJECTED (offending tag surfaced). Against the candidate's `size() <= 64` guard this MUST fail (the check is skipped → wrongly accepted). Confirm RED before T004.
- [ ] T004 [Foundational] Widen the fixed ≤64-bit mask in `include/fixpp/wire/validator.hpp` (`consume_group`, lines ~307–325: `check_required … size() <= 64`, `full_mask`, `seen_mask`) to a **dynamic width** — enforce for **every** per-group required-member count, fail-CLOSED (Article XV, FR-004/RC5), preserving the existing linear `req_bit` scan and delimiter pre-mark; remove the `<= 64` skip and the `>= 64 ? ~0` saturation. Makes T003 GREEN. No allocation on the no-group / no-required-member fast path (data-model §Group-instance membership check state).
- [ ] T005 [Foundational] Reconcile the per-group store contract to data-model.md §Per-group required-member set + Contract 1a: confirm `table_view` exposes BOTH `group_required_members(no_tag)` (bare / global-first-seen fallback) AND `group_required_members(msg_type, parent_path, no_tag)` (context, queried first; bare only on miss), populated in `dictionary.cpp::as_table_view()` from `rule==Required && group_no_tag==no_tag`. If either accessor or the context/bare split is absent or mis-keyed vs the candidate, correct it in `include/fixpp/dict/table_view.hpp` + `src/dictionary/dictionary.cpp`. (No behavior change if the candidate already matches — this is a contract pin, not a rewrite.)

**Checkpoint**: fail-closed dynamic-width check in place; both store legs exist with the queried-context-first / bare-fallback contract. User stories can proceed.

---

## Phase 3: User Story 1 — Conforming message omitting an optional group is accepted (Priority: P1) 🎯 MVP

**Goal**: strict `validate()` accepts a conforming frame that legitimately omits an optional repeating group (the shipped correctness defect).

**Independent Test**: parse a conforming instance of each affected message that omits the optional group; assert runtime `validate()` returns accept (no `wire_required_field_missing`), across all affected versions — dictionary + validator only, no session.

### Tests for User Story 1 (write FIRST, confirm RED against pre-candidate, GREEN with candidate loader fix)

- [ ] T006 [P] [US1] Real-frame ACCEPT regression (Contract 4 accept), named messages: in `tests/wire/validator_type_check_test.cpp` (or a sibling `tests/wire` frame test in the `wire_pure_tests` bucket), parse a FIX44 `PositionReport` (AP) with no `NoUnderlyings` and a FIX50SP2 `TradeCaptureReport` (AE) with no `NoSides`, run each through `dictionary_driven_validator::validate()`, assert **ACCEPT** (previously `wire_required_field_missing(732)` / tag 54). Attach to the bucket label from T002. **Discrimination note (enshrine-trap guard):** these frames go GREEN immediately against the committed candidate, so they do NOT self-prove the loader fix is correct. Discrimination is supplied REPO-WIDE by T016(a) (revert the `in_group` gate → these very frames + every message go RED); do NOT mark T006/T007 done until T016(a)'s RED capture exists. ([[feedback_coverage_push_enshrines_bugs]])
- [ ] T007 [P] [US1] Real-frame ACCEPT, one representative conforming frame per affected version — FIX44 / FIX50SP2 / **FIX42** (FIX42 runtime-only, no typed tier per SC-004) — omitting its optional group; assert ACCEPT. Same bucket. **FIX42 + vlatest are the highest-risk done-by-baseline spots** — FIX42 routes through `orchestra_loader` NO (it's a QuickFIX-XML dict via `xml_loader`; vlatest is the `orchestra_loader` path), and both are the versions where "candidate already handles it" is least certain — pick real conforming frames and confirm the ACCEPT actually exercised the loader-scoped set, do not pre-assume.
- [ ] T008 [P] [US1] Derivation-unit corroboration (supplementary, does NOT exercise `validate()`): extend `tests/dictionary/required_scope_test.cpp` so the message-level required set from `table_view::required_fields()` EXCLUDES the group-scoped tags — FIX44 AP excludes 732/733; FIX50SP2 AE excludes 54 — reusing the candidate's existing AP/AE/D pins (confirm they assert the exclusion, extend if a version is missing). Bucket `-L dictionary`.

### Implementation for User Story 1

- [ ] T009 [US1] No new product code expected — the loader `in_group` group-scope fix is the candidate baseline (T001). If T006/T007 reveal a conforming frame still rejected (a group-scope leak the candidate missed on some version), fix `expand_field_list` in `src/dictionary/xml_loader.cpp` and/or `src/dictionary/orchestra_loader.cpp` (vlatest) so no group-member required enters the message-level `required_out`; otherwise mark T009 done-by-baseline with that note.

**Checkpoint**: conforming-omit frames accepted on every affected version; message-level derivation excludes group-scoped tags.

---

## Phase 4: User Story 2 — Malformed repeating-group instance is rejected (Priority: P2)

**Goal**: a present group whose instance omits an intra-group `required='Y'` member is rejected, offending tag surfaced (the inverse hole); enforcement universal / dynamic-width (depends on T004).

**Independent Test**: construct a group with ≥2 instances where a later instance omits a required member; assert `validate()` rejects with the offending tag; a fully-populated group is accepted.

### Tests for User Story 2

- [ ] T010 [P] [US2] Real-frame REJECT regression (Contract 4 reject), one representative message per affected version (FIX44 / FIX50SP2 / FIX42): in `tests/wire/validator_type_check_test.cpp`, a group whose second instance omits an intra-group required member → REJECT with the missing tag surfaced (extend/parameterize the candidate's existing single reject pin to cover each version). Bucket from T002.
- [ ] T011 [P] [US2] No-false-reject pin (Contract 4): a message whose every group instance carries all its required members → ACCEPT (the per-instance check introduces no false reject). Same bucket. (T003's >64 synthetic dynamic-width case already lives in this file from Phase 2.)

### Implementation for User Story 2

- [ ] T012 [US2] Per-instance reject logic is the candidate `consume_group()` as widened by T004 — no further product code expected. If T010/T011 surface a nesting-level or context-store-miss defect (e.g. a reused-tag group resolving the wrong per-context required set), fix in `include/fixpp/wire/validator.hpp` `consume_group` (reuse 072's nesting-aware structure per Assumptions) and/or the T005 store population; otherwise mark done-by-baseline.

**Checkpoint**: malformed-instance frames rejected on every affected version, dynamic-width, with the offending tag; complete instances accepted.

---

## Phase 5: User Story 3 — The two validation tiers agree (Priority: P2)

**Goal**: for versions WITH a typed tier (v44 / v50sp2 / vlatest), the runtime validator and the generated typed `validate_<Msg>` return identical accept/reject verdicts — guarding the Phase-0 "no codegen change" conclusion. FIX42 excluded (no typed validator, L-077-1/#196, `main.cpp:132`).

**Independent Test**: run the same conforming frame (US1) and malformed frame (US2) through BOTH the runtime validator and the generated typed validator; assert identical verdicts.

### Tests for User Story 3

- [ ] T013 [P] [US3] NEW two-tier verdict-agreement test (Contract 3) `tests/wire/required_scope_two_tier_test.cpp`: for the named messages + one-per-version corpus (conforming AND malformed frames), assert `runtime_validate(f)` verdict == `generated_typed_validate_<Msg>(f)` verdict, **v44 / v50sp2 / vlatest only**. FIX42 explicitly excluded with the L-077-1 rationale in a comment. This is ordinary isolation-safe verdict-comparison coverage → grouped-bucket `-L` selection (finalize the label with T002; a standalone target only if isolation justifies it — quickstart §5).

### Implementation for User Story 3

- [ ] T014 [US3] No codegen change is expected (Phase-0: codegen top-level check already `group_no_tag==0`-filtered; 0 optional-component sites). If T013 fails — a typed tier over-/under-requires vs the runtime tier — it localizes a missed codegen leg: fix `tools/codegen/fixpp-codegen/ir.cpp` (regen affected goldens, re-run FR-008 byte-identity T017) and record the scope reversal in research.md. Otherwise mark done-by-baseline confirming the tiers already agree.

**Checkpoint**: the tiers return identical verdicts for every affected typed-tier message — the no-codegen-change conclusion is test-backed.

---

## Phase 6: User Story 4 — Required-set derivation matches an independent oracle across every dictionary (Priority: P1)

**Goal**: the correctness proof — exact set-equality between an independent raw-XML oracle and the shipped required set, per message, across all 10 dicts, cross-checked against QuickFIX. The tightest constraint; primary guard against over- AND under-correction repo-wide.

**Independent Test**: a non-circular census (independent pugixml walker, no loader/IR code shared) computing full-ancestor-chain component-AND + StandardHeader/Trailer carve-out, compared exact-set-equal to `table_view::required_fields()` (Step-2 pre-skip input) and the codegen IR projection; plus per-group legs; cross-checked to a QuickFIX golden.

### Tests for User Story 4 — the census (Contract 1 + 1a)

- [ ] T015 [US4] NEW non-circular message-level census (Contract 1) `tests/dictionary/required_scope_census_test.cpp` (standalone exact-set gate, `-R required_scope_census`): an independent raw-XML pugixml walker `expected(D, msg)` computing message-level required = own `required='Y'` AND full-ancestor-chain componentRef-usage AND, NOT enclosed by any group, **EXCEPT StandardHeader/Trailer tags {8,9,34,35,49,52,56,10} never dropped**. For every message in all 10 dicts assert `expected == table_view::required_fields(msg_type)` (pre-skip span, both directions — also verifies 8/9/10 present) AND `expected ==` codegen `MessageIR` top-level required list (present for every version incl. FIX42). Non-circularity banner: the ban applies to the `expected()` **walker only** — it MUST NOT call `XmlLoader`/`OrchestraLoader`/`build_ir()`. The actual-side IR-projection leg legitimately calls `build_ir()` + `collect_top_fields()` directly (mirroring the `codegen_067_emit_builders_unit_test` precedent, which links `ir.cpp` into a non-`tests/codegen` binary); linking `ir.cpp` into this census binary for the shipped-IR side is expected, not a circularity breach. Failure names msg, dict, differing tag(s).
- [ ] T016 [US4] Prove the message-level census RED on **two** witnesses (Contract 1 RED-proof obligation) — these two RED captures are the **load-bearing claim of the entire feature**; they MUST be PERFORMED and their RED output CAPTURED (not asserted/assumed — [[feedback_sanitizer_canary_must_be_proven_red]]), each reverted after capture: (a) revert the `in_group` gate in `expand_field_list` → group-member leak restored → census RED (this is ALSO the repo-wide discrimination proof for T006/T007/T008/T010 — capture which messages/tags flip); (b) inject a synthetic **non-header/trailer** optional-component-`required='Y'` field → the stronger full-component-AND oracle drops it while the loader keeps it → census RED (even though the real corpus has 0 optional-component sites — makes the "scope narrowing does not narrow verification" claim load-bearing, RC1). Paste both actual RED ctest excerpts into `.specify/decisions/079-required-presence-scope-verify.md`; the orchestrator re-checks these captures directly (a subagent "went RED" claim does not satisfy this task).
- [ ] T017 [US4] NEW per-group required-member census (Contract 1a) in the same `required_scope_census_test.cpp` — TWO legs (distinct contracts): **(1) context store (PRIMARY)** `group_required_members(msg_type, parent_path, no_tag)` == the walker's per-context required set, exact set-equality both directions, for every `(msg_type, parent_path, no_tag)` in all 10 dicts; **(2) bare store** `group_required_members(no_tag)` == the global first-seen variant (fallback contract — NOT required to equal every context; e.g. FIX44 tag 295 `{}` vs `{299}`). Also census the shipped **maximum** per-group required-member count across all 10 dicts (RC5 — pins the small-count assumption so the dynamic-width check cannot silently regress). Prove RED by an injected/omitted per-context required member.

### Tests for User Story 4 — QuickFIX parity (Contract 2)

- [ ] T018 [P] [US4] QuickFIX required-set golden GENERATOR (local-only, behind `-DFIXPP_BUILD_QUICKFIX_GOLDEN=ON`, no CI link): a `quickfix_required_golden` target reading each of the 9 QuickFIX-schema dicts via quickfix-cpp 1.16.0 `DataDictionary::isRequiredField(msgType, tag)` (encodes the component AND-rule `:510/:522` + per-group required members `:560/:570`), emitting a checked-in golden with a **manifest + content hash + stale-golden regen/diff rule** (hardened 075 precedent). No vlatest/Orchestra row (QuickFIX can't parse Orchestra — an absent-surface row goes spuriously RED). Add `.gitattributes -text` on the golden if text (CRLF-hash trap, [[feedback_byte_exact_sha1_gate_needs_gitattributes_text]]).
- [ ] T019 [US4] NEW QuickFIX parity gtest (Contract 2) `tests/wire/required_scope_parity_test.cpp` (standalone exact-set gate, `-R required_scope_parity`): consume the checked-in golden (no QuickFIX link), assert `quickfix_required_set(dict, msg) == expected(dict, msg)` (the census oracle) exact set-equality per message, 9 QuickFIX dicts. Confirms the oracle encodes the AND-rule faithfully (breaks the same-wrong-reading circularity).

### Implementation for User Story 4

- [ ] T020 [US4] No product code expected — the census/parity assert the candidate baseline is correct. If T015/T017/T019 surface a real over-/under-require (message-level or per-context), fix the responsible loader (`src/dictionary/xml_loader.cpp` / `orchestra_loader.cpp`) or store population (`src/dictionary/dictionary.cpp`) and re-run; record any candidate correction in research.md (the candidate was a hypothesis). Otherwise mark done-by-baseline.

**Checkpoint**: exact set-equality proven across all 10 dicts (runtime + IR + per-group), max-count pinned, QuickFIX-parity confirmed, all four RED witnesses demonstrated.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: hot-path bench (mandatory), regression floor, control pin, then the two mandatory close-out tasks.

- [ ] T021 [P] Hot-path perf bench (Article VIII §3 — **absolute**, ships in THIS PR, not a MET-PARTIAL note, [[feedback_gateb_perf_change_needs_bench_not_a_metpartial_note]]): extend `bench/wire/validator_bench.cpp` with before/after rows for (a) a no-group message, (b) a shallow single group, (c) a nested / multi-instance group — measuring the `consume_group` per-instance required-member scan delta. Outcome may be "within noise" but the bench MUST exist and be reported in the PR body (plan.md §Performance Goals).
- [ ] T022 [P] Read/reify golden byte-identity regression floor (SC-006, FR-008): confirm `codegen_determinism` + the read goldens (v44 / v42 / vt11 / v50sp2 / vlatest) are byte-identical before/after — the candidate touches only loader `required_out`, not `FieldRef.rule` or IR inputs, so NO golden may change (no delta allowance). Run the quickstart §6 bucket.
- [ ] T023 [P] Control pin (SC-007): assert FIX44 `NewOrderSingle` (D) message-level required set never contained `Symbol`(55) and still does not (no over-correction; QuickFIX AND-rule parity) — confirm the candidate's existing D control in `required_scope_test.cpp` covers this, extend if not.
- [ ] T024 Finalize the ctest bucket labels / standalone targets the quickstart TODOs defer: wire the new grouped tests (T006/T007/T010/T011/T013) into their `-L` buckets (note `wire_pure_tests` currently carries NO `LABELS` — attach a `wire` (or feature) label so `-L wire` selects the new US1/US2/US3 frames) and register the two standalone gates (T015 `required_scope_census`, T019 `required_scope_parity`) as `add_test(NAME …)` per Article VII §8; update quickstart.md's `<finalized at /tasks>` placeholders with the concrete strings AND **correct** quickstart §6's regression-floor regex — today it reads `-L 'codegen_determinism|dictionary_pure|wire_pure|wire_dict'`, which matches ZERO real labels (actual shipped labels: `codegen`, `dictionary`, `wire`) → a silent 0-tests-selected false-green ([[feedback_ci_gate_observes_not_asserts_witness_skips_into_green]]). Correct it to the real label strings.
- [ ] T025 Run the full quickstart.md end-to-end (steps 1–6) under the local clang-debug build (`-j2`); confirm all steps GREEN with the census proven RED on BOTH witnesses and the bench reported. **Explicitly assert each `ctest` invocation selects a NON-ZERO test count** (ctest exits 0 on zero matches — a mis-typed `-L`/`-R` selector is a silent false-green; step 6's regression-floor selector especially, per F1/T024). This is the /speckit-verify input floor (pipeline step 12).

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [ ] T026 [P] **Catalogue close-out**: this feature CORRECTS existing `done` OFFICIAL rows rather than owning a new one (063/066/075 amendment precedent) — append a 079 correction note to `spec/feature-catalogue.md` rows **W-014** (validator required-fields clause: message-level set now excludes group-scoped requireds; per-instance group required members now enforced dynamic-width) and **W-006/W-007** (per-instance group required-member enforcement), citing the PR + census/parity evidence; add the matching `spec/coverage-index.md` note under §3 (Message validator — required fields) and §3.2 (Repeating groups). No row flips to `done` (none is in-progress/backlog). Record why no new row is owned.
- [ ] T027 **Feature-completeness audit (MUST be the FINAL task)**: assert against the tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale; (ii) every spec FR-001…FR-010 (+ FR-009a) and SC-001…SC-008 (+ SC-003a) maps to a landed test AND a landed implementation (or a done-by-baseline note citing `177a0535`); (iii) the W-014/W-006/W-007 amendments + coverage-index notes are present. Record the verdict (100% or fully-waived, with the FIX42-typed-tier and 0-optional-component-site carve-outs enumerated) in `.specify/decisions/079-required-presence-scope-verify.md` `## Completeness` (or a sibling `079-required-presence-scope-completeness.md`). Hard `/gate-b` precondition (pre-flight 4d).

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: no dependencies.
- **Foundational (Phase 2)**: depends on Setup. **BLOCKS US2, US3, US4** — T004 (dynamic-width) gates the per-instance reject (US2) and the max-count census leg (US4); T005 (store contract) gates the per-group census (US4/T017) and two-tier (US3/T013).
- **US1 (Phase 3, P1)**: depends only on the candidate loader baseline (Setup/T001) — it is the MVP and does NOT require T004 (accept-path only). Can start right after Setup.
- **US2 (Phase 4, P2)**: depends on Foundational (T004).
- **US3 (Phase 5, P2)**: depends on Foundational (T005) + US1/US2 frames (reuses their corpora).
- **US4 (Phase 6, P1)**: depends on Foundational (T004 max-count, T005 store legs).
- **Polish (Phase 7)**: depends on all user stories; T026/T027 are the last two tasks, T027 final.

### User Story Dependencies

- US1 (P1): independent, MVP.
- US4 (P1): the correctness proof; independent of US1/US2 product code but reuses no frames — pure census/parity. Highest-value verification.
- US2 (P2): needs T004.
- US3 (P2): reuses US1 + US2 frames.

### Within Each User Story

- Tests written FIRST, confirmed RED (against the pre-candidate / injected-fault state) before the implementation task marks done-by-baseline or applies a fix.
- The census RED-proof (T016) and per-context RED-proof (T017) are non-negotiable — a never-RED completeness gate proves nothing ([[feedback_sanitizer_canary_must_be_proven_red]], [[feedback_completeness_gate_exact_set_not_subset]]).

### Parallel Opportunities

- T002 [P] alongside T001.
- Within US1: T006/T007/T008 [P] (different assertions/files).
- Within US2: T010/T011 [P].
- Within US4: T018 [P] (golden generator, local-only) alongside T015/T017; T019 depends on T018's golden.
- Polish: T021/T022/T023/T026 [P] (independent files); T024/T025 serialize; T027 last.

---

## Implementation Strategy

### MVP First (User Story 1)

1. Phase 1 Setup → Phase 3 US1 (accept-path, candidate baseline already lands the fix) → **STOP & VALIDATE**: conforming-omit frames accepted on all versions. This alone closes the shipped false-reject defect.

### Incremental Delivery

1. Setup + Foundational (T004 dynamic-width, T005 store legs) → foundation ready.
2. US1 → conforming accepted (MVP).
3. US4 → the correctness proof (census + parity, 4 RED witnesses) — the leg that distinguishes "correct" from "passes three examples".
4. US2 → malformed rejected (dynamic-width).
5. US3 → two tiers agree (guards no-codegen-change).
6. Polish → bench + regression floor + close-out.

### Notes

- The candidate (`177a0535`) is a HYPOTHESIS — every done-by-baseline task must be confirmed by a test that was RED before the candidate / an injected fault, not assumed ([[feedback_coverage_push_enshrines_bugs]]).
- FIX42 carve-out (no typed tier — L-077-1/#196) is threaded through US3/SC-004/SC-008 and the two-tier test scope; do not add a FIX42 typed-tier row.
- 0 optional-component over-require sites (Phase-0) is a SAFETY-NET claim made load-bearing by T016(b)'s synthetic-injection RED witness — never drop it.
- Commit after each task or logical group; select tests by `-L` bucket (grouped) or `-R` (the two standalone exact-set gates only), never `-R <exe-name>` on a grouped test.
