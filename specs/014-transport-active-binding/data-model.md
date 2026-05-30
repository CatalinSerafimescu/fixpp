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

**Before (013 stub, `src/session/reconnect_fsm.cpp:53-61`):**
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
2. **Rotation check** (E-3): `snap = factory_->cert_source_snapshot()` — read through the abstract `TransportFactory*` (the method is **promoted to a pure-virtual on the abstract base** by 014, since the FSM holds `TransportFactory*`, not the concrete factory; see E-3 / plan §XIV.2); if `snap != last_active_source_` emit `credentials_rotated` on the strand **before** building/`make`.
3. Build the per-attempt `SslCtxConfig ssl_cfg` from `snap`. **Hold `ssl_cfg` in the attempt scope** — it is consumed by BOTH `make()` (step 3) and `async_handshake(ssl_cfg)` (step 5), and `async_handshake` takes it by `const&` `[[clang::lifetimebound]]` (`tls_transport.hpp:116-118`), so `ssl_cfg` MUST outlive the `co_await` at step 5 — never pass a temporary (a dangling `SslCtxConfig` → UB/ASan).
4. `t = factory_->make(exec, ssl_cfg, mr)`; on `make` failure → count attempt, continue.
5. `co_await t->async_connect(ep)`; on failure → release `t`, count attempt, continue.
6. Reach the TLS specialization via the shipped discipline (`tls_transport.hpp:61-67`): `auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(t.get());` — exactly one `dynamic_cast<TlsTransport*>`, null-checked (`TlsTransport` inherits *virtually* from `Transport`, so `static_cast` down that edge is ill-formed; and the base `make()` returns a `Transport`, so the check must be runtime-recoverable). On `tls == nullptr` (non-TLS / cast-fail → cannot handshake) → release `t`, count attempt, continue. Else `co_await tls->async_handshake(ssl_cfg)`; on failure → release `t`, count attempt, continue. Capture `hr : handshake_result`. (`ssl_cfg` from step 3 is still in scope here — required by the lifetimebound arg.) **The FSM is the first code in the tree to reach the TLS specialization** (`drive_reconnect_attempt` is the only handshake-issue site today), so it *sets* this single-`dynamic_cast`-stored-once discipline rather than following an existing site.
7. **Authorize** (E-2): run the decision with `hr.peer_id`; on fail-closed → emit `session_event_compid_authorization_failed` + `session_compid_unauthorized`, release `t`, **count attempt, continue** (Q1 reason-agnostic). This does **NOT** drive the terminal `Disconnected` transition (unlike 013's open-Logon path at `session.cpp:1004-1005`/`:1799-1800`); only loop-exhaustion at the cap (below) transitions to terminal Disconnected. The inherited 013 *event/code shapes* are reused; the *FSM disposition* differs (reconnect path = retry-to-cap; open path = terminal).
8. **Success — cross-object handoff (`ReconnectFsm` → `Session`)**: on a successful attempt the FSM hands the live `std::unique_ptr<Transport>` + `handshake_result` + `bound_principal` to the owning `Session` via the **private** consumer entry point **`Session::install_reconnected_transport(unique_ptr<Transport>, handshake_result, bound_principal)`** (the named US1-AC1 deliverable site) — `reconnect_fsm_` is a direct value member of `Session` (`session.hpp:517`), so the FSM calls back into its owner directly (the same `Session`-injected channel pattern as the `credentials_rotated` callback). `install_reconnected_transport` rebinds `transport_send_` to the new transport, stores `hr.peer_id` for the authorize site (E-2), and re-enters `LogonSent`. The FSM owns none of `transport_send_`/`emit_event`/`record_state_transition_`/`seqnum_mgr_` — those are `Session` members, `reconnect_fsm.hpp:149-172`, hence the handoff. The three values cross the **private** `Session`-side surface only (defined in `session.cpp`, which already includes the heavy `tls_transport.hpp`/`compid_authorization_policy.hpp`); they NEVER appear in any `reconnect_fsm.hpp` public signature. (Any `credentials_rotated` for this attempt was already emitted at step 2 via the FSM's injected strand-bound callback, before `make()` — see below.) `drive_reconnect_attempt` `co_return`s `expected_t<void>{}` on success — its **public** signature is unchanged from the 013 stub (`asio::awaitable<expected_t<void>>`, `reconnect_fsm.hpp:83-84`); no new public type enters that header (preserving the `[const §XV.9]` header-minimalism: a `handshake_result`-carrying return would drag `tls_transport.hpp`→`tls/pinset.hpp`'s `std::shared_mutex` into the `<asio/awaitable.hpp>` closure — the `8e2d362` regression class).

At loop exhaustion → `fsm_state::Disconnected`. Cancellation at any `co_await` releases the partially-constructed `t` (RAII `unique_ptr`) with no leak (SC-004).

**Return shape (concrete):** `drive_reconnect_attempt`'s **public** return is **unchanged from the 013 stub** — `asio::awaitable<expected_t<void>>` (`reconnect_fsm.hpp:83-84`). There is **no `reconnect_outcome` public type**: the live `unique_ptr<Transport>` + `handshake_result` + `bound_principal` are NOT carried out through the return; they are handed to `Session` via the **private** `Session::install_reconnected_transport(transport, handshake_result, bound_principal)` (step 8) — a direct call from `reconnect_fsm_` into its owning `Session`. This keeps the public `reconnect_fsm.hpp` signature minimal and prevents a heavy `handshake_result`-carrying type from entering the header's `<asio/awaitable.hpp>` closure (`[const §XV.9]`, the `8e2d362` regression class — see step 8). The three values cross only the private `Session`-side surface in `session.cpp` (which already includes the heavy headers). Should an internal helper struct be convenient for the `install_reconnected_transport(...)` arguments, it is a `Session`-private/`session/`-internal detail (not in any `reconnect_fsm.hpp` signature), covered by Gate A — but the args may equally pass positionally.

**Where the `credentials_rotated` emit + rotation-detect state live (reconciles with E-3):** the FSM has no `SessionEvent` emit path (`recent_events()` + the session strand are `Session` members). FR-009 requires the emit to land **before `make()`** (step 2), i.e. it cannot wait for the success handoff at step 8. **Decision: the rotation-detect state `{last_active_source_, last_active_fp_}` lives on the FSM (it owns the per-attempt `cert_source_snapshot()` read, E-3), and the FSM is given an explicit strand-bound emit callback `std::function<void(session_event_credentials_rotated)> emit_credentials_rotated_` injected by `Session` at FSM construction.** When the FSM detects a staged rotation at step 2 it computes `{old,new}` fingerprints and invokes the callback right there — before `make()` — so the `Session` performs the actual strand-bound emit at the FR-009-mandated point. (The success handoff at step 8 carries only the transport + identity, NOT the rotation event.) This keeps the strand/emit on `Session` (which owns them) while the detect/compute stays on the FSM (which owns the snapshot read), and preserves FR-009's emit-before-`make()` ordering exactly.

**Invariants:** (I-1) no production path mints+discards a transport (FR-005); (I-2) at most one in-flight attempt's transport at a time (spec Edge: reconnect exclusivity) — guaranteed by the single coroutine; (I-3) `load_credentials()` fires exactly once per handshake (FR-013a witness).

---

## E-2 — Authorization identity-source switch (US2)

**Call sites (unchanged location):** the **three-way guard** at `src/session/session.cpp:953-1008` (acceptor `LogonReceived`) and `:1757-1803` (initiator `LogonSent` peer-Logon-ack) — byte-for-byte symmetric (the RC#A fail-closed-footgun fix landed in 013 Gate-B). **Today's shipped gate (verified, NOT a fail-OPEN skip):**
```cpp
const bool is_mtls = (security_profile.k == mtls_ca || == mtls_pinned);
if (cfg_.logon_peer_identity_override.has_value()) {     // session_config.hpp:224 (test seam)
    auto auth_r = cfg_.compid_authorization_policy.authorize(*cfg_.logon_peer_identity_override,
                                                             cfg_.target_comp_id);
    // ... fail-closed (emit compid_authorization_failed + Disconnected) / peer_identity_bound ...
} else if (is_mtls) {
    // (2) mTLS + no peer_identity available → FAIL CLOSED:
    //     emit session_event_compid_authorization_failed + record_state_transition_(Disconnected) + co_return.
    //     (session.cpp:992-1006 acceptor / :1790-1801 initiator)
} // else (3) non-mTLS → skip (genuinely permissive; no client cert → CompID binding inapplicable)
```
So under mTLS-without-override 013 **already fails CLOSED** — there is no fail-OPEN hole on the mTLS path. The live initiator path simply has no identity *source* today, so the mTLS gate can only ever take arm (2) and fail-close; it cannot admit a legitimate peer.

**After (014), identity-source precedence on the *initiator* path** (014 inserts a *source* ahead of the seam; it does NOT introduce arm (2)):
1. **live** `handshake_result.peer_id` captured by `drive_reconnect_attempt` (when an mTLS handshake occurred) → drives `authorize()`, making the already-fail-CLOSED mTLS gate *operable* with a real identity (admit on-list; arm (2) fail-close becomes off-list/absent rather than unconditional).
2. else **`logon_peer_identity_override`** (test seam) → retained for binding-logic tests (off/on/absent). Removed in 015.
3. else the inherited 013 arms unchanged: `else if (is_mtls)` → fail CLOSED (arm 2); non-mTLS → permissive skip (arm 3).

The dependence on the test seam / absence of a live identity is removed **on the live path only**. Fail-closed/permissive semantics, extraction order, and `session_event_compid_authorization_failed`/`session_compid_unauthorized` shapes inherited unchanged (FR-007); the reconnect-path *FSM disposition* is retry-to-cap (E-1 step 7), not the open path's terminal Disconnected. **Acceptor live binding stays via the seam → 015** (Q2); T-041 stays `implementing`.

**Invariant:** (I-4) on the initiator live path, the identity reaching `authorize()` is `handshake_result.peer_id` — no fabricated stand-in (SC-003).

---

## E-3 — Rotation-detect FSM state (US3)

New owning members on `ReconnectFsm`:
```cpp
std::shared_ptr<fixpp::tls::cert_source> last_active_source_{};  // strong-ref: must outlive to fingerprint on next rotation
std::array<std::byte, 32>                last_active_fp_{};      // SHA-256 of last_active_source_'s leaf DER
```
Per E-1 step 2: `snap = factory_->cert_source_snapshot()` — read through the abstract `TransportFactory*` the FSM holds (`reconnect_fsm.hpp:152`). Since the shipped `cert_source_snapshot()` is **concrete-only** (`asio_tls_transport_factory`, `transport_factory.hpp:167-168`) and is **absent from the abstract base** (which has exactly 2 pure-virtuals, `:54-98`), 014 **promotes it to a pure-virtual on the abstract `TransportFactory`** so this call compiles through the abstract pointer (2/5 → 3/5, under the §XIV.2 cap; plan §XIV.2 + contracts C4). The asio impl already has the override (`transport_factory.cpp:193`); the test-local `TransportFactory` subclasses each add a trivial override returning their held source (mandatory once the base is pure-virtual). This also corrects the inherited-false 013 doc-comment at `reconnect_fsm.hpp:20-23`/`:76-82`, which already describes this call as if the base had the method. If `last_active_source_ == nullptr` → initial load, set members, **no** event. Else if `snap != last_active_source_` → rotation staged: compute `new_fp` (OpenSSL SHA-256 over `snap`'s `local_credentials` leaf DER), invoke the injected strand-bound `emit_credentials_rotated_` callback with `{old=last_active_fp_, new=new_fp}` **before** `make()` (E-1: the FSM owns the snapshot read, `Session` owns the strand-bound emit), then set `last_active_source_=snap; last_active_fp_=new_fp`. No-op rotation (`new_fp==last_active_fp_`) still emits (FR-011).

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

No **new** fixture for FR-013 — the happy-path/counter/bench use the **existing** `tests/tls/fixtures/leaf_rsa2048.pem` (+ `ca.pem`), which the `bench_tls_handshake_loopback` scaffold already hard-codes at `bench_tls_handshake_loopback.cpp:44` (CMake target `bench_tls_handshake_loopback`, `bench/transport/CMakeLists.txt:22`). The ed25519/ed448/multi_san fixtures above are for the FR-012/FR-014 *rejection* cells only, not the bench. No fixture for FR-015 (catalogue label only).

---

### Traceability

| FR | Entity / Record | Site |
|----|-----------------|------|
| FR-001..005 | Reconnect attempt / E-1 | `reconnect_fsm.cpp` |
| FR-006..008 | Authenticated peer identity / E-2 | `session.cpp:953-1008` (acceptor) / `:1757-1803` (initiator) |
| FR-009..011 | Credential-rotation observation / E-3 | `reconnect_fsm.cpp` + `session_event.hpp` |
| FR-012 | E-5 | `test_tls_validation_failed_taxonomy.cpp` + Ed25519/Ed448 fixture |
| FR-013 | E-1 I-3 | `test_session_invariant_counter_witness.cpp` + `bench_tls_handshake_loopback.cpp` |
| FR-014 | E-5 | `test_verify_peer_pmr_oom.cpp` + multi-SAN fixture |
| FR-015 | — | `fuzz_transport_handshake.cpp` catalogue label |
| FR-016 | E-4 | `error.hpp` + `seqnum_manager.{cpp}` + `seqnum_manager_test.cpp` |
