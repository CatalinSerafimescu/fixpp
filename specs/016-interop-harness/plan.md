# Implementation Plan: Interop Harness (Session-Layer Interop Gate)

**Branch**: `016-interop-harness` | **Date**: 2026-06-01 | **Spec**: [`spec.md`](./spec.md)
**Input**: Feature specification from `specs/016-interop-harness/spec.md`

**Pipeline state** (authority = `.specify/pipeline.md`, not this line): `/speckit-specify` (2026-06-01) → `/speckit-clarify` (2026-06-01, **3 Qs** — deliverable boundary = SUT tests+goldens in lib / orchestration in parent; corpus = full v1.0 sweep built by this feature; TLS-logon cells in v1.0, mutual-cert mTLS = v1.1) → **`/speckit-plan` (this doc, step 3)** → next per `[const §XVII.1]` / `[pipeline.md step 4]` is **Phase-4 Gate A** on the 016 bundle. Then step 5 `/speckit-tasks` → 6 `/speckit-analyze` → 7 `/speckit-checklist` → 9 `/speckit-checklist-audit` (**MANDATORY**, blocks step 10) → 10 `/speckit-implement` → 11 `/simplify` → 12 `/speckit-verify` → 14 Gate B → 19 MARK DONE.

## Summary

016 realizes the **session-layer** slice of the Phase-9 per-release interop gate (a v1.0-GA *precursor*, not the full release gate — the §VII.6 business-message clause stays an open residual, FR-027/SC-008) as a **predominantly test-and-corpus feature with a small, bounded production prerequisite on the reconnect path** (see Production surface). It pairs fixpp's shipped runtime engine (015) as the live System-Under-Test against QuickFIX-cpp and QuickFIX-J — both roles, **FIX 4.4** (FIX 5.0 SP2 / FIXT.1.1 cells are `deferred:fixt-routing` placeholders, FR-003), plain-TCP and TLS-logon — capturing each session's wire dialogue and diffing it against a per-scenario golden transcript (US1); builds the v1.0 thorny-issues corpus from a **bounded, capped** three-engine tracker sweep and replays it against fixpp (US2); closes the remaining reference-unit-test-parity GAP rows (US3); wires the whole thing as a release gate with a per-PR smoke + a full sanitizer matrix at release-prep (US4, via the parent-harness gate-contract); and emits the interop badge + transcripts (US5).

**Deliverable boundary (clarify Q1):** the **committed library deliverable** is the SUT-side artifacts under **`tests/interop/`** — fixpp scenario drivers (built on the public 015 `Engine`), per-scenario golden transcripts, the golden-diff/normalization test utility, the executable thorny-corpus scenarios, and the parity unit tests. The **cross-engine fork-exec orchestration** + the **counterparty-engine clones/builds** stay in the gitignored parent `phase-9-harness/` and are **NOT vendored** into the submodule (FR-026). Corpus *research* (sweep notes, consolidated analysis, provenance write-ups) also stays in the parent — `[const §XV.18]` + `no-research.yml` forbid `research/`/`decisions/` content in the fixpp repo; only the *executable* scenario fixtures + a provenance index live under `tests/interop/`.

**Production surface:** the happy-path established-session cells need **zero** new `src/`/`include/` code — drivers use the existing public `Engine` (015) + `Framer` (004) surfaces; wire capture happens at the parent's passthrough-proxy layer, so **no new library tap/hook is built** (R3). But the claim is **bounded, not zero** — two named escape hatches can touch production code (R-prod):

1. **Reconnect-path prerequisite (FR-004/FR-028).** The 015 down-peer carry-forward (CLAUDE.md L2) confirms there is **no `SessionConfig` reconnect-policy field** and the engine connect is not promptly cancellable, so a *reliable* live reconnect cell + a provable `stop()` is **not** free. v1.0 takes one of: (a) a **small, bounded** production change — wire a `SessionConfig` reconnect-policy field + bound/cancel the connect (touched lines under Article IX §1 95/85 with a `verify.md` assessment); or (b) restrict v1.0 reconnect cells to a shape that avoids it (e.g. acceptor-side reconnect) and defer initiator-reconnect cells. Decided at `/speckit-tasks`.
2. **Bucket-4 model confirmation (R-parity).** Default = confirm-via-witness, no impl. If the witness *fails*, the session-layer fix **leaves 016 as a separate feature** with its own coverage — 016 does not silently absorb a behavior change.

If 016 ships purely tests-only after scoping (1b + Bucket-4 confirmed), the Article IX §1 touched-module gate is **N/A by construction** and `verify.md` says so.

## Technical Context

**Language/Version**: C++23 (per `[const §II]`; `std::expected`, coroutines, `std::pmr`) for the SUT drivers + parity tests; the parent-harness orchestration is out-of-repo (Python/shell per `phase-9-harness/`, not constrained by this plan).
**Primary Dependencies**: the shipped public fixpp surface — `Engine` (015, `include/fixpp/session/engine.hpp`), `Session` (`on_inbound_frame`, `open()`), `wire::Framer` (004), `transport/` (012 TCP+TLS), 013 recovery/authorization; GoogleTest/GoogleMock under ctest (`[const §VII.1]`). Counterparty engines (QuickFIX-cpp v1.16.0, QuickFIX-J 3.0.1) are external production binaries built/managed by the parent harness.
**Storage**: N/A (in-memory session state; golden transcripts are checked-in text fixtures; corpus scenarios are checked-in fixture files under `tests/interop/`).
**Testing**: ctest GoogleTest targets (parity unit tests run standalone; live-interop drivers run **counterparty-required**, skipped-with-reason when the binary is absent — FR-023); the parent harness drives the cross-engine pairings. Sanitizer matrix ASan/UBSan/TSan (`[const §IX.2]`) on fixpp's process only (FR-021).
**Target Platform**: Linux (WSL2 dev — correctness interop runs fine on WSL; **benchmarks do not** and are out of this feature's scope per `harness-execution-environment.md` §1.1); CI is the gate. Full live matrix + sanitizers run at release-prep (Pattern A / GitHub Actions two-tier, R4).
**Project Type**: single C++ library (fixpp) — this feature is interop test-scaffolding + golden corpus + parity tests, not a production module.
**Performance Goals**: N/A for this feature. The `[const §VIII.4]` "session throughput parity-or-better vs QuickFIX" target is the **separate bench tier** (`benchmark-plan.md` / `harness-bench.yml`, on-demand), explicitly out of scope here (R6).
**Constraints**: every scenario reconciles to the FIX spec, not to an engine (FR-018, the engine-drift rule); sanitizer-only failures block the tag (FR-020); only fixpp is instrumented (FR-021); corpus append-only (FR-013); no research content in-repo (`[const §XV.18]`); the live matrix must degrade gracefully when a counterparty binary is unavailable (FR-023).
**Scale/Scope**: 28 FRs / 8 SCs / 5 user stories. Happy-path matrix ≈ 2 engines × 2 roles × **FIX 4.4 (LIVE)** × session-event chains + 4 TLS-logon cells (5.0SP2/FIXT cells are `deferred:fixt-routing` placeholders, FR-003); thorny corpus = **bounded, capped** v1.0 sweep (pre-seeded + capped 2yr closed tail; open-issue `watch:` bucket phased as a follow-on, FR-010); parity = the ~3 remaining Track-1 GAP witnesses + the Bucket-4 model confirmation + the disposition update.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

*Articles validated against `.specify/constitution.md`:*

- **Article VI.5 (Normative References per artifact)**: ✓ — spec carries a **## Normative References** section with **exact** coverage-index entries (Gate A round-1 fix): admin `[FIX-SL §4.3/§4.5.1/§4.5.5/§4.5.4/§4.6]`, recovery `[FIX-SL §4.8.2/§4.8.5/§4.8.6/§4.4.2/§4.4.3]`, seqnum/gap `[FIX-SL §4.5.3/§4.8.1]`, FIXT `[FIX-SL §4.3.7]`, TLS `[FIXS §3.2/§3.4]`, `[FIX-TC]`, `[const §VII.6/§IX.2]`. The prior wildcard/mis-anchored cites (`[FIX-SL §5.3.x]`, recovery `[FIX-SL §4.4]`, `[FIX-SL §6.2]`, `[FIX50SP2 §3.1]`) are corrected.
- **Article VII.6 (v1.0 interop test — RESOLVED at Gate A round 1, option (c))**: ✓ — `[const §VII.6]` mandates the v1.0 interop test cover **"Logon → NewOrderSingle → ExecutionReport → Logout"** — a *business-message* flow. 016 is re-framed as the **session-layer** interop gate (a v1.0-GA *precursor*); its Success Criteria **explicitly do NOT discharge** the §VII.6 business clause, now **NORMATIVE** in **FR-027 + SC-008** (not just plan/research prose). That clause stays an open **v1.0-GA residual** satisfied when the app-message layer (A-001/A-006, `backlog`) lands — tracked in `spec/behaviors-and-limitations.md` + the catalogue, not closed by this badge. Business cells stay present-but-deferred (FR-005). A permanent deferral past v1.0 would require an Article XX amendment. Recorded in the spec `## Clarifications` Gate A round-1 block. Respects user #8 (no code).
- **Article VII.1/VII.3/VII.5 (TDD; framework; conformance corpus)**: ✓ — GoogleTest/GoogleMock under ctest; parity + corpus scenarios are red-first executable tests; the harness extends (does not replace) `tests/conformance/` (TC-001..TC-017).
- **Article VIII (perf/bench)**: ✓ by exclusion — no perf change in this feature; the QuickFIX throughput-parity target is the separate bench tier (R6). No `bench/` baseline touched.
- **Article IX.1 (coverage 95/85 touched modules)**: ✓ with **two pinned escape hatches** (Gate A round-1, R-prod). This feature predominantly adds tests, not modules; the parity tests *raise* coverage on existing `wire/`/`session/`/`store/` modules. The two named production-touch paths are bounded, not asserted-away: **(1)** the FR-004/FR-028 reconnect-policy field — if scoped-in (R-prod option 1a), its touched lines fall under 95/85 with a `verify.md` assessment + a coverage-index entry; if avoided (1b), no surface; **(2)** R-parity Bucket-4 — if the witness fails, the fix **leaves 016 as a separate feature** with its own coverage, never silently absorbed. If 016 ships tests-only after scoping, the touched-module gate is **N/A by construction** and `verify.md` states it explicitly.
- **Article IX.2 (sanitizer matrix)**: ✓ — FR-019/FR-020 ARE the Tier-1 ASan/UBSan/TSan gate, applied to live counterparty traffic (the strongest signal); fixpp-only instrumentation (FR-021).
- **Article X.4 (append-only error slots)**: ✓ — no new error slots expected (the feature exercises existing codes); next free slot is 122 if R-parity ever needs one. No renumbering.
- **Article XI.2/XI.4 (cancellation; per-session strand)**: ✓ (inherited) — drivers use the 015 engine's cancellation + strand model unchanged; the headline sanitizer target is the engine teardown under live traffic (carry-forward from 015's `stop()` lesson + the down-peer L2 note).
- **Article XII (security; TLS)**: ✓ — TLS-logon cells (FR-025) drive the 011/012 surface with a real `SecurityProfile`; no new security surface, no fail-OPEN. Mutual-cert mTLS deferred to v1.1 with an explicit note.
- **Article XIV (pluggable interfaces ≤5)**: ✓ — R3 builds **no new library interface** (wire capture is parent-side proxy). If a tap were ever added it would need ≤5 methods + Gate A justification — explicitly rejected here per Simplicity First.
- **Article XV.18 (no research/decision content in-repo)**: ⚠ **research item R2** — the corpus has two halves: *executable scenario fixtures + provenance index* (allowed under `tests/interop/`) vs *sweep notes / consolidated analysis* (research → parent only). R2 pins the exact boundary so `no-research.yml` stays green (`*-decisions.md`, `research/`, `book/` patterns).
- **Article XVI.3/XVI.4 (/clarify + /analyze mandatory)**: ✓ — /clarify done (3 Qs, session-FSM/wire/security triggers); /analyze is pipeline step 6 (will catch the §VII.6 tension).
- **Article XVII.1 (Gate A blocks /tasks)**: ✓ — Gate A is the next pipeline step; this is a large design (harness architecture + corpus model + the §VII.6 adjudication) → Gate A warranted ("when in doubt, run it").
- **Article XVII.7 (local build resource gate)**: ✓ — `/speckit-implement` + `/speckit-verify` builds run `-j2`, one preset at a time, with `AskUserQuestion` approval first (`[[feedback_build_resource_cap_oom]]`).
- **Article XVII.8 (/speckit-verify mandatory)**: ✓ — step 12 before Gate B; the verify record is the gate-label precondition.

**Result**: PASS. The §VII.6 tension is **RESOLVED at Gate A round 1** (option (c) — session-layer reframing; §VII.6 business clause is an open v1.0-GA residual made NORMATIVE in FR-027/SC-008). Remaining ⚠ research items: R2 corpus in-repo boundary, R-prod production-surface escape hatches (reconnect-policy field / Bucket-4), R-scope corpus bound. None are unjustified violations.

## Project Structure

### Documentation (this feature)

```
specs/016-interop-harness/
├── plan.md              # This file
├── research.md          # Phase 0 output (R1–R8 below)
├── data-model.md        # Phase 1 output (Scenario / Cell / CorpusScenario / ParityRow / Golden entities)
├── contracts/           # Phase 1 output (scenario-descriptor + golden-format + parity-disposition schemas)
│   ├── scenario-descriptor.md
│   ├── golden-transcript-format.md
│   ├── parity-disposition.md
│   └── parent-harness-gate-contract.md   # US4/US5 gate + badge named-check obligations (Gate A round-1)
├── quickstart.md        # Phase 1 output (run one cell locally; run the full gate)
└── checklists/
    └── requirements.md   # /speckit-specify output (PASS)
```

### Source Code (repository root)

```
tests/interop/                      # NEW — the committed SUT-side deliverable (FR-026)
├── support/
│   ├── interop_fixture.hpp/.cpp     # GoogleTest fixture: bring up a fixpp Engine (015) in a role on a port
│   ├── golden_diff.hpp/.cpp         # byte-stream normalization (strip timestamps/seqnums/non-det) + diff (FR-006)
│   └── counterparty_probe.hpp       # detect counterparty binary/port availability → skip-with-reason (FR-023)
├── happy/                          # US1 — one driver per matrix cell; counterparty-required
│   ├── golden/                      # per-scenario golden transcripts HP-<id>.fix (FR-006)
│   └── hp_<engine>_<role>_<ver>_<chain>_test.cpp
├── thorny/                         # US2 — executable corpus, append-only, by category
│   ├── <category>/<engine>-issue-NNN_test.cpp   # provenance in the test name + a header comment
│   └── CORPUS-INDEX.md              # in-repo provenance index (issue→scenario→priority); NOT the research write-up
└── parity/                         # US3 — the remaining GAP witnesses
    ├── resend_abort_on_failing_write_test.cpp   # QFJ-646 (Bucket-2, still open)
    ├── inbound_sequencereset_arms_test.cpp      # confirm S-023/#90 arms (or extend)
    └── replay_subsumes_reorder_queue_test.cpp   # Bucket-4 model confirmation (R-parity)

.github/workflows/                  # NEW (in-repo) — US4 per-PR smoke (FR-022)
└── interop-smoke.yml               # one cell HP-QFcpp-init-fix44-logon-hb-logout, normal build, gates transport/session-touching PRs

# Parent repo (gitignored, NOT in this submodule) — referenced via the gate-contract, not created here:
#   ../../phase-9-harness/        orchestration (fork-exec QFC/QFJ), engine clones, sweep notes, consolidated corpus analysis
#   ../../phase-9-harness/        release-prep full-matrix + sanitizer gate evaluator + result schema + badge emitter (US4/US5)
# Contract for the above parent obligations: contracts/parent-harness-gate-contract.md (named checks FR-019/020/022/024)
```

**Structure Decision**: Single C++ library (fixpp). 016 adds **only `tests/interop/`** — no `src/`/`include/` module (R3). The deliverable boundary (clarify Q1 / FR-026) keeps orchestration + engine management + corpus *research* in the gitignored parent `phase-9-harness/`; the submodule commits the SUT drivers, goldens, executable corpus scenarios, parity witnesses, and a thin provenance index. This honors `[const §XV.18]` (no research content in-repo), the existing `tests/abi/golden/` golden-file discipline, and `[[karpathy-guidelines]]` Simplicity First (no speculative library tap). A library-side wire-capture tap (`2l-tap.md` / S-036) is the **rejected** alternative — the parent proxy already captures bytes, so a new pluggable interface (Article XIV) would be unjustified surface.

## Complexity Tracking

> The `[const §VII.6]` business-message interop clause vs. the session-only scope is **RESOLVED at Gate A round 1** (option (c) — session-layer reframing; the business clause is an open v1.0-GA residual made NORMATIVE in FR-027/SC-008). This was never a code-complexity violation — it is a scope/constitution disposition, now written into the spec's binding surface. No new modules, no new interfaces, no new error slots.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|--------------------------------------|
| §VII.6 business-message cell deferred (session-only badge) | User #8 + option (a): no app-message layer exists yet (A-001/A-006 `backlog`); shipping a fake business path to satisfy the letter of §VII.6 would be speculative non-value code | Implementing A-001/A-006 now (matrix option (b)) was explicitly rejected by the user; the honest disposition is to keep §VII.6 an open v1.0-GA item (R7), not to pretend the session-only badge closes it |

## Notes

- **Headline correctness target**: the engine `stop()`/teardown path **under live counterparty traffic + the full sanitizer matrix** (FR-019/FR-021) — this is the regime 015's unit tests couldn't reach, and where the 015 down-peer-teardown L2 note + `[[feedback_engine_stop_must_close_transports_total_cancel_insufficient]]` predict the highest signal.
- **Engine-drift rule (FR-018)** is the load-bearing discipline: when QFC and QFJ disagree, assert the FIX spec, file an `OPEN-QUESTIONS.md` entry (parent), never pin to an engine (`harness-execution-environment.md` §2).
- **§VII.6 disposition is settled** (Gate A round 1, option (c)) — the session-layer reframing + the open-v1.0-GA-residual disposition are NORMATIVE in FR-027/SC-008 and recorded in the spec `## Clarifications` Gate A round-1 block; `/analyze` (step 6) should confirm the spec surface matches, not re-open it.
- The 015 down-peer initiator teardown carry-forward (CLAUDE.md L2) is a **latent interop landmine** — a matrix cell that points fixpp at a not-yet-listening counterparty could hit the busy-spin; the fixture must start the counterparty first or bound the connect (cross-ref in research R5).

## Gate A

- Round 1 applied 2026-06-01: Codex P1=2 P2=4 P3=1; Opus post-judging P1=2 P2=6 P3=2; rewrite addresses root causes #1 (binding dispositions → spec normative surface: session-layer reframing + §VII.6 residual), #2 (parent-harness gate-contract for US4/US5 deliverables), #3 (honest production-surface: reconnect prerequisite), #4 (corpus scope bound). Reviews: research/reviews/codex_016-interop-harness_gate_a_review.md, research/reviews/opus_016-interop-harness_gate_a_adversarial_review.md.
- Round 2 applied 2026-06-01: Codex P1=0 P2=1 P3=1; Opus post-judging P1=0 P2=1 P3=2; rewrite extends the parent-harness gate-contract cell_result schema (kind/matrix_disposition/deferred_reason/priority/tracking_issue_state; evaluate over live rows only; per-deferred-cell row required), renames the stale checklist title to Session-Layer, and self-flags the FR-010 watch-bucket phasing as a refinement of the 2026-05-22 scope. Reviews: research/reviews/codex_016-interop-harness_gate_a_2_review.md, research/reviews/opus_016-interop-harness_gate_a_2_adversarial_review.md.
- Round 3 (verification) 2026-06-01: Codex P1=0 P2=1 P3=0; Opus post-judging P1=0 P2=1 P3=0 (rounds 1+2 confirmed genuinely closed; lone residual = a false-green hole: the tier-agnostic evaluator excluded FR-023 `skip:` rows from both the green % and `blocking_failures`, so a required live cell skipped at release-prep could still mint a GA badge). Loop exhausted at rewrites 2/2 → **Step F user escalation; user chose targeted-fix + verification pass.** Orchestrator hand-edit under user direction: made the evaluator `tier`-aware (`smoke` vs `release-prep`) — at release-prep a `skip:*` on a required `live` cell is a blocking miss + forces `badge_eligible: false` (FR-023/SC-001); smoke-tier skip stays non-blocking. Round-4 verification pass next. Reviews: research/reviews/codex_016-interop-harness_gate_a_3_review.md, research/reviews/opus_016-interop-harness_gate_a_3_adversarial_review.md.
- Round 4 (verification) 2026-06-01: Codex **P1=0 P2=0 P3=0 — "Gate A ready"** (confirmed first-hand the `tier`-aware skip rule closes the false-green hole + no regression to live-fail/corpus/deferred handling). **CONVERGED.** Per the 015 precedent, the round-4 Opus adversarial judge was NOT separately spawned — the fix is the single one-line rule the round-3 Opus judge already pre-approved as "correct, minimal, and sufficient," Codex round-4 independently confirms closure with first-hand cites, and the orchestrator verified the exact edit first-hand. Combined post-judging tally P1=0 P2=0 P3=0. Independence per `[const §XVII.3]` preserved (author Opus / reviewers Codex ×4 fresh / judges Opus ×3 fresh / rewriters Opus ×2 fresh / round-3 hand-edit by orchestrator under user direction). Review: research/reviews/codex_016-interop-harness_gate_a_4_review.md.

---

*Based on Constitution — see `.specify/constitution.md`*
