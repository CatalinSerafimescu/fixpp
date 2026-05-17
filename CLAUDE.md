<!-- SPECKIT START -->
**Active feature:** `005-session-establishment-fsm` — the `fixpp::session` connection-lifecycle slice: the 7-state FIX session FSM + Logon/Logout/Heartbeat/TestRequest/Reject + MsgSeqNum mgmt + HeartBtInt/CompID/BeginString gating + SendingTime/MaxLatency, plus the folded `core/` time-helper #4 (first Phase-4 `session/` feature; S-001/2/3/4/7/8/9/15/16/19/20).

For technologies, project structure, build/test commands, and gate status, read the current plan: [`specs/005-session-establishment-fsm/plan.md`](specs/005-session-establishment-fsm/plan.md). Design anchors: `.specify/2e-msgstore.md` **v0.4** (consumes the `MessageStore` seam; `seqnum_t` ownership re-pointed here), `.specify/2d-threading.md` **v0.4** (`Clock`/`effective_clock`/`cancellable_dispatch`/two-phase close/`SessionConfig`), `.specify/2f-async-mutex.md` **v1.5** (seqnum counter), `[FIX-SL]` + `[FIX-TC]` — on conflict the anchor wins. There is **no Phase-2 FSM doc by design** (`[2e §3.1]`) — Gate A is the first FSM design review of record.

Three `/clarify` decisions, user-answered 2026-05-17 (OSS-reference-grounded): **Q1** sequence gap → explicit `RecoveryPending` FSM state — 005 owns entry/hold/surface, the ResendRequest/SequenceReset exits are a deferred-feature seam. **Q2** capability-partitioned `[FIX-TC]` subset ships green; the concrete TC-### split is fixed in plan/research D-10 against the version-partitioned QuickFIX/J acceptance-definition oracle. **Q3** stale inbound `SendingTime(52)` → `Reject(SessionRejectReason=10, tag 52)`→`Logout`→disconnect (Logon→logout-with-error).

Companion artifacts in the same directory:
- [`spec.md`](specs/005-session-establishment-fsm/spec.md) — feature spec (anchored; carries /clarify Q1/Q2/Q3)
- [`research.md`](specs/005-session-establishment-fsm/research.md) — Phase 0 D-1..D-13 (D-1 seqnum_t handoff, D-2 FSM/RecoveryPending, D-3 time-helper+Q3, D-10 Q2 TC split)
- [`data-model.md`](specs/005-session-establishment-fsm/data-model.md) — E1..E9, the `[FIX-SL §4.10]` transition matrix, `session_*` error slots 43..N, SessionConfig-consumed fields
- [`contracts/`](specs/005-session-establishment-fsm/contracts/) — shape oracles: `session_fsm`/`session`/`admin_messages`/`seqnum`/`sending_time`/`fix_time`/`session_config_consumed`/`session_errors`.hpp
- [`quickstart.md`](specs/005-session-establishment-fsm/quickstart.md) — build / test / bench (TSan) / coverage / `/speckit-verify` / `/gate-a` / `/gate-b` / module-exit bookkeeping

Previous feature (merged): [`004-wire-codec`](specs/004-wire-codec/plan.md) — `fixpp::wire` module + the 2b cutover. Gate A + Gate B converged; PR #68 on `main`. Earlier: [`003-dictionary-codegen`](specs/003-dictionary-codegen/plan.md) (PR #67), [`002-dictionary-xml-loader`](specs/002-dictionary-xml-loader/plan.md) (PR #66), [`001-core-decimal`](specs/001-core-decimal/plan.md) (Gate A/B converged 2026-05-12/13).
<!-- SPECKIT END -->
