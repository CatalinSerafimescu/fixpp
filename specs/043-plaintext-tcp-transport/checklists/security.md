# Security & Fail-Closed Requirements Quality Checklist: Plaintext TCP transport (insecure_plain_tcp)

**Purpose**: Validate the QUALITY of the requirements for a non-TLS transport added to a TLS-default FIX
engine — completeness, clarity, consistency, measurability, and coverage of the security-surface,
fail-closed, and scope-discipline aspects. This is a requirements-quality gate (a Gate-B precondition per
pipeline step 9), NOT an implementation test.
**Created**: 2026-06-17
**Feature**: [spec.md](../spec.md) · design [research.md](../research.md) D-1..D-13 ·
[data-model.md](../data-model.md) E-1..E-7
**Depth**: formal release gate · **Audience**: Gate-B reviewer / checklist-auditor

> How to read: each item asks whether the *requirement is well-written*, not whether the code works.
> `[Spec §X]` = checks an existing requirement; `[Gap]` = checks for a possibly-missing one;
> `[Ambiguity]`/`[Conflict]`/`[Assumption]` = flags wording that may not survive implementation.

## Security-Surface — Opt-In & No-Implicit-Default

- [x] CHK001 Is the requirement that `insecure_plain_tcp` is opt-in-only and never an implicit default stated unambiguously (not merely implied by the `unset`-reject)? [Clarity, Spec §FR-007] — PASS: spec.md FR-007 explicitly states "insecure_plain_tcp is never selected implicitly" and FR-001 qualifies it as NOT the `unset` sentinel; unambiguous per-profile statements, not implied by reject-only.
- [x] CHK002 Is the `unset`-sentinel-still-rejected behaviour specified as *unchanged from today* with an explicit error outcome, rather than left to inference? [Completeness, Spec §FR-007/§SC-002] — PASS: FR-007 "the `unset` sentinel is still rejected at `Session::open()`"; SC-002 pins the error as `error::invalid_session_config`; explicit and complete.
- [x] CHK003 Is "never selected implicitly or silently" given a verifiable acceptance form (a witness exists, not just prose)? [Measurability, Spec §US2-AC3/§SC-002] — PASS: US2 AC3 states "no path selects `insecure_plain_tcp` implicitly"; SC-002 specifies the same; T018 in tasks.md provides the explicit witness task.
- [x] CHK004 Are the requirements consistent that adding `insecure_plain_tcp` leaves all existing TLS profiles and their behaviour entirely unaffected (purely additive)? [Consistency, Spec §FR-007/§SC-006] — PASS: FR-007 "Existing TLS profiles and their behaviour MUST be entirely unaffected"; SC-006 "zero regression in the TLS transport, factory, and session FSM"; consistent across both.
- [x] CHK005 Is the constitution authority for reopening the closed `SecurityProfile` set cited precisely (the v0.3 §XII.5 amendment), not vaguely? [Traceability, Spec §FR-001/Normative Refs] — PASS: spec.md FR-001 cites "v0.3 constitution amendment (§XII.5)"; Normative Refs has `[const §XII.5] (amended v0.3, 2026-06-17)`; constitution.md Article XII §5 verified to resolve at lines 183-189 in the signed-off v0.3 revision.

## Fail-Closed — Profile↔Factory Consistency

- [x] CHK006 Does the spec define the consistency check on the **effective/resolved** factory (not only an explicit override), so a wrong engine-default is caught? [Completeness, Spec §FR-008/§SC-003] — PASS: FR-008 "MUST resolve the **effective** transport factory" and "MUST catch **both** an explicitly-supplied mismatched override **and** a wrong *engine-default* factory"; SC-003 AC3 explicitly names the TLS-profile + no-override + plaintext-engine-default case.
- [x] CHK007 Is "effective factory" defined unambiguously (the exact resolution order: plaintext+no-override ⇒ built-in plaintext; else override.value_or(engine_default))? [Clarity, Spec §FR-003a/§FR-008] — PASS: FR-003a states "plaintext + no override ⇒ built-in plaintext factory; otherwise the explicit `transport_factory_override` if present, else the engine-default"; FR-008 repeats the identical resolution order. Unambiguous.
- [x] CHK008 Are ALL mismatch directions enumerated as requirements — plaintext-profile+TLS-override, TLS-profile+plaintext-override, AND TLS-profile+no-override+plaintext-engine-default? [Coverage, Spec §FR-008/§US3] — PASS: FR-008 enumerates "a TLS profile...requires kind `tls`; `insecure_plain_tcp` requires kind `plaintext`" covering both override directions; US3 AC1 (plaintext+TLS override), AC2 (TLS+plaintext override), AC3 (TLS+no-override+plaintext-engine-default) enumerate all three.
- [x] CHK009 Is the *timing* of the reject specified as "at `open()`, before any connect/handshake attempt" (not merely "rejected")? [Clarity, Spec §FR-008/§US3] — PASS: FR-008 "MUST reject the session...before any connect/handshake attempt"; US3 intro "rejected at `Session::open()`, **before** any connect/handshake attempt".
- [x] CHK010 Is the auto-correct case (plaintext profile + no override + default TLS factory ⇒ corrected, NOT rejected) specified to avoid conflict with the reject rule? [Consistency, Spec §FR-003a/§FR-008] — PASS: FR-003a "a plaintext profile with NO override and the default TLS factory is auto-corrected to the built-in plaintext factory, not rejected"; FR-008 echoes "a plaintext profile with no override is auto-corrected to the built-in plaintext factory, not rejected". Consistent; no conflict.
- [x] CHK011 Is the error outcome for a mismatch pinned to a specific slot (`invalid_session_config`, 53) rather than an unspecified failure? [Measurability, Spec §FR-008/§D-11] — PASS: FR-008 "MUST reject the session with `error::invalid_session_config`"; research.md D-11 confirms slot 53; specific and measurable.
- [x] CHK012 Does a requirement state the resolved/checked factory is the SAME object subsequently used to mint (wired into the FSM before reconnect), preventing a late nullptr/stale-pointer escape? [Completeness, Spec §FR-003a] — PASS: FR-003a "The resolved effective factory MUST be the *same* object used for BOTH the FR-008 consistency check AND reconnect minting (it MUST be wired into the reconnect FSM at `open()`, before any connect attempt)"; E-6/D-6 confirm the same-object wire-before-reconnect contract.

## Fail-Closed — Peer Identity & Authorization

- [x] CHK013 Is the `live_peer_id_ == nullopt` requirement stated as a MUST on BOTH the reconnected and accepted handoffs (not a SHOULD, not one site only)? [Clarity, Spec §SC-004/§D-10] — PASS: spec.md Clarifications (Gate A r1) states "`live_peer_id_` is a **MUST** everywhere — both `install_reconnected_transport` and `attach_accepted_transport` MUST leave `live_peer_id_ == nullopt`"; D-10 §2 and §3 each state MUST for the respective site.
- [x] CHK014 Does the spec require that NO fabricated/empty `handshake_result` is constructed on the plaintext path (so a present-but-empty identity can never land in `live_peer_id_`)? [Completeness, Spec §D-10] — PASS: D-10 §2 "The implementation **MUST NOT** construct or pass a fake `handshake_result` whose empty `peer_id` would land in `live_peer_id_` as a *present-but-empty* `peer_identity`"; E-7 confirms the acceptor twin requirement.
- [x] CHK015 Are the requirements consistent that the mTLS-gated `compid_authorization_policy` is skipped while the cert-independent `check_comp_id` remains in effect — two distinct checks, not conflated? [Consistency, Spec §FR-008a/§Clarifications] — PASS: FR-008a explicitly distinguishes the two: `compid_authorization_policy` (CompID↔TLS-cert-identity binding, MUST be skipped) vs `check_comp_id` inbound 49/56 match (MUST remain in effect); Clarifications section makes the same distinction explicitly.
- [x] CHK016 Is the "no peer authentication on this profile" consequence documented as an explicit limitation (L-043-x), not left implicit? [Completeness, Spec §FR-008a/§Edge Cases] — PASS: FR-008a concludes "The plaintext profile therefore provides **no peer authentication** (documented limitation L-043-x)"; Edge Cases section references L-043-x explicitly; T025 tasks the B&L entry.
- [x] CHK017 Are ALL `handshake_result`-dependent consumers enumerated (peer_id, captured_pinset, negotiated_cipher) with a stated inert/skip requirement for each, rather than only peer_id? [Coverage, Spec §Edge Cases/§D-10] — PASS: spec.md Edge Cases section lists "handshake_result-dependent reads (peer identity, negotiated cipher, captured pinset): ...MUST be inert/skipped"; D-10 "Other two `handshake_result` fields (P2-B)" enumerates all three fields (`peer_id`, `captured_pinset`, `negotiated_cipher`) with per-field inert disposition.

## Three-Site Acceptor Coordination

- [x] CHK018 Does the spec require ALL THREE coordinated acceptor sites (profile-map arm, listener accept-factory selection, post-accept handshake skip) — not two — as a lockstep change? [Completeness, Spec §Clarifications(Gate A r1)/§SC-001] — PASS: spec.md Clarifications (Gate A r1) states "**Three** coordinated code sites"; SC-001 requires the acceptor to be exercised "through `run_accept_loop`...so the three coordinated acceptor sites (profile-map arm, plaintext accept-factory selection, post-accept handshake skip — E-7) are all exercised"; E-7 enumerates all three with explicit site labels.
- [x] CHK019 Is the requirement explicit that the profile-map `else` arm must NOT fall through to `mtls_ca` for `insecure_plain_tcp` (the silent-TLS-listener footgun)? [Clarity, Spec §Clarifications/§D-4 site #1] — PASS: D-4 states "the `else` arm today maps every unrecognised profile kind — *including `insecure_plain_tcp`* — to `mtls_ca`...The fix MUST add an explicit `insecure_plain_tcp` arm that does **not** build an `ssl_cfg` / does not fall through to `mtls_ca`"; E-7 site #1 repeats the explicit no-`mtls_ca`-fallthrough requirement.
- [x] CHK020 Is the acceptor exercised end-to-end through `run_accept_loop` (not only a direct-transport loopback) made a stated acceptance condition, so all three sites are covered? [Measurability, Spec §SC-001] — PASS: SC-001 explicitly states "The acceptor side is witnessed end-to-end through `run_accept_loop` on `insecure_plain_tcp` (not only a direct-transport loopback)"; T007 tasks the end-to-end test through `run_accept_loop`.
- [x] CHK021 Is the listener Config-contract change (a `transport_kind` selector + concretely-typed accept factory) specified as a requirement, including why a base `TransportFactory*` is insufficient? [Completeness, Spec §D-4 site #2/§E-7] — DD-DECIDED §D-4/§E-7: D-4 site #2 specifies the `transport_security_kind transport_kind{tls}` selector on `asio_listener::Config` and states the rationale for concrete-typed hold ("`make_accepted()` is a concrete (non-virtual) method...callable only on the concrete type...a base `TransportFactory*` would not compile"); E-7 site #2 repeats. This is design-anchor decided, not spec prose; SC-001 + T015 cover the acceptance condition.
- [x] CHK022 Is the inert-TLS-validation-event-hooks consequence for plaintext accepted transports documented (acceptor twin of the initiator inert-consumer analysis)? [Coverage, Spec §E-7/§D-10] — DD-DECIDED §D-10/§E-7: D-10 last paragraph and E-7 both document "plaintext accepted transports run no handshake, so `set_listener_events(...)` wiring is **inert** — no `session_event_tls_validation_failed` / TLS-validation events"; documented as L-043-x. T025 tasks the B&L entry. Design-anchor decided and traceable.

## Handshake-Skip Gating

- [x] CHK023 Is the handshake skip specified as gated on the **session profile** (not on the `dynamic_cast` result alone), with the rationale that a cast-gated skip would silently downgrade a misconfigured TLS session? [Clarity, Spec §FR-005/§Clarifications] — PASS: FR-005 "MUST skip...gated on the session profile" and "skip MUST NOT be gated on the cast result alone, which would silently downgrade a misconfigured TLS session"; Clarifications repeats the rationale identically.
- [x] CHK024 Is the preserved fail-closed behaviour for TLS profiles (null `dynamic_cast<TlsTransport*>` ⇒ error) stated as an explicit invariant that the plaintext change must not weaken? [Consistency, Spec §FR-005] — PASS: FR-005 "For TLS profiles the existing 'null `dynamic_cast<TlsTransport*>` ⇒ error' behaviour MUST be preserved (fail-closed; the skip MUST NOT be gated on the cast result alone)"; explicit invariant preservation stated.
- [x] CHK025 Is the connect → (no handshake) → Logon sequence defined for both initiator and acceptor symmetrically? [Coverage, Spec §FR-005/§D-7/§D-8] — PASS: FR-005 covers initiator FSM ("`insecure_plain_tcp` ⇒ skip...connect → (no handshake) → Logon sequence MUST be honoured"); FR-004 requires acceptor `make_accepted()` symmetry; D-7 (FSM) and D-8 (acceptor run_accept_loop) each define the skip symmetrically.
- [x] CHK026 Is the SK→TK mapping requirement clear that plaintext must NOT build an `SslCtxConfig` / set a TLS `tls_profile` (which would arm a handshake)? [Clarity, Spec §D-7] — DD-DECIDED §D-7/§E-6: D-7 states "The session.cpp SK→TK mapping must NOT build an `SslCtxConfig` / set a TLS `tls_profile` for plaintext (which would arm a handshake). For `insecure_plain_tcp` the mapping leaves `tls_profile = unset`"; E-6 item (5) repeats. Also in spec.md Clarifications: "For `insecure_plain_tcp` the SK→TK mapping leaves `tls_profile=unset` and builds **no** `SslCtxConfig`". Settled at design anchor.

## Loud-Insecure Friction ([[deprecated]] witness)

- [x] CHK027 Is the friction requirement specified at the operator's *selection site* (the session-layer enumerator), with a concrete diagnostic class (`[[deprecated]]`)? [Clarity, Spec §FR-006/§D-9] — PASS: FR-006 "MUST surface a compile-time `[[deprecated]]`-class diagnostic at the construction/selection site (the session-layer `SecurityProfile::kind` enumerator)"; D-9 specifies the exact attribute text including message. Concrete and located.
- [x] CHK028 Is "observable at build time" (SC-005) defined with a measurable witness mechanism (an automated negative-compile / `try_compile`), not satisfiable by a manual note? [Measurability, Spec §SC-005] — SPEC-FIXED: SC-005 in spec.md previously said only "observable at build time as a deprecation-class diagnostic" without excluding a manual note; research.md D-13 open-items explicitly offered a manual fallback ("OR a documented manual witness") inconsistent with tasks.md T017's exclusion of that fallback. SC-005 amended to require "an automated negative-compile test (e.g. a CMake `try_compile` / the repo's negative-compile harness, compiled with `-Werror=deprecated-declarations`); a manual one-time note is NOT sufficient." research.md D-13 open-items updated to close the fallback language. Edits: `spec.md §SC-005`, `research.md §D-13 open items`.
- [x] CHK029 Is the requirement consistent that fixpp-internal references suppress the diagnostic while the operator's site still warns (so the project build stays clean)? [Consistency, Spec §D-9] — PASS: D-9 specifies `#pragma clang diagnostic ignored "-Wdeprecated-declarations"` suppression for internal references (session.cpp:1173-1176 precedent); spec.md Clarifications: "fixpp-internal references suppress via `#pragma clang diagnostic ignored`"; T020 tasks the sweep. Consistent.
- [x] CHK030 Is the pre-existing `one_way_ca` session-layer drift explicitly scoped OUT (flagged, not silently inherited or "fixed" here)? [Conflict, Spec §FR-006/§D-9] — PASS: FR-006 "(NOTE: this is *stronger* than the current session-layer `one_way_ca`, which carries no such attribute despite §XII.5 — a pre-existing constitution-vs-code drift left out of 043 scope; see research.md D-9)"; D-9 "Parity decision: bringing session-layer `one_way_ca` to parity is **OUT OF 043 SCOPE**". Explicitly flagged and deferred.

## No-New-Surface & Scope Discipline

- [x] CHK031 Is the no-new-public-surface boundary (FR-013) enumerated specifically (the enumerator + transport/factory types + reuse of existing error slots — no new wire/error/codegen)? [Clarity, Spec §FR-013] — PASS: FR-013 "MUST NOT introduce new public wire fields, error slots, or codegen surface beyond the `insecure_plain_tcp` enumerator, the plaintext transport/factory types, and any consistency-check reuse of the existing `error::invalid_session_config` slot." Specifically enumerated; D-11 lists exact error slots reused. No ambiguity.
- [x] CHK032 Is the bench-driver exclusion (FR-012) stated as an explicit non-goal with the relationship "only makes the rows satisfiable", avoiding scope creep? [Completeness, Spec §FR-012/§SC-007] — PASS: FR-012 "MUST NOT introduce the phase-9 perf bench driver (Tier-4 item 15a) — it only makes the `TLS off` benchmark rows satisfiable"; SC-007 "without this feature shipping the bench driver itself". Explicit non-goal with relationship stated.
- [x] CHK033 Is the `EncryptMethod(98)≠0`-still-rejected requirement on the plaintext path stated explicitly (transport-plaintext ≠ app-layer-encryption), not assumed via shared code? [Completeness, Spec §FR-009/§Edge Cases] — PASS: FR-009 "Application-layer encryption (`EncryptMethod(98) ≠ 0`) MUST remain rejected on a plaintext session (constitution §XII.7 unchanged) — plaintext removes *transport* encryption only, never permits app-layer encryption"; Edge Cases section repeats; T008 explicitly witnesses it on the new plaintext surface.
- [x] CHK034 Is the ≤5-pure-virtual constraint satisfaction documented (the new `kind()` is DEFAULTED, keeping the pure count at 3) as a stated design requirement? [Traceability, Spec §Normative Refs/§D-5] — PASS: Normative Refs `[const §XIV.2]` cited; D-5 states "The **pure**-virtual count stays **3** (well under the `[const §XIV.2]` 5/5 cap)"; transport_factory.hpp verified to have exactly 3 `= 0` methods (lines 75, 94, 108); no `kind()` `= 0`. Traced and verified.

## Acceptance-Criteria Quality & Edge Cases

- [x] CHK035 Are all eight Success Criteria (SC-001..SC-008) individually measurable and each traceable to ≥1 requirement, with no SC stated as an unverifiable outcome? [Measurability, Spec §Success Criteria] — PASS: SC-001→FR-002/FR-004 (T005/T007); SC-002→FR-007 (T018); SC-003→FR-008 (T021); SC-004→FR-008a (T008); SC-005→FR-006 (T017); SC-006→FR-007 (T029); SC-007→FR-012 (T026/T028); SC-008→FR-010/FR-011 (T006). Each SC has a named witness task and a traceable FR.
- [x] CHK036 Is the close-path requirement quantified (no `tls_close_timeout` wait, no close-notify, `transport_read_truncated` inapplicable) rather than "closes directly"? [Clarity, Spec §FR-011/§SC-008] — PASS: FR-011 "MUST close the socket directly with **no** TLS bidi shutdown step and **no** `tls_close_timeout` wait; `transport_read_truncated`...does not apply"; SC-008 "returns **without** a `tls_close_timeout`-length delay and emits no TLS close-notify". Fully quantified.
- [x] CHK037 Are the `Transport::Config` TCP-knob reuse requirements specified (which knobs apply, which TLS-only knobs are inert) so SC-008 is objectively checkable? [Completeness, Spec §FR-010/§SC-008] — PASS: FR-010 lists specific applying knobs (tcp_nodelay default ON, keepalive, send/recv buffers, SO_LINGER, SO_REUSEADDR) and identifies inert TLS-only knobs (`tls_handshake_timeout`, `tls_close_timeout`); D-12 confirms the same. SC-008 references FR-010/FR-011.
- [x] CHK038 Are the connect-failure and reconnect edge cases on the plaintext path specified to surface the same `transport_connect_*` variants and the same fresh-transport-per-attempt rule as TLS? [Coverage, Spec §Edge Cases] — PASS: Edge Cases section: "a refused/timed-out TCP connect surfaces the same `transport_connect_*` error variants"; "each reconnect attempt mints a fresh plaintext transport via the factory (same fresh-transport-per-attempt rule as TLS); the reconnect FSM does **not** attempt a TLS handshake step". Both edge cases specified.
- [x] CHK039 Is the mixed-acceptor edge case defined (a TLS ClientHello on a plaintext acceptor is consumed as garbage FIX bytes; no downgrade/upgrade negotiation exists)? [Edge Case, Spec §Edge Cases] — PASS: Edge Cases section: "an acceptor configured plaintext accepts plain connections only; a TLS ClientHello arriving on a plaintext acceptor is consumed as ordinary (garbage) FIX bytes and fails framing/Logon — no TLS downgrade/upgrade negotiation exists".
- [x] CHK040 Is there a stated requirement that the new plaintext transport is covered by the debug strand-confinement invariant (not silently skipped on the null TLS downcast)? [Gap, Spec §D-13] — DD-DECIDED §D-13/T016: D-13 "the 043 implement phase MUST close this: either add an `asio_plain_transport*` arm to the assert...or promote `socket_executor()` to a base `Transport` accessor so the assert works for both"; T016 is the assigned task. Design anchor mandates the requirement and the pipeline.md tasks gate will enforce it.

## Notes

- Check items off as resolved: `[x]`. For a PASS, cite the spec/research line satisfying it; for a fail,
  record the gap and whether it is SPEC-FIXED / DD-DECIDED / WAIVED at the step-9 checklist-audit.
- This checklist tests requirement WRITING quality; the implementation witnesses live in `tasks.md`
  (T005–T029) and are validated by `/speckit-implement` + Gate B, not here.

---

## Audit Result

**Audited**: 2026-06-17 (pipeline.md step 9)
**Auditor**: checklist-auditor subagent (Claude Sonnet 4.6)

| Disposition | Count |
|---|---|
| PASS | 34 |
| SPEC-FIXED | 1 |
| DD-DECIDED | 4 |
| WAIVED | 0 |
| **Total** | **40** |

### PASS items
CHK001–CHK016 (all), CHK018–CHK020, CHK023–CHK025, CHK027, CHK029–CHK039.

### SPEC-FIXED items
- CHK028 — SC-005 in `spec.md` did not exclude a manual note; `research.md` D-13 open-items offered a manual fallback inconsistent with T017's exclusion of it. **Edits**: (1) `spec.md §SC-005`: added "The witness MUST be an automated negative-compile test (e.g. a CMake `try_compile` / the repo's negative-compile harness, compiled with `-Werror=deprecated-declarations`); a manual one-time note is NOT sufficient." (2) `research.md §D-13 open-items`: closed the "OR a documented manual witness" fallback language.

### DD-DECIDED items
- CHK021 — anchor `D-4/E-7`; rationale: concrete-typed accept factory rationale (why base `TransportFactory*` is insufficient) settled at the design anchor, not spec prose; SC-001 + T015 cover the acceptance condition.
- CHK022 — anchor `D-10/E-7`; rationale: inert TLS-validation-event-hooks for plaintext accepted transports is the acceptor twin of D-10's initiator analysis, documented and tasked via T025.
- CHK026 — anchor `D-7/E-6` + spec.md Clarifications; rationale: SK→TK no-`SslCtxConfig` mapping fully specified in research.md D-7 and data-model.md E-6, also stated in spec.md Clarifications.
- CHK040 — anchor `D-13/T016`; rationale: design anchor D-13 mandates the strand-confinement assert extension, T016 tasks it; the requirement is frozen in the design authority.

### WAIVED items
*(none)*

### Anchors spot-verified
- `[const §XII.5]` (amended v0.3) — resolves in constitution.md Article XII §5 lines 183-189.
- `[const §XII.7]` — resolves in constitution.md Article XII §7 line 191.
- `[const §XIV.2]` — resolves in constitution.md Article XIV §2 line 215.
- `[const §XII.9]` — resolves in constitution.md Article XII §9 line 193.
- All FR-001..FR-013, SC-001..SC-008, US1/US2/US3, D-1..D-13, E-1..E-7 — all resolve in the signed-off bundle artifacts.
- `run_accept_loop`, `install_reconnected_transport`, `attach_accepted_transport` — confirmed in live library source (engine.cpp, session.cpp).
- `TransportFactory` 3 pure-virtuals (`make`, `reload_credentials`, `cert_source_snapshot`, all `= 0`) — confirmed in transport_factory.hpp lines 75, 94, 108. `make_accepted` confirmed concrete-only on `asio_tls_transport_factory` (line 159).

All anchors resolve in signed-off revision constitution.md v0.3 (2026-06-17).
