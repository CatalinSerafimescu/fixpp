# Phase 1 Data Model: Persistent seqnum hydrate

## Entities / state

| Entity | Where | Change |
|--------|-------|--------|
| `SeqnumManager::hydrate(in, out)` | `seqnum_manager.{hpp,cpp}` | **new** awaitable setter — acquire `mutex_`, `next_inbound_ = in; next_outbound_ = out`. Mirrors `set_next_inbound` (both counters). First production outbound setter (FR-008). |
| `Session::ensure_hydrated_()` | `session.{hpp,cpp}` | **new** one-shot helper: `if (hydrated_) co_return ok; if (hydrating_) co_return ok; hydrating_ = true; if (!store_is_persistent_) { hydrating_ = false; co_return ok; } auto in = co_await store_->next_seqnum(inbound,false); auto out = co_await store_->next_seqnum(outbound,false); …on read failure → hydrating_ = false; Disconnected + error (FR-006)…; co_await seqnum_mgr_.hydrate(*in, *out); hydrated_ = true; hydrating_ = false;` — `hydrated_` latches **only after** success (D-9). On the acceptor reset-Logon branch the **inbound** seed is withheld (hydrate outbound only / leave `next_inbound` at 1) so the reset arm owns the post-state (C2.4 / RC-1); the pseudocode above is the non-reset common path. |
| `Session::hydrated_` | `session.hpp` | **new** `bool` flag, strand-confined, default `false`. One-shot guard, set **only after** hydrate succeeds (D-9 — a transient read failure stays retryable on the next reconnect). |
| `Session::hydrating_` | `session.hpp` | **new** `bool` flag, strand-confined, default `false`. Re-entrancy guard cleared on failure (D-9); does NOT make a failed hydrate sticky. |
| `Session::store_is_persistent_` | `session.hpp` | **new** `bool`, captured at `open()` inside the `if (cfg_.store_factory)` branch (`session.cpp:779`) from the new `MessageStoreFactory::yields_persistent_store()` accessor (default `true`; `MemoryStoreFactory` → `false`; null-store path leaves it `false`) — a one-bit additive **factory** surface, NOT a 5th `MessageStore` pure-virtual (D-10/New-A). Gates hydrate's reads: a memory/null store ⇒ `false` ⇒ zero added reads/alloc (FR-005). `MessageStore` cap stays 4. |
| `MessageStoreFactory::yields_persistent_store()` | `message_store_factory.hpp` | **new** non-pure `virtual bool … const noexcept`, default `true` (safe default: a custom store hydrates unless it opts out). `MemoryStoreFactory` overrides → `false`; `FileStoreFactory` inherits `true`. On the **factory** interface — does NOT touch the `MessageStore` 4-pure-virtual cap (Article XIV.2). NOT the flush-hook tag (orthogonal to durability, D-10). |
| `persist_inbound_advance_()` | `session.cpp` | **new** named helper: `if (!store_is_persistent_) co_return ok; auto r = co_await store_->next_seqnum(inbound,true); if (!r) { Disconnected; co_return std::unexpected(...); } co_return ok;` — called at every PERSIST site in the disposition matrix below (RC-2). Failure → Disconnected (D-3). |
| inbound persist call sites | `session.cpp` (see disposition matrix) | **new** `persist_inbound_advance_()` invoked at each `check_inbound`-success site that advanced the manager, **after** delivery/handling; NOT at no-advance arms. |
| `session.cpp:1517` I-3 comment | `session.cpp` + prose | **edited** → deliver-then-persist (D-8). |

No new wire field, no new error slot, no new `MessageStore` pure-virtual (the `MessageStore`
interface stays at 4; the persistence discriminator is one non-pure accessor on the **factory**),
no FSM state, no config key.

## Counter timing (the core of the design)

| Event | In-memory (`SeqnumManager`) | Durable (`store_`) |
|-------|------------------------------|--------------------|
| Cold open, persistent store, **non-reset** Logon | outbound `hydrate` ← store; inbound seed ← store (one-shot, D-6) | unchanged (read only) |
| Cold open, persistent store, **reset** Logon (`141=Y`/`reset_on_logon`) | outbound `hydrate` ← store; **inbound seed withheld** so the reset arm owns the post-state (D-6/RC-1) | unchanged (read only) |
| Cold open, memory/null store (`store_is_persistent_==false`) | stays at 1 | n/a — no read (FR-005 byte-identical, D-10) |
| Reconnect (same process), hydrate succeeded | unchanged (NO re-hydrate, D-6) | unchanged |
| Reconnect after a **transient** hydrate read-failure | re-attempts hydrate (`hydrated_` still false, D-9) | re-read |
| In-seq inbound msg `S` accepted (incl. resend-fill) | `check_inbound` advances `S→S+1` **before** delivery (gate, unchanged) | `persist_inbound_advance_()` advances **after** delivery (D-2/D-4) |
| Outbound send | `assign_outbound` (existing) | `store(...,outbound)` (existing `:3782`) |
| Inbound `SequenceReset`-GapFill jump `N→M` | `set_next_inbound(M)` (existing) | **not persisted** (D-5) → store lags (INV-H1) |
| Inbound persist write fails | — | fatal → Disconnected (D-3) |

## Persist disposition matrix (RC-2 — `persist_inbound_advance_()` keyed to each `check_inbound`-success site)

A single tail persist would miss every admin message that returns early after advancing the
counter, and a blind pre-return helper would over-persist the no-advance arms. The durable inbound
write is therefore keyed to each site where `check_inbound` **advanced** the manager:

| Site (`session.cpp`) | `check_inbound` advanced? | Disposition |
|----------------------|---------------------------|-------------|
| Acceptor Logon (`NotConnected`, `:1596`) — in-seq | yes | **PERSIST** (after the reply Logon / Active) |
| Initiator Logon-ack (`LogonSent`, `:2841`) — in-seq | yes | **PERSIST** (after Active) |
| Heartbeat (`:2568`) | yes | **PERSIST** (after echo) |
| TestRequest (`:2605`) | yes | **PERSIST** (after reply) |
| ResendRequest (`:2634`) | yes | **PERSIST** (after reply) |
| Logout (`:2463`) | yes | **PERSIST** (after handling) |
| Reject (`:2470`) | yes | **PERSIST** (after handling) |
| In-seq app message (tail `:2732`) | yes | **PERSIST** (after `fromApp`) |
| **Resend-fill replayed in-seq app** (PossDup app arriving in-sequence through the Active path) | yes | **PERSIST** (New-2 — these advance the counter and DO persist; distinct from the GapFill jump) |
| Too-low / PossDup redelivery (`:2257`/`:2269`/`:2292`) | no (returns false, no advance) | **NO-PERSIST** |
| `validate_sequence_numbers=false` deliver-without-advance (`:2294-2316`) | no (delivered, counter not advanced) | **NO-PERSIST** |
| **GapFill exact-match `35=4`, `validate_sequence_numbers=false`** (`:2339`, advanced via `check_inbound` at `:2253`; dispatches `fromAdmin`, `co_return`s at `:2359` WITHOUT the tail and WITHOUT `apply_inbound_sequence_reset`) | **yes** (`+1` via `:2253`, S5 ordering artifact) | **PERSIST** (after `fromAdmin` returns — RC-B) |
| **Reset-mode `35=4`, `validate_sequence_numbers=false`** (S6, `:1968-2026`; runs BEFORE `check_inbound`, deliver-without-advance) | no (counter unchanged) | **NO-PERSIST** (no advance) |
| **Absolute `NewSeqNo` jump** via `apply_inbound_sequence_reset` (validate-ON, `:2026`/`:2361`) | jump via `set_next_inbound`, NOT a `+1` advance | **NO-PERSIST** (D-5 / INV-H1 lower bound) |

Resend-fill vs GapFill-jump (New-2): an ordinary resend **fill** (a PossDup app message replayed
*in sequence*) arrives through the normal in-seq Active path and PERSISTS like any in-seq message;
only the `apply_inbound_sequence_reset` **absolute jump** is excluded. The matrix must not let the
jump exclusion accidentally suppress fill persistence.

**`35=4` three-way split (RC-B):** the `msg_type=="4"` family does not collapse to one disposition.
The validate-off **exact-match GapFill** (`:2339`) reaches its branch AFTER `check_inbound` already
`+1`-advanced the manager at `:2253` and early-`co_return`s at `:2359` without `apply_inbound_sequence_reset`,
so it **PERSISTs** (else the durable inbound counter silently lags by one for every accepted
exact-match GapFill on a validate-off session — a 028-interop-cell config). The validate-off
**Reset-mode `35=4`** (S6, `:1968-2026`) runs BEFORE `check_inbound` and delivers without advancing
→ NO-PERSIST. The validate-ON **absolute jump** via `apply_inbound_sequence_reset` (`:2026`/`:2361`)
is the INV-H1 lower-bound exclusion → NO-PERSIST. **Fatal-after-advance exits** (outbound emit/assign
failure, TestReqID-mismatch `:2531`) do not persist because the session terminates; INV-H1 keeps the
durable counter a safe lower bound (reconnect re-hydrates the last durable value).

## Invariants

- **INV-H1** (lower bound): `store.next_inbound ≤ manager.next_inbound` at all times. The
  persisted counter is never *ahead* of the true expected ⇒ restart never skips an inbound;
  013 ResendRequest reconciles any residual gap.
- **INV-H2** (deliver-then-persist): the durable inbound advance for message `S` happens strictly
  **after** the `fromApp`/`fromAdmin` callback for `S` returns ⇒ at-least-once (crash mid-delivery
  ⇒ durable still `≤ S` ⇒ re-deliver on restart, deduped by PossDup).
- **INV-H3** (one-shot/cold): `ensure_hydrated_` mutates the manager **at most once** per session
  lifetime, on the first counter touch; reconnects never re-hydrate.
- **INV-H4** (non-persistent no-op): `store_is_persistent_ == false` (memory store, null store)
  ⇒ no read, no mutation, byte-identical frames, zero added allocation (D-10 — a configured
  `MemoryStore` is non-null but discriminated as non-persistent at `open()`, so it skips the read).
- **INV-H5** (precedence): the **outbound** hydrate runs **before** `reset_on_logon` (024 reset
  wins, initiator); the **inbound** seed is withheld on a reset Logon so the 013 received-141 /
  `reset_on_logon` reset arm owns the post-state (RC-1 / D-6) — hydrate never lets a resumed
  `next_inbound` pre-empt a peer reset Logon and fatal it as too-low.
- **INV-H6** (latch-after-success): `hydrated_` is set **only after** both reads + `hydrate()`
  succeed; a transient read failure leaves `hydrated_ == false` so the next reconnect retries
  (D-9). Re-entrancy is guarded by `hydrating_`, cleared on failure.

## Witness matrix (→ quickstart.md / tasks)

| # | Witness | Asserts |
|---|---------|---------|
| W1 | Initiator restart resume outbound | store `next_outbound=42` → cold open → Logon `34=42` (SC-001) |
| W2 | Inbound durable-tracking + restart resume | drive a stream that **includes admin frames (heartbeats)** + app messages so the persist matrix is exercised — a heartbeat-only stream must still durably track; 5 accepted in-seq inbounds → durable `next_inbound=6`; restart → resume `6` (SC-002, FR-001, RC-2) |
| W3 | Deliver-then-persist ordering | durable counter NOT advanced until after the `fromApp` callback returns (INV-H2) — assert via a callback-observing store/app |
| W4 | Acceptor cold resume | store `{in=37,out=42}` → acceptor cold open → peer Logon `34=37` is in-seq (not too-high fatal), reply samples `34=42` (FR-009, both-roles) |
| W5 | Post-GapFill lower bound + recovery precondition | GapFill jump manager `N→M` not persisted; `store.next_inbound ≤ manager` (INV-H1). Restart: assert the lower-bound recovery is non-fatal **only with `enable_next_expected_msg_seq_num` ON (or a peer reset Logon)** — and assert that **knob-OFF** restart-after-GapFill whose peer Logon is too-high **fatals on the Logon gate** (the documented L-029-1 case, its own assertion). Do NOT let the witness name claim "recovers via ResendRequest" for the knob-off path. (SC-004, L-029-1, RC-1/New-1, [[feedback_witness_asserts_named_postcondition_not_proxy]]) |
| W6 | Inbound persist failure fatal | injected `persist_inbound_advance_()` (`next_seqnum(inbound,true)`) failure → Disconnected (fatal); durable counter stays the last-persisted lower bound (NOT "manager unchanged" — in-memory advanced pre-delivery, so the in-flight msg may replay at-least-once, never skipped) (SC-006, D-3) |
| W7 | Non-persistent store no-op | `store_is_persistent_==false` (memory store AND null store) → no read, counters start 1, byte-identical + full regression (SC-003, INV-H4, D-10) |
| W8 | One-shot fires exactly once + happens-before | `ensure_hydrated_` mutates once across initiator AND acceptor; a reconnect does NOT re-hydrate (INV-H3); **hydrate completes-before the first `check_inbound` on both roles** (New-4 — a happens-before witness, not just a call count) — invariant-count regression ([[feedback_half_restructure_symmetric_api]]) |
| W9 | Precedence | (a) `reset_on_logon=true` + persisted `{37,42}` → outbound resets to 1 (reset wins, INV-H5). (b) Acceptor cold-hydrated, peer Logon `34=1,141=Y`, `reset_on_logon=false` → the inbound seed is **withheld** so `check_inbound(1)` is in-sequence and the received-141 reset wins (manager `next_inbound` per existing 013 policy); assert the session does **NOT** fatal as too-low at `:1615` (RC-1 corrected ordering — this is the witness the pre-fix design could not pass) |
| W10 | Live interop (skip-without-counterparty) | restart a fixpp side mid-session vs QFcpp/QFJ → resumes both counters, peer-ahead recovers |
| W11 | Hydrated initiator 789 advertisement | persisted `{in=37,out=42}` + `enable_next_expected_msg_seq_num=true` → the initiator Logon advertises `789=37` (hydrated `next_inbound`), and `seqnums_at_one` is false so **no** spurious `141=Y` (New-3, 027/789 interaction, research §New-3) |
| W12 | Validate-off `35=4` persist split (RC-B / New-B) | `validate_sequence_numbers=false` + exact-match GapFill `35=4` (advanced via `:2253`) → the durable inbound counter advances `+1` (PERSIST after `fromAdmin`); sibling: Reset-mode `35=4` validate-off → **no** persist (counter unchanged). A 028-interop-cell config (a live path). |
| W13 | Custom-store discriminator (RC-A / New-B) | a **custom persistent** factory (`yields_persistent_store()==true`, or the FileStore path) is discriminated persistent → hydrate runs; a **custom non-persistent** factory (`yields_persistent_store()==false`, like `MemoryStoreFactory`) → discriminated non-persistent → hydrate skipped. Tests the discriminator beyond the two built-ins by coincidence. |
