# Contract: `refresh_on_logon` per-logon re-hydrate (025)

Behavioral contract for the knob + the `force` latch-bypass. Anchors verified against merged
`session.cpp` (submodule main `0b9c8b8`).

## C1 — Config surface

- `SessionConfig::refresh_on_logon` : `bool`, explicit default `false`. No loader key added this
  slice (matches the 024/028 additive-knob precedent; a future `cfg_loader` `RefreshOnLogon`→field
  name-map is separate). Reading a default-constructed config returns `false`.

## C2 — `ensure_hydrated_(apply_inbound_seed, force=false)`

- **C2.1** When `force == false`: byte-identical to 029 — the `hydrated_` one-shot early-return
  fires if already hydrated this lifetime. (Cold path unchanged.)
- **C2.2** When `force == true`: the `hydrated_` early-return is **bypassed**; the body proceeds to
  the `hydrating_` re-entrancy guard, the `store_is_persistent_` skip, the two `next_seqnum(dir,false)`
  reads, the `apply_inbound_seed`-gated `hydrate(in,out)`, and the latch — all unchanged from 029.
- **C2.3** `force` does **not** alter the `store_is_persistent_` skip (`:576`): a non-persistent
  store is a no-op even under `force` (INV-RoL-2 / FR-005).
- **C2.4** `force` does **not** alter the `apply_inbound_seed` withhold (`:601`): a forced
  re-hydrate on a reset Logon still withholds the inbound seed (INV-RoL-5 / FR-009).
- **C2.5** A read/hydrate failure under `force` transitions to `Disconnected` with no partial seed,
  exactly as the cold path (INV-RoL-6 / FR-006). No new error slot.
- **C2.6** The `hydrated_` latch is left set after a forced call; this is benign (subsequent forced
  calls ignore it; a subsequent non-forced call short-circuits as already-hydrated, which is the
  intended cold-path behavior).

## C3 — Call-site force expression (the suppression gate)

At both call sites the `force` argument is:

```cpp
const bool refresh_active =
    cfg_.refresh_on_logon
    && cfg_.reset_seqnum_policy_field != fixpp::session::reset_seqnum_policy::bilateral_strict;
```

- **C3.1 (initiator)** `emit_initiator_logon_()` `:658`: `co_await ensure_hydrated_(!withhold_inbound,
  /*force=*/refresh_active)`. `withhold_inbound` stays `cfg_.reset_on_logon` (029). The hydrate
  precedes `peek_outbound()` (`:699`).
- **C3.2 (acceptor)** `NotConnected` Logon `:1738`: `co_await ensure_hydrated_(!withhold_inbound,
  /*force=*/refresh_active)`. `withhold_inbound` stays `peer_sent_reset || cfg_.reset_on_logon`
  (029). The hydrate precedes `check_inbound` and the reply `peek_outbound()` (`:1951`).
- **C3.3 (suppression)** Under `bilateral_strict`, `refresh_active == false`, so the call degrades to
  the 029 cold one-shot — zero extra reads, no malformed Logon (INV-RoL-3 / FR-008).

## C4 — Store-wins semantic

- **C4.1** When `refresh_active` and `store_is_persistent_` and the latch is bypassed, the manager
  counters are set to the store's values **unconditionally** (up or down). No `max(store, live)`
  clamp (INV-RoL-4 / FR-003).
- **C4.2** Direction: a store advanced above live moves the manager UP (W1); a store set below live
  moves it DOWN (W2). DOWN is required for the standby-follows-primary-reset-to-1 use case.

## C5 — Composition with the establishment FSM (unchanged interactions)

- **C5.1** `reset_on_logon` (024): no special-casing. The 029 ordering (outbound hydrate → durable
  reset at `:673` → reset-flag decision at `:713`) already yields body `34=1`; a forced re-hydrate
  before the reset is overwritten by the reset (witnessed by 029's
  `test_persistent_seqnum_hydrate.cpp:585-609`, which remains green).
- **C5.2** `bilateral_strict`: suppressed (C3.3). The unconditional `141=Y` (`:713-715` arm A) is
  never paired with a refreshed non-1 body.
- **C5.3** received-141 (acceptor): the `:1925` reset still owns the post-state; the inbound seed is
  withheld (C2.4 / C3.2). FR-009.
- **C5.4** 789 NextExpectedMsgSeqNum (027): unaffected — `next_inbound_unsafe()` is sampled after the
  (possibly refreshed) hydrate, so an advertised `789` reflects the refreshed inbound, consistent.

## C6 — Out of scope (explicit)

- The cold-open `bilateral_strict` malformed-Logon gap inherited from 029 (L-029-3) — see research
  D-RoL-6; routed to Gate A (default: defer).
- Making `bilateral_strict` itself `{1,1}`-guarded like QFJ/QFcpp — a 024 change touching the
  `:1795` mutual-agreement handshake.
- A `cfg_loader` `RefreshOnLogon` name-map.
- Advance-only/`max` semantics (rejected, D-RoL-1).
