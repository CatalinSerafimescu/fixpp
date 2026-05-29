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

**Decision**: `SessionConfig::reset_seqnum_policy` enum with three modes — `bilateral_strict` (default; QFJ-style; refuse Logon if peer's response lacks 141=Y), `bilateral_lenient` (QFC-mirror; auto-mirror 141=Y in our Logon response), `unilateral` (Fix8-style; honour any received 141=Y regardless of our outbound flag). Default = `bilateral_strict` per `[const §XII.5]` no-implicit-default (Article XII §5 mandates an explicit `SecurityProfile` choice — same explicit-opt-in design pattern; the constitution has no stand-alone "security-default-deny" article, §XII.5 is the nearest anchor) + 2-of-3 industry convergence (QFC + QFJ favour bilateral; Fix8 unilateral is the outlier). *(Anchor corrected from `[const §VI]` 2026-05-28 per `/speckit-checklist-audit` Pass 2 binding.md CHK043.)*

**Rationale (Clarifications Q1)**: Reference-engine sweep showed industry-divergent semantics:
- QuickFIX-cpp `Session.cpp:203-207,684,705` mirrors peer's 141=Y (bilateral lenient).
- QuickFIX/J `Session.java:2282-2285` strictly enforces bilateral consent.
- Fix8 `session.cpp:479,581-585,946` unilaterally resets on any peer 141=Y.

Operator-config-per-session resolves the divergence without forcing a fixpp opinion on which engine is "right". Default-strict matches `[const §XII.5]` no-implicit-default. The two non-default modes are escape hatches for operators with QFC-style or Fix8-style counterparty pairings.

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

**Backward-compatibility note**: Adding per-binding-entry override modes (operator-declared source field per entry) in a later feature is BACKWARD-COMPATIBLE — the canonical-fixed order is the v1.0 default that future overrides extend, not replace. Existing operator configs that rely on the default order remain valid; new configs may opt-in to per-binding overrides without breaking existing bindings.

**Anchor**: `[FIXS §4.4]` normative + Clarifications Q2=A.

### D-9 — CompIdAuthorizationPolicy is allow-list only in v1.0 (default-deny)

**Decision**: `CompIdAuthorizationPolicy` is allow-list only in v1.0 — operator MUST enumerate every `{principal → {compid_set}}` binding; empty policy rejects ALL Logons (default-deny). NO deny-list mode; NO hybrid mode. Adding deny-list / hybrid modes in a later feature is BACKWARD-COMPATIBLE (one-way restriction); removing the allow-list semantic is NOT.

**Rationale (Clarifications Q3)**: Same reference-engine sweep as Q2 — no industry precedent. `[const §XII.5]` no-implicit-default (Article XII §5 mandates an explicit `SecurityProfile` choice) is the nearest constitutional anchor for explicit-deny-unless-configured — applied here. The allow-list shape matches 011's `Pinset` + `SecurityProfile::mtls_pinned` allow-list contract — same operator mental model, single audit surface. Misconfigured deploys fail CLOSED.

**Alternatives considered**:
- (a) Deny-list only (default-allow): rejected — `[const §XII.5]` no-implicit-default rules out an opt-out-only safety model for a security surface.
- (b) Hybrid per-entry: rejected — adds operator-config complexity for marginal flexibility; defer.
- (c) Two-tier: rejected — same as (b).

**Anchor**: `[const §XII.5]` no-implicit-default + 011 FR-023 (Pinset allow-list precedent) + Clarifications Q3=A. *(Anchor corrected from `[const §VI]` 2026-05-28 per `/speckit-checklist-audit` Pass 2 binding.md CHK043.)*

### D-10 — Binding event surfaces principal_source for operator audit

**Decision**: `SessionEvent::peer_identity_bound` carries a `principal_source: enum { CN, SAN_DNS, SAN_URI, SHA256_FINGERPRINT }` field so operators can audit which `peer_identity` field bound the session. This is BIG for incident response (e.g., "why did this session bind on fingerprint instead of CN?" → "the cert had no CN" → fixable upstream).

**Rationale**: Operator-readability + audit-trail completeness; cost is one enum byte per event.

**Anchor**: FR-022 / US2 AC5.

---

## §4 reload_credentials concurrency — Clarifications Q4=A

### D-11 — Atomic swap INSIDE the factory; defer rotation effects until in-flight handshake completes

**Decision**: `Session::reload_credentials(new_cert_source)` is the operator-facing forwarder; it delegates to `TransportFactory::reload_credentials(new_cert_source)` which is the binding atomic-swap entry. The factory owns `std::atomic<std::shared_ptr<cert_source>> cert_source_slot_`; `reload_credentials` performs `cert_source_slot_.store(new_cert_source)` — strand-free, race-free, O(1). Both initiator-side rotation AND acceptor-side rotation route through the SAME factory call per `[[feedback_half_restructure_symmetric_api]]` — the factory IS the symmetric authority (no per-side reload entry). `TransportFactory` ownership stays BEHAVIOURALLY per-Session per 2h Appendix D §D.1+§D.2 sign-off (`include/fixpp/transport/transport_factory.hpp:156-164`) — see D-14 for the 013-specific storage-type reconciliation (the storage type is `shared_ptr<TransportFactory>` for SessionConfig-copy semantics per 010 FR-001a precedent, BUT the 2h "no factory shared across Sessions" invariant is preserved via a `/speckit-implement`-time hygiene assertion at `Session::open`); the atomic `cert_source_slot_` lives INSIDE the factory, NOT around it.

**`make(...)` consumption path**: `TransportFactory::make(exec, ssl_cfg, mr) noexcept -> expected_t<unique_ptr<Transport>>` (per `[2h §4.7]:961-964`) takes `SslCtxConfig` BY VALUE — built by the FSM caller (`ReconnectFsm::drive_reconnect_attempt`) from a per-attempt atomic snapshot of `cert_source_slot_`. The FSM-side sequence is:
1. `auto snap = factory_->cert_source_slot_load();` — atomic snapshot of the current `shared_ptr<cert_source>` (factory exposes a typed accessor; caller now holds a strong-ref keeping the cert_source alive past the swap).
2. `auto ssl_cfg = fixpp::tls::make_ssl_ctx_config(profile, snap, clock, pinset, mr);` — per `[2g §4.5]`.
3. `auto tr = factory_->make(exec, std::move(ssl_cfg), mr);` — minted Transport carries the snapshot's leaf cert + key.

If a `reload_credentials` lands BETWEEN steps 1 and 3 (or during the handshake that follows), the in-flight handshake completes against the OLD `cert_source` (held strong-ref via `snap`); the NEXT `drive_reconnect_attempt` reads the NEW snapshot. NO mid-handshake `SSL_CTX` mutation (would invoke OpenSSL undefined behaviour per OpenSSL 3.x documentation on `SSL_CTX` lifetime).

**Captured-by-value-copy invariant** (closes the `[[feedback_weak_ptr_cache_needs_owning_context]]` axis): the FSM's `snap` is a `std::shared_ptr<cert_source>` (strong-ref) — never a raw `cert_source*` and never a `weak_ptr`. The strong-ref keeps the OLD cert_source alive past the factory's `cert_source_slot_.store(new)`; the OLD cert_source is destructed when (a) the in-flight handshake completes AND (b) every captured snapshot's shared_ptr count drops to 0. This is the SAFE-by-construction lifetime story; document it as a binding invariant in §8 anti-pattern guards.

**Rationale (Clarifications Q4)**: Reference-engine sweep found NO engine supports in-process cert rotation at all (QFC + QFJ initialize SSL_CTX once at startup; Fix8 same; all require full process restart). This is a fixpp greenfield surface — operator-burden reduction + SLA-visibility minimisation drives the design (no constitution Article maps directly to "operator-burden reduction" as a normative principle; rationale stands on its own merits). *(`[const §VI]` cite removed 2026-05-28 per `/speckit-checklist-audit` Pass 2 — Article VI is "Spec Coverage Discipline", unrelated to operator UX.)*

Atomicity inside the factory (with per-attempt FSM-side snapshot reads from the atomic slot) is the safe-by-construction choice. Worst-case rotation latency = one in-flight HANDSHAKE duration (~50–500 ms typical for TLS 1.3 1-RTT) — NOT an in-flight `make()` window; `make()` itself is one-shot per call.

**Alternatives considered**:
- (a) Abort + restart in-flight handshake on rotation: rejected — leaks side-effects (peer sees connect→close→reconnect); operationally noisy.
- (b) Cooperative (operator-guaranteed quiescence): rejected — too easy to misconfigure; operator must reason about cross-thread races.
- (c) Two-phase stage + commit (operator calls reload_credentials twice — once to stage, once to commit): rejected — adds API complexity for an edge-case need; the atomic-swap shape covers 99 %.

**Anchor**: `[2j §3.12]` IN-PROCESS variant reservation + Clarifications Q4=A. (Operator-burden rationale is non-constitutionally-anchored; see Rationale paragraph above.)

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

**Consumed**: `[2h §4.7]:961-964` — `TransportFactory::make(asio::any_io_executor exec, fixpp::tls::SslCtxConfig ssl_cfg, std::pmr::memory_resource* mr) noexcept -> expected_t<std::unique_ptr<Transport>>` (the design-doc signature; the shipped header lives at `include/fixpp/transport/transport_factory.hpp:72-75`). The cached-SSL-CTX FR-026 contract is BINDING for `reload_credentials` invariant — the swap point is `TransportFactory::reload_credentials(new_source)` (013-introduced pure-virtual; pushes the factory's pure-virtual count 1→2, still under 5/5 cap), an atomic store on the factory-internal `cert_source_slot_: std::atomic<std::shared_ptr<cert_source>>`. The subsequent `make(...)` call's `SslCtxConfig` is built by the FSM caller from a per-attempt atomic snapshot read (see D-11).

**`SessionConfig::transport_factory_override` field** (consumed from 2h Appendix D §D.2 reservation per `transport_factory.hpp:156-164`): 2h declared as `std::unique_ptr<fixpp::transport::TransportFactory>`; 013 stores as `std::shared_ptr<fixpp::transport::TransportFactory>` per the 010 FR-001a precedent (SessionConfig copy-constructibility — see data-model.md E-4 "Ownership reconciliation"). Default `nullptr`. Engine substitutes the default factory at `Session::open`-time when `nullptr`. Resolution rule: `resolved_factory = SessionConfig::transport_factory_override.value_or(EngineConfig::default_transport_factory)`. 2h reserved this field for "the post-012 session-Phase-4 spec" — i.e., this feature; the wiring lands in 013's `SessionConfig` delta (4th new field per E-4).

**Factory ownership BEHAVIOUR stays "per-Session, no cross-Session sharing"** per 2h Appendix D §D.1+§D.2 sign-off. The storage type is `shared_ptr<TransportFactory>` (013-specific) — see data-model.md E-4 "Ownership reconciliation" for the rationale. The 2h binding constraint is preserved via `/speckit-implement`-time hygiene assertion at `Session::open`. The atomic-swap slot for `cert_source` lives INSIDE the factory; `ReconnectFsm::factory_` is a non-owning `TransportFactory*` (the SessionConfig owns the shared_ptr; the engine guarantees the factory outlives the FSM per `[arch §5.6]` frozen-at-open rule).

**Half-restructure obligation closed by construction** per `[[feedback_half_restructure_symmetric_api]]` + 012 RC#B precedent: by routing BOTH initiator-side rotation AND acceptor-side rotation through the SAME `TransportFactory::reload_credentials(...)` call, the factory IS the symmetric authority. There is no per-side reload entry to keep in sync — eliminating the half-restructure trap by construction (the 012 RC#B saga where FR-026 caching covered only the initiator half cost 2 extra Gate B rounds; 013's design closes this class by making the factory the single authority).

**Anchor**: `[2h §4.7]:961-964` + `include/fixpp/transport/transport_factory.hpp:72-75,156-164` + `[[project_012_2h_transport_closed]]` RC#B saga + `[[feedback_half_restructure_symmetric_api]]`.

### D-15 — TLS verify_peer + peer_identity from 011 (LOCKED)

**Consumed**: Shipped `verify_peer(SslCtxConfig const& cfg, std::span<const Certificate> peer_chain) noexcept -> core::expected_t<peer_identity>` (`include/fixpp/tls/security_profile.hpp:121-122`) — **single-arg `expected_t`** over the master `core::error` enum (`expected_t<T> = std::expected<T, error>` per `include/fixpp/core/error.hpp:588`). NOT a two-arg `expected_t<peer_identity, tls_verify_error>` (that form is not a valid alias signature — `tls_verify_error` is not a type at all; earlier drafts of this bundle invented it).

**Master-enum surface — 6 distinct `error::tls_*` variants** per shipped `include/fixpp/core/error.hpp:403-429`:
- `tls_handshake_failed = 88` — GROUPING variant per `[2g §6.6]`; collapses 10+ sub-reasons.
- `tls_rsa_key_too_large = 89` — DoS bound.
- `tls_cert_der_too_large = 90` — DoS bound.
- `tls_san_entries_exceeded = 91` — DoS bound.
- `tls_pin_mismatch = 92` — peer cert SHA-256 not in Pinset.
- `tls_load_cancelled = 93` — cancellation.

**Sub-reason surface — thread-local `last_handshake_sub_reason() noexcept -> std::string_view`** (`security_profile.hpp:124-144`) per 011 v0.1 T037 brief — "enum-only return + thread-local sub-reason carrier". When `verify_peer` returns `tls_handshake_failed`, the thread-local carries the specific sub-reason as a static-storage string literal: `"rsa_under_min"`, `"ecdsa_curve"`, `"sigalg_disallowed"`, `"chain_too_deep"`, `"x509_v1"`, `"expired"`, `"not_yet_valid"`, `"empty_chain"`, etc. The `[2g §6.6]` 15-variant table is the DESIGN-DOC ENUMERATION of these sub-reasons — they surface via the thread-local string, NOT as distinct master-enum variants.

**`peer_identity`** carries `cn`, `sans`, `sha256_fingerprint`, `cipher_suite` — UNCHANGED.

**FR-026 binding (corrected)**: 013 surfaces the actual master-enum `error::code` + the `last_handshake_sub_reason()` diagnostic string as a single `SessionEvent::tls_validation_failed{code, sub_reason, peer_endpoint, reason_string}`. The test plan covers all 6 master-enum cells + 3 representative sub_reason cells (per `[[feedback_trap_throw_pmr_witness_enumerate_sites]]` — enumerate REPRESENTATIVE sites of the action, not coalesce). The 15-variant design-doc enumeration drives test case selection for the `sub_reason` axis but is NOT the enum cardinality on the wire.

**Anchor**: `include/fixpp/tls/security_profile.hpp:121-144` + `include/fixpp/core/error.hpp:403-429` + `[2g §6.6]` (design-doc sub-reason enumeration) + 011 `/clarify` Q2 operator_config_error vs cert_expired distinction (FR-027, discriminated via `sub_reason`).

### D-16 — SessionEvent is a NEW 013-introduced surface; 010 F-04 shipped a DIFFERENT accessor

**Ground-truth**: 010 F-04 shipped `Session::fsm_visit_history() const noexcept -> std::span<const fsm_state>` (`include/fixpp/session/session.hpp:237-250`) — a fixed 16-entry `std::array<fsm_state, 16>` ring of `fsm_state` enum values (FSM-state observation; the ring records the set of transitions over the most recent ≤16 `record_state_transition_()` calls; physical-buffer order, NOT chronologically ordered; membership-witness only). It is **NOT a variant union of session events**; it is a fixed array of enum values.

**No shipped `SessionEvent` type**: `grep -rn 'SessionEvent\\b' include/ src/` returns zero hits. The bundle's earlier framing — "extend the existing 010 F-04 SessionEvent variant union with 5 new alternatives" — was based on a misreading of 010's shipped surface.

**Decision**: 013 introduces `SessionEvent` as a NEW public variant union in `include/fixpp/session/session_event.hpp` with 5 initial alternatives (`peer_identity_bound`, `compid_authorization_failed`, `tls_validation_failed`, `credentials_rotated`, `sequence_numbers_reset`). 013 also introduces a NEW ring-buffer accessor `Session::recent_events() const noexcept -> std::span<const SessionEvent>` (013-defined; distinct from `fsm_visit_history()`). The two accessors are complementary: `fsm_visit_history()` observes FSM-state-transition values (UNCHANGED); `recent_events()` observes the new variant-event stream (013's contribution).

**Future evolution**: post-013 features may append additional `SessionEvent` variants append-only; this is the standard `std::variant<...>`-extension pattern. No breaking change to 013's 5 initial alternatives.

**Anchor**: `include/fixpp/session/session.hpp:237-250` (shipped `fsm_visit_history` accessor) + Assumption A.3 + FR-035; NOT extending 010's surface.

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

1. **FR-024 binding-policy symmetry** — initiator path AND acceptor path must consume the SAME `CompIdAuthorizationPolicy`; 012 RC#B's saga (FR-026 covered only initiator half until Codex r3) is the precedent. Mitigation: `test_compid_binding_symmetric.cpp` exercises both halves; invariant-counting witness on `CompIdAuthorizationPolicy::authorize` call site.
2. **FR-026 6-master-enum-cells + 3 representative sub_reason cells** — `[[feedback_trap_throw_pmr_witness_enumerate_sites]]`: test plan must drive ALL 6 master-enum cells (`tls_handshake_failed` GROUPING + `tls_rsa_key_too_large` + `tls_cert_der_too_large` + `tls_san_entries_exceeded` + `tls_pin_mismatch` + `tls_load_cancelled` per shipped `error.hpp:403-429`) AND at least 3 representative sub_reason cells (covering peer-cert / operator-config / cipher-policy axes — e.g., `"expired"`, `"tls_pin_empty_at_open"`, `"sigalg_disallowed"`). Mitigation: `test_tls_validation_failed_taxonomy.cpp` has 6+3 cells.
3. **FR-033 reload_credentials in-flight handshake defer** — atomicity inside the factory (`cert_source_slot_: std::atomic<std::shared_ptr<cert_source>>`) is the binding contract; the FSM-side `drive_reconnect_attempt` reads the atomic snapshot before building `SslCtxConfig` for `make(...)`. If a future fixer modifies the FSM to capture a raw `cert_source*` from the snapshot or to read the slot lazily inside the handshake coroutine, the invariant breaks silently. Mitigation: `test_reload_credentials_in_flight.cpp` cell drives the race deterministically via `mock_clock` + `mock_transport` scripted handshake delay.
6. **FR-033 captured-by-value-copy strong-ref lifetime** — `[[feedback_weak_ptr_cache_needs_owning_context]]`: the FSM's per-attempt cert_source snapshot MUST be captured as `std::shared_ptr<cert_source>` (strong-ref) — NEVER a raw `cert_source*` and NEVER a `weak_ptr`. The strong-ref keeps the OLD cert_source alive past the factory's `cert_source_slot_.store(new)`; the OLD cert_source is destructed only when (a) the in-flight handshake completes AND (b) every captured snapshot's shared_ptr count drops to 0. If a future fixer "optimises" the snapshot to a raw pointer (or worse, a weak_ptr-keyed cache without a strong-ref-keeping owner), an in-flight handshake on the OLD source reads dead memory after the operator's rotate call returns. Mitigation: invariant-counting witness on `cert_source::~cert_source()` destructor + concurrent-rotation race test in `test_reload_credentials_in_flight.cpp`.
4. **FR-022 principal-extraction canonical-fixed** — if a fixer "improves" the principal extraction to SAN-first or operator-overridable, the v1.0 lock-in breaks. Mitigation: `test_compid_binding_principal_extraction.cpp` pins the order with 4 explicit cells.
5. **FR-009 005-FR-008-amendment-in-place** — the spec amendment to 005 must propagate to 005's plan / data-model / tests; if missed, a future audit might re-trigger the "fatal-disconnect" interpretation. Mitigation: /speckit-tasks-time task row to amend 005 in-place + cross-check at completeness-audit per `[[feedback_simplify_pass_catches_9th_burn]]`.

---

## §9 Open questions surfaced by this research

None at Gate-A start. Re-evaluate at Gate-A Round 1 review.
