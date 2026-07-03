# Phase 0 Research — Outbound store-failure disposition (059)

All line numbers verified against the current tree on `main` (submodule HEAD `f525446`), 2026-07-03.

## Grounded facts (the mechanism, re-confirmed)

- **Two independent counters.** The wire counter (`SeqnumManager::next_outbound_`) is advanced by `assign_outbound()` in `send_impl` (`session.cpp:~4478`) **before** `store_then_emit` runs. The store keeps its **own** next-expected counter, advanced only on a fully successful `store()` (`memory_store.hpp:196`; `file_store.cpp` Region 3, skipped on `!io_ok`).
- **The swallow.** `store_then_emit` (`session.cpp:4780-4801`) does `auto store_r = co_await store_->store(...); (void)store_r;` (`:4790`). The store returns `store_io_failure` / `store_seqnum_out_of_order` / `store_capacity_exhausted` as `std::unexpected` — **returned, not thrown** — so the surrounding `try/catch` (which only catches `asio::system_error{operation_aborted}`) never sees them. The frame is then transmitted unconditionally in Step 2 (`:4817-4834`).
- **Retain-before-transmit (I-3).** Step 1 (store) precedes Step 2 (transmit). A `co_return` in the store-failure branch skips the transmit.
- **The asymmetry (S-P2-1).** `persist_inbound_advance_` (`session.cpp:699-709`) and `persist_outbound_advance_` (`:716-726`) each do: `if (!r) { record_state_transition_(fsm_state::Disconnected); co_return std::unexpected(error::store_io_failure); }` — fatal-when-persistent, gated on `store_is_persistent_`. The outbound **frame** store on the send path is the odd one out (logged-then-proceed).
- **`store_is_persistent_`** is set once at `open()` (`session.cpp:1154`) from `cfg_.store_factory->yields_persistent_store()` — `false` for `MemoryStore`, `true` for `FileStore`; null-store path leaves it `false`. Single reliable capture point covering both roles.
- **Rehydrate on reconnect.** `ensure_hydrated_` (`session.cpp:639-693`) early-returns when `hydrated_ && !force` (`:642-645`). It is called on reconnect by **both** roles: initiator via the shared `emit_initiator_logon_` emit point (`:761`) and acceptor via the inbound-Logon handler (`:2062`), each with `force = refresh_on_logon && !bilateral_strict` (default `false`). It re-applies **both** the inbound seed (gated by `apply_inbound_seed`) and the outbound seed via `seqnum_mgr_.hydrate(...)` — which is why forcing it (by clearing the latch) has cross-feature blast radius; see D4.
- **Targeted outbound reseed exists.** `SeqnumManager::set_next_outbound(n)` (`seqnum_manager.hpp:135-141`) forces the outbound counter only — minted for the 032 initiator restore. `peek_outbound()` (`:91`) reads it synchronously. `reset_to_one()` (`:118-124`) resets **both** counters to 1 for a reset-Logon (the `bilateral_strict` / `reset_on_logon` path), overriding any prior outbound value.
- **`store_cancelled` is shutdown-only.** Returned on the `store()` path exclusively when `async_lock()` is cancelled/drained (`memory_store.hpp:145`, `file_store.cpp:1028`) — see D7.

---

## D1 — Disposition: fatal-when-persistent (Option A), NOT counter-reconcile (Option B)

**Decision.** On `!store_r` in `store_then_emit`: if `store_is_persistent_` → fail closed (transition `Disconnected`, propagate the error, do not transmit). Else → today's `(void)store_r;` logged-then-proceed.

**Rationale.**
- Symmetric with the already-fatal durable counter-write paths (`persist_*_advance_`) — same durable-write class, now the same disposition. Removes the S-P2-1 asymmetry that is the root of the S-P1-1 freeze.
- Constitutionally mandated: Art. XV §15 forbids silent app/session message loss and names `disconnect-and-recover` (disconnect → `ResendRequest` on reconnect) as the sanctioned mode. The current swallow is precisely the banned silent-desync.
- Because store precedes transmit, fail-closed = **disconnect-before-transmit**: the un-retained seqnum `k` never reaches the wire, so the peer's last-seen (`k-1`) stays consistent with the durable store counter (`k`) — the restart/reconnect desync (blast #2) is eliminated at the source.

**Alternatives rejected.**
- **Option B — reconcile the store counter up to the assigned seq** (accept `k`, advance the store past the un-written bytes). *Rejected:* for the `store_io_failure` sub-case this is actively wrong — it would advance the store's counter over bytes that were never written, so a later `retrieve(k)` returns a hole/garbage while the store *claims* to hold `k`. Reconcile only makes sense for a pure order-check rejection, not a write fault, and the disposition must not depend on distinguishing them (FR-004). Fail-closed is uniformly correct across all three error classes.
- **Option C — keep swallowing but log louder.** *Rejected:* does not fix the freeze or the restart desync; the whole point is that "advertise a retention guarantee you can't honour" is the defect.

## D2 — Error code: propagate the store's own error, no new code

**Decision.** `co_return std::unexpected(store_r.error())` — surface the store's actual error (`store_io_failure` / `store_seqnum_out_of_order` / `store_capacity_exhausted`).

**Rationale.** Symmetric with `persist_*_advance_` (which return `store_io_failure`); preserves the diagnostic distinction for logging; introduces **no new error code** (A-4). The existing `operation_aborted` throw-path keeps returning `dispatch_aborted` unchanged (D5).

**Alternative rejected.** Coercing every store failure to `dispatch_aborted` — loses the diagnostic and conflates cancellation with I/O fault.

## D3 — Location: the disposition lives INSIDE `store_then_emit`

**Decision.** Do the reconcile (D4) + `record_state_transition_(fsm_state::Disconnected)` + `co_return unexpected` **inside** `store_then_emit`, not at each call site.

**Rationale.** `store_then_emit` has ~two-dozen callers; most propagate `emit_r`, but two are best-effort `(void)co_await store_then_emit(...)` (ResendRequest-reject `session.cpp:2742`; a Logout emit `:3233`). Putting the fatal transition inside the callee makes fail-closed **caller-independent**: even a best-effort caller that ignores the return still leaves the session `Disconnected` on a persistent failure. This is what makes FR-006 hold without editing every call site. (`persist_*_advance_` use the identical "callee owns the `Disconnected` transition" shape, so double-transition-to-`Disconnected` is already a tolerated idempotent case.)

**Consequence for the census (FR-006 / SC-006).** The `/tasks` census does not need to add a return-check at every site; it needs to confirm each best-effort site *tolerates* a mid-flow `Disconnected` (i.e. does not, after the swallowed emit, resume `Active` or emit further frames assuming success). For the Logout path the session is already tearing down (disconnect is redundant-safe); for the ResendRequest-reject path, failing closed when we cannot even store the reject is correct. Both are verified by reading each site's post-emit continuation.

## D4 — Reconcile-from-durable (FR-007 / US3): targeted outbound-only reseed via `set_next_outbound(durable_k)` — NOT clearing `hydrated_`

**Decision.** In the fatal branch, before the `Disconnected` transition, **best-effort** reconcile the in-memory outbound wire counter to the durable store counter:
1. `auto dk = co_await store_->next_seqnum(direction_t::outbound, /*increment=*/false)` — read the durable outbound counter (unchanged on a `store_io_failure`; the true value on an out-of-order/capacity failure).
2. if `dk` has a value: `co_await seqnum_mgr_.set_next_outbound(*dk)` — the **existing** outbound-only setter (`seqnum_manager.hpp:140`, minted for the 032 initiator `peer_ack_sent_reset_flag` restore). If the read fails (e.g. cancellation), skip the reconcile — the disconnect still happens (degrades to the safe disconnect-loop).

Do **NOT** clear `hydrated_`.

**Rationale.** After fail-closed the manager sits at `k+1` (assign_outbound already ran) while durable is `k`. A direct outbound-only reseed to the durable value fixes exactly the desync, is fully self-contained in `store_then_emit`, and is **policy-agnostic**: for a plain persistent session the reconnect Logon then uses `k` (correct); for `bilateral_strict` / `reset_on_logon` the reconnect's `reset_to_one()` overrides our `k` back to 1 (harmless). It touches **only** the outbound counter — never inbound, never the Logon-seed path. Rewinding to `k` is safe precisely because seq `k` was never transmitted (D1), and `set_next_outbound` is already a sanctioned restore operation.

**Why NOT clear `hydrated_` (the rejected first design; advisor-flagged blast radius).** Clearing the one-shot latch forces `ensure_hydrated_` to re-run on the next reconnect **regardless of `force`** — which (a) **bypasses the `bilateral_strict` suppression** `refresh_active = refresh_on_logon && !bilateral_strict` whose absence re-introduces INV-RoL-3 (a non-1 outbound seed into a `bilateral_strict` reset-Logon → malformed Logon), and (b) **re-applies the inbound seed** (gated by `apply_inbound_seed`), pulling unrelated inbound sequencing into a store-failure recovery. The targeted reseed avoids both by construction.

**Alternatives rejected.**
- **Clear `hydrated_` (+ a `bilateral_strict` guard).** *Rejected:* couples the fix to the reconnect re-hydration machinery's per-policy guards and the inbound-seed logic; larger cross-feature blast radius than a one-counter reseed.
- **Compute `k = stamped_seq` instead of reading durable.** *Rejected:* only valid for the `store_io_failure` sub-case; reading the durable counter is uniform across all failure classes (FR-004).

**Verification note (US3 both roles + both policies).** The reconcile happens at disconnect time, so `peek_outbound() == durable_k` is assertable immediately, before reconnect. W3 MUST include a plain-session variant (reconnect resumes at `k`) **and** a `bilateral_strict` variant **and** a `reset_on_logon` variant, asserting outbound resumes correctly **and inbound sequencing is unaffected** (the trap the original finding is about — no test combined the conditions). The acceptor shares `ensure_hydrated_` but the reconcile itself is role-independent (it is done in `store_then_emit`, before any reconnect).

## D7 — `store_cancelled` is cancellation-class: excluded from fail-closed (FR-005 parity)

**Decision.** In the failure branch, `store_cancelled` is handled **before** the persistence gate and keeps today's behaviour (absorb → proceed); it never trips fail-closed or the reconcile. Only the genuine retain-failure classes (`store_io_failure`, `store_seqnum_out_of_order`, `store_capacity_exhausted`) fail closed on a persistent store.

**Rationale.** `store_cancelled` is returned **only** when the store's `async_lock()` is cancelled/drained — the shutdown path (`memory_store.hpp:145`, `file_store.cpp:1028`; FileStore explicitly "does NOT surface `store_cancelled` under graceful close", `file_store.hpp:171`). It is the *returned* twin of the *thrown* `operation_aborted` that D5/FR-005 deliberately excludes. Failing closed + reconciling on a normal drain would be wrong (spurious disconnect/counter-rewind during shutdown). Excluding it cannot mask a genuine retain failure because it is unreachable off the cancellation path (source-verified: two `store()`-path return sites, both `!async_lock()`).

## D5 — Preserve the `operation_aborted` cancellation path verbatim (FR-005)

**Decision.** Leave the existing `try { ... } catch (const asio::system_error& e) { if (operation_aborted) co_return dispatch_aborted; ... }` exactly as-is. The new logic is a check on the **returned** `store_r` (a non-throwing value), placed where `(void)store_r;` is today — inside the `try`, before Step 2. A `co_return` from inside the `try` is well-formed.

**Rationale.** Cancellation (shutdown) is not a durable retain failure and must keep its current `dispatch_aborted` disposition; conflating them would spuriously trip the fatal path during normal close. `feedback_reset_cancellation_state_wipes_pre_entry_cancel` and the F5 drift history (`feedback_async_mutex_us3_asio_cancel_and_subagent_seams`) both warn against disturbing this window.

## D6 — Test infrastructure

**Decision.** Two mechanisms, each matched to the leg it proves:

1. **FileStore `store()`-pwrite fault-injection seam (new).** A `FIXPP_TEST_HOOKS`-gated hook (an `std::atomic<bool>` "fail next store pwrite" arm, mirroring `g_force_abort_after_reset_lambda` / `g_post_rename_reopen_fail_hook`) forces one `raw_pwrite_all` in `store()` to return `false` → `io_ok=false` → `store_io_failure`. This is the **only** way to drive the real durable path incl. the on-disk counter and the restart-recovers-below-peer-last-seen leg (SC-001 blast #2, SC-002 restart). Compiled unconditionally with the declaration gated in `file_store.hpp` (the established pattern: a gated *increment/call* would be unreachable from library-linked tests — `feedback_ci_gate_observes_not_asserts...` cousin).
2. **Bounded MemoryStore at capacity (no new infra).** `store_capacity_exhausted` is naturally reachable by filling a bounded MemoryStore — this drives the **volatile-unchanged** arm (US2 / FR-003 / SC-003) without any mock.

**Rationale.** The FileStore seam is load-bearing for the P1 flagship blast (durable counter + restart). The volatile arm needs no mock. This keeps the infra minimal and avoids a general-purpose mock store whose fidelity would itself need review.

**Alternatives considered.**
- **A general GoogleMock `MessageStore`.** *Deferred/rejected as primary:* useful to isolate `store_then_emit`'s branch from disk, but it cannot exercise the durable on-disk counter / restart leg, which is the whole P1 amplification. If a pure-unit isolation of the disposition branch is wanted, a minimal fake persistent store (yields_persistent_store()==true, arm-to-fail) may be added at `/tasks` — decided there, not a plan blocker.
- **`errno`/`ENOSPC` real-disk injection (e.g. tmpfs size cap).** *Rejected:* flaky, platform-specific, slow; the in-code seam is deterministic and matches the repo's established fault-injection idiom.

**RED-first mandate.** Per `feedback_fail_placeholder_red_test` + `feedback_sanitizer_canary_must_be_proven_red`, the SC-001 cascade witness is run against **current `main`** (pre-fix) and must FAIL (post-`k` loss + stuck durable counter + restart-below), captured as evidence, before the fix makes it GREEN.

---

## Verify-at-implement (advisor items 3–5; not design blockers)

- **V3 — idempotent double-transition.** The callee-owned `record_state_transition_(Disconnected)` (D3) means the *common* propagating-caller path sets `Disconnected` twice (callee + caller). Confirm `record_state_transition_` is idempotent for `Disconnected→Disconnected` (no repeated teardown / duplicate reconnect-reschedule / duplicate event); if not, guard the internal transition with an already-`Disconnected` check.
- **V4 — reconnect parity.** Confirm a `record_state_transition_(Disconnected)` originating in `store_then_emit` drives the **same** downstream teardown + reconnect scheduling as `persist_*_advance_` (it uses the identical call, so parity is expected) — so US3's in-process reconnect actually fires from a send-path-originated disconnect.
- **V5 — RED-for-the-right-reason.** The FileStore seam is new this feature, so "RED on `main`" means *seam present, disposition fix absent*. The W1 cascade must fail for the **cascade** reason (post-`k` loss + stuck durable counter), not a miswired seam (`feedback_fail_placeholder_red_test`). Assert the seam fired (a probe count) before asserting the cascade.

## Open items handed to `/tasks` (not blockers)

- Final test file names/dirs vs the existing `tests/` convention; same-name `#ifdef` twin for any OS-specific seam code.
- Whether to add a minimal fake persistent store for a pure-unit disposition test in addition to the FileStore seam.
- Exact ordering of the reconcile read + `set_next_outbound` + `record_state_transition_` in the fatal branch (reconcile is best-effort and precedes the transition; all inside the existing `try` so a cancellation during the reconcile read is absorbed).
