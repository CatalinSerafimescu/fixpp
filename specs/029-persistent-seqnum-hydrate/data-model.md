# Phase 1 Data Model: Persistent seqnum hydrate

## Entities / state

| Entity | Where | Change |
|--------|-------|--------|
| `SeqnumManager::hydrate(in, out)` | `seqnum_manager.{hpp,cpp}` | **new** awaitable setter — acquire `mutex_`, `next_inbound_ = in; next_outbound_ = out`. Mirrors `set_next_inbound` (both counters). First production outbound setter (FR-008). |
| `Session::ensure_hydrated_()` | `session.{hpp,cpp}` | **new** one-shot helper: `if (hydrated_) co_return ok; hydrated_ = true; if (!store_) co_return ok; auto in = co_await store_->next_seqnum(inbound,false); auto out = co_await store_->next_seqnum(outbound,false); …on read failure → Disconnected + error (FR-006)…; co_await seqnum_mgr_.hydrate(*in, *out);` |
| `Session::hydrated_` | `session.hpp` | **new** `bool` flag, strand-confined, default `false`. One-shot guard (cold-open only). |
| inbound persist call | `session.cpp` Active/LogonReceived in-seq path | **new** `next_seqnum(inbound,true)` **after** the in-seq `fromApp`/`fromAdmin` returns; failure → Disconnected (D-3). |
| `session.cpp:1517` I-3 comment | `session.cpp` + prose | **edited** → deliver-then-persist (D-8). |

No new wire field, no new error slot, no new `MessageStore` pure-virtual, no FSM state, no config key.

## Counter timing (the core of the design)

| Event | In-memory (`SeqnumManager`) | Durable (`store_`) |
|-------|------------------------------|--------------------|
| Cold open, `store_` present | `hydrate(in,out)` ← store (one-shot, D-6) | unchanged (read only) |
| Cold open, no `store_` | stays at 1 | n/a — no read (FR-005 byte-identical) |
| Reconnect (same process) | unchanged (NO re-hydrate, D-6) | unchanged |
| In-seq inbound msg `S` accepted | `check_inbound` advances `S→S+1` **before** delivery (gate, unchanged) | `next_seqnum(inbound,true)` advances **after** delivery (D-2/D-4) |
| Outbound send | `assign_outbound` (existing) | `store(...,outbound)` (existing `:3782`) |
| Inbound `SequenceReset`-GapFill jump `N→M` | `set_next_inbound(M)` (existing) | **not persisted** (D-5) → store lags (INV-H1) |
| Inbound persist write fails | — | fatal → Disconnected (D-3) |

## Invariants

- **INV-H1** (lower bound): `store.next_inbound ≤ manager.next_inbound` at all times. The
  persisted counter is never *ahead* of the true expected ⇒ restart never skips an inbound;
  013 ResendRequest reconciles any residual gap.
- **INV-H2** (deliver-then-persist): the durable inbound advance for message `S` happens strictly
  **after** the `fromApp`/`fromAdmin` callback for `S` returns ⇒ at-least-once (crash mid-delivery
  ⇒ durable still `≤ S` ⇒ re-deliver on restart, deduped by PossDup).
- **INV-H3** (one-shot/cold): `ensure_hydrated_` mutates the manager **at most once** per session
  lifetime, on the first counter touch; reconnects never re-hydrate.
- **INV-H4** (no-store no-op): `store_ == nullptr` ⇒ no read, no mutation, byte-identical frames,
  zero added allocation.
- **INV-H5** (precedence): hydrate runs **before** `reset_on_logon` (024 reset wins, initiator)
  and **before** `check_inbound` (013 received-141 reset wins, acceptor).

## Witness matrix (→ quickstart.md / tasks)

| # | Witness | Asserts |
|---|---------|---------|
| W1 | Initiator restart resume outbound | store `next_outbound=42` → cold open → Logon `34=42` (SC-001) |
| W2 | Inbound durable-tracking + restart resume | 5 in-seq inbounds → durable `next_inbound=6`; restart → resume `6` (SC-002, FR-001) |
| W3 | Deliver-then-persist ordering | durable counter NOT advanced until after the `fromApp` callback returns (INV-H2) — assert via a callback-observing store/app |
| W4 | Acceptor cold resume | store `{in=37,out=42}` → acceptor cold open → peer Logon `34=37` is in-seq (not too-high fatal), reply samples `34=42` (FR-009, both-roles) |
| W5 | Post-GapFill lower bound | GapFill jump manager `N→M` not persisted; `store.next_inbound ≤ manager` (INV-H1); restart → ResendRequest, recovers (SC-004, L-029-1) |
| W6 | Inbound persist failure fatal | injected `next_seqnum(inbound,true)` failure → Disconnected, manager unchanged, no partial state (SC-006, D-3) |
| W7 | Memory/null store no-op | no `store_` → no read, counters start 1, byte-identical + full regression (SC-003, INV-H4) |
| W8 | One-shot fires exactly once | `ensure_hydrated_` mutates once across initiator AND acceptor; a reconnect does NOT re-hydrate (INV-H3) — invariant-count regression ([[feedback_half_restructure_symmetric_api]]) |
| W9 | Precedence | `reset_on_logon=true` + persisted `{37,42}` → outbound resets to 1 (reset wins, INV-H5); received-141 acceptor → manager 1 after hydrate (INV-H5) |
| W10 | Live interop (skip-without-counterparty) | restart a fixpp side mid-session vs QFcpp/QFJ → resumes both counters, peer-ahead recovers |
