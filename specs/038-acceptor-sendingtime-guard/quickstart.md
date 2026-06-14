# Quickstart — Verifying 038-acceptor-sendingtime-guard

How to witness each group. All cells are in-process (no live counterparty). Group 1 needs a **controllable clock** so SendingTime divergence is deterministic.

## Group 1 — Acceptor first-Logon SendingTime guard (`tests/session/test_acceptor_logon_sending_time.cpp`, NEW)

Harness: an acceptor `Session` over `mock_transport` with an injectable clock (the established-Q3 tests already drive a mock clock — reuse that fixture shape, e.g. `test_sending_time_precision.cpp` / `sending_time_test.cpp`). Set `cfg_.sending_time_threshold = 120 s` (or default).

Reject cells (RED before the guard, GREEN after):
1. **stale-past**: feed an inbound first Logon whose `52` is `now − 10 min`. Assert: session ends `Disconnected`; on-wire `Reject(35=3, 371=52, 373=10)` and **NO Logout frame**; `toAdmin` observed the Reject; next-expected-inbound unchanged; (persistent store) durable inbound counter unchanged.
2. **stale-future**: `52 = now + 10 min`. Same assertions (absolute divergence).
3. **malformed-52**: `52 = "NOTATIME"`. Same assertions (parse failure → reason=10, never silently tolerated).
4. **absent-52**: Logon with no `52`. Same assertions (per Clarifications, reason=10 — NOT RequiredTagMissing). Assert NO Logout (the negative — distinguishes this from the established-Q3 shape).

No-regression cell:
5. **conforming**: `52 = now` (within window). Assert: session establishes (`Active`/reply-Logon sent), output byte-identical to a pre-feature acceptor establishment with the same inputs (capture-and-compare, or a field-set assertion).

Coverage: each new branch (absent / malformed / stale / conforming / build-fail / assign-fail) must hit DA/BRDA in the coverage preset; defensive build-fail arms reuse the established-Q3 `std::unexpected` shape and carry a verify-record rationale if not reachable.

Pre-existing acceptor fixtures: any existing acceptor-establishment test that hard-codes a fixed/stale `52` against a real clock will now reject — migrate those to a controllable/current clock (this is expected fixture churn, not a regression; flag at `/speckit-verify`).

## Group 2 — Reconnect callback containment (`tests/session/test_credentials_rotated_emit.cpp`, EXTEND)

Reuse the 014 standalone-FSM injection path (`emit_credentials_rotated_` injected directly). Cell:
- Inject a `credentials_rotated` callback that **throws**; drive a reconnect attempt in which credentials rotated (`snap != last_active_source_`, `rotated == true`). Assert: the throw does not propagate out of `drive_reconnect_attempt`; the attempt reaches its policy outcome (proceeds to `make()`); the baseline (`last_active_fp_`) was updated (so a second attempt does NOT re-emit the same rotation).
- Transparency cell: a non-throwing callback → existing 014 behaviour unchanged.

## Group 3 — FIXT 1137 reject witnesses (`tests/session/test_fixt_logon_establishment.cpp`, EXTEND)

FIXT acceptor; feed an inbound first Logon with (a) `1137` absent, (b) `1137` non-conformant. Assert for each: on-wire `Reject(35=3, 371=1137)` (`373=1` / `373=5`), `toAdmin` observed, `Disconnected`, no establishment. Confirm `git diff -- src/` is empty for this group (test-only).

## Sanitizers / verify

`/speckit-verify` runs the 6-preset Tier-1 matrix (one at a time): debug, release, asan, ubsan, tsan, coverage. Touched suites: the new `test_acceptor_logon_sending_time` + the extended credentials-rotated and FIXT-establishment suites, plus the session regression suites. No bench/fuzz/abidiff impact (no parser/ABI/bench surface).
