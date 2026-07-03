# Data model — async_mutex hardening (Cluster-4)

Field/state deltas only (this is a single primitive; no persistent data). Layout-golden-relevant
changes are flagged. Nothing here changes `sizeof(async_mutex)` (see research.md D-1).

## `async_mutex` members

| Member | Current | After | Layout impact |
|---|---|---|---|
| `waiter_pool_free_` | `std::atomic<detail::waiter_record*>` (8 B) | `std::atomic<std::uint64_t>` packing `{gen:54, slot_index:10}` (8 B) | none (same 8 B) — the ABA fix's tagged head (D-1) |
| `waiter_pool_slot` (element of `waiter_pool_storage_`) | `struct { alignas(max_align_t) std::byte storage[256]; }` (256 B) | `struct { alignas(max_align_t) std::byte storage[248]; std::atomic<std::uint32_t> free_link; }` (still **256 B**, 248+4 rounded to align 16) | **none** — `sizeof(slot)` unchanged → 512×256 unchanged (D-1). `free_link` = the PERSISTENT free-list link (mutex-lifetime), the AM-P1 part-2 fix |
| `waiter_pool_next_` | `std::atomic<std::uint32_t>`, unconditional `fetch_add` | same type; **bounded CAS** allocation (refuses past capacity) | none (D-4) |
| `in_flight_resumers_` | `std::atomic<std::uint32_t>`, relaxed dec/read | same type; **release** decrement / **acquire** reads | none (D-2) |
| all others (`state_`, `next_drain_head_`, `active_holders_count_`, `draining_`, …) | — | unchanged | none |

`sizeof(async_mutex)` stays `131120`, `alignof` `16` — guarded by `test_async_mutex_layout_golden` +
a new `static_assert(sizeof(waiter_pool_slot)==256)`.

## `detail::waiter_record` members

| Member | Current | After | Notes |
|---|---|---|---|
| `next_` | `waiter_record*` (chain link for state-LIFO/residual/reap) | unchanged | still the chain link for queued/granted/cancelled records |
| *(free-list link)* | reused `next_` pointer while free (current) | **MOVED OUT of `waiter_record`** → the per-slot `waiter_pool_slot::free_link` (above) | Gate-A BLOCKER fix: `release_ref` destroys the record (`:659`) BEFORE the free-list push and placement-new (`:831`) reinitializes it, so a record MEMBER link has a churning lifetime = UB. The slot link is constructed once and outlives every record. `sizeof(waiter_record)` static_assert tightens `<=256` → `<=248` |

Free-list link semantics: while a record is **free** it is absent from all chains (chain membership
holds a ref; the free push requires refcount 0 in `release_ref`), so a record is never simultaneously
chained (`next_`) and free (slot `free_link`) — confirmed no aliasing hazard (Gate-A: both reviewers).

## State / invariant changes

- **Free-list head** is now a generation-tagged `{gen, index}`; pop bumps generation (ABA-safe), push
  carries the loaded generation. Empty-list sentinel = reserved index value. (D-1)
- **Destructor precondition** widens: terminate if `state_ != not_locked` OR
  `next_drain_head_ != nullptr` OR **`in_flight_resumers_ != 0`** (the load-bearing barrier —
  decremented last, `:583`). `active_holders_count_` is NOT gated on (it decrements early in `unlock`,
  `:961`, so `==0` is not a "mutex untouched" barrier — Gate-A both reviewers). (D-3)
- **Drain terminal condition** unchanged in logic (`holders==0 ∧ resumers==0 ∧ both-lists-empty`) but
  its `in_flight_resumers_` read is now `acquire`, pairing with the runner's `release` decrement. (D-2)
- **Chain-walk `granted` arm** and **runner null-awaiter arm**: impossible-state traps / result-disarm
  (D-5/D-6) — no state-shape change, defensive assertions only.

## Test-visible seam (compile-gated, non-production)

- `FIXPP_ASYNC_MUTEX_TEST_SEAM` (undefined in all library + non-seam test builds) gates a no-op hook
  with TWO phases — `pop_pre_link_load` (head load → free-link load; AM-P1 part 2) and `pop_pre_cas`
  (free-link load → CAS; AM-P1 part 1) — letting the deterministic witness pin each half of the window.
  Zero production footprint. The seam-enabled test target is standalone (ODR — research.md D-7).
