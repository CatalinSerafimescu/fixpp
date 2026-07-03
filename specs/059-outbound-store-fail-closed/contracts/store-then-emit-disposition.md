# Internal contract — `store_then_emit` outbound-retain disposition (059)

**Scope.** This is an **internal** contract (a session-private coroutine's error disposition). It is **not** a public C++ API, not a C-ABI symbol, and not a wire behaviour. No `include/fix/c_api.h` change, no new `fixpp::core::error` enumerator, no new pluggable-interface method (Art. XIV §2 unchanged). It is documented here because it changes the **message-store contract's failure semantics on the send path**, which is a Gate-A trigger (Art. XVII §1).

## Contract

`asio::awaitable<expected_t<void>> Session::store_then_emit(seqnum_t stamped_seq, span<const byte> frame) noexcept`

**Pre-state:** `assign_outbound()` has already advanced the in-memory wire counter for `stamped_seq`. Store precedes transmit (I-3).

**Retain step (Step 1) post-conditions — NEW disposition:**

1. **Success** (`store()` returns a value): unchanged — proceed to transmit. Observable behaviour byte-identical to `main`.
2. **`store_cancelled` return** (any store): unchanged — absorb → proceed. Cancellation-class (shutdown drain); MUST NOT fail closed or reconcile (FR-005 parity with thrown `operation_aborted`). Checked **before** the persistence gate.
3. **Genuine failure return on a persistent store** (`!store_r && store_is_persistent_`, error ∈ {`store_io_failure`, `store_seqnum_out_of_order`, `store_capacity_exhausted`}): the coroutine MUST, before transmitting:
   - best-effort reconcile the outbound wire counter to the durable value: `dk = co_await store_->next_seqnum(outbound, false)`; if `dk`, `co_await seqnum_mgr_.set_next_outbound(*dk)` (FR-007). Reconcile failure is non-fatal — the disconnect still proceeds.
   - transition the session FSM to `Disconnected` (`record_state_transition_`),
   - `co_return std::unexpected(store_r.error())` — surfacing the store's own error code (no new code),
   - and MUST NOT transmit `frame`. (The hydration latch `hydrated_` is **not** touched — see research D4.)
4. **Genuine failure return on a volatile store** (`!store_r && !store_is_persistent_`): unchanged — log-then-proceed; transmit proceeds (L-008-2 stands).
5. **Thrown `operation_aborted`** (cancellation): unchanged — `co_return dispatch_aborted`.
6. **Other throw:** unchanged — absorbed.

**Caller contract (FR-006).** Callers are NOT required to inspect the return to obtain fail-closed: post-condition (2) is owned by the callee, so a persistent failure disconnects the session regardless. Callers that DO propagate `!emit_r` (the majority) additionally short-circuit their own flow (already the case). Best-effort callers (`(void)co_await store_then_emit(...)` at `session.cpp:2742`, `:3233`) MUST be verified to tolerate a mid-flow `Disconnected` — i.e. they must not resume `Active` or emit further frames assuming success after the swallowed call. (Verified in the SC-006 census.)

## Error channel (existing codes reused)

| Store failure | Returned error (persistent path) |
|---|---|
| durable I/O fault (`raw_pwrite_all`/`fdatasync` fails) | `store_io_failure` |
| out-of-order (`seq != next`) | `store_seqnum_out_of_order` |
| bounded capacity exhausted | `store_capacity_exhausted` |
| store mutex cancelled/drained | `store_cancelled` — **excluded** from fail-closed (cancellation-class, shutdown-only); keeps today's absorb→proceed |

All pre-existing `fixpp::core::error` enumerators — **no additions** (A-4 / Art. X untouched).

## Test-only surface (FileStore fault-injection seam)

A `FIXPP_TEST_HOOKS`-gated arm in `file_store.cpp` forces one `store()` `pwrite` to fail (`io_ok=false` → `store_io_failure`). Declaration gated in `file_store.hpp`; the arm/call site is compiled unconditionally (library links without `FIXPP_TEST_HOOKS`, matching the existing `g_force_abort_after_reset_lambda` idiom). **Not reachable from production callers.**
