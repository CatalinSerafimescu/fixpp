# Contract: transport endpoint configuration (GAP-002 / FR-004..005a)

Added to `include/fix/c_api/session.h`. Primitive `(host, port)` only — NO transport handle, NO
`fixpp_endpoint_t` PoD (those stay deferred per `[2i §7.8]`). Recorded as a **LOCAL Gate-A deviation**
(clarify A) — `[2i §2]` non-goal #7 / §7.8 NOT reopened.

```c
/**
 * fixpp_session_config_set_tcp_endpoint — set the session's TCP endpoint.
 *
 * For an INITIATOR: the peer endpoint to connect to. For an ACCEPTOR: the bind endpoint (port 0 =
 * OS-assigned; read back via fixpp_session_acceptor_bound_endpoint). Sets SessionConfig::reconnect_endpoint
 * = {host, port}; the C-ABI layer ALSO sets the internal SessionConfig::transport_send placeholder (the
 * no-op the engine's auto-derived plaintext factory replaces at connect/accept) — the consumer never
 * references transport_send. Call BEFORE fixpp_session_open (open copies the config by value).
 *
 * Reentrancy: SINGLE_THREAD (a session-config setter, per the fixpp_session_config_set_* family).
 *
 * @return FIXPP_ERR_OK; FIXPP_ERR_NULL_HANDLE (NULL cfg or host); FIXPP_ERR_CAPI_CONFIG_INVALID
 *         (empty / unusable host).
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_config_set_tcp_endpoint(
    fixpp_session_config_t* cfg, const char* host, uint16_t port);

/**
 * fixpp_session_acceptor_bound_endpoint — read back an acceptor's OS-assigned bound port.
 *
 * Writes *port_out = Engine::acceptor_bound_endpoint(id).port. For the port-0 ephemeral-bind workflow:
 * a session not yet bound yields *port_out = 0 with FIXPP_ERR_OK (poll until non-zero). The bind host is
 * consumer-known (no host out-param — avoids a host-string lifetime contract).
 *
 * Reentrancy: THREAD_SAFE (atomic read of the engine snapshot, like fixpp_session_is_established).
 *
 * @return FIXPP_ERR_OK (+ *port_out, possibly 0 if not yet bound); FIXPP_ERR_NULL_HANDLE (NULL session
 *         or port_out); FIXPP_ERR_INVALID_HANDLE (destroyed session).
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_acceptor_bound_endpoint(
    fixpp_session_t* session, uint16_t* port_out);
```

**Implementation notes:**
- `set_tcp_endpoint` (src/capi/config.cpp): NULL-check cfg/host → `cfg->cfg.reconnect_endpoint =
  fixpp::transport::Endpoint{host, port};` + `cfg->cfg.transport_send = [](std::span<const std::byte>){};`
  (the L-050-5 seam's two lines, now public). Validate host non-empty → else `CAPI_CONFIG_INVALID`.
  **Do NOT** set `reset_seqnum_policy_field` here — that is the job of the separate
  `fixpp_session_config_set_reset_seqnum_policy` setter (FR-005b / E-4, now **shipped** — see below);
  `set_tcp_endpoint` writes only the endpoint + the `transport_send` placeholder.
- `acceptor_bound_endpoint` (src/capi/session.cpp): NULL-check → validate `session->valid` / engine live
  → `*port_out = session->engine->state_->engine_->acceptor_bound_endpoint(session->id).port;` (the
  `state_->engine_` reach `fixpp_session_is_established` already uses; seam pattern
  `capi_loopback_support.hpp:82-86`). Steady-state thunk — exception escape → abort (FR-011).

**Witness (US2 / SC-001):** part of the pure-public-header **two-engine** live round-trip
(`tests/capi/public_roundtrip_test.cpp`): the acceptor (engine A) is bound via port-0 +
`acceptor_bound_endpoint` readback, then the initiator (engine B) `set_tcp_endpoint("127.0.0.1", <read-back
port>)`, the pair establishes, an app message is exchanged. NULL cfg/host → `NULL_HANDLE`.

---

## Reset-seqnum policy setter (FR-005b / E-4) — co-located session.h config setter

Pinned at Gate-A r1 (user decision) so the SC-001 ABI surface is deterministic (7 symbols). A session-config
setter in the same `session.h` family (NOT transport — kept here as a co-located config setter).

```c
typedef enum fixpp_reset_seqnum_policy {
    FIXPP_RESET_SEQNUM_BILATERAL_STRICT  = 0,  /* production default */
    FIXPP_RESET_SEQNUM_BILATERAL_LENIENT = 1,
    FIXPP_RESET_SEQNUM_UNILATERAL        = 2
} fixpp_reset_seqnum_policy;

/**
 * fixpp_session_config_set_reset_seqnum_policy — set the session's seqnum-reset policy.
 * Writes SessionConfig::reset_seqnum_policy_field. Enumerators mirror the C++
 * enum class reset_seqnum_policy : uint8_t (session_config.hpp:92-95, same values).
 * Reentrancy: SINGLE_THREAD (a session-config setter).
 * @return FIXPP_ERR_OK; FIXPP_ERR_NULL_HANDLE (NULL cfg); FIXPP_ERR_CAPI_CONFIG_INVALID (out-of-range enum).
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_config_set_reset_seqnum_policy(
    fixpp_session_config_t* cfg, fixpp_reset_seqnum_policy kind);
```

**Implementation notes (src/capi/config.cpp):** NULL-check cfg → range-check `kind` ∈ {0,1,2} (else
`CAPI_CONFIG_INVALID`) → `cfg->cfg.reset_seqnum_policy_field = static_cast<reset_seqnum_policy>(kind);`.
**Witness:** SC-001 sets the policy explicitly through this setter (no L-050-5 internal cast); plus a NULL
cfg → `NULL_HANDLE` and an out-of-range enum → `CAPI_CONFIG_INVALID` unit check.
