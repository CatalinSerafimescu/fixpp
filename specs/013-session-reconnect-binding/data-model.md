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
| `factory_` | `std::shared_ptr<TransportFactory>` | shared (engine + session) | session | consumed from 012; held by-shared-ptr so reload_credentials can atomically swap the underlying cert_source slot without invalidating the factory pointer |
| `heartbeat_timer_` | `asio::steady_timer` | by-value | Active | armed on Active entry; rearmed on every outbound; cancelled on Disconnected entry |
| `test_request_timer_` | `asio::steady_timer` | by-value | Active | armed when inbound liveness window > 1.2× HeartBtInt; cancelled on inbound traffic |
| `logout_timer_` | `asio::steady_timer` | by-value | LogoutSent | armed on Session::logout() call; fires session_logout_disconnect_timeout |
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
| `outstanding_begin_` | `std::uint32_t` | by-value | AwaitingResend | BeginSeqNo we sent in ResendRequest(2) |
| `outstanding_end_` | `std::uint32_t` | by-value | AwaitingResend | EndSeqNo we sent; 0 means "infinity" per D-2 |
| `started_at_` | `std::chrono::steady_clock::time_point` | by-value | AwaitingResend | populated on ResendRequest emit; consumed for recovery-elapsed metric |
| `inbound_filled_through_` | `std::uint32_t` | by-value | AwaitingResend | high-water-mark of what we've received in the replay; advances as PossDup=Y or GapFill arrives |
| `outbound_replay_cursor_` | `std::uint32_t` | by-value | AwaitingResend | current position in our outbound replay (if peer issued a counter-ResendRequest); not always populated |

**Validation rules**: `outstanding_begin_ <= outstanding_end_ OR outstanding_end_ == 0`; `inbound_filled_through_ <= outstanding_end_ OR outstanding_end_ == 0`.

**Allocator**: none (POD-like; embedded in `ReconnectFsm`).

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

**Validation rules**: `add_binding(principal, compid)` rejects empty strings (throws `std::invalid_argument` per `[arch §5.3]` construction-time carve-out); `authorize(...)` is `noexcept` and returns `expected_t<bound_principal>` for runtime use.

**Allocator**: PMR (operator may supply a long-lived arena; default = monotonic upstream pulling from `std::pmr::get_default_resource()`).

**Anchor**: D-8, D-9, D-10; FR-019..FR-025.

---

## E-4 — SessionConfig extensions (EXTENDED)

**File**: `include/fixpp/session/session_config.hpp` (extends 010's shipped form)

**New fields** (appended per `[arch §5.6]` frozen-at-open carve-out — operator-set at SessionConfig-build time; immutable thereafter except via the explicit `reload_credentials` API for `cert_source` which lives elsewhere):

| Field | Type | Default | Notes |
|---|---|---|---|
| `reset_seqnum_policy` | `enum class reset_seqnum_policy : std::uint8_t { bilateral_strict, bilateral_lenient, unilateral }` | `bilateral_strict` | per D-6 / FR-017 / Clarifications Q1=A |
| `logout_disconnect_timeout_ms` | `std::uint32_t` | `2000` | per D-13 / FR-008 / Clarifications Q5=A |
| `compid_authorization_policy` | `CompIdAuthorizationPolicy` (value) | default-constructed (empty allow-list = default-deny per D-9) | per FR-023 |

**Validation rules**: `logout_disconnect_timeout_ms` MUST be > 0 (validated at SessionConfig-build time; `[arch §5.3]` permits construction-time throw); `compid_authorization_policy` MAY be empty (operator must opt-in to allowing Logons by declaring at least one binding).

**Anchor**: D-6, D-9, D-13; FR-008, FR-017, FR-023; `[arch §5.6]` frozen-at-open.

---

## E-5 — SessionEvent variant extensions (EXTENDED)

**File**: `include/fixpp/session/session_event.hpp` (extends 010 F-04's shipped variant union)

**5 new variants** appended to the existing 010 SessionEvent variant set (per FR-035 — NO new event channel; the existing ring-buffer accessor surfaces all):

```cpp
namespace fixpp::session {

// (existing 010 SessionEvent variants surveyed at /speckit-implement-time)

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
};

struct tls_validation_failed {
    fixpp::tls::tls_verify_error variant;            // precise [2g §6.6]:986-1004 variant per FR-026
    std::string_view peer_endpoint;                  // [[clang::lifetimebound]] — "host:port"
    std::string_view reason_string;                  // [[clang::lifetimebound]] — operator-readable summary
};

struct credentials_rotated {
    std::array<std::byte, 32> old_sha256;            // by-value
    std::array<std::byte, 32> new_sha256;            // by-value
};

struct sequence_numbers_reset {
    bool by_peer_request;                            // true = peer sent 141=Y; false = we sent 141=Y
};

// SessionEvent is the existing variant union (010 F-04):
//   std::variant< ... existing 010 variants ...,
//                 peer_identity_bound,
//                 compid_authorization_failed,
//                 tls_validation_failed,
//                 credentials_rotated,
//                 sequence_numbers_reset >;

}  // namespace fixpp::session
```

**Lifetime rules**: all `string_view` / `span` fields carry `[[clang::lifetimebound]]` discipline — they view into `peer_identity` material, inbound message bytes, or session-config strings; consumer copies if it needs to outlive the event-emit synchronous context.

**Anchor**: D-10, D-15, D-16; FR-018, FR-020, FR-021, FR-026, FR-027, FR-032, FR-035.

---

## E-6 — Session method extensions (EXTENDED)

**File**: `include/fixpp/session/session.hpp` (extends 005/009's shipped form)

**New methods**:

```cpp
namespace fixpp::session {

class Session {
public:
    // ... existing 005/009/010 methods ...

    // FR-030 / FR-031 / FR-032 / FR-033 — atomic swap at transport_factory::make(...)
    // entry; in-flight handshake observes OLD; NEXT handshake observes NEW.
    // Returns expected_t<void>::ok() on swap-accepted; swap is O(1) under no
    // contention. Emits SessionEvent::credentials_rotated BEFORE next handshake.
    [[nodiscard]] expected_t<void>
    reload_credentials(std::shared_ptr<fixpp::tls::cert_source> new_source) noexcept;

    // FR-008 / US1 AC5 — initiator-graceful Logout. Emits Logout(5), awaits peer
    // reply for `timeout` (default = SessionConfig::logout_disconnect_timeout_ms),
    // closes Transport, transitions to Disconnected. Surfaces
    // error::session_logout_disconnect_timeout if elapsed before peer reply.
    [[nodiscard]] asio::awaitable<expected_t<void>>
    logout(std::chrono::milliseconds timeout) noexcept;

    // Convenience overload: uses SessionConfig::logout_disconnect_timeout_ms.
    [[nodiscard]] asio::awaitable<expected_t<void>>
    logout() noexcept;
};

}  // namespace fixpp::session
```

**Validation rules**: `reload_credentials(new_source)` rejects `new_source == nullptr` (returns `expected_t::unexpected(error::session_invalid_argument)` — slot allocation TBD at /speckit-tasks-time, NOT in 013's 5-new-slot budget; uses existing `session_invalid_*` family if present). `logout(timeout)` validates `timeout > 0ms`.

**Anchor**: D-11, D-12, D-13; FR-008, FR-030..FR-033; FR-035.

---

## E-7 — asio_listener method extensions (EXTENDED, transport side)

**File**: `include/fixpp/transport/listener.hpp` + `src/transport/asio_listener.cpp` (extends 012's shipped form)

**New method on `asio_listener` (CONCRETE, NOT pure-virtual on `Listener` per the 012 `cancel()` precedent)**:

```cpp
namespace fixpp::transport {

class asio_listener final : public Listener {
public:
    // ... existing 012 methods ...

    // FR-030 acceptor half — symmetric with Session::reload_credentials per
    // [[feedback_half_restructure_symmetric_api]]. Atomic swap at
    // transport_factory::make(...) entry for the accept-adoption path.
    // In-flight accept observes OLD; NEXT accept observes NEW.
    [[nodiscard]] expected_t<void>
    reload_credentials(std::shared_ptr<fixpp::tls::cert_source> new_source) noexcept;
};

}  // namespace fixpp::transport
```

**Validation rules**: same as E-6 — rejects nullptr.

**Anchor**: D-11; FR-030; `[[feedback_half_restructure_symmetric_api]]`.

---

## §Error slot allocation — append to `include/fixpp/core/error.hpp`

5 new `error::session_*` variants at the contiguous block 116..120, appended after 012's `transport_*` block (which occupies 94..115 per shipped post-PR-#85 header — `transport_accept_cancelled = 115` per `[2h §6.6]:1199`):

| Slot | Variant | FR | Notes |
|---|---|---|---|
| 116 | `session_seqnum_reset_mismatch` | FR-017 / US1 AC7 | bilateral_strict mode; peer's Logon-response lacks 141=Y when ours had 141=Y; per D-7 |
| 117 | `session_compid_unauthorized` | FR-021 / US2 AC2 | binding-policy reject (unmatched principal OR principal→compid pair); per D-9 |
| 118 | `session_logout_disconnect_timeout` | FR-008 / US1 AC5 | Logout-reply window elapsed; per D-13 |
| 119 | `session_heartbeat_timeout` | FR-004 / US1 AC4 | inbound liveness window elapsed (2× HeartBtInt without inbound) |
| 120 | `session_testreqid_mismatch` | FR-006 | inbound Heartbeat carries TestReqID(112) that doesn't match the most recent outbound TestRequest's TestReqID |

**C-ABI coalescing** (owned by 2i, NOT 013): these 5 slots join the existing `FIXPP_ERR_SESSION` group at the C-ABI boundary. NO new C-ABI symbol; NO new gRPC RPC.

**Reconciliation rule** (carry-forward from 012): the `/speckit-implement`-time rewriter MUST cross-check the actual `include/fixpp/core/error.hpp` to confirm the boundary remains at 115 (no ±N drift from a 012 carry-forward waiver-close shipping an additional `transport_*` variant); future ±1 adjustment is reconciled at /implement-time without re-running Gate A. NEVER renumber existing slots.

**Anchor**: D-7, D-9, D-13; FR-008, FR-017, FR-021; `[const §X.2]` ABI append-only; Assumption A.8.

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
   ┌─────────────────────────────────┐         ┌─────────────────────────────────┐
   │ Session::reload_credentials(new)│         │ in-flight handshake               │
   │  ↓                              │         │  consumes OLD cert_source       │
   │ std::atomic<shared_ptr>::store  │         │  (transport_factory::make(...)  │
   │   (factory_->cert_source_slot,  │  ── OK ─▶│   returned before swap)          │
   │    new) — O(1), strand-free     │         │  ...completes...                 │
   │  ↓                              │         │                                  │
   │ return expected_t::ok()         │         │ NEXT handshake:                  │
   └─────────────────────────────────┘         │  transport_factory::make(...)    │
                                                │  reads atomic; sees NEW          │
                                                │  emit credentials_rotated event  │
                                                └─────────────────────────────────┘
```

---

## §Validation summary

All E-1..E-7 entities + the 5 new error slots cover the spec's FR-001..FR-038 and SC-001..SC-008 surfaces. Cross-walk:

- **FR-001..FR-008** (reconnect FSM cadence): E-1 ReconnectFsm; E-6 Session::logout
- **FR-009..FR-016** (recovery sub-protocol): E-1 + E-2; FR-016 catalogue flips covered in plan.md §Phase 2
- **FR-017..FR-018** (ResetSeqNumFlag handshake): E-4 reset_seqnum_policy + E-5 sequence_numbers_reset event + slot 116
- **FR-019..FR-025** (CompID↔TLS-identity binding): E-3 CompIdAuthorizationPolicy + E-4 field + E-5 peer_identity_bound + compid_authorization_failed events + slot 117
- **FR-026..FR-029** (TLS validation outcome → SessionEvent): E-5 tls_validation_failed event
- **FR-030..FR-034** (in-process reload_credentials): E-6 Session::reload_credentials + E-7 asio_listener::reload_credentials + E-5 credentials_rotated event
- **FR-035..FR-038** (cross-cutting symmetry + completeness): E-5 SessionEvent extension shape; FR-037 symmetry obligation tracked in plan.md test plan; FR-038 production-shaped exercise tracked in plan.md test plan.
- **SC-001..SC-008**: measurable outcomes covered by the test plan (10 named tests + fuzz + perf alloc-guard + 2 benches) per plan.md §Test Plan.
