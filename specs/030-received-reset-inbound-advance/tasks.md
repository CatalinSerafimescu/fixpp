---
description: "Task list — 030 received-reset inbound advance correction"
---

# Tasks: Received-Reset Inbound Advance Correction (acceptor + initiator 141=Y off-by-one)

**Input**: Design documents from `specs/030-received-reset-inbound-advance/`
**Prerequisites**: plan.md, spec.md (both present), research.md, quickstart.md. No data-model.md / contracts/ (no new entity or external interface — internal counter correction).

**Tests**: REQUIRED. Per `[const §VII]` (RED-first TDD) and the spec's Discriminating-Witness / Fault-injection-witness mandates, every behavioral change is witnessed test-first. Tests are written RED, then the production change makes them GREEN.

**Organization**: by user story (US1 acceptor P1, US2 initiator P1, US3 789-advertisement P2). The acceptor finding is the live-observed defect; the initiator arm is the symmetric reachable twin (FR-009).

## Format: `[ID] [P?] [Story] Description`
- **[P]**: parallelizable (different file/region, no dependency on an incomplete task)
- **[Story]**: US1 / US2 / US3 (story phases only)
- All paths are repo-root = library submodule.

## Path Conventions
- Production: `src/session/session.cpp` (the two received-141 arms + the shared `reset_seqnums_to_one_durable` helper).
- Tests: `tests/session/test_reset_on_lifecycle.cpp` (new witnesses + contract split), plus the 3 pin files.

---

## Phase 1: Setup

- [X] T001 Confirm a green baseline before any edit: build the debug preset and run the session reset/seqnum suites (`session_reset_on_lifecycle`, `reset_seqnum_policy_matrix`, `persistent_seqnum_hydrate`, `next_expected_msgseqnum`) — record the current pass counts so the blast-radius flips (Phase 6) are attributable. Build parallelism ≤ `-j2` per the WSL2 OOM cap. **DONE 2026-06-10: baseline 7/7 GREEN after clearing a STALE BUILD ARTIFACT — `build/linux-clang-debug/.../session.cpp.o` was carried over from the prototype branch `fix/acceptor-received-141-next-inbound` (which HAD the fix → binary returned `next_inbound=2`), so the pins falsely RED'd at first run. `touch src/session/session.cpp` + rebuild restored the true defect baseline (pins at next_inbound=1 / 789=1 / durable_inbound=1 all PASS). The other build dirs (asan/ubsan/tsan/coverage/gcc-release) are likely contaminated too → force-rebuild them in T027.**
- [X] T002 Re-verify the exact source anchors against the current `src/session/session.cpp` before editing (line numbers drift): the acceptor received-141 reset call (`reset_seqnums_to_one_durable(reset_disposition::logged)`, ~`:1942`, block ends ~`:1947`), the 789 read (~`:1974`), `logon_inbound_advanced` set site (~`:1798`), the initiator Logon-ack arm (`peer_ack_sent_reset_flag` reset ~`:3162`, swallow ~`:3167-3169`, `check_inbound` advance ~`:3119`), the shared helper `reset_seqnums_to_one_durable(disposition)` (~`:505-528`), `store_is_persistent_`, and the existing fatal sites (`:682`, `:1764`). Update any cite in the bundle that has drifted. **DONE 2026-06-10 — verified current anchors: acceptor reset `:1941-1947` (`reset_disposition::logged`); 789 read `:1972-1975`; `logon_inbound_advanced` decl `:1705` / set `:1798`; 029 acceptor net-advance persist guard `:2039` (`logon_inbound_advanced && !peer_sent_reset && !cfg_.reset_on_logon`). Initiator: `logon_inbound_advanced_init` decl `:3116` / set `:3141`; `peer_ack_sent_reset_flag` decl `:3117` / set `:3150`; reset block `:3157-3179` (hand-rolled `reset_to_one()` `:3162` + swallow `:3167-3170`); 029 initiator persist guard `:3307`. Helper `reset_seqnums_to_one_durable` `:505-533` (fatal check `:524`). `store_is_persistent_` `:926`. `persist_inbound_advance_` `:621` (does `next_seqnum(inbound, increment=true)` → post-reset store 1→2, no-op if non-persistent). `set_next_inbound(seqnum_t)` `seqnum_manager.hpp:132`. `seqnum_t = uint32_t`, `seqnum_min = 1` (`seqnum.hpp:28/30`). Restore goes INSIDE each reset block (gated by peer_sent_reset / peer_ack_sent_reset_flag), additionally guarded on the consumed-advance bool — NOT unconditionally on the advance bool (which is true on every normal in-seq Logon).**

---

## Phase 2: Foundational (blocking — shared test harness for the FR-010 fault witnesses)

**Purpose**: the fault-injection witnesses (US1 sc3, US2 sc2) and the contract-witness split (Phase 6) all need a persistent store-double that can fail a reset on demand, plus a non-persistent factory variant. Establish/confirm these before the story phases.

- [X] T003 In `tests/session/test_reset_on_lifecycle.cpp`, confirm the existing `StoreDoubleFactory` / store-double exposes `fail_next_reset()` (used by the merged witness (5) `ResetOnLogon_Off_Inbound141_StoreFailure_StillActive:531-558`) and that it inherits `yields_persistent_store()==true`. If a `yields_persistent_store()==false` factory variant does not already exist, add a minimal non-persistent sibling factory (override returning `false`) for the contract-split non-persistent witness. No production code in this task.

---

## Phase 3: User Story 1 — Acceptor accepts post-reset traffic without a spurious resend (Priority: P1) 🎯 MVP

**Goal**: after a peer `Logon(141=Y)` on the knob-off received-141 acceptor arm, next-expected-inbound is 2; the peer's seq-2 message is accepted with no `ResendRequest`; on a persistent store `store == manager == 2`; a persistent reset failure is fatal.

**Independent test**: drive a received-141 Logon into an acceptor (`reset_on_logon=false`), feed seq-2 → no `ResendRequest`, next_inbound→3; persistent store ⇒ durable==2; fault-injected reset failure ⇒ Disconnect.

### RED tests (write first; all fail against current code)

- [X] T004 [P] [US1] In `tests/session/test_reset_on_lifecycle.cpp`, add `ResetOnLogon_Off_Received141_NextInboundIsTwo` — peer `Logon(34=1,141=Y)`, knob off; assert `next_inbound_unsafe() == 2` immediately post-consume (FR-001). (RED: currently 1.)
- [X] T005 [P] [US1] In `tests/session/test_reset_on_lifecycle.cpp`, add `Received141_PeerNextMsgSeq2_HarmCheck` — after the reset, feed peer Heartbeat at `34=2`; assert NO `ResendRequest` emitted and `next_inbound`→3 (FR-002). (RED: currently emits a spurious ResendRequest.)
- [X] T006 [US1] In `tests/session/test_reset_on_lifecycle.cpp`, add the **acceptor discriminating triple** witness (027 advertisement on): assert `next_inbound==2` AND `reply.MsgSeqNum==1` AND `reply.789==2` in one test (FR-001/003/004; spec §Discriminating Witness). Parse 34/141/789 from the actual reply frame (sound field-parse, not `strip_tag` — the 025 W5a lesson).
- [X] T007 [P] [US1] In `tests/session/test_reset_on_lifecycle.cpp`, add the persistent-store INV-H1 witness: after the received-141 reset on a **persistent** store, assert `store.durable_inbound == 2` **directly on the store** AND `store == manager == 2` (FR-005; the 029 W9b proxy-gap lesson — assert the store value, not a manager proxy).
- [X] T008 [US1] In `tests/session/test_reset_on_lifecycle.cpp`, add the **fault-injection** witness (FR-010): persistent store + `fail_next_reset()` on the received-141 path → assert (i) session **Disconnected** + store error propagated, AND (ii) persist-to-2 NOT reached → no `store > manager` (`durable > in-memory`) ever observable.
- [X] T009 [P] [US1] In `tests/session/test_reset_on_lifecycle.cpp`, add the **guard** witness (FR-007): a received-141 sub-path with no consumed in-sequence reset Logon (`logon_inbound_advanced` false) does NOT fire the restore+persist (no spurious set).

### Implementation (make T004–T009 GREEN)

- [X] T010 [US1] In `src/session/session.cpp` acceptor received-141 arm (~`:1942`): change the reset disposition to `store_is_persistent_ ? reset_disposition::fatal : reset_disposition::logged` (FR-010); keep the existing `:1943-1946` error handler.
- [X] T011 [US1] In `src/session/session.cpp` acceptor arm, **between the reset (~`:1942`) and the 789 read (~`:1974`)**, add the guarded restore+persist: when `logon_inbound_advanced`, restore next-expected-inbound to `seqnum_min+1` in the manager (`set_next_inbound`) AND persist the store to `seqnum_min+1` (the `next_seqnum(inbound, /*increment=*/true)` write-through) — giving `store == manager == 2`. Do NOT loosen the 029 `:2039` net-advance persist guard; this is a dedicated arm-local persist. Comment the rationale (consumed seq-1 reset Logon = surviving net-advance; outbound stays seq 1).

**Checkpoint**: US1 RED tests now GREEN; `reset_on_logon=true` path and steady-state untouched (SC-004).

---

## Phase 4: User Story 2 — Initiator accepts post-reset traffic without a spurious resend (Priority: P1)

**Goal**: the same correction on the separate, reachable initiator Logon-ack `peer_ack_sent_reset_flag` arm (FR-009), consolidated onto the shared helper for symmetric disposition.

**Independent test**: drive an initiator reset handshake whose peer Logon-ack carries `141=Y` → next_inbound==2 post-consume; seq-2 accepted, no `ResendRequest`; persistent reset failure ⇒ Disconnect.

### RED tests

- [X] T012 [P] [US2] In `tests/session/test_reset_on_lifecycle.cpp`, add the initiator witness: peer Logon-ack `141=Y` consumed at `34=1` → assert `next_inbound==2`; then feed peer seq-2 → accepted, NO `ResendRequest` (FR-009; no `reply.789` clause — initiator builds no reply Logon here). (RED.)
- [X] T013 [P] [US2] In `tests/session/test_reset_on_lifecycle.cpp`, add the initiator INV-H1 witness (persistent): `store.durable_inbound == 2` directly, `store == manager == 2`.
- [X] T014 [US2] In `tests/session/test_reset_on_lifecycle.cpp`, add the initiator **fault-injection** witness (FR-010, symmetric to T008): persistent store + reset failure → Disconnect, persist-to-2 not reached, no `store > manager`.

### Implementation (make T012–T014 GREEN)

- [X] T015 [US2] In `src/session/session.cpp` initiator Logon-ack arm (~`:3162`): **consolidate** the hand-rolled `reset_to_one()` + swallowed `(*store_).reset()` (~`:3167-3169`) onto the shared `reset_seqnums_to_one_durable(store_is_persistent_ ? fatal : logged)` helper (FR-010; [[feedback_half_restructure_symmetric_api]]).
- [X] T016 [US2] In `src/session/session.cpp` initiator arm, after the consolidated reset, add the symmetric guarded restore+persist (manager `set_next_inbound(seqnum_min+1)` + store write-through), guarded on the consumed-ack advance — the initiator's guard local is **`logon_inbound_advanced_init`** (`~:3116` decl / `~:3141` set), NOT the acceptor's `logon_inbound_advanced` — mirroring T011.

**Checkpoint**: both P1 arms fixed + witnessed; symmetric.

---

## Phase 5: User Story 3 — Acceptor advertises correct next-expected (Priority: P2)

**Goal**: confirm the 027 acceptor-reply `789` corrects 1→2 — it derives automatically from US1's restored counter (`next_inbound_unsafe()`), so no new production code; this phase locks it with an isolated witness.

- [X] T017 [US3] In `tests/session/test_next_expected_msgseqnum.cpp` (or `test_reset_on_lifecycle.cpp`), confirm an isolated witness: acceptor with 027 on, received-141 → reply `MsgSeqNum==1` AND `789==2` (FR-004). (This overlaps the T006 triple; keep it as the 027-suite-local assertion. The Phase-6 rename of `AcceptorReplyReceived141_Advertises1→2` is the pin form.)

**Checkpoint**: 789 correction proven; no separate impl (counter-derived).

---

## Phase 6: Blast radius — flip the 7 pins (6 value-pins + 1 contract-witness split)

**Each pin individually re-read to confirm it pins THIS off-by-one (a justified correction), not a distinct behavior.** Run after the implementation so the flips turn RED→GREEN on the corrected code.

- [X] T018 [P] In `tests/session/test_reset_seqnum_policy_matrix.cpp`, flip the 3 acceptor value-pins `Bilateral{Strict,Lenient}`/`Unilateral_Acceptor_CountersResetToOne`: `next_inbound` 1→2 (update assertions + the "must be 1" messages).
- [X] T019 [P] In `tests/session/test_reset_seqnum_policy_matrix.cpp`, flip the **initiator** value-pin `BilateralStrict_Initiator_CountersResetToOne` (`:593-594`): `next_inbound` 1→2 (the FR-009 path). **Also flip the now-stale comment at `~:590` ("peer's next post-reset message is seq=1") and the assertion message string at `:594` ("must be 1 after 141=Y mutual reset" → "must be 2")** so the message matches the corrected value (analyze F2).
- [X] T020 [P] In `tests/session/test_persistent_seqnum_hydrate.cpp`, flip the W9b pin `Acceptor_ResetLogon_InboundSeedWithheld_NoTooLowFatal` — **both** sub-assertions: `next_inbound` (`:1587`) 1→2 AND `store->durable_inbound` (`:1610`) 1→2. **Also flip the now-obsolete 029-era comments: `~:1588` ("reset won over hydrate") and `~:1612-1614` ("Post-fix GREEN: durable_inbound=1 (persist correctly skipped)")** to the 030-correct framing (the consumed-Logon advance now SURVIVES the reset under persist-to-2; durable=2, persist NOT skipped) — analyze F1; the plan's grep-sweep needle (`reset won over hydrate`) must catch these.
- [X] T021 [P] In `tests/session/test_next_expected_msgseqnum.cpp`, flip `Reset.AcceptorReplyReceived141_Advertises1` (`:1441`): advertise `789` 1→2 and rename → `_Advertises2`.
- [X] T022 In `tests/session/test_reset_on_lifecycle.cpp`, **split** the merged 024 contract witness (5) `ResetOnLogon_Off_Inbound141_StoreFailure_StillActive` (`:531-558`): the persistent variant now asserts **Disconnect** + error propagated (FR-010 / 024 I-07 contract amendment); add a NEW sibling witness using the `yields_persistent_store()==false` factory (from T003) that **retains** stay-Active for non-persistent stores.

**Checkpoint**: full session suite GREEN on the corrected code; 7 pins reconciled.

---

## Phase 7: Polish & Cross-Cutting

- [X] T023 [P] §VI delta — `spec/feature-catalogue.md`: amend the Notes of S-017 (received-141 reset machinery), S-031 (789 advertisement), S-032 (ResetSeqNumFlag) to cite `030-received-reset-inbound-advance` (received-141 nets next-expected-inbound=2 + reply 789=2; persistent reset failure now fatal). No new S-row.
- [X] T024 [P] §VI delta — `spec/behaviors-and-limitations.md`: add **B-030-1** (received-141 reset advances next-expected-inbound to 2 on both arms — reference-engine-conformant; reply MsgSeqNum stays 1, reply 789=2 when 027 on) and **B-030-2** (persistent received-141 durable-reset failure now disconnects — the 024 FR-001/C2.6 I-07 amendment; non-persistent keeps stay-Active). **One exhaustive grep-sweep** for the now-obsolete "next_inbound 1→2 breaks byte identity" / I-07-stay-Active rationale (B&L:563 / B-024-1 / `session.cpp` comment at `:1724-1726` + the initiator/acceptor reset comments) and correct each (the stale-doc-drift class).
- [X] T025 [P] `spec/coverage-index.md`: add coverage notes mapping the new guarded restore+persist branches (both arms) + the fatal-when-persistent disposition + the fault-injection witnesses ↔ `tests/session/test_reset_on_lifecycle.cpp`, under the 024/027/029 entries.
- [X] T026 `codegraph sync` from the submodule cwd after the code edits land (per project CodeGraph rules).
- [X] T027 Run the full 6-preset verify matrix (debug + ASan/UBSan/TSan + coverage + the codegen-graph-check), strictly ONE preset at a time, ≤`-j2` (WSL2 OOM cap). Confirm coverage ≥95/85 per `[const §IX.1]` and record lcov DA/BRDA, enumerating the **four** new guard branches against their DA/BRDA lines (analyze B1): (a) acceptor `logon_inbound_advanced` true-arm (restore fires), (b) acceptor `logon_inbound_advanced` false-arm (guard skips — T009), (c) initiator `logon_inbound_advanced_init` true-arm (T012/T016), (d) the `store_is_persistent_ ? fatal : logged` both arms (fatal covered by the fault-injection witnesses T008/T014; logged/non-persistent covered by the T022 non-persistent sibling). **Also confirm FR-006/SC-004 non-regression explicitly**: the `reset_on_logon=true` knob suite (`ResetOnLogon_Acceptor_*` / `ResetOnLogon_Initiator_*` in `test_reset_on_lifecycle.cpp`, ~`:322-753`) and the broader steady-state suite remain byte-identical/green (analyze E1 — these are the named SC-004/FR-006 regression witnesses, not just an implicit full-suite pass). This is the `/speckit-verify` evidence (SC-003). **DONE 2026-06-10: 6-preset matrix GREEN, each preset CLEAN-BUILT one-at-a-time (`--target clean` + `-j2`) to defeat the stale-prototype-object false-greens that masked pins during /implement — debug 435/435 (incl. codegen-cleanliness #130), ASan 434/434, UBSan 434/434, TSan 434/434, coverage 435/435, gcc-release 435/435. Coverage = 100% branch on the 030 new code (both arms of every new conditional; counts recorded in coverage-index 030 §Coverage). The coverage check found ONE uncovered branch — the initiator restore guard FALSE-arm (3200) — the symmetric twin of the witnessed acceptor T009 was missing; added `Initiator_Received141Ack_GuardSkipsWhenNoConsumedReset` (commit after f54fd63) → now T5/F1. FR-006/SC-004 non-regression confirmed (full suite incl. the `ResetOnLogon_*` knob suite green under all presets).**
- [ ] T028 Live interop close-out (SC-001) — re-run the acceptor received-141 cell vs **both** QuickFIX-cpp and QuickFIX-J: assert the session reaches Active with **zero** fixpp `ResendRequest` and the peer's seq-2 message accepted (skip-without-counterparty guard for CI). This is the true close-out of the live-found defect; unblocks the other acceptor feature cells. **STATUS 2026-06-10 — DEFERRED to the Item-1 live-interop golden-capture effort (NOT a simple re-run):** (1) no received-`141` knob-OFF interop cell exists yet — only the `reset_on_logon`-ON cell (`hp_fix44_reset_on_logon_test.cpp`); a NEW received-141 cell (fixpp acceptor `reset_on_logon=false` + QF initiator sending `Logon(141=Y,34=1)` then a msg at 34=2) must be authored. (2) The live rig is not stood up in this session (`FIXPP_TLS_FIXTURE_DIR` unset; QFcpp/QFJ present at `reference-engines/` but not running as counterparties). The fix's CORRECTNESS is fully established in-process (discriminating-triple + initiator + fault-injection witnesses) + the 6-preset matrix + 100% branch coverage; SC-001 is the end-to-end live confirmation tracked under [[project_live_quickfix_hostile_scenarios]] / Item 1, consistent with prior features' skip-without-counterparty live deferral.

---

## Dependencies & Execution Order

- **Phase 1 (Setup)** → **Phase 2 (Foundational test harness)** → block everything.
- **US1 (Phase 3)** and **US2 (Phase 4)** both depend on Phase 2; they touch separate `session.cpp` arms so the *implementation* tasks are largely independent, but share `test_reset_on_lifecycle.cpp` (sequence the test-file edits to avoid conflicts). US1 is the MVP (the live-found defect).
- **US3 (Phase 5)** depends on US1 (789 derives from the acceptor's restored counter).
- **Phase 6 (pins)** depends on the implementation (US1+US2) being GREEN — flip pins last so they turn RED→GREEN on corrected code, proving they were stale pins of the defect.
- **Phase 7 (Polish/verify/live)** is last; T027 (verify matrix) and T028 (live cell) gate Gate B.

## Parallel Opportunities
- T004/T005/T007/T009 (US1 RED tests, distinct test bodies) — author in parallel, but commit-sequence the shared file.
- T012/T013 (US2 RED tests) in parallel with each other.
- T018/T019/T020/T021 (pin flips, distinct files) — fully parallel.
- T023/T024/T025 (doc deltas, distinct files) — parallel.

## Implementation Strategy
- **MVP = US1** (acceptor): the live-observed defect. Ship US1 RED→GREEN first; it is independently demonstrable (the live cell SC-001 is acceptor-side).
- **US2** must land in the same change (FR-009 — reachable symmetric twin; shipping US1 alone leaves a known initiator conformance bug).
- TDD throughout: tests RED before the `session.cpp` edits; the discriminating triple (T006) + fault-injection (T008/T014) are the witnesses that no weaker proxy can pass.
