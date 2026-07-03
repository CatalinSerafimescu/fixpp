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

## W3 — US3 / SC-004: reconcile-from-durable on reconnect (three HONEST policy variants)

**Setup:** `FileStore` session; drive one persistent `store()` failure (W1 step 2) to fail closed via `Session::send` (broadened guard → `Disconnected`); **disarm** the seam (fault cleared).

**Assert (at disconnect time, before any reconnect):** `peek_outbound() == durable_k` — the fatal branch reconciled the wire counter down via `set_next_outbound` (done in `store_then_emit`, not deferred to the reconnect). **Assert inbound sequencing is unchanged** (the manager's inbound counter is untouched — this is the trap the original finding is about: no prior test combined the store-failure + reconnect + policy conditions).

**Variant A — plain persistent session** (no reset knob, not `bilateral_strict`): trigger an in-process reconnect; the next send stores + transmits at seq `k` — **clean resume**, no repeating disconnect, no gap, no reuse. (FR-007.)

**Variant B — `reset_on_logon`:** the durable reset (`reset_seqnums_to_one_durable`, gated on `cfg_.reset_on_logon`, `session.cpp:776-782`) runs before the reconnect Logon and overrides the outbound counter to 1; the reconcile is neutral. Assert the reconnect Logon is `34=1` + `141=Y`, well-formed, and inbound sequencing is correct.

**Variant C — `bilateral_strict` (the DEFAULT policy):** there is **NO** durable reset (`bilateral_strict` only forces `141=Y`, `session.cpp:816-818`). After the same fail-closed + reconcile at `k>1`, assert the reconnect Logon carries `34=k` (k>1) **with** `141=Y` — the **pre-existing, deferred L-029-3** malformed-Logon (`behaviors-and-limitations.md:1252-1265`). The point of this variant is a **regression guard**: assert 059 does **not worsen** L-029-3 — the reconciled `durable_k` and an un-reconciled `k+1` are *both* non-1, so the malformed Logon is the pre-existing limitation, not a 059-introduced defect. Do **NOT** assert clean recovery here.

**Roles.** The reconcile is role-independent (done in `store_then_emit`). Reconnect **reset**, however, reaches through different sites per role: initiator via `reset_on_logon` (`:776`), acceptor via received-`141=Y` / inbound-Logon (`session.cpp:2141`, `:2414`). Exercise the initiator role directly; for the acceptor path either add an acceptor-side reset witness or record a sourced exclusion at `/tasks` (its reconnect-reset entry point differs from the initiator's).

## Cross-cutting gates (at `/speckit-verify`)

- **Sanitizers:** ASan/UBSan/TSan Tier-1 green on the new tests. The store path is `async_mutex`-guarded and strand-confined; use a `thread_pool(≥2)` harness for the store-failure tests (`feedback_single_threaded_harness_masks_strand_races`).
- **Coverage:** the new fatal branch and BOTH `store_is_persistent_` arms carry discriminating, mutation-tested witnesses; no uncovered error/edge branch without a recorded assessment (Art. IX §1). **Flag for the `/tasks` coverage-design gate — two hard-to-witness arms** (`feedback_coverage_design_gate_ex_ante_vs_measured`): (a) the `store_cancelled`-on-a-persistent-store branch (disposition-table row 2) is only reachable on shutdown drain — needs a discriminating witness or a recorded assessment; (b) the reconcile "durable read **failed** → skip reconcile, still fail closed" *else*-arm (D4 step 2) has no obvious witness recipe — same treatment. Also cover the `Session::send` guard broadening: assert an app-veto (`app_do_not_send`) still leaves the session `Active` (must not be caught by the widened predicate).
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
