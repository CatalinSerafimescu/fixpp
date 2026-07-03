# Internal contract — `store_then_emit` outbound-retain disposition (059)

**Scope.** This is an **internal** contract (a session-private coroutine's error disposition). It is **not** a public C++ API, not a C-ABI symbol, and not a wire behaviour. No `include/fix/c_api.h` change, no new `fixpp::core::error` enumerator, no new pluggable-interface method (Art. XIV §2 unchanged). It is documented here because it changes the **message-store contract's failure semantics on the send path**, which is a Gate-A trigger (Art. XVII §1).

## Contract

`asio::awaitable<expected_t<void>> Session::store_then_emit(seqnum_t stamped_seq, span<const byte> frame) noexcept`

**Pre-state:** `assign_outbound()` has already advanced the in-memory wire counter for `stamped_seq`. Store precedes transmit (I-3).

**Retain step (Step 1) post-conditions — NEW disposition:**

1. **Success** (`store()` returns a value): unchanged — proceed to transmit. Observable behaviour byte-identical to `main`.
2. **`store_cancelled` return** (any store): unchanged — absorb → proceed. Cancellation-class (shutdown drain); MUST NOT fail closed or reconcile (FR-005 parity with thrown `operation_aborted`). Checked **before** the persistence gate.
3. **Genuine failure return on a persistent store** (`!store_r && store_is_persistent_`, error ∈ {`store_io_failure`, `store_seqnum_out_of_order`, `store_capacity_exhausted`}): the coroutine MUST, before transmitting, return the **same shape** as the existing transport-write failure (`co_return dispatch_aborted`) — an error return with **no internal FSM transition**:
   - capture `err = store_r.error()` **first** (before the reconcile read, so a reconcile-time throw cannot pre-empt it),
   - best-effort reconcile the outbound wire counter to the durable value: `dk = co_await store_->next_seqnum(outbound, false)`; if `dk`, `co_await seqnum_mgr_.set_next_outbound(*dk)` (FR-007). Reconcile failure is non-fatal — the caller-owned disconnect still proceeds.
   - `co_return std::unexpected(err)` — surfacing the store's own error code (no new code),
   - and MUST NOT transmit `frame`. The `Disconnected` transition is **caller-owned** (not fired inside `store_then_emit`), exactly as it is for the transport-failure return today. (The hydration latch `hydrated_` is **not** touched — see research D4.)
4. **Genuine failure return on a volatile store** (`!store_r && !store_is_persistent_`): unchanged — log-then-proceed; transmit proceeds (L-008-2 stands).
5. **Thrown `operation_aborted`** (cancellation): unchanged — `co_return dispatch_aborted`.
6. **Other throw:** unchanged — absorbed.

**Caller contract (FR-006) — parity with the transport-failure return.** The persistent-fatal return is the same shape callers already handle on a transport-write failure, so no caller needs a new return-check beyond what already exists. Census of the 26 `store_then_emit` sites (`9305e69`):
- **Propagating, broad guard `if (!emit_r) → Disconnected` (9):** `:864 :1781 :1825 :2496 :2820 :3379 :3542 :4615 :4673` — fail closed unchanged.
- **Propagating, narrow guard `== dispatch_aborted` (1 — the residual):** `Session::send` (`:4046`, consuming `send_impl` `:4488`, the primary US1 app path). It misses the store class, so its guard is broadened to `dispatch_aborted || {store_io_failure, store_seqnum_out_of_order, store_capacity_exhausted}`, keeping app-veto (`app_do_not_send`/`app_payload_malformed`, `:4468-4470`) non-fatal. This is the one genuinely-new-in-059 call-site edit.
- **Swallow-and-continue (16):** `:2130 :2344 :2656 :2680 :2742 :2869 :2921 :2959 :2982 :3176 :3233 :3687 :4902 :4963 :5221 :5264` — each `(void)`s the return and continues **identically to today's transport-failure return**; a pre-existing disposition (documented, not re-engineered). The un-retained frame is never transmitted on any of them.
(All classified in the SC-006 census.)

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
