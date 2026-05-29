---
description: "Task list — 014-transport-active-binding"
---

# Tasks: Live Transport Wiring — Reconnect, Identity Binding, Credential-Rotation Events

**Input**: Design documents from `specs/014-transport-active-binding/`
**Prerequisites**: plan.md, spec.md, research.md (R1–R6), data-model.md (E-1..E-5), contracts/ (error_slots.hpp + realized-behavior.md C1–C4)

**Tests**: INCLUDED — TDD red-green-refactor is mandatory per `[const §VII.1/§VII.3]` and plan §Testing. Write each story's tests first and confirm they FAIL before implementing.

**Paths**: repo root = the library submodule `research/G19-fix-fpml-iso20022/library/`. All `src/`, `include/`, `tests/`, `bench/` paths below are relative to that root.

**Scope guard**: 014 wires the **initiator** live path only; the public multi-session engine (continuous read-pump, connect-loop, registry, acceptor production path, test-seam removal, full T-041 closure) is **015** (spec Out of Scope §1; Clarifications Q2). NO new public *type*; **two** public-surface deltas: error slot `120` (FR-016) + the `TransportFactory::cert_source_snapshot()` concrete→abstract promotion (C4).

---

## Phase 1: Setup (Shared baseline verification)

**Purpose**: De-risk the realization by re-confirming the shipped-reality anchors the plan depends on (the plan re-verified every load-bearing claim against post-PR-#86 `main`).

- [X] T001 Re-verify the plan's shipped-reality baseline anchors on the branch base before coding — confirm: `error::session_invalid_argument = 119` is still the boundary (no ±N drift before assigning slot 120) in `include/fixpp/core/error.hpp`; the `make()`+`(void)t` discard stub at `src/session/reconnect_fsm.cpp:53-61`; the three-way Logon guard at `src/session/session.cpp:953-1008` (acceptor) / `:1757-1803` (initiator); `cert_source_snapshot()` concrete-only at `include/fixpp/transport/transport_factory.hpp:167-168`; the FSM-held `TransportFactory*` forward-declare at `include/fixpp/session/reconnect_fsm.hpp:152`/`:40-48`; `credentials_rotated` defined-not-emitted at `include/fixpp/session/session_event.hpp:102-105` + DEFERRED note `session.hpp:274`. Record any drift as a plan defect.
- [X] T002 [P] Confirm CodeGraph index is fresh (`codegraph sync` from the submodule) and the existing live-handshake fixtures `tests/tls/fixtures/leaf_rsa2048.pem` + `ca.pem` are present and addressable via `FIXPP_TLS_FIXTURE_DIR` (happy-path/counter/bench reuse them per data-model E-5).

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The contract-delta promotion + the shared live-loopback test harness that ALL of US1/US2/US3 depend on.

**⚠️ CRITICAL**: No user-story work can begin until this phase is complete — once `cert_source_snapshot()` becomes pure-virtual every test-local factory must override it or the whole suite stops compiling.

- [X] T003 Promote `cert_source_snapshot() const noexcept -> std::shared_ptr<fixpp::tls::cert_source>` from the concrete `asio_tls_transport_factory` to a **pure-virtual on the abstract `TransportFactory`** in `include/fixpp/transport/transport_factory.hpp` (count 2/5 → 3/5, under the `[const §XIV.2]` cap). The concrete asio override already exists (`src/transport/transport_factory.cpp:193`) — no body change, it now overrides an abstract method. (C4; plan §XIV.2)
- [X] T004 Add a trivial `cert_source_snapshot()` override (return the held source) to the test-local `TransportFactory` subclasses so the existing suite compiles. Exactly two exist (verified at /analyze): `tests/session/test_reconnect_happy_path.cpp:325` (`TestTransportFactory` — override **MISSING**, ADD it) and `tests/session/test_reload_credentials_in_flight.cpp:107` (`mock_transport_factory` — override **already present at `:137`**, VERIFY only). Re-confirm no new subclass appeared via `grep -rnE 'public.*TransportFactory' tests/`. (C4) — depends on T003.
- [X] T005 [P] Provide a shared live-loopback-TLS session test harness/helper (loopback acceptor + initiator `Session` wired to a real `asio_tls_transport_factory` + `ReconnectFsm`, fixtures via `FIXPP_TLS_FIXTURE_DIR`, inbound fed through the existing `Session::on_inbound_frame` seam `src/session/session.cpp:862`) reused by the US1/US2/US3 live tests — so US2/US3 do not depend on US1's test file. (research R2; plan §Testing)

**Checkpoint**: Base contract widened, suite compiles, live harness available — user stories can begin.

---

## Phase 3: User Story 1 — Initiator session self-heals after a dropped connection (Priority: P1) 🎯 MVP

**Goal**: Realize `ReconnectFsm::drive_reconnect_attempt` as a real bounded reconnect loop (connect → handshake → live transport → resume), replacing the 013 mint-then-discard stub. (FR-001..005; E-1; C1)

**Independent Test**: Over the loopback-TLS harness, drop the transport and observe a new connection + handshake + resume to Active under the `ReconnectPolicy` backoff/cap; drive failing peers (unreachable, TLS failure) and confirm each consumes one attempt and the loop terminates at the cap. Uses a permissive/override policy so US1 stands alone without US2's binding teeth.

### Tests for User Story 1 (write FIRST, confirm FAIL) ⚠️

- [ ] T006 [P] [US1] `tests/session/test_reconnect_live_happy_path.cpp` — drop → `drive_reconnect_attempt` → `async_connect` → `async_handshake(ssl_cfg)` → resume to `Active` over the loopback-TLS harness (FR-001/002; US1 AC1; SC-001).
- [ ] T007 [P] [US1] `tests/session/test_reconnect_backoff_cap.cpp` — connect-fail / handshake-fail / make-fail each consume exactly one attempt and retry per `delay_for_attempt(n)` to `max_attempts`, then `fsm_state::Disconnected`; no infinite retry; deterministic via `mock_clock` (FR-002/003; US1 AC2; SC-002). (Auth-fail cell added by US2 T013.)
- [ ] T008 [P] [US1] `tests/session/test_reconnect_cancel_mid_handshake.cpp` — `cancellation_type::total` mid-handshake aborts the attempt and releases the in-flight transport; ASan no-leak / no orphaned socket across N ≥ 3 cancelled attempts; exercises `enable_total_cancellation()` (FR-004; US1 AC3; SC-004; `[[feedback_asio_cospawn_total_cancellation_default]]`).

### Implementation for User Story 1

- [ ] T009 [US1] Realize `drive_reconnect_attempt` in `src/session/reconnect_fsm.cpp` (+ contract/doc updates in `include/fixpp/session/reconnect_fsm.hpp`): remove the `make()`+`(void)t` stub (`:53-61`); per attempt build the per-attempt `SslCtxConfig ssl_cfg` held in attempt scope (the `async_handshake` arg is `const&` `[[clang::lifetimebound]]`, `tls_transport.hpp:116-118` — never a temporary) → `make(exec, ssl_cfg, mr)` → `async_connect` → exactly one null-checked `dynamic_cast<fixpp::transport::TlsTransport*>(t.get())` (`tls_transport.hpp:61-67`) → `async_handshake(ssl_cfg)` → capture `handshake_result`; wrap in the `ReconnectPolicy` retry-to-cap loop (every make/connect/handshake failure = one attempt, back off, continue; exhaustion → `fsm_state::Disconnected`); honour `cancellation_state` with `enable_total_cancellation()` and RAII-release the partially-built transport on cancel. KEEP the `TransportFactory` forward-declare and do **NOT** `#include transport_factory.hpp` into the header (`[const §XV.9]`); correct the inherited-false doc-comments at `reconnect_fsm.hpp:20-23`/`:76-82`. Public return stays `asio::awaitable<expected_t<void>>` — no `reconnect_outcome` type. (FR-001..005; E-1 steps 1,3–6,8; C1; I-1/I-2)
- [ ] T010 [US1] Add the private handoff `Session::install_reconnected_transport(std::unique_ptr<Transport>, handshake_result, bound_principal)` in `src/session/session.cpp` (+ private decl in `include/fixpp/session/session.hpp`) and call it from the FSM success path: rebind `transport_send_` (`session.hpp:496`), store `handshake_result.peer_id` for the authorize site, re-enter `LogonSent` to re-drive Logon to `Active`. The three heavy values cross only this private `Session`-side surface in `session.cpp` (already includes the heavy headers) — never a `reconnect_fsm.hpp` signature. (FR-001; E-1 step 8; C1) — depends on T009.

**Checkpoint**: An initiator session self-heals end-to-end over loopback-TLS under a permissive/override policy — MVP complete and independently testable.

---

## Phase 4: User Story 2 — The live handshake identity drives the authorization decision (Priority: P1)

**Goal**: Swap the authorization identity **source** to the real `handshake_result.peer_id` on the live initiator path, making the already-fail-CLOSED mTLS gate *operable* (admit on-list, fail-close off-list/absent) and removing the fabricated stand-in on that path. (FR-006..008; E-2; C2)

**Independent Test**: On the live initiator path confirm the identity reaching `authorize()` is the real handshake `peer_id` (no fabricated payload); via the `logon_peer_identity_override` binding-logic seam drive on-list (admit) / off-list / absent (fail-closed: `session_compid_unauthorized` + `session_event_compid_authorization_failed`, not Active).

### Tests for User Story 2 (write FIRST, confirm FAIL) ⚠️

- [ ] T011 [P] [US2] `tests/session/test_live_identity_binding.cpp` — on the live initiator reconnect path the identity passed to `CompIdAuthorizationPolicy::authorize()` is the real `handshake_result.peer_id`; assert no fabricated/stand-in identity on the live path (FR-006; US2 AC1; SC-003; I-4).
- [ ] T012 [P] [US2] `tests/session/test_compid_binding_seam.cpp` — drive the `logon_peer_identity_override` seam (`session_config.hpp:224`): on-list → admit to Active; off-list / absent → fail-closed (`session_compid_unauthorized` + `session_event_compid_authorization_failed`), not Active; inherited 013 extraction order/shapes unchanged (FR-007; US2 AC2/AC3; SC-003).
- [ ] T013 [P] [US2] Extend `tests/session/test_reconnect_backoff_cap.cpp` with the **auth-fail** cell: an off-list/absent identity under a binding policy consumes exactly one attempt and retries to the cap (reason-agnostic, NOT terminal-early, NO new code/cap) (FR-003/FR-007; Clarifications Q1; research R4; US1 AC2) — extends T007's file (author after T007 lands; the `[P]` is relative to T011/T012 only).

### Implementation for User Story 2

- [ ] T014 [US2] In `src/session/reconnect_fsm.cpp` thread the captured `handshake_result.peer_id` into the reconnect-path authorization decision (E-1 step 7): on a binding-policy fail-closed emit the inherited `session_event_compid_authorization_failed` + `session_compid_unauthorized`, release the transport, **count one attempt and continue** (retry-to-cap, NOT the terminal `Disconnected` of 013's open-Logon path); only loop-exhaustion is terminal. Pass `bound_principal` to the success handoff. (FR-006/007; E-1 step 7; E-2; C2; Q1) — depends on T009/T010.
- [ ] T015 [US2] In `src/session/session.cpp` add the live-identity arm to the three-way guard at the **initiator** site `:1757-1803`: identity SOURCE = FSM-held live `handshake_result.peer_id` ahead of the override-seam arm; keep the override-seam arm + the `else if (is_mtls)` fail-CLOSED arm + the non-mTLS permissive skip; remove the residual fabricated auth payload on the live path (FR-008). KEEP the `logon_peer_identity_override` seam. The acceptor site `:953-1008` stays seam-only — **document the acceptor live-binding deferral to 015** (clarified asymmetry per `[[feedback_half_restructure_symmetric_api]]`; T-041 stays `implementing`). (FR-006/007/008; E-2; C2) — depends on T014.

**Checkpoint**: Under a binding policy the live identity admits the on-list peer and fails closed otherwise on the initiator path; auth-fail retries to cap.

---

## Phase 5: User Story 3 — Operators observe genuine credential rotation (Priority: P2)

**Goal**: Emit exactly one real `credentials_rotated{old_sha256, new_sha256}` on the session strand at the next `drive_reconnect_attempt` after `reload_credentials`, before `make()`, with real leaf SHA-256 fingerprints. (FR-009..011; E-3; C3)

**Independent Test**: Stage a new `cert_source` via `reload_credentials`, force a reconnect, confirm one `credentials_rotated` on the strand before the rotated cert is used carrying the real old/new leaf fingerprints; a no-op rotation still emits with `old==new`.

### Tests for User Story 3 (write FIRST, confirm FAIL) ⚠️

- [ ] T016 [P] [US3] `tests/session/test_credentials_rotated_emit.cpp` — after `reload_credentials`, exactly one `credentials_rotated` emitted on the session strand BEFORE `make()`, carrying the REAL SHA-256 leaf fingerprints (not the 013 all-zero stub); first-ever load emits NO event; no-op rotation emits `old==new` (not suppressed). All cells run over the live loopback harness (the real-fingerprint cells require it) — depends on T005. (FR-009/010/011; US3 AC1/AC2; SC-005; I-5/I-6).

### Implementation for User Story 3

- [ ] T017 [US3] Add FSM-held rotation-detect state to `include/fixpp/session/reconnect_fsm.hpp` + `src/session/reconnect_fsm.cpp` — `last_active_source_ : std::shared_ptr<fixpp::tls::cert_source>` (strong-ref owning member, `[[feedback_weak_ptr_cache_needs_owning_context]]`) + `last_active_fp_ : std::array<std::byte,32>`; in `drive_reconnect_attempt` step 2 read `factory_->cert_source_snapshot()` through the abstract pointer, detect rotation (`snap != last_active_source_`), compute the new leaf SHA-256 via OpenSSL over the loaded `local_credentials` leaf DER, invoke the injected strand-bound emit callback **before** `make()`, then update both members; first load = no event; no-op still emits (FR-009/010/011; E-1 step 2; E-3; C3; I-7) — depends on T009.
- [ ] T018 [US3] Wire the `Session`-injected strand-bound `emit_credentials_rotated_ : std::function<void(session_event_credentials_rotated)>` at `ReconnectFsm` construction in `src/session/session.cpp` (+ `include/fixpp/session/session.hpp`): `Session` performs the actual on-strand emit of `SessionEvent::credentials_rotated` when the FSM detects a rotation; resolve the `session.hpp:274` "DEFERRED to 014" comment (FR-009; E-1; E-3; C3; `[const §XI.4]`) — depends on T017.

**Checkpoint**: Genuine credential rotation is observable with real fingerprints.

---

## Phase 6: User Story 4 — Carry-forward hardening discharged (Priority: P3)

**Goal**: Close the five 013/012 Gate-B carry-forward waivers, now witnessable against the live TLS fixture. (FR-012..016; E-4/E-5; SC-006)

**Independent Test**: Each obligation has its own passing witness; the fuzz-scope catalogue entry is re-labelled. FR-012/014/015/016 are independent of US1–US3; FR-013 needs the live harness.

### FR-012 — sigalg_disallowed cell

- [ ] T019 [P] [US4] Generate the unknown-`EVP_PKEY` leaf fixture(s) `tests/tls/fixtures/leaf_ed25519.pem` (and/or `leaf_ed448.pem`), CA-signed as needed for the loopback path (E-5).
- [ ] T020 [US4] Add the `sigalg_disallowed` `sub_reason` cell to `tests/transport/test_tls_validation_failed_taxonomy.cpp` exercising the `[const §XII.3]` signature-algorithm allow-list rejection against the Ed25519/Ed448 fixture (FR-012; SC-006) — depends on T019.

### FR-013 — once-per-handshake counter + handshake bench

- [ ] T021 [P] [US4] Re-target `tests/session/test_session_invariant_counter_witness.cpp` (today infeasible/zero under `mock_transport`, `:22-35`) at the live loopback fixture so `cert_source::load_credentials()` == 1 per handshake is genuinely asserted (FR-013a; I-3; SC-006) — depends on T005.
- [ ] T022 [P] [US4] Wire the `bench/transport/bench_tls_handshake_loopback.cpp` scaffold (its in-file TODOs at `:16`/`:42-49`/`:52-67`) to the live `asio_tls_transport_factory` + loopback acceptor using `leaf_rsa2048.pem` + `ca.pem` (`:44`); CMake target `bench_tls_handshake_loopback` (`bench/transport/CMakeLists.txt:22`); establish the first real 1-RTT handshake baseline (FR-013b; SC-006; plan §Performance).

### FR-014 — PMR-OOM witness depth

- [ ] T023 [P] [US4] Generate the multi-SAN leaf fixture `tests/tls/fixtures/leaf_multi_san.pem` (≥2 SAN-DNS entries) (E-5).
- [ ] T024 [US4] Extend `tests/transport/test_verify_peer_pmr_oom.cpp` with the multi-SAN fixture so `throw_on_nth_resource` exhausts at the **mid** (SAN-DNS construction) and **tail** sites, not only the boundary (`N=1`) (FR-014; `[[feedback_trap_throw_pmr_witness_enumerate_sites]]`; SC-006) — depends on T023.

### FR-015 — fuzz scope re-label

- [ ] T025 [P] [US4] Re-label the `tests/fuzz/fuzz_transport_handshake.cpp` catalogue/scope entry to its actual post-MVP scope — doc/catalogue only, NO harness body change, no new parser-touching code (FR-015; SC-006).

### FR-016 — slot-74 stand-in cleanup

- [ ] T026 [P] [US4] Append `error::session_seqnum_too_high = 120` in `include/fixpp/core/error.hpp` after `session_invalid_argument = 119` (cross-check boundary still 119, no drift; never renumber; slot 70 stays a permanent hole; slot 74 keeps its real meaning) (FR-016; E-4; contracts/error_slots.hpp; `[const §X.4]`).
- [ ] T027 [US4] Change the too-high branch at `src/session/seqnum_manager.cpp:71-78` to `co_return std::unexpected(error::session_seqnum_too_high)` + update the comment (and any in `seqnum_manager.hpp`); flip the assertion at `tests/session/seqnum_manager_test.cpp:145-150` to expect `session_seqnum_too_high`; update the contract note. Zero behavioural change (the 3 callers `session.cpp:904/1261/1703` discard the code) (FR-016; E-4; I-8) — depends on T026.

**Checkpoint**: All five carry-forward witnesses pass; fuzz entry re-labelled.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Catalogue closure, completeness gate, and the SC-007 suite-green discipline (pre-`/simplify`/`/speckit-verify`).

- [ ] T028 Update `feature-catalogue.md` + `coverage-index.md` with the 014 row; flip the FR-012/013/014/015/016 carry-forward rows `waived`/`carry-forward` → `done`; record **T-041 stays `implementing`** (advanced on the initiator live path, full closure → 015) (`[[feedback_feature_completeness_gate]]`; plan §Catalogue rows).
- [ ] T029 Run the feature-completeness audit (tasks ↔ FR/SC ↔ catalogue, 100% or explicitly waived) — Gate-B precondition per `[const §XVII.8]` / `[[feedback_feature_completeness_gate]]` — depends on T028.
- [ ] T030 [P] Run the quickstart.md walkthroughs (US1 reconnect, US2 binding, US3 rotation, US4 witnesses) and confirm each command/behaviour matches.
- [ ] T031 Run the **unfiltered** Tier-1 ctest (and `-L sync`) — the suite-green claim must NOT come from a name-scoped `-R` subset (SC-007; new includes into the awaitable-corpus headers `reconnect_fsm.hpp`/`session.hpp` per `[[feedback_awaitable_header_mutex_include_edge]]`; the `8e2d362` regression class).
- [ ] T032 [P] Re-sync the CodeGraph index from the submodule (`codegraph sync`) after the code-changing phases so search/impact stay accurate (project CLAUDE.md).

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup (P1)** → no deps.
- **Foundational (P2)** → after Setup. T003 → T004; T005 independent. BLOCKS all stories.
- **US1 (P3)** → after Foundational. **MVP.**
- **US2 (P4)** → after US1 (shares `reconnect_fsm.cpp`/`session.cpp`; needs the live handshake `peer_id` source from US1).
- **US3 (P5)** → after US1 (injects the rotation emit into `drive_reconnect_attempt`; shares `reconnect_fsm.cpp`/`session.cpp`).
- **US4 (P6)** → FR-012/014/015/016 independent of US1–US3 (run anytime after Setup); FR-013a (T021) needs the Foundational live harness (T005); FR-013b (T022) stands up its own loopback in `bench/transport/` (independent of T005).
- **Polish (P7)** → after all desired stories.

> **Honest cross-story coupling**: the spec states US1 is the central value — "until this works… every other behaviour here is unreachable." US2/US3 therefore genuinely depend on US1's live path (not artificial). US4's FR-012/014/015/016 are the only truly independent slice and can proceed in parallel with US1–US3.

### Within a story

- Tests written and FAILING before implementation.
- `reconnect_fsm.cpp` body (T009) before the `session.cpp` handoff/guard wiring that consumes it (T010/T014/T015) and before the rotation injection (T017).

### Parallel opportunities

- Setup: T002 ‖ T001.
- Foundational: T005 ‖ (T003 → T004).
- US1 tests: T006 ‖ T007 ‖ T008.
- US2 tests: T011 ‖ T012 ‖ T013.
- US4: T019 ‖ T021 ‖ T022 ‖ T023 ‖ T025 ‖ T026 (distinct files); then T020 (after T019), T024 (after T023), T027 (after T026).
- Polish: T030 ‖ T032.

---

## Parallel Example: User Story 1 tests

```bash
# Write the three US1 test files together (different files, no deps):
Task: "tests/session/test_reconnect_live_happy_path.cpp — drop → reconnect → handshake → resume"
Task: "tests/session/test_reconnect_backoff_cap.cpp — connect/handshake/make fail → one attempt each → cap"
Task: "tests/session/test_reconnect_cancel_mid_handshake.cpp — total-cancel mid-handshake → release, ASan no-leak"
```

## Parallel Example: User Story 4 (independent carry-forwards)

```bash
Task: "tests/tls/fixtures/leaf_ed25519.pem (FR-012 fixture)"
Task: "tests/tls/fixtures/leaf_multi_san.pem (FR-014 fixture)"
Task: "wire bench/transport/bench_tls_handshake_loopback.cpp (FR-013b)"
Task: "re-label tests/fuzz/fuzz_transport_handshake.cpp catalogue scope (FR-015)"
Task: "append error::session_seqnum_too_high = 120 in include/fixpp/core/error.hpp (FR-016)"
```

---

## Implementation Strategy

### MVP first (User Story 1 only)

1. Phase 1 Setup → 2. Phase 2 Foundational (CRITICAL — base widening + harness) → 3. Phase 3 US1 → **STOP & VALIDATE** the self-heal flow over loopback-TLS (permissive/override policy) → MVP.

### Incremental delivery

US1 (self-heal) → US2 (binding teeth on the live identity) → US3 (rotation observability) → US4 (carry-forward witnesses). US4's FR-012/014/015/016 can land in parallel at any point after Setup.

### Build/verify discipline (this box)

- Resource cap per `[[feedback_build_resource_cap_oom]]`: clang/build parallelism max `-j2`; sanitizer presets + the verify matrix run ONE AT A TIME, sequentially.
- TSan is central (reconnect loop + cancel-release + cross-strand `credentials_rotated` emit); SC-004 is the ASan no-leak witness.
- `/simplify` (step 11) → `/speckit-verify` (step 12, unfiltered ctest per SC-007) → Gate B (step 14) follow this `/tasks` step per `.specify/pipeline.md`.

---

## Notes

- `[P]` = different files, no dependency on an incomplete task.
- `[Story]` labels apply to US phases only (Setup/Foundational/Polish carry none).
- 014 adds NO new public type; the two public-surface deltas (slot 120 + `cert_source_snapshot()` promotion) are both Gate-A-blessed and captured in `contracts/`.
- Commit after each task or logical group (the optional `before_tasks`/`after_tasks` git-commit hooks are available via `/speckit-git-commit`).
