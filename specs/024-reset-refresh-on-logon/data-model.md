# Phase 1 Data Model: ResetOn{Logon,Logout,Disconnect} Knobs (024, S-017)

No new persistent entity, no codegen, no error slot. The "data model" is the three additive `SessionConfig` fields and the reset-trigger disposition table.

## Additive config fields (`include/fixpp/session/session_config.hpp`)

Placed next to the `013` `reset_seqnum_policy_field` (the related wire-handshake policy):

| Field | Type | Default | QuickFIX cfg key | Meaning |
|-------|------|---------|------------------|---------|
| `reset_on_logon` | `bool` | `false` | `ResetOnLogon` | Reset both seqnums to 1 at Logon (initiator: outbound Logon, which then carries `141=Y`; acceptor: inbound-Logon processing). |
| `reset_on_logout` | `bool` | `false` | `ResetOnLogout` | Reset both seqnums to 1 at Logout teardown — Logout sent (local graceful path) OR received (peer-initiated, arrives via terminal close) → terminal. |
| `reset_on_disconnect` | `bool` | `false` | `ResetOnDisconnect` | Reset both seqnums to 1 on ANY transport disconnect/teardown, incl. an abnormal drop. |

All three are value-typed PODs, FROZEN at `Session::open` ([arch §5.6], same as the rest of `SessionConfig`). No-implicit-default ([const §XII.5]): each default is explicit and documented inline.

**ABI note**: adding three `bool` members changes the `SessionConfig` struct layout → a normal source rebuild is required (no C-ABI surface; `SessionConfig` is a C++-only value type). Default-`false` preserves all existing wire/seqnum behavior.

## Reset-trigger disposition table

The durable reset helper — `reset_seqnums_to_one_durable(disposition)` = `co_await seqnum_mgr_.reset_to_one()` **then** `co_await store_->reset()`, with a store-failure disposition keyed on the **trigger cause** (knob-driven Logon = fatal / 013-only received-`141` = I-07 / teardown = logged) — is triggered at the transition points fixpp actually converges on. `{1,1}` denotes "both next_inbound and next_outbound at seqnum_min (1)".

| Lifecycle event | Site (`session.cpp`) | Knob | Action | `141=Y` emitted? |
|-----------------|----------------------|------|--------|------------------|
| Initiator Logon (outbound) | `open()` initiator arm `~:519-561` | `reset_on_logon` | `reset_seqnums_to_one_durable()` **before** `peek_outbound()`; build Logon | **Yes** — `send_reset_flag \|\| ((reset_on_logon \|\| reset_on_logout \|\| reset_on_disconnect) && {1,1})` (OR-of-three; extends today's `bilateral_strict`-only emission) |
| Acceptor Logon (inbound) | inbound-Logon handler — **BEFORE `check_inbound(seq)` `:1437`** (single combined decision, subsumes the `:1584` `141`-receipt reset) | `need_logon_reset = reset_on_logon \|\| peer_sent_reset` | **one** `reset_seqnums_to_one_durable()` before sequence validation so a fresh peer `34=1` is admitted; stricter disposition (knob → fatal; 013-only → I-07); exactly one store I/O | reply Logon mirrors received `141=Y` (existing path) |
| Logout teardown (Logout sent **or** received) | local: `close(graceful)` `:880`; **peer-initiated**: inline `→ Disconnected` `:~2095-2174` then `close(terminal)` via read-pump EOF — keyed on `logout_seen` (either direction), NOT `close_mode::graceful` | `reset_on_logout` | `reset_seqnums_to_one_durable()` (logged-then-proceed on store failure) | N/A (teardown — no Logon) |
| Any disconnect/teardown (incl. abnormal drop) | `close()` convergence, **before** the seqnum-mutex drain `~:1002` | `reset_on_disconnect` | `reset_seqnums_to_one_durable()` (logged-then-proceed on store failure) | N/A (teardown) |

### Precedence / interaction rules

1. **Knob-reset vs `141=Y`-handshake reset** (acceptor): collapsed into ONE combined pre-validation decision `need_logon_reset = reset_on_logon || peer_sent_reset` → a single `reset_seqnums_to_one_durable()` call before `check_inbound` (FR-002/FR-003). `FileStore::reset()` is non-idempotent I/O, so the single combined decision (not "two calls onto `{1,1}`") is what guarantees exactly one observable `store_->reset()` (FR-009). The disposition is the stricter applicable one: knob present → fatal (block `Active`); 013-only received-`141` with knobs off → I-07 logged-then-proceed, UNCHANGED from today (FR-001).
2. **`141` emission vs `013` policy** (initiator): emission is driven by the OR-of-three (`reset_on_logon || reset_on_logout || reset_on_disconnect`) gated on `{1,1}`; the `013` `reset_seqnum_policy_field` independently governs *validation* of the peer's `141` echo. Orthogonal — the knobs work under all three policy modes. The `reset_on_logout`/`reset_on_disconnect` arms emit `141=Y` on the **next** initiator Logon when a prior teardown left seqnums `{1,1}`.
3. **`reset_on_logout` + `reset_on_disconnect` on one teardown**: a graceful Logout that then disconnects would trigger both predicates in `close()`. A **single-fire guard** ("reset already done this teardown" flag) ensures the durable reset — `store_->reset()`, which does full I/O on every call and is NOT a no-op — fires **at most once** per teardown. The sequence result is `{1,1}`; the durable I/O happens exactly once (FR-009).
4. **Gap suppression** (initiator/acceptor `reset_on_logon`): a reset starts a fresh `{1,1}` space. **Initiator**: the reset precedes any inbound → no pre-reset gap → no ResendRequest. **Acceptor**: the reset runs before `check_inbound`, so `check_inbound` sees `next_inbound_==1` and admits `34=1` with no too-low disconnect (there is no ResendRequest path in the handshake state — a gap there is fatal) (FR-003).
5. **All knobs off**: no reset branch taken anywhere; `open()`'s `141` predicate reduces to today's `send_reset_flag` (FR-001, zero regression).

## State transitions

No new `fsm_state`. The reset mutates seqnum-manager + store counters as a side effect at the existing transitions:
- `…→ LogonSent` / inbound-Logon-accepted (the `reset_on_logon` arm)
- `Active → LogoutSent → Disconnected` (local graceful Logout, `reset_on_logout`) AND `Active/LogonReceived → Disconnected` via peer-received Logout `35=5` (`reset_on_logout`, keyed on `logout_seen`)
- `* → Disconnected` (any, `reset_on_disconnect`)
