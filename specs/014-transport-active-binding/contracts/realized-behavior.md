# Contracts — realized behaviour (014-transport-active-binding)

014 introduces **no new public type**, but it has **two public-surface deltas**: (1) the error-enum append `error::session_seqnum_too_high = 120` (`error_slots.hpp`), and (2) the promotion of `TransportFactory::cert_source_snapshot()` to a pure-virtual on the abstract base (C4). What follows are the **behavioural contracts** for the three 013 stubs that 014 makes real, plus C4 (the abstract-factory promotion). They bind `/speckit-tasks` and `/speckit-implement`; a divergence between these and the implemented behaviour is a Gate-B finding. On any conflict with a cited shipped header, the **shipped header wins**.

---

## C1 — `ReconnectFsm::drive_reconnect_attempt` (US1 / FR-001..005)

**Surface.** Session-internal driver in the public header `include/fixpp/session/reconnect_fsm.hpp`. Its **public signature is unchanged from the 013 stub** — `asio::awaitable<expected_t<void>>` (`reconnect_fsm.hpp:83-84`); 014 realizes the *body*, not the signature. There is **no new `reconnect_outcome` public type**: the live transport + `handshake_result` + `bound_principal` are NOT returned out of the header; they are delivered to the owning `Session` through the **private** `Session::install_reconnected_transport(...)` handoff (below). Defining a `handshake_result`-carrying return type in this public header would drag `tls_transport.hpp`→`tls/pinset.hpp`'s `std::shared_mutex` into the `<asio/awaitable.hpp>` closure consumed by `session.hpp` — the `8e2d362` `[const §XV.9]` regression class the bundle polices (C4); keeping the return `expected_t<void>` avoids re-arming it through the return type.

**Contract.**
- Walks `ReconnectPolicy` (`schedule`, `max_attempts`, `delay_for_attempt(n)` + jitter) — consumes it; does **not** redefine it (FR-002).
- Per attempt: `cert_source_snapshot` → (rotation emit, C3) → build per-attempt `SslCtxConfig ssl_cfg` → `make(exec, ssl_cfg, mr)` → `async_connect` → reach the TLS specialization via **exactly one `dynamic_cast<fixpp::transport::TlsTransport*>(t.get())`** with a null-check (`tls_transport.hpp:61-67`; `TlsTransport` inherits virtually from `Transport`, so `static_cast` down that edge is ill-formed and `make()` returns the base `Transport` — the check is runtime-recoverable: `nullptr`/non-TLS → count the attempt and continue) → `tls->async_handshake(ssl_cfg)` → `authorize` (C2). `async_handshake` takes `ssl_cfg` by `const&` `[[clang::lifetimebound]]` (`tls_transport.hpp:116-118`), so `ssl_cfg` MUST be held in the attempt scope across both `make()` and `async_handshake()` — never a temporary (dangling → UB/ASan). The FSM is the **first** handshake-issue site in the tree, so it sets the single-`dynamic_cast` discipline. The 013 mint-then-`(void)t`-discard at `reconnect_fsm.cpp:53-61` is **removed**; **no** production path may mint a transport and discard it (FR-005).
- **Every** failure cause — `make`, connect, handshake, **or authorization** — consumes exactly one attempt and retries per the schedule to the cap, then `fsm_state::Disconnected` (FR-003; reason-agnostic per Clarifications Q1). No infinite retry. On the reconnect path an authorize failure does **NOT** drive terminal Disconnected (unlike 013's open-Logon path); only loop-exhaustion at the cap does — see C2.
- **Cancellation** (`cancellation_type::total`): aborts the in-flight attempt promptly and releases the partially-constructed transport (RAII) — no leak, no orphaned socket (FR-004). The coroutine MUST `enable_total_cancellation()` (else a `total` stop silently hangs — `[[feedback_asio_cospawn_total_cancellation_default]]`).
- On success: `co_return`s `expected_t<void>{}` (the unchanged public return) and hands the authorized live transport + `handshake_result` + `bound_principal` to `Session` through the **private** `Session`-side consumer — **`Session::install_reconnected_transport(unique_ptr<Transport>, handshake_result, bound_principal)`** (the US1-AC1 deliverable site). Because `reconnect_fsm_` is a direct value member of `Session` (`session.hpp:517`), the FSM calls this private method on its owner directly (the same `Session`-injected channel pattern as the `credentials_rotated` callback, C3); the three values cross only the private `Session` surface in `session.cpp` — never a `reconnect_fsm.hpp` public signature. `install_reconnected_transport` rebinds `transport_send_`, stores `peer_id` for the authorize site (C2), and re-enters `LogonSent` (re-drive Logon to Active). The FSM owns none of `transport_send_`/`emit_event`/`record_state_transition_`/`seqnum_mgr_` (those are `Session` members, `reconnect_fsm.hpp:149-172`), hence the cross-object handoff. The continuous inbound read-pump / public connect-loop / registry are **015** (Clarifications Q2); 014 proves resume via the loopback fixture + the `on_inbound_frame` seam.

---

## C2 — Live identity → `authorize()` (US2 / FR-006..008)

**Surface.** No signature change. `CompIdAuthorizationPolicy::authorize(peer_identity const&, std::string_view) const noexcept -> expected_t<bound_principal>` (`compid_authorization_policy.cpp:296`) called inside the three-way guard at `session.cpp:953-1008` (acceptor) / `:1757-1803` (initiator).

**Contract.**
- **Baseline (shipped 013, re-verified):** the guard is three-way — (1) `logon_peer_identity_override` present → `authorize()`; (2) `else if (is_mtls)` → **fail CLOSED** (emit `compid_authorization_failed` + Disconnected, `:992-1006`/`:1790-1801`); (3) non-mTLS → skip (permissive). So 013 **already fails CLOSED under mTLS-without-override** — there is no fail-OPEN hole on the mTLS path. 014 does NOT introduce fail-CLOSED from scratch; it changes the identity **source**.
- On the **initiator live path**, the identity passed to `authorize()` is the real `handshake_result.peer_id` (FR-006) — no test-seam/fabricated source remains on that path, making the already-fail-CLOSED mTLS gate *operable* with a live identity (admit on-list; arm (2) fail-closes the off-list/absent case rather than firing unconditionally for want of any identity).
- Under a binding policy, an absent or off-allow-list identity fails closed: refuse Active, emit `session_compid_unauthorized` + `compid_authorization_failed` (FR-007). On the reconnect path such a failure counts as one reconnect attempt and retries to the cap (C1, Q1); it does **NOT** drive the terminal `Disconnected` transition that 013's open-Logon path does (`session.cpp:1004-1005`/`:1799-1800`) — only loop-exhaustion at the cap is terminal. The inherited 013 *event/code shapes* are reused; the *FSM disposition* differs between the open path (terminal) and the reconnect path (retry-to-cap).
- Fail-closed/permissive semantics, the CN→SAN-DNS→SAN-URI→SHA-256 extraction order, and the event/code shapes are **inherited unchanged** from 013 (FR-019/020/022) — 014 changes only the identity **source** (and the reconnect-path disposition above).
- The `logon_peer_identity_override` test seam (`session_config.hpp:224`) **remains** for binding-logic tests (off/on/absent). Acceptor-side live binding + seam removal + full T-041 closure = **015** (FR-008; T-041 stays `implementing`).

---

## C3 — `credentials_rotated` emission (US3 / FR-009..011)

**Surface.** No type change — emits the already-defined `SessionEvent::credentials_rotated{old_sha256, new_sha256}` (`session_event.hpp:102-105`, `std::array<std::byte,32>`).

**Contract.**
- After `reload_credentials` has staged a new `cert_source`, emit **exactly one** `credentials_rotated` on the **session strand** at the next `drive_reconnect_attempt`, **before** the new `cert_source_snapshot()` is passed to `make()` (FR-009).
- `old_sha256`/`new_sha256` are the **real** SHA-256 end-entity (leaf) fingerprints of the old/new `cert_source`, raw 32-byte arrays computed from the loaded leaf DER — replacing 013's all-zero stub (FR-010).
- A no-op rotation (`old_sha256 == new_sha256`) is **not** suppressed — it still emits (FR-011).
- Rotation detect is FSM-held (`last_active_source_` strong-ref + `last_active_fp_`); the first-ever load is not a rotation (no event). The FSM reads the snapshot through the abstract `TransportFactory::cert_source_snapshot()` — promoted to a pure-virtual by 014 (see C4). The strand-bound emit is performed by `Session` via an injected `emit_credentials_rotated_` callback the FSM invokes at the detect site (before `make()`), since the FSM owns no `SessionEvent` emit path. No new `cert_source` pure-virtual.

---

## C4 — `TransportFactory::cert_source_snapshot()` promotion (contract delta)

**Surface delta.** `cert_source_snapshot() const noexcept -> std::shared_ptr<fixpp::tls::cert_source>` ships **concrete-only** on `asio_tls_transport_factory` (`transport_factory.hpp:167-168`, body `transport_factory.cpp:193`); the abstract `TransportFactory` (`:54-98`) has exactly **2** pure-virtuals (`make` `:74`, `reload_credentials` `:95`) and the FSM holds the abstract `TransportFactory*` (`reconnect_fsm.hpp:152`). The FSM's documented rotation read therefore does **not** compile through the base today, and the 013 header prose at `reconnect_fsm.hpp:20-23`/`:76-82` already describes the call as if the base had it.

**Contract.** 014 **promotes `cert_source_snapshot()` to a pure-virtual on the abstract `TransportFactory`**: count **2/5 → 3/5**, still under the `[const §XIV.2]` cap of 5. This is no NEW concept (the production concrete impl already has it; the asio override needs no body change). Every **test-local `TransportFactory` subclass** (e.g. in `tests/session/test_reconnect_happy_path.cpp`, `tests/session/test_reload_credentials_in_flight.cpp`) gains a trivial `cert_source_snapshot()` override returning its held source (mandatory once the base method is pure-virtual, or they won't compile). It is an **explicit, Gate-A-blessed widening** of 012's frozen `TransportFactory` contract (re-emission discipline: owned here, not smuggled). The FSM-header forward-declare of `TransportFactory` (`reconnect_fsm.hpp:40-48`) is **preserved** — the call stays in `reconnect_fsm.cpp`; the header is NOT `#include`d (it would drag `tls/pinset.hpp`'s `std::shared_mutex` into the awaitable closure, the `8e2d362` `[const §XV.9]` regression class). The inherited-false 013 doc-comments are corrected in the same pass.

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
