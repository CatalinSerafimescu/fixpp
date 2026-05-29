# Quickstart — Live Transport Wiring (014-transport-active-binding)

Audience: a fixpp contributor verifying the three realized behaviours + the carry-forwards. 014 wires the **initiator** live path; the public multi-session engine (continuous read-pump, connect-loop, registry, acceptor production path) is **015**, so the walkthroughs below drive the FSM/attempt level and feed inbound frames through the existing `Session::on_inbound_frame` seam (as all session tests do).

## Prereqs

```bash
cd research/G19-fix-fpml-iso20022/library
export FIXPP_TLS_FIXTURE_DIR="$PWD/tests/tls/fixtures"   # leaf_*.pem, ca.pem (+ 014's ed25519/ed448/multi_san)
# Build the session + transport tests on the debug preset (resource-gated; ask before building):
cmake --build build/linux-clang-debug --target fixpp_session_tests fixpp_transport_tests
```

## 1 — Initiator self-heals after a drop (US1)

A loopback-TLS acceptor + an initiator `Session` whose `ReconnectFsm` holds the real `TransportFactory`:

```
transport drops
  → drive_reconnect_attempt walks ReconnectPolicy.delay_for_attempt(n) (honouring cancellation)
  → make() → async_connect → async_handshake   (the 3 steps the 013 stub skipped)
  → handshake_result captured (peer_id owned by value)
  → authorize(peer_id, target_comp_id)          (see §2)
  → success: live transport bound to transport_send_, Logon re-driven → Active
```
Failing peer (unreachable / TLS failure / **off-list identity**) each consumes **one** attempt and retries to `max_attempts`, then `fsm_state::Disconnected` — no infinite retry (reason-agnostic per Clarifications Q1).

```bash
ctest --test-dir build/linux-clang-debug -R 'reconnect_live_happy_path|reconnect_backoff_cap|reconnect_cancel_mid_handshake' -V
```
`reconnect_cancel_mid_handshake` runs under ASan to witness **no leak / no orphaned socket** (SC-004).

## 2 — Live identity drives authorization (US2)

On the initiator live path the identity is the **real** `handshake_result.peer_id` (not the 013 fabricated/skip arm):

```
binding policy + on-list identity   → authorize() OK   → peer_identity_bound, session reaches Active
binding policy + off-list / absent  → fail closed      → session_compid_unauthorized
                                                          + compid_authorization_failed, NOT Active
```
The `logon_peer_identity_override` test seam still drives the off/on/absent cells (the acceptor-side live binding + seam removal land in 015; T-041 stays `implementing`).

```bash
ctest --test-dir build/linux-clang-debug -R 'live_identity_binding|compid_binding_seam' -V
```

## 3 — Observe genuine credential rotation (US3)

```cpp
session.reload_credentials(new_cert_source);   // stages the new source (013 control plane)
// ... next reconnect ...
// drive_reconnect_attempt emits, on the session strand, BEFORE make():
//   SessionEvent::credentials_rotated{ old_sha256, new_sha256 }   // REAL leaf fingerprints
```
A no-op rotation (new leaf == current) still emits with `old == new` (FR-011).

```bash
ctest --test-dir build/linux-clang-debug -R 'credentials_rotated_emit' -V
```

## 4 — Carry-forward witnesses (US4)

```bash
# FR-012 sigalg_disallowed cell (Ed25519/Ed448 fixture); FR-014 multi-SAN PMR-OOM depth:
ctest --test-dir build/linux-clang-debug -R 'tls_validation_failed_taxonomy|verify_peer_pmr_oom' -V
# FR-013a once-per-handshake counter (now over the LIVE fixture):
ctest --test-dir build/linux-clang-debug -R 'session_invariant_counter_witness' -V
# FR-013b handshake bench baseline:
cmake --build build/linux-clang-release --target bench_tls_handshake_loopback && \
  ./build/linux-clang-release/bin/bench_tls_handshake_loopback
# FR-016 seqnum-too-high code:
ctest --test-dir build/linux-clang-debug -R 'seqnum_manager_test' -V
```
FR-015 is a catalogue/scope re-label on `fuzz_transport_handshake` (no harness change).

## 5 — Suite-green claim (SC-007)

Per the 013 close-out lesson (`8e2d362`), the suite-green claim MUST come from the **unfiltered** Tier-1 ctest (or `-L sync`), never a name-scoped `-R` subset — a new include into an awaitable-corpus header can drag a `std::mutex` into the co_await closure that only the corpus/sync gate catches:

```bash
ctest --test-dir build/linux-clang-debug -L sync -V    # plus the full unfiltered run in /speckit-verify
```
