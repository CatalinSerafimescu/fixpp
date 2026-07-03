# Phase 1 Data Model — Outbound store-failure disposition (059)

No new data **types**. This feature changes a *disposition* (control flow over existing state). The "model" here is the relevant state + the invariants the change must establish.

## Relevant existing state

| State | Owner | Meaning | Touched? |
|---|---|---|---|
| `SeqnumManager::next_outbound_` | `SeqnumManager` | in-memory wire counter; advanced by `assign_outbound()` per send | read-only here; re-seeded via existing `hydrate()` on reconnect |
| store next-outbound counter | `MessageStore` impl | store's own next-expected; advanced only on successful `store()`; on `FileStore` it is the **durable** on-disk counter | unchanged |
| `store_is_persistent_` | `Session` | `true` for `FileStore`/custom persistent, `false` for `MemoryStore`/null | **read** (the disposition gate) |
| `hydrated_` | `Session` | one-shot latch; `ensure_hydrated_` re-reads the store when `!hydrated_` or `force` | **NOT touched** (clearing it has cross-feature blast radius — see research D4) |
| `SeqnumManager::set_next_outbound(n)` | `SeqnumManager` | existing outbound-only setter (032 restore) | **called** — reconcile the wire counter to the durable value (FR-007) |
| FSM state (`fsm_state`) | `Session` | `Active` / `Disconnected` / … | **written** (`Disconnected`) in the fatal branch |

## Disposition decision (the change), as a table

Inside `store_then_emit`, after `auto store_r = co_await store_->store(...)`:

Evaluated top-to-bottom (first match wins):

| # | Condition | Disposition | Requirement |
|---|---|---|---|
| 1 | `store_r.has_value()` (success) | proceed to transmit (Step 2) — **byte-identical to today** | happy path unchanged |
| 2 | `!store_r` **and** `store_r.error() == store_cancelled` | cancellation-class (shutdown drain) — today's behaviour (absorb → proceed). **No fail-closed, no reconcile.** | FR-005 (parity with thrown `operation_aborted`) |
| 3 | `!store_r` **and** `store_is_persistent_` (genuine retain failure: `store_io_failure` / `store_seqnum_out_of_order` / `store_capacity_exhausted`) | **FAIL CLOSED**: (a) best-effort reconcile — `dk = co_await store_->next_seqnum(outbound,false)`; if `dk`, `co_await seqnum_mgr_.set_next_outbound(*dk)`; (b) `record_state_transition_(Disconnected)`; (c) `co_return std::unexpected(store_r.error())`. **Do not transmit.** | FR-001, FR-002, FR-004, FR-007, FR-008 |
| 4 | `!store_r` **and not** `store_is_persistent_` | logged-then-proceed (today's `(void)store_r;`) → transmit | FR-003 (L-008-2 stands) |
| 5 | store awaitable **throws** `operation_aborted` | existing catch → `co_return dispatch_aborted` — **unchanged** | FR-005 |
| 6 | store awaitable throws other | existing catch → absorb (volatile-style) — unchanged | FR-005 |

Skip-store cases (554-mask over-bound; `skip_store`) are unaffected — they never call `store()`. The reconcile (row 3a) is best-effort: if the durable read fails, the disconnect still occurs (degrades to the safe repeating-disconnect, not a wedge).

## Invariants established

- **INV-059-1 (no silent persistent freeze).** After the fix, on a persistent store there is **no** reachable state where `SeqnumManager::next_outbound_ > store.next_outbound` while `fsm_state == Active`. The first persistent retain failure transitions to `Disconnected`, so no subsequent swallowed-out-of-order retain can accumulate. (FR-008; SC-001/002.)
- **INV-059-2 (no un-retained frame on the wire).** A frame whose persistent `store()` failed is never transmitted (fatal `co_return` precedes Step 2). Therefore peer-last-seen ≤ durable-store-counter at every disconnect boundary. (FR-002.)
- **INV-059-3 (reconnect consistency).** The fatal branch reconciles `next_outbound` to the durable store counter at disconnect time (via `set_next_outbound`), so `peek_outbound() == durable-store-counter == peer-last-seen + 1` immediately, and an in-process reconnect resumes cleanly. For `bilateral_strict`/`reset_on_logon` the reconnect `reset_to_one()` overrides to 1 (also consistent). **Inbound sequencing is untouched.** (FR-007; SC-004.)
- **INV-059-4 (volatile unchanged).** On a non-persistent store, byte-for-byte the same behaviour as `main`: no `Disconnected` transition, no `hydrated_` clear, transmit proceeds. (FR-003; SC-003.)
- **INV-059-5 (cancellation unchanged).** `operation_aborted` on the store awaitable still yields `dispatch_aborted` with no new `Disconnected`/`hydrated_` side effects beyond today's. (FR-005.)
- **INV-059-6 (caller-independent fail-closed).** Because the `Disconnected` transition is owned by `store_then_emit`, every caller — including the two best-effort `(void)co_await` sites — leaves the session `Disconnected` on a persistent failure. (FR-006; SC-006.)

## State transition (persistent store, one transient fault)

```
Active ──send(k)──▶ store(k) FAILS (persistent, genuine — not store_cancelled)
   │                       │
   │                       ├─ dk = read durable outbound counter (== k)     (best-effort)
   │                       ├─ set_next_outbound(dk)  → manager back to k     [INV-059-3]
   │                       ├─ record_state_transition_(Disconnected)
   │                       └─ co_return unexpected(err)  (frame k NOT sent)  [INV-059-2]
   ▼
Disconnected ──(fault clears)──▶ reconnect
   │   plain session:      manager already at k → Logon/send resumes at k
   │   bilateral_strict /  reset_to_one() overrides → resumes at 1
   │   reset_on_logon:     (both consistent; inbound untouched)
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
