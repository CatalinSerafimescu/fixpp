# Implementation Plan: Acceptor inbound-Logon SendingTime guard + session/reconnect hardening riders

**Branch**: `038-acceptor-sendingtime-guard` | **Date**: 2026-06-14 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/038-acceptor-sendingtime-guard/spec.md`

## Summary

Close the session-scoped subset of the Fable **F-f** tail — three cleanly-separated, independently witnessable groups:

- **GROUP 1 (primary, P1) — Acceptor first-Logon SendingTime(52) latency guard.** fixpp validates inbound `SendingTime(52)` latency at exactly two sites: the steady-state Q3 guard (`session.cpp:2381`, reached only for an *already-established* session) and the initiator's Logon-ack Guard-3 (`session.cpp:3415`). The **acceptor's `NotConnected` first-Logon arm** (`session.cpp:~1793-2300`) establishes the session via `interpret_logon` — which **explicitly skips tags 34/52/98** (`admin_messages.cpp:226`) — and never reaches the Q3 guard, so a Logon with an absent/empty, malformed, or hours-stale `52` establishes unchecked. Fix: insert a SendingTime check in the first-Logon arm (after `interpret_logon` + the FIXT `1137` validation succeed, before the session establishes / before any inbound persist), scaffolded on the **in-arm `1137` reject sibling** (`session.cpp:2102-2136`, the proven pre-establishment emit shape): **`Reject(35=3, RefTagID=52, SessionRejectReason=10)` → `assign_outbound` → `fire_to_admin_` → `store_then_emit` → `record_state_transition_(Disconnected)` — NO Logout** (the in-arm `1137` reject emits no Logout; the established-Q3 Logout is a *live-session* teardown, not applicable pre-establishment). Per Clarifications, absent/empty, malformed, AND stale all share the single `reason=10` disposition (mirrors established-Q3's reason code, which already maps empty `52` → reason=10). Grounded against QuickFIX-cpp (`Session::next` → `isGoodTime` → `doBadTime`, role-agnostic on the Logon) and QFJ (`isGoodTime`/`doBadTime`), not symmetry-derived — the *MUST-reject* fact from QF, the *frame shape* from fixpp's in-arm `1137` precedent. Amends catalogue **S-019** (`[FIX-SL §4.2.3]`) via the **S-033 / §4.5.4** reject taxonomy.

- **GROUP 2 (rider, P2) — Reconnect `credentials_rotated` callback exception containment.** `reconnect_fsm.cpp:200` invokes the user-supplied `emit_credentials_rotated_(...)` callback bare inside the `noexcept` `drive_reconnect_attempt` coroutine. This is **not** a `std::terminate` defect (an exception escaping a coroutine body is delivered to the promise, not to `terminate` — distinct from the noexcept-*method* class of `[[feedback_noexcept_boundary_user_callback_terminate]]`); it is a graceful-degradation gap — a throwing callback abandons the in-flight reconnect attempt. Fix: wrap the single callback invocation in `try/catch` matching the established `authorize_logon` callback-guard shape so the throw is contained and the attempt proceeds per policy. Relates to **T-040** / 014 (`session_event_credentials_rotated`). Unit-test-backed only; SC-free by design.

- **GROUP 3 (rider, test-only, P3) — FIXT `DefaultApplVerID(1137)` reject witness.** The existing acceptor `1137` reject arms (`session.cpp:2070-2128`: absent → `Reject 373=1`; non-conformant → `Reject 373=5`, both `RefTagID=1137`, then Disconnected) are fail-closed by code-read but have **zero session-level negative witnesses** (031's analogous reject arms got three). Add negative witness(es) asserting the on-wire `Reject(371=1137)` + its `toAdmin` observation + the disconnect. **No production change** (FR-009).

### Why one feature, not three

All three live on the same acceptor-establishment / reconnect surface and ship as one Gate-A/Gate-B unit, but they are kept as three independent FR groups (each with its own witness and SC disposition) so review can judge each on its own — the primary Group-1 scrutiny is not diluted by the two small riders. Group 3 is pure test addition; Group 2 is a one-site `try/catch`; only Group 1 changes establishment control flow.

## Technical Context

**Language/Version**: C++23 (Clang 22 local == CI per `[const Art.II §2]`)
**Primary Dependencies**: `fixpp::session::check_sending_time` (`sending_time.cpp:36` — `|inbound − now| ≤ max_latency`, returns `session_sending_time_accuracy` slot 71); `fixpp::core::fix_string_to_utc_time` (parse); `scan_frame_header(frame).sending_time` (extract inbound `52`); `fixpp::session::build_reject` (`admin_messages.cpp` — no `build_logout`, this path emits no Logout); `Session::fire_to_admin_` (`session.cpp:331`); `store_then_emit`; `record_state_transition_`; `cfg_.sending_time_threshold` (existing, default 120 s) + `cfg_.sending_time_precision`; the acceptor `NotConnected` first-Logon arm (`session.cpp:~1793-2300`); the in-arm FIXT `1137` reject (`session.cpp:2102-2136`) as the emit template; the established-Q3 guard (`session.cpp:2373-2459`) as the validation-logic reference; `ReconnectFsm::drive_reconnect_attempt` + `emit_credentials_rotated_` (`reconnect_fsm.cpp:194-200`)
**Storage**: none — no `MessageStore` change. Group 1 explicitly does NOT call `persist_inbound_advance_` on the reject path (a session that never establishes must not mutate persisted inbound state — `[[feedback_unconditional_persist_at_multiexit_gate_breaks_lowerbound]]`). The single `Reject` it emits advances the OUTBOUND counter only (one `assign_outbound`; `store_then_emit` persists that outbound frame), exactly as the in-arm `1137` reject does
**Testing**: GoogleTest. Group 1: acceptor first-Logon cells driven off a **controllable clock** (mock clock) — stale-past, stale-future, malformed, absent-`52`, and a conforming-within-window establishment (no-regression); assert no-establish + `Reject(373=10, 371=52)` + **NO Logout** + `Disconnected` + inbound seqnum unadvanced. Group 2: a `credentials_rotated` callback that throws during a reconnect attempt → attempt still reaches its policy outcome. Group 3: FIXT acceptor `1137` absent/non-conformant → on-wire `Reject(371=1137)` + `toAdmin` observation + disconnect. ASan/UBSan over the session + reconnect suites
**Target Platform**: Linux (Tier-1). Group 1 is acceptor-role-specific (the initiator + established paths already guard)
**Project Type**: single library (`fixpp`)
**Performance Goals**: no change on the conforming path — the guard adds one parse + one `check_sending_time` comparison per inbound Logon (admin path, not the hot app path); the reject emits only on the non-conforming (terminal) path. No heap (stack `build_reject` buffer, mirroring the `1137` reject). `[const §XV.1]` preserved by construction
**Constraints**: no new public wire field, error slot, config option (reuse `sending_time_threshold`), codegen, or C-ABI (FR-010); session/+reconnect only (FR-011); conforming-path establishment byte-identical (FR-005); no inbound persist on the reject path (FR-004); Group 3 production-change-free (FR-009)
**Scale/Scope**: Group 1 ~30-40 LoC production (the first-Logon SendingTime check + Reject/Disconnect block — no Logout, inline-replicating the in-arm `1137` reject) in `session.cpp`; Group 2 ~3-5 LoC (`try/catch`) in `reconnect_fsm.cpp`; Group 3 0 LoC production + 1 test file. No header-graph change (`tools/check_layers.py` unaffected); no new header

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Article | Relevance | Disposition |
|---|---|---|
| **VI — Spec Coverage Discipline (100% FIX Rule)** | SendingTime validation on a new path; Normative References (§VI.5) | No new message/field — `SendingTime(52)`, `Reject(35=3)`, `DefaultApplVerID(1137)` all pre-exist. Group 1 extends S-019's `[FIX-SL §4.2.3]` latency check to the acceptor first-Logon; Group 3 witnesses S-020's `1137` reject. **`## Normative References` section present in `spec.md` per §VI.5** (cites `[FIX-SL §4.2.3]`→S-019, `[FIX-SL §4.5.4]`→S-007/S-033, `[FIX-SL §4.4]`→S-020, `[impl]`→T-040). Catalogue gains traceability amendments to S-019 (+ a B&L conformance row); no new OFFICIAL row. PASS. |
| **VII — Testing Requirements (TDD)** | New behavior + verification gap | New seam: acceptor first-Logon latency guard (Group 1) gets red-before/green-after cells on a controllable clock; Group 2 gets a throwing-callback cell; Group 3 closes the 1137 witness gap. PASS. |
| **VII §7 — Fuzz (parser-touching modules)** | Inbound `52` parse | **N/A — no new parser.** Group 1 reuses the existing `fix_string_to_utc_time` parser (already fuzzed via `wire/` + the established-Q3 path); the guard adds a *call site*, not a decode path. No new fuzz harness required. |
| **IX §1 — Coverage / Sanitizers** | New + changed branches | Every new branch in the Group-1 guard (absent / malformed / stale / conforming; reject-build-fail; assign-fail) reaches DA/BRDA via the mock-clock cells; the `build_reject` error arm reuses the `1137`-reject `std::unexpected` pattern. Full ASan/UBSan over touched suites. **Coverage target ≥95/85 on touched files; any uncovered defensive arm carries a verify-record rationale.** PASS. |
| **X — ABI Policy** | Public surface | No C ABI change. Group 1 may add ONE private `Session` helper (internal, non-exported) if extracted; no public signature, error variant, or symbol added. PASS. |
| **XI — Concurrency & Coroutines** | reconnect coroutine, admin emit on strand | Group 1 emits on the session strand exactly as established-Q3 does (same `fire_to_admin_` + `assign_outbound` path); Group 2's `try/catch` is inside the existing `drive_reconnect_attempt` coroutine — no new strand/executor/await. No `std::mutex` introduced. PASS. |
| **XII — Security & TLS** | Anti-replay flavor | Group 1 *adds* an anti-replay (stale-timestamp) check at the establishment boundary — strictly hardening; no TLS/cert/identity surface touched. PASS (net-positive). |
| **XIII — Observability & Logging** | toAdmin observation | Group 1's Reject routes through `fire_to_admin_` (036/FR-008 — admin emits MUST be observable; `[[feedback_admin_emit_bypasses_fire_to_admin]]`); Group 3 asserts that observation. No trace-context change. PASS. |
| **XVI §3 / XVII §1 — `/clarify`, `/analyze`, Gate A, `/plan` sign-off (Session FSM trigger)** | Acceptor establishment FSM | This touches the **session FSM** (establishment-time reject + a new terminal transition on the first-Logon arm) → all four mandatory controls apply. `/clarify` ✓ (1 Q resolved). `/analyze` pending. **Gate A after this plan, before `/tasks`.** User `/plan` sign-off required. |
| **XV §1 / VIII §5 — banned per-message heap** | Reject build | `build_reject` writes into a stack `std::array` buffer (mirroring the `1137` reject `:2115`); no per-message `new`/`delete`. PASS by construction. |
| **Dependencies / Version Management** | None added | No new third-party dependency. |

**Surface delta**: no new wire field, `SessionConfig`/`EngineConfig` field, error variant, codegen, C-ABI, or public-signature change. The change is a new establishment-time guard reusing the existing `build_reject` builder + `check_sending_time` + `fire_to_admin_` + `store_then_emit`, a one-site `try/catch`, and test additions. **Session-FSM trigger → Gate A mandatory.**

## Project Structure

### Documentation (this feature)

```text
specs/038-acceptor-sendingtime-guard/
├── plan.md              # This file
├── research.md          # Phase 0 — reference-engine ground truth (QFcpp/QFJ validate latency role-agnostically on the Logon; processing halted); the no-Logout shape (scaffold on the in-arm 1137 reject); the missing-52 / interpret_logon-skips-52 fact; scope non-expansions
├── data-model.md        # Phase 1 — the acceptor first-Logon disposition matrix (52 absent/malformed/stale/conforming → outcome) + the guard's ordering vs interpret_logon / 1137 / persist; Group 2 + Group 3 state notes
├── quickstart.md        # Phase 1 — the mock-clock acceptor first-Logon witness recipe (4 reject cells + 1 conforming no-regression cell), the throwing-callback reconnect cell, the 1137 negative-witness cell
├── contracts/
│   └── acceptor-logon-sendingtime.md   # internal contract: the acceptor first-Logon SendingTime disposition + the persist/observation invariants
├── checklists/
│   └── requirements.md  # spec-quality checklist (done; clarify resolved)
└── tasks.md             # /speckit-tasks output (NOT created here)
```

### Source Code (repository root)

```text
src/session/
├── session.cpp          # GROUP 1 — acceptor NotConnected first-Logon arm (~:1793-2300):
│                        #   after interpret_logon success + the 1137 FIXT validation, BEFORE the session
│                        #   establishes (before reply Logon / before any persist_inbound_advance_):
│                        #     - extract hdr.sending_time via scan_frame_header(frame)
│                        #     - gate on effective_clock_ (mirror established-Q3 :2381)
│                        #     - ok := !empty AND parse-ok AND check_sending_time(parsed, now, threshold)
│                        #     - if !ok: Reject(35=3, RefTagID=52, reason=10) → assign_outbound → fire_to_admin_
│                        #       → store_then_emit → record_state_transition_(Disconnected) → co_return {}
│                        #       (scaffold on the in-arm 1137 reject :2102-2136 — NO Logout; NO persist_inbound_advance_)
│                        #   established-Q3 (:2373-2459) AND the 1137 reject (:2102-2136) UNCHANGED (surgical)
└── reconnect_fsm.cpp    # GROUP 2 — wrap emit_credentials_rotated_(...) (:200) in try/catch
                         #   (authorize_logon callback-guard shape); contain the throw, continue the attempt

include/fixpp/session/session.hpp   # GROUP 1 — IF a private helper is extracted (single new caller →
                                    #   leaning inline-replicate, NO helper; see research D-3), otherwise no header change

tests/session/
├── test_acceptor_logon_sending_time.cpp   # NEW (Group 1) — mock-clock acceptor first-Logon cells:
│                        #   stale-past / stale-future / malformed-52 / absent-52 → no-establish + Reject(373=10,371=52)
│                        #   + NO Logout + Disconnected + inbound-seqnum-unadvanced; conforming-within-window → establishes
│                        #   (byte-identity no-regression vs pre-feature acceptor establishment)
├── test_credentials_rotated_emit.cpp      # EXTEND (Group 2) — add a throwing-callback cell: reconnect attempt
│                        #   with a credentials_rotated callback that throws → attempt reaches its policy outcome (not abandoned)
└── test_fixt_logon_establishment.cpp      # EXTEND (Group 3) — 1137 negative witnesses: absent + non-conformant →
                         #   on-wire Reject(371=1137) + toAdmin observation + Disconnected (NO production change)

spec/
├── behaviors-and-limitations.md          # B-038-1: acceptor first-Logon now enforces SendingTime MaxLatency (parity with
│                                         #   initiator + established paths); L-038-1: absent/empty 52 dispositioned as reason=10
│                                         #   (NOT RequiredTagMissing=1) — documented divergence from QuickFIX (internal consistency)
└── feature-catalogue.md / coverage-index.md   # S-019 amendment (acceptor first-Logon latency guard); traceability for 038

specs/005-*  +  specs/033-*   # dated notes: 005 S-019 now also covers the acceptor first-Logon path (was established+initiator only);
                              # 033 1137 reject path now carries session-level negative witnesses (no behavior change)
```

**Structure Decision**: Single-library, in-place. Group 1 adds an establishment-time guard in one arm of `session.cpp` reusing existing builders + `check_sending_time` + `fire_to_admin_`; the merged established-Q3 path is left untouched (surgical). Group 2 is a one-site `try/catch` in `reconnect_fsm.cpp`. Group 3 is test-only. No new module, header-graph edge, or exported surface.

## Phase 0 — Research

See [research.md](./research.md). Key decisions:

1. **Reference-engine ground truth (Group 1)** — both QFcpp (`Session::next` → `isGoodTime` → `doBadTime`, `Session.cpp:996`) and QFJ (`Session.java:1821`) validate inbound SendingTime latency **role-agnostically on the Logon**, BEFORE message-type routing; on failure: `Reject(SessionRejectReason=10 SendingTimeAccuracy)` + `generateLogout()` + disconnect, and `next()` returns false (processing halted, session NOT established). fixpp takes the *MUST-reject* fact + reason=10 from this, but the *frame shape* from its own in-arm `1137` reject (Reject + disconnect, **no Logout** — D-3), not QF's added Logout. Verified, not symmetry-derived.
2. **Missing-`52` is NOT caught upstream (Group 1)** — `interpret_logon` (`admin_messages.cpp:226`) explicitly skips tags 34/52/98, and the steady-state Q3 guard is not reached on the establishment path; so absent/empty `52` is currently unvalidated at the acceptor first-Logon. Per Clarifications 2026-06-14: absent/empty, malformed, AND stale all share one `reason=10` disposition mirroring established-Q3 (which already maps empty `52` → reason=10). Accepted, documented divergence from QFcpp/QFJ (which use `RequiredTagMissing=1` for absent) in favour of internal consistency.
3. **Scaffold on the in-arm `1137` reject; inline, not extract-a-helper (Group 1 design choice)** — the closest precedent is the FIXT `1137` reject in the SAME `NotConnected` first-Logon arm (`:2102-2136`): `Reject(35=3, 371=…)` → `assign_outbound` → `fire_to_admin_` → `store_then_emit` → `Disconnected`, **no Logout**. This is the correct pre-establishment shape (the established-Q3 Logout is a live-session teardown) and it proves the outbound-seqnum + observation machinery works before establishment. Inline-replicate that block with `371=52`/`373=10`; do NOT extract a shared helper (a single new caller does not justify the abstraction — `[const §XV]` simplicity; Karpathy #2 — and extracting would re-point the merged path, the seam-reorder trap `[[feedback_seam_removal_profile_gate_ordering]]`). Leave both the `1137` reject and established-Q3 untouched.
4. **No inbound persist on reject (Group 1)** — the first-Logon reject path must NOT call `persist_inbound_advance_` (the session never establishes; persisting inbound would over-advance the durable counter — `[[feedback_unconditional_persist_at_multiexit_gate_breaks_lowerbound]]`). Only the OUTBOUND counter advances (one `assign_outbound` for the Reject; `store_then_emit` persists that outbound frame), exactly as the in-arm `1137` reject.
5. **Guard ordering (Group 1)** — the SendingTime check fires AFTER `interpret_logon` (CompID/BeginString/HeartBtInt) and the FIXT `1137` validation succeed, BEFORE the session establishes. This mirrors fixpp's own established-Q3 ordering (Guard-2 CompID → Guard-3 SendingTime); it differs from QFcpp's `next()` order (SendingTime before CompID), but the observable outcome differs only in the simultaneously-bad-CompID-and-`52` edge (which reason fires first) — internal consistency wins.
6. **Group 2 mechanism** — an exception escaping `drive_reconnect_attempt` (a `noexcept` coroutine) is delivered to the coroutine promise's `unhandled_exception`, NOT to `std::terminate`. Confirm where the co_spawn completion handler lands the failure; the `try/catch` is graceful-degradation containment matching `authorize_logon`, distinct from the noexcept-*method* `terminate` class. SC-free.
7. **Scope non-expansions** — (a) only the acceptor first-Logon arm changes; initiator + established paths untouched; (b) no new config knob (reuse `sending_time_threshold`); (c) Group 3 adds zero production code; (d) the other F-f tail items (wire-parser overflow, C-ABI sentinel, coverage waivers, §XV.9 corpus gate, B&L back-fill) stay out of scope.

## Phase 1 — Design & Contracts

- [data-model.md](./data-model.md) — the acceptor first-Logon **SendingTime disposition matrix** (`52` absent/empty | malformed | stale-past | stale-future | conforming → establish? / reject reason / outbound-advance / inbound-advance); the guard's position in the first-Logon validation order; Group 2's reconnect-attempt outcome-on-throw; Group 3's 1137 reject observable set.
- [contracts/acceptor-logon-sendingtime.md](./contracts/acceptor-logon-sendingtime.md) — the internal contract: (a) every non-conforming first-Logon `52` (absent/malformed/stale) yields `Reject(35=3, 371=52, 373=10)` + `Disconnected` with **no Logout**, the Reject observed via `toAdmin`, session not established, inbound seqnum unadvanced; (b) a conforming first-Logon establishes byte-identically to today; (c) Group 2: a throwing `credentials_rotated` callback does not abandon the reconnect attempt; (d) Group 3: the 1137 reject is observable (on-wire `371=1137` + `toAdmin` + disconnect).
- [quickstart.md](./quickstart.md) — the mock-clock acceptor first-Logon witness recipe (4 reject cells + 1 conforming no-regression), the throwing-callback reconnect cell, the 1137 negative-witness cell; how to drive the controllable clock so existing acceptor fixtures with a fixed `52` are migrated, not treated as regressions.

## Complexity Tracking

No constitution violations to justify. The feature *adds* a hardening guard (anti-replay parity) and *closes* a verification gap; it reuses the existing `build_reject` builder, the `check_sending_time` validator, the `fire_to_admin_` + `store_then_emit` paths, and the existing `sending_time_threshold` config. The two design choices with alternatives — (a) Reject + Disconnect with no Logout (vs established-Q3's Logout), and (b) inline-replicate the in-arm `1137` reject vs extract-a-shared-helper — are both decided in favour of consistency with the nearest in-arm pre-establishment precedent and surgical minimalism (research D-3): the `1137` reject is the correct shape and a single new caller does not justify a helper.

## Gate A

- _Pending — run after this plan, before `/tasks` (Session-FSM trigger, `[const §XVII.1]`)._
