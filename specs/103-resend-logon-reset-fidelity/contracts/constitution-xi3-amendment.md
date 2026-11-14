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

The two existing sentences stay byte-identical; one sentence is inserted between them.

> 3. **Awaitable mutex required in coroutine context.** `fixpp::sync::async_mutex` (own implementation, BSL-1.0
> algorithm attribution to avast/asio-mutex) is the only allowed mutex shape for coroutines. One exception is
> permitted, for the serialisation of outbound frames against a resend answer and the drain of in-flight outbound
> frames before an in-place reconnect only (feature 103): a
> **strand-confined shared/exclusive gate**, `detail::replay_gate`, whose every acquire, release and wake runs in
> its session's serialised execution domain (the domain that already orders the session's plain-member state,
> such as its send slot), which uses no OS synchronisation primitive and no atomic, never blocks a thread, never
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
  Bump: MINOR, per Article XX §4; no CHANGELOG.md entry. Does a PR mergeable under v4.3 fail under
    v4.4? None does: the amendment permits one more shape and adds no banned pattern, budget or
    obligation.
    NOTE: the constitution's version is independent of the library's and of the C ABI's.
  Modified: Article XI §3 — a sentence inserted between its two sentences: one exception to
    "the only allowed mutex shape for coroutines", for the serialisation of outbound frames against a
    resend answer and the drain of in-flight outbound frames before an in-place reconnect only
    (feature 103): a strand-confined shared/exclusive gate, detail::replay_gate,
    confined to its session's serialised execution domain, with no OS primitive and no atomic, never
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

- The inserted sentence is the only change to §3, and the two existing sentences are byte-identical.
- Each property in the sentence is one that D5′ states and a cell checks: confinement to the session's domain
  (D5′.1; R-588-h under TSan), no atomic and no OS primitive (D5′.1), no change to the caller's cancellation
  filter or state (D5′ "Cancellation"; R-588-g's filter witness and R-588-f(3)), no unlock before destruction and
  no teardown drain (D5′ "Rationale": unlike `async_mutex`'s `not_locked` precondition and `cancel_and_drain`), not
  a general send gate (FR-014's scope; spec Out of Scope).
- The permission is scoped to the resend-answer serialisation and the reconnect drain (research D1, owner ruling
  2026-11-15), so it cannot be cited for another gate.
