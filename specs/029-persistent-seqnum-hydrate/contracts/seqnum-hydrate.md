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

- **C2.1** one-shot, latch-after-success (D-9 / INV-H6): `if (hydrated_) co_return ok;`
  then a strand-scoped re-entrancy guard `if (hydrating_) co_return ok; hydrating_ = true;`.
  `hydrated_ = true` is set **only after** both reads + `hydrate()` succeed — **not** before the
  awaits. On any failure, clear `hydrating_` and leave `hydrated_ == false` so the next reconnect
  retries (a pre-latched `hydrated_` makes a transient read failure sticky — Codex P2#5).
- **C2.2** non-persistent skip (D-10): `if (!store_is_persistent_) { hydrating_ = false; co_return
  ok; }` — no read (INV-H4 / FR-005). `store_is_persistent_` is a single `bool` captured at
  `open()` inside the `if (cfg_.store_factory)` branch (`session.cpp:779`) from the new
  `MessageStoreFactory::yields_persistent_store()` accessor (default `true`; `MemoryStoreFactory`
  → `false`; null-store path leaves it `false`) — a one-bit additive **factory** surface, NOT a 5th
  `MessageStore` pure-virtual. A configured `MemoryStore` is non-null but discriminated
  non-persistent via its factory's override (its `next_seqnum(false)` posts + locks + allocs —
  `memory_store.hpp:359`). The accessor is on the factory interface, so the `MessageStore`
  4-pure-virtual cap (Article XIV.2) is untouched. The flush-hook tag is NOT reused (orthogonal to
  durability — a custom persistent store with no flush hook would be wrongly skipped, D-10).
- **C2.3** read both: `next_seqnum(inbound,false)` then `next_seqnum(outbound,false)`.
  A read failure → `hydrating_ = false; record_state_transition_(Disconnected)` +
  `std::unexpected(err)` (FR-006); **no partial seed** — the manager is mutated only after BOTH
  reads succeed, and `hydrated_` is NOT set.
- **C2.4** hydrate (RC-1 split): hydrate the **outbound** counter unconditionally
  (`co_await seqnum_mgr_.hydrate(...)` writes outbound; it only feeds the Logon's `34=`, never a
  validation gate). Apply the **inbound** seed only on the **non-reset** Logon branch — when the
  header pre-scan shows the peer is announcing a reset (`141=Y`) or `reset_on_logon` is set, the
  inbound seed is **withheld** so `check_inbound` sees the construction value (1) in-sequence and
  the existing reset arm owns the post-state (RC-1). Then `hydrated_ = true; hydrating_ = false;`.
- **C2.5** call sites (exactly two; one-shot makes double-call safe):
  - initiator: first line of `emit_initiator_logon_()`, **before** the `reset_on_logon` block.
  - acceptor: in the `NotConnected` inbound-Logon case, **after** the `peer_sent_reset` /
    `reset_on_logon` header pre-scan (`:1585-1587`) so the inbound-seed decision (C2.4) is made
    against the announced reset, and **before** `check_inbound` (`:1596`) for the non-reset branch.
- **C2.6** precedence (INV-H5): the outbound hydrate runs before `reset_on_logon` (024); the
  inbound seed is conditioned on / runs after the Logon-gate reset decision so the 013 received-141
  reset (`:1760`) and the `reset_on_logon` reset (`:1587`) still win — a hydrated `next_inbound`
  never pre-empts a peer reset Logon into a too-low fatal at `:1615`. Knob-off lower-bound recovery
  for a too-high peer Logon is **not** non-fatal on the Logon path (`:1615`/`:2856`) unless
  `enable_next_expected_msg_seq_num` (789) is on (narrows SC-004 / L-029-1).
- **C2.7** 789 interaction (New-3): the hydrated `next_inbound` feeds the initiator
  `initr_next_expected` (789, `:603`) and the acceptor reply 789 — a hydrated initiator with the
  knob on advertises `789 = <hydrated next_inbound>`; `seqnums_at_one` (`:596`) is false on a
  resumed session so no spurious `141=Y` is emitted.

## C3 — inbound persist (deliver-then-persist, site-keyed) — RC-2

- **C3.0** helper: a named `persist_inbound_advance_()` does the durable write
  (`co_await store_->next_seqnum(direction_t::inbound, /*increment=*/true)` when
  `store_is_persistent_`; failure → Disconnected). It is invoked at every **PERSIST** site in the
  disposition matrix (data-model.md §Persist disposition matrix), **not** as a single tail write.
- **C3.1** PERSIST sites: after the callback/handling returns at each `check_inbound`-success site
  that advanced the manager — acceptor Logon, initiator Logon-ack, Heartbeat, TestRequest,
  ResendRequest, Logout, Reject, in-seq app, **and resend-fill replayed in-seq app** (these DO
  persist — they advance the counter like any in-seq message). For the **terminal** sites
  (Logout `:2462`, Reject) the persist runs **after `fromAdmin` but before**
  `record_state_transition_(Disconnected)` — `store_` is still live and the session is in its
  prior state when the persist runs; not persisting these is still a safe lower bound (INV-H1) but
  the matrix persists them for QFcpp-parity (`incrNextTargetMsgSeqNum`).
- **C3.2** ordering (INV-H2): the persist MUST follow the callback/handling — never precede it.
- **C3.3** failure (D-3): a failed persist → `record_state_transition_(Disconnected)` +
  `std::unexpected(store_io_failure)`. Fatal.
- **C3.4** `35=4` is a **three-way** split, not one disposition (RC-B):
  1. **GapFill exact-match, `validate_sequence_numbers=false`** (`:2339`, reached AFTER
     `check_inbound` `+1`-advanced at `:2253`; dispatches `fromAdmin`, `co_return`s at `:2359`
     WITHOUT the tail and WITHOUT `apply_inbound_sequence_reset`) → **PERSIST** (after `fromAdmin`
     returns — the manager DID advance, so the durable counter must follow it).
  2. **Reset-mode `35=4`, `validate_sequence_numbers=false`** (S6, `:1968-2026`, runs BEFORE
     `check_inbound`, deliver-without-advance) → **NO-PERSIST** (no advance).
  3. **Absolute `NewSeqNo` jump** via `apply_inbound_sequence_reset` (validate-ON, `:2026`/`:2361`)
     → **NO-PERSIST** (INV-H1 / D-5 — the jump is via `set_next_inbound`, not a `+1`; the durable
     counter stays ≤ the manager. No code at the jump site touches `store_`).

  Other NO-PERSIST sites (the manager did NOT `+1`-advance): too-low / PossDup redelivery,
  `validate_sequence_numbers=false` deliver-without-advance. Distinguish the GapFill *absolute jump*
  (no-persist) from an ordinary resend *fill* arriving in-sequence (persist, C3.1) — and from the
  validate-off exact-match GapFill above, which DID advance and so persists.
- **C3.5** non-persistent: `store_is_persistent_ == false` ⇒ no persist call (byte-identical, INV-H4).

## C4 — what does NOT change

- The in-memory `check_inbound` gate (advance-before-deliver) — unchanged.
- The outbound store write (`store(...,outbound)` `:3782`, I-07 logged-then-proceed) — unchanged
  (L-029-2 residual documented, not fixed here; spec.md New-2 is descriptive, not a "MUST NOT").
- `MessageStore` pure-virtual count — stays 4 (no `set_seqnum`, no `is_persistent()` on
  `MessageStore` — the non-persistent discriminator is a single `Session` bool captured at `open()`
  from a non-pure `MessageStoreFactory::yields_persistent_store()` accessor, D-10; the FACTORY
  surface does not touch the `MessageStore` Article XIV.2 cap).
- Wire frames, error slots, codegen, C-ABI — none.
- Reconnect behavior — no re-hydrate (one-shot).
