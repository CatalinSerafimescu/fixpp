# Phase 1 Data Model — Outbound store-failure disposition (059)

No new data **types**. This feature changes a *disposition* (control flow over existing state). The "model" here is the relevant state + the invariants the change must establish.

## Relevant existing state

| State | Owner | Meaning | Touched? |
|---|---|---|---|
| `SeqnumManager::next_outbound_` | `SeqnumManager` | in-memory wire counter; advanced by `assign_outbound()` per send | **reconciled** to the durable store counter at disconnect time via `set_next_outbound(durable_k)` inside `store_then_emit`'s fatal branch (FR-007) — NOT re-seeded via `hydrate()` on reconnect |
| store next-outbound counter | `MessageStore` impl | store's own next-expected; advanced only on successful `store()`; on `FileStore` it is the **durable** on-disk counter | unchanged |
| `store_is_persistent_` | `Session` | `true` for `FileStore`/custom persistent, `false` for `MemoryStore`/null | **read** (the disposition gate) |
| `hydrated_` | `Session` | one-shot latch; `ensure_hydrated_` re-reads the store when `!hydrated_` or `force` | **NOT touched** (clearing it has cross-feature blast radius — see research D4) |
| `SeqnumManager::set_next_outbound(n)` | `SeqnumManager` | existing outbound-only setter (032 restore) | **called** — reconcile the wire counter to the durable value (FR-007) |
| FSM state (`fsm_state`) | `Session` | `Active` / `Disconnected` / … | **written** (`Disconnected`) by the **caller** on the fatal-return (parity with the transport-failure return); NOT written inside `store_then_emit` |

## Disposition decision (the change), as a table

Inside `store_then_emit`, after `auto store_r = co_await store_->store(...)`:

Evaluated top-to-bottom (first match wins):

| # | Condition | Disposition | Requirement |
|---|---|---|---|
| 1 | `store_r.has_value()` (success) | proceed to transmit (Step 2) — **byte-identical to today** | happy path unchanged |
| 2 | `!store_r` **and** `store_r.error() == store_cancelled` | cancellation-class (shutdown drain) — today's behaviour (absorb → proceed). **No fail-closed, no reconcile.** | FR-005 (parity with thrown `operation_aborted`) |
| 3 | `!store_r` **and** `store_is_persistent_` (genuine retain failure: `store_io_failure` / `store_seqnum_out_of_order` / `store_capacity_exhausted`) | **FAIL CLOSED** — same return shape as the existing transport-write failure: (a) capture `err = store_r.error()` **first**; (b) best-effort reconcile — `dk = co_await store_->next_seqnum(outbound,false)`; if `dk`, `co_await seqnum_mgr_.set_next_outbound(*dk)`; (c) `co_return std::unexpected(err)`. **Do not transmit. No `record_state_transition_` inside `store_then_emit`** — the `Disconnected` transition is caller-owned (the send caller sees the error return, exactly as for a transport failure). | FR-001, FR-002, FR-004, FR-007, FR-008 |
| 4 | `!store_r` **and not** `store_is_persistent_` | logged-then-proceed (today's `(void)store_r;`) → transmit | FR-003 (L-008-2 stands) |
| 5 | store awaitable **throws** `operation_aborted` | existing catch → `co_return dispatch_aborted` — **unchanged** | FR-005 |
| 6 | store awaitable throws other | existing catch → absorb (volatile-style) — unchanged | FR-005 |

Skip-store cases (554-mask over-bound; `skip_store`) are unaffected — they never call `store()`. The reconcile (row 3a) is best-effort: if the durable read fails, the disconnect still occurs (degrades to the safe repeating-disconnect, not a wedge).

## Invariants established

- **INV-059-1 (no silent persistent freeze).** After the fix, on a persistent store no wire/durable divergence accumulates across sends. This holds on the **reconcile + no-transmit**, not on a transition: the un-retained frame is never put on the wire (fatal `co_return` precedes Step 2), and the best-effort reconcile restores `next_outbound == durable` after each failure — so even where a swallow-and-continue site keeps the session in its own flow, the next send cannot silently retain-freeze past the durable counter. On the primary app send path (`Session::send`) the failure additionally transitions to `Disconnected`. (FR-008; SC-001/002.)
- **INV-059-2 (no un-retained frame on the wire).** A frame whose persistent `store()` failed is never transmitted (fatal `co_return` precedes Step 2). Therefore peer-last-seen ≤ durable-store-counter at every disconnect boundary. (FR-002.)
- **INV-059-3 (reconnect consistency, policy-scoped).** The fatal branch reconciles `next_outbound` to the durable store counter at disconnect time (via `set_next_outbound`), so `peek_outbound() == durable-store-counter == peer-last-seen + 1` immediately. Reconnect resume then depends on the reset policy: **plain persistent** → clean resume at `k` (FR-007); **`reset_on_logon`** → the durable reset (gated on `cfg_.reset_on_logon`, `session.cpp:776-782`) overrides to 1 (reconcile neutral); **`bilateral_strict` (default)** → no reset runs (`bilateral_strict` only forces `141=Y`, `:816-818`), so the reconnect Logon carries `34=k` (k>1) + `141=Y` = the **pre-existing, deferred L-029-3** malformed Logon (`behaviors-and-limitations.md:1252-1265`) — 059 does not worsen it (un-reconciled `k+1` is also non-1) and clean resume is NOT claimed. **Inbound sequencing is untouched** in all cases. (FR-007; SC-004.)
- **INV-059-4 (volatile unchanged).** On a non-persistent store, byte-for-byte the same behaviour as `main`: no `Disconnected` transition, no `hydrated_` clear, transmit proceeds. (FR-003; SC-003.)
- **INV-059-5 (cancellation unchanged).** `operation_aborted` on the store awaitable still yields `dispatch_aborted` with no new `Disconnected`/`hydrated_` side effects beyond today's. (FR-005.)
- **INV-059-6 (fail-closed via the shared error-return channel).** The persistent retain failure surfaces as an error return of the **same shape** as the existing transport-write failure, so every caller handles it exactly as it handles that pre-existing return. Census of the 26 `store_then_emit` sites (re-stamped post-T006/T007, T013a): **9 propagating-broad-guard** (`if(!emit_r)→Disconnected`: `:864 :1781 :1825 :2496 :2820 :3379 :3542 :4623 :4681`) fail closed unchanged; **1 propagating-narrow-guard** (`Session::send` `:4051`, gating on `== dispatch_aborted`) is broadened to the store-fatal class so the primary app path fails closed; **16 swallow-and-continue** (`:2130 :2344 :2656 :2680 :2742 :2869 :2921 :2959 :2982 :3176 :3233 :3687 :4946 :5007 :5265 :5308`) exhibit their **pre-existing** transport-failure continuation (documented best-effort — the un-retained frame is never transmitted). (FR-006; SC-006.)

## State transition (persistent store, one transient fault)

```
Active ──send(k)──▶ store(k) FAILS (persistent, genuine — not store_cancelled)
   │                       │   [inside store_then_emit — mirrors transport-failure return]
   │                       ├─ err = store_r.error()                         (capture FIRST)
   │                       ├─ dk = read durable outbound counter (== k)     (best-effort)
   │                       ├─ set_next_outbound(dk)  → manager back to k     [INV-059-3]
   │                       └─ co_return unexpected(err)  (frame k NOT sent)  [INV-059-2]
   │                       ╎   ↳ CALLER transitions Disconnected (Session::send broadened guard)
   ▼
Disconnected ──(fault clears)──▶ reconnect
   │   plain persistent:     manager already at k → Logon/send resumes at k       (clean, FR-007)
   │   reset_on_logon:       durable reset overrides → resumes at 1               (reconcile neutral)
   │   bilateral_strict:     NO reset → Logon 34=k(>1) + 141=Y = pre-existing L-029-3 (not worsened)
   │   (inbound sequencing untouched in all cases)
   ▼
Active ──send(k)──▶ store(k) SUCCEEDS ─▶ transmit(k)   (clean resume, no gap, no reuse)
```

## Documentation deltas (data of record, updated at `/implement`)

| Doc | Change | Requirement |
|---|---|---|
| `spec/behaviors-and-limitations.md` — **L-008-2** | Narrow to volatile MemoryStore only; add that the **persistent** leg now **fails closed** (new B-/L- row for the persistent disposition). | FR-009 |
| `spec/feature-catalogue.md` | New 059 row (status/evidence/tests). | close-out |
| `spec/coverage-index.md` | 059 touched-module coverage entry. | close-out |
| parent `phases/phase-4/session/059-*.md` + `phases/phase-4.md` dashboard | lifecycle record. | close-out |
| `remaining-work/perf-and-hardening-findings.md` — Cluster 3 | mark S-P1-1 / S-P2-1 discharged (execution-order item ①). | close-out |
