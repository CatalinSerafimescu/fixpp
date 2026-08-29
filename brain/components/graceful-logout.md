---
type: Flow Decision Map
title: Graceful logout phase 1 — confirmation, timeout, and the removed field
description: run_logout_phase1's invariants, and the split-brain bug that deleting a field prevented.
status: stable
refs:
  - include/fixpp/session/session.hpp
  - src/session/session.cpp
codegraph_entry: [run_logout_phase1, SeqnumManager, Session]
---

# Graceful logout — phase 1

> ## ⚠️ The CODE is authoritative. This page is not.
>
> SecondBrain is a **consultant**, not a source of truth. It points you at the right files and explains
> **why** a decision was taken and what was **rejected** — that half is historical and does not change
> retroactively. It does **not** establish what the code does today.
>
> **Anything here describing current behaviour is a LEAD TO CHECK, not a fact to cite.** Verify against
> source before you rely on it, and cite the source, not this page.
>
> This page exists because signed-off design documents rotted. **It has no immunity from that** — a page
> trusted instead of read becomes the next fossil, and it would be a worse one, because it is the page
> people come to for the fossil list.

## Invariants, and where each is enforced

| Invariant | Why | Enforced at |
|---|---|---|
| **Outbound sequence numbers are read via `seqnum_mgr_.peek_outbound()` and advanced via `assign_outbound()` — the bare `next_outbound_seq_` field was DELETED** | ⭐ **the deletion IS the fix.** Two writers — the admin paths and `Session::send` — could diverge; removing the field made split-brain **unrepresentable** rather than merely unlikely | `SeqnumManager` is the only accessor [005 data-model E3; 009 FR-001(a)] |
| **`logout_confirmed_` distinguishes a peer-confirmed close from a timeout** | a graceful logout that times out and one the peer acknowledged must not be reported identically — only the second proves the peer processed it | set on inbound Logout while in `LogoutSent`; `run_logout_phase1` polls it. **Single-writer on strand** |
| **`logout_seen_` is written only at Logout-specific sites, and the code enumerates them in place** | the teardown reset predicate in `close()` depends on it, so an unlisted write site would corrupt teardown | 024 T013 — ⚠️ **the comment states a COUNT of write sites. Counts rot.** Re-derive with a symbol search before relying on it; the durable part is that the set is *closed and named*, not how many are in it |

> ⭐ **The first row is the most transferable lesson on this page.** The bug class was *"two writers to
> one counter"*, and the fix was not a lock, a convention, or a comment — it was **removing the second
> way to write it**. A defect you cannot express cannot regress. Compare `async_mutex.hpp`'s
> `std::terminate` on a live holder: also an unrepresentable-state design.

## See also

Teardown ordering across the engine is in
[`message-store-quiescence`](./message-store-quiescence.md) — `Engine::stop()` joins every role loop
**before** clearing the registry, which is what makes the store's destructor precondition hold by
construction.
