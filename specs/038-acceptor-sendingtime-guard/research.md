# Phase 0 Research — 038-acceptor-sendingtime-guard

Ground truth for the three groups. Reference engines under `reference-engines/` (QuickFIX-cpp v1.16.0, QuickFIX-J 3.0.1); fixpp call sites on submodule branch `038-acceptor-sendingtime-guard` (off `main` @ `795cde1`).

## D-1 — Reference engines validate inbound SendingTime latency role-agnostically on the Logon (Group 1)

**QuickFIX-cpp** (`src/C++/Session.cpp`): the SendingTime latency check lives in `Session::next(const Message&, ...)` — the single inbound dispatch entry, reached for EVERY inbound message including the establishing Logon, BEFORE message-type routing:

```cpp
// Session.cpp ~:986-1000
const SendingTime &sendingTime = FIELD_GET_REF(header, SendingTime);
...
if (!isGoodTime(sendingTime)) {
  doBadTime(msg);
  return false;            // processing halted — Logon NOT processed/established
}
if (!isCorrectCompID(...)) { doBadCompID(msg); return false; }
```

`doBadTime` (`:1073-1075`): `generateReject(msg, SessionRejectReason_SENDINGTIME_ACCURACY_PROBLEM /*=10*/); generateLogout();` — Reject **then** Logout, then `next()` returns false → the caller disconnects. Note QFcpp checks SendingTime **before** CompID; missing `52` throws `FieldNotFound` and is caught lower as `REQUIRED_TAG_MISSING` (reason=1).

**QuickFIX-J** (`quickfixj-core/.../Session.java`): identical structure — `if (!isGoodTime(msg)) { doBadTime(msg); }` (`:1821-1822`), `doBadTime` at `:1896`, `isGoodTime` at `:1910` (early-returns `true` when `!checkLatency`).

**Conclusion**: the acceptor MUST validate inbound-Logon SendingTime; on failure → reject (SendingTime-accuracy, reason=10), session NOT established. Verified against source, **not** symmetry-derived. **Caveat on shape**: QFcpp/QFJ `doBadTime` emit Reject **+ Logout** (their `doBadTime` is shared across all states via `next()`). fixpp takes the *MUST-reject* fact from QF but the *frame shape* from its own in-arm pre-establishment precedent (the `1137` reject — D-3), which emits **no Logout**. This is the same internal-consistency-over-QF-fidelity choice as the reason-code disposition (D-2).

## D-2 — Missing-`52` is NOT caught upstream; the established-Q3 lumps empty→reason=10 (Group 1)

- `interpret_logon` (`admin_messages.cpp:226`) — the acceptor first-Logon validator — comment: *"We skip 34, 52, 98 for this validation step."* It validates BeginString(8) / CompIDs(49/56) / HeartBtInt(108) only. It never extracts or checks `52`.
- The steady-state Q3 guard (`session.cpp:2381`) is in the `LogonReceived/Active` arm; the acceptor `NotConnected` first-Logon arm establishes and returns before reaching it.
- ⇒ At the acceptor first-Logon, an absent/empty, malformed, or stale `52` is currently **unvalidated** — the session establishes regardless. (Spec's original "missing-52 handled upstream" assumption was falsified here.)
- The established-Q3 block (`:2381-2398`) already treats **empty OR parse-failure OR out-of-range** uniformly: `sending_time_ok` starts false, stays false on empty and on parse-failure, and the `!sending_time_ok` path fires `reason=10`. So mapping absent/empty → reason=10 at the first-Logon site is **internally consistent** with fixpp's established path.

**Decision (Clarifications 2026-06-14)**: absent/empty, malformed, and stale all → single `Reject(reason=10, RefTagID=52)` + Disconnect (no Logout — see D-3 for the shape). Documented divergence from QFcpp/QFJ (which split absent → `RequiredTagMissing=1`) in favour of internal consistency. (Alternative — split missing→reason=1, or add upstream required-field validation — rejected: a second disposition for the same defect, and a wider establishment-validation touch.)

## D-3 — Reject + Disconnect, NO Logout; scaffold on the in-arm `1137` reject (Group 1 shape + design)

**The correct emit shape is `Reject(35=3, 371=52, 373=10)` → `Disconnected`, with NO Logout.** Two sibling emit blocks exist:

- **Established-Q3** (`session.cpp:2400-2459`): Reject(371=52/373=10) → `fire_to_admin_` → **Logout** → `fire_to_admin_` → `Disconnected`. This runs in the *already-established* state, where the Logout is the graceful teardown of a **live** session.
- **In-arm `1137` reject** (`session.cpp:2102-2136`, the SAME `NotConnected` first-Logon arm): `Reject(35=3, 371=1137)` → `assign_outbound` → `fire_to_admin_` → `store_then_emit` → `Disconnected`, **NO Logout**.

The first-Logon SendingTime guard is pre-establishment, so the `1137` reject is the nearest precedent. Mirroring established-Q3's Logout would contradict the in-arm convention; the same internal-consistency principle that fixes the reason code (D-2) fixes the shape — pre-establishment rejects do not Logout. **Decision: scaffold Group 1 on the `1137` block** — it also proves `build_reject` + `assign_outbound` + `fire_to_admin_` + `store_then_emit` work pre-establishment, removing any doubt that the outbound machinery is ready before the session establishes.

**Inline, not extract-a-helper.** A single new caller does not justify a shared helper (`[const §XV]` simplicity; Karpathy "no abstractions for single-use"), and extracting would re-point a merged path (the seam-reorder trap `[[feedback_seam_removal_profile_gate_ordering]]`). Inline-replicate the `1137`-shaped block with `371=52`/`373=10`; leave both the `1137` reject and established-Q3 untouched. The existing `emit_session_reject_` helper (`:1729`) is Reject-only and does not carry the distinct `371`/`372` fields inline, so it is not reused here (the `1137` path is itself kept inline for the same reason — see its `:2106-2108` comment).

## D-4 — No inbound persist on the reject path (Group 1 invariant)

The first-Logon reject path must NOT call `persist_inbound_advance_`. The session never establishes; persisting an inbound advance for a rejected establishing Logon would push the durable inbound counter past what was legitimately consumed (`[[feedback_unconditional_persist_at_multiexit_gate_breaks_lowerbound]]`). Only the OUTBOUND counter advances — one `assign_outbound()` for the emitted Reject — and `store_then_emit` persists that OUTBOUND reject frame (the `1137` block does this with I-07 logged-then-proceed on store error); persisting an OUTBOUND frame is orthogonal to the inbound-counter invariant. Witnessed directly: assert the manager's inbound next-expected is unchanged across the rejected Logon (and, with a persistent store, the durable inbound counter too).

## D-5 — Guard ordering (Group 1)

The SendingTime check fires **after** `interpret_logon` (CompID/BeginString/HeartBtInt) and the FIXT `1137` validation succeed, **before** the session establishes (before the reply Logon at `session.cpp:2280` and before any inbound persist). This mirrors fixpp's established-Q3 ordering (Guard-2 CompID → Guard-3 SendingTime). It differs from QFcpp's `next()` order (SendingTime before CompID); the observable difference is confined to the simultaneously-bad-CompID-AND-bad-`52` edge (which reason fires first), where internal consistency with the established path wins. CompID/BeginString mismatch already disconnects in `interpret_logon`, so a bad-CompID Logon never reaches the SendingTime guard — no double-emit.

## D-6 — Group 2: coroutine-body throw goes to the promise, not `terminate`

`ReconnectFsm::drive_reconnect_attempt` is a `noexcept` `asio::awaitable<expected_t<void>>` (`reconnect_fsm.cpp:104`). `emit_credentials_rotated_(...)` is invoked bare at `:200`. An exception escaping a coroutine **body** is delivered to the coroutine promise's `unhandled_exception()` (and surfaces at the awaiting `co_spawn` completion), NOT to `std::terminate` — so this is **not** the noexcept-*method* terminate class of `[[feedback_noexcept_boundary_user_callback_terminate]]` (033 FQ-2, which was a `std::function` called from a non-coroutine `noexcept` method). The defect is graceful-degradation: a throwing user callback abandons the in-flight reconnect attempt. Fix: `try { emit_credentials_rotated_(...); } catch (...) { /* contain; the rotation notification is best-effort */ }` matching the established `authorize_logon` callback-guard shape — the attempt then proceeds to its policy outcome. SC-free (no measurable user-facing guarantee; unit-test-backed). **To confirm during data-model**: the exact post-catch continuation (the `rotated` notification is informational; containment must not skip the subsequent `make()`/connect steps).

## D-7 — Group 3: the 1137 reject is already correct; only witnesses are missing

The FIXT acceptor `1137` reject arms (`session.cpp:2070-2128`): absent `1137` → `Reject(373=1 RequiredTagMissing, 371=1137)`; non-conformant → `Reject(373=5 ValueIsIncorrect, 371=1137)`; both then `Disconnected`. Both already route through `fire_to_admin_` (`:2128`, post-036). The behaviour is fail-closed and correct by code-read; the gap is purely that no session-level test asserts the observable outcome (the on-wire `371=1137` frame + `toAdmin` observation + disconnect), where 031's analogous reject arms got three witnesses. Group 3 adds those witnesses; **zero production change** (FR-009).

## D-8 — Scope non-expansions

- (a) Only the acceptor `NotConnected` first-Logon arm changes; the initiator Logon-ack Guard-3 (`:3415`) and the established-Q3 guard (`:2373`) are untouched.
- (b) No new config knob — reuse `cfg_.sending_time_threshold` (default 120 s) and the `effective_clock_` gating exactly as established-Q3.
- (c) Group 3 adds zero production code.
- (d) Out of scope (other F-f tail items): wire-parser tag-overflow guard, C-ABI decimal INT64_MIN sentinel, coverage-waiver remediation, §XV.9 no-std-mutex corpus-gate T066 list, B&L 001-014 back-fill.
- (e) No new wire field, error slot, codegen, or C-ABI surface (FR-010).

## D-9 — Fixture blast radius is bounded (Group 1, drives `/tasks`)

The guard rejects an establishing Logon whose `52` diverges from `effective_clock_` by > threshold. Risk: existing acceptor-establishment tests whose inbound Logon `52` is misaligned with the session's (mock) clock would start rejecting. Quantified: the canonical establishment fixture `tests/session/logon_handshake_test.cpp` pins its `mock_clock` to `system_clock::time_point{} + seconds{1704067200}` (= 2024-01-01 00:00:00 UTC) **and** stamps the inbound Logon `52 = "20240101-00:00:00.000"` — i.e. `|52 − now| = 0 ≤ 120 s` → **conforming, no reject**. So well-aligned establishment fixtures are unaffected by construction. Breakage is **bounded to tests whose inbound first-Logon `52` is deliberately or accidentally misaligned** with their session clock (e.g. a hardcoded `52` against a real `system_clock`, or a different mock epoch). `mock_clock` infra exists at `include/fixpp/core/test/mock_clock.hpp`. **Action for `/speckit-tasks`**: a fixture-audit task — grep the acceptor-establishment suites for inbound-Logon `52` production vs the session clock, migrate any misaligned fixture to a controllable/current `52` (expected churn, NOT a regression). This is a known, bounded number, not a mid-phase surprise.
