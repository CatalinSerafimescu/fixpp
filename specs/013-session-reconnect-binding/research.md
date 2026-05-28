# Phase 0 Research — 013-session-reconnect-binding

**Branch**: `013-session-reconnect-binding` | **Date**: 2026-05-28 | **Plan**: [plan.md](plan.md) | **Spec**: [spec.md](spec.md)

This document records the binding decisions D-1..D-N consumed or established by this feature, per Phase 0 of `/speckit-plan`. Every decision cites its evidence (constitution / design-doc anchor / Clarifications Q+answer / reference-engine `file:line`) so Gate A can audit grounding without re-running the `/speckit-clarify` reference-engine sweep.

**No NEEDS CLARIFICATION items remain.** The `/speckit-clarify` pass on 2026-05-28 resolved all 5 ambiguities Q1..Q5 (recorded in spec.md §Clarifications) with per-Q reference-engine sweep across QuickFIX-cpp v1.16.0 (`reference-engines/quickfix/src/C++/`), QuickFIX/J v3.0.1 (`reference-engines/quickfix-j/quickfixj-core/src/main/java/quickfix/`), Fix8 v1.4.3 (`reference-engines/fix8/include/fix8/`, `reference-engines/fix8/runtime/`), per `[[feedback_always_invoke_speckit_clarify]]`.

---

## §1 Recovery sub-protocol — 2e-recovery upgrade discharge

### D-1 — Recovery FSM shape: AwaitingResend as transient flag, NOT new fsm_state value

**Decision**: When inbound `MsgSeqNum > next_expected_inbound`, the FSM stays at `fsm_state::Active` and sets a transient flag (`awaiting_resend: bool`) + populates a `ResendState` struct; on gap close, the flag clears. NO new value in the 6-state `fsm_state` enum.

**Rationale**: `[arch §5.6]` frozen-at-open rule + ABI stability rule from `[const §X.4]` — the 6-state enum's `LogonReceived = 2` slot was specifically *chosen* by 005 to be ABI-stable. Adding a 7th state at this point would force every consumer who switches on `fsm_state` to add a default branch or risk silent miss; the cost of widening the enum exceeds the benefit of structurally distinguishing the recovery sub-state.

**Alternatives considered**:
- (a) Add `fsm_state::AwaitingResend = 6`: rejected. Touches `session_fsm.hpp` ABI; 005's published enum is the binding contract for 010/011/012 already-merged consumers (per `[[project_010_session_cfg_lifetime_closed]]` 010 F-04 ring-buffer accessor consumes `fsm_state` directly).
- (b) Layered sub-state machine (recovery FSM nested inside Active): rejected. Conceptually clean but adds two enums + a sub-transition matrix; the per-session strand serialisation makes the flag-on-Active model race-free without the extra machinery.

**Reference-engine alignment** (all three engines model recovery as a flag, NOT a state):
- QuickFIX-cpp: `Session.cpp:1430-1455` `Session::nextResendRequest` is called from the inbound-dispatch path while the `Session` object is in steady-state; no `state_` enum value distinguishes recovery.
- QuickFIX/J: `Session.java:1862-1903` `nextResendRequest(...)` mutates the `SessionState` field directly (`state.setResendRange(beginSeqNo, endSeqNo)`); no enum widening.
- Fix8: `session.cpp:1147-1198` `session::handle_resend_request` operates on `_state` (the per-session `f8_atomic<States::SessionStates>` integer); no recovery-specific state.

**Anchor**: `[2e-recovery]` is not a Phase-2 design doc (the recovery upgrade was specified in 005 + 2e-msgstore + this feature incrementally); `[FIX-SL §4.3]` (Logon + Resend) is the protocol ground truth.

### D-2 — ResendRequest range semantics: `EndSeqNo=0` means "infinity"

**Decision**: System honours `ResendRequest{BeginSeqNo, EndSeqNo=0}` as `EndSeqNo = our_last_outbound` per FIX-SL §4.3.2. This is the standard "give me everything from BeginSeqNo onwards" convention.

**Rationale**: All three reference engines + FIX-SL §4.3.2 explicit normative text.

**Reference-engine alignment**:
- QuickFIX-cpp `Session.cpp:1473-1486` `Session::resend(...)` treats `EndSeqNo == 0` as "to the end of store".
- QuickFIX/J `Session.java:1990-2010` same semantic.
- Fix8 `session.cpp:1232-1247` same semantic.

**Anchor**: `[FIX-SL §4.3.2]` normative.

### D-3 — Admin span collapse to SequenceReset-GapFill: forbidden to replay admin bodies

**Decision**: When peer's ResendRequest range `[B..E]` contains spans where every message is admin (Heartbeat, TestRequest), the System collapses each such span to ONE `SequenceReset(4){GapFillFlag(123)=Y, NewSeqNo(36)=last_admin+1}` rather than replaying admin bodies. FIX-SL §4.3.5 explicitly forbids admin replay.

**Rationale**: Re-emitting admin bodies (e.g., a Heartbeat from 5 minutes ago) makes no semantic sense and may confuse the peer's liveness-tracking; the normative text addresses this exact failure mode.

**Reference-engine alignment**:
- QuickFIX-cpp `Session.cpp:1473-1525` `Session::resend` skips admin (the per-msgtype switch dispatches admin to `appendAdmin = false` branch that emits GapFill).
- QuickFIX/J `Session.java:1987-2075` `resendMessages` same.
- Fix8 `session.cpp:1232-1296` `session::do_resend` same.

**Anchor**: `[FIX-SL §4.3.5]` normative.

### D-4 — Store-horizon edge case: GapFill to first stored seqnum, then replay

**Decision**: If peer asks for `[B..E]` and our store only has `[M..E]` where `M > B`, we emit `SequenceReset(4){GapFillFlag=Y, NewSeqNo=M}` then replay `[M..E]`. Per FIX-SL §4.3.5.

**Rationale**: This is the standard handling for post-store-purge re-Logon scenarios. The peer's recovery loop converges; the peer's application logic (if any) for the gap-filled range is its concern.

**Reference-engine alignment**: All three engines implement this same shape; cites omitted for brevity (FIX-SL §4.3.5 is explicit normative text).

**Anchor**: `[FIX-SL §4.3.5]` normative.

### D-5 — PossDupFlag(43)=Y dedup: inbound replay deduplicated against MessageStore

**Decision**: System processes inbound application messages with `PossDupFlag(43)=Y` by checking `MessageStore` for a prior delivery of the same MsgSeqNum; if found, the duplicate delivery is SUPPRESSED (NOT re-dispatched to `Application::fromApp`); if not found, the message is RE-STORED (preserves the recovered timeline). Per FIX-SL §4.3.6.

**Rationale**: Without dedup, the peer's GapFill replay could double-fire app callbacks on the recovering side; with dedup, the operator sees idempotent recovery.

**Reference-engine alignment**:
- QuickFIX-cpp `Session.cpp:1620-1670` `Session::doPossDup` skips duplicate dispatch.
- QuickFIX/J `Session.java:2123-2158` `verifyMessage` with `checkTooHigh=false` for PossDup.
- Fix8 `session.cpp:1395-1422` `session::handle_application` with `is_dup` short-circuit.

**Anchor**: `[FIX-SL §4.3.6]` normative + 008-message-store FR-001 (the dedup query path is owned by 008's `MessageStore::retrieve`).

---

## §2 ResetSeqNumFlag(141)=Y handshake — Clarifications Q1=A

### D-6 — Per-session operator-configured policy: bilateral_strict default

**Decision**: `SessionConfig::reset_seqnum_policy` enum with three modes — `bilateral_strict` (default; QFJ-style; refuse Logon if peer's response lacks 141=Y), `bilateral_lenient` (QFC-mirror; auto-mirror 141=Y in our Logon response), `unilateral` (Fix8-style; honour any received 141=Y regardless of our outbound flag). Default = `bilateral_strict` per `[const §VI]` security-default-deny + 2-of-3 industry convergence (QFC + QFJ favour bilateral; Fix8 unilateral is the outlier).

**Rationale (Clarifications Q1)**: Reference-engine sweep showed industry-divergent semantics:
- QuickFIX-cpp `Session.cpp:203-207,684,705` mirrors peer's 141=Y (bilateral lenient).
- QuickFIX/J `Session.java:2282-2285` strictly enforces bilateral consent.
- Fix8 `session.cpp:479,581-585,946` unilaterally resets on any peer 141=Y.

Operator-config-per-session resolves the divergence without forcing a fixpp opinion on which engine is "right". Default-strict matches `[const §VI]` security-default. The two non-default modes are escape hatches for operators with QFC-style or Fix8-style counterparty pairings.

**Alternatives considered**:
- (a) Pick one industry mode (e.g., always bilateral_strict): rejected — locks out operators paired with QFC or Fix8 counterparties.
- (b) Engine-wide policy (not per-session): rejected — multi-tenant acceptors need per-session granularity (different counterparties may run different engines).

**Anchor**: `[FIX-SL §4.4.1]` Logon normative + Clarifications Q1=A.

### D-7 — bilateral_strict reject surfaces as session_seqnum_reset_mismatch

**Decision**: New `error::session_seqnum_reset_mismatch = 116` variant; the System emits Logout(5) with `SessionStatus(1409)=...` (slot allocation TBD at /speckit-implement-time — uses an existing `SessionStatus` enum value or adds one), then closes the Transport, and surfaces this variant via the `Session::open()` awaitable's return.

**Rationale**: The mismatch is a session-protocol violation (not a transport-layer or TLS-layer concern); the error envelope must distinguish "we sent 141=Y, peer didn't acknowledge" from generic Logon-failure shapes.

**Anchor**: FR-017 / US1 AC7 / Assumption A.8.

---

## §3 CompID↔TLS-identity binding — Clarifications Q2=A + Q3=A

### D-8 — Principal extraction canonical-fixed order: CN → SAN-DNS → SAN-URI → SHA-256-fingerprint

**Decision**: `CompIdAuthorizationPolicy` extracts the principal from `peer_identity` using the canonical-fixed order `CN → SAN-DNS → SAN-URI → SHA-256-fingerprint, first-non-empty wins`. The order is invariant in v1.0 (not operator-overridable per-binding); the chosen field name is surfaced in `SessionEvent::peer_identity_bound::principal_source` for operator audit.

**Rationale (Clarifications Q2)**: Reference-engine sweep found NO engine implements CompID↔cert binding at all:
- QuickFIX-cpp `SSLSocketAcceptor.cpp:311` validates the cert chain only; no CompID binding.
- QuickFIX/J `X509TrustManagerWrapper` standard trust manager only; no application-layer CompID linkage.
- Fix8 — same finding.

This is a fixpp greenfield surface (per FIXS §4.4 normative "authorization linked to authentication" — no industry-canonical implementation exists). Canonical-fixed order minimises operator configuration burden + matches conventional mTLS operator reasoning (CN/SAN are the inspect-first fields; fingerprint is the cryptographic fallback). Single deterministic rule.

**Alternatives considered**:
- (a) SAN-first (RFC 5280-style): rejected — RFC 5280 §4.2.1.6 deprecates CN for hostname binding, but in fixpp's mTLS context the CN field is overwhelmingly the operator-managed slot (operators set CN to map to a logical service identity).
- (b) Per-binding operator declaration of source field: rejected — adds operator-config complexity for marginal flexibility; defer to v1.x if demand emerges.
- (c) Fingerprint-only: rejected — operators lose the human-readable principal name in audit logs.

**Anchor**: `[FIXS §4.4]` normative + Clarifications Q2=A.

### D-9 — CompIdAuthorizationPolicy is allow-list only in v1.0 (default-deny)

**Decision**: `CompIdAuthorizationPolicy` is allow-list only in v1.0 — operator MUST enumerate every `{principal → {compid_set}}` binding; empty policy rejects ALL Logons (default-deny). NO deny-list mode; NO hybrid mode. Adding deny-list / hybrid modes in a later feature is BACKWARD-COMPATIBLE (one-way restriction); removing the allow-list semantic is NOT.

**Rationale (Clarifications Q3)**: Same reference-engine sweep as Q2 — no industry precedent. `[const §VI]` security-default-deny rules absent precedent. The allow-list shape matches 011's `Pinset` + `SecurityProfile::mtls_pinned` allow-list contract — same operator mental model, single audit surface. Misconfigured deploys fail CLOSED.

**Alternatives considered**:
- (a) Deny-list only (default-allow): rejected — `[const §VI]` security-default-deny rules.
- (b) Hybrid per-entry: rejected — adds operator-config complexity for marginal flexibility; defer.
- (c) Two-tier: rejected — same as (b).

**Anchor**: `[const §VI]` security-default + 011 FR-023 (Pinset allow-list precedent) + Clarifications Q3=A.

### D-10 — Binding event surfaces principal_source for operator audit

**Decision**: `SessionEvent::peer_identity_bound` carries a `principal_source: enum { CN, SAN_DNS, SAN_URI, SHA256_FINGERPRINT }` field so operators can audit which `peer_identity` field bound the session. This is BIG for incident response (e.g., "why did this session bind on fingerprint instead of CN?" → "the cert had no CN" → fixable upstream).

**Rationale**: Operator-readability + audit-trail completeness; cost is one enum byte per event.

**Anchor**: FR-022 / US2 AC5.

---

## §4 reload_credentials concurrency — Clarifications Q4=A

### D-11 — Atomic swap at transport_factory::make(...) entry; defer rotation until in-flight handshake completes

**Decision**: `Session::reload_credentials(new_cert_source)` (and `asio_listener::reload_credentials(new_cert_source)`) atomically swap the underlying `cert_source` slot on the held `TransportFactory`. The swap is `std::atomic<std::shared_ptr<cert_source>>::store(...)` — strand-free, race-free, O(1). The next `transport_factory::make(...)` call observes the NEW; any in-flight handshake (already past `make`) observes the OLD. NO mid-handshake `SSL_CTX` mutation (would invoke OpenSSL undefined behaviour per OpenSSL 3.x documentation on `SSL_CTX` lifetime).

**Rationale (Clarifications Q4)**: Reference-engine sweep found NO engine supports in-process cert rotation at all (QFC + QFJ initialize SSL_CTX once at startup; Fix8 same; all require full process restart). This is a fixpp greenfield surface — `[const §VI]` operator-burden reduction + SLA-visibility minimisation drives the design.

Atomicity at `transport_factory::make(...)` entry (not mid-handshake) is the safe-by-construction choice. Worst-case rotation latency = one handshake duration (~50–500 ms typical for TLS 1.3 1-RTT).

**Alternatives considered**:
- (a) Abort + restart in-flight handshake on rotation: rejected — leaks side-effects (peer sees connect→close→reconnect); operationally noisy.
- (b) Cooperative (operator-guaranteed quiescence): rejected — too easy to misconfigure; operator must reason about cross-thread races.
- (c) Two-phase stage + commit (operator calls reload_credentials twice — once to stage, once to commit): rejected — adds API complexity for an edge-case need; the atomic-swap shape covers 99 %.

**Anchor**: `[2j §3.12]` IN-PROCESS variant reservation + `[const §VI]` operator-burden + Clarifications Q4=A.

### D-12 — credentials_rotated event semantics

**Decision**: `SessionEvent::credentials_rotated{old_sha256, new_sha256}` emits BEFORE the first handshake on the rotated source (not at the `reload_credentials` call-site). This way the operator's event log shows "rotation took effect at handshake X" rather than "operator clicked rotate at time T".

**Rationale**: Causal-correlation friendliness for incident response; the operator can correlate the event with a specific re-Logon in the session log.

**Anchor**: FR-032 / US4 AC2.

---

## §5 Logout disconnect timeout — Clarifications Q5=A

### D-13 — Default 2000 ms; operator override per session

**Decision**: `SessionConfig::logout_disconnect_timeout_ms = 2000` default. Operator may override per session.

**Rationale (Clarifications Q5)**: Reference-engine sweep showed 2-of-3 effectively-immediate (QFC `Session.cpp:617` immediate close; Fix8 no logout-specific wait) vs QFJ's 2 s (`SessionState.java:51` `logoutTimeoutMs=2000L`). Industry diverges; QFJ's middle ground captures typical RTT clean-shutdown windows without stalling operator-driven Logouts. Operator-overridable for low-latency / high-latency outlier deployments.

**Alternatives considered**:
- (a) 0 ms immediate (QFC + Fix8): rejected — drops in-flight Logout-replies; operator audit incomplete.
- (b) 5000 ms (the prior §A.7 default before Clarifications): rejected — too conservative; stalls operator-driven shutdowns; no engine precedent.
- (c) Operator-required-no-default: rejected — adds boilerplate to every SessionConfig.

**Anchor**: `[FIX-SL §4.6]` Logout normative + QuickFIX/J `SessionState.java:51` evidence + Clarifications Q5=A.

---

## §6 Consumed surfaces — already-shipped binding contracts (LOCKED)

### D-14 — Transport / TransportFactory contract from 012 (LOCKED)

**Consumed**: `[2h §4.7]:907-910` — `TransportFactory::make(asio::any_io_executor exec, fixpp::tls::SslCtxConfig ssl_cfg, std::pmr::memory_resource* mr) noexcept -> expected_t<std::unique_ptr<Transport>>`. The cached-SSL-CTX FR-026 contract is BINDING for `reload_credentials` invariant — the cache invalidation point IS the atomic swap at `make(...)` entry (D-11).

**Half-restructure obligation** per `[[feedback_half_restructure_symmetric_api]]` + 012 RC#B precedent: the FR-026 cache covers BOTH initiator AND accept paths (012 Codex r3 caught the half-restructure); 013's `reload_credentials` must verify the invariant holds for BOTH halves (initiator-side rotation via `Session::reload_credentials`; acceptor-side rotation via `asio_listener::reload_credentials`).

**Anchor**: `[2h §4.7]` + `[[project_012_2h_transport_closed]]` RC#B saga + `[[feedback_half_restructure_symmetric_api]]`.

### D-15 — TLS verify_peer + peer_identity from 011 (LOCKED)

**Consumed**: `[2g §6.6]:986-1004` — 15 `tls_verify_error` variants; `verify_peer(...) -> expected_t<peer_identity, tls_verify_error>`; `peer_identity` carries `cn`, `sans`, `sha256_fingerprint`, `cipher_suite`.

**FR-026 binding**: 013 surfaces EACH of the 15 variants as a distinct `SessionEvent::tls_validation_failed{variant=...}` — NOT coalesced to a generic "tls error". Per `[[feedback_trap_throw_pmr_witness_enumerate_sites]]`, 013's test plan enumerates all 15 (not just first) to avoid the false-pass axis 011 round-2 closed.

**Anchor**: `[2g §6.6]` + 011 `/clarify` Q2 operator_config_error vs cert_expired distinction (FR-027).

### D-16 — SessionEvent ring-buffer accessor from 010 F-04 (LOCKED)

**Consumed**: 010 F-04 closure added the ring-buffer accessor for SessionEvent observability (per `[[project_010_session_cfg_lifetime_closed]]`). 013 EXTENDS the variant set with 5 new variants on the same ring; NO new event channel, NO breaking change to existing variants. Verified at /speckit-implement-time that the variant union from 010 is `std::variant<...>` or `boost::variant<...>` (or equivalent extensible shape); add new variants append-only.

**Anchor**: 010 F-04 + Assumption A.3 + FR-035.

### D-17 — MessageStore::retrieve + PossDupFlag from 008 (LOCKED)

**Consumed**: 008 `MessageStore::retrieve(BeginSeqNo, EndSeqNo) -> asio::generator<owning_message_t<>>` (or equivalent). 013's outbound replay path consumes this; the durable-before-transmit invariant per `[2e §6.1.4]` is UNCHANGED (we don't replay messages that weren't durably stored).

**Anchor**: 008 + `[2e §6.1.4]`.

---

## §7 Deferred / out-of-scope decisions

- **`SessionStatus(1409)` enum extension**: spec.md US1 AC6 mentions "SessionStatus(1409)=session_seqnum_too_low (slot 69, unchanged from 005)" and US2 AC2 mentions "SessionStatus(1409)=session_compid_unauthorized". At /speckit-tasks-time: cross-check the existing `SessionStatus` enum in `include/fixpp/session/admin_messages.hpp` for `session_compid_unauthorized` slot — if absent, allocate at next free `SessionStatus` slot (this is a FIX-wire-level enum, separate from `error::session_*` C++ enum; the FIX wire value is what the peer sees in the rejection Logout). NOT a Gate-A concern (no design impact); /tasks-time decision per Assumption A.8.
- **gRPC `reload_credentials` trigger**: explicitly deferred to v1.x per `[2j §3.12]` RC#5 + Article XVIII roadmap. 013 ships ONLY the in-process variant.
- **Deny-list / hybrid CompIdAuthorizationPolicy modes**: deferred per D-9. Adding later is backward-compatible.
- **Per-binding-entry principal-source override**: deferred per D-8. Adding later is backward-compatible.
- **Interop matrix against QuickFIX-cpp / QuickFIX/J / Fix8**: deferred to the 014-interop-harness feature per `[[project_release_interop_quickfix_fix8]]`. 013 unblocks it (the FSM-reconnect + CompID-binding surface is what 014 needs); 014 is a separate Spec-Kit cycle.
- **2e-recovery upgrade markers + 44 fixpp gap tags**: the actual count may be ±N; cross-check at /speckit-implement-time per FR-016. NOT a Gate-A concern (mechanical removal, no design impact).

---

## §8 Anti-pattern library candidates this feature MUST guard against

Per `[[feedback_simplify_pass_catches_9th_burn]]` — /simplify catches binding-contract drifts after completeness PASS. Surfaces in 013 most at-risk for this class:

1. **FR-024 binding-policy symmetry** — initiator path AND acceptor path must consume the SAME `CompIdAuthorizationPolicy`; 012 RC#B's saga (FR-026 covered only initiator half until Codex r3) is the precedent. Mitigation: `test_compid_binding_symmetry.cpp` exercises both halves; invariant-counting witness on `CompIdAuthorizationPolicy::authorize` call site.
2. **FR-026 enumerate-all-15-variants** — `[[feedback_trap_throw_pmr_witness_enumerate_sites]]`: test plan must drive EACH of the 15 `tls_verify_error` variants (not just first). Mitigation: `test_tls_validation_failed_all_variants.cpp` has 15 cells.
3. **FR-033 reload_credentials in-flight handshake defer** — atomicity at `make(...)` entry is the binding contract; if a future fixer commits modify `make` to evaluate `cert_source` lazily inside the handshake coroutine, the invariant breaks silently. Mitigation: `test_reload_credentials_in_flight.cpp` cell drives the race deterministically via `mock_clock` + `mock_transport` scripted handshake delay.
4. **FR-022 principal-extraction canonical-fixed** — if a fixer "improves" the principal extraction to SAN-first or operator-overridable, the v1.0 lock-in breaks. Mitigation: `test_compid_binding_principal_extraction.cpp` pins the order with 4 explicit cells.
5. **FR-009 005-FR-008-amendment-in-place** — the spec amendment to 005 must propagate to 005's plan / data-model / tests; if missed, a future audit might re-trigger the "fatal-disconnect" interpretation. Mitigation: /speckit-tasks-time task row to amend 005 in-place + cross-check at completeness-audit per `[[feedback_simplify_pass_catches_9th_burn]]`.

---

## §9 Open questions surfaced by this research

None at Gate-A start. Re-evaluate at Gate-A Round 1 review.
