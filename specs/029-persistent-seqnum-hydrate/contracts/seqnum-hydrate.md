# Contract: seqnum hydrate + inbound persistence

## C1 — `SeqnumManager::hydrate(seqnum_t next_inbound, seqnum_t next_outbound)`

```cpp
[[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
hydrate(seqnum_t next_inbound, seqnum_t next_outbound) noexcept;
```

- Acquires `mutex_` (async); on drain/cancel → `std::unexpected(session_already_closed)`.
- Sets `next_inbound_ = next_inbound; next_outbound_ = next_outbound;`. No validation
  (caller supplies store-recovered values ≥ 1).
- **C1.1**: production-only (not test-gated). Distinct from `set_counters_for_test`.
- **C1.2**: idempotent at the value level — calling twice with the same values is a no-op
  effect; the *one-shot* guard lives in the caller (`ensure_hydrated_`), not here.

## C2 — `Session::ensure_hydrated_()`

```cpp
[[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> ensure_hydrated_() noexcept;
```

- **C2.1** one-shot: `if (hydrated_) co_return ok;` then `hydrated_ = true;` (set before the
  awaits so a re-entrant call is a no-op).
- **C2.2** no-store: `if (store_ == nullptr) co_return ok;` — no read (INV-H4 / FR-005).
- **C2.3** read both: `next_seqnum(inbound,false)` then `next_seqnum(outbound,false)`.
  A read failure → `record_state_transition_(Disconnected)` + `std::unexpected(err)` (FR-006);
  **no partial seed** — the manager is mutated only after BOTH reads succeed.
- **C2.4** hydrate: `co_await seqnum_mgr_.hydrate(in, out)`.
- **C2.5** call sites (exactly two; one-shot makes double-call safe):
  - initiator: first line of `emit_initiator_logon_()`, **before** the `reset_on_logon` block.
  - acceptor: first line of the `NotConnected` inbound-Logon case, **before** `interpret_logon`.
- **C2.6** precedence (INV-H5): runs before `reset_on_logon` (024) and before `check_inbound` (013).

## C3 — inbound persist (deliver-then-persist)

- **C3.1**: on the in-seq accepted path, **after** the `fromApp`/`fromAdmin` callback returns
  (and after `check_inbound` advanced the in-memory counter), issue
  `co_await store_->next_seqnum(direction_t::inbound, /*increment=*/true)` when `store_ != nullptr`.
- **C3.2** ordering (INV-H2): the persist MUST follow the callback — never precede it.
- **C3.3** failure (D-3): a failed persist → `record_state_transition_(Disconnected)` +
  `std::unexpected(store_io_failure)`. Fatal.
- **C3.4** GapFill (INV-H1 / D-5): a `SequenceReset`-GapFill jump is **not** persisted; the
  durable counter stays ≤ the manager (lower bound). No code at the jump site touches `store_`.
- **C3.5** no-store: `store_ == nullptr` ⇒ no persist call (byte-identical, INV-H4).

## C4 — what does NOT change

- The in-memory `check_inbound` gate (advance-before-deliver) — unchanged.
- The outbound store write (`store(...,outbound)` `:3782`, I-07 logged-then-proceed) — unchanged
  (L-029-2 residual documented, not fixed here).
- `MessageStore` pure-virtual count — stays 4 (no `set_seqnum` added).
- Wire frames, error slots, codegen, C-ABI — none.
- Reconnect behavior — no re-hydrate (one-shot).
