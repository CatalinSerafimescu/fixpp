# Tasks: RefreshOnLogon — per-logon re-hydrate of seqnum counters from the store

**Input**: Design documents from `specs/025-refresh-on-logon/`
**Prerequisites**: plan.md (Gate A converged round 3, 2 rewrites: RC#1 narrow-claims-to-the-025-delta + defer the inherited 029 cold-open `bilateral_strict` gap as L-029-3; 2 refresh sites not 3; defer not fold-in), spec.md (US1–US3, FR-001..012, SC-001..007), data-model.md (Change-set C1–C8, force-trigger truth table, INV-RoL-1..6, witness matrix W1–W9 incl. the W5a/W5b split), contracts/refresh-knob.md (C1–C6), research.md (D-RoL-1..D-RoL-6)
**Branch**: `025-refresh-on-logon` | repository root = library submodule | rides on the merged 029 spine (`SeqnumManager::hydrate`, `ensure_hydrated_`, `store_is_persistent_`)

**Tests**: REQUIRED (TDD / RED-first, `[const §VII]`). The data-model witness matrix W1–W9 + the spec Success Criteria SC-001..007 are the authoritative witness list; each test task names the W#/SC/INV it covers. Write each cluster RED before its implementation.

## Format: `[ID] [P?] [Story] Description`

- **[P]** = parallelizable (different file, no incomplete-task dependency). Witnesses in the same shared file (`tests/session/test_refresh_on_logon.cpp`) are NOT mutually [P]; impl edits to the same file (`src/session/session.cpp`) are NOT mutually [P].
- **[Story]** = US1 (store-wins re-hydrate) / US2 (default-off + non-persistent byte-identical) / US3 (no malformed reset Logon / RC-1 preserved). Setup, Foundational, Polish carry no story label.
- Line numbers (`:NNNN`) are anchors from the merged tree (`0b9c8b8`) — confirm each site by its surrounding code, not the literal line (edits shift them).

---

## Phase 1: Setup (shared infrastructure)

**Purpose**: Test scaffolding + ctest target registration for the new unit file and the live standby-re-hydrate interop cell.

- [X] T001 [P] Create `tests/session/test_refresh_on_logon.cpp` skeleton (GoogleTest, reusing the 029 harness from `tests/session/test_persistent_seqnum_hydrate.cpp` — its `Fixture`, `OutboundCapture`, the pre-seedable + fail-on-Nth + read-counting test `MessageStore`, and the configurable `MessageStoreFactory` with settable `yields_persistent_store()`). All knob-on witnesses set `cfg.reset_seqnum_policy_field = bilateral_lenient` UNLESS the witness is about `bilateral_strict`. Register the ctest target `session_refresh_on_logon` in `tests/session/CMakeLists.txt`.
- [X] T002 [P] Add the standby-re-hydrate live cell to `tests/interop/happy/hp_fix44_restart_resume_test.cpp` (extend the 029 restart-resume file; skip-without-counterparty guard per the 016/018/027/028/029 harness pattern) — a fixpp standby (lenient, `refresh_on_logon=on`) re-hydrates a primary-advanced store, then logs on vs a QFcpp/QFJ peer. No new ctest target (reuses the 029 interop cell registration).

**Checkpoint**: `session_refresh_on_logon` configures and builds (empty/skipped); ctest discovers it.

---

## Phase 2: Foundational (blocking prerequisites — MUST complete before US1/US2/US3)

**Purpose**: The two additive surfaces (config bool + the `force` latch-bypass) and the two call-site gates that every story builds on. ⚠️ No story work begins until this phase is complete. Implement these AFTER the US1 RED witnesses (T010/T011) exist, per TDD — but they are listed here as the shared prerequisite for all stories.

- [X] T003 Add `bool refresh_on_logon = false;` to `include/fixpp/session/session_config.hpp` near `reset_on_logon` (`:247`), with a doc comment: QuickFIX `RefreshOnLogon`; store-wins (up or down); **standby/backup topologies only** (active-session reconnect can regress past the INV-H1 lag — L-025-1); **no-op under `bilateral_strict`** (the default) and under a non-persistent store. Explicit `= false` default ([const §XII.5]). — contract C1, data-model C1, FR-001.
- [X] T004 Add a defaulted `bool force = false` second parameter to the `ensure_hydrated_` declaration in `include/fixpp/session/session.hpp`. — contract C2, data-model C2.
- [X] T005 In `src/session/session.cpp` `ensure_hydrated_` (`:561`): change the `hydrated_` one-shot early-return (`:564-566`) from `if (hydrated_)` to `if (hydrated_ && !force)`. Change NOTHING else — the `hydrating_` re-entrancy guard (`:568`), the `store_is_persistent_` skip (`:576`, INV-RoL-2), the both-reads-before-mutate (`:583-594`), the `apply_inbound_seed` withhold (`:601`, INV-RoL-5), and the fatal disposition (`:586`/`:592`/`:605`, INV-RoL-6) are reused verbatim. The `hydrated_` latch (`:610`) stays — a forced call leaves it set (benign; non-persistent path never sets it, C2.6). — contract C2.1–C2.6, data-model C3, FR-002/004/005/006, research D-RoL-4.
- [X] T006 In `src/session/session.cpp`, wire the call-site gate at BOTH sites: introduce `const bool refresh_active = cfg_.refresh_on_logon && cfg_.reset_seqnum_policy_field != fixpp::session::reset_seqnum_policy::bilateral_strict;` and pass it as `/*force=*/refresh_active` to the existing `ensure_hydrated_` calls — initiator `emit_initiator_logon_()` (`:658`, before `peek_outbound()` `:699`) and acceptor `NotConnected` Logon (`:1738`, before `check_inbound` + reply `peek_outbound()` `:1951`). The `apply_inbound_seed` argument is UNCHANGED from 029 (initiator `!cfg_.reset_on_logon`; acceptor `!(peer_sent_reset || cfg_.reset_on_logon)`). Under `bilateral_strict` `refresh_active==false` ⇒ zero extra reads (INV-RoL-3 / FR-008). — contract C3.1–C3.3, data-model C4/C5, FR-002/008/009, research D-RoL-3/D-RoL-5.

**Checkpoint**: builds green; `refresh_on_logon` default `false`; the `force` path compiles; cold path (`force=false`) byte-identical to 029 (the 029 hydrate regression suite stays green).

---

## Phase 3: User Story 1 — Hot-standby adopts the store's counters on each logon (Priority: P1) 🎯 MVP

**Goal**: knob-on, non-strict, persistent store → the manager adopts the store's counters (up OR down) at each 2nd+ logon. **Independent test**: W1 + W2.

- [X] T010 [US1] Write W1 RED in `tests/session/test_refresh_on_logon.cpp`: persistent store `{in:50,out:60}`, live `{in:40,out:42}`, `refresh_on_logon=true`, `bilateral_lenient`; drive a 2nd logon; assert both manager counters == store's HIGHER values. RED pre-impl: the 029 one-shot latch makes the 2nd logon a no-op → stays `{40,42}`. — SC-001, FR-002/003, INV-RoL-4.
- [X] T011 [US1] Write W2 RED (store-wins DOWN — distinguishes store-wins from advance-only): persistent store `{in:5,out:6}`, live `{in:40,out:42}`, knob-on, lenient; drive a 2nd logon; assert both counters == store's LOWER values. An advance-only/`max` impl would FAIL this. — SC-002, FR-003, INV-RoL-4.
- [X] T012 [US1] Confirm W1+W2 GREEN after Phase 2 (T005/T006). Assert the store read **call-count** increases by exactly 2 (inbound+outbound) per refreshing logon via the counting store (not a proxy — [[feedback_witness_asserts_named_postcondition_not_proxy]]).

**Checkpoint**: US1 GREEN — store-wins re-hydrate works both directions; this is the MVP.

---

## Phase 4: User Story 2 — Default-off + non-persistent byte-identical no-op (Priority: P1)

**Goal**: knob-off ⇒ 029 one-shot unchanged; non-persistent store ⇒ no-op even knob-on. **Independent test**: W3 + W4.

- [X] T020 [US2] Write W3 RED then GREEN: `refresh_on_logon=false` (default), persistent store `{in:50,out:60}`; record store read-count after cold open = N; drive a 2nd logon; assert read-count == N (NO re-read) and counters retain their live values; then run the full existing session/recovery/029-hydrate regression suite and assert green (byte-identity). — SC-003, FR-004/010, INV-RoL-1.
- [X] T021 [US2] Write W4 RED then GREEN: `refresh_on_logon=true` on a non-persistent store (`yields_persistent_store()==false`), lenient; drive logons; assert ZERO store reads on the refresh path (the `:576` skip fires even under `force`), behaviour byte-identical to knob-off. — SC-004, FR-005, INV-RoL-2, contract C2.3/C2.6.

**Checkpoint**: US2 GREEN — zero-regression floor holds; non-persistent is a no-op.

---

## Phase 5: User Story 3 — No malformed reset Logon; received-141 still wins (Priority: P2)

**Goal**: under `bilateral_strict` the re-hydrate is suppressed (the knob adds no NEW malformed Logon); the acceptor received-141 reset still wins under refresh; a refresh read-failure is fatal. **Independent test**: W5a + W5b + W6 + W7.

- [X] T030 [US3] Write W5a RED then GREEN: `refresh_on_logon=true`, `bilateral_strict` (default), persistent store `{in:37,out:42}`; drive a 2nd logon (initiator); assert (a) the per-logon re-hydrate did NOT run — zero EXTRA store reads beyond the 029 cold one-shot — and (b) the emitted establishment is the knob-off `bilateral_strict` path (the knob adds no NEW malformed Logon). Assert BOTH postconditions (read-count AND emitted bytes). — SC-005, FR-008, INV-RoL-3, contract C3.3.
- [X] T031 [US3] Write W5b (the L-029-3 gap witness, clearly labeled NOT a 025 guarantee): `refresh_on_logon=false`, `bilateral_strict`, persistent store with non-1 outbound, initiator cold open; assert ONLY the inherited-029 behavior as-is (do NOT assert the cold-open Logon is valid — that is the deferred L-029-3 gap). Documents the inherited path so a future 029/024 follow-up has a baseline. — data-model W5b, L-029-3.
- [X] T032 [US3] Write W6 RED then GREEN: `refresh_on_logon=true`, lenient, ACCEPTOR with persistent non-1 inbound `{in:37}`; feed a peer reset Logon `34=1,141=Y` on a 2nd logon; assert the inbound seed is withheld (`apply_inbound_seed=false` via `peer_sent_reset`), the `:1925` received-141 reset establishes `{1,1}`, the peer `34=1` is accepted (in-seq, not too-low), session reaches Active. — SC-006, FR-009, INV-RoL-5, contract C5.3.
- [X] T033 [US3] Write W7 RED then GREEN: `refresh_on_logon=true`, lenient, fault-injecting store that succeeds at cold open and FAILS the inbound read on the 2nd logon; drive a 2nd logon; assert `Disconnected`, no partial seed (manager unchanged from pre-read), no new error slot (reuses the 029 store-failure disposition). — SC-007, FR-006, INV-RoL-6, contract C2.5.

**Checkpoint**: US3 GREEN — suppression + RC-1 + fatal all hold; no NEW malformed wire from the knob.

---

## Phase 6: Polish & cross-cutting

- [X] T040 [P] Write W8: no-heap under mallocnesia on the per-logon re-hydrate path (knob-on, lenient, non-allocating ready-awaitable store) — zero allocations attributable to the refresh. [const §VIII.5].
- [X] T041 Complete W9 (the live standby-re-hydrate interop cell, T002) — drive against a running QFcpp/QFJ peer; capture the golden; `GTEST_SKIP` without a counterparty. [const §VII.6].
- [X] T042 §VI delta (Polish — verified by `/speckit-checklist-audit`, not a runtime witness; 027/028 precedent): `spec/feature-catalogue.md` **S-018** `backlog`→`done` (feature `025-refresh-on-logon`, evidence_pr `(pending merge)`, Tests `tests/session/test_refresh_on_logon.cpp` + the interop cell, gap-note → "shipped: per-logon store-wins re-hydrate on the 029 spine, suppressed under `bilateral_strict`"); `spec/coverage-index.md` §4.3.12 + §4.4 move S-018 `backlog`→`done` (025). — FR-012.
- [X] T043 B&L update in `spec/behaviors-and-limitations.md`: **retire L-024-1** (RefreshOnLogon IMPLEMENTED via 025 — keep the ID with a "DISCHARGED by 025" status line); **add L-025-1** (store-wins re-hydrate can regress an ACTIVE session past the INV-H1 lag on reconnect → enable `refresh_on_logon` ONLY on backup/standby topologies, operator responsibility, QuickFIX-faithful; AND `refresh_on_logon` is suppressed under `bilateral_strict`, so a no-op out of the box until a non-strict policy is selected); **add L-029-3** (the inherited 029 `bilateral_strict`-emits-`141=Y`-with-non-1-body gap — cold-open hydrate AND any non-1-outbound reconnect — DEFERRED 029/024 follow-up, NOT closed by 025; the real fix is the `:1795` mutual-agreement handshake). — FR-012, research D-RoL-6, L-025-1/L-029-3.
- [ ] T044 Run the local Tier-1 verify gate (`/speckit-verify`): the 6-preset sanitizer matrix (ASan/UBSan/TSan + coverage + mallocnesia no-heap) on `session_refresh_on_logon` + the 029 regression + the interop ctest. Confirm coverage ≥95/85 on the new `force` branch + the two call-site `refresh_active` expressions (incl. the `bilateral_strict` short-circuit) + the refresh read-failure branch ([const §IX.1]).

**Checkpoint**: all witnesses GREEN; verify matrix GREEN; §VI surfaces updated; ready for Gate B.

---

## Dependencies & order

- **Phase 1 (T001–T002)** → **Phase 2 (T003–T006)** → stories.
- **TDD ordering**: write the US1 RED witnesses (T010/T011) BEFORE Phase 2 impl makes them green (T012); same RED-then-GREEN within US2/US3.
- **US1 (P1, MVP)** is independently testable (W1+W2). **US2 (P1)** and **US3 (P2)** depend only on Phase 2, not on US1 — but all share `src/session/session.cpp` + `tests/session/test_refresh_on_logon.cpp`, so impl/test tasks in the same file are sequential (not [P]).
- **Polish (T040–T044)** after all stories green.

## Parallel opportunities

- T001 ∥ T002 (different files).
- T040 (mallocnesia, may be a separate fixture) ∥ T042/T043 (doc surfaces) — different files.
- Within a story, witnesses sharing `test_refresh_on_logon.cpp` are NOT [P]; impl edits sharing `session.cpp` are NOT [P].

## MVP scope

**US1** (T001–T006 + T010–T012) = the store-wins per-logon re-hydrate. Delivers the feature's core value (a standby adopts the store's counters). US2/US3 add the zero-regression floor + the establishment-safety guarantees.
