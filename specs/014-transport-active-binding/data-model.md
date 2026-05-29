# Phase 1 — Data Model: 014-transport-active-binding

014 introduces **no new public type**. The "entities" are the spec's Key Entities, expressed as the *state and value flow* through already-shipped types. The design records (E-1..E-5) capture the realization decisions that `/speckit-tasks` will turn into tasks.

---

## Entities (from spec § Key Entities)

### Reconnect attempt
One governed cycle inside `ReconnectFsm::drive_reconnect_attempt`: read `cert_source` snapshot → `TransportFactory::make` → `Transport::async_connect` → `TlsTransport::async_handshake` → capture `handshake_result` → `authorize(handshake_result.peer_id, asserted_compid)`. Bounded by `ReconnectPolicy{schedule, max_attempts, delay_for_attempt(n)+jitter}` (`reconnect_policy.hpp:30`). **Every** failure cause — connect, handshake, **or authorization** (Q1, reason-agnostic) — counts as exactly one attempt; at the cap → terminal-disconnected (`fsm_state::Disconnected`). Not a stored struct; it is the coroutine's per-iteration scope + the loop counter.

### Authenticated peer identity
`fixpp::tls::peer_identity` carried **by value** inside `handshake_result.peer_id` (`tls_transport.hpp:52-53`, owning SAN/CN material). Extraction order CN→SAN-DNS→SAN-URI→SHA-256 is the 011/013 contract (unchanged). It is the input to `CompIdAuthorizationPolicy::authorize`; 014 changes only its **source** (013 stub/override → live handshake) on the initiator path.

### Credential-rotation observation
Our **own** `cert_source` end-entity SHA-256, old and new, as `std::array<std::byte,32>` in `SessionEvent::credentials_rotated{old_sha256, new_sha256}` (`session_event.hpp:102-105`). Emitted on the session strand at the rotated-source `drive_reconnect_attempt`, before `make()`.

---

## E-1 — `drive_reconnect_attempt` realization (US1)

**Before (013 stub, `src/session/reconnect_fsm.cpp:124-131`):**
```cpp
if (factory_ != nullptr) {
    auto exec = co_await asio::this_coro::executor;
    fixpp::tls::SslCtxConfig ssl_cfg{};
    auto t = factory_->make(exec, std::move(ssl_cfg), nullptr);
    (void)t;                       // ← minted then DISCARDED
}
co_return expected_t<void>{};
```
Not called by any production path.

**After (014):** the bounded retry loop, per attempt `n` in `[0, max_attempts)`:
1. `co_await` the backoff `delay_for_attempt(n)` (skip for `n==0`), honouring `cancellation_state` (`total` → abort, `transport_*_cancelled`).
2. **Rotation check** (E-3): `snap = factory_->cert_source_snapshot()`; if `snap != last_active_source_` emit `credentials_rotated` on the strand **before** building/`make`.
3. Build `SslCtxConfig` from `snap`; `t = factory_->make(exec, cfg, mr)`; on `make` failure → count attempt, continue.
4. `co_await t->async_connect(ep)`; on failure → release `t`, count attempt, continue.
5. `co_await static_cast<TlsTransport&>(*t).async_handshake()`; on failure → release `t`, count attempt, continue. Capture `hr : handshake_result`.
6. **Authorize** (E-2): run the decision with `hr.peer_id`; on fail-closed → emit `compid_authorization_failed` + `session_compid_unauthorized`, release `t`, **count attempt, continue** (Q1 reason-agnostic).
7. **Success**: hand `t` + `hr` back to the owning `Session` (rebind `transport_send_`, store `hr.peer_id`, re-drive Logon → Active); `co_return` success.

At loop exhaustion → `fsm_state::Disconnected`. Cancellation at any `co_await` releases the partially-constructed `t` (RAII `unique_ptr`) with no leak (SC-004). Return type may refine from `expected_t<void>` to carry the outcome to `Session` (session-internal; not frozen ABI; covered by Gate A).

**Invariants:** (I-1) no production path mints+discards a transport (FR-005); (I-2) at most one in-flight attempt's transport at a time (spec Edge: reconnect exclusivity) — guaranteed by the single coroutine; (I-3) `load_credentials()` fires exactly once per handshake (FR-013a witness).

---

## E-2 — Authorization identity-source switch (US2)

**Call sites (unchanged location):** `src/session/session.cpp:958` (acceptor `LogonReceived`) and `:1758` (initiator `LogonSent` peer-Logon-ack). **Today's gate:**
```cpp
if (cfg_.logon_peer_identity_override.has_value()) {     // session_config.hpp:224 (test seam)
    auto auth_r = cfg_.compid_authorization_policy.authorize(*cfg_.logon_peer_identity_override,
                                                             cfg_.target_comp_id);
    // ... fail-closed / peer_identity_bound ...
} else {
    // SKIP authorize  →  fail-OPEN on the live path
}
```

**After (014), identity-source precedence on the *initiator* path:**
1. **live** `handshake_result.peer_id` captured by `drive_reconnect_attempt` (when an mTLS handshake occurred) → drives `authorize()`. Closes the initiator-path fail-OPEN hole.
2. else **`logon_peer_identity_override`** (test seam) → retained for binding-logic tests (off/on/absent). Removed in 015.
3. else inherited 013 permissive/skip semantics (non-mTLS / no binding policy) — unchanged.

The fabricated/skip arm is removed **on the live path only**. Fail-closed/permissive semantics, extraction order, and `compid_authorization_failed`/`session_compid_unauthorized` shapes inherited unchanged (FR-007). **Acceptor live binding stays via the seam → 015** (Q2); T-041 stays `implementing`.

**Invariant:** (I-4) on the initiator live path, the identity reaching `authorize()` is `handshake_result.peer_id` — no fabricated stand-in (SC-003).

---

## E-3 — Rotation-detect FSM state (US3)

New owning members on `ReconnectFsm`:
```cpp
std::shared_ptr<fixpp::tls::cert_source> last_active_source_{};  // strong-ref: must outlive to fingerprint on next rotation
std::array<std::byte, 32>                last_active_fp_{};      // SHA-256 of last_active_source_'s leaf DER
```
Per E-1 step 2: `snap = factory_->cert_source_snapshot()`. If `last_active_source_ == nullptr` → initial load, set members, **no** event. Else if `snap != last_active_source_` → rotation staged: compute `new_fp` (OpenSSL SHA-256 over `snap`'s `local_credentials` leaf DER), emit `credentials_rotated{old=last_active_fp_, new=new_fp}` on the strand **before** `make()`, then set `last_active_source_=snap; last_active_fp_=new_fp`. No-op rotation (`new_fp==last_active_fp_`) still emits (FR-011).

**Invariants:** (I-5) exactly one `credentials_rotated` per staged rotation, before `make()` (FR-009); (I-6) fingerprints are the REAL leaf SHA-256, never the 013 all-zero stub (FR-010); (I-7) strong-ref ownership of `last_active_source_` per `[[feedback_weak_ptr_cache_needs_owning_context]]`.

---

## E-4 — Error slot append (FR-016)

`include/fixpp/core/error.hpp`: append `session_seqnum_too_high = 120` after `session_invalid_argument = 119`. `src/session/seqnum_manager.cpp:71-78` returns it instead of `session_test_request_unanswered` (74). Slot **70** = permanent hole; slot **74** keeps its real meaning. `tests/session/seqnum_manager_test.cpp:145-150` assertion flips. **Zero behavioural change** — the 3 callers (`session.cpp:904`/`:1261`/`:1703`) discard the code.

**Invariant:** (I-8) append-only; boundary-at-119 cross-checked before assigning 120; no renumber (`[const §X.4]`). C++ enum only — no C-ABI symbol.

---

## E-5 — Carry-forward witness fixtures (US4)

New test fixtures under `tests/tls/fixtures/` (addressed via `FIXPP_TLS_FIXTURE_DIR`):
- `leaf_ed25519.pem` / `leaf_ed448.pem` — unknown-`EVP_PKEY` leaf → `[const §XII.3]` allow-list rejects → `sub_reason="sigalg_disallowed"` (FR-012).
- `leaf_multi_san.pem` — ≥2 SAN-DNS entries → `throw_on_nth_resource` reaches mid/tail allocation sites (FR-014).

No fixture for FR-013 (re-targets the existing counter witness at the live happy-path fixture) or FR-015 (catalogue label only).

---

### Traceability

| FR | Entity / Record | Site |
|----|-----------------|------|
| FR-001..005 | Reconnect attempt / E-1 | `reconnect_fsm.cpp` |
| FR-006..008 | Authenticated peer identity / E-2 | `session.cpp:958/1758` |
| FR-009..011 | Credential-rotation observation / E-3 | `reconnect_fsm.cpp` + `session_event.hpp` |
| FR-012 | E-5 | `test_tls_validation_failed_taxonomy.cpp` + Ed25519/Ed448 fixture |
| FR-013 | E-1 I-3 | `test_session_invariant_counter_witness.cpp` + `bench_tls_handshake_loopback.cpp` |
| FR-014 | E-5 | `test_verify_peer_pmr_oom.cpp` + multi-SAN fixture |
| FR-015 | — | `fuzz_transport_handshake.cpp` catalogue label |
| FR-016 | E-4 | `error.hpp` + `seqnum_manager.{cpp}` + `seqnum_manager_test.cpp` |
