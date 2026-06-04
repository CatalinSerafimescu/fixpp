# Tasks: G2 Business-Message Interop — typed NewOrderSingle + ExecutionReport

**Feature**: `020-g2-business-messages` | **Branch**: `020-g2-business-messages`
**Inputs**: [spec.md](./spec.md), [plan.md](./plan.md), [research.md](./research.md), [data-model.md](./data-model.md), [contracts/business-messages.md](./contracts/business-messages.md), [quickstart.md](./quickstart.md)

This project follows TDD (`[const §VII]`): every behavioral task lands its test **RED first**, then the implementation makes it green. Tasks are organized by user story so each story is an independently testable increment.

## Format: `[ID] [P?] [Story] Description`

- **[P]** = parallelizable (different file, no incomplete dependency).
- **[USx]** = user-story phase task. Setup / Foundational / Polish carry no story label.
- Each task names an exact file path.

## Library/source map (from plan Structure Decision)

- **NEW (write side):** `include/fixpp/session/business_messages.hpp` + `src/session/business_messages.cpp` — `build_new_order_single` / `build_execution_report` (body-only builders; `decimal_t` numerics; stack-scratch-then-copy atomicity).
- **EDIT (send path):** `src/session/session.cpp` `send_impl` — MsgType→field-3, digit-only BodyLength, valid checksum (FR-004a); opaque-payload fail-closed validation (FR-016).
- **EDIT:** `include/fixpp/core/error.hpp` — `app_payload_malformed = 131`.
- **READ side:** consume generated `fixpp::v44::{NewOrderSingle,ExecutionReport}` flyweights (no new prod code).
- **Tests:** `tests/session/test_business_messages_build.cpp`, `..._read.cpp`, `..._roundtrip.cpp`; in-repo interop `tests/interop/test_business_message_interop.cpp`.
- **Parent harness (tests-only):** `phase-9-harness/` — responding counterparty `Application`s + business-message cells + goldens.

---

## Phase 1: Setup (baseline re-verification)

- [X] T001 Re-verify the research.md ground-truth claims on the branch base before coding — record any drift as a bundle defect: (a) `Session::send_impl` (`src/session/session.cpp`) stamps `8/9/34/49/52/56` then appends the app payload, so MsgType lands ~7th (≈`session.cpp:2809`) and BodyLength is **zero-padded** `9=000000` reserve + right-align backpatch (≈`session.cpp:2768`/`:2823`); (b) `wire::Writer::commit()` writes **digit-only** BodyLength (`src/wire/writer.cpp`; `.specify/2b-wire.md`); (c) next-free `error::` slot is **131** (019 ends at `app_callback_threw = 130`) in `include/fixpp/core/error.hpp`; (d) generated `fixpp::v44::{NewOrderSingle,ExecutionReport}` exist with the claimed accessors (`build/<preset>/_codegen/include/fixpp/v44/Messages.hpp`); (e) `owning_<Msg>` in `Reify.hpp` has no setters/serialize; (f) `admin_messages.hpp` builders emit **complete** frames (not bodies); (g) `tools/check_layers.py` grants `session → {core,dictionary,wire,transport,log,otel}`.
- [X] T002 Apply the 3 carried Gate-A round-2 P3 nits (doc-only): in `specs/020-g2-business-messages/quickstart.md` add the standard headers used by the snippets (`<array>`, `<memory_resource>`, `<span>`, and `<fixpp/v44/Messages.hpp>`) or label them abbreviated; add the arena-lifetime caveat to the `dec(...)` helper; confirm the ExecRpt `exec_type()`/`ord_status()` decode types (`expected_t<char>`) against generated `Messages.hpp`.

---

## Phase 2: Foundational (blocking prerequisites)

- [X] T003 [P] Add error enumerator `app_payload_malformed = 131` in `include/fixpp/core/error.hpp` (append-only after 019's 130; add the matching `error_message()` string; cross-check the boundary stays 131 with no ±N drift — exact-SET completeness asserted at T020 per [[feedback_completeness_gate_exact_set_not_subset]]). (FR-016.)
- [X] T004 Create the `include/fixpp/session/business_messages.hpp` declaration skeleton (the two `build_*` signatures from `contracts/business-messages.md`; `noexcept`, `expected_t<std::span<std::byte>>`, `const fixpp::decimal_t&` numerics) + register `src/session/business_messages.cpp` in the `session` library CMake target. Decls + empty/`unexpected` stub bodies only — makes US1 tests compile-and-fail RED. (FR-003.)

---

## Phase 3: User Story 1 — Typed surface + loopback round-trip (Priority: P1) 🎯 MVP

**Goal**: a user builds typed NewOrderSingle/ExecutionReport from named fields, sends via `Engine::send`, and reads the inbound message via the generated `fixpp::v44` accessors — all in-process over a loopback TLS engine, with the send path emitting wire-conformant framing.

**Independent test**: build→parse fidelity for both messages; loopback `Engine::send` → peer `fromApp` typed read with field fidelity; invalid-field build fails closed; stored frame has MsgType field-3 + digit-only BodyLength.

### Tests for User Story 1 (write FIRST, confirm FAIL) ⚠️

- [X] T005 [P] [US1] `tests/session/test_business_messages_build.cpp` — AS1/AS2: `build_new_order_single` / `build_execution_report` produce wire-conformant `35=D`/`35=8` bodies whose fields parse back to exactly the supplied values; **INV-2** `Builder_Output_ContainsNoEngineTags` (scan body asserts no `8=/9=/34=/49=/52=/56=/10=`); **INV-3** `Builder_NumericFidelity_DecimalValueEquality` — numeric fidelity by `decimal_t` value-equality (incl. trailing-zero forms `190.5`≡`190.50`); **INV-4** `Builder_InvalidField_NoUsableOutput` (empty required string / out-of-range enum char / unformattable decimal / ill-formed UTCTimestamp / too-small `out` → typed error, returned span absent, no partial frame); FR-008 TransactTime length+shape validation; **§VIII.5 alloc witness** `Builder_NoHeap_CountingResource` — wrap both builders in a `counting_resource` and assert **zero** heap allocations on the write path (the witness promised by plan §VIII.5 + contract). (US1 AS1/AS2/AS5; FR-001/002/004/005/007/008; INV-2/3/4; `[const §VIII.5]`.)
- [X] T006 [P] [US1] `tests/session/test_business_messages_read.cpp` — AS4: over a parsed `MessageView<Index>` of a `35=D` and a `35=8` frame, construct `fixpp::v44::NewOrderSingle`/`ExecutionReport` and assert each minimal accessor returns the correct typed value (`decimal_t` numerics via caller arena `mr`); a missing/ill-typed required field surfaces as the accessor's `expected_t` error. (US1 AS4; FR-006.)
- [X] T007 [US1] `tests/session/test_business_messages_roundtrip.cpp` — the send-path + round-trip RED suite (loopback TLS engine, mirrors `test_019_g2_enablement_witness.cpp`): AS3 typed `Engine::send(NOS)` → peer `fromApp` `35=D` read via v44 accessors with fidelity (+ ExecRpt reverse); **INV-1** `SendPath_StoredFrame_Field3MsgType_UnpaddedBodyLength_ValidChecksum` asserting on the **captured `transport_send`/stored bytes** (RED before the send-path fix — lenient `fromApp` masks it); **INV-8** `OpaquePayload_Malformed_RejectedNoSeqnumConsumed` (empty / no-leading-35 / duplicate-35 / embedded session-trailer tag → `app_payload_malformed`, no seqnum consumed); **INV-7** `SendFromInsideFromApp_NoDeadlockNoUAF` under a **multi-threaded `io_context`** (re-entrant `Engine::send` from inside `fromApp`); **INV-5** `InboundReject_EmitsBusinessMessageReject_SessionSurvives` — loopback: the acceptor `Application`, reading an inbound `35=D` via the v44 accessors and obtaining an `expected_t` error (or a deliberately malformed field), returns a reject from `fromApp` ⇒ peer receives `BusinessMessageReject(35=j)` and the session stays `Active` (the FR-009/SC-005 in-engine demonstration at the 020 level, on 019's wired path). (US1 AS3; FR-004a/006/009/016; SC-005; INV-1/5/7/8.)

### Implementation for User Story 1

- [X] T008 [US1] Implement `build_new_order_single` + `build_execution_report` in `src/session/business_messages.cpp`: build into a **local stack scratch buffer** via hand-written body-only field append (no `wire::Writer` body-only mode), copy into caller `out` **only on full success** (INV-4 atomicity); lead with `35=D`/`35=8` then business fields (no engine tags); OrdType fixed `2`; numerics via `decimal_t::format(span)`; validate fail-closed (empty string / enum range / decimal format / UTCTimestamp length+shape / buffer size). Makes T005 green. (FR-001/002/003/004/005/007/008.)
- [X] T009 [US1] Send-path framing fix in `src/session/session.cpp` `send_impl`: emit the app frame so **MsgType(35) is field-3** (hoist the payload's leading `35=` after `9=`, ahead of `49/56/34/52`) and **BodyLength is digit-only/unpadded** with a valid checksum — preferably by routing the app-send framing through `wire::Writer` (which already commits digit-only), else correct the manual backpatch. Preserve the 019 `Engine::send` contract + opaque witness. Makes T007 INV-1 green. (FR-004a.)
- [X] T010 [US1] Opaque-payload fail-closed validation in the app-send path (`Engine::send`→`send_impl`), **before** seqnum assignment/store: require exactly one leading `35=`; reject empty payload / duplicate `35=` / any embedded session header-or-trailer tag (`8/9/34/49/52/56/10`) / payload not final-SOH-terminated / empty MsgType value (`35=` immediately followed by SOH) with `error::app_payload_malformed` (131); no transmit, no seqnum consumption. Makes T007 INV-8 green. (FR-016.)
- [X] T011 [US1] Confirm the re-entrant `fromApp`→`Engine::send` path (T007 INV-7) is deadlock/UAF-free under a multi-threaded executor; if it cannot be made safe on-strand, hoist the responder's reply off-strand and record an explicit waiver in `plan.md ## Gate A`/B&L (per Assumptions). (INV-7; L-019-3.)

**Checkpoint US1**: typed build+read+loopback round-trip green under debug; INV-1/7/8 green; this is the MVP.

---

## Phase 4: User Story 2 — Live business-message interop vs QuickFIX-J, both roles (Priority: P2)

**Goal**: live `Logon→NOS→ExecRpt→Logout` vs QuickFIX-J in both roles; discharges `[const §VII.6]`.

### Tests for User Story 2 (write FIRST, confirm FAIL) ⚠️

- [X] T012 [P] [US2] `tests/interop/test_business_message_interop.cpp` — in-repo SUT cells for the live QFJ business round-trip, both roles (fixpp-init × QFJ-acc and fixpp-acc × QFJ-init): assert fixpp originates/receives `35=D`, receives/originates `35=8` read via v44 accessors with fidelity, clean `Logout`; **skip cleanly** when no counterparty (`FIXPP_TLS_FIXTURE_DIR`/counterparty-absent contract). (US2 AC1/AC2; FR-010/013.)

### Implementation for User Story 2

- [X] T013 [US2] Add the responding QuickFIX-J `Application` (emit one fully-filled `ExecutionReport`: ExecType=F/OrdStatus=2, LeavesQty=0, CumQty=OrderQty, AvgPx=Price, echo Symbol/Side, fresh OrderID/ExecID per inbound NewOrderSingle) in `phase-9-harness/quickfixj/.../InteropCounterparty.java` (extend the 018 JSSE counterparty). (FR-010.)
- [X] T014 [US2] Add the fixpp-acceptor responding test-fixture `Application` (replies via `Engine::send`+`build_execution_report` from inside `fromApp`) used by the both-role cells; wire it into the in-repo interop fixture. (FR-010; E3; INV-7.)
- [X] T015 [US2] Extend `phase-9-harness/tools/run_interop_cell.py` + `emit_matrix.py` with the QFJ business-message cells (both roles) and business-message goldens (`golden/<cell>.fix`) normalizing non-deterministic fields — `52=`/`60=`/`34=`/IDs (`11/37/17`) **and decimal value-form drift** (`190.5`≡`190.50`, compared by `decimal_t` value, not byte). (US2 AC3; FR-012; N4.)

**Checkpoint US2**: live QFJ NOS→ExecRpt round-trip green both roles (when counterparty provisioned); `[const §VII.6]` discharged.

---

## Phase 5: User Story 3 — Live business-message interop vs QuickFIX-cpp, both roles (Priority: P3)

**Goal**: same round-trip vs QuickFIX-cpp, both roles (additive assurance).

### Tests for User Story 3 (write FIRST, confirm FAIL) ⚠️

- [X] T016 [P] [US3] Extend `tests/interop/test_business_message_interop.cpp` with the QFcpp business round-trip cells, both roles; skip-without-counterparty. (US3 AC1/AC2; FR-011/013.)

### Implementation for User Story 3

- [X] T017 [US3] Add the responding QuickFIX-cpp `Application` (ExecutionReport per NewOrderSingle, same fully-filled semantics) in `phase-9-harness/quickfix-cpp/counterparty/` (extend the 016 SSL counterparty program). (FR-011.)
- [X] T018 [US3] Add the QFcpp business-message cells + goldens to `run_interop_cell.py`/`emit_matrix.py` (mirror T015). (US3; FR-011/012.)

**Checkpoint US3**: live QFcpp round-trip green both roles (when provisioned).

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: catalogue/coverage closure, the forward-obligation deferrals the user asked for, the completeness gate, and the unfiltered-suite-green discipline (pre-`/simplify` / `/speckit-verify`).

- [X] T019 Catalogue + deferral bookkeeping (`[const §VI]`; [[feedback_feature_completeness_gate]]): in `spec/feature-catalogue.md` — A-001/A-006 **stay `backlog`** with a gap-note citing `020-g2-business-messages` (minimal FIX-4.4 NOS→ExecRpt live interop, both roles) as partial evidence (FR-014, **no done-flip**); in `spec/coverage-index.md` record the same as a partial-evidence note; in `spec/behaviors-and-limitations.md` add **L-020-1** (minimal-field-only, full FIX 4.4 field/group coverage deferred — FR-015a) and **L-020-2** (all-protocol-version coverage 4.2/5.0SP2/FIXT.1.1 **scheduled post-v1.0** — FR-015b) + the new send-path framing behavior (B-020-*: app sends now MsgType-field-3 + digit-only BodyLength); add both deferrals to the deferred-work registry; note `app_payload_malformed = 131` in the error taxonomy. Also add the one-line `.specify/architecture.md §4.4` amendment sanctioning `session/business_messages.hpp` as a hand-written body-builder bridge (per plan Structure Decision), OR record a waiver that `tools/check_layers.py` enforcement suffices and no prose amendment is needed.
- [X] T020 Feature-completeness audit ([[feedback_feature_completeness_gate]]; `[const §XVII.8]` precondition for `/gate-b`): FR-001..FR-016 (incl. FR-004a/FR-015a/FR-015b) ↔ task ↔ test; SC-001..SC-006 ↔ test (SC-003/004 live-cell-gated, SC-006 doc-gated); **named-test map: INV-1/2/3/4/5/7/8 ↔ named test (T005/T007); INV-6 (no NEW concurrency) is an architecture-assertion — no standalone test, satisfied structurally by reusing 019's send path**; exact-SET error-enumerator completeness (slot 131 present, no unexpected, per [[feedback_completeness_gate_exact_set_not_subset]]); catalogue gap-note + L-020-1/2 recorded. 100% or explicit waiver-with-rationale → record at `.specify/decisions/020-g2-business-messages-completeness.md` (gitignored). Depends T019.
- [X] T021 Unfiltered Tier-1 green discipline before `/simplify`/`/speckit-verify`: build + run the full ctest (UNFILTERED, or `-L sync` for the awaitable-corpus per [[feedback_awaitable_header_mutex_include_edge]]) with a clean `git status` (the codegen-build-graph-cleanliness gate, [[feedback_codegen_build_graph_cleanliness_gate]]); `-j2` cap, sanitizer presets one-at-a-time ([[feedback_build_resource_cap_oom]]). Record green.

---

## Dependencies & Execution Order

### Phase dependencies
- **Setup (P1)** → **Foundational (P2)** → **US1 (P3)** → **US2 (P4)** → **US3 (P5)** → **Polish (P6)**.
- US2 depends on US1 (the typed builders + send-path fix must exist before live cells). US3 depends on US1 (US3 is independent of US2 but shares the builders + interop test file, so sequence after US2 to avoid the same-file contention on `test_business_message_interop.cpp`).

### Within a story
- Tests (RED) → implementation (green). T008 (builder impl) is gated on **T005 RED**; T009/T010 (send_impl) are gated on **T007 RED** (INV-1 + INV-8 are their targets). T008 ∥ T009/T010 by file (builder vs send_impl), but **T009 (framing) and T010 (validation) touch the same `send_impl` so are sequential with each other**.

### Parallel opportunities
- T003 [P] (error slot) ∥ T002 (doc nits).
- T005 [P], T006 [P] (different test files) once T004 lands.
- T013 (QFJ Java) ∥ T014 (fixpp fixture) — different files/languages.
- T009 and T010 are **NOT** parallel (same `send_impl`).

## Implementation Strategy

### MVP first (User Story 1 only)
Ship Phase 1–3: the typed builders + read consumption + send-path framing fix + opaque-payload validation, proven by the in-process loopback round-trip. This is a self-contained, mergeable increment (records partial G2 evidence; does not need a live counterparty).

### Incremental delivery
US2 (live QFJ) discharges `[const §VII.6]`; US3 (live QFcpp) is additive. Both skip cleanly without a provisioned counterparty, so CI stays green; live runs happen at the interop/release tier.

### Build/verify discipline (this box)
- Conan deps + presets per [[feedback_conan_preset_build_infra_gotchas]]; if asan/ubsan/tsan fail logically while debug passes, rebuild `fixpp-codegen` + `rm _codegen` + reconfigure ([[project_codegen_emitter_staleness]]).
- The send-path change (T009/T010) is the highest-risk production edit — run ASan/UBSan/TSan on `test_business_messages_roundtrip` + the live-outbound suites before sign-off ([[feedback_gateb_full_sanitizer_before_signoff]]); a single-threaded harness masks strand/re-entrancy races ([[feedback_single_threaded_harness_masks_strand_races]]) — INV-7 MUST run multi-threaded.
- `codegraph sync` after code-changing tasks.

## Notes
- READ side adds **no production code** — it consumes the merged generated `fixpp::v44` flyweights; the only new prod surface is the two builders (`business_messages.{hpp,cpp}`), the `send_impl` framing/validation change, and error slot 131.
- The send-path framing fix (FR-004a) also corrects 019's latent opaque-path ordering for ALL app sends — keep the 019 opaque witness green.
- Live cells are tests-only/parent-harness (`phase-9-harness/`), reusing 016/018 infrastructure.
