# Implementation Plan: Interop Harness (Per-Release Interop Gate)

**Branch**: `016-interop-harness` | **Date**: 2026-06-01 | **Spec**: [`spec.md`](./spec.md)
**Input**: Feature specification from `specs/016-interop-harness/spec.md`

**Pipeline state** (authority = `.specify/pipeline.md`, not this line): `/speckit-specify` (2026-06-01) → `/speckit-clarify` (2026-06-01, **3 Qs** — deliverable boundary = SUT tests+goldens in lib / orchestration in parent; corpus = full v1.0 sweep built by this feature; TLS-logon cells in v1.0, mutual-cert mTLS = v1.1) → **`/speckit-plan` (this doc, step 3)** → next per `[const §XVII.1]` / `[pipeline.md step 4]` is **Phase-4 Gate A** on the 016 bundle. Then step 5 `/speckit-tasks` → 6 `/speckit-analyze` → 7 `/speckit-checklist` → 9 `/speckit-checklist-audit` (**MANDATORY**, blocks step 10) → 10 `/speckit-implement` → 11 `/simplify` → 12 `/speckit-verify` → 14 Gate B → 19 MARK DONE.

## Summary

016 realizes the Phase-9 per-release interop gate as a **predominantly test-and-corpus feature with near-zero production surface**. It pairs fixpp's shipped runtime engine (015) as the live System-Under-Test against QuickFIX-cpp and QuickFIX-J — both roles, FIX 4.4 + 5.0 SP2, plain-TCP and TLS-logon — capturing each session's wire dialogue and diffing it against a per-scenario golden transcript (US1); builds the v1.0 thorny-issues corpus from a fresh three-engine tracker sweep and replays it against fixpp (US2); closes the remaining reference-unit-test-parity GAP rows (US3); wires the whole thing as a release gate with a per-PR smoke + a full sanitizer matrix at release-prep (US4); and emits the interop badge + transcripts (US5).

**Deliverable boundary (clarify Q1):** the **committed library deliverable** is the SUT-side artifacts under **`tests/interop/`** — fixpp scenario drivers (built on the public 015 `Engine`), per-scenario golden transcripts, the golden-diff/normalization test utility, the executable thorny-corpus scenarios, and the parity unit tests. The **cross-engine fork-exec orchestration** + the **counterparty-engine clones/builds** stay in the gitignored parent `phase-9-harness/` and are **NOT vendored** into the submodule (FR-026). Corpus *research* (sweep notes, consolidated analysis, provenance write-ups) also stays in the parent — `[const §XV.18]` + `no-research.yml` forbid `research/`/`decisions/` content in the fixpp repo; only the *executable* scenario fixtures + a provenance index live under `tests/interop/`.

**Production surface:** expected **zero or near-zero** new `src/`/`include/` code. The happy-path drivers use the existing public `Engine` (015) + `Framer` (004) surfaces; wire capture happens at the parent's passthrough-proxy layer, so **no new library tap/hook is built** (R3). The only candidate production touch is US3's Bucket-4 model decision (store-replay vs. reorder-queue) — default per R-parity is **confirm-via-witness, no new impl**. If Gate A or the corpus forces a real behavior fix, it is a scoped session-layer change, surfaced then.

## Technical Context

**Language/Version**: C++23 (per `[const §II]`; `std::expected`, coroutines, `std::pmr`) for the SUT drivers + parity tests; the parent-harness orchestration is out-of-repo (Python/shell per `phase-9-harness/`, not constrained by this plan).
**Primary Dependencies**: the shipped public fixpp surface — `Engine` (015, `include/fixpp/session/engine.hpp`), `Session` (`on_inbound_frame`, `open()`), `wire::Framer` (004), `transport/` (012 TCP+TLS), 013 recovery/authorization; GoogleTest/GoogleMock under ctest (`[const §VII.1]`). Counterparty engines (QuickFIX-cpp v1.16.0, QuickFIX-J 3.0.1) are external production binaries built/managed by the parent harness.
**Storage**: N/A (in-memory session state; golden transcripts are checked-in text fixtures; corpus scenarios are checked-in fixture files under `tests/interop/`).
**Testing**: ctest GoogleTest targets (parity unit tests run standalone; live-interop drivers run **counterparty-required**, skipped-with-reason when the binary is absent — FR-023); the parent harness drives the cross-engine pairings. Sanitizer matrix ASan/UBSan/TSan (`[const §IX.2]`) on fixpp's process only (FR-021).
**Target Platform**: Linux (WSL2 dev — correctness interop runs fine on WSL; **benchmarks do not** and are out of this feature's scope per `harness-execution-environment.md` §1.1); CI is the gate. Full live matrix + sanitizers run at release-prep (Pattern A / GitHub Actions two-tier, R4).
**Project Type**: single C++ library (fixpp) — this feature is interop test-scaffolding + golden corpus + parity tests, not a production module.
**Performance Goals**: N/A for this feature. The `[const §VIII.4]` "session throughput parity-or-better vs QuickFIX" target is the **separate bench tier** (`benchmark-plan.md` / `harness-bench.yml`, on-demand), explicitly out of scope here (R6).
**Constraints**: every scenario reconciles to the FIX spec, not to an engine (FR-018, the engine-drift rule); sanitizer-only failures block the tag (FR-020); only fixpp is instrumented (FR-021); corpus append-only (FR-013); no research content in-repo (`[const §XV.18]`); the live matrix must degrade gracefully when a counterparty binary is unavailable (FR-023).
**Scale/Scope**: 26 FRs / 7 SCs / 5 user stories. Happy-path matrix ≈ 2 engines × 2 roles × 2 FIX versions × session-event chains + 4 TLS-logon cells; thorny corpus = full v1.0 sweep (pre-seeded + 2yr closed tail + all open, 3 engines); parity = the ~3 remaining Track-1 GAP witnesses + the Bucket-4 model confirmation + the disposition update.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

*Articles validated against `.specify/constitution.md`:*

- **Article VI.5 (Normative References per artifact)**: ✓ — spec now carries a **## Normative References** section (`[FIX-TC]`, `[FIX-SL §4.4/§6.2/§5.3.x]`, `[FIXS §3.2/§3.4]`, `[FIX50SP2 §3.1]`, `[const §VII.6/§IX.2]`).
- **Article VII.6 (v1.0 interop test — the headline TENSION)**: ⚠ **Complexity Tracking #1**. `[const §VII.6]` mandates the v1.0 interop test cover **"Logon → NewOrderSingle → ExecutionReport → Logout"** — a *business-message* flow. The spec's session-only scope (clarify-confirmed option (a) / user #8) **defers** the `NewOrderSingle/ExecutionReport` cells (FR-005, tagged `deferred:app-messages`). This is a genuine constitution↔spec tension that **`/speckit-analyze` (step 6) will flag** and **Gate A must adjudicate**. Disposition proposed in R7 (do not pre-decide here): the session-only badge does **not** discharge `[const §VII.6]`; that clause stays an open **v1.0-GA** item satisfied when the app-message layer (A-001/A-006, currently `backlog`) lands — tracked, not closed. A permanent deferral past v1.0 would require an Article XX amendment. **Surfaced, not hidden** (`[[karpathy-guidelines]]` #1).
- **Article VII.1/VII.3/VII.5 (TDD; framework; conformance corpus)**: ✓ — GoogleTest/GoogleMock under ctest; parity + corpus scenarios are red-first executable tests; the harness extends (does not replace) `tests/conformance/` (TC-001..TC-017).
- **Article VIII (perf/bench)**: ✓ by exclusion — no perf change in this feature; the QuickFIX throughput-parity target is the separate bench tier (R6). No `bench/` baseline touched.
- **Article IX.1 (coverage 95/85 touched modules)**: ✓ with note — this feature adds tests, not modules; the parity tests *raise* coverage on existing `wire/`/`session/`/`store/` modules. If R-parity forces a Bucket-4 impl, its touched lines fall under 95/85 with the standard `verify.md` assessment.
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

**Result**: PASS with **one ⚠ tension (Complexity #1 — §VII.6 business-message clause vs session-only scope; Gate A adjudicates)** and **two ⚠ research items (R2 corpus in-repo boundary, R3/R-parity production-surface confirmation)**. None are unjustified violations; all are decisions Gate A / `/analyze` will scrutinize.

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
│   └── parity-disposition.md
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

# Parent repo (gitignored, NOT in this submodule) — referenced, not created here:
#   ../../phase-9-harness/        orchestration (fork-exec QFC/QFJ), engine clones, sweep notes, consolidated corpus analysis
```

**Structure Decision**: Single C++ library (fixpp). 016 adds **only `tests/interop/`** — no `src/`/`include/` module (R3). The deliverable boundary (clarify Q1 / FR-026) keeps orchestration + engine management + corpus *research* in the gitignored parent `phase-9-harness/`; the submodule commits the SUT drivers, goldens, executable corpus scenarios, parity witnesses, and a thin provenance index. This honors `[const §XV.18]` (no research content in-repo), the existing `tests/abi/golden/` golden-file discipline, and `[[karpathy-guidelines]]` Simplicity First (no speculative library tap). A library-side wire-capture tap (`2l-tap.md` / S-036) is the **rejected** alternative — the parent proxy already captures bytes, so a new pluggable interface (Article XIV) would be unjustified surface.

## Complexity Tracking

> One real tension to track: the `[const §VII.6]` business-message interop clause vs. the session-only scope. This is **not** a code-complexity violation — it is a scope/constitution adjudication deferred to Gate A. No new modules, no new interfaces, no new error slots.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|--------------------------------------|
| §VII.6 business-message cell deferred (session-only badge) | User #8 + option (a): no app-message layer exists yet (A-001/A-006 `backlog`); shipping a fake business path to satisfy the letter of §VII.6 would be speculative non-value code | Implementing A-001/A-006 now (matrix option (b)) was explicitly rejected by the user; the honest disposition is to keep §VII.6 an open v1.0-GA item (R7), not to pretend the session-only badge closes it |

## Notes

- **Headline correctness target**: the engine `stop()`/teardown path **under live counterparty traffic + the full sanitizer matrix** (FR-019/FR-021) — this is the regime 015's unit tests couldn't reach, and where the 015 down-peer-teardown L2 note + `[[feedback_engine_stop_must_close_transports_total_cancel_insufficient]]` predict the highest signal.
- **Engine-drift rule (FR-018)** is the load-bearing discipline: when QFC and QFJ disagree, assert the FIX spec, file an `OPEN-QUESTIONS.md` entry (parent), never pin to an engine (`harness-execution-environment.md` §2).
- **§VII.6 must be re-surfaced at `/analyze`** — do not let it pass silently; the analyze step's Constitution-Alignment pass (D) is exactly where this belongs, and Gate A signs off the disposition.
- The 015 down-peer initiator teardown carry-forward (CLAUDE.md L2) is a **latent interop landmine** — a matrix cell that points fixpp at a not-yet-listening counterparty could hit the busy-spin; the fixture must start the counterparty first or bound the connect (cross-ref in research R5).

---

*Based on Constitution — see `.specify/constitution.md`*
