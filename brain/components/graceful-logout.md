---
type: Flow Decision Map
title: Graceful logout phase 1 — confirmation, timeout, and the removed field
description: run_logout_phase1's invariants, and the split-brain bug that deleting a field prevented.
status: stable
refs:
  - include/fixpp/session/session.hpp
  - src/session/session.cpp
  - .specify/536-537-538-recovery-liveness.md
  - spec/behaviors-and-limitations.md
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
| **`logout_confirmed_` distinguishes a peer-confirmed close from a timeout** | a graceful logout that times out and one the peer acknowledged must not be reported identically — only the second proves the peer processed it | set on inbound Logout while in `LogoutSent`; `run_logout_phase1` polls it. **Single-writer on strand**. ⚠️ **"polls it" is superseded by fixpp#536** (`.specify/536-537-538-recovery-liveness.md`, D-536-3): the wait races this session's own wake, which the LogoutSent arm calls, against `detail::await_deadline`, and the outcome is read from `logout_confirmed_` after the race, never from which arm finished |
| **`logout_seen_` is written only at Logout-specific sites, and the code enumerates them in place** | the teardown reset predicate in `close()` depends on it, so an unlisted write site would corrupt teardown | 024 T013 — ⚠️ **the comment states a COUNT of write sites. Counts rot.** Re-derive with a symbol search before relying on it; the durable part is that the set is *closed and named*, not how many are in it. ⚠️ **The local site moved (ruling R-24, below):** `run_logout_phase1` sets the flag only after its Logout's emit returned success. Since fixpp#590 the local site is skipped for a Logout that was cut (the next row): a Logout that was stored and not transmitted is not a Logout that was sent, so a configured `reset_on_logout` does not fire for it |
| **A Logout whose `toAdmin` started a terminal `close()` is CUT: the Logout step stores it, does not send it, and leaves at once** | one condition gets one ending (owner ruling R-8), and it is QuickFIX's: numbered, stored, not transmitted. Before it, on a `FileStore` with a live transport, timing decided whether that Logout reached the peer. The step writes `Disconnected` when the cut is decided, before the Logout takes its number, so nothing that arrives during the Logout's store call meets an Active session | `run_logout_phase1`: the two reads of `waited_terminal_closes_` around the Logout's `toAdmin`, the record `logout_cut_`, and `store_then_emit`'s `transmit` parameter. Design: `.specify/563-564-callback-send.md` §2.6; B&L `B-563-5` |

| **Phase 1 does not decide the close alone — it RACES a real `asio::steady_timer`** | `close(graceful)` must terminate even when phase 1 wedges behind the write gate or `async_write`, so it cannot depend solely on a Clock the caller injects | `Session::close`, graceful branch: `co_await (run_logout_phase1() \|\| close_grace.async_wait(...))`, with `close_grace` armed for `cfg_.logout_disconnect_timeout_ms`. A terminal close that joins cancels `close_grace` (093 plan OD-29). **One case in which nothing cancels it** (fixpp#590, owner ruling R-9): for a Logout that was cut, the joining close does not call the wake. The cut step ends by itself when the Logout's store call returns; where that call has not returned at the timer's expiry, the timer ends the join. What the timer does not end is B&L `L-563-7` and `B-563-5`, "The bound" |

> ⚠️ **THE TRAP THAT ROW SETS FOR TESTS, and it cost an 86-minute CI wedge and then a false green.**
> `close_grace` runs on **real time**. A `mock_clock` does **not** govern it. So in a mock-clock test
> **any bounded wait longer than `logout_disconnect_timeout_ms` will complete the close off that
> timer** — the test goes green having never exercised the mock-clock timeout it exists to test.
>
> Measured on PR #337 (2026-08-31), stated as history rather than as a current reading: deleting the
> mock `advance()` outright still passed, in **2202 ms**, against a 10 s pump budget and a 2 s
> `close_grace`. Bounding the budget under the grace period turned the same mutant red.
>
> **Derive a close-wait budget from `cfg.logout_disconnect_timeout_ms`, never from a helper's
> default.** And note what will *not* catch this: an arm that forces the guard to MISS. This defect
> is a spurious **HIT** — four forced-miss arms passed while it was live. The arm that finds it
> deletes the mechanism under test and asserts RED. See `tests/session/conformance/tc_logout_test.cpp`.

> ⭐ **The first row is the most transferable lesson on this page.** The bug class was *"two writers to
> one counter"*, and the fix was not a lock, a convention, or a comment — it was **removing the second
> way to write it**. A defect you cannot express cannot regress. Compare `async_mutex.hpp`'s
> `std::terminate` on a live holder: also an unrepresentable-state design.

## A Logout that was not sent, and the write the close waits for (fixpp#536 / #537 / #538 / #598)

The design is `.specify/536-537-538-recovery-liveness.md` §10 (D-OUT-6, rulings R-17 and R-24) and §11
(D-598-4); the user-facing statements are B&L `B-538-6`, `B-538-7` and `B-598-1`.

- **No wait for a reply to a Logout that was not sent (R-17).** `run_logout_phase1` reads what
  became of its Logout. When it was not sent, for any reason (a cancelled store call, a persistent
  store that refused it, a failed write, a Logout that could not be built), the phase records
  `Disconnected`, returns the error, arms no wait, and `close()` goes on to its terminal part. Before,
  the phase ignored the send's result and waited the logout timeout for an answer that could not come.
  A session with neither a live transport nor a callback sink keeps the wait: every frame of such a
  session is reported as sent.
  - **Rejected:** leaving the wait and filing an issue; the rule for the cancelled store only; QuickFIX's
    timing, which keeps the connection until the logout timeout (QuickFIX C++ and QuickFIX/J mark the
    Logout as sent whatever their send returned; the owner kept "close at once" with that fact before
    them); a result of `store_then_emit` that tells "no transport" from "transmitted".
- **No teardown reset for a Logout that was not sent (R-24), a change against `main`.** With
  `reset_on_logout` set, the teardown reset in `close()` runs when `logout_seen_` is set; the phase now
  sets it only on the path where its Logout's emit returned success, so a close whose Logout was not
  sent keeps the store's numbers. On `main` the flag was set after the emit whatever it returned. A
  Logout that was sent and never answered still resets: that case is fixpp#595. **Rejected:** keeping
  the reset. The reasons given with the ruling: the three QuickFIX engines reset for `ResetOnLogout`
  only when the peer's Logout is received, and 024's clause C3.1 names the local site as the Logout
  that was sent.
- **The close still waits for its Logout's write (§11, D-598-4).** On a `FileStore`, once the
  Logout's store call holds the store's lock no cancellation reaches its write, so `close(graceful)`
  and an escalating `close(terminal)` return when the write's job has run its body, not when the grace
  timer expires. Before, on a file executor of more than one thread, the grace timer's cancellation
  could skip the write and the store then counted a Logout it had not written. Whether the close should
  be bounded against a file executor that stalls is batch B41's subject; the host must keep the file
  executor running its jobs until `Engine::stop()` returns (`B-598-2`).

## See also

Teardown ordering across the engine is in
[`message-store-quiescence`](./message-store-quiescence.md) — `Engine::stop()` joins every role loop
**before** clearing the registry, which is what makes the store's destructor precondition hold by
construction.
