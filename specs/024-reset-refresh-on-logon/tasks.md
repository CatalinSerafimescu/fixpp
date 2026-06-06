---
description: "Task list for 024-reset-refresh-on-logon"
---

# Tasks: ResetOn{Logon,Logout,Disconnect} Sequence-Number Lifecycle Knobs (S-017, G3 slice 3)

**Input**: Design documents from `specs/024-reset-refresh-on-logon/`
**Prerequisites**: plan.md, spec.md, research.md (D1–D6), data-model.md, contracts/reset-knobs.md (C1–C7), quickstart.md
**Repository root** = the library submodule (`research/G19-fix-fpml-iso20022/library/`). All paths below are submodule-relative.

**Tests**: REQUIRED and RED-first — `[const §VII]` TDD is binding for this codebase. Every behavior lands as a failing GoogleTest witness before the production change.

**Scope reality** (from plan.md Complexity Tracking + research D1–D6):
- Three additive `SessionConfig` bool knobs (default `false`) trigger a durable seqnum reset to `{1,1}` at **three existing FSM transition sites**, reusing the `013` `reset_to_one()` + `MessageStore::reset()` primitive via a new `reset_seqnums_to_one_durable()` helper. **All-off ⇒ pure no-op** (zero regression to existing sessions and the `013` `ResetSeqNumFlag(141)` flow).
- The single load-bearing wire-behavior delta: the initiator emits `ResetSeqNumFlag(141)=Y` via the **OR-of-three** predicate (`(reset_on_logon||reset_on_logout||reset_on_disconnect) && {1,1}`), matching QFcpp `shouldSendReset()` / QFJ `isResetNeeded()`.
- **`RefreshOnLogon` (S-018) is OUT of scope** (descoped — manager not store-seeded at `open()`; C7.1). Do not add any refresh / store-hydrate code or test.

**Gate A convergence notes baked into the tasks** (do not regress): (a) acceptor reset MUST run **before** `check_inbound` (too-low fatal in handshake); (b) `reset_on_logout` keys on a dedicated **`logout_seen_`** flag (NOT `onLogout_fired_`, which fires on any `Active→!Active`); (c) durable-helper disposition is **cause-keyed** (knob-driven Logon reset = fatal; existing 013-only received-`141` reset = I-07 logged-then-proceed; teardown = logged); (d) acceptor Logon overlap collapses to one `need_logon_reset` → one store reset; (e) teardown double-trigger collapses via a single-fire guard (`FileStore::reset()` is non-idempotent I/O).

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Build-graph registration for the new unit suite.

- [X] T001 Register a new GoogleTest executable in `tests/session/CMakeLists.txt`, mirroring the 021/022 pattern (`session_inbound_poss_dup_tolerance` / `session_send_allow_pos_dup_strip`): `session_reset_on_lifecycle` (source `test_reset_on_lifecycle.cpp`), linking `fixpp_session` + `fixpp_mock_clock` + `$<TARGET_OBJECTS:session_test_support>` + `GTest::gtest`/`gtest_main`, including `${CMAKE_SOURCE_DIR}/tests`, compiling `FIXPP_TEST_HOOKS` (required — the `set_counters_for_test()` seed-non-1 seam is gated behind it), setting the `TSAN_OPTIONS` env + asio suppression, tagged `LABELS "024;s017;reset-knobs"`. Create empty placeholder `tests/session/test_reset_on_lifecycle.cpp` so the build graph configures. **Also extend the shared mock store** (`tests/support/store_double.hpp` `StoreDouble`) with two test seams: (a) a `reset_call_count()` accessor backed by a counter incremented in `reset()` — the single-fire-guard witnesses (T005(7), T010(5)) assert **exactly one** `store_->reset()`, and the current double clears state without counting calls (analyze C2); (b) a "fail next reset" toggle (e.g. `fail_next_reset()`) making `reset()` return an `expected` error once — for the durable-disposition witnesses (T004(5) all-off I-07 → still-Active; T005(9) knob fatal → blocks-Active). → verify: `cmake` configures clean; the target builds (empty); `StoreDouble::reset_call_count()` compiles + starts at 0; the fail-toggle returns an error on the next `reset()` then clears.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The additive public-surface fields and the shared durable-reset helper both user stories build on.

**⚠️ Note**: T002 adds the knob *values only* (default `false`); T003 adds the helper but wires **no trigger** — so the behavioral witnesses in US1/US2 are RED on *behavior* (no reset fires), not on a compile error.

- [X] T002 Add three additive POD fields — `bool reset_on_logon = false;`, `bool reset_on_logout = false;`, `bool reset_on_disconnect = false;` — to `include/fixpp/session/session_config.hpp` immediately next to `reset_seqnum_policy_field` (the related 013 wire-handshake knob), each with the data-model §"Additive config fields" doc-comment (QuickFIX cfg-key parity for NAMING only — NOT parsed by `cfg_loader` this slice, C1.2; default-off no-implicit-default `[const §XII.5]`; struct-layout/source-rebuild note). No behavior change. (data-model §1; research D5; contract C1.1) → verify: library builds; the three fields are public default-`false` members; no other field touched; no `cfg_loader` change.
- [X] T003 Implement the shared durable-reset helper `reset_seqnums_to_one_durable(disposition)` in `src/session/session.cpp` (private `Session` coroutine): `co_await seqnum_mgr_.reset_to_one()` **then** `co_await store_->reset()`, with a **cause-keyed** store-failure disposition parameter — `fatal` (propagate the error → caller blocks reaching `Active`) for the knob-driven Logon path, `logged` (I-07 logged-then-proceed) for teardown. Does NOT change the existing 013 received-`141` reset yet (that stays its current inline I-07 form until T008 folds it into the combined acceptor decision). No trigger wiring. (research D2; data-model §"Durable reset helper"; contract C2.6) → verify: library builds; helper is callable, unused (or referenced only by a compile-time check); no behavior change yet; all-off path untouched.

**Checkpoint**: Foundation ready — both user stories can proceed.

---

## Phase 3: User Story 1 — Reset sequence numbers at Logon (Priority: P1) 🎯 MVP

**Goal**: With `reset_on_logon`, the initiator resets to `{1,1}` before building its Logon and announces it via `141=Y` (OR-of-three predicate); the acceptor resets **before** `check_inbound` so a fresh peer `34=1` at a local-expected>1 acceptor is admitted — both durable, both roles, all-off byte-identical.

**Independent Test**: Seed non-1 seqnums, `reset_on_logon=true`, drive a Logon; assert `{1,1}` + persisted + `141=Y` on the initiator's Logon + `Active` + no ResendRequest below the reset point; acceptor admits `34=1` at local-expected>1 with no disconnect.

### Tests for User Story 1 (RED-first)

> Author all US1 witnesses in `tests/session/test_reset_on_lifecycle.cpp` (same file ⇒ T004/T005 sequential). They are the quickstart US1 rows + the disposition-table initiator/acceptor rows.

- [X] T004 [US1] Write the **initiator** ResetOnLogon witnesses in `tests/session/test_reset_on_lifecycle.cpp`: (1) **`ResetOnLogon_Initiator_ResetsAndEmits141`** — seed seqnums non-1 (`set_counters_for_test`), `reset_on_logon=true`, open as initiator → captured outbound Logon has `MsgSeqNum=1` AND `141=Y`; persisted counters == 1 (C2.1, C2.6 / **SC-001, SC-004**); (2) **`ResetOnLogon_Off_No141Beyond013`** — `reset_on_logon=false`, policy `bilateral_lenient` → outbound Logon has NO `141=Y` (today's behavior), no logon-time reset (C2.3); (3) **`ResetOnLogon_AllPolicies_LogonClean`** — for each `reset_seqnum_policy_field` ∈ {strict, lenient, unilateral} with `reset_on_logon=true` → reaches `Active`, no wedge (C2.4); (4) **`ResetOnLogon_SuppressesPreResetGap`** — seed a pre-reset inbound gap, `reset_on_logon=true`, logon → no ResendRequest for seqnums below the reset point (C2.5 / FR-003); (5) **`ResetOnLogon_Off_Inbound141_StoreFailure_StillActive`** — knobs OFF, inbound Logon carries `141=Y`, force a `store_->reset()` failure → session STILL reaches `Active` (proves the existing 013 received-`141` path keeps I-07 logged-then-proceed; all-off zero-regression, C2.6 / FR-001). Reuse the 021/022 `session_test_support` fixtures + `extract_field`/`peek_outbound`.
- [X] T005 [US1] Write the **acceptor** ResetOnLogon witnesses in the same file: (6) **`ResetOnLogon_Acceptor_AdmitsFresh34eq1_LocalExpectedGt1`** — acceptor local next-expected > 1, inbound `34=1`, `reset_on_logon=true` → no too-low disconnect, no ResendRequest, reaches `Active`, seqnums `{1,1}` (C2.2 / FR-002/FR-003 / **SC-001**); (7) **`ResetOnLogon_Acceptor_ResetsIdempotentWith141`** — `reset_on_logon=true` + inbound `141=Y`: single combined `need_logon_reset` → assert **exactly one** observable `store_->reset()` I/O (via the T001 `reset_call_count()` seam), result `{1,1}` (C2.2, C5.1); (8) **`ResetOnLogon_BothRoles_Resets`** — the reset holds as initiator AND acceptor (C5.2); (9) **`ResetOnLogon_DurableStoreFailure_BlocksActive`** — `reset_on_logon=true` (knob-driven → **fatal** disposition), force a `store_->reset()` failure on the Logon path → the session does NOT reach `Active` (the error propagates), the dual of T004(5)'s all-off I-07 arm (C2.6 / FR-008 fatal arm; analyze C1). Use the T001 `StoreDouble` `reset_call_count()` + a store-failure injection seam (add a thin "fail next reset" toggle to `StoreDouble` if absent — note which).
- [X] T006 [US1] Build + run `session_reset_on_lifecycle`; **confirm RED** for the behavioral witnesses (knob value exists, helper exists, but no trigger wired → no reset/141 fires; the acceptor fresh-`34=1` disconnects). The all-off regression witness (5) should already PASS (it characterizes the unchanged 013 path). → verify: failures are *behavioral* not compile errors — assert the specific RED signals (no `141=Y` in the captured Logon; acceptor disconnects on fresh `34=1`); if it fails to compile, fix fixture wiring first.

### Implementation for User Story 1

> Production sites in `src/session/session.cpp`: the `open()` initiator arm (~:519-561) and the inbound-Logon acceptor handler (~:1437-1592). Sequential where same-region.

- [X] T007 [US1] Implement the **initiator** ResetOnLogon in `Session::open()` initiator arm (`src/session/session.cpp` ~:519-561): when `cfg_.reset_on_logon`, `co_await reset_seqnums_to_one_durable(fatal)` **before** `peek_outbound()` (`:522`) so the Logon carries `MsgSeqNum=1`; extend the outbound-Logon `141` flag from `send_reset_flag = (reset_seqnum_policy_field == bilateral_strict)` (`:523-526`) to `send_reset_flag || ((reset_on_logon || reset_on_logout || reset_on_disconnect) && seqnums=={1,1})` — the OR-of-three predicate, where the `{1,1}` guard MUST be evaluated against the **post-reset live manager state** (e.g. `seqnum_mgr_.peek_outbound() == seqnum_min` AND `next_inbound == seqnum_min`) AFTER `reset_seqnums_to_one_durable()` completes — never via a value sampled before the reset (analyze B1). (research D1/D3; data-model table initiator row + precedence rule 2; contract C2.1 / **SC-004**) → verify: T004 witnesses (1)(2)(3)(4) GREEN; off-default emits `141` iff `bilateral_strict`.
- [X] T008 [US1] Implement the **acceptor** ResetOnLogon in the inbound-Logon handler (`src/session/session.cpp` ~:1437-1592): compute `peer_sent_reset` (from `hdr.reset_seqnum_flag == "Y"`), then `need_logon_reset = cfg_.reset_on_logon || peer_sent_reset`; when set, `co_await reset_seqnums_to_one_durable(<fatal if reset_on_logon else logged>)` **BEFORE** `check_inbound(seq)` (`:1437`) — a single combined reset that **subsumes** the existing `:1584` `141`-receipt reset (remove/guard the old `:1584` reset so it does not run a second `store_->reset()`); reply Logon mirrors `141=Y` (existing path). Preserve the all-off path exactly: when `reset_on_logon==false` and `peer_sent_reset` via received `141=Y`, the disposition stays `logged` (I-07) — byte-identical to today. (research D2/D3; data-model acceptor row; contract C2.2, C5.1) → verify: T005 witnesses (6)(7)(8) GREEN; T004 (5) still GREEN (all-off 013 path unchanged); exactly one store reset on overlap.
- [X] T009 [US1] Build + run `session_reset_on_lifecycle` US1 subset GREEN; confirm the all-off regression witness (5) stays GREEN and no existing 005/013 session/seqnum test regressed (run the `session` + `seqnum` labels) — this is the **SC-003** all-off byte/semantics-identity gate. → verify: US1 witnesses green; `ctest -L "session|seqnum"` no regression.

**Checkpoint**: US1 complete — ResetOnLogon resets + announces `141=Y` (both roles), acceptor admits fresh `34=1`, all-off byte-identical.

---

## Phase 4: User Story 2 — Reset at Logout and at Disconnect (Priority: P2)

**Goal**: `reset_on_logout` resets at a Logout teardown in EITHER direction (sent or received); `reset_on_disconnect` resets on ANY disconnect incl. abnormal drop; both durable, both roles, double-trigger collapses to one store reset; and a `reset_on_logout`/`reset_on_disconnect` session that reset to `{1,1}` at a prior teardown announces it via `141=Y` on its next initiator Logon (the OR-of-three predicate from T007).

**Independent Test**: Drive a session to `Active` with non-1 seqnums; (a) local graceful Logout, (b) peer-initiated Logout, (c) abnormal transport drop — assert persisted counters == 1 when the respective knob is on, unchanged when off; double-trigger → exactly one store reset.

### Tests for User Story 2 (RED-first)

> All US2 witnesses in `tests/session/test_reset_on_lifecycle.cpp` (same file ⇒ T010/T011 sequential).

- [X] T010 [US2] Write the **teardown reset** witnesses (**SC-002** — logouts/disconnects leave `{1,1}`): (1) **`ResetOnLogout_LocalInitiated_Resets`** — `reset_on_logout=true`, local graceful Logout (`close(graceful)`) → persisted counters == 1 (C3.1); (2) **`ResetOnLogout_PeerInitiated_Resets`** — `reset_on_logout=true`, inbound `35=5` (peer Logout, which transitions inline to `Disconnected` ~:2095 → `close(terminal)`) → persisted counters == 1 (C3.1 — the half a `close(graceful)`-only key would miss); (3) **`ResetOnDisconnect_AbnormalDrop_Resets`** — `reset_on_disconnect=true`, raw transport EOF (no Logout) → persisted counters == 1 (C4.1, C4.2); (4) **`ResetOnLogout_Off_Preserves`** / **`ResetOnDisconnect_Off_Preserves`** — off ⇒ counters preserved (C3.2, C4.3); (5) **`ResetOnLogoutAndDisconnect_DoubleTrigger_OneStoreReset`** — both on, graceful Logout then disconnect → assert **exactly one** observable `store_->reset()` (single-fire guard, C5.1); (6) **`ResetOnLogout_BothRoles_Resets`** + **`ResetOnDisconnect_BothRoles_Resets`** (C5.2).
- [X] T011 [US2] Write the **logout/disconnect → 141-on-next-Logon** witnesses (these exercise the T007 OR-of-three predicate through the US2 knobs): (7) **`ResetOnLogout_NextInitiatorLogon_Emits141`** and (8) **`ResetOnDisconnect_NextInitiatorLogon_Emits141`** — a session whose seqnums are `{1,1}` (post-teardown reset) opens its next initiator Logon → the Logon carries `141=Y` even though `reset_on_logon` is false (C2.1 OR-of-three; data-model precedence rule 2). (Depends on T007's predicate.)
- [X] T012 [US2] Build + run `session_reset_on_lifecycle`; **confirm RED** for the US2 behavioral witnesses (no teardown reset wired → counters survive; no `logout_seen_` → peer-logout/abnormal arms fail). → verify: behavioral RED (counters not 1 after teardown), not compile errors.

### Implementation for User Story 2

> Production sites in `src/session/session.cpp`: a dedicated `logout_seen_` flag set at the two Logout sites, and the teardown reset in `close()` (~:935-1025).

- [X] T013 [US2] Add a dedicated `bool logout_seen_` member to `Session` (`include/fixpp/session/session.hpp` private state — additive POD, no awaitable-closure mutex per §XV.9) and set it `true` at BOTH (a) the local graceful-Logout-**sent** path (`close(close_mode::graceful)` / the generateLogout site ~:880) and (b) the inbound-`35=5` peer-Logout transition (~:2095). Do **NOT** derive it from `onLogout_fired_` (which fires on any `Active→!Active`, incl. abnormal close — would collapse logout into disconnect). (research D3 RC2; contract C3.1) → verify: library builds; `logout_seen_` set only on actual Logout sent/received; grep confirms no `onLogout_fired_`-as-logout-predicate.
- [X] T014 [US2] Implement the teardown reset in `Session::close()` (`src/session/session.cpp` ~:935-1025): when `(logout_seen_ && cfg_.reset_on_logout) || cfg_.reset_on_disconnect`, `co_await reset_seqnums_to_one_durable(logged)` — placed **before BOTH** drains in `close()`: before `write_gate_.cancel_and_drain()` (the first drain, ~:997) AND before `seqnum_mgr_.drain()` (~:1013), so `reset_to_one()` does not hit a drained seqnum mutex (`session_already_closed` silent no-op). Confirm the exact line anchors when implementing — the plan's "~:1002" is approximate; the binding constraint is "before the first drain" (analyze F1). Guard with a **single-fire** flag so a logout+disconnect double-trigger performs exactly one `store_->reset()` (C5.1; `FileStore::reset()` is non-idempotent I/O — assert via the T001 `reset_call_count()` seam). Fires on graceful, terminal, AND abnormal (read-pump EOF → `close(terminal)`). (research D3; data-model teardown rows + precedence rules 3/4; contract C3.1, C4.1, C4.2, C5.1 / **SC-002**) → verify: T010/T011 GREEN; double-trigger = one store reset; off ⇒ preserved.
- [X] T015 [US2] Build + run `session_reset_on_lifecycle` full suite GREEN; **author a no-heap witness** `ResetKnobs_NoHeapOnResetPath` in `test_reset_on_lifecycle.cpp` asserting zero global-heap allocation across `reset_seqnums_to_one_durable()` on the `open()` and `close()` reset paths, verified under the **mallocnesia LD_PRELOAD** interceptor (the binding gate per [[feedback_tracking_pmr_resource_false_pass]]) — the plan §VIII.5 commitment, not "covered by existing discipline" (analyze E3); note the reset fires at lifecycle boundaries, not the per-message hot path, so this is a no-regression guard rather than a §VIII.5-MUST hot-path cell. Run the §XV.9 awaitable-include watch-item check (UNFILTERED Tier-1 or `-L sync`) to confirm the `session_config.hpp` fields + `session.hpp` `logout_seen_` added no `std::mutex` into the `session.hpp` awaitable closure; re-run `ctest -L "session|seqnum"` for no regression. → verify: full 024 suite green; no-heap cell green under mallocnesia; no `sync`-corpus regression; all-off path still byte-identical.

**Checkpoint**: US1 + US2 both functional; S-017 behaviors delivered.

---

## Phase 5: Polish & Cross-Cutting Concerns

**Purpose**: Live interop proof (SC-005, both roles), the catalogue/coverage/docs close-out (applied at Polish per the 020/021/022 precedent), and the binding local verify mirror.

- [X] T016 [P] Add the live **fixpp-initiator ResetOnLogon** interop cell extending the 018 fixture (`tests/interop/`) + parent `phase-9-harness/`, skip-without-counterparty: fixpp initiator with `reset_on_logon=true` → outbound Logon carries `141=Y` + `34=1`, the live QFcpp/QFJ acceptor accepts, both sides resync from seqnum 1 (capture via the engine-log-seam, 016 P4). Run under normal + ASan/UBSan/TSan; use the SAME skip-without-counterparty guard as the 018 cells. (quickstart "Live interop cell"; contract C6.1 / SC-005 / FR-010)
- [X] T017 [P] Add the live **fixpp-acceptor ResetOnLogon** interop cell (`tests/interop/` + `phase-9-harness/`), skip-without-counterparty: a live QFcpp/QFJ initiator sends a Logon with `141=Y` + fresh `34=1`; fixpp acceptor with `reset_on_logon=true` resets-before-validation and accepts (no too-low disconnect), resync from 1. Same sanitizer matrix + same skip guard. (quickstart; contract C6.2 / SC-005 / FR-010 — the both-roles half)
- [X] T018 [P] Update `spec/feature-catalogue.md`: flip **S-017** (`:37`) `backlog → done`, citing 024, with the completion note (three reset knobs on the 013 `reset_to_one()` primitive at open()/inbound-Logon/close(); default false = no-op). Update **S-018** (`:38`) to keep `backlog` with a gap-note: *"Descoped from 024 — SeqnumManager not store-seeded at open(); RefreshOnLogon needs a store→manager hydrate-on-open path (008-boundary). Deferred to its own slice."* (plan §VI delta; FR-011 / contract C7.1)
- [X] T019 [P] Update `spec/behaviors-and-limitations.md`: add **B-024-1** (the three reset knobs reset seqnums to 1 at their lifecycle event; default off; the initiator announces a reset via `141=Y` (OR-of-three); durable + idempotent/single-fire) and **L-024-1** (`RefreshOnLogon` NOT implemented — seqnum manager not store-seeded at open; no construction-time store cache to refresh; operators needing external-store seqnum mutation must restart; tracked for a future store-hydrate slice). (plan §VI delta)
- [X] T020 [P] Update `spec/coverage-index.md`: flip **S-017 → done** with a 024 reference, asserting an **exact-set** diff (the done-flip moves exactly S-017; the deferred set loses exactly S-017 and RETAINS S-018 + `NextExpectedMsgSeqNum(789)` + the remaining named G3 knobs `CheckCompID`/`validateSequenceNumbers`/`MaxLatency`) — not a subset-presence check ([[feedback_completeness_gate_exact_set_not_subset]]). (plan §VI delta)
- [X] T021 Update `library/CLAUDE.md` active-feature pointer for 024 (status → IMPLEMENTED; next = `/simplify` → `/speckit-verify` → Gate B) per the merge-bookkeeping convention.
- [ ] T022 Run the full local Tier-1 verify mirror (`/speckit-verify`): ASan/UBSan/TSan on the three reset-wiring sites + the interop cells; coverage ≥95/85 on the new knob branches (each knob's on-arm + off-arm + the durable-helper disposition split + the single-fire guard); the §XV.9 watch-item (UNFILTERED Tier-1 or `-L sync`); confirm all-off byte-identity. Produces `.specify/decisions/024-reset-refresh-on-logon-verify.md` — the required evidence for `/gate-b`. (plan Constitution Check IX.1/IX.2/XV.9; pipeline step 17)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (T001)**: no dependencies — start immediately.
- **Foundational (T002, T003)**: after Setup. T002 (fields) + T003 (durable helper) block both user stories.
- **US1 (T004–T009)**: after T002+T003. Internal: T004 → T005 (RED tests) → T006 (confirm RED) → **T007 → T008** (impl; T008's acceptor combined-reset must keep T004(5) all-off GREEN) → T009 (GREEN + no regression).
- **US2 (T010–T015)**: after T002+T003. T011 also depends on **T007** (the OR-of-three predicate). Internal: T010 → T011 (RED tests) → T012 (confirm RED) → **T013 → T014** (impl: flag then teardown reset) → T015 (GREEN + §XV.9 + no regression).
- **Polish (T016–T022)**: after US1 + US2. T016–T020 are `[P]` (distinct files); T021 sequential; T022 verify runs last.

### User Story Independence

- **US1 (P1)** = the Logon path (open() + inbound-Logon). **US2 (P2)** = the teardown path (logout_seen_ + close()). They touch disjoint `session.cpp` regions and are independently testable, EXCEPT US2's T011 (141-on-next-Logon) reuses US1's T007 predicate — note that single cross-link. US1 is the MVP and ships/validates alone.

### Within stories

- US1: tests RED before impl; initiator (T007) before acceptor (T008) is not strictly required, but T008 must preserve the all-off 013 path (T004(5)). US2: `logout_seen_` (T013) before the teardown reset (T014) that reads it.

### Parallel Opportunities

- US1 (T004–T009) and US2's teardown core (T010, T012–T015) touch disjoint regions and can largely interleave; only T011 gates on T007.
- Polish doc tasks **T016, T017, T018, T019, T020** are all `[P]` (distinct files: two interop fixtures, catalogue, B&L, coverage-index).

---

## Parallel Example

```text
# After T001–T003 (setup + foundation):
US1 (Logon):     T004 → T005 → T006 → T007 → T008 → T009
US2 (teardown):  T010 → T012 → T013 → T014 → T015   (T011 after T007)

# Polish docs in parallel once both stories land:
T016 | T017 | T018 | T019 | T020   (all [P], distinct files)
```

---

## Implementation Strategy

### MVP First (User Story 1)

1. T001–T003 (setup + fields + helper) → US1 (ResetOnLogon, both roles, 141 predicate, all-off zero-regression). The inbound-correctness core ships with the highest-value knob.

### Incremental Delivery

1. Setup + foundation + US1 → ResetOnLogon delivered (MVP), all-off byte-identical.
2. US2 → ResetOnLogout (either direction) + ResetOnDisconnect (incl. abnormal drop), single-fire-guarded, + 141-on-next-Logon.
3. Polish → live interop cells green (both roles), catalogue/coverage/B&L updated (S-017 → done; S-018 stays backlog), `/speckit-verify` evidence produced for Gate B.

---

## Notes

- `[P]` = different files, no dependencies. Same-file test authoring (T004/T005, T010/T011) and the same-region `session.cpp` impl tasks are deliberately **not** `[P]`.
- `[Story]` label maps each task to US1 / US2; Setup / Foundational / Polish carry no story label.
- RED-first is binding (`[const §VII]`): verify witnesses fail on *behavior* before the production change. The all-off + inbound-`141` + store-failure → still-Active witness (T004(5)) is the **zero-regression guard** for the cause-keyed disposition split — it must stay GREEN through T008.
- The only production surface is the three `SessionConfig` bools + the `logout_seen_` flag + the `reset_seqnums_to_one_durable()` helper + the three wiring sites — no new module, codegen, error slot, or C-ABI; the `141` field is already emitted by 013.
- **`RefreshOnLogon` / S-018 is OUT of scope** — add no store-hydrate code/test (C7.1).
- Next pipeline steps after `/speckit-tasks`: `/speckit-analyze` → `/speckit-checklist` → `/speckit-checklist-audit` (MANDATORY gate before `/speckit-implement`) → `/speckit-implement` → `/simplify` → `/speckit-verify` → Gate B (per `.specify/pipeline.md`).
```
