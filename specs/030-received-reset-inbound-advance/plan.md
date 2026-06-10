# Implementation Plan: Received-Reset Inbound Advance Correction (acceptor 141=Y off-by-one)

**Branch**: `030-received-reset-inbound-advance` | **Date**: 2026-06-10 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/030-received-reset-inbound-advance/spec.md`

## Summary

A **conformance bug fix** on the received-141 path (peer `Logon(141=Y)`, local `reset_on_logon`
knob OFF). Today the engine consumes the peer reset Logon — `check_inbound` advances
next-expected-inbound 1→2 — and then `reset_seqnums_to_one_durable()` unconditionally
re-bases **both** counters back to 1, clobbering that advance. Net next-expected-inbound = 1
instead of 2, so the peer's next genuine message (seq 2) reads too-high → fixpp emits a
**spurious `ResendRequest`**, and (when 027 advertisement is on) the reply Logon advertises
`789=1` instead of `2`.

**The reference engines do it in the opposite order** (grounded in `research.md` /
spec Clarifications): QuickFIX-cpp and QuickFIX-J **reset *then* increment** — `reset()`
rebases to 1, then the consumed in-sequence reset Logon advances target 1→2 (QFcpp
`nextLogon` ~206/265; QFJ 2215/2303), and the 789 advertisement is `nextTarget(1)+1=2`
(QFcpp ~710 "incoming Logon did not increment the target SeqNum yet"; QFJ 2269-2278).
fixpp's own `reset_on_logon=true` knob path already nets 2. **Only the received-141
(knob-off) arm is wrong.**

**Fix (persist-to-2 inbound restore, BOTH arms):** after the durable reset on the received-141
arm, restore the next-expected-inbound to `seqnum_min + 1` (= 2) in BOTH the in-memory manager
(`SeqnumManager::set_next_inbound`) AND (on a **persistent** store) the durable store (one
`next_seqnum(inbound, increment=true)` write-through), guarded on the reset Logon having actually
been consumed (`logon_inbound_advanced`), giving **`store == manager == 2`** on a persistent store.
The **outbound** counter stays re-based to `seqnum_min` so the reply Logon's `MsgSeqNum` is
byte-identical (seq 1); only the `789` *content* corrects 1→2 (027-on). Persisting 2 is correct
write-through for the consumed seq-1 reset Logon (a *surviving net-advance*); INV-H1
(`store ≤ manager`) holds with **equality** — this is NOT the 029 over-persist class (which is
`durable > manager` with no surviving advance;
[[feedback_unconditional_persist_at_multiexit_gate_breaks_lowerbound]]).

**INV-H1 equality is guaranteed by a scoped-fatal reset, NOT asserted on faith:** on a persistent
store the received-141 durable reset is made **fatal** (disconnect on store-reset failure), so
persist-to-2 only ever runs **after** a known-good reset — there is no path where it advances a
*stale* store to `N+1` (which on a session with N>1 received messages would give `store > manager`,
the 029 silent-inbound-skip-on-restart harm). A swallowed (`logged`) reset failure followed by
persist-to-2 is exactly that over-persist; the fatal flip closes it. This aligns with 029 **D-3**
("inbound-correctness failures are fatal") and the existing fatal reset sites (`session.cpp:682`
initiator knob, `:1764` acceptor knob). Implementation: pass
`store_is_persistent_ ? reset_disposition::fatal : reset_disposition::logged` to the shared helper
on both arms. Non-persistent stores keep `logged` (the durable write-through no-ops, INV-H4; the
reset cannot meaningfully fail) — so a received-141 reset never disconnects on a non-persistent
store. A *manager-only* restore (store left at 1) is rejected: the knob-off Logon gate has no
ResendRequest arm, so store=1/manager=2 turns into a fatal-disconnect-on-restart, re-opening the
T034 gap 029 closed (see Gate A flagged-#1).

**Both arms in scope:** the acceptor `NotConnected` Logon handler AND the **initiator**
Logon-ack `peer_ack_sent_reset_flag` arm (`session.cpp:3119` advance / reset at `:3162`,
swallow at `:3167-3169`) are **separate code paths with the identical clobber**, both reachable
— fixed symmetrically (restore + persist + the scoped-fatal disposition on each) per
[[feedback_half_restructure_symmetric_api]] (see Gate A flagged-#2). The initiator arm currently
**hand-rolls** its reset (`seqnum_mgr_.reset_to_one()` at `:3162` then `(*store_).reset()` with a
swallowed `(void)store_rst_r` at `:3167-3169`); the fix **consolidates** it onto the shared
`reset_seqnums_to_one_durable(disposition)` helper with the same scoped-fatal disposition, so the
two arms share one primitive (no hand-rolled copy that could drift from the acceptor's policy).

**Restore placement (load-bearing):** on the acceptor arm the restore+persist MUST land
between the reset call (`session.cpp:1942`, block ends `:1947`) and the 789 read (`:1974`) so the
789 advertisement reflects the corrected counter — a naive copy of the post-Active persist site
(`:2040`) passes the counter/harm witnesses but silently leaves `reply.789=1`, failing the
discriminating triple. Use a dedicated restore+persist on the arm; do NOT loosen the 029-fixed
`:2039` net-advance persist guard. On the acceptor arm only the disposition **argument** changes
(`logged`→`store_is_persistent_ ? fatal : logged`); the `if (!rst_r) { Disconnected; co_return }`
handler at `:1943-1946` already exists and simply becomes live on a persistent store-reset failure.

The 024 comment that placed the reset *after* the inbound check (for outbound byte-identity)
was correct *for the outbound side* but over-applied — it conflated "outbound reply = seq 1"
with "inbound next-expected = 1". This fix corrects the inbound side only and leaves the
outbound rebase (and the comment's outbound rationale) intact.

**Scope is small (~2 arms × restore+persist + tests), risk is in the cross-feature pins.** 7 pins
total (6 value-pins + 1 contract-witness split): 6 pre-existing **value-pins** across 013/024/027/029
currently pin the defective value (next_inbound=1 / `789=1`) — including the **initiator**
`BilateralStrict_Initiator_CountersResetToOne` — and must flip to the corrected value, each
individually re-verified as pinning *this* case (the 029 hydrate W9b has **two** sub-assertions
that both flip — `next_inbound` and `durable_inbound`); plus the 024 contract-witness (5) split
(persistent→Disconnect / non-persistent→stay-Active). See Project Structure / R-5 for the full list.

**Provenance:** prototyped + harm-confirmed on the isolated branch
`fix/acceptor-received-141-next-inbound` (manager-only restore; `session_reset_on_lifecycle`
22/22 green; harm reproduced deterministically — peer seq-2 → spurious ResendRequest). This
pipeline re-derives that fix TDD-first with the discriminating triple witness and lands it
properly with the pin updates + full verify matrix + Gate B.

## Technical Context

**Language/Version**: C++23 (Clang; asio awaitables, `std::expected`) — [const §II]
**Primary Dependencies**: `Session::on_inbound_frame` — TWO arms: (a) the acceptor received-141 arm (`session.cpp:1941`, the `reset_seqnums_to_one_durable(...)` call at `:1942` whose disposition becomes `store_is_persistent_ ? fatal : logged`, inside the `peer_sent_reset && !cfg_.reset_on_logon` block; restore+persist lands between the reset call `:1942` (block ends `:1947`) and the 789 read `:1974`); (b) the **initiator** Logon-ack arm (reset at `:3162` / swallow `:3167-3169`, after the `check_inbound` advance at `:3119`), consolidated onto `reset_seqnums_to_one_durable(disposition)` with the same scoped-fatal disposition. `SeqnumManager::set_next_inbound` (existing, used by 027 behind-side) + a durable write-through (`next_seqnum(inbound, increment=true)`, the `persist_inbound_advance_` equivalent, no-op when `!store_is_persistent_`); the `logon_inbound_advanced` local set after `check_inbound` success (~:1798); 027 `honor_peer_next_expected_`/789 advertisement (`next_inbound_unsafe()` read); `store_is_persistent_` (existing member set at open from `yields_persistent_store()`) gates the disposition. No new deps, no codegen, no wire field, no new error slot, no new config knob, **no new `reset_disposition` enum value** (the existing `fatal`/`logged` are selected per-store at the call site).
**Storage**: the existing `MessageStore`. The restore is **persist-to-2** — on a **persistent** store, after the durable reset wrote `seqnum_min`, a dedicated post-reset write-through brings the store back to `seqnum_min+1` (= 2) so `store == manager == 2` (INV-H1 holds with equality; the consumed reset Logon is a surviving net-advance, NOT 029 over-persist). The equality is **guaranteed** because the persistent-store durable reset is **fatal** (disconnect on failure), so persist-to-2 only runs after a known-good reset and never advances a stale store. On a **non-persistent** store the write-through is a no-op (INV-H4) and only the manager is restored. The normal `:2039` persist guard excludes `peer_sent_reset` (so the normal site does not persist this Logon), hence the dedicated arm-local persist; the `:2039` guard is NOT loosened. No store schema/interface change.
**Testing**: GoogleTest; ASan/UBSan/TSan; coverage llvm-cov. New + amended witnesses, per role: **acceptor** — discriminating **triple** (next_inbound==2 AND reply.MsgSeqNum==1 AND reply.789==2, 027-on); **initiator** — next_inbound==2 + harm-repro; both — harm-repro (peer seq-2 accepted, NO ResendRequest), guard (no consumed reset Logon ⇒ no restore), INV-H1 (durable store == manager == 2 after fix, asserted on the store directly), and a **fault-injection witness** (persistent store seeded to N=37 + `fail_next_reset()` on the received-141 path ⇒ session Disconnected + `reset_call_count()==1`, persist-to-2 NOT reached → store retains N=37 (last-good lower bound) — the FR-010 soundness proof; seed N>1 makes assertion (ii) genuinely discriminating). Plus: `reset_on_logon=true` knob path unchanged; policy matrix (bilateral-strict/lenient, unilateral); non-persistent store (durable write-through no-ops AND a received-141 reset failure does NOT disconnect — the split sibling of merged witness (5)). Live acceptor interop cell re-run vs QFcpp/QFJ (skip-without-counterparty). — [const §VII, §IX]
**Target Platform**: Linux/Clang Tier-1 (sanitizer matrix); the live cell runs vs QFcpp/QFJ in the parent harness.
**Project Type**: single C++ library (`fixpp`) + tests + interop-harness extension.
**Performance Goals**: one extra in-memory counter set on the received-141 path only (cold/rare); no hot-path or allocation change; default (no reset / knob-off-no-141) paths untouched.
**Constraints**: `noexcept`/`expected_t` preserved; the restore reuses the existing `set_next_inbound` awaitable + the existing durable-persist primitive (no new include into the `session.hpp` closure — [const §XV.9] N/A, confirm at verify); persist-to-2 keeps `store == manager` (equality, INV-H1 preserved, NOT over-persist); `reset_on_logon=true` and all steady-state paths **byte-identical**.
**Scale/Scope**: a guarded restore+persist (`set_next_inbound(seqnum_min+1)` + one durable write-through) plus the scoped-fatal disposition (`store_is_persistent_ ? fatal : logged`) on EACH of the two arms — acceptor received-141 (argument-only flip; existing `:1943-1946` handler) and initiator Logon-ack (consolidate the hand-rolled reset onto the helper) (~12–15 effective LoC + comments); net-new regression tests in `tests/session/test_reset_on_lifecycle.cpp` (acceptor triple + initiator witness + harm-repro + guard + INV-H1 + fault-injection); **7 pins** (6 value-pins + the witness-(5) contract-amendment split). No FSM state, no new store/config surface, no new `reset_disposition` enum value, no codegen/C-ABI/wire change.

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked after Phase 1.*

| Article | Gate | Status |
|---------|------|--------|
| **II** Language | C++23/Clang, no new deps | ✅ PASS |
| **VI** Spec coverage | **No net-new catalogue row** — this corrects behavior already owned by 024 (received-141 reset) under existing row **S-017** (`spec/feature-catalogue.md:37`); the 027 row **S-031** (789 advertisement) gains a corrected received-141 sub-behavior; **S-032** (the dedicated `[FIX-SL §4.4.2]` ResetSeqNumFlag row, currently `backlog`) is the section authority. Amend S-017 + S-031 notes to cite 030 as the conformance correction, and add a **behavior** entry (received-141 nets next-expected-inbound=2, reference-engine-conformant) + a stale-pin note. `spec.md` carries a **Normative References** section (`[FIX-SL §4.4.2]` ResetSeqNumFlag, `[FIX-SL §4.4]` session-initiation/logon-sync, `[FIX-SL §4.4.1]` NextExpectedMsgSeqNum, `[FIX-SL §4.8.2]` ResendRequest) per §VI.5. Exact §VI delta below (Polish). | ⚠ RESOLVED (delta specified) |
| **VII** Testing/TDD | RED-first: (1) acceptor discriminating **triple** (027-on); (1b) **initiator** witness — next_inbound==2 on the `peer_ack_sent_reset_flag` arm; (2) harm-repro (both arms) — post-141 peer seq-2 accepted in-sequence, **NO ResendRequest** (currently RED: emits one); (3) next_inbound==2 immediately post-Logon (knob-off); (4) guard — no consumed reset Logon ⇒ no restore (no spurious set); (5) `reset_on_logon=true` path unchanged; (6) INV-H1 (durable **store == manager == 2**, asserted on the store directly); (7) policy matrix + non-persistent store hold; (8) **fault-injection** (both arms) — persistent store + `fail_next_reset()` ⇒ session **Disconnected** + error propagated, persist-to-2 NOT reached, no `store > manager` (FR-010 soundness proof); (9) the **witness-(5) split** — merged `ResetOnLogon_Off_Inbound141_StoreFailure_StillActive` flips to assert **Disconnect** on its default-persistent factory, + a NEW non-persistent sibling (`yields_persistent_store()==false`) retaining **stay-Active**. The 2 prototype tests (`ResetOnLogon_Off_Received141_NextInboundIsTwo`, `Received141_PeerNextMsgSeq2_HarmCheck`) seed (2)/(3) on the acceptor. | ✅ planned |
| **VII.6** Interop | live acceptor cell: peer `141=Y` reset vs QFcpp/QFJ → session reaches Active, **zero** fixpp ResendRequest, peer seq-2 accepted (SC-001, the true close-out) | ✅ planned |
| **VIII.5** Allocator | restore is a counter set (no container, no frame body); no new allocation; the existing no-heap witnesses on the reset path remain green | ✅ PASS (no new alloc) |
| **IX.1** Coverage | ≥95/85 on the new guarded restore+persist branch on BOTH code paths (acceptor received-141 + initiator Logon-ack; `logon_inbound_advanced` true→restore, false→skip) + the failure arm of `set_next_inbound` and the durable write-through + the **scoped-fatal reset-failure arm** (persistent store-reset failure → Disconnected, covered by the fault-injection witness) on both arms | ✅ planned |
| **IX.2** Sanitizers | ASan/UBSan/TSan on the received-141 session path + the amended/new reset tests + interop ctest | ✅ planned |
| **X** ABI | no C-ABI/error-slot/wire change; reuses an existing `SeqnumManager` method; source rebuild only | ✅ source rebuild (no surface change) |
| **XI.4** Threading | the restore runs on the existing session strand inside the inbound handler (same strand as the reset it follows); reuses `set_next_inbound`'s `async_mutex`; no new concurrency surface | ✅ PASS |
| **XII.5** No-implicit-default | **no new config flag** — the fix adds no knob; it is gated only by the pre-existing `peer_sent_reset && !reset_on_logon` condition and the `logon_inbound_advanced` consume-guard. The scoped-fatal disposition and persist-to-2 are keyed on the existing **runtime** `store_is_persistent_` (derived from the configured store's `yields_persistent_store()`), NOT a new config option — the behavior is store-determined, not user-toggled | ✅ PASS (no new knob) |
| **XIV.2** Pluggable ≤5 pure-virtual | no interface touched (`MessageStore` stays at 4; the restore+persist reuses existing methods, adds no virtual) | ✅ PASS |
| **XV.9** Banned (`std::mutex` in awaitable hdr) | no new include; `set_next_inbound` already in the awaitable corpus | ✅ N/A (confirm at verify) |
| **XVI.3/4** /clarify before /plan | Session 2026-06-10 — no user-facing decisions; the one assumption grounded by the QFcpp/QFJ source sweep (reset-then-increment; QFJ 2202-2204 infers reset from MsgSeqNum==1) | ✅ PASS |
| **XVII.1** Gate A before /tasks | mandatory — runs after this plan, before `/speckit-tasks` | ⏳ PENDING (Gate A next) |

**Result**: PASS to proceed. No new config/ABI/wire/interface surface; on a **persistent** store
INV-H1 is preserved with equality (`store == manager == 2`), **guaranteed** by the scoped-fatal
durable reset (persist-to-2 runs only after a known-good reset); on a non-persistent store the
write-through no-ops and only the manager advances. The fatal-when-persistent disposition amends
the 024 FR-001/C2.6 I-07 contract for the persistent received-141 sub-case (now disconnect, was
stay-Active). Default and `reset_on_logon=true` paths byte-identical. Decisions **explicitly
flagged for Gate A** below (resolved at rounds 1–2 — see the `## Gate A` section).

### Flagged for Gate A

1. **Persistence: manager-only vs persist-to-2 — RESOLVED (Gate A round 1: persist-to-2).**
   The bundle's original design restored only the in-memory manager, leaving the store at
   `seqnum_min`. Gate A round 1 **rejected** that as unsafe: the knob-off Logon gate has **no
   ResendRequest arm** (`session.cpp:1789-1793` — knob-off too-high falls to
   `record_state_transition_(Disconnected)`; pinned by `test_persistent_seqnum_hydrate.cpp:1509-1512`),
   so store=1/manager=2 turns into a **fatal-disconnect-on-restart** (hydrate seeds 1, peer
   seq-2 reads too-high), re-opening the exact T034 inbound-persistence gap 029 closed,
   localized to this path. The fix **persists the store to 2 as well** (a dedicated post-reset
   write-through), giving `store == manager == 2`. The 029 over-persist lesson
   ([[feedback_unconditional_persist_at_multiexit_gate_breaks_lowerbound]]) resolves the
   **opposite** way here — the discriminating question "is there a surviving net-advance?" is
   **YES** (the consumed seq-1 reset Logon), so `store == manager` is *equality, not
   `durable > manager` over-persist*; INV-H1 holds. The witness asserts `store.durable_inbound
   == seqnum_min+1` (== 2) and `store == manager` **directly** (the 029 W9b proxy-gap lesson).
   **Gate A round 2 refinement:** the equality is true only because the persistent-store durable
   reset is made **fatal** (see flagged-#4) — a swallowed (`logged`) reset failure would leave the
   store stale and persist-to-2 would advance it to `N+1` (`store > manager`, the 029 harm). The
   original wording ("INV-H1 holds with equality" unconditionally) was the round-2 P1 defect.

2. **Role scope — RESOLVED (Gate A round 1: BOTH arms in scope).** The acceptor and initiator
   received-141 paths are **separate code paths** (acceptor `NotConnected` Logon handler vs
   initiator `peer_ack_sent_reset_flag` Logon-ack arm — reset at `session.cpp:3162`, swallow at
   `:3167-3169`) with the
   **identical clobber**, and the initiator arm is **reachable** (bilateral_strict initiator
   that sends `141=Y` + peer ack; bilateral-lenient / unilateral). Gate A round 1 **rejected**
   the bundle's acceptor-only scope: it had cited 025's L-025-2 unreachable-arm lesson
   ([[feedback_symmetric_api_claim_unreachable_arm]]) to license a deferral — a **mis-application**
   (L-025-2 covers an engine-*unreachable* effect; this arm is reachable with identical harm).
   The governing lesson is [[feedback_half_restructure_symmetric_api]]: **fix both arms
   symmetrically in one pass** (restore+persist on each) with **symmetric witnesses** (acceptor
   triple + initiator next_inbound==2/harm-repro). No L-030-1 deferral. (The `reply.789` clause
   is acceptor-reply-specific — the initiator builds no reply Logon on this arm; 789-advertise is
   acceptor-role here, a later initiator re-advertise derives from the same counter per 027.)

3. **Blast-radius pin re-verification — RESOLVED (7 pins: 6 value-pins + 1 contract split).**
   Gate A round 1 confirmed the 5 acceptor-side value-pins pin *this* off-by-one *value* (justified
   corrections); the 029 hydrate W9b is one test with **two** sub-assertions (`next_inbound` 1→2 at
   `:1587` AND `durable_inbound` 1→2 at `:1610`) that both flip under persist-to-2 (its "reset won
   over hydrate" comment inverts). The `next_expected_msgseqnum` rename
   `AcceptorReplyReceived141_Advertises1 → _Advertises2` is a content correction, not a semantic
   redefinition. **One of the 6 value-pins is the initiator cell**
   `BilateralStrict_Initiator_CountersResetToOne` (`test_reset_seqnum_policy_matrix.cpp:593-594`,
   `next_inbound` 1→2) — an EXISTING pin of the *initiator* off-by-one on the `peer_ack_sent_reset_flag`
   arm FR-009 corrects (round 1's "no initiator pin / witnesses net-new" claim was false; found at
   Gate A round 3). The net-new FR-009 initiator witnesses sit alongside this pin, they do not replace it.
   **Gate A round 2 adds the contract pin in a DIFFERENT category** (a contract behavior, not a counter
   value): merged 024 witness (5) `ResetOnLogon_Off_Inbound141_StoreFailure_StillActive`
   (`tests/session/test_reset_on_lifecycle.cpp:531-558`) asserts stay-Active under a swallowed
   store-reset failure; its `StoreDoubleFactory` inherits the default `yields_persistent_store()
   ==true`, so the flagged-#4 fatal flip hits it. It is **split**: the persistent variant flips to
   assert **Disconnect**; a NEW non-persistent sibling (`yields_persistent_store()==false`) retains
   stay-Active. Total = 6 value-pins + 1 contract-witness split = **7 pins**.

4. **Scoped-fatal received-141 durable reset (persistent stores) — RESOLVED (Gate A round 2,
   user decision: Option (a)).** Gate A round 2 (Codex P1 + Opus judge CONFIRM) found the
   persist-to-2 soundness claim is **false under a swallowed (`logged`) store-reset failure**: the
   helper (`session.cpp:505-533`) propagates the in-memory `reset_to_one()` (→ manager {1,1}) but
   **swallows** the durable `store_->reset()` on `logged` (`:527-528`); the initiator hand-rolls the
   same swallow (`:3167-3169`). On that fault path the store stays stale-N while the manager reaches
   the reset base, and persist-to-2 then advances the **stale** store N→N+1 → `store > manager` for
   any N>1 (INV-H1 violation, the 029 silent-inbound-skip-on-restart harm). **Resolution (user):
   make the durable reset FATAL when the store is persistent** (`store_is_persistent_ ? fatal :
   logged`), so persist-to-2 only runs after a known-good reset and `store == manager == 2` truly
   holds; a persistent-store reset failure DISCONNECTS (no inconsistent durable state — the session
   re-opens, re-hydrates stale-N, peer re-drives the reset). Aligns with 029 **D-3** and the
   existing fatal sites (`:682`/`:1764`). Rejected alternatives: (b) skip-persist-on-failure — does
   NOT restore INV-H1 (store still stale-N > manager-2); (c) gate on `store_is_persistent_` +
   document under an L-030-x — fixes nothing (the over-persist exists ONLY on persistent stores, the
   case (c) would document). Non-persistent stores are unaffected (write-through no-ops, INV-H4; no
   hydrate-on-restart, so no over-persist hazard; the reset stays effectively `logged`). The
   initiator arm is **consolidated** onto the shared `reset_seqnums_to_one_durable(disposition)`
   helper (was a hand-rolled `reset_to_one()` + swallowed `(*store_).reset()`) so both arms share
   one disposition primitive ([[feedback_half_restructure_symmetric_api]]). This **amends the 024
   FR-001/C2.6 I-07 contract** for the persistent received-141 sub-case (now disconnect, was
   stay-Active) — see flagged-#3's witness-(5) split. A **fault-injection witness** (persistent
   store seeded to N=37 + `fail_next_reset()` ⇒ Disconnected + `reset_call_count()==1` + `store==37`
   retained) is the soundness proof on both arms.

**Exact §VI delta (applied at Polish):**
- `spec/feature-catalogue.md`: amend the **S-017** row (`:37`, "ResetOnLogon / ResetOnLogout / ResetOnDisconnect", owned by 024 — the row that carries the received-141 post-check reset rationale) Notes + the 027 row **S-031** (`:347`, 789) to cite `030-received-reset-inbound-advance` as the conformance correction (received-141 nets next-expected-inbound=2, store==manager==2; acceptor reply 789=2). **Also record on S-017 the 024 FR-001/C2.6 I-07 contract amendment**: a received-141 durable reset failure on a **persistent** store is now **fatal** (disconnect), not logged-then-proceed (non-persistent keeps stay-Active). Optionally touch S-005/S-014 only for the spurious-ResendRequest symptom. **Do NOT amend S-024** (it is BeginSeqNo/EndSeqNo on ResendRequest, `:48` — unrelated to reset semantics). Note **S-032** (the dedicated `[FIX-SL §4.4.2]` ResetSeqNumFlag row) exists, currently `backlog`. No new S-row.
- `spec/coverage-index.md`: add a coverage note mapping the new guarded restore+persist branch (both arms) ↔ `tests/session/test_reset_on_lifecycle.cpp` (acceptor triple + initiator witness + harm-repro + guard + INV-H1 + fault-injection + the witness-(5) split) under the existing 024/027 entries.
- `spec/behaviors-and-limitations.md`: add **B-030-1** (received-141 reset advances next-expected-inbound to 2 — reference-engine-conformant; store==manager==2 on a persistent store; reply MsgSeqNum stays 1, reply 789=2 when 027 on; both acceptor and initiator arms). Add **B-030-2** (a received-141 durable reset failure on a **persistent** store is fatal — disconnect — guaranteeing INV-H1; non-persistent stores keep the 024 logged-then-proceed / stay-Active behavior). **No L-030-1** — both arms are in scope and witnessed.
- **Obsolete-prose grep-sweep (stale-doc-bundle-drift, [[feedback_verify_caught_design_pivot_stale_doc_bundle_drift]]):** ONE exhaustive sweep — NOT per-finding patches — to amend two now-obsolete framings: (1) the "`next_inbound` 1→2 breaks byte identity" rationale (030 establishes `next_inbound` 1→2 is the **correct** post-state — the consumed reset Logon, not a byte-identity break; the byte-identity that matters is the *outbound reply* seq, independent); and (2) the "received-141 reset failure is logged-then-proceed / session still reaches Active / the 024 fatal disposition must not bleed onto this all-off path" framing, now obsolete for **persistent** stores (flagged-#4 makes it fatal). Needles: `breaks byte identity` / `breaking byte identity` / `reset won over hydrate` / `next_inbound 1→2` / `logged-then-proceed` / `still reach Active` / `must not bleed onto this all-off path`. Known sites: `spec/behaviors-and-limitations.md:571-575` (the **B-024-1** block) and the **S-017** Notes in `spec/feature-catalogue.md:37` (both carry the obsolete byte-identity prose); the 029 hydrate W9b "reset won over hydrate" comment; the `session.cpp` persist-guard comment (`:2031-2032`) and the 024 reset comment; **and the I-07 source comments the fatal flip obsoletes — `session.cpp:1938-1940` (the "Logged (I-07): a store failure is swallowed; the session still reaches Active" block), the helper comment at `:518-520`/`:527` (logged path), and the witness-(5) comments at `test_reset_on_lifecycle.cpp:525-528`/`:548-557` ("024 fatal disposition MUST NOT bleed onto this all-off path" + the stay-Active rationale)**. These source-comment edits land in `/implement`, not in this docs bundle — listed here only so the sweep is exhaustive. Preserve correct uses of "1→2 is the fix" and the non-persistent stay-Active behavior — only the persistent-store-obsoleted framings change.

## Project Structure

### Documentation (this feature)

```text
specs/030-received-reset-inbound-advance/
├── plan.md              # this file
├── spec.md              # /speckit-specify + /speckit-clarify output
├── research.md          # Phase 0 (reference-engine grounding + design decisions)
├── quickstart.md        # Phase 1 (how to reproduce harm + verify the fix)
├── checklists/
│   └── requirements.md  # spec quality checklist
└── tasks.md             # /speckit-tasks output (NOT created here)
```

No `data-model.md` or `contracts/` — this feature introduces **no new entities and no new
external interface** (it corrects an internal counter on an existing path). Per the plan
template, those artifacts are skipped as not-applicable.

### Source Code (repository root = library submodule)

```text
src/session/session.cpp            # acceptor received-141 arm: reset call :1942 (block ends :1947) disposition
                                   #   logged→(store_is_persistent_ ? fatal : logged); + guarded restore+persist
                                   #   between :1942 and the 789 read :1974 (handler :1943-1946 already exists);
                                   # initiator Logon-ack arm: consolidate reset :3162 / swallow :3167-3169 onto
                                   #   reset_seqnums_to_one_durable(scoped-fatal); + symmetric guarded restore+persist
tests/session/test_reset_on_lifecycle.cpp   # + acceptor triple, + initiator witness, + harm-repro, + guard, + INV-H1,
                                   #   + fault-injection (persistent reset-fail → Disconnect, no store>manager);
                                   #   SPLIT merged witness (5) ResetOnLogon_Off_Inbound141_StoreFailure_StillActive
                                   #   (:531-558) → persistent variant asserts Disconnect + NEW non-persistent
                                   #   (yields_persistent_store()==false) sibling retains stay-Active
tests/session/test_reset_seqnum_policy_matrix.cpp   # pin flips ×4 (next_inbound 1→2): 3 acceptor + BilateralStrict_Initiator_CountersResetToOne :593-594 (initiator off-by-one)
tests/session/test_persistent_seqnum_hydrate.cpp    # pin flip ×1 test / W9b — 2 sub-asserts: next_inbound :1587 + durable_inbound :1610, both 1→2
tests/session/test_next_expected_msgseqnum.cpp      # pin flip ×1 (:1441 AcceptorReplyReceived141_Advertises1 → _Advertises2)
```

**Pin count: 7** — 6 value-pins (the 3 acceptor policy-matrix + 1 hydrate-W9b-with-2-sub-asserts +
1 next-expected + 1 initiator policy-matrix `BilateralStrict_Initiator_CountersResetToOne` `:593-594`)
**+** 1 contract-witness split (witness (5), `test_reset_on_lifecycle.cpp:531-558`).

**Structure Decision**: surgical edits to BOTH the acceptor received-141 arm and the initiator
Logon-ack arm in `session.cpp` (separate code paths, symmetric fix); all test work lands in the
existing session test files (no new test target). The exact pin file names/paths are confirmed
in `/speckit-tasks` via grep (the corrected paths above already resolve to real files).

## Complexity Tracking

No constitution violations to justify. The only "complexity" is the cross-feature pin flips,
which are tracked as a first-class deliverable (SC-003) with a per-pin re-verification
requirement, not hidden as incidental edits.

## Gate A

- Round 1 applied 2026-06-10: Codex P1=1 P2=3 P3=1; Opus post-judging P1=2 P2=3 P3=1; rewrite addresses 3 root clusters (persistence flip manager-only→persist-to-2, initiator arm in-scope, Article VI+doc-fidelity) + restore-placement constraint + witness/pin recount. Reviews: research/reviews/codex_030-received-reset-inbound-advance_gate_a_review.md, research/reviews/opus_030-received-reset-inbound-advance_gate_a_adversarial_review.md.
- Round 2 applied 2026-06-10: Codex P1=1 P2=0 P3=2; Opus post-judging P1=1 P2=0 P3=3; rewrite encodes the user decision (scoped-fatal reset for persistent stores) closing the persist-to-2-under-swallowed-reset-failure P1, the 024 FR-001/C2.6 contract amendment (persistent-store received-141 reset failure now disconnects), blast-radius 5→6 pins (witness (5) split), a fault-injection witness, initiator-arm helper consolidation, and the two P3 cite/checklist fixes. Reviews: research/reviews/codex_030-received-reset-inbound-advance_gate_a_2_review.md, research/reviews/opus_030-received-reset-inbound-advance_gate_a_2_adversarial_review.md.
- Round 3 applied 2026-06-10 (post-exhaustion targeted amendment, user-approved): Codex P1=0/P2=0/P3=0; Opus post-judging P1=1 (blast-radius undercount, not a design defect). Corrected pin count 6→7 (added value-pin BilateralStrict_Initiator_CountersResetToOne, next_inbound 1→2), deleted the false "no initiator pin" claim (spec/research/plan), fixed the discovery grep needle. No design change. Reviews: research/reviews/codex_030-received-reset-inbound-advance_gate_a_3_review.md, research/reviews/opus_030-received-reset-inbound-advance_gate_a_3_adversarial_review.md.
