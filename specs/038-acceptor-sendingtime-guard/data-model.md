# Phase 1 Data Model — 038-acceptor-sendingtime-guard

No new types, fields, or persisted state. This documents the decision matrices + invariants the three groups realize.

## Group 1 — Acceptor first-Logon SendingTime disposition matrix

Inputs at the acceptor `NotConnected` first-Logon arm, AFTER `ensure_hydrated_` (`:1925`, read-only outbound/inbound seed) but BEFORE `reset_on_logon` (`:1927`) and `check_inbound` (`:1936`) — so the Reject carries the hydrated outbound seq while inbound is seeded-not-advanced and no durable reset fires — and therefore before the FIXT `1137` validation and `authorize_logon`, with `effective_clock_` set and threshold `T = cfg_.sending_time_threshold ? : 120 s`:

| inbound `52` state | `check_sending_time` | Establish? | Emitted | Reject `373` | Outbound advance | Inbound advance/persist |
|---|---|---|---|---|---|---|
| absent / empty | n/a (treated bad) | **No** | Reject(35=3,371=52), **no Logout** | **10** | +1 (Reject) | **none** |
| present, unparseable | n/a (parse fail) | **No** | Reject(35=3,371=52), **no Logout** | **10** | +1 | **none** |
| present, parses, `\|52−now\| > T` (stale-past OR future) | fails | **No** | Reject(35=3,371=52), **no Logout** | **10** | +1 | **none** |
| present, parses, `\|52−now\| ≤ T` | ok | **Yes** (unchanged) | reply Logon (as today) | — | as today | as today |

Invariants:
- **INV-1 (uniform reason)**: every non-conforming case → `SessionRejectReason(373)=10`, `RefTagID(371)=52` (Clarifications 2026-06-14; mirrors established-Q3 reason code, diverges from QuickFIX `RequiredTagMissing=1` for absent — documented).
- **INV-2 (no-establish, no-Logout)**: a rejected first-Logon emits the Reject then transitions straight to `fsm_state::Disconnected` with **no Logout** (matches the in-arm `1137` reject `:2102-2136`; the established-Q3 Logout is a live-session teardown, not applicable pre-establishment). The session never reaches `Active`/`LogonReceived`.
- **INV-3 (observation)**: the Reject is passed to `fire_to_admin_` before `store_then_emit` (036/FR-008); a throwing `toAdmin` → `app_callback_threw` + `Disconnected` (same as the `1137` reject).
- **INV-4 (inbound seeded-not-advanced; no inbound persist; Reject carries hydrated outbound N)**: the guard fires AFTER `ensure_hydrated_` (`:1925`, read-only — seeds the in-memory counters from the durable store without mutating it) but BEFORE `reset_on_logon` (`:1927`) and `check_inbound` (`:1936`, which is what *advances* the in-memory inbound counter on success). So on a rejected first-Logon the in-memory next-expected-inbound is **seeded but NOT advanced** (persistent store → seeded to the durable value `D`; memory/non-persistent store → `ensure_hydrated_` no-ops, so it stays at `seqnum_min`), the **durable inbound counter is unchanged** (no `check_inbound` advance, no `persist_inbound_advance_`), and **no durable seqnum reset fires** (the guard precedes `reset_on_logon`). Seed ≠ advance — FR-004's "MUST NOT be advanced or persisted" holds in-memory AND durable. The Reject's `assign_outbound` runs against the **hydrated** outbound counter (`ensure_hydrated_` seeds outbound unconditionally), so the frame is stamped with the durable next-outbound `N` (NOT the construction counter 1): `store(N, …)` passes `seq == next_seqnum` and advances durable outbound to `N+1` — wire-correct. (Contrast: the `1137` reject at `:2102-2136` runs AFTER `check_inbound`, so it does leave the in-memory inbound advanced — it only ever proved "no durable persist"; this guard's placement before `check_inbound` is what makes the strong in-memory "not advanced" claim true, NOT the `1137` sibling.)
- **INV-5 (boundary)**: `|52−now| == T` is conforming (within window) — same boundary semantics as established-Q3's `check_sending_time` (`≤ max_latency`).
- **INV-6 (conforming emitted-output identity)**: a within-window first-Logon establishes with **emitted output** identical to pre-feature (the guard is a pure pre-check on the failure side; it adds one parse + one `check_sending_time` on the conforming path but emits identical bytes).

Guard position in the first-Logon validation order: `interpret_logon → ensure_hydrated_ → SendingTime(52) [NEW] → reset_on_logon → check_inbound → 1137 → authorize_logon → establish`. (D-5.) The `52` guard runs after `ensure_hydrated_` (`:1925`, read-only seed) but precedes `reset_on_logon` (`:1927`), `check_inbound` (`:1936`), `1137`, and credential authorization, so it pre-empts every downstream gate while the Reject carries the hydrated outbound seq and inbound is seeded-not-advanced.

### Group 1 — simultaneous-bad ordering (which gate wins)

Because the `52` guard runs before `check_inbound`, `1137`, and `authorize_logon`, a first-Logon that is bad on `52` AND on a downstream gate rejects on `52` (reason=10) first:

| simultaneous defect | wins | emitted |
|---|---|---|
| bad-`52` + bad-`1137` | **`52`** (reason=10) | `Reject(371=52, 373=10)`, no Logout, Disconnected — the `1137` reject is never reached |
| bad-`52` + bad-credentials (553/554) | **`52`** (reason=10) | `Reject(371=52, 373=10)`, no Logout, Disconnected — the silent credential `Disconnected` is never reached (an observable change from today: a previously-silent credential disconnect now emits a public `52` Reject first) |
| bad-`52` + too-low inbound seqnum | **`52`** (reason=10) | `Reject(371=52, 373=10)`, no Logout, Disconnected — the `52` guard precedes `check_inbound`, so the too-low fatal is never reached |

(CompID/BeginString mismatch is dispositioned earlier inside `interpret_logon` and disconnects before the `52` guard, so a bad-CompID Logon never reaches it — no `52`-vs-CompID contest.) This SendingTime-before-seqnum ordering is MORE faithful to QFcpp's `next()` (which runs `isGoodTime` before the seqnum checks); CompID-before-SendingTime is the one divergence from QFcpp (forced by `interpret_logon`'s existing structure). See D-5.

### Group 1 — defensive/error branches to witness (or verify-record waive)

Each of the following arms on the new guard path MUST be witnessed by a named cell OR carry an explicit `/speckit-verify`-record waiver rationale (no overclaim — `[[feedback_witness_asserts_named_postcondition_not_proxy]]`):

- **empty `52=` distinct from absent-52** — the empty-string short-circuits before `fix_string_to_utc_time`; witness it independently from absent (different parse path, same reason=10 disposition).
- **`toAdmin` throw on the new Reject** → `app_callback_threw` + `Disconnected` (INV-3); witness directly, do not proxy.
- **`assign_outbound` failure** on the Reject path.
- **`build_reject` build-failure** → FAIL CLOSED (`Disconnected`), never silent-success (`[[feedback_fixed_buffer_build_failure_silent_success]]`; buffer pinned ≥512B — see quickstart / plan).
- **inbound counter seeded-not-advanced (store-type-aware)** — memory/non-persistent store → in-memory `next_inbound` stays at `seqnum_min` (`ensure_hydrated_` no-ops); persistent store → in-memory `next_inbound` is at the **seeded** durable value `D`, NOT advanced, AND the durable inbound counter is unmoved across the rejected Logon. A flat `== seqnum_min` assertion fails on the persistent path — discriminate by store type (discharges INV-4 directly).
- **persistent-store reconnect, durable outbound `N>1`, bad-`52`** — the emitted Reject carries `34=N` (the hydrated durable outbound seq), NOT `34=1`, the store accepts it (no `store_seqnum_out_of_order` / I-05), and the durable inbound counter is unchanged. Direct witness that the guard runs AFTER `ensure_hydrated_` (see quickstart cell 14).

## Group 2 — Reconnect `credentials_rotated` callback-seam containment

The live `Session`-path `emit_credentials_rotated_` is an engine-owned `noexcept` lambda → `noexcept` `emit_event` (ring-buffer write, no user code), so the throw is unreachable in production; G2 hardens the standalone-FSM injection seam. State around `reconnect_fsm.cpp:194-209`:

```
if (rotated && emit_credentials_rotated_):
    emit_credentials_rotated_(event)        # <-- bare today; wrap in try/catch
last_active_source_ = snap                  # baseline update (MUST run even if callback threw)
last_active_fp_     = new_fp
... Step 3 (ssl_cfg) ... Step 4 (make) ... handshake ...
```

- **INV-7 (containment)**: a throw from `emit_credentials_rotated_(...)` is caught at the call site; control falls through to the baseline update (`last_active_source_`/`last_active_fp_`) and the remaining attempt steps (`make()` → handshake). The attempt reaches its policy outcome instead of unwinding the coroutine.
- **INV-8 (no spurious re-emit)**: because the baseline update still runs after a caught throw, the NEXT attempt does not re-detect the same rotation and re-emit.
- **INV-9 (transparent)**: a non-throwing callback yields behaviour identical to today (the `try/catch` is inert on the happy path).
- SC-free: no measurable user-facing outcome; unit-test-backed (FR-006/FR-007).

## Group 3 — FIXT 1137 reject observable set (witness only)

For the existing reject arms (`session.cpp:2070-2128`), the witness asserts the observable set — no new state:

| inbound `1137` | on-wire frame | `373` | `toAdmin` observed | terminal state |
|---|---|---|---|---|
| absent | `Reject(35=3, 371=1137)` | 1 (RequiredTagMissing) | yes | `Disconnected` |
| non-conformant value | `Reject(35=3, 371=1137)` | 5 (ValueIsIncorrect) | yes | `Disconnected` |

- **INV-10 (no production change)**: Group 3 asserts current behaviour only; `git diff -- src/` for this group is empty (FR-009).
