# Contract: Constitution amendment — Article XI §3, a second permitted coroutine mutex shape (v4.3 → v4.4)

**Owner ruling (2026-11-14, Gate A round 1; spec Clarifications):** the amendment rides 103's PR (MINOR, Article
XX; precedent feature 097's v4.3). Article XX §1: "Any conflict between this document and a feature spec must be
resolved by amending the article first … then proceeding." Applying this text is therefore the **first**
implementation step (plan.md phase 0), before any D5′ code.

**Why it is needed.** Article XI §3 makes `fixpp::sync::async_mutex` "the only allowed mutex shape for coroutines".
`detail::replay_gate` (research D5′) is an awaitable shared/exclusive exclusion primitive with suspended contenders,
so it is a coroutine mutex shape in §3's sense, whatever it is called. The strand-local argument does not
distinguish it: `async_mutex` is itself used strand-locally. `async_mutex` was rejected for documented reasons
(D5′ "Alternatives considered": exclusive only, so sends would queue behind each other; its lock sequence leaves
#583's terminal filter; it needs a `cancel_and_drain` in both teardowns, #544 R-10).

## Proposed replacement text of Article XI §3

Article XI §3 has three sentences after its heading (the `async_mutex` sentence, the bold `std::mutex` ban, and "Enforced by clang-tidy custom check or grep gate."). All three stay byte-identical; one sentence is inserted after the first.

> 3. **Awaitable mutex required in coroutine context.** `fixpp::sync::async_mutex` (own implementation, BSL-1.0
> algorithm attribution to avast/asio-mutex) is the only allowed mutex shape for coroutines. One exception is
> permitted, for the serialisation of outbound frames against a resend answer and the wait of an in-place reconnect
> for the session's in-flight outbound frames and inbound chain, only (feature 103): a
> **strand-confined shared/exclusive gate**, `detail::replay_gate`, whose every acquire, and every release and wake
> made by a resumed coroutine, runs in its session's serialised execution domain (the domain that already orders
> the session's plain-member state, such as its send slot); a release made by the destructor of a coroutine frame
> destroyed without being resumed runs only while that domain's execution context is being destroyed, when no
> handler of the domain runs or will run again; which uses no OS synchronisation primitive and no atomic, never blocks a thread, never
> resets or otherwise changes its caller's cancellation filter or cancellation state, and needs no unlock before
> its destruction and no drain at teardown; it is not a general session send gate, and any other use of such a gate requires its own
> amendment. **Plain `std::mutex` is banned in any header that includes `asio::awaitable<...>`.** Enforced by
> clang-tidy custom check or grep gate.

Unchanged and not amended: Article XV §9 ("`std::mutex` in coroutine context. Use `fixpp::sync::async_mutex`
(Article XI §3).") still bans `std::mutex`; its pointer to §3 follows the amended clause. Article XV §3 ("Coarse
global session lock") is not touched: the gate is per session and scoped to one resend answer.

## Sync Impact Report entry (prepended to `.specify/memory/constitution.md`)

```text
<!--
Sync Impact Report — v4.3 → v4.4 (<date>) — PENDING
  Pending. Article XX §2 sequence, in this order: Codex Gate A on feature 103 (which reviews this text in
    specs/103-resend-logon-reset-fidelity/contracts/constitution-xi3-amendment.md), THEN a scoped Codex
    Gate A review of the amended clause body as applied, THEN Gate B on 103's PR (Article XVII §2), THEN
    owner sign-off (Article XX §2), each recorded under Review and sign-off below. This header reads
    PENDING until all four have happened, then RATIFIED.
  Bump: MINOR. Article XX §4 makes only backwards-incompatible amendments (banned-pattern additions,
    perf-budget tightening) v-major; a permissive exception is therefore MINOR by implication and by the
    097 (v4.3) precedent. No CHANGELOG.md entry. Does a PR mergeable under v4.3 fail under
    v4.4? None does: the amendment permits one more shape and adds no banned pattern, budget or
    obligation.
    NOTE: the constitution's version is independent of the library's and of the C ABI's.
  Modified: Article XI §3 — a sentence inserted after its first sentence: one exception to
    "the only allowed mutex shape for coroutines", for the serialisation of outbound frames against a
    resend answer and the wait of an in-place reconnect for the session's in-flight outbound frames and
    inbound chain, only (feature 103): a strand-confined shared/exclusive gate, detail::replay_gate,
    whose every acquire, and every release and wake made by a resumed coroutine, runs in its session's
    serialised execution domain (a release by the destructor of a frame destroyed unresumed runs only
    while that domain's execution context is destroyed, when no handler of it runs again), with no OS primitive and no atomic, never
    blocking a thread, never changing its caller's cancellation filter or state, needing no unlock
    before destruction and no teardown drain, not a general session send gate; any other use needs
    its own amendment.
  Modified at the RATIFIED flip: the `> **Status:**` blockquote (v4.4 first, v4.3 moved to "Prior").
  Unchanged: Article XV §9 (bans std::mutex; its pointer to §3 follows the amended clause) and
    Article XV §3.
  Rationale: fixpp#588 as widened by the owner (2026-11-14) orders Session::send, the liveness loop's
    Heartbeat and TestRequest, close()'s Logout and the pending-Logout acknowledgement around a resend
    answer, with no wait and no allocation on the normal path and no terminal cancellation filter
    left on the caller (spec FR-014, FR-015). async_mutex is exclusive only (sends would queue behind
    each other), its lock sequence leaves #583's terminal filter, and it needs a cancel_and_drain in
    both teardowns (#544 R-10); the gate has none of these (research D5′).
  Affected catalogue rows: none. Affected specs: specs/103-resend-logon-reset-fidelity/ (research D5′,
    plan Constitution Check). Brain: brain/components/session.md's #544 note on "a session send gate".
  Rides: feature 103; its PR is titled in Article XX §2 form
    (`Constitution: amend §XI.3 — strand-confined replay gate for resend-answer serialisation`, as a
    prefix or the title, as 097 did).
  Review and sign-off: filled in this order before the RATIFIED flip. Gate A: <103's Gate A result>.
    Scoped amendment round: <rounds and tallies>. Gate B: <PR, rounds>. Owner sign-off: <date>.
-->
```

## What Gate A round 2 checks on this text

- The inserted sentence is the only change to §3, and its three existing sentences are byte-identical.
- Each property in the sentence is one that D5′ states, checked as follows (Gate A round 2, Codex #7: a runtime cell
  cannot establish a structural property, so the two kinds are separate):
  - **Behavioural cells:** no change to the caller's cancellation filter or state (R-588-g's filter witness, R-588-f(3)); a gate
    built, held, released and destroyed with no waiter needs nothing at teardown (R-588-g's no-waiter
    construct/destroy cell under ASan).
  - **Structural checks:** confinement to the session's domain (Gate A round 3, Codex #3: a clean TSan run proves
    only that no race occurred on the paths exercised; reworded at loop 2 round 2, Codex P2-2): a call-site census
    of every gate acquire, release and `wake_all` site and of every wake source (data-model.md's replay-gate list),
    each classified as a coroutine-body site (shown to run on the session strand; `note_engine_stop_` included: it
    runs inside `Engine::stop()`'s `co_spawn` on the session strand) or a destructor site (a shared holder, the
    exclusive guard, the waiter count, the `inbound_depth_` holder), which runs on the strand on every resumed exit
    and otherwise only under research D1's lifetime rule. A Debug `FIXPP_ASSERT(strand.running_in_this_thread())`,
    where the executor is a strand, at every **acquire** (the shared fast path, the contended wait's `++waiters`,
    the exclusive entry, the reconnect drain's wait) and inside `wake_all()`'s acting branch (`waiters != 0`), never
    on a bare decrement in a destructor: a pending waiter means the timer exists, and a Session that owns the timer
    is destroyed before its context, so no waiter is pending at a supported teardown. G9 (research) is the
    teardown witness. R-588-h under TSan stays as race evidence only. No atomic and no OS synchronisation primitive in `detail::replay_gate` (a source check on
    the gate's header and source: no `std::atomic`, `std::mutex`, `std::shared_mutex`, `std::condition_variable` or
    `<atomic>` / `<mutex>` / `<shared_mutex>` / `<condition_variable>` include, run in `/speckit-verify`, proven to
    fire on a seeded `std::atomic` in a scratch copy); no unlock-before-destruction precondition and no teardown
    drain (a source/lifecycle proof in the verify record: the gate's destructor has no precondition, and neither
    `close()` nor `Engine::stop()` calls any gate drain).
  - **Scope:** not a general send gate (FR-014's scope; spec Out of Scope), a review property of the text.
- The permission is scoped to the resend-answer serialisation and the reconnect's wait for in-flight outbound frames
  and the inbound chain (research D1, owner ruling 2026-11-15), so it cannot be cited for another gate; a further
  use of the gate needs its own amendment (Article XX §1).

## How the clause's terms are read (checklist audit; not amendment text)

The amended sentence above is unchanged; these readings are what the structural checks test it against.

- **"Its session's serialised execution domain"** is the session's serialisation domain as session.hpp defines it
  for `close()` and `send()`: the session strand under `threading_mode::per_session_strand` (the default), and the
  attested already-serialised executor under `threading_mode::direct_executor`. The Debug strand assertion runs
  only where the executor is a strand; under `direct_executor` the confinement rests on the caller's attestation,
  as it does for every strand-confined member, and the call-site census (T075) is the check.
- **"No OS synchronisation primitive and no atomic"** is about `detail::replay_gate`'s own code, which is what the
  source check on its header reads. The `asio::steady_timer` service it waits on is asio's, as for every other
  session timer, and is outside the clause.
- **The clause governs `detail::replay_gate` only.** The bound's join counter (a `std::shared_ptr<std::atomic<int>>`,
  research D4.4) is not part of the gate: it follows `liveness_counter_`'s FQ-C pattern, because its guard may run
  at the destruction of a detached frame after the Session is gone, where a plain member could not be touched. The
  no-atomic check is therefore not widened to session.hpp (tasks, "Bundle findings" 7).
- **Lock order.** The gate is acquired before the seqnum mutex and before `write_gate_`, and no path acquires the
  gate while it holds either `async_mutex` (research D5′ "Deadlock analysis"; spec FR-014). The call-site census
  reads each acquire site against it.
- **The two uses.** Every acquire site and every wake source of data-model.md's replay-gate list serves the
  resend-answer serialisation, the reconnect's wait, or both; the census (T075) classifies each one so, and a site
  that serves neither is "any other use", which needs its own amendment.
