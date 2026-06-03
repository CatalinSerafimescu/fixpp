---
description: "Task list — 019-app-callbacks (Application callback layer, Phase-5 slice 1)"
---

# Tasks: Application Callback Layer (Phase-5, slice 1)

**Input**: Design documents from `/specs/019-app-callbacks/`
**Prerequisites**: plan.md (required), spec.md (required), research.md (D1–D8), data-model.md (INV-1..7), contracts/application-interface.md, quickstart.md
**Branch**: `019-app-callbacks` | **Gate A**: converged round 3 (2 rewrites), 2026-06-04

**Tests**: TDD is REQUESTED (plan.md "every callback site + reject/veto/throw lands red-first"; `[const §VII.3]`). Each user story writes its tests FIRST (confirm FAIL) before implementation.

**Organization**: by user story (US1 inbound P1 → US2 outbound P2 → US3 lifecycle P3), preceded by Setup + Foundational, followed by the strand/throw cross-cutting phase, the G2 witness, and Polish.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: parallelizable (different files, no incomplete-task dependency)
- **[Story]**: US1/US2/US3 (user-story phases only)

---

## Phase 1: Setup (baseline re-verification)

**Purpose**: confirm every shipped seam the bundle wires into is still as research.md states, before writing code.

- [ ] T001 Re-verify the research.md "Existing seams (ground truth)" + Gate-A source claims on the branch base — record any drift as a bundle defect before coding: `dispatch_app_callback(F&&)` posts via `asio::post(exec_,…)` at `include/fixpp/session/session.hpp:319`; `executor()` valid only post-`open()` at `session.hpp:159-161`; `on_inbound_frame(std::span<const std::byte>)` runs on `exec_` at `session.hpp:233`; the **local** `const bool is_session_admin` + the default `build_reject`→`Reject(35=3)` branch for non-admin MsgTypes in `Active` at `src/session/session.cpp:~1924-1948`; `Session::send(std::span<const std::byte>) → asio::awaitable<expected_t<void>>` at `session.hpp:240`; `close(close_mode::{graceful,terminal})` at `session.hpp:116`; the `record_state_transition_` edge for `onLogon`/`onLogout`; `EngineConfig` carries **no** `application` field; next-free `error::` slot is **129** (121 `session_unknown_acceptor_session` + 017's 122–128) in `include/fixpp/core/error.hpp`; `admin_messages.hpp` ships 7 session-admin builders and **no** `BusinessMessageReject(35=j)`; `.specify/architecture.md §4.4` lists `Application` incl. `onCreate`; `tools/check_layers.py` grants `session → {core, dictionary, wire, transport, log, otel}` (no new module).
- [ ] T002 Fix the carried Gate-A P3: stale `INV-1..6` → `INV-1..7` label at `specs/019-app-callbacks/plan.md:55` (Project Structure documentation row; trivial doc-only).

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: the public surface + the reusable internals every user story builds on. MUST complete before US1–US3.

- [X] T003 [P] Create the public `Application` interface in `include/fixpp/session/application.hpp` — 7 `virtual` methods, **all default (0 pure-virtual)** per `[const §XIV.2]` + research D2: `onCreate/onLogon/onLogout(const SessionId&) → void`; `fromAdmin/fromApp(const wire::MessageView<access_mode::Index>&, const SessionId&) → core::expected_t<void>`; `toAdmin(...) → void`; `toApp(...) → core::expected_t<void>`. Header-minimalism: no `std::mutex`, no exception control-flow (`[const §XV.9]`). (FR-001/FR-015; contracts/application-interface.md.) DONE: `include/fixpp/session/application.hpp` created; compile-smoke + `check_no_std_mutex_corpus` pass.
- [X] T004 [P] Add error enumerators `app_do_not_send = 129` and `app_callback_threw = 130` in `include/fixpp/core/error.hpp` (append-only after 017's 128; cross-check the boundary stays 130 with no ±N drift; exact-SET completeness asserted at T022). (research D7; FR-007/FR-011.) DONE: slots added + `error_message()` entries added; 017 boundary test updated to expect slot 131 as unknown; new `test_019_error_completeness.cpp` pins slots 129/130 exactly; 5/5 tests pass.
- [X] T005 Add `std::shared_ptr<session::Application> application{nullptr}` to `EngineConfig` (`include/fixpp/core/engine_config.hpp`); `nullptr` ⇒ no callbacks, behaviour identical to pre-019 (FR-002). Forward-declare `Application` to avoid an awaitable-corpus include edge (`[const §XV.9]`; `[[feedback_awaitable_header_mutex_include_edge]]`). DONE: fwd-decl + field added; `check_no_std_mutex_corpus` passes; compile-smoke verifies field existence and null default.
- [X] T006 Factor the admin/app classifier — extract the inline `is_session_admin` (MsgType(35)) logic in `src/session/session.cpp` into a single reusable internal helper (the RC#3 prerequisite; research D8). **No behaviour change in this task** — the app-accept branch that consumes it lands in US1 (T011); here the classifier is just made reusable + unit-pinned so it is a single source of truth. DONE: `src/session/msgtype_classifier.hpp` with `detail::is_admin_msgtype()`; session.cpp uses it; `test_019_msgtype_classifier.cpp` pins all 6 admin + sample app + edge cases (4/4 tests pass).
- [X] T007 Add a reusable on-strand callback-dispatch RAII helper (e.g. `callback_dispatch_scope` in `session.hpp`, **release-safe** — the existing `dispatch_guard` is `#ifndef NDEBUG`-only and local to the `dispatch_app_callback` lambda) usable at the direct (D3) call sites, plus the `try{…}catch(...){ log; clear-guard; close(terminal); record app_callback_threw; }` throw→terminal-close wrapper scaffold (FR-011; research D3/D5; round-1 New finding on guard locality). Direct invocation preserves the strand invariant; `dispatch_guard` stays the debug-only check (INV-2 wording). DONE: `Session::callback_dispatch_scope` (release-safe RAII) + `Session::invoke_callback_safe<F>()` (static template throw-wrapper) added to `session.hpp`; all 4 wrapper test cases pass in compile-smoke.

**Checkpoint**: public `Application` + `EngineConfig::application` + slots 129/130 + reusable classifier + dispatch/throw scaffold exist; no behaviour change yet; corpus still green.

---

## Phase 3: User Story 1 — Receive inbound application & admin messages (Priority: P1) 🎯 MVP

**Goal**: surface inbound app messages to `fromApp` and admin messages to `fromAdmin` (after FSM accept), on the strand, with reject mapping. The irreducible core + half of G2.

**Independent Test**: register an `Application` on an engine-driven session, drive to `Active`, feed an inbound app frame + an inbound admin frame, assert `fromApp`/`fromAdmin` each fire exactly once on `exec_` with the right msg + `SessionId`; then reject each and assert the mapped peer reject (`BusinessMessageReject(35=j)` / `Reject(35=3)`) on the wire.

### Tests for User Story 1 (write FIRST, confirm FAIL) ⚠️

- [X] T008 [P] [US1] `tests/session/test_application_inbound.cpp` — `fromApp`/`fromAdmin` fire exactly once on `exec_` with the parsed `MessageView` + `SessionId` **after** FSM accept (FR-003/FR-004; INV-6: a session-FSM-invalid message never reaches them); **a multi-message ordering scenario** (feed two app frames A then B, assert `fromApp` fires in A→B arrival order — SC-001 "in arrival order"); `application==nullptr` ⇒ no invocation, behaviour unchanged (FR-002/INV-1); a `fromAdmin` reject ⇒ engine emits session `Reject(35=3)` (FR-005/INV-4). (US1 AC1/AC2/AC4; SC-001/SC-003.) DONE: 6/6 tests pass; fromApp/fromAdmin dispatch wired via T011; INV-6 + SC-001 + FR-014 + INV-4 cells all green.
- [X] T009 [P] [US1] `tests/session/test_application_business_reject.cpp` — **MANDATORY named test (Gate A RC#3)**: a `fromApp` reject ⇒ engine emits `BusinessMessageReject(35=j)` (with `RefMsgType(372)`/`RefSeqNum(45)`/`BusinessRejectReason(380)`) and **NOT** a session `Reject(35=3)`; an accept emits no reject. (US1 AC3; FR-005; SC-003; `[FIX50SP2] Infrastructure / Business Rejects`, catalogue A-014.) DONE: 2/2 tests pass; 35=j assertion + NOT 35=3 assertion + 372/45/380 field presence all verified.

### Implementation for User Story 1

- [X] T010 [US1] Add the `BusinessMessageReject(35=j)` builder in `include/fixpp/session/admin_messages.hpp` + `src/session/admin_messages.cpp` — same **stack-buffer** discipline as `build_reject` (stack `std::array<std::byte,…>`, zero-alloc per `[const §VIII.5]`); fixed/default reason code for slice 1 (per-error-code reasons deferred). (research D4; makes T009 pass.) DONE: `build_business_message_reject()` added; builds fields 35=j, 45, 372, 380 with stack-buffer Writer; no heap allocation.
- [X] T011 [US1] Wire inbound delivery in `src/session/session.cpp` `on_inbound_frame` — **after** the FSM accepts the frame, classify via T006 → `fromAdmin` (admin) / `fromApp` (app), invoked **directly on `exec_`** (D3) through the T007 dispatch scope + throw wrapper. Add the **app-accept branch** that suppresses today's default `Reject(35=3)` for a known application MsgType **when an `Application` is registered**; a `fromApp`/`fromAdmin` reject return-value emits the mapped reject (T010 / existing `build_reject`). Preserve FR-014 byte-identity when no `Application` is registered. (FR-003/FR-004/FR-005/FR-010; INV-4/INV-6.) DONE: fromAdmin dispatch at top of Active block (covers Heartbeat/TestRequest/Reject/ResendRequest); fromApp dispatch at bottom of guard-5 fallthrough; app-accept branch suppresses Reject(35=3) only when application!=nullptr; null application preserves pre-019 Reject; throw path calls close(terminal).

**Checkpoint**: inbound app + admin delivery with reject mapping works on the strand — the MVP (a peer business message is observable).

---

## Phase 4: User Story 2 — Originate & intercept outbound application messages (Priority: P2)

**Goal**: a public any-thread `Engine::send` that runs `toApp` (inspect/veto) before transmit; `toAdmin` on engine-originated admin emits (inspect-only). Completes the G2 round-trip.

**Independent Test**: call `co_await engine.send(id, app_payload)` and assert it crosses the wire after `toApp`; a `toApp` veto ⇒ not transmitted, session stays `Active`; an engine admin emit ⇒ `toAdmin` fires + still sent; send on a non-established session ⇒ defined error, nothing transmitted.

### Tests for User Story 2 (write FIRST, confirm FAIL) ⚠️

- [X] T012 [P] [US2] `tests/session/test_application_outbound.cpp` — `co_await Engine::send` crosses the wire after `toApp` fires (US2 AC1); a `toApp` veto (`error::app_do_not_send`) ⇒ 0 transmits, awaited result `unexpected(app_do_not_send)`, session `Active` (US2 AC2; FR-007/INV-5; SC-004); `toAdmin` fires before an engine-originated admin emit + the message is still sent (US2 AC3; FR-008); send on a non-established session ⇒ awaited `unexpected(session_invalid_state_for_send=77)`, nothing transmitted (US2 AC4; FR-013); unknown `SessionId` ⇒ `unexpected(session_invalid_argument=119)`. (research D6.) DONE: 8/8 tests pass; all AC1-AC4 + 3 extra cells (toApp other-error, Engine::send registered-but-unestablished, re-entrant send no-deadlock).

### Implementation for User Story 2

- [X] T013 [US2] Add `asio::awaitable<core::expected_t<void>> Engine::send(const SessionId&, std::span<const std::byte> app_payload)` in `include/fixpp/session/engine.hpp` + `src/session/engine.cpp` — any-thread: registry lookup capturing a **strong/owning session keepalive** that outlives the posted work (the 014 detached-write UAF class, `[[feedback_detached_cospawn_write_not_in_join_counter]]`), `asio::post` onto the target `exec_`, run `toApp` (veto/abort), then the existing `Session::send` durable-before-transmit path. Backpressure = the awaited result (no silent-drop queue — `[const §XV.15]`). Re-entrant `send` from inside an on-strand callback is enqueued behind the current dispatch (post-only ⇒ no deadlock). (FR-006/FR-007/FR-013.) DONE: Engine::send added; SessionEntry::session changed to shared_ptr for keepalive; toApp wired in Session::send_impl (after complete-frame build, before store_then_emit); Engine::send posts+awaits Session::send via co_spawn(use_awaitable).
- [X] T014 [US2] Wire `toAdmin` at **every engine-originated admin emit site** in `src/session/session.cpp` (each `store_then_emit` admin call — Logon / Logout / Heartbeat / TestRequest / ResendRequest / SequenceReset), inspect-only, admin **not** vetoable, **directly on `exec_`** via the T007 scope + throw wrapper. **The `toApp` call site lives in `engine.cpp` (T013, inside the posted closure on `exec_`) — T014 does NOT add a second `toApp` call in `session.cpp`** (that would double-invoke `toApp` for `Engine::send`-originated messages); T014's only `toApp`-related work is confirming the T007 dispatch scope wraps the T013 site. (FR-007/FR-008/FR-010.) DONE: `fire_to_admin_()` private helper added to Session; wired at 7 sites: initiator Logon, acceptor reply Logon, Logout-on-seqnum-mismatch, confirming Logout (Active), ResendRequest, Heartbeat echo × 2, liveness Heartbeat, liveness TestRequest, close() Logout, SequenceReset-GapFill.

**Checkpoint**: a user can originate + veto outbound app messages and observe outbound admin — G2 round-trip is drivable through the public surface.

---

## Phase 5: User Story 3 — Session lifecycle notifications (Priority: P3)

**Goal**: `onCreate` (post-`open()`, pre-Logon), `onLogon` (Active), `onLogout` (leaves established: graceful / terminal / callback-threw) — each once, in order, on the strand.

**Independent Test**: drive a session create → logon → logout under the engine; assert `onCreate`/`onLogon`/`onLogout` each fire exactly once, in order, on `exec_`, with the right `SessionId`.

### Tests for User Story 3 (write FIRST, confirm FAIL) ⚠️

- [X] T015 [P] [US3] `tests/session/test_application_lifecycle.cpp` — `onCreate` fires once **after `Session::open()` initializes `exec_`** and **before** first Logon (US3 AC1; FR-009); `onLogon` once at `Active` (AC2); `onLogout` once when the session leaves established — **one test per exit path: graceful close, terminal close, callback-threw** — asserting exactly-once across all three (AC3; FR-009; INV-7 lifecycle once-only). All on `exec_`, in order. Also: fromAdmin fires for inbound Logout(35=5) and inbound SequenceReset(35=4) [FR-004 completeness]; inbound Logout fires BOTH fromAdmin AND onLogout independently. DONE: 9/9 tests pass; all US3 AC1/AC2/AC3 + fromAdmin-completeness cells (Logout+SeqReset) green.

### Implementation for User Story 3

- [X] T016 [US3] Fire `onCreate` after `Session::open()` initializes `exec_` (pre first Logon) in `src/session/session.cpp` (end of open(), after exec_ init, via invoke_callback_safe); pin `onLogon`/`onLogout` to `record_state_transition_` `Active↔!Active` edge with fire-once `onLogon_fired_`/`onLogout_fired_` guards; `onLogout` fires on ANY `Active→!Active` transition (Active→LogoutSent for graceful, Active→Disconnected for terminal); `lifecycle_cb_threw_` flag lets coroutine callers terminal-close after onLogon throws. fromAdmin completeness: wire fromAdmin for inbound Logout(35=5) and SequenceReset-Reset-mode(35=4) via inline parse+invoke_callback_safe before `record_state_transition_`. On-strand via T007 scope + invoke_callback_safe. (FR-009; INV-7; FR-004.) DONE: 9/9 lifecycle tests pass; full regression 40/40 pass; all 019 tests 7/7 pass.

**Checkpoint**: full lifecycle observability layered cleanly on US1/US2.

---

## Phase 6: Cross-cutting — strand serialization, drain, and throw disposition (FR-010/011/012; SC-005)

**Purpose**: the sanitizer-critical invariants spanning all callbacks — the headline correctness target.

### Tests (write FIRST, confirm FAIL) ⚠️

- [ ] T017 [P] `tests/session/test_application_strand.cpp` — no two callbacks for one session run concurrently (TSan; INV-2 — the `exec_` strand is the real guarantee, `dispatch_guard` is the debug-only check); the Engine drains all dispatched callback work + any `Engine::send`-posted-but-not-run work before a `Session` is destroyed (`stop()`→`close(terminal)`+join-before-registry-clear), no callback runs against a freed session (ASan; FR-012/INV-3); `Engine::send`'s post holds the session keepalive so `stop()` racing a post is UAF-free (ASan); re-entrant `send` from inside an on-strand callback does not deadlock. (SC-005.)
- [ ] T018 [P] `tests/session/test_application_throw.cpp` — a throwing user callback at **every** site (`fromApp`/`fromAdmin`/`toApp`/`toAdmin`/`onCreate`/`onLogon`/`onLogout`) ⇒ the engine catches at the dispatch boundary, logs, clears the re-entrancy guard, terminal-closes the session, records `error::app_callback_threw`, and never propagates the exception inward or corrupts session state. (FR-011.)

### Implementation

- [ ] T019 Harden drain + keepalive in `src/session/engine.cpp` — `stop()` drains posted-but-not-yet-run `Engine::send` work; the registry holds a strong session ref outliving posted work; the T007 throw wrapper is applied at all 7 callback sites; an `application==nullptr` zero-delta regression witness confirms SC-006/INV-1 (existing session/engine suites unchanged). (FR-011/FR-012/FR-014; SC-005/SC-006.)

**Checkpoint**: callback serialization, drain, keepalive, and throw-disposition are proven (full ASan/UBSan/TSan matrix runs at `/speckit-verify` step 12 for SC-005).

---

## Phase 7: G2 enablement witness

- [ ] T020 [P] `tests/interop/` — a minimal `NewOrderSingle → ExecutionReport` **opaque-payload** round-trip witness driven through the public surface (one side `co_await engine.send(...)` an opaque app payload, the other observes via `fromApp`), proving G2 is **implementable** on top of this slice (SC-002). NOT the full typed QuickFIX interop cell (that — with A-001/A-006 typed messages — is the downstream feature).

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: catalogue closure, the completeness gate, and the unfiltered-suite-green discipline (pre-`/simplify` / `/speckit-verify`).

- [ ] T021 Add `L-019-1` (slice-1 limitation: outbound interception is inspect+veto only — in-place outbound **modification** deferred to a later Phase-5 slice; research D1) to `spec/behaviors-and-limitations.md`; add the `APP-001` (Application callback interface) row to `spec/feature-catalogue.md` + `coverage-index.md`; note slots `app_do_not_send=129` / `app_callback_threw=130` in the error taxonomy. (`[const §VI]`; `[[feedback_feature_completeness_gate]]`.)
- [ ] T022 Feature-completeness audit (`[[feedback_feature_completeness_gate]]`; `[const §XVII.8]` precondition for `/gate-b`): FR-001..FR-015 ↔ task ↔ test; SC-001..SC-006 ↔ test (SC-005 verify-gated); INV-1..INV-7 ↔ test; exact-SET error-enumerator completeness (129/130 present, no unexpected, per `[[feedback_completeness_gate_exact_set_not_subset]]`); catalogue `APP-001` + `L-019-1` recorded. 100% or explicit waiver-with-rationale. Depends T021.
- [ ] T023 Incremental `linux-clang-debug` build (`-j2`, OOM-cap `[[feedback_build_resource_cap_oom]]`) exit 0, then the **UNFILTERED** Tier-1 `ctest` (NOT `-R/-L 019`-scoped) on a clean tree — confirms no awaitable-corpus include-edge / `[const §XV.9]` regression from the `application.hpp`/`engine`/`session` edits (the `sync`-labelled corpus guard + the `#132 codegen-build-graph` git-cleanliness gate both run). (`[[feedback_awaitable_header_mutex_include_edge]]`; `[[feedback_codegen_build_graph_cleanliness_gate]]`. Full ASan/UBSan/TSan matrix runs at `/speckit-verify`.)
- [ ] T024 **Bench regression gate** (`[const §VIII.3]` — no perf change merged without a benchmark in the same PR; the inbound `on_inbound_frame` parse→`fromApp` dispatch + the new `Engine::send` outbound entry are hot/perf-sensitive paths): run the existing `bench/session/` suite on `linux-clang-release` and diff against `bench/baselines/session/*.json` with ±5% tolerance (`[const §VIII.2]`; mirrors the 009 T025 / 015 bench-diff pattern — a run-and-diff gate, NOT new benchmark authoring). The `[const §VIII.5]` zero-alloc claim is guaranteed by the D3 direct-invocation design (no queue-node alloc); this task confirms no dispatch-overhead regression. If a genuine regression appears, record it with rationale or fix before `/gate-b`.

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup (P1)** → **Foundational (P2)** → all user stories.
- **US1 (P3)** is the MVP; **US2 (P4)** depends on the Foundational `Engine::send`/classifier and conceptually on US1's wiring; **US3 (P5)** layers on US1/US2.
- **Cross-cutting (P6)** depends on all callback sites existing (T011/T014/T016).
- **G2 witness (P7)** depends on US1 (`fromApp`) + US2 (`Engine::send`).
- **Polish (P8)** last; T022 depends T021; T023 on a clean tree after all code lands.

### Within a story

Tests (write FIRST, confirm FAIL) → implementation → checkpoint.

### Parallel opportunities

- Foundational: T003 (`application.hpp`) ∥ T004 (error slots) — different files.
- Per story, the `[P]` test files are independent of each other and of other stories' tests.
- T017 ∥ T018 (different test files).

## Parallel Example: Foundational independent files

```
T003 include/fixpp/session/application.hpp   (new public interface)
T004 include/fixpp/core/error.hpp            (slots 129/130)
```

## Implementation Strategy

### MVP first (User Story 1 only)

Foundational (T003–T007) → US1 (T008–T011) delivers the irreducible callback core: a peer business message is observable via `fromApp` with reject mapping. Independently demonstrable.

### Incremental delivery

US1 (inbound + reject) → US2 (`Engine::send` + `toApp` veto + `toAdmin`) → US3 (lifecycle) → cross-cutting (strand/drain/throw) → G2 witness → Polish. Each adds value without breaking the prior.

### Build/verify discipline (this box)

- Resource cap `[[feedback_build_resource_cap_oom]]`: clang/build parallelism max `-j2`; the sanitizer presets + the 6-preset verify matrix run strictly ONE AT A TIME, sequentially.
- Headline correctness target: **callback serialization + drain + `Engine::send` keepalive + throw-disposition under the full ASan/UBSan/TSan matrix** (SC-005; the 014/015 teardown lessons + `[[feedback_gateb_full_sanitizer_before_signoff]]`). Every `co_spawn`/post must reset total cancellation or `stop()` hangs (`[[feedback_asio_cospawn_total_cancellation_default]]`).
- Pipeline per `.specify/pipeline.md`: this `/speckit-tasks` → `/speckit-analyze` (step 6) → `/speckit-checklist` + `/speckit-checklist-audit` (step 9, MANDATORY, blocks `/speckit-implement`) → `/speckit-implement` → `/simplify` (step 11) → `/speckit-verify` (step 12, unfiltered ctest) → `/gate-b` (step 14) → merge → step 19.

## Notes

- **D3 invocation model**: inbound/lifecycle/emit callbacks are invoked **directly on `exec_`** (zero-alloc `[const §VIII.5]`, no droppable queue `[const §XV.15]`); only the any-thread `Engine::send` posts. The `dispatch_app_callback` post seam is used only by `send`.
- **No new module**: `application.hpp` lives in `session/` (`.specify/architecture.md §4.4`); no `check_layers.py` ALLOWED-map change — re-confirm at `/speckit-implement` (`[[feedback_gate_b_check_layers_post_fixer]]`).
- **C ABI / config-file parsing / store-log factories / per-session Application override** are OUT of scope (later Phase-5 slices).
