# Phase 1 Data Model — 013-session-reconnect-binding

**Branch**: `013-session-reconnect-binding` | **Date**: 2026-05-28 | **Plan**: [plan.md](plan.md) | **Research**: [research.md](research.md)

This document enumerates the entities E-1..E-7 introduced or extended by this feature, with fields, ownership, allocator policy, lifetime, validation rules, and state transitions. Cross-references decisions D-1..D-17 from `research.md` and FRs from `spec.md`.

---

## E-1 — ReconnectFsm (NEW)

**File**: `include/fixpp/session/reconnect_fsm.hpp` + `src/session/reconnect_fsm.cpp`

**Purpose**: Driver layer on top of the shipped 6-state `fsm_state` (NotConnected / LogonSent / LogonReceived / Active / LogoutSent / Disconnected). Owns the `ReconnectPolicy` walk, per-attempt `Transport` minting via `TransportFactory`, the transient `awaiting_resend` flag on `Active`, the Heartbeat / TestRequest cadence timers, and the Logout / force-disconnect timeout.

**Fields**:

| Field | Type | Ownership | Lifetime | Notes |
|---|---|---|---|---|
| `policy_` | `ReconnectPolicy` (value) | by-value | reconnect-cycle | consumed verbatim from 012 [2h §4.4]; mutable across cycles (operator may rotate via SessionConfig swap pattern) |
| `attempt_index_` | `std::uint32_t` | by-value | reconnect-cycle | resets to 0 on Active; increments per failed reconnect |
| `factory_` | `TransportFactory*` (non-owning) | non-owning ref | session | consumed from 012; the smart-pointer to TransportFactory is owned by SessionConfig::transport_factory_override (storage type `shared_ptr<TransportFactory>` per 013 E-4 ownership reconciliation; behaviour-equivalent to 2h Appendix D §D.1+§D.2 unique_ptr sign-off via Session::open-time hygiene assertion); ReconnectFsm holds a raw pointer (the engine guarantees the factory outlives the FSM per [arch §5.6] frozen-at-open rule); the atomic-swap slot for cert_source lives INSIDE the factory (`cert_source_slot_: std::atomic<std::shared_ptr<cert_source>>`), so `reload_credentials` does NOT need to swap the factory pointer itself |
| `heartbeat_timer_` | `asio::steady_timer` | by-value | Active | armed on Active entry; rearmed on every outbound; cancelled on Disconnected entry |
| `test_request_timer_` | `asio::steady_timer` | by-value | Active | armed when inbound liveness window > 1.2× HeartBtInt; cancelled on inbound traffic |
| `logout_timer_` | `asio::steady_timer` | by-value | LogoutSent | armed on Session::logout() call; fires session_logout_timeout (slot 73, 005-era reused per F1/D1 2026-05-28) |
| `awaiting_resend_` | `bool` | by-value | Active | TRANSIENT FLAG, NOT a new fsm_state value per D-1; set on FR-009 entry; cleared on gap close |
| `resend_state_` | `ResendState` (value; see E-2) | by-value | AwaitingResend | populated on FR-009 entry; reset on gap close or session exit |
| `last_outbound_testreqid_` | `std::optional<std::string>` | by-value | Active | echoed by inbound Heartbeat per FR-006; mismatch → session_testreqid_mismatch |

**State transitions** (transient flag on Active):

```text
Active (awaiting_resend=false)
  ↓ inbound MsgSeqNum > next_expected_inbound (FR-009)
  ↓ issue ResendRequest(2){[next_expected_inbound, inbound_msgseqnum-1]}
Active (awaiting_resend=true)
  ↓ all expected messages received OR SequenceReset-GapFill received
  ↓ next_expected_inbound advanced to gap-end+1
Active (awaiting_resend=false)
```

**Validation rules**: `policy_.schedule` non-empty (validated at SessionConfig-build time); `attempt_index_ <= policy_.max_attempts` (or 0=infinite); `heartbeat_timer_` and `test_request_timer_` are MUTUALLY EXCLUSIVE in steady state (exactly one armed when in Active).

**Allocator**: per-session PMR arena (consumed from SessionConfig). Construction-time only; no allocations on hot path per `[const §VIII.5]` / `[const §XV.1]`.

**Anchor**: D-1, D-2, D-3, D-4, D-5; `[FIX-SL §4.3]`, `[FIX-SL §4.5]`, `[FIX-SL §4.6]`.

---

## E-2 — ResendState (NEW)

**File**: `include/fixpp/session/resend_state.hpp` + `src/session/resend_state.cpp` (mostly header-only)

**Purpose**: Per-session state for the Resend sub-protocol. Tracks outstanding `ResendRequest` we issued + inbound replay we are receiving + outbound replay we are sending. Lives across the `awaiting_resend=true` window; reset on protocol exit.

**Fields**:

| Field | Type | Ownership | Lifetime | Notes |
|---|---|---|---|---|
| `outstanding_begin` | `std::uint32_t` | by-value | AwaitingResend | BeginSeqNo we sent in ResendRequest(2) |
| `outstanding_end` | `std::uint32_t` | by-value | AwaitingResend | EndSeqNo we sent; 0 means "infinity" per D-2 |
| `started_at` | `std::chrono::steady_clock::time_point` | by-value | AwaitingResend | populated on ResendRequest emit; consumed for recovery-elapsed metric |
| `inbound_filled_through` | `std::uint32_t` | by-value | AwaitingResend | high-water-mark of what we've received in the replay; advances as PossDup=Y or GapFill arrives |
| `outbound_replay_cursor` | `std::uint32_t` | by-value | AwaitingResend | current position in our outbound replay (if peer issued a counter-ResendRequest); not always populated |
| `inbound_held` | `std::pmr::vector<held_inbound_msg>` | owning (PMR-allocated) | AwaitingResend | inbound messages with `MsgSeqNum > next_expected_inbound` that arrived during AwaitingResend; held in strict-MsgSeqNum order until the gap closes. Empty in the default-constructed shape (no heap alloc until first push). Drained on `reset()`. Added 2026-05-28 per `/speckit-analyze` C1 resolution to back FR-009 SPEC-FIX. |

**`held_inbound_msg` sub-struct** (sibling type declared in `contracts/resend_state.hpp`):

| Field | Type | Notes |
|---|---|---|
| `msg_seqnum` | `std::uint32_t` | the inbound MsgSeqNum (drives strict-order replay when the gap closes) |
| `sending_time` | `std::chrono::system_clock::time_point` | the wire `SendingTime(52)` — preserved for FR-015 PossDupFlag dedup and any later `OrigSendingTime(122)` re-emit |
| `payload` | `std::pmr::vector<std::byte>` | the raw inbound frame bytes (deep-copied into held storage; the inbound `string_view` from the framer does not outlive AwaitingResend) |

**Validation rules**: `outstanding_begin <= outstanding_end OR outstanding_end == 0`; `inbound_filled_through <= outstanding_end OR outstanding_end == 0`; every `held_inbound_msg.msg_seqnum > next_expected_inbound` (callers MUST enforce — the queue is not self-validating).

**Allocator**: the struct itself is value-typed and embedded in `ReconnectFsm`; the `inbound_held` `std::pmr::vector` uses a PMR allocator threaded through from the Session's session-arena (typically `[2a §4.2]`-class PMR). Default-construction is alloc-free (empty vector). Push (`inbound_held.emplace_back(...)`) allocates from the PMR arena; this is COLD-PATH (only fires when inbound arrives during AwaitingResend, which is rare). The "no allocations on the FSM transition Active↔AwaitingResend" plan.md §Constraints invariant holds: the transition is just `awaiting_resend_ = true/false` + `ResendState::reset()` (which calls `inbound_held.clear()`, deallocating any held buffers back to the arena — the vector's storage may shrink but does not necessarily release to the arena, depending on PMR policy).

**Anchor**: D-2, D-3, D-4, D-5; FR-009..FR-015.

---

## E-3 — CompIdAuthorizationPolicy (NEW)

**File**: `include/fixpp/session/compid_authorization_policy.hpp` + `src/session/compid_authorization_policy.cpp`

**Purpose**: Operator-supplied allow-list of `{principal → {compid_set}}` bindings. Consulted at every inbound Logon (both initiator and acceptor halves per FR-024). Allow-list-only in v1.0 per D-9 / FR-023.

**Fields**:

| Field | Type | Ownership | Lifetime | Notes |
|---|---|---|---|---|
| `bindings_` | `std::pmr::unordered_map<std::pmr::string, std::pmr::flat_set<std::pmr::string>>` | by-value (PMR-owned) | SessionConfig lifetime | key = principal value (CN OR SAN-DNS OR SAN-URI OR SHA-256-fingerprint string); value = set of authorized CompIDs for that principal |
| `mr_` | `std::pmr::memory_resource*` | non-owning ref | SessionConfig lifetime | upstream allocator for the map + key/value strings |

**Public API** (per contracts/compid_authorization_policy.hpp):

```cpp
namespace fixpp::session {

struct bound_principal {
    std::string_view value;              // principal value (CN / SAN-DNS / SAN-URI / fingerprint)
    enum class source : std::uint8_t {
        CN,                              // peer_identity.cn
        SAN_DNS,                         // peer_identity.sans (DNS type)
        SAN_URI,                         // peer_identity.sans (URI type)
        SHA256_FINGERPRINT,              // peer_identity.sha256_fingerprint
    };
    source from;
};

class CompIdAuthorizationPolicy {
public:
    // Construct empty (= default-deny per D-9 / FR-023).
    CompIdAuthorizationPolicy() noexcept;
    explicit CompIdAuthorizationPolicy(std::pmr::memory_resource* mr) noexcept;

    // COPY-CONSTRUCTIBLE per 010 W-5 SessionConfig static_assert
    // (`include/fixpp/session/session_config.hpp:176-180`); the underlying
    // std::pmr::unordered_map<std::pmr::string, std::pmr::flat_set<std::pmr::string>>
    // storage already supports copy. SessionConfig is held BY VALUE by Session
    // per 010 FR-001 by-value membership; every SessionConfig field MUST be
    // copy-constructible.
    CompIdAuthorizationPolicy(CompIdAuthorizationPolicy const&);
    CompIdAuthorizationPolicy& operator=(CompIdAuthorizationPolicy const&);
    CompIdAuthorizationPolicy(CompIdAuthorizationPolicy&&) noexcept;
    CompIdAuthorizationPolicy& operator=(CompIdAuthorizationPolicy&&) noexcept;
    ~CompIdAuthorizationPolicy();

    // Add an allow-list binding. Throws at construction time on malformed inputs
    // (empty principal / empty compid; `[arch §5.3]` carve-out permits).
    void add_binding(std::string_view principal, std::string_view compid);

    // Authorise: extract principal from peer_identity per FR-022 canonical-fixed
    // order, look up in bindings_, return bound_principal on success OR
    // session_compid_unauthorized on miss / empty-policy / unmatched-compid.
    [[nodiscard]] expected_t<bound_principal>
    authorize(fixpp::tls::peer_identity const& pid,
              std::string_view asserted_compid) const noexcept;
};

}  // namespace fixpp::session
```

**Copy semantics**: copy is value semantics on the underlying `std::pmr::unordered_map<std::pmr::string, std::pmr::flat_set<std::pmr::string>>` (deep-copies the bindings using the destination's PMR memory_resource). PMR cost is acceptable at SessionConfig copy time (engine bootstrap / session-open path, not the hot path).

**Validation rules**: `add_binding(principal, compid)` rejects empty strings (throws `std::invalid_argument` per `[arch §5.3]` construction-time carve-out); `authorize(...)` is `noexcept` and returns `expected_t<bound_principal>` for runtime use.

**Allocator**: PMR (operator may supply a long-lived arena; default = monotonic upstream pulling from `std::pmr::get_default_resource()`).

**Anchor**: D-8, D-9, D-10; FR-019..FR-025; 010 W-5 SessionConfig static_assert.

---

## E-4 — SessionConfig extensions (EXTENDED)

**File**: `include/fixpp/session/session_config.hpp` (extends 010's shipped form)

**New fields** (4 fields appended per `[arch §5.6]` frozen-at-open carve-out — operator-set at SessionConfig-build time; immutable thereafter except via the explicit `reload_credentials` API for the factory-internal `cert_source` slot):

| Field | Type | Default | Notes |
|---|---|---|---|
| `reset_seqnum_policy` | `enum class reset_seqnum_policy : std::uint8_t { bilateral_strict, bilateral_lenient, unilateral }` | `bilateral_strict` | per D-6 / FR-017 / Clarifications Q1=A |
| `logout_disconnect_timeout_ms` | `std::uint32_t` | `2000` | per D-13 / FR-008 / Clarifications Q5=A |
| `compid_authorization_policy` | `CompIdAuthorizationPolicy` (value; COPY-CONSTRUCTIBLE per E-3 + 010 W-5) | default-constructed (empty allow-list = default-deny per D-9) | per FR-023 |
| `transport_factory_override` | `std::shared_ptr<fixpp::transport::TransportFactory>` (per 010 FR-001a precedent — see "Ownership reconciliation" below) | `nullptr` (engine substitutes `EngineConfig::default_transport_factory` at `Session::open`-time) | per 2h Appendix D §D.2 reservation (`include/fixpp/transport/transport_factory.hpp:156-164`); 2h reserved this field for "the post-012 session-Phase-4 spec" — i.e., this feature; the wiring lands HERE. Resolution rule: `resolved_factory = transport_factory_override.value_or(EngineConfig::default_transport_factory)`. ReconnectFsm holds a non-owning `TransportFactory*` raw pointer extracted from the shared_ptr (per E-1). |

**Ownership reconciliation — `unique_ptr` (2h) vs `shared_ptr` (010 W-5)**:

2h Appendix D §D.1+§D.2 declared the field as `std::unique_ptr<TransportFactory>` with the binding constraint "no shared factory across sessions"; 010 W-5 added `static_assert(std::is_copy_constructible_v<SessionConfig>)` (`include/fixpp/session/session_config.hpp:176-180`) — Session holds SessionConfig BY VALUE, requiring every field to be copy-constructible. `std::unique_ptr<T>` is move-only and BREAKS the static_assert.

013 reconciles via the 010 FR-001a precedent (the same pattern 010 applied for `store_factory: unique_ptr → shared_ptr` for identical reason): the storage type is `std::shared_ptr<TransportFactory>` for SessionConfig-copy semantics ONLY. The 2h binding constraint "no factory shared across Sessions" is preserved via a `/speckit-implement`-time hygiene assertion at `Session::open` — e.g., `assert(factory.use_count() <= /* engine-anchored copies known to caller */)`, or equivalent ownership-uniqueness check. The shared_ptr is NEVER used for genuine cross-Session factory sharing; it is a copy-semantics adapter only. 2h Appendix D §D.1 invariant ("Factory is unique_ptr per [arch §5.6] — no mid-session swap, no shared factory across sessions") is honoured behaviourally even though the storage type changed.

**Alternative considered (rejected)**: keep `std::unique_ptr<TransportFactory>` and amend 010 W-5 to allow move-only SessionConfig fields. Rejected because (a) 010 W-5's by-value `Session::cfg_` membership is a load-bearing invariant proven through a recurring saga (PR #82 / PR #84 / PR #85 all rely on it); (b) the SessionConfig copy semantics are operator-visible (operator may copy a SessionConfig to derive a related session config); (c) the FR-001a precedent (`store_factory`) is the exact same trade-off already accepted upstream.

**Validation rules**: `logout_disconnect_timeout_ms` MUST be > 0 (validated at SessionConfig-build time; `[arch §5.3]` permits construction-time throw); `compid_authorization_policy` MAY be empty (operator must opt-in to allowing Logons by declaring at least one binding); `transport_factory_override` MAY be `nullptr` (engine substitutes default factory).

**Anchor**: D-6, D-9, D-13, D-14; FR-008, FR-017, FR-023, FR-030; `[arch §5.6]` frozen-at-open; 2h Appendix D §D.1+§D.2 sign-off; 010 W-5 SessionConfig static_assert.

---

## E-5 — SessionEvent (NEW public variant union introduced by 013)

**File**: `include/fixpp/session/session_event.hpp` (NEW public header introduced by 013)

**Ground-truth**: 010 F-04 shipped `Session::fsm_visit_history() const noexcept -> std::span<const fsm_state>` (`include/fixpp/session/session.hpp:237-250`) — a fixed 16-entry `std::array<fsm_state, 16>` ring of `fsm_state` enum values. There is NO shipped `SessionEvent` type — `grep -rn 'SessionEvent\\b' include/ src/` returns zero hits. 013 introduces `SessionEvent` as a NEW public type; this is NOT an extension of 010's surface.

**Naming convention** (binding for `/speckit-implement`): prose uses bare names (`peer_identity_bound`, `compid_authorization_failed`, `tls_validation_failed`, `credentials_rotated`, `sequence_numbers_reset`) in spec.md FRs, plan.md Summary, and this data-model document; the shipped struct identifiers in `include/fixpp/session/session_event.hpp` carry the `session_event_` prefix (e.g., `session_event_tls_validation_failed`) per the 012 `error::transport_*` precedent + 013 `error::session_*` precedent (avoids ADL collisions with 010 `fsm_visit_*` style). The `using SessionEvent = std::variant<...>` declaration uses the prefixed names; quickstart `std::is_same_v<T, ...>` checks use the prefixed names (code identifiers). Prose-vs-code distinction is intentional and locked.

**5 initial alternatives** introduced by this feature (post-013 expansions append append-only):

```cpp
namespace fixpp::session {

struct peer_identity_bound {
    std::string_view cn;                             // [[clang::lifetimebound]] into peer_identity
    std::span<std::string_view const> sans;          // [[clang::lifetimebound]]
    std::array<std::byte, 32> sha256_fingerprint;    // by-value (cheap)
    std::string_view cipher;                         // [[clang::lifetimebound]]
    std::string_view bound_compid;                   // [[clang::lifetimebound]] into the inbound Logon
    bound_principal::source principal_source;        // which field bound
};

struct compid_authorization_failed {
    std::string_view cn;                             // [[clang::lifetimebound]]
    std::string_view asserted_compid;                // [[clang::lifetimebound]]
    std::span<std::string_view const> expected_compids;  // empty if no binding for principal
    bound_principal::source principal_source;        // CHK015 SPEC-FIXED — which cert field was extracted
};

struct tls_validation_failed {
    fixpp::core::error code;                          // precise master-enum variant per shipped
                                                      // include/fixpp/core/error.hpp:403-429
                                                      // (6 cells: tls_handshake_failed GROUPING +
                                                      //  tls_rsa_key_too_large / tls_cert_der_too_large /
                                                      //  tls_san_entries_exceeded / tls_pin_mismatch /
                                                      //  tls_load_cancelled). FR-026.
    std::string_view sub_reason;                      // [[clang::lifetimebound]] — 011's thread-local
                                                      // last_handshake_sub_reason() value at the time
                                                      // of verify_peer failure ("rsa_under_min" /
                                                      // "ecdsa_curve" / "sigalg_disallowed" /
                                                      // "chain_too_deep" / "x509_v1" / "expired" /
                                                      // "not_yet_valid" / "empty_chain" /
                                                      // "tls_pin_empty_at_open" / ...). Sub-reason
                                                      // discriminator within the tls_handshake_failed
                                                      // GROUPING; empty string when code is one of
                                                      // the 5 specific tls_* variants. FR-027.
    std::string_view peer_endpoint;                   // [[clang::lifetimebound]] — "host:port"
    std::string_view reason_string;                   // [[clang::lifetimebound]] — operator-readable summary
};

struct credentials_rotated {
    std::array<std::byte, 32> old_sha256;            // by-value
    std::array<std::byte, 32> new_sha256;            // by-value
};

struct sequence_numbers_reset {
    bool by_peer_request;                            // true = peer sent 141=Y; false = we sent 141=Y
};

// NEW 013-introduced public variant union:
using SessionEvent = std::variant<
    peer_identity_bound,
    compid_authorization_failed,
    tls_validation_failed,
    credentials_rotated,
    sequence_numbers_reset
>;

}  // namespace fixpp::session
```

**Sub-reason capture semantics**: `tls_validation_failed::sub_reason` is captured by COPY into a session-arena string at event-emit time (NOT by raw view into the thread-local storage from 011's `last_handshake_sub_reason()`). 011's thread-local is a static-storage string literal, but copying into the session arena preserves the value across thread-of-emit boundaries (the event might be observed on the operator's thread, not the verify_peer-calling thread). Per data-model E-5 lifetime rules, the consumer may further copy if it needs to outlive the event-emit synchronous context.

**Lifetime rules**: all `string_view` / `span` fields carry `[[clang::lifetimebound]]` discipline — they view into `peer_identity` material, inbound message bytes, or session-config / session-arena strings; consumer copies if it needs to outlive the event-emit synchronous context.

**Anchor**: D-10, D-15, D-16; FR-018, FR-020, FR-021, FR-026, FR-027, FR-032, FR-035; shipped `include/fixpp/tls/security_profile.hpp:121-144` (verify_peer signature + last_handshake_sub_reason); shipped `include/fixpp/core/error.hpp:403-429` (6-cell master-enum surface).

---

## E-6 — Session method extensions (EXTENDED)

**File**: `include/fixpp/session/session.hpp` (extends 005/009's shipped form)

**New methods**:

```cpp
namespace fixpp::session {

class Session {
public:
    // ... existing 005/009/010 methods ...
    // (NB: 010 F-04's fsm_visit_history() const noexcept -> std::span<const fsm_state>
    //  is the EXISTING shipped accessor — UNCHANGED by this feature; it observes
    //  FSM-state transitions and is distinct from recent_events() below.)

    // FR-030 — operator-facing forwarder. Delegates to the held
    // TransportFactory*::reload_credentials(new_source) which performs the
    // atomic store on the factory-internal cert_source_slot_:
    // std::atomic<std::shared_ptr<cert_source>>. Both initiator and acceptor
    // halves route through the SAME factory call per
    // [[feedback_half_restructure_symmetric_api]] — the factory IS the symmetric
    // authority. Returns expected_t<void>::ok() on swap-accepted; swap is O(1)
    // under no contention. Emits SessionEvent::credentials_rotated BEFORE the
    // next handshake on the rotated source (per D-12 / FR-032).
    [[nodiscard]] expected_t<void>
    reload_credentials(std::shared_ptr<fixpp::tls::cert_source> new_source) noexcept;

    // FR-008 / US1 AC5 — initiator-graceful Logout. Emits Logout(5), awaits peer
    // reply for `timeout` (default = SessionConfig::logout_disconnect_timeout_ms),
    // closes Transport, transitions to Disconnected. Surfaces
    // error::session_logout_timeout (slot 73, 005-era reused per F1/D1 2026-05-28) if elapsed before peer reply.
    [[nodiscard]] asio::awaitable<expected_t<void>>
    logout(std::chrono::milliseconds timeout) noexcept;

    // Convenience overload: uses SessionConfig::logout_disconnect_timeout_ms.
    [[nodiscard]] asio::awaitable<expected_t<void>>
    logout() noexcept;

    // FR-035 — NEW 013-introduced ring-buffer accessor for SessionEvent
    // observability. Returns a std::span view over the underlying fixed-capacity
    // ring of SessionEvent values (capacity ≤ 16, mirroring fsm_visit_history()
    // shape). DISTINCT from the 010 F-04 fsm_visit_history() accessor:
    // - fsm_visit_history() -> std::span<const fsm_state>  (FSM-state transitions)
    // - recent_events()      -> std::span<const SessionEvent>  (variant events)
    // Both accessors coexist; neither replaces the other. Membership-witness
    // semantics per 010 F-04 contract (NOT chronologically ordered; tests assert
    // via Contains / std::find).
    [[nodiscard]] std::span<const SessionEvent> recent_events() const noexcept;
};

}  // namespace fixpp::session
```

**Validation rules**: `reload_credentials(new_source)` rejects `new_source == nullptr` and returns `expected_t::unexpected(error::session_invalid_argument)` — slot 119 per contracts/session_errors.hpp (added 2026-05-28 per `/speckit-analyze` finding C3 resolution; renumbered from 121 to 119 per F1/D1 2026-05-28). No existing `session_invalid_*` slot fits the runtime-API-argument-validation semantic (66 is FIX-protocol Logon-shape; 76 is Session::open config-time; 77 is FSM-state-wrong-for-send). `logout(timeout)` validates `timeout > 0ms`.

**Anchor**: D-11, D-12, D-13; FR-008, FR-030..FR-033; FR-035.

---

## E-7 — TransportFactory::reload_credentials (EXTENDED, transport side — binding atomic-swap entry)

**File**: `include/fixpp/transport/transport_factory.hpp` + `src/transport/transport_factory.cpp` (extends 012's shipped form)

**New method on the abstract `TransportFactory` base + concrete `asio_tls_transport_factory` override**. By placing the method on the abstract base, BOTH initiator and acceptor halves route through the SAME factory call — eliminating the half-restructure trap by construction (the factory IS the symmetric authority per `[[feedback_half_restructure_symmetric_api]]`). The acceptor side does NOT need a `Listener::reload_credentials` method — the abstract `Listener` pure-virtual cap stays at 1 (`async_accept` only); the operator reaches the factory via the listener-built `TransportFactory*` handle.

```cpp
namespace fixpp::transport {

class TransportFactory {
public:
    virtual ~TransportFactory() = default;

    // EXISTING (012) — make() is unchanged.
    [[nodiscard]] virtual core::expected_t<std::unique_ptr<Transport>>
        make(asio::any_io_executor             exec,
             fixpp::tls::SslCtxConfig          ssl_cfg,
             std::pmr::memory_resource*        mr) noexcept = 0;

    // FR-030 / FR-033 / D-11 — atomic swap on the factory-internal cert_source
    // slot. Both initiator-side and acceptor-side rotation route through this
    // SAME call (symmetric authority per [[feedback_half_restructure_symmetric_api]]).
    // O(1), strand-free. Rejects nullptr -> error::session_invalid_argument (slot 119, renumbered from 121 per F1/D1 2026-05-28).
    [[nodiscard]] virtual core::expected_t<void>
        reload_credentials(std::shared_ptr<fixpp::tls::cert_source> new_source) noexcept = 0;
};

class asio_tls_transport_factory final : public TransportFactory {
public:
    // ... existing 012 methods (ctor, make, make_accepted) ...

    [[nodiscard]] core::expected_t<void>
        reload_credentials(std::shared_ptr<fixpp::tls::cert_source> new_source) noexcept override;

    // 013-introduced typed accessor for the FSM to snapshot the current
    // cert_source. Returns a strong-ref shared_ptr by value — the captured
    // copy keeps the cert_source alive past any subsequent reload_credentials
    // store. NEVER returns a raw pointer or weak_ptr per
    // [[feedback_weak_ptr_cache_needs_owning_context]] anti-pattern guard.
    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source>
        cert_source_snapshot() const noexcept;

private:
    // ... existing 012 fields (cfg_, ssl_cfg_, ssl_ctx_) ...

    // 013-introduced atomic slot — the binding atomic-swap target for FR-033.
    std::atomic<std::shared_ptr<fixpp::tls::cert_source>> cert_source_slot_;
};

}  // namespace fixpp::transport
```

**Pluggable-interface caps post-013**: `TransportFactory` pure-virtual count moves 1 → 2 (was just `make`; +1 for `reload_credentials`). Still well under `[const §XIV.2]` 5/5 cap. `Listener` stays at 1 (no acceptor-side reload method).

**Validation rules**: `reload_credentials(nullptr)` returns `error::session_invalid_argument` (slot 119 per contracts/session_errors.hpp; renumbered from 121 to 119 per F1/D1 2026-05-28; 4 new slots total 116..119).

**Anchor**: D-11, D-14; FR-030; `[[feedback_half_restructure_symmetric_api]]`; `[[feedback_weak_ptr_cache_needs_owning_context]]`; `include/fixpp/transport/transport_factory.hpp:52-76` (existing abstract base shape).

---

## §Error slot allocation — append to `include/fixpp/core/error.hpp`

4 new `error::session_*` variants at the contiguous block 116..119, appended after 012's `transport_*` block (which occupies 94..115 per shipped post-PR-#85 header — `transport_accept_cancelled = 115` per `[2h §6.6]:1199`). Slots 73 (`session_logout_timeout`) and 74 (`session_test_request_unanswered`) are REUSED from 005-era for FR-008 and FR-004 emissions per F1/D1 resolution 2026-05-28 — reference-engine sweep across QuickFIX-cpp / QuickFIX-J / Fix8 confirmed zero precedent for typed code-level discrimination of these timeout classes:

| Slot | Variant | FR | Notes |
|---|---|---|---|
| 73 | `session_logout_timeout` | FR-008 / US1 AC5 | REUSED from 005-era; Logout-reply window elapsed; per D-13; F1/D1 resolution 2026-05-28 |
| 74 | `session_test_request_unanswered` | FR-004 / US1 AC4 | REUSED from 005-era; inbound liveness window elapsed (2× HeartBtInt without inbound); F1/D1 resolution 2026-05-28 |
| 116 | `session_seqnum_reset_mismatch` | FR-017 / US1 AC7 | bilateral_strict mode; peer's Logon-response lacks 141=Y when ours had 141=Y; per D-7 |
| 117 | `session_compid_unauthorized` | FR-021 / US2 AC2 | binding-policy reject (unmatched principal OR principal→compid pair); per D-9 |
| 118 | `session_testreqid_mismatch` | FR-006 | inbound Heartbeat carries TestReqID(112) that doesn't match the most recent outbound TestRequest's TestReqID |
| 119 | `session_invalid_argument` | FR-033 | runtime nullptr rejection on `reload_credentials` (and future public-API runtime argument validation); added 2026-05-28 per `/speckit-analyze` C3 resolution — no existing `session_invalid_*` slot fits (66 FIX-Logon-shape; 76 config-time; 77 FSM-state); per D-11; renumbered from 121 to 119 per `/speckit-analyze` F1/D1 2026-05-28 |

**C-ABI coalescing** (owned by 2i, NOT 013): the 4 new slots (116..119) join the existing `FIXPP_ERR_SESSION` group at the C-ABI boundary. Slots 73 and 74 are already in the group. NO new C-ABI symbol; NO new gRPC RPC.

**Reconciliation rule** (carry-forward from 012): the `/speckit-implement`-time rewriter MUST cross-check the actual `include/fixpp/core/error.hpp` to confirm the boundary remains at 115 (no ±N drift from a 012 carry-forward waiver-close shipping an additional `transport_*` variant); future ±1 adjustment is reconciled at /implement-time without re-running Gate A. NEVER renumber existing slots.

**Anchor**: D-7, D-9, D-13; FR-008, FR-017, FR-021; `[const §X.4]` ABI append-only; Assumption A.8; F1/D1 resolution 2026-05-28 per `/speckit-analyze`.

---

## §State transition diagrams (for Gate A + checklist review)

### Reset-seqnum handshake matrix (FR-017 / D-6)

```text
                    Our outbound Logon
                    ┌────────────────────┬────────────────────┐
                    │  141=Y             │  141=N             │
┌───────────────────┼────────────────────┼────────────────────┤
│ bilateral_strict  │ peer ack 141=Y?    │ peer 141=Y? reject │
│                   │  yes → reset both  │   (we didn't send) │
│                   │  no  → mismatch    │ peer 141=N?        │
│                   │       (slot 116)   │   normal session   │
├───────────────────┼────────────────────┼────────────────────┤
│ bilateral_lenient │ peer ack 141=Y?    │ peer 141=Y?        │
│                   │  yes → reset both  │   auto-mirror; we  │
│                   │  no  → normal      │   reply with 141=Y │
│                   │       (no reset)   │   reset both       │
├───────────────────┼────────────────────┼────────────────────┤
│ unilateral        │ peer 141=Y?        │ peer 141=Y?        │
│                   │  yes/no: reset    │   reset both       │
│                   │  both anyway       │ peer 141=N?        │
│                   │                    │   normal session   │
└───────────────────┴────────────────────┴────────────────────┘
```

### Active ↔ AwaitingResend transient (FR-009 / D-1)

```text
        normal inbound (next_expected_inbound matches)
        ┌──────────────────────────────────────────┐
        │                                          │
        ▼                                          │
   ┌──────────────────────────────────────────┐    │
   │ Active                                   │────┘
   │  awaiting_resend=false                   │
   │  resend_state_ default                   │
   └────┬─────────────────────────────────────┘
        │ inbound MsgSeqNum > next_expected_inbound (FR-009)
        │ emit ResendRequest(2){next_expected_inbound..msgseqnum-1}
        │ populate resend_state_
        ▼
   ┌──────────────────────────────────────────┐
   │ Active                                   │
   │  awaiting_resend=TRUE                    │
   │  resend_state_ tracks outstanding range  │
   │  inbound application traffic above       │
   │   next_expected_inbound HELD             │
   │  outbound application traffic CONTINUES  │
   └────┬─────────────────────────────────────┘
        │ peer replays [B..E] (PossDup=Y or GapFill)
        │ next_expected_inbound advances
        │ gap closes
        ▼
   (back to Active, awaiting_resend=false)
```

### reload_credentials atomic swap (FR-033 / D-11)

```text
   Operator thread (any executor)              FSM thread (session strand)
   ┌─────────────────────────────────┐         ┌─────────────────────────────────────┐
   │ Session::reload_credentials(new)│         │ FSM driving an in-flight handshake: │
   │  ↓ delegates to                 │         │   shared_ptr<cert_source> snap =    │
   │ factory_->reload_credentials    │         │     factory_->cert_source_snapshot()│
   │  ↓ atomic store on              │         │   (CAPTURED BY VALUE COPY before    │
   │ factory.cert_source_slot_       │  ── OK ─▶│    the swap; strong-ref keeps OLD  │
   │  std::atomic<shared_ptr<cs>>    │         │    cert_source alive past store)    │
   │  .store(new)                    │         │   ssl_cfg = make_ssl_ctx_config(    │
   │  — O(1), strand-free            │         │     profile, snap, clock, …);       │
   │  ↓                              │         │   tr = factory_->make(exec,         │
   │ OLD shared_ptr ref drops from   │         │           std::move(ssl_cfg), mr);  │
   │ the slot; surviving captured    │         │   …handshake proceeds on OLD…       │
   │ copies keep cert_source alive   │         │                                     │
   │  ↓                              │         │ NEXT FSM drive_reconnect_attempt:   │
   │ return expected_t::ok()         │         │   shared_ptr<cert_source> snap2 =   │
   └─────────────────────────────────┘         │     factory_->cert_source_snapshot()│
                                                │     (NOW reads NEW from atomic)     │
                                                │   …emits credentials_rotated event… │
                                                │   make(...) builds Transport from   │
                                                │   NEW cert_source                   │
                                                └─────────────────────────────────────┘
```

**Strong-ref invariant** (closes the `[[feedback_weak_ptr_cache_needs_owning_context]]` axis):

Both the `make(...)` caller (the FSM) and any in-flight handshake CAPTURE a `std::shared_ptr<cert_source>` (NOT a raw pointer, NOT a `weak_ptr`) BY VALUE COPY before / during the call via `factory_->cert_source_snapshot()`. The factory's atomic slot `store(...)` releases the slot's strong ref; the captured copy keeps the OLD `cert_source` alive for the in-flight handshake's lifetime. NO raw `cert_source*` is captured anywhere on the FSM path — eliminates the 012 RC#B-class `[[feedback_weak_ptr_cache_needs_owning_context]]` trap (where a `weak_ptr` cache with no other strong owner becomes a no-cache and re-fires side effects). Documented as anti-pattern guard #6 in research.md §8.

**Atomic-swap point disambiguation** (FR-033 prose):

- The atomic STORE point is `TransportFactory::reload_credentials(new)` — executes synchronously when the operator calls.
- "In-flight handshake observes OLD" means: an FSM attempt that already called `cert_source_snapshot()` to build its `SslCtxConfig` for `make(...)` continues on OLD for that handshake's duration.
- "In-flight `make()`" is NOT a thing — `make(...)` is one-shot per call (it accepts `SslCtxConfig` by-value, so the binding is fixed at the call site).
- Worst-case rotation latency = one in-flight HANDSHAKE duration (~50–500 ms TLS 1.3 1-RTT) — measured from the operator's `reload_credentials` return to the moment all in-flight handshakes have either completed or been cancelled and the NEXT `drive_reconnect_attempt` calls `cert_source_snapshot()` to read NEW.

---

## §Validation summary

All E-1..E-7 entities + the 4 new error slots 116..119 (plus reused 005-era slots 73/74 per F1/D1 2026-05-28) cover the spec's FR-001..FR-038 and SC-001..SC-008 surfaces. Cross-walk:

- **FR-001..FR-008** (reconnect FSM cadence): E-1 ReconnectFsm; E-6 Session::logout
- **FR-009..FR-016** (recovery sub-protocol): E-1 + E-2; FR-016 catalogue flips covered in plan.md §Phase 2
- **FR-017..FR-018** (ResetSeqNumFlag handshake): E-4 reset_seqnum_policy + E-5 sequence_numbers_reset event + slot 116
- **FR-019..FR-025** (CompID↔TLS-identity binding): E-3 CompIdAuthorizationPolicy + E-4 field + E-5 peer_identity_bound + compid_authorization_failed events + slot 117
- **FR-004 / FR-008** (Heartbeat timeout / Logout timeout): reuse shipped slots 74 (session_test_request_unanswered) / 73 (session_logout_timeout) per F1/D1 2026-05-28
- **FR-026..FR-029** (TLS validation outcome → SessionEvent): E-5 tls_validation_failed event
- **FR-030..FR-034** (in-process reload_credentials): E-6 Session::reload_credentials (operator-facing forwarder) + E-7 TransportFactory::reload_credentials (binding atomic-swap entry; symmetric authority for initiator + acceptor halves) + E-5 credentials_rotated event
- **FR-035..FR-038** (cross-cutting symmetry + completeness): E-5 SessionEvent extension shape; FR-037 symmetry obligation tracked in plan.md test plan; FR-038 production-shaped exercise tracked in plan.md test plan.
- **SC-001..SC-008**: measurable outcomes covered by the test plan (10 named tests + fuzz + perf alloc-guard + 2 benches) per plan.md §Test Plan.
