# Contracts — realized behaviour (014-transport-active-binding)

014 changes **no public type signature** (the sole signature delta — `error::session_seqnum_too_high = 120` — is in `error_slots.hpp`). What follows are the **behavioural contracts** for the three 013 stubs that 014 makes real. They bind `/speckit-tasks` and `/speckit-implement`; a divergence between these and the implemented behaviour is a Gate-B finding. On any conflict with a cited shipped header, the **shipped header wins**.

---

## C1 — `ReconnectFsm::drive_reconnect_attempt` (US1 / FR-001..005)

**Surface.** Session-internal driver in the public header `include/fixpp/session/reconnect_fsm.hpp`. Its signature MAY refine from the 013 stub (`asio::awaitable<expected_t<void>>`) to carry the reconnect outcome to the owning `Session`; this is a 013-stub realization within `session/`, **not** a frozen ABI, and is covered by Gate A.

**Contract.**
- Walks `ReconnectPolicy` (`schedule`, `max_attempts`, `delay_for_attempt(n)` + jitter) — consumes it; does **not** redefine it (FR-002).
- Per attempt: `cert_source_snapshot` → (rotation emit, C3) → `make` → `async_connect` → `TlsTransport::async_handshake` → `authorize` (C2). The 013 mint-then-`(void)t`-discard at `reconnect_fsm.cpp:124-131` is **removed**; **no** production path may mint a transport and discard it (FR-005).
- **Every** failure cause — `make`, connect, handshake, **or authorization** — consumes exactly one attempt and retries per the schedule to the cap, then `fsm_state::Disconnected` (FR-003; reason-agnostic per Clarifications Q1). No infinite retry.
- **Cancellation** (`cancellation_type::total`): aborts the in-flight attempt promptly and releases the partially-constructed transport (RAII) — no leak, no orphaned socket (FR-004). The coroutine MUST `enable_total_cancellation()` (else a `total` stop silently hangs — `[[feedback_asio_cospawn_total_cancellation_default]]`).
- On success: hands the authorized live transport + `handshake_result` back to `Session` (rebind `transport_send_`, store `peer_id`, re-drive Logon to Active). The continuous inbound read-pump / public connect-loop / registry are **015** (Clarifications Q2); 014 proves resume via the loopback fixture + the `on_inbound_frame` seam.

---

## C2 — Live identity → `authorize()` (US2 / FR-006..008)

**Surface.** No signature change. `CompIdAuthorizationPolicy::authorize(peer_identity const&, std::string_view) const noexcept -> expected_t<bound_principal>` (`compid_authorization_policy.cpp:296`) called at `session.cpp:958` (acceptor) / `:1758` (initiator).

**Contract.**
- On the **initiator live path**, the identity passed to `authorize()` is the real `handshake_result.peer_id` (FR-006) — there is no fabricated stand-in and no fail-OPEN skip on that path.
- Under a binding policy, an absent or off-allow-list identity fails closed: disconnect, refuse Active, emit `session_compid_unauthorized` + `compid_authorization_failed` (FR-007). Such a failed attempt counts as one reconnect attempt and retries to the cap (C1, Q1).
- Fail-closed/permissive semantics, the CN→SAN-DNS→SAN-URI→SHA-256 extraction order, and the event/code shapes are **inherited unchanged** from 013 (FR-019/020/022) — 014 changes only the identity **source**.
- The `logon_peer_identity_override` test seam (`session_config.hpp:224`) **remains** for binding-logic tests (off/on/absent). Acceptor-side live binding + seam removal + full T-041 closure = **015** (FR-008; T-041 stays `implementing`).

---

## C3 — `credentials_rotated` emission (US3 / FR-009..011)

**Surface.** No type change — emits the already-defined `SessionEvent::credentials_rotated{old_sha256, new_sha256}` (`session_event.hpp:102-105`, `std::array<std::byte,32>`).

**Contract.**
- After `reload_credentials` has staged a new `cert_source`, emit **exactly one** `credentials_rotated` on the **session strand** at the next `drive_reconnect_attempt`, **before** the new `cert_source_snapshot()` is passed to `make()` (FR-009).
- `old_sha256`/`new_sha256` are the **real** SHA-256 end-entity (leaf) fingerprints of the old/new `cert_source`, raw 32-byte arrays computed from the loaded leaf DER — replacing 013's all-zero stub (FR-010).
- A no-op rotation (`old_sha256 == new_sha256`) is **not** suppressed — it still emits (FR-011).
- Rotation detect is FSM-held (`last_active_source_` strong-ref + `last_active_fp_`); the first-ever load is not a rotation (no event). No new `cert_source`/`TransportFactory` pure-virtual (`[const §XIV.2]` caps untouched).

---

### Verification anchors

| Contract | Witness test | Success criterion |
|----------|--------------|-------------------|
| C1 happy-path | `test_reconnect_live_happy_path.cpp` | SC-001 |
| C1 cap / reason-agnostic | `test_reconnect_backoff_cap.cpp` | SC-002 |
| C1 cancel/release | `test_reconnect_cancel_mid_handshake.cpp` | SC-004 (ASan no-leak) |
| C2 live source | `test_live_identity_binding.cpp` | SC-003 |
| C2 fail-closed seam | `test_compid_binding_seam.cpp` | SC-003 |
| C3 real fingerprints | `test_credentials_rotated_emit.cpp` | SC-005 |
| (US4) carry-forwards | FR-012..016 witnesses | SC-006 |
| (suite) unfiltered ctest | `-L sync` / no `-R` subset | SC-007 (`[[feedback_awaitable_header_mutex_include_edge]]`) |
