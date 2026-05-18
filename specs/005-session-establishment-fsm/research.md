# Phase 0 — Research & Decisions — 005-session-establishment-fsm

**Anchors:** `.specify/2e-msgstore.md` v0.4, `.specify/2d-threading.md` v0.4, `.specify/2f-async-mutex.md` v1.5, `[FIX-SL]`, `[FIX-TC]`. OSS grounding: `reference-engines/{quickfix-cpp@v1.16.0, fix8@1.4.3, quickfixj@QFJ_RELEASE_3_0_1}` (gitignored, CodeGraph-indexed; see memory `project_reference_engines_setup`). On conflict the anchor wins. All NEEDS-CLARIFICATION items from the spec are resolved here; the 3 user-answered `/clarify` decisions (Q1/Q2/Q3, 2026-05-17) are carried in as fixed inputs.

---

### D-1 — `seqnum_t` width & the `[2e §10 Q9]` handoff mechanism

**Decision:** `using seqnum_t = std::uint32_t;` with `seqnum_min = 1`, `seqnum_max = std::numeric_limits<std::uint32_t>::max()`, published from `include/fixpp/session/seqnum.hpp` — the **same path** `2e`'s placeholder occupies. The handoff (`[2e §10 Q9]` / `[2e §3.1]` / `[2e §4.7]`) is executed **in place**: 005 takes ownership of that file, deletes the `// PLACEHOLDER ALIAS` comment block, and the alias + constants stay byte-identical, so every existing `2e` `#include <fixpp/session/seqnum.hpp>` resolves to the now-canonical 005 type with **zero edits to 2e code** (`[const §VI.5]` single-line-edit handoff; mechanism = "(a) re-export" per `[2e §4.7]`, realized as in-place promotion).

**Rationale:** (1) matches `2e`'s bound placeholder exactly → minimal cross-doc churn, no 2e include rewiring, no widening of a C-ABI shape 2i has not yet frozen. (2) Matches observed-implementation convention: QuickFIX C++ (`SEQNUM`/`int`, 32-bit), fix8 (`unsigned`), QuickFIX/J (`int`) — `[2e §4.7]` explicitly names this convention. (3) Overflow is already wired session-fatal via `[2e §6.7] store_seqnum_overflow` (no wrap) — consistent with FR-008/FR-009/SC-003.

**Alternatives considered:** `std::uint64_t` (rejected — `[2e §1.2]` magnitude domain has no v1.0 never-reset use case; would force a 2e header rewrite + a wider, not-yet-frozen 2i C-ABI shape; all three reference engines are 32-bit). `std::uint64_t` remains a clean post-v1.0 widening if a never-reset use case appears (single-file edit, the handoff seam is preserved).

---

### D-2 — FSM state model (Session-2026-05-18 / Gate A round 1 — supersedes the original `RecoveryPending` decision)

**Decision:** 6 states — `NotConnected`, `LogonSent`, `LogonReceived`, `Active`, `LogoutSent`, `Disconnected` — the exact `[FIX-SL §4.10]` state set, **no bundle-invented `RecoveryPending` half-state**, with the `[FIX-SL §4.10]` transition matrix (every state×event cell in the FR-001 event alphabet defined; no implicit/undefined transition, no silent no-op — see data-model.md transition matrix + guard precedence). A detected `MsgSeqNum`-too-high gap is a **session-fatal** transition: surface `session_seqnum_gap_unrecoverable`, emit an orderly Logout-with-text, disconnect. **No** ResendRequest/SequenceReset is emitted (that recovery is deferred). Receipt of out-of-scope admin (`ResendRequest`/`SequenceReset`) is a defined bounded transition (`session_admin_not_supported`).

**Rationale (Gate A round 1 — corrected OSS survey, 2026-05-18):** The original Session-2026-05-17 Q1 survey was verified **inverted** against the bound oracle and the pinned engines:
- QuickFIX/J `.def` oracle (the plan's own executable oracle), `fix42/1a_ValidLogonMsgSeqNumTooHigh.def` and `fix42/2b_MsgSeqNumTooHigh.def`: both **require an outbound `E…35=2…` ResendRequest** on the gap (the file headers literally say "send a resend request"). A passive hold-emit-nothing state fails the QFJ exact-transcript comparison on the **first `E` line**.
- QuickFIX C++ `Session.cpp` `doTargetTooHigh`: `queue(...)` **then immediately** `generateResendRequest(...)`; and enters recovery from the logon state too.
- fix8 `runtime/session.cpp` `sequence_check`: `send(generate_resend_request(...)); do_state_change(st_resend_request_sent);` — the state transition is **inseparable** from emitting the ResendRequest; `st_resend_request_sent` exists *because a ResendRequest was just sent*, it is not a hold-only seam.

All three engines **couple gap-detection to ResendRequest emission** and enter recovery at logon time — the *opposite* of the original answer. There is no `[FIX-SL §4.10]` half-state and no FIX Session Layer section that sanctions "detect a gap, hold, emit nothing, stay connected"; `[FIX-SL §4.8.2]` *is* the ResendRequest section (owned by deferred S-005/S-024), so it cannot ground a no-ResendRequest state. Since ResendRequest/SequenceReset/gap-fill is explicitly deferred (FR-017), the only option green-able against the bound oracle in isolation is the **smaller honest slice**: too-high = session-fatal disconnect for 005, the too-high oracle cases (`1a_ValidLogonMsgSeqNumTooHigh`, `2b_MsgSeqNumTooHigh`) move to deferred-with-traceability, and the real ResendRequest-driven recovery is the deferred session-recovery feature's — which `[2e §3.1]`/`[2e §4 last bullet]` already forward-reference to the Phase-4 session line (the *successor* recovery feature, not this establishment slice), so this re-scope contradicts no signed-off anchor. (Memory `project_reference_engines_setup` caveat stands: engines inform FSM shape; `[FIX-SL]` is primary for phasing — and `[FIX-SL]` has no passive-hold state, confirming the re-scope.)

**`LogonReceived`:** retained as the acceptor-side intermediate (`NotConnected → LogonReceived → Active`) per `[FIX-SL §4.3.10]`; the initiator path is `NotConnected → LogonSent → Active`. Simultaneous-logon resolves to a single `Active` without double-confirm (spec edge case; matched against QuickFIX/J `1b_DuplicateIdentity` / `20_SimultaneousResendRequest` analogues).

---

### D-3 — Time-helper #4 home, grammar & the Q3 stale-SendingTime disposition

**Decision (home):** the folded `core/` time-helper row #4 lives in **`include/fixpp/core/fix_time.hpp` + `src/core/fix_time.cpp`** (`core/`, NOT `session/`). It reuses `[2d §4.1]`'s `fixpp::core::utc_time_point` (`std::chrono::time_point<std::chrono::system_clock>`) and adds a `fixpp::core::duration` alias; functions `utc_time_to_fix_string(utc_time_point, precision) → fixed-capacity buffer` and `fix_string_to_utc_time(std::span<const char>) → expected_t<utc_time_point>`. **Grammar:** FIX UTCTimestamp `YYYYMMDD-HH:MM:SS` with optional `.sss` (ms, FIX 4.x default) or `.ssssss` (µs where the negotiated version permits); never coarser than seconds; a format→parse round trip is lossless at the emitted precision. Folding it here (vs a standalone `002-core-time`) closes the `core/` module-exit row #4 with a real consumer (`SendingTime(52)`), per the 2026-05-17 carry-in resolution (session README); `tasks.md` carries explicit time-helper tasks + flips `core/`'s exit checkbox at merge.

**Decision (Q3 disposition, fixed by /clarify):** inbound `SendingTime(52)` diverging from `effective_clock.now()` by more than `MaxLatency` → emit `Reject` with `SessionRejectReason=10` (SendingTime accuracy) referencing tag 52, then `Logout`, then disconnect — **except** when the offending message is the `Logon` itself, in which case **logout-with-error, no standalone reject** (no session is established yet). Engine-unanimous: QuickFIX C++/J `doBadTime` (`generateReject(BAD_TIME=10)` + `generateLogout`), fix8 `BadSendingTime`; matches `[FIX-SL §4.2.3]`. QuickFIX/J oracle: `1d_InvalidLogonBadSendingTime.def` (Logon path) vs `2o_SendingTimeValueOutOfRange.def` (established path).

---

### D-4 — MessageStore seam consumption scope

**Decision:** 005 **consumes** the `[2e §4.1]` 4-pure-virtual `MessageStore` (`store`/`retrieve`/`next_seqnum`/`reset`) + `retrieve_visitor`; it does **not** define, redefine, or implement it. In this feature only `store()` (durable-before-transmit) and `next_seqnum()` (counter read/increment) are exercised on the live path; `retrieve()` + the awaitable visitor walk is the **deferred recovery feature's** seam (FR-010/FR-017); `reset()` is not called (ResetOn* / S-017 deferred). A `tests/support/store_double.hpp` in-memory test-double satisfies the seam (NOT the production `MemoryStore` S-012). Ordering per `[2e §root cause #1]`/`[2e §7.6]`: **outbound** = post-`wire::Writer::commit`, pre-`transport::async_write`, `co_await store->store(seq, committed_span, outbound)`; **inbound** = post-Parser, pre-`fromAdmin`/`fromApp`. `next_seqnum` callsites per `[2e §7.6]`: outbound read-without-increment to stamp `MsgSeqNum(34)`, then post-`store()` increment; inbound read-with-increment after validate. `store_seqnum_overflow` is surfaced session-fatal through a session-level error callback (`[2e §6.7]`/`[2e §7.6]` N6).

**Rationale:** spec FR-010 + `[2e §1.1]` ("2e defines the seam; the session-module spec walks it") + `[2e §7.6]` enumerate exactly these callsites. Walking `retrieve()` requires ResendRequest/GapFill logic = deferred. A test-double satisfying the 4 methods keeps the feature independently testable without S-012/S-013.

---

### D-5 — Clock seam + `effective_clock`

**Decision:** consume `[2d §4.1]` `fixpp::core::Clock` (4-pv: `now`/`steady_now`/`sleep_until`/`cancel_sleeps`, `include/fixpp/core/clock.hpp`). The session resolves `effective_clock = SessionConfig::clock_override ?: EngineConfig::clock` **once at `Session::open`**, bound to session lifetime (`[2d §7.9]`, NFR-015). Usage: `steady_now()` for heartbeat/test-request/graceful-close *elapsed* deltas; `now()` for the outbound `SendingTime(52)` wire stamp **and** the inbound MaxLatency check (`[2d §6.6]` per-call discipline); `sleep_until(steady deadline)` for every timer (heartbeat, test-request, graceful-close), armed under the awaiter's cancellation slot. `fixpp::core::mock_clock` (`[2d §4.3]`) drives all timed tests deterministically (no real wall-clock anywhere on the session path).

**Rationale:** `[2d §7.9]` root-cause-2 effective-clock rule + `[2d §6.6]` (`steady_now` for elapsed, `now` for wire timestamps) are prescriptive; `mock_clock` as `clock_override` is exactly how the conformance corpus achieves determinism (`[2d §7.9]` closes §10 Q5 on this).

---

### D-6 — Cancellation composition (two-phase + cancellable_dispatch)

**Decision:** per `[2d §6.5]` — coroutines + ASIO native cancellation slots end-to-end, **no `stop_token`** (`[const §XI.2]`). The parser-completion → `fromAdmin`/`fromApp` hand-off goes through `fixpp::core::cancellable_dispatch(session_executor, slot, handler)` (`[2d §6.5]`): a slot signalled before pickup reaps the handler (`expected_t<void>{unexpect, dispatch_aborted}`); signalled during, ASIO standard "next checkpoint" semantics. `Session::close(graceful)` opens a **child** `asio::cancellation_state`; the Logout `async_write` and the `Clock::sleep_until` close-timeout run under the child — phase-2 root `cancellation_type::total` fires only after phase-1 resolves. `Session::close(terminal)` skips phase 1. `close()` is **idempotent** (second call same outcome, no side effects; never-opened/already-closed → `session_already_closed`). The graceful-close timeout *value* is owned by 005 (D-8).

**Rationale:** `[2d §6.5]` specifies the three deterministic fire/not-fire cases for `fromApp` and the two-phase Logout exchange verbatim; 005 is the first consumer that exercises it.

---

### D-7 — `async_mutex` usage

**Decision:** consume `fixpp::sync::async_mutex` (`[2f §4.1]`, `include/fixpp/core/sync/async_mutex.hpp`) for the seqnum-counter bookkeeping (`[2f §7.3]`/`[2f §7.6]`) — defence-in-depth; under the v1.0 per-session-strand single-domain discipline contention is structurally zero. Per `[const §XI.5]` the store-write/seqnum path is **mutex regardless of `SessionConfig::lock_policy`** (spin opt-in does not apply there). Graceful close issues `co_await mutex.cancel_and_drain()` on every session-owned mutex before destroying its owner (`[2f §4.7.3]`/`[2f §7.6]`). No `std::mutex` in any header that includes `asio::awaitable<...>` (`[const §XV.9]` grep gate).

**Rationale:** `[2f §7.3]`/`[2f §7.6]` name the seqnum counter as the Phase-4 consumer explicitly; the cap/lock-policy rule is `[const §XI.5]`.

---

### D-8 — Threshold default values 005 owns

`[2d §4.5]` declares `heartbeat_interval`, `test_request_threshold`, `sending_time_threshold` (all `std::optional`) and `RejectPolicy reject_policy` but explicitly defers their **values** to the session-module spec (this feature). **Decisions:**

| Knob | Default | Source / rationale |
|---|---|---|
| `heartbeat_interval` | **30 s** | QuickFIX/J `HeartBtInt` default; common FIX-engine practice; caller-tunable. `HeartBtInt=0` ⇒ heartbeating disabled (`[FIX-SL §4.3.4]`). |
| `test_request_threshold` (inbound-silence grace) | **1 × negotiated `HeartBtInt`** | `[FIX-SL §4.5.5]` "no response within a reasonable transmission time" → one heartbeat interval (QuickFIX/J `4a_NoDataSentDuringHeartBtInt` semantics). |
| `sending_time_threshold` (`MaxLatency`) | **120 s** | QuickFIX `MaxLatency` default; `[FIX-SL §4.2.3]`. Caller-tunable; not a FIX-spec invariant. |
| `reject_policy` | **`strict_reject_then_logout`** | already the `[2d §4.5]` default; matches Clarification Q3's `Reject(10)→Logout→disconnect`. |

These are `SessionConfig`-tunable knobs (rules-of-engagement, not spec invariants — coverage-index `§3.1` note). The engine substitutes these 005-defined fallbacks at `Session::open` when the optional is `std::nullopt` (`[2d §4.5]` contract).

---

### D-9 — Error variants (`session_*`, slots 43..N)

**Decision:** append new `fixpp::core::error` variants at **unused slots 43..N**, non-renumbering (`[const §X.4]`), under the per-doc-prefix discipline (2a `DECIMAL`, 2b `WIRE`, 2c `DICT`, 2d `THREAD`, 2e `STORE` → **005 `SESSION`**, C-ABI coalescing target `FIXPP_ERR_SESSION_*` owned by 2i). Current `error.hpp` occupancy: 1, 10–13, 20–29, 30–42 → first free = **43**. Provisional set (final list pinned in data-model.md, reviewed at Gate A):

- `session_invalid_logon` (BeginString/CompID/first-msg-not-Logon refusal — FR-003/004, US1#3/#4)
- `session_compid_mismatch` (FR-004), `session_begin_string_unsupported` (FR-003)
- `session_seqnum_too_low` (fatal, no-PossDup, FR-008, `[FIX-SL §4.1]`)
- `session_seqnum_gap_unrecoverable` (too-high gap → session-fatal; recovery deferred — Session-2026-05-18, FR-008/FR-001; replaces the removed `session_recovery_pending`)
- `session_admin_not_supported` (deferred admin RR/SeqReset bounded reject — FR-017)
- `session_sending_time_accuracy` (Q3, `SessionRejectReason=10`, FR-013)
- `session_msg_type_invalid_for_state` (FR-007, `[FIX-SL §4.5.4]`)
- `session_logout_timeout` (graceful-close force-disconnect, FR-005)
- `session_test_request_unanswered` (liveness unhealthy, FR-006)
- `session_invalid_config` / `session_already_closed` — **reuse the `[2d §6.5]`-named variants** if 005 (first threading consumer) is the introduction site; coordinate the minimal `[2d §6.7]` `thread_*`/cancellation variants it actually exercises (`dispatch_aborted`, `clock_sleeps_cancelled`) at the same slots. **Reuse, do not duplicate**, `[2e §6.7] store_seqnum_overflow` (already defined) for overflow.

**Cross-doc coordination note (Gate A item, not a blocker):** 005 is the first feature to *consume* the `[2d]` threading + `[2e]` store error surfaces at runtime, so it introduces the `session_*` slots **and** the minimal `[2d §6.7]` cancellation/config slots it exercises. The full `[2d]` `thread_*` set lands when a 2d-proper / `transport/` feature ships; this is a sequencing fact for Gate A to confirm, mirroring how 004 introduced the `wire_*` set ahead of a standalone 2b feature.

---

### D-10 — Q2 conformance subset: concrete TC-### split mapped to the QuickFIX/J `.def` oracle (Clarification Q2, fixed here)

**Oracle:** `reference-engines/quickfixj/quickfixj-core/src/test/resources/quickfix/test/acceptance/definitions/server/` @ QFJ_RELEASE_3_0_1 — verified server dirs: `fix40, fix41, fix42, fix43, fix44, fix50, fixLatest, future` (**no `fixt11`/`fix50sp2` dir** — Gate A round 1 finding). 005 validates against **`fix42` and `fix44`** (the codegen-namespace versions 005 covers per the SC-001 version-coverage ledger); `fix40/41/43/50` and the absent FIXT/5.0SP2 are deferred-with-traceability (version-scope deferral, coverage-index.md). `tests/session/conformance/tc_*.cpp` are parameterized GTest scenarios authored from the **in-scope** `.def` subset; deferred `.def` cases are recorded in `coverage-index.md` as deferred-with-traceability to the named later "session recovery" feature. **There is no `[const §V.5]` scoped-subset allowance** (`[const §V.5]` is the README-disclaimer rule; `[const §VII.5]` mandates the full corpus every PR) — `[const §VII.5]` is **NOT SATISFIED** for the deferred cases and the partial corpus proceeds under an explicit **Article XVII §1 recorded Gate-A-blocker waiver** (`[const §XVII.1]`, `constitution.md:255`; the article itself is not waived) with rationale (plan Constitution Check + Complexity Tracking), not a satisfied gate.

**IN SCOPE — must ship green this PR:**

| `[FIX-TC]` | Capability | QuickFIX/J `.def` oracle (per version) | Spec |
|---|---|---|---|
| TC-001 | Establishment (initiator+acceptor Logon) | `1a_ValidLogonWithCorrectMsgSeqNum`, `1b_DuplicateIdentity` | US1, SC-001 |
| TC-001/002 | CompID / BeginString / first-msg gating | `1c_InvalidSenderCompID`, `1c_InvalidTargetCompID`, `1d_InvalidLogonWrongBeginString`, `2i_BeginStringValueUnexpected`, `2k_CompIDDoesNotMatchProfile`, `1e_NotLogonMessage` | US1#3/#4, SC-002 |
| TC-002 | Standard-header validation: in-sequence + MsgSeqNum-too-low (fatal) + MsgType (2q/2r) | `2a_MsgSeqNumCorrect`, `2c_MsgSeqNumTooLow`, `2q_MsgTypeNotValid`, `2r_UnregisteredMsgType` | US2#1/#2, US5#1, SC-003/006 |
| TC-005 | Receive Reject message (scenario 7 — log RefSeqNum/SessionRejectReason, continue) | `7_*` | US5#1, SC-006 |
| TC-010 | Message validation (scenario 14; SessionRejectReason 0–16) — **scenario-14 corpus is exactly `14a`–`14j` (10 sub-cases; verified `ls fix42/ fix44/ fixLatest/` — no `14k`–`14o`). IN SCOPE: `14a`–`14g` (the session-layer reject taxonomy).** | `14a_BadField`, `14b_RequiredFieldMissing`, `14c_TagNotDefinedForMsgType`, `14d_TagSpecifiedWithoutValue`, `14e_IncorrectEnumValue`, `14f_IncorrectDataFormat`, `14g_HeaderBodyTrailerFieldsOutOfOrder` | US5#1, SC-006 |
| TC-004 | Liveness (heartbeat / test request) | `4a_NoDataSentDuringHeartBtInt`, `4b_ReceivedTestRequest` | US3, SC-004 |
| TC-009 | Logout (incl. timeout force-disconnect) | `12_*`, `13_*`, `13b_UnsolicitedLogoutMessage` | US4, SC-005 |
| (SendingTime) | MaxLatency / Q3 disposition | `1d_InvalidLogonBadSendingTime`, `2o_SendingTimeValueOutOfRange` | US5#2, SC-007 |

**DEFERRED-with-traceability (recorded in `coverage-index.md` → later "session recovery" feature):** **the too-high-seqnum cases `2b_MsgSeqNumTooHigh` and `1a_ValidLogonMsgSeqNumTooHigh`** (Gate A round 1 / Session-2026-05-18 — they require the deferred `ResendRequest(35=2)` to pass the QFJ comparison; 005 treats too-high as session-fatal, the recovery feature delivers the non-fatal ResendRequest-driven flow), TC-006 ResendRequest (`8_*`), TC-007 SequenceReset-GapFill (`10_*`), TC-008 SequenceReset-Reset (`11a/b/c_*`), TC-012 PossDup/PossResend (`2e_PossDup*`, `19a/19b_PossResend*`; S-010), TC-014 synchronize seqnums (scenario 9, optional), store-recovery / RefreshOnLogon (S-018). **Scenario-14 repeating-group/repeated-tag sub-cases `14h_RepeatedTag`, `14i_RepeatingGroupCountNotEqual`, `14j_OutOfOrderRepeatingGroupMembers`** — these are repeating-group / repeated-tag dictionary-validation cases, not the session-layer reject taxonomy 005 owns; deferred-with-traceability to a later dictionary-validation / `wire/` follow-up (005 ships `14a`–`14g` only). **Version-scope deferral:** `fix40/41/43/50` dirs and the absent FIXT.1.1/5.0SP2 (no oracle dir; FIXT logon-time `DefaultApplVerID(1137)`/`[FIX-SL §4.4]` deferred per FR-017) — 005 claims only FIX.4.2/4.4. **S-016 third-party-addressing deferral:** `OnBehalfOfCompID(115)`/`DeliverToCompID(128)` (QFJ `ReverseRoute*` defs) — recorded against a later third-party-addressing feature; 005 owns only the 49/56 point-to-point portion. Wire-level garbled/checksum/bodylength cases (`2d/3c_GarbledMessage`, `3b_InvalidChecksum`, `2m_BodyLengthValueNotCorrect`) are `wire/`'s (merged PR #68) — referenced, not re-tested here, except the session-layer "unknown MsgType / missing MsgSeqNum" slice under TC-002.

---

### D-11 — Roles in scope

**Decision:** **both initiator and acceptor** roles (a usable FIX engine needs both; spec Assumptions). One `Session` = one counterparty pair; the multi-session registry / acceptor connection demux is a transport/control-plane concern, **deferred** (not this feature). First-session sequence numbers start at 1; sequence persistence/recovery across reconnect (S-018) and reset settings (S-017) deferred — a fresh logical sequence per session lifetime; the store seam is consumed via a test double (no concrete persistent impl required, FR-010/Assumptions).

---

### D-12 — Fuzz & abidiff non-applicability (recorded)

**Decision:** **no new fuzz harness** — the session FSM is not parser-touching (`wire/` owns framing/parse fuzzing per `[const §VII.7]`; session README exit criteria: "Session FSM is not parser-like — no fuzz-harness exit gate"). **`[const §IX.5]` abidiff N/A** — no C-ABI surface added (`[const §X.2]`, FR-015; `fixpp_session_*` C ABI owned by 2i, a later feature). Both recorded for explicit non-applicability so Gate A / `/speckit-verify` mark them SKIPPED-with-reason, not silently missing (mirrors the 004 precedent).

---

### D-13 — Interop & throughput parity deferral (recorded)

**Decision:** the `[const §VII.6]` v1.0 QuickFIX Logon→NewOrderSingle→ExecutionReport→Logout interop test and the `[const §VIII.4]` session-throughput-parity-vs-QuickFIX gate require a **real socket** and the application layer — owned by `transport/` (blocked on this module) and a later milestone. This feature delivers the session-semantics substrate + the Q2 conformance subset against an in-memory transport double; interop/parity are recorded as downstream v1.0 release gates, **not this-PR blockers** (mirrors 004 research D-14).
