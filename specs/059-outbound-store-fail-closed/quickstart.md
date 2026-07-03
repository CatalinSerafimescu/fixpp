# Quickstart — verifying 059 (outbound store fail-closed)

This is the "how to verify" recipe (Art. XVI §5 CI evidence). Three witnesses map 1:1 to the user stories; the P1 witness is proven **RED on `main`** before the fix.

## W1 — US1 / SC-001+SC-002: persistent fail-closed + no silent freeze (the flagship)

**Setup:** a session with a `FileStore` (persistent). Arm the new `FIXPP_TEST_HOOKS` seam to fail the **next** `store()` pwrite once. Peer role available to send a `ResendRequest`.

**RED (seam present, disposition fix absent — MUST fail for the *cascade* reason, not a miswired seam; V5):**
0. Assert the seam fired (a `FIXPP_TEST_HOOKS` probe count) so the RED is provably the cascade, not a no-op seam (`feedback_fail_placeholder_red_test`).
1. Send app messages up to seq `k-1` (all retained).
2. Arm the seam; send message `k` → store fails, but (pre-fix) the frame is transmitted and the failure swallowed; session stays `Active`.
3. Disarm; send `k+1 … k+n` → each `store()` now fails the order check and is swallowed; session still `Active`.
4. Peer sends `ResendRequest[k, k+n]`.
5. **Assert (these hold on `main`, proving the defect):** the replay for `k … k+n` is a `SequenceReset-GapFill` (real app messages lost); the durable outbound counter on disk is stuck at `k`; after a simulated restart, `ensure_hydrated_` recovers `next_outbound == k`, below the peer's last-seen `k+n`.

**GREEN (post-fix — MUST pass):**
- At step 2 the session transitions to `Disconnected` and frame `k` is **not** transmitted (assert the transport saw nothing for `k`).
- The freeze cascade (steps 3–5) is unreachable — the session is already down.
- After a restart, durable counter `== k` and peer-last-seen `== k-1` agree (no re-stamp below peer-seen). (SC-002.)

## W2 — US2 / SC-003: volatile store unchanged

**Setup:** a session with a **bounded** `MemoryStore` (volatile) filled to its configured outbound capacity (no mock needed — the next `store()` returns `store_capacity_exhausted`).

**Assert:** on the capacity failure the session does **not** disconnect and behaves byte-for-byte as `main` (logged-then-proceed; transmit proceeds). Existing volatile-store tests remain green. (FR-003.)

## W3 — US3 / SC-004: reconcile-from-durable on reconnect (three policy variants)

**Setup:** `FileStore` session; drive one persistent `store()` failure (W1 step 2) to fail closed; **disarm** the seam (fault cleared).

**Assert (at disconnect time, before any reconnect):** `peek_outbound() == durable_k` — the fatal branch reconciled the wire counter down via `set_next_outbound` (the reconcile is done in `store_then_emit`, not deferred to the reconnect). **Assert inbound sequencing is unchanged** (the manager's inbound counter is untouched — this is the trap the original finding is about: no prior test combined the store-failure + reconnect + policy conditions).

**Variant A — plain persistent session** (no reset policy): trigger an in-process reconnect; the next send stores + transmits at seq `k` — **no repeating disconnect**, no gap, no reuse. (FR-007.)

**Variant B — `bilateral_strict`:** after the same fail-closed + reconcile, the reconnect Logon `reset_to_one()` overrides the outbound counter to 1; assert the reconnect Logon is well-formed (seqnum 1, no INV-RoL-3 malformed-Logon) and inbound sequencing is correct — i.e. the reconcile did **not** re-hydrate inbound or bypass the strict-reset suppression.

**Variant C — `reset_on_logon`:** same as B — the reset path owns the post-reconnect state; assert clean resume.

Exercise on the initiator role (drives the reconcile directly); note the reconcile is role-independent (done in `store_then_emit`, before any reconnect), so the acceptor path is covered by the shared code.

## Cross-cutting gates (at `/speckit-verify`)

- **Sanitizers:** ASan/UBSan/TSan Tier-1 green on the new tests. The store path is `async_mutex`-guarded and strand-confined; use a `thread_pool(≥2)` harness for the store-failure tests (`feedback_single_threaded_harness_masks_strand_races`).
- **Coverage:** the new fatal branch and BOTH `store_is_persistent_` arms carry discriminating, mutation-tested witnesses; no uncovered error/edge branch without a recorded assessment (Art. IX §1).
- **Conformance:** TC-001..017 stay green (no session-FSM regression).
- **No hot-path perf delta:** happy path unchanged; state in the verify doc (no bench required).

## Build/run (fill exact preset at `/tasks`)

```bash
cd research/G19-fix-fpml-iso20022/library
conan install . -of build/<preset> --build=missing
cmake --preset <preset> -DFIXPP_TEST_HOOKS=ON
cmake --build build/<preset> -j2 --target test_store_fail_closed_persistent \
      test_store_fail_open_volatile test_store_fail_reconnect_reconcile
ctest --test-dir build/<preset> -R 'store_fail' --output-on-failure
```
