# Research — async_mutex hardening (Cluster-4) — Phase 0

Resolves the design unknowns before Gate A. The two load-bearing items (D-1 free-list redesign, D-7
deterministic AM-P1 witness) are the ones Gate A / Fable will hammer; they are settled here on paper.

Provenance for every defect: `phase-9/perf-investigation/findings/async-mutex-phase0-verification.md`
(Opus + Codex concurrence) + `async-mutex-review.md`.

---

## D-1 — AM-P1 free-list redesign: generation-tagged packed head + PERSISTENT per-slot link

**Decision.** Replace the tagless Treiber free-list with a **generation-tagged packed head** stored in
the existing `waiter_pool_free_` atom, retyped `std::atomic<detail::waiter_record*>` →
`std::atomic<std::uint64_t>`, packing `{ generation, slot_index }`. Each free slot's forward link is a
**persistent per-slot atomic index** whose lifetime is the *mutex's* lifetime — NOT a `waiter_record`
member (see the "lifetime" note; this is the Gate-A-1 BLOCKER fix).

- **Bit layout:** `slot_index` needs 10 bits (512 slots + a sentinel for "empty"); give the
  **generation the remaining 54 bits**. Generation-wrap needs 2⁵⁴ pops while one thread sits inside the
  pop window — unreachable (>50 yr at 10M contended pops/s; contrast the sibling `waiter_pool_next_`
  u32 whose 2³² wrap is reachable in ~7 min — D-4). Sentinel: `index == 0x3FF` = empty list.
- **Storage (BLOCKER fix — persistent slot metadata):** restructure `waiter_pool_slot` from a bare
  256-byte buffer to `struct { alignas(max_align_t) std::byte storage[248]; std::atomic<std::uint32_t>
  free_link; }`. `sizeof(waiter_pool_slot)` stays **256** (248 + 4, rounded to `alignof(max_align_t)=16`),
  so the layout golden is preserved. `free_link` is constructed ONCE when `waiter_pool_storage_{}` is
  value-initialized and lives for the whole mutex; the per-waiter `::new (slot.storage) waiter_record{}`
  (`:831`) only ever touches `storage[248]` and never the link. The `waiter_record` gains **no** member;
  its `:619` static_assert tightens to `sizeof(waiter_record) <= 248`.
- **Pop** (in `async_lock` initiation, replaces `:808-816`):
  1. `head = waiter_pool_free_.load(acquire)` → unpack `{gen, idx}`; if `idx == empty` fall through to
     the bump allocator (D-4).
  2. `next_idx = waiter_pool_storage_[idx].free_link.load(acquire)` — a **live-object atomic** load
     (the link outlives every record churn), closing the AM-P1 part-2 data race (the current `:810`
     reads `free_head->next_` as a plain field of a slot being concurrently reused).
  3. `CAS(waiter_pool_free_, head, pack(gen+1, next_idx), acq_rel, acquire)` — the **generation bump**
     defeats ABA: a stale head observed after a pop-pop-push cycle carries the old generation, the CAS
     fails, retry.
- **Push** (in `release_ref`, replaces `:666-671`, AFTER `~waiter_record()` at `:659`): compute
  `this_idx` from `(record - storage.data()) / sizeof(waiter_pool_slot)`; `do {
  waiter_pool_storage_[this_idx].free_link.store(head_idx, relaxed); new_head = pack(gen_of_loaded_head,
  this_idx); } while (!CAS(release/acquire))`. Because `free_link` is slot metadata (not the destroyed
  record), the store-after-dtor problem the member shape had (Gate-A-1) does not arise.

**Why the plain-read race needs a PERSISTENT atomic link, not just the tag, and not a record member
(Gate-A BLOCKER, both reviewers).** (a) The generation tag alone makes a stale CAS *fail* but the step-2
*read* of the successor is still a conflicting access with a concurrent slot reuse → UB even when the
CAS later fails. (b) Making the link `atomic` is necessary but INSUFFICIENT if the atomic is a
`waiter_record` member: `release_ref` destroys the record (`:659`) *before* the free-list push, and
`async_lock` reinitializes it via placement-new (`:831`) — so a member atomic's lifetime churns, and
loading/storing it across that churn is UB (atomic construction is itself a non-atomic store; TSan
instruments it as a plain store → SC-003 could go RED). Hosting the link in the slot (constructed once,
mutex-lifetime) removes both the ABA and the reuse race cleanly. Both halves of AM-P1 are addressed.

**Layout-golden safety (verified by construction).**
- `512 × sizeof(waiter_pool_slot) = 512 × 256 = 131072`; golden `131120 = 131072 + 48` (48 B of
  `async_mutex` scalar members). The slot restructure keeps `sizeof(waiter_pool_slot)==256` (checked by
  a `static_assert`), so `sizeof(async_mutex)` is unchanged.
- Retyping `waiter_pool_free_` (8 B → 8 B) adds **no** async_mutex byte.
- `waiter_record` gains no member (the link left the record) → its size only *shrinks-or-equal*; the
  tightened `<= 248` static_assert is the compile-time net (current members — `mutex_`, `next_`,
  `phase_`, `result_`, `attached_awaiter_`, `exec_storage_[64]`, `refcount_`, two fn-ptrs — sum well
  under 248; `exec_storage_` is only 64 B). Exact size verified by the build's static_assert (not a
  blocker).

**Rejected alternatives:** (a) `free_next_` as a `waiter_record` member — the Gate-A BLOCKER above
(lifetime churn / store-after-dtor). (b) type-pun `next_` storage as an atomic-index-while-free — same
lifetime-churn class, worse (aliases a pointer member the chains use); rejected. (c) a parallel
`std::array<std::atomic<uint32_t>,512>` async_mutex member — clean lifetime but +2048 B → breaks the
golden (would need an explicit FR-011 justification); the slot-tail approach preserves the golden and is
preferred. (d) hazard pointers / epoch reclamation — far heavier, unjustified for a fixed 512-slot pool.
(e) a 128-bit `{ptr,tag}` CAS — needs `cmpxchg16b` assumptions + a wider atom → golden risk.

---

## D-2 — AM-P2-1 ordering: release/acquire + documented strand-local drain intent

**Decision (clarified 2026-07-02, belt-and-suspenders):**
- Change the runner's `in_flight_resumers_.fetch_sub(1, relaxed)` (`:583`) → `release`; the drain
  terminal `load(relaxed)` (`:1195-1196`) → `acquire`; and the destructor's new in-flight check (D-3)
  → `acquire`. This establishes the happens-before that makes cross-executor drain-then-destroy
  memory-safe (the runner's free-list push into mutex storage is visible before the drain can observe
  0 and free the mutex). Free on x86 (compiles identically); correct on ARM64.
- **AND** tighten the drain/destructor documentation (contract-delta doc) to state the drain is
  **strand-local by design** — the ordering fix makes cross-executor teardown *safe*, the contract
  text makes the *intended* envelope explicit. The destructor guard (D-3) enforces it.

Update the now-false justification comments at `:263-266` / `:566-569` ("Relaxed: drain-read is
strand-local") to reflect the release/acquire pairing.

---

## D-3 — AM-P2-2 destructor guard extension

**Decision.** Extend `~async_mutex` (`:641-646`) to ALSO `std::terminate()` when
`in_flight_resumers_.load(acquire) != 0`, not only on `state_ != not_locked` /
`next_drain_head_ != nullptr`. **`in_flight_resumers_` is THE load-bearing barrier** — it is
decremented as the runner's LAST statement (`:583`), after every mutex touch, so a non-zero value at
destroy means a runner tail may still dereference the mutex → terminate. This converts the silent
cancel-then-destroy UAF into the loud precondition failure the destructor already promises in debug AND
release. MUST NOT terminate on a correctly drained-then-destroyed mutex (the drain's terminal condition
forces `in_flight_resumers_==0` before returning — `:1195-1196`), so no false positive.

**Gate-A refinement (both reviewers): `active_holders_count_` is NOT a valid teardown barrier — do not
gate the guard on it.** `unlock()` decrements `active_holders_count_` *early* (`:961`), before it
finishes touching `state_`/`draining_`, so a `==0` reading does not prove the mutex is untouched. It is
included at most as a *best-effort* secondary signal, never as the safety property. The genuine
cross-executor granted-holder-vs-drain UAF this exposes is handled by tightening the CONTRACT (out of
scope for safe destruction), not by the guard — see the contract-delta and D-2 note.

---

## D-4 — AM-P2-3 bounded exhaustion counter

**Decision.** Replace the unconditional `waiter_pool_next_.fetch_add(1, acq_rel)` (`:818`) with a
**bounded CAS loop** that refuses to increment past `waiter_pool_capacity_` (512): load, if
`>= capacity` return the `sync_lock_alloc_failed` fail-closed path without incrementing, else
`CAS(cur, cur+1)`. Eliminates the u32 wrap-and-reissue AND the pathological contended-atomic traffic
during an exhaustion storm. Composes with D-1 (the bump allocator is the free-list-empty fallback; both
feed the same slot-index space).

---

## D-5 — AM-P3-1 chain-walk trap

**Decision (Gate-A reconciled with FR-005).** In the two chain-walk `else` arms (`:1001-1003` residual,
`:1056-1058` reversed-LIFO), replace the silent step-past of a `granted` record with a **loud
release-safe trap**: `std::terminate()` (the same idiom as the destructor guard, D-3), guarded by a
debug `assert(false && ...)` for a readable debug message. Rationale (both reviewers): FR-005 requires a
trap in release too; a debug-only `assert` has no NDEBUG witness and `std::unreachable()` is UB-not-a-
trap. The state is **provably impossible today** (Fable re-verified: a record is marked `granted` only
inside the walk that simultaneously unchains it — `:981`/`:1036` + fast-grant; unlock is holder-
serialized; drain/unlock overlap is out of contract), so `terminate` has **no false-fire path**. This
supersedes the earlier "debug-assert + release fallthrough" option, which FR-005 forbids.

---

## D-6 — AM-P3-2 phantom-unlock disarm

**Decision (Gate-A specified — no self-unlocking disarm).** Primary: `assert(attached_awaiter_ != nullptr)`
(debug) + `std::terminate()` (release) for a *granted* record in the null-awaiter arm — the state is
unreachable today (`attached_awaiter_` is only nulled at `:931-934` after handler invoke). **The
defensive `result_` disarm, IF added, MUST neutralize the guard WITHOUT invoking `unlock()`** — i.e.
call the guard's `release()` (which disengages ownership) BEFORE the engaged `async_lock_guard`'s
destructor runs, or overwrite `result_` with an `unexpected` in a way that does not run the engaged
guard's `~async_lock_guard` (`:328-337`, which calls `unlock()`). Failure direction MUST be a *leaked
lock*, never a phantom `unlock()` / mutual-exclusion break (Gate-A: Codex flagged the naïve
overwrite-is-itself-an-unlock hazard; Fable confirmed the leaked-lock direction is the correct one).

---

## D-7 — AM-P1 deterministic witness: a compile-gated test seam (the hard problem)

**Problem.** FR-008/SC-007 require every witness RED against the pre-fix code. AM-P1's ABA window is a
few instructions and TSan-invisible; a blind N-thread stress harness runs GREEN against the *buggy*
code (the window almost never lands) → non-discriminating.

**Decision.** Add a **compile-gated test seam** to `async_mutex.hpp`, zero cost in production:

```cpp
#ifdef FIXPP_ASYNC_MUTEX_TEST_SEAM
    if (detail::async_mutex_test_seam) detail::async_mutex_test_seam(phase);  // default no-op
#endif
```

**TWO seam phases (Gate-A: Fable) — one per HALF of AM-P1:**
- `pop_pre_cas` (between the free-link load and the CAS): pins T1 there while T2 executes pop-pop-push
  of the same head → lands the **ABA (part 1)** every run. RED against tagless code (stale successor
  installs → next allocation aliases a live record), GREEN after the generation bump.
- `pop_pre_link_load` (between the head load and the free-link load): pins T1 there while T2 pops and
  reuses the slot → T1's subsequent link load races the reuse → lands the **plain-read/reuse race
  (part 2)** every run. This is the witness that would have caught the Gate-A-1 BLOCKER: **TSan-RED**
  against the member-`free_next_` shape, **GREEN** with the mutex-lifetime slot-`free_link`. Without
  this phase, part 2 has no discriminating witness (T2's reuse would always follow T1's load).

The witness asserts the discriminating post-condition (no slot returned twice / no parked record
clobbered / exact completion count). Behind `#ifndef FIXPP_ASYNC_MUTEX_TEST_SEAM` the hook compiles to
nothing — no prod branch.

- **ODR (Gate-A: Fable, hard requirement — not a footnote):** `async_mutex.hpp` is header-only with
  many inline out-of-line bodies. A seam-enabled TU linked against ANY non-seam TU that also
  instantiates those bodies is an ODR violation (differing token sequences under the macro). The
  `test_async_mutex_aba_interleave` target MUST be **standalone** — it links no fixpp object/library
  that instantiates `async_mutex` bodies; the macro is defined for that target ONLY. This constraint is
  carried explicitly into plan/tasks.
- **Pre-fix RED runs known UB (Gate-A: Fable):** the pre-fix RED execution of the ABA/reuse phases
  exercises the very races being fixed. Run the pre-fix mutation-RED check on the **non-TSan lane**, or
  document the expected TSan report — do not let it masquerade as an unexpected sanitizer failure.
- **This touches the production header** → Gate-A design decision (logged in Complexity Tracking). It is
  the ONLY production concession.

**Alternative (Gate-A discussion, not primary):** an extracted CDSChecker/GenMC model of the pop/push
cycle proving ABA-freedom exhaustively. Rejected as primary: separate tool/build lane + model-drift
risk (the model can diverge from the header). The seam tests the *real* header. The reasoning argument
(generation bump ⇒ no stale-successor install) accompanies the seam per FR-013.

---

## D-8 — AM-P3-3 OOM disposition: accept documented terminate (asymmetry is inherent, not a bug)

**Decision.** Document terminate-on-OOM at the resume `asio::post` (`:596`) as an **accepted
fail-stop**, consistent with the primitive's terminate-on-contract-violation posture. Rationale: "fail
closed" is NOT available at `:596` — the waiter is already granted and the resume MUST occur; there is
no error channel to return. The trapped slot-assign counterpart (`:857-873`) CAN fail closed only
because it runs **pre-grant** (returns `sync_lock_alloc_failed` before any commitment). So the
asymmetry is inherent to grant-ordering, not a defect. Pre-touching the recycler (the §6.4 warm-up) is
rejected as added warmup complexity for a path that is alloc-free in steady state and unreachable
except under genuine OOM. The contract-delta doc records the accepted terminate + this rationale, which
closes the D-3 "asymmetry claim" the review flagged.

---

## D-9 — Test matrix (full closure, clarified 2026-07-02)

**Convert to genuinely-MT** (real threads, contended arbitration; per FR-008): `test_race_cancel_during_resume`,
`test_race_multi_cancel`, `test_race_cancel_pre_drain`, `test_result_write_race`,
`test_cancellation_mid_wait` (closes T-1/T-2/T-5). **Extend** `test_destructor_release_death` with the
P2-2 shapes (T-4): BOTH the cancel-delivered-then-destroy shape AND the **grant-shaped sibling**
(Gate-A: Fable — owner unlocks-and-destroys inside the runner with the tail pending, NO cancellation;
same UAF, same guard catch). **Add:** the D-7 ABA interleave with BOTH seam phases (`pop_pre_cas` for
AM-P1 part 1, `pop_pre_link_load` for part 2); pool-exhaustion + free-list reuse (T-3, exercises
D-1/D-4); FIFO-across-cycles with arrivals between unlocks (T-7); genuinely-MT drain×cancel×destroy
teardown witnesses for D-2/D-3. **Preserve** `test_async_mutex_layout_golden` and the `no-std-mutex`
gate.

**Anti-enshrinement:** every witness is mutation-tested (revert its target fix → RED) per SC-007;
coverage is measured lcov BRDA/DA on the coverage lane; the 100%-reachable-branch inventory + any
unreachability waivers are enumerated at the **coverage-design gate** (after `/tasks`, Fable scenario
review). AM-P1's branch-coverage does NOT by itself prove correctness (FR-013) — the D-7 seam +
reasoning carry that.

---

## Open items carried to later gates (not blockers)

- Exact `sizeof(waiter_record)` headroom → confirmed by the `:619` static_assert at build (D-1).
- The precise branch inventory + unreachability waivers → the coverage-design gate after `/tasks`.
- D-5/D-6 exact assert-vs-defensive shape → implement-time one-liners, both specified above.
