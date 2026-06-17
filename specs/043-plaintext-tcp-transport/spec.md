# Feature Specification: Plaintext TCP transport (insecure_plain_tcp)

**Feature Branch**: `043-plaintext-tcp-transport`
**Created**: 2026-06-17
**Status**: Draft
**Input**: User description: "Plaintext TCP transport (asio_plain_transport) — a sibling to asio_tls_transport that carries the FIX byte stream over a plain TCP socket with NO TLS. Gated behind the new SecurityProfile::kind::insecure_plain_tcp."

## Context & Background

Today fixpp's only real-socket transport is `asio_tls_transport` — TLS is mandatory on every
established session (the in-memory `mock_transport` is a scripted test double, not a socket). Two
drivers make a plaintext transport worth building:

1. **Production interop.** Plaintext FIX over a transport secured *beneath* the application — a
   colocation cross-connect or a VPN/IPsec tunnel — is a common industry deployment. TLS/FIXS is the
   newer overlay; being TLS-only is itself a real interop limitation versus QuickFIX-cpp / QuickFIX/J /
   Fix8, all of which run plaintext by default.
2. **Benchmark fairness.** `phases/phase-9/benchmark-plan.md` runs workloads wl-01…wl-11 with `TLS off`
   to isolate engine throughput. fixpp cannot satisfy those rows today, so any comparison is
   "fixpp+OpenSSL vs QF-plaintext", not an engine comparison (`benchmark-readiness.md` blocker #3).

The security caveat is binding: a non-TLS path MUST be an explicit, loud opt-in and MUST NOT become an
implicit default. The closed `SecurityProfile` set was reopened for exactly this purpose by the
**constitution v0.3 amendment (Article XII §5, 2026-06-17)**, which added a fourth profile kind
`insecure_plain_tcp` with the same loud, `[[deprecated]]`-class construction-site friction as the
legacy `one_way_ca` profile.

This feature builds the plaintext transport + factory and wires the session/reconnect path to honour
the new profile. It deliberately **excludes** the phase-9 perf bench driver (a separate downstream
consumer, REMAINING-WORK Tier-4 item 15a) — this feature only makes the `TLS off` rows *possible*.

## Clarifications

### Session 2026-06-17

- Q: How is the plaintext opt-in surfaced in the public API? → A: A new closed-enum value
  `SecurityProfile::kind::insecure_plain_tcp` (not an orthogonal transport-security selector), keeping a
  single `open()`-time gate and reusing the `one_way_ca` `[[deprecated]]` friction precedent.
- Q: Is the non-TLS profile permitted as production-candidate or bench-only? → A: Production-candidate
  (benchmark-readiness.md §3 "DECISION TAKEN 2026-06-13"); the loud-insecure friction is the mitigation.
- Q: Does it apply to both session roles? → A: Both — initiator and acceptor (the plain factory carries
  an acceptor `make_accepted()` path symmetric to the TLS factory).
- Q: Where does the plaintext factory come from, and what is the FR-008 mismatch? → A: **Auto-derive from
  profile** — `insecure_plain_tcp` ⇒ the engine uses a built-in credential-free plaintext factory (no
  explicit override needed); TLS profiles ⇒ the TLS factory. An *explicit* `transport_factory_override`
  whose kind disagrees with the profile (TLS factory + plaintext profile, or plaintext factory + TLS
  profile) is rejected at `open()` with `error::invalid_session_config`; a plaintext profile left with the
  default TLS factory is auto-corrected, not an error. (Mirrors QFJ `SocketUseSSL` / Fix8 `ssl_context=`
  config-derived transport rather than quickfix-cpp's separate-class selection.) The factory advertises
  its kind via a defaulted `TransportFactory::kind()` (default `tls`; so existing factories/test doubles
  are unaffected).
- Q: What authorization stays active on the plaintext path (no cert identity exists)? → A: The 015
  `compid_authorization_policy` (CompID↔TLS-cert-identity binding) is **skipped** — it consumes a
  handshake `peer_id` that plaintext never produces, and it is already gated to mTLS-only today (skipped
  for `one_way_ca`/non-mTLS, `session.cpp` case 3), so plaintext behaves identically to `one_way_ca` here.
  The **cert-independent** `check_comp_id` inbound 49/56-vs-configured-CompID match (default on) **still
  applies** unchanged. `handshake_result`-dependent reads (peer DN, negotiated cipher, captured pinset,
  cert-event spans) are inert/empty. Net: no peer authentication on this profile — documented as a
  limitation (L-043-x). *(Correction to the question's framing: the surviving CompID check is
  `check_comp_id`, not `compid_authorization_policy`, which is itself the cert-identity binding.)*
- Q: What gates the reconnect-FSM handshake skip? → A: **The session profile** — `insecure_plain_tcp` ⇒
  skip the `dynamic_cast<TlsTransport*>` + `async_handshake` step entirely (connect → Logon). For TLS
  profiles the existing "null cast ⇒ error" stays fail-closed (a TLS profile that received a non-TLS
  transport is still a bug). NOT gated on the cast result alone (which would silently downgrade a
  misconfigured TLS session to plaintext).

### Session 2026-06-17 (Gate A round 1)

- Q: Does the FR-008 profile↔factory consistency check key on the explicit override or the resolved
  factory? → A: The **effective/resolved** factory. A TLS profile with no session override but a plaintext
  *engine-default* factory must fail at `open()` (not late at the FSM `dynamic_cast`). FR-008 + SC-003 +
  US3 AC#3 + D-6/E-6 updated.
- Q: Is `live_peer_id_` left unset on plaintext a SHOULD or a MUST? → A: A **MUST** everywhere — the
  plaintext handoff constructs/passes no `handshake_result` and both `install_reconnected_transport` and
  `attach_accepted_transport` MUST leave `live_peer_id_ == nullopt` (fail-closed-by-construction; verified
  not a live hole today since every reader is `is_mtls`-gated). research D-10 ⇄ data-model E-7 reconciled.
- Q: Is the plaintext acceptor a two-site or three-site change? → A: **Three** coordinated code sites
  (engine.cpp:664-681 profile-map arm — no `mtls_ca` fall-through; asio_listener `Config.transport_kind`
  accept-factory selection; engine.cpp:783-797 handshake skip) plus a listener Config contract change;
  plaintext accepted transports receive no TLS-validation event hooks. D-4/D-8/E-7 updated.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Establish a plaintext FIX session over plain TCP (Priority: P1)

An operator who controls a secured link (colo cross-connect / VPN) configures a session with
`security_profile = insecure_plain_tcp` and a plaintext transport factory. The session connects over a
plain TCP socket — **no TLS handshake, no TLS bytes on the wire** — and exchanges FIX (Logon → app
traffic → Logout) exactly as a TLS session does. This works for both an initiator and an acceptor.

**Why this priority**: This is the feature's core capability and the MVP. Without it neither the
production-interop driver nor the benchmark-fairness driver is satisfied.

**Independent Test**: Stand up a plaintext initiator and a plaintext acceptor (or drive the
`asio_plain_transport` directly against a loopback `asio::ip::tcp::acceptor`); assert a Logon/Logout
round-trip completes over a plain socket and that no TLS ClientHello is ever emitted.

**Acceptance Scenarios**:

1. **Given** a session configured with `security_profile = insecure_plain_tcp` and a plaintext transport
   factory, **When** `Session::open()` runs, **Then** the session connects over a plain TCP socket with
   no TLS handshake step and reaches the post-connect Logon emission.
2. **Given** a plaintext initiator and a plaintext acceptor on a loopback endpoint, **When** the
   initiator opens, **Then** both sides complete a FIX Logon exchange and a clean Logout.
3. **Given** an established plaintext session, **When** `close()` is called, **Then** the socket is
   closed directly with no TLS bidi-shutdown (close-notify) step and no `tls_close_timeout` wait.

---

### User Story 2 - Opt-in only, with loud insecure friction (Priority: P1)

A developer who selects `insecure_plain_tcp` is loudly warned at compile time that transport security is
OFF — the same `[[deprecated]]`-class diagnostic that `one_way_ca` produces. A developer who selects
nothing gets the unchanged no-implicit-default behaviour: the `unset` sentinel is rejected at
`Session::open()`. Plaintext is never selected implicitly or silently.

**Why this priority**: This is a security-surface feature; the guardrail is as load-bearing as the
capability. Shipping the plaintext path without the loud, explicit-opt-in friction would be the actual
risk the constitution amendment guards against.

**Independent Test**: Compile a TU that selects `insecure_plain_tcp` and assert the deprecation-class
diagnostic fires; separately assert a default-constructed `SecurityProfile` (kind::unset) is still
rejected at `open()` with `error::invalid_session_config`.

**Acceptance Scenarios**:

1. **Given** source that selects `insecure_plain_tcp`, **When** it is compiled, **Then** a
   `[[deprecated]]`-class diagnostic announcing that transport security is OFF is emitted at the
   selection site (the session-layer enumerator) — the construction-site diagnostic §XII.5 prescribes.
2. **Given** a default-constructed `SecurityProfile` (kind::unset), **When** `Session::open()` runs,
   **Then** it returns `error::invalid_session_config` — unchanged from today.
3. **Given** any session that did not explicitly select `insecure_plain_tcp`, **When** it opens, **Then**
   the plaintext path is never taken (no implicit default).

---

### User Story 3 - Fail closed on profile↔transport mismatch at open() (Priority: P2)

A misconfiguration in which the security profile and the transport factory disagree — `insecure_plain_tcp`
paired with a TLS factory, or a TLS profile paired with a plaintext factory — is rejected at
`Session::open()`, **before** any connect/handshake attempt, rather than being discovered late at
handshake time (where a TLS profile + plain factory would silently never authenticate, or a plaintext
profile + TLS factory would attempt a handshake the peer never speaks).

**Why this priority**: Defence-in-depth against the most dangerous failure mode of a mixed
TLS/plaintext world — a session that *believes* it is secure but is not, or that hangs. It is P2 because
US1+US2 already deliver a usable, safe MVP; this hardens the boundary.

**Independent Test**: Construct sessions with each mismatched (profile, factory) pairing — including a TLS
profile with NO session override but a *plaintext engine-default factory* — and assert `open()` returns
`error::invalid_session_config`; construct each matched pairing and assert `open()` succeeds.

**Acceptance Scenarios**:

1. **Given** `security_profile = insecure_plain_tcp` with an explicit TLS transport factory override,
   **When** `Session::open()` runs, **Then** it returns `error::invalid_session_config`.
2. **Given** a TLS `security_profile` (mtls_ca / mtls_pinned / one_way_ca) with an explicit plaintext
   transport factory override, **When** `Session::open()` runs, **Then** it returns
   `error::invalid_session_config`.
3. **Given** a TLS `security_profile` with **no** session override but a plaintext *engine-default*
   factory (`EngineConfig::default_transport_factory`), **When** `Session::open()` runs, **Then** it
   returns `error::invalid_session_config` at `open()` — the **effective** (resolved) factory's kind is
   checked, not just an explicit override, so the mismatch fails closed at open() rather than late at the
   FSM cast.
4. **Given** matched pairings (plaintext profile + plaintext factory; TLS profile + TLS factory; and a
   plaintext profile with no override, auto-derived to the built-in plaintext factory),
   **When** `Session::open()` runs, **Then** it succeeds.

---

### Edge Cases

- **Connect failure on the plain path**: a refused/timed-out TCP connect surfaces the same
  `transport_connect_*` error variants as the TLS path (no handshake stage to confuse the diagnosis).
- **Reconnect**: each reconnect attempt mints a fresh plaintext transport via the factory (same
  fresh-transport-per-attempt rule as TLS); the reconnect FSM does **not** attempt a TLS handshake step
  on the plaintext profile.
- **EncryptMethod(98) ≠ 0 on a plaintext session**: rejected at inbound Logon (constitution §XII.7) —
  plaintext removes *transport* encryption only, never permits app-layer encryption. 043 adds the inbound
  enforcement (the prior baseline skipped tag 98 inbound — S-021/TC-017 gap; see FR-009).
- **`handshake_result`-dependent reads** (peer identity, negotiated cipher, captured pinset): a plaintext
  session has no `handshake_result`; consumers that assume one (TLS cert-event spans, identity readback)
  MUST be inert/skipped on the plaintext path, not crash.
- **CompID↔TLS-identity binding (015)**: `compid_authorization_policy.authorize(peer_id, compid)` is a
  TLS-profile property already gated to mTLS-only; on `insecure_plain_tcp` there is no peer certificate
  identity to bind, so it is skipped exactly as for `one_way_ca`/non-mTLS — no error, documented as L-043-x
  (no peer authentication). The cert-independent `check_comp_id` inbound-CompID match is unaffected.
- **Mixed acceptor**: an acceptor configured plaintext accepts plain connections only; a TLS ClientHello
  arriving on a plaintext acceptor is consumed as ordinary (garbage) FIX bytes and fails framing/Logon —
  no TLS downgrade/upgrade negotiation exists.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The session-layer `SecurityProfile` closed enum MUST gain a fourth kind
  `insecure_plain_tcp`, accepted by `Session::open()` (it is NOT the `unset` sentinel). Per the v0.3
  constitution amendment (§XII.5).
- **FR-002**: A plaintext transport (`asio_plain_transport`) MUST carry the FIX byte stream over a plain
  `asio::ip::tcp` socket implementing the five base `Transport` operations (connect, read-some, composed
  write, cancel, close) with their existing contracts (cancellation → `transport_*_cancelled`, in-flight
  exclusivity, idempotent close). It MUST NOT be a `TlsTransport` and MUST NOT expose `async_handshake`.
- **FR-003**: A plaintext transport factory (sibling to the TLS factory) MUST mint `asio_plain_transport`
  instances. It MUST construct successfully **without any TLS credentials / cert source** (no SSL_CTX,
  no cert load).
- **FR-003a [Profile-derived factory]**: When `security_profile = insecure_plain_tcp` and no explicit
  `transport_factory_override` is supplied, the engine MUST use a built-in plaintext factory (the
  operator need not hand-install one and need not clear the default TLS factory). TLS profiles continue to
  resolve to the configured TLS factory. (Config-derived transport selection, per the QFJ/Fix8 precedent.)
  The resolved effective factory MUST be the *same* object used for BOTH the FR-008 consistency check AND
  reconnect minting (it MUST be wired into the reconnect FSM at `open()`, before any connect attempt — not
  only validated), so a plaintext/no-override session actually connects through the checked factory rather
  than failing late on an unwired (null/stale) factory pointer.
- **FR-004 [Acceptor symmetry]**: The plaintext factory MUST provide an acceptor path equivalent to the
  TLS factory's `make_accepted()` — adopting an already-accepted plain socket — so plaintext works for
  the **acceptor role**, not just the initiator. (Guards against the half-restructure trap.)
- **FR-005**: The reconnect/connect path MUST skip the TLS handshake stage **gated on the session
  profile** — `insecure_plain_tcp` ⇒ skip the `dynamic_cast<TlsTransport*>` + `async_handshake` step
  entirely; the connect → (no handshake) → Logon sequence MUST be honoured. For TLS profiles the existing
  "null `dynamic_cast<TlsTransport*>` ⇒ error" behaviour MUST be preserved (fail-closed; the skip MUST
  NOT be gated on the cast result alone, which would silently downgrade a misconfigured TLS session).
- **FR-006 [Loud insecure friction]**: Selecting `insecure_plain_tcp` MUST surface a compile-time
  `[[deprecated]]`-class diagnostic at the construction/selection site (the session-layer
  `SecurityProfile::kind` enumerator), announcing that transport security is OFF — the construction-site
  diagnostic `[const §XII.5]` prescribes. (NOTE: this is *stronger* than the current session-layer
  `one_way_ca`, which carries no such attribute despite §XII.5 — a pre-existing constitution-vs-code drift
  left out of 043 scope; see research.md D-9.)
- **FR-007 [No implicit default]**: The no-implicit-default rule MUST be preserved unchanged — the
  `unset` sentinel is still rejected at `Session::open()`, and `insecure_plain_tcp` is never selected
  implicitly. Existing TLS profiles and their behaviour MUST be entirely unaffected.
- **FR-008 [Fail-closed consistency]**: `Session::open()` MUST resolve the **effective** transport factory
  (per FR-003a: plaintext + no override ⇒ built-in plaintext factory; otherwise the explicit
  `transport_factory_override` if present, else the engine-default factory) and MUST reject the session
  with `error::invalid_session_config`, before any connect/handshake attempt, when the **effective**
  factory's kind disagrees with the profile category — a TLS profile (mtls_ca/mtls_pinned/one_way_ca)
  requires an effective factory of kind `tls`; `insecure_plain_tcp` requires kind `plaintext`. This MUST
  catch **both** an explicitly-supplied mismatched override **and** a wrong *engine-default* factory (e.g.
  a plaintext default factory installed at engine level for a TLS session, which must fail at `open()`
  before connect — not late at the FSM `dynamic_cast`). (Per FR-003a, a plaintext profile with NO override
  and the default TLS factory is auto-corrected to the built-in plaintext factory, not rejected.) Matched
  pairings MUST open.
- **FR-008a [Authorization on the plaintext path]**: On `insecure_plain_tcp` the 015
  `compid_authorization_policy` (CompID↔TLS-cert-identity binding) MUST be skipped — identically to how it
  is already skipped for non-mTLS profiles (`one_way_ca`/unset) — because no handshake `peer_id` exists.
  The cert-independent `check_comp_id` inbound-CompID match MUST remain in effect unchanged. The plaintext
  profile therefore provides **no peer authentication** (documented limitation L-043-x).
- **FR-009**: Application-layer encryption (`EncryptMethod(98) ≠ 0`) MUST be rejected on every session,
  plaintext included (constitution §XII.7). **Implementation note (2026-06-17, user-ratified):** the
  pre-043 baseline did NOT enforce this on the **inbound** path — `interpret_logon` skipped tag 98, a gap
  S-021 documented ("inbound 98≠0 not handled") and TC-017 backlogged. The original "remain rejected /
  unchanged" phrasing was a false-green against that baseline. 043 **closes the gap**: `interpret_logon`
  now rejects an inbound Logon carrying `98 ≠ "0"` (present-but-malformed fails closed) with
  `session_invalid_logon`, unconditionally across all profiles. Witnessed by
  `tests/session/test_interpret_logon_encrypt_method.cpp`.
- **FR-010**: The plaintext transport MUST honour the existing `Transport::Config` TCP knobs (tcp_nodelay
  default ON, keepalive, send/recv buffers, SO_LINGER, SO_REUSEADDR on the acceptor) identically to the
  TLS transport; the TLS-only knobs (`tls_handshake_timeout`, `tls_close_timeout`) are inert on this path.
- **FR-011**: `close()` on a plaintext transport MUST close the socket directly with **no** TLS bidi
  shutdown step and **no** `tls_close_timeout` wait; `transport_read_truncated` (a TLS close-notify
  concept) does not apply.
- **FR-012 [Scope exclusion]**: This feature MUST NOT introduce the phase-9 perf bench driver (Tier-4
  item 15a) — it only makes the `TLS off` benchmark rows satisfiable.
- **FR-013 [No new public surface beyond the profile + transport]**: This feature MUST NOT introduce new
  public wire fields, error slots, or codegen surface beyond the `insecure_plain_tcp` enumerator, the
  plaintext transport/factory types, and any consistency-check reuse of the existing
  `error::invalid_session_config` slot.

### Key Entities

- **`SecurityProfile::kind::insecure_plain_tcp`**: the new closed-enum value (session layer) selecting
  the non-TLS path; default-rejected `unset` sentinel unchanged.
- **`asio_plain_transport`**: the plain-TCP `Transport` implementation (5 base ops; no handshake).
- **plaintext transport factory**: sibling to `asio_tls_transport_factory`; mints `asio_plain_transport`
  with no credentials; carries the acceptor `make_accepted()` path.
- **profile↔factory consistency check**: the `Session::open()` validation arm enforcing that a TLS
  profile uses a TLS factory and the plaintext profile uses a plaintext factory.

## Normative References

- **`[const §XII.5]`** (amended v0.3, 2026-06-17) — reopened the closed `SecurityProfile` set; added
  `insecure_plain_tcp`; opt-in-only + loud `[[deprecated]]`-class friction; §1–§4 inapplicable on this
  profile.
- **`[const §XII.7]`** — `EncryptMethod(98) ≠ 0` rejected. **043 ENFORCES this on the inbound Logon path**
  (the pre-043 baseline did not — a false-green corrected here; closes the S-021/TC-017 gap; FR-009).
- **`[const §XII.9]`** — security-affecting features trigger all four mandatory controls (`/clarify`,
  `/analyze`, Codex Gate A, user `/plan` sign-off). This feature is in scope.
- **`[const §XIV.2]`** — pluggable-interface ≤5 pure-virtual cap (`Transport` base; plaintext uses none
  of the `TlsTransport` extension slots).
- **2h transport design doc** — `Transport` / `TransportFactory` contracts (the plaintext transport
  honours the same base contracts; the "every v1.0 transport is TLS-capable" partition note is the
  premise this feature revises).
- **`phases/phase-9/benchmark-readiness.md` §3** + **`benchmark-plan.md`** — the benchmark-fairness driver
  and the `TLS off` rows.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A plaintext initiator and a plaintext acceptor complete a FIX Logon → Logout round trip
  over a plain TCP socket, and no TLS ClientHello / handshake byte is ever emitted on the connection. The
  acceptor side is witnessed end-to-end through `run_accept_loop` on `insecure_plain_tcp` (not only a
  direct-transport loopback) so the three coordinated acceptor sites (profile-map arm, plaintext
  accept-factory selection, post-accept handshake skip — E-7) are all exercised.
- **SC-002**: A default-constructed / `unset` `SecurityProfile` is still rejected at `Session::open()`
  with `error::invalid_session_config`, and no code path selects `insecure_plain_tcp` implicitly.
- **SC-003**: All profile↔factory mismatch directions are rejected at `Session::open()` with
  `error::invalid_session_config` — both *explicitly-overridden* directions **and** a TLS profile with no
  override but a plaintext *engine-default* factory (the **effective** factory is checked, not just the
  override); both matched pairings (and a plaintext profile with no override) open successfully.
- **SC-004**: On a plaintext session the 015 cert-identity CompID authorization is skipped — no
  `peer_id`-dependent path runs, `compid_authorization_policy` is **not** called, and **no** peer-identity
  state exists (`live_peer_id_ == nullopt`) — while `check_comp_id` still rejects a mismatched inbound
  CompID. The plaintext profile authenticates no peer identity but preserves the cert-independent CompID
  match.
- **SC-005**: Selecting `insecure_plain_tcp` is observable at build time as a deprecation-class
  diagnostic (the loud-insecure friction is present, not silently optimisable away). The witness MUST
  be an automated negative-compile test (e.g. a CMake `try_compile` / the repo's negative-compile
  harness, compiled with `-Werror=deprecated-declarations`); a manual one-time note is NOT sufficient.
- **SC-006**: All existing TLS sessions and the full pre-existing test suite are behaviour-unchanged —
  the plaintext path is purely additive (zero regression in the TLS transport, factory, and session FSM).
- **SC-007**: The `benchmark-plan.md` `TLS off` rows (wl-01…wl-11) become satisfiable — a plaintext
  initiator+acceptor pair can be stood up over a real socket — without this feature shipping the bench
  driver itself.
- **SC-008**: A `Transport::Config` TCP knob takes effect on the plaintext path (e.g. `tcp_nodelay`
  default ON / a non-default keepalive or buffer value is observable on the plain socket), and
  `close()` on an established plaintext transport returns **without** a `tls_close_timeout`-length delay
  and emits no TLS close-notify (FR-010 / FR-011).

## Assumptions

- The plaintext opt-in is a new `SecurityProfile::kind` value (decided 2026-06-17), not an orthogonal
  selector; the constitution amendment (v0.3) is the ratifying authority and its Gate A is folded into
  this feature's Gate A.
- The plaintext transport reuses the existing encryption-agnostic `Transport` base contract verbatim;
  no change to the `Transport` / `TransportFactory` *base* virtual surface is required (the factory's
  existing `make()` carries an `SslCtxConfig` parameter that the plaintext factory ignores; whether to
  ignore-the-param vs add a credential-free entry point is a `/plan` decision, not a spec concern).
- `[[deprecated]]`-class friction is compile-time only (on the session enumerator); no additional runtime
  block on plaintext sessions is required — the constitution amendment permits the profile.
- Loopback / in-process socket tests are an acceptable witness for the round-trip SCs; live cross-engine
  plaintext interop (QFcpp/QFJ/Fix8) and the actual benchmark run are downstream (Tier-1 / Tier-4),
  out of this feature's scope.
- Per-message read/write allocation behaviour on the plaintext path matches the TLS path (caller-owned
  buffers; transport never allocates a read buffer) — no new alloc-NFR surface beyond the existing one.
