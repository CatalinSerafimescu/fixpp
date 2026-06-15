# Quickstart — Verifying 038-acceptor-sendingtime-guard

How to witness each group. All cells are in-process (no live counterparty). Group 1 needs a **controllable clock** so SendingTime divergence is deterministic.

## Group 1 — Acceptor first-Logon SendingTime guard (`tests/session/test_acceptor_logon_sending_time.cpp`, NEW)

Harness: an acceptor `Session` over `mock_transport` with an injectable clock (the established-Q3 tests already drive a mock clock — reuse that fixture shape, e.g. `test_sending_time_precision.cpp` / `sending_time_test.cpp`). Set `cfg_.sending_time_threshold = 120 s` (or default).

Reject cells (RED before the guard, GREEN after):
1. **stale-past**: feed an inbound first Logon whose `52` is `now − 10 min`. Assert: session ends `Disconnected`; on-wire `Reject(35=3, 371=52, 373=10)` and **NO Logout frame**; `toAdmin` observed the Reject; next-expected-inbound unchanged; (persistent store) durable inbound counter unchanged.
2. **stale-future**: `52 = now + 10 min`. Same assertions (absolute divergence).
3. **malformed-52**: `52 = "NOTATIME"`. Same assertions (parse failure → reason=10, never silently tolerated).
4. **absent-52**: Logon with no `52` field. Same assertions (per Clarifications, reason=10 — NOT RequiredTagMissing). Assert NO Logout (the negative — distinguishes this from the established-Q3 shape).
5. **empty-52** (distinct from absent): Logon with `52=` (present but empty). The empty-string short-circuits BEFORE `fix_string_to_utc_time` — a different parse path from absent-52, so witness it independently. Same reason=10 disposition + NO Logout.

Defensive / error-branch cells (named witness OR explicit `/speckit-verify`-record waiver per arm — no overclaim, `[[feedback_witness_asserts_named_postcondition_not_proxy]]`):
6. **toAdmin-throws on the new Reject**: register a `toAdmin` that throws → assert `app_callback_threw` + `Disconnected` (INV-3; assert directly, do not proxy a sibling).
7. **assign_outbound failure**: induce `assign_outbound` failure on the Reject path → assert the fail-closed outcome.
8. **build_reject build-failure** → FAIL CLOSED: assert the guard transitions to `Disconnected` (never silent-success) on a `build_reject` failure. Buffer pinned ≥512B (both siblings use `std::array<std::byte,512>` — `:2115`/`:2407`); `[[feedback_fixed_buffer_build_failure_silent_success]]` (the 037 256B-overflow trap). A near-cap RED witness if a CompID-length path can approach the buffer cap.
9. **persistent-store inbound-counter seeded-not-advanced**: with a persistent store seeded to durable inbound `D`, assert the DURABLE inbound counter is unmoved (`== D`) across the rejected Logon AND the in-memory next-expected-inbound is at the **seeded** `D` (NOT advanced, NOT `seqnum_min`) — because `ensure_hydrated_` ran before the guard. Store-type-aware: the memory-store cells (1-5) assert in-memory `next_inbound == seqnum_min`; this persistent cell asserts seeded-`D`-unchanged (a flat `== seqnum_min` would FAIL on the persistent path). Discharges INV-4 directly.

Simultaneous-bad ordering cells (the `52` guard precedes `check_inbound`/`1137`/`authorize_logon` — see data-model "simultaneous-bad ordering"):
10. **bad-52 + bad-1137**: stale `52` AND a non-conformant `1137` → assert the `52` reject wins (`Reject 371=52, 373=10`, no Logout, Disconnected); the `1137` reject is never reached (no `371=1137` on the wire).
11. **bad-52 + bad-credentials**: stale `52` AND credentials that `authorize_logon` would reject → assert the `52` reject wins (public `Reject 371=52`); the silent credential `Disconnected` is never reached (observable change from today).
12. **bad-52 + too-low inbound seqnum**: stale `52` AND a too-low `34` → assert the `52` reject wins (reason=10); the too-low seqnum fatal is never reached (the guard precedes `check_inbound`).

No-regression cell:
13. **conforming**: `52 = now` (within window). Assert: session establishes (`Active`/reply-Logon sent), **emitted output** byte-identical to a pre-feature acceptor establishment with the same inputs (capture-and-compare, or a field-set assertion).

Persistent-reconnect Reject-seq cell (the direct witness that the guard runs AFTER `ensure_hydrated_`):
14. **persistent-store reconnect, durable outbound `N>1`, bad-`52`**: with a persistent store whose durable outbound counter is seeded to `N > 1` (a reconnect), feed a stale/malformed `52`. Assert: the emitted `Reject` carries `34 = N` (the **hydrated** durable outbound seq) — NOT `34 = 1`; the store **accepts** the Reject (no `store_seqnum_out_of_order` / I-05); the durable inbound counter is unchanged. This proves the guard fires after `ensure_hydrated_` (an un-hydrated placement would stamp `34=1`, trip the store `seq == next_seqnum` contract, and put an out-of-sequence Reject on the wire).

Coverage: each new branch (absent / empty / malformed / stale-past / stale-future / conforming / toAdmin-throw / assign-fail / build-fail / inbound-seeded-not-advanced / persistent-reconnect-hydrated-outbound-seq) must hit DA/BRDA in the coverage preset; any defensive arm not reachable carries a verify-record rationale.

Pre-existing acceptor fixtures: any existing acceptor-establishment test that hard-codes a fixed/stale `52` against a real clock will now reject — migrate those to a controllable/current clock (this is expected fixture churn, not a regression; flag at `/speckit-verify`).

## Group 2 — Reconnect callback containment (`tests/session/test_credentials_rotated_emit.cpp`, EXTEND)

Reuse the 014 standalone-FSM injection path (`emit_credentials_rotated_` injected directly). Cell:
- Inject a `credentials_rotated` callback that **throws**; drive a reconnect attempt in which credentials rotated (`snap != last_active_source_`, `rotated == true`). Assert: the throw does not propagate out of `drive_reconnect_attempt`; the attempt reaches its policy outcome (proceeds to `make()`); the baseline (`last_active_fp_`) was updated (so a second attempt does NOT re-emit the same rotation).
- Transparency cell: a non-throwing callback → existing 014 behaviour unchanged.

## Group 3 — FIXT 1137 reject witnesses (`tests/session/test_fixt_logon_establishment.cpp`, EXTEND)

FIXT acceptor; feed an inbound first Logon with (a) `1137` absent, (b) `1137` non-conformant. Assert for each: on-wire `Reject(35=3, 371=1137)` (`373=1` / `373=5`), `toAdmin` observed, `Disconnected`, no establishment. Confirm `git diff -- src/` is empty for this group (test-only).

## Sanitizers / verify

`/speckit-verify` runs the 6-preset Tier-1 matrix (one at a time): debug, release, asan, ubsan, tsan, coverage. Touched suites: the new `test_acceptor_logon_sending_time` + the extended credentials-rotated and FIXT-establishment suites, plus the session regression suites. No bench/fuzz/abidiff impact (no parser/ABI/bench surface).
