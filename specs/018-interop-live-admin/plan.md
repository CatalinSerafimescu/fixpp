# Implementation Plan: Live Session-Admin Interop Round-Trips (gap-fill G1)

**Branch**: `018-interop-live-admin` | **Date**: 2026-06-03 | **Spec**: [`spec.md`](./spec.md)
**Input**: Feature specification from `specs/018-interop-live-admin/spec.md`

**Pipeline state** (authority = `.specify/pipeline.md`, not this line): `/speckit-specify` (2026-06-03) → `/speckit-clarify` (2026-06-03, **3 Qs** — both fixpp roles; recovery covers inbound-detect + outbound-answer; cadence pinned `108=1`s / ≥3 beats per dir / ±1 / ~5s) → **`/speckit-plan` (this doc)** → next per `[const §XVII.1]` is **Phase-4 Gate A** on the 018 bundle. Then step 5 `/speckit-tasks` → 6 `/speckit-analyze` → 7 `/speckit-checklist` → 9 `/speckit-checklist-audit` (**MANDATORY**, blocks step 10) → 10 `/speckit-implement` → 11 `/simplify` → 12 `/speckit-verify` → 14 Gate B → 19 MARK DONE.

## Summary

G1 extends the **already-minted** session-layer interop badge (016 P0–P6, 2026-06-02; `BADGE.md`, 32/32 live cells GREEN over TLS, both roles) so the live fixpp↔QuickFIX-J cells assert **real bidirectional FIX 4.4 session-admin traffic on the established session**, beyond the Logon/Logout handshake the badge captures today. Four scenario groups, each for **both fixpp roles** (clarify Q1): (US1) bidirectional TestRequest→Heartbeat with `112` echo; (US2) idle Heartbeat cadence (`108=1`s, ≥3 beats/dir, ±1, ~5s); (US3) recovery dialogue **both directions** (clarify Q2 — fixpp detects an inbound gap and issues ResendRequest, AND fixpp answers a QFJ ResendRequest via 013's outbound replay / `SequenceReset-GapFill`); (US4) session-level Reject(35=3) over the live link with session survival.

**Architecture decision (R1 — headline Gate-A item).** The public post-015 surface, as verified during planning, does **not** let an in-process driver originate a *specific* admin message (`Session::send()` is app-payload-only; admin frames are FSM/liveness-emitted) nor observe a received admin frame's contents (only `state()`, `fsm_visit_history()`, `recent_events()` are exposed). Combined with the clarify decision to use the **engine-log seam** (no MITM), this fixes the deliverable split:

- **Wire-frame assertions are golden-based** (parent harness): the bidirectional admin frames (QFJ's `112`-echoing Heartbeat, fixpp's `ResendRequest`, QFJ's `GapFill`, the `Reject`, the cadence beats) are asserted by diffing **enriched golden transcripts** captured from the QuickFIX-J engine-log (`toAdmin`/`fromAdmin`).
- **In-process assertions are fixpp-state-only**: the driver asserts fixpp's observable own state through the scenario — FSM end-state (back to `Active` after recovery), `fsm_visit_history()` membership, outbound seqnum deltas, bounded graceful stop — via the existing `InteropEngineFixture` (`run_until` / `stop_within`).
- **Active manipulation is parent-harness capability**: inducing the inbound gap (drop/withhold a frame via the passthrough layer), driving QFJ to issue a `ResendRequest` (QFJ restart/reconnect at a lower sequence), and injecting the malformed admin frame all happen in the gitignored parent `phase-9-harness/`, not the submodule.

**Production surface: ZERO (bounded, not a silent escape hatch).** Unlike 016 (which scoped-in the reconnect-policy field), G1 expects **no `src/`/`include/` change** — the engine send path, liveness loop, recovery sub-protocol (005/013/S-023), and Reject path already shipped. **Escape hatch (R-prod):** if a scenario genuinely cannot be expressed via the public surface + parent orchestration (e.g., a scenario truly requires fixpp to *originate* a specific admin message in-process), that is surfaced as a **finding that re-triggers Gate A** (`[const §XVII.1]`) — it is NOT met with a silent production change, and G1 does not silently absorb a behavior change.

**Deliverable boundary (mirrors 016 clarify Q1).** The committed **library** deliverable under `tests/interop/` is: the enriched per-cell **golden transcripts** (`happy/golden/`), the **golden-diff assertions** for the new admin frames, the **minimal in-process FSM/seqnum witnesses** extending the existing `hp_fix44_*` cells, the **MATRIX.md** both-role admin-chain rows, and the **`cell_results.yaml`** rows validated by the existing in-repo schema-check. The **cross-engine orchestration** (QFJ launch/drive, gap/malformed injection, capture) and **corpus/sweep research** stay in the parent (`[const §XV.18]`).

**Rejected alternative (Architecture B):** in-process active driving via test-only seams into `Session` internals (`pending_test_req_id_`, an inbound-frame observer, an admin-originate hook). Rejected — it either adds public production API (Gate-A-scope, contradicts the "no production behavior" scope) or reaches into private state via friend seams (a banned-pattern risk and a divergence from clarify decision-b). Recorded in research R1.

## Technical Context

**Language/Version**: C++23 (`[const §II]`) for the SUT-side golden-diff assertions + FSM/seqnum witnesses; the parent-harness orchestration is out-of-repo (Python/shell under `phase-9-harness/`, not constrained by this plan).
**Primary Dependencies**: the shipped public fixpp surface — `Engine` (015, `include/fixpp/session/engine.hpp`), `Session` (`open()`, `send()`, `state()`, `fsm_visit_history()`, `recent_events()`), `wire::Framer` (004), `transport/` TLS (011/012), the liveness loop + recovery FSM (005/013/S-023); GoogleTest/GoogleMock under ctest (`[const §VII.1]`); the existing `tests/interop/support/` fixture (`InteropEngineFixture`, `golden_diff`, `scenario_descriptor`, `counterparty_probe`). Counterparty = QuickFIX-J 3.0.1 (external, parent-managed).
**Storage**: N/A — golden transcripts are checked-in text fixtures under `tests/interop/happy/golden/`; no runtime persistence beyond the in-memory session store.
**Testing**: ctest GoogleTest targets — golden-diff + FSM witnesses run **counterparty-required** (skip-with-reason when QFJ absent, FR-009/FR-023-class), value-parameterized over `(Counterparty × Role)`; the parent harness drives the cross-engine pairing at release-prep tier. Sanitizer matrix ASan/UBSan/TSan on fixpp's process (`[const §IX.2]`).
**Target Platform**: Linux (WSL2 dev; CI is the gate). Full live matrix + sanitizers at release-prep (two-tier, not per-PR — clarify-decision-2 of 016 ROADMAP).
**Project Type**: single C++ library (fixpp) — this feature is interop test-scaffolding + enriched goldens, **not** a production module.
**Performance Goals**: N/A (no perf change; the QuickFIX throughput-parity target is the separate bench tier, out of scope).
**Constraints**: every scenario reconciles to the FIX spec, not to an engine (engine-drift rule); only fixpp is instrumented for sanitizers; live cells degrade gracefully when QFJ is absent (never silent-pass); no research/sweep content in-repo (`[const §XV.18]`); golden normalizer canonicalizes only `52=`/`10=` (reuse 016 P4 normalizer); session-admin scope only — the `[const §VII.6]` business-message clause stays the open v1.0-GA residual (G2), NOT touched here.
**Scale/Scope**: 11 FRs (+ FR-004a, FR-005a) / 6 SCs / 4 user stories. Admin matrix ≈ 4 scenario groups × **both roles** × FIX 4.4 (LIVE) vs QuickFIX-J, over `one_way_ca` TLS. Bounded extension of the existing `happy/` cells + new goldens; **zero production surface** expected.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design. Articles validated against `.specify/constitution.md`.*

- **Article VI / VI.5 (Spec Coverage; Normative References)**: ✓ — the spec carries exact `[FIX-SL]` anchors for each scenario (admin `§4.5.1`/`§4.5.5`/`§4.5.4`; recovery `§4.8.2`/`§4.8.5`/`§4.8.6`; seqnum/gap `§4.5.3`/`§4.8.1`; liveness `§4.5`). A `## Normative References` section is added at `/speckit-tasks` time if Gate A flags a gap (016 precedent). No NEW spec rows — G1 exercises existing S-003/S-004/S-005/S-006/S-007/S-014/S-023 behaviours live.
- **Article VII.1/VII.5 (TDD; conformance corpus)**: ✓ — golden-diff + FSM/seqnum witnesses are red-first executable tests under ctest; the harness extends (does not replace) the 016 `tests/interop/` corpus; goldens are captured at first paired run, never hand-fabricated (016 T009 rule).
- **Article VIII (perf/bench)**: ✓ by exclusion — no perf change; no `bench/` baseline touched.
- **Article IX.1 (coverage 95/85 touched modules)**: ✓ — **N/A by construction**: zero production surface expected (tests + goldens only). If R-prod fires (a scenario needs production code), the touched lines fall under 95/85 with a `verify.md` assessment + coverage-index entry — but the default is no production touch. The witnesses *raise* coverage on existing `session/` admin/recovery paths against live traffic.
- **Article IX.2 (sanitizer matrix)**: ✓ — the in-repo interop ctest runs under ASan/UBSan/TSan against live QFJ admin traffic (fixpp-only instrumentation). **This also discharges the orthogonal 016 `/speckit-verify` YELLOW sanitizer waiver** (the "run the interop ctest under sanitizers once before GA" item, ROADMAP §"How this discharges 016's deferrals").
- **Article X.4 (append-only error slots)**: ✓ — no new error slots (existing Reject/recovery codes exercised). Next free slot remains 122 if ever needed.
- **Article XI.2/XI.4 (cancellation; per-session strand)**: ✓ (inherited) — drivers use the 015 engine's cancellation + strand model unchanged; the headline TSan target is engine teardown under live admin traffic (carry-forward from the 015 `stop()` + down-peer lessons).
- **Article XII (security; TLS)**: ✓ — `one_way_ca` cells reuse the 011/012 surface with a real `SecurityProfile`; no new security surface; mutual mTLS stays `deferred:v1.1-mtls`.
- **Article XIV (pluggable interfaces ≤5)**: ✓ — **no new library interface** (Architecture A builds none). A test-only observation seam, if ever needed (Architecture B), would require Gate-A justification — explicitly rejected here per Simplicity First.
- **Article XV.9 (awaitable-include hygiene; tests-only marker)**: ✓ — interop test files carry the `[const §XV.9] tests/-only` marker (existing pattern); no awaitable-corpus header gains an include.
- **Article XV.18 (no research/decision content in-repo)**: ✓ — orchestration + any QFJ-behaviour sweep notes stay in the parent `phase-9-harness/`; only executable goldens + assertions live under `tests/interop/`.
- **Article XVI.3/XVI.4 (/clarify + /analyze mandatory)**: ✓ — /clarify done (3 Qs); /analyze is pipeline step 6.
- **Article XVII.1 (Gate A blocks /tasks)**: ✓ — Gate A is the next pipeline step; the architecture split (R1) + the zero-production-surface claim warrant it ("when in doubt, run it").
- **Article XVII.7 (local build resource gate)**: ✓ — `/speckit-implement` + `/speckit-verify` builds run `-j2`, one preset at a time, with `AskUserQuestion` approval (`[[feedback_build_resource_cap_oom]]`).
- **Article XVII.8 (/speckit-verify mandatory)**: ✓ — step 12 before Gate B; the verify record is the gate-label precondition.

**Result**: PASS. No unjustified violations. Open ⚠ items routed to Phase 0 research: **R1** (architecture split — golden-based vs in-process; the headline Gate-A item), **R-prod** (zero-production-surface escape hatch), **R3** (gap/malformed-injection mechanism at the parent harness), **R5** (both-role cell expansion incl. driving QFJ to issue a ResendRequest).

## Project Structure

### Documentation (this feature)

```text
specs/018-interop-live-admin/
├── plan.md              # This file
├── research.md          # Phase 0 output (R1–R8)
├── data-model.md        # Phase 1 output (AdminScenarioCell / AdminRoundTrip / GoldenAdminTranscript / CellResultRow)
├── contracts/           # Phase 1 output
│   ├── admin-scenario-descriptor.md      # per-cell admin round-trip descriptor (extends 016 scenario-descriptor)
│   ├── golden-admin-transcript-format.md # enriched golden frames + normalizer reuse
│   └── parent-harness-admin-contract.md  # parent obligations: drive QFJ admin, gap/malformed injection, capture
├── quickstart.md        # Phase 1 output (run one admin cell locally; run the G1 matrix)
└── checklists/
    └── requirements.md  # /speckit-specify output (PASS)
```

### Source Code (repository root)

```text
tests/interop/                          # EXTENDS the 016 deliverable (no new top-level dirs)
├── support/
│   ├── interop_fixture.{hpp,cpp}        # REUSED as-is (run_until / stop_within); no change expected
│   ├── scenario_descriptor.hpp          # EXTENDED — admin round-trip descriptor fields (R1/contracts)
│   ├── golden_diff.{hpp,cpp}            # REUSED — same 52=/10= normalizer; assert new admin frames
│   └── counterparty_probe.hpp           # REUSED — skip-with-reason when QFJ absent
├── happy/
│   ├── hp_fix44_testrequest_echo_test.cpp     # EXTENDED — bidirectional 112 echo, both roles (US1)
│   ├── hp_fix44_idle_heartbeat_cadence_test.cpp  # NEW — ≥3 beats/dir, ±1, ~5s (US2)
│   ├── hp_fix44_seqnum_recovery_test.cpp      # EXTENDED — inbound-detect (US3.i)
│   ├── hp_fix44_recovery_outbound_answer_test.cpp # NEW — fixpp answers QFJ ResendRequest (US3.ii / FR-004a)
│   ├── hp_fix44_reject_invalid_admin_test.cpp # EXTENDED — live Reject crosses + session survives (US4)
│   ├── MATRIX.md                        # EXTENDED — admin-chain rows × both roles, spec_ref per cell
│   └── golden/                          # NEW enriched goldens: HP-*-admin-*.fix (captured at first paired run)
└── KNOWN-LIMITATIONS.md                 # EXTENDED — any peer divergence (reject-vs-disconnect, GapFill shape)

# Parent (gitignored, NOT vendored): phase-9-harness/ gains QFJ admin-driver + gap/malformed
# injection + enriched-golden capture + cell_results emission for the G1 cells.
```

**Structure Decision**: Single C++ library; G1 is a **bounded extension of the existing `tests/interop/` deliverable** — two new cell files (US2 cadence, US3.ii outbound recovery), three extended cells, an extended scenario descriptor + MATRIX, and new enriched goldens. **No new top-level directories, no new module, zero `src/`/`include/` change** (R-prod escape hatch aside). The active orchestration lives in the gitignored parent harness, mirroring the 016 boundary.

## Complexity Tracking

> No Constitution Check violations requiring justification. The one structural decision (Architecture A: golden-based wire assertion + parent-driven manipulation, vs the rejected in-process-seam Architecture B) is recorded in the Summary + research R1 and is the headline Gate-A review item; it reduces rather than adds complexity (zero production surface, no new interface).
