# Phase 0 Research: Interop Harness

**Feature**: `016-interop-harness` | **Date**: 2026-06-01
**Inputs**: `phases/phase-9.md`, `phases/phase-9/{work-plan,REFRESH-2026-06-01-post-015,harness-execution-environment,unit-test-parity-matrix,cross-communication-test-plan}.md`, `phase-9-harness/{STATUS,manifest/scenarios.yaml}`, `.specify/constitution.md`, shipped 012/013/014/015 surfaces.

This phase resolves the plan's ⚠ items and pins the decisions that shape `data-model.md` + `contracts/`. The 9.A/9.B/9.G investigation already happened (in `phases/phase-9/` + `phase-9-harness/`); this is consolidation, not re-derivation.

---

## R1 — Driver shape: how the fixpp SUT is driven against a counterparty

- **Decision**: A **GoogleTest fixture** (`tests/interop/support/interop_fixture`) brings up a fixpp `Engine` (015) in the requested role bound to a TCP port; the **parent harness** (`phase-9-harness/`) is responsible for fork-exec'ing the counterparty engine on the paired port and for the passthrough proxy that captures wire bytes. The library test is parameterized by `(role, fix_version, port, security)` and asserts on fixpp's observable session state + (via the parent-supplied capture) the golden diff.
- **Rationale**: keeps the committed deliverable inside the project's ctest/GoogleTest discipline (`[const §VII.1]`) while honoring the clarify-Q1 boundary (orchestration stays in the parent). The fixpp side is the only side that must be sanitizer-instrumentable (FR-021), and a GoogleTest target is the natural unit for the ASan/UBSan/TSan presets.
- **Alternatives rejected**: (a) in-process FFI link to QuickFIX-cpp — rejected (OSS-002 Path-B verdict; couples the submodule to a counterparty's headers); (b) a Python/pytest orchestrator owning the fixpp side too — rejected (would move fixpp driving out of the C++ sanitizer presets). The Python/shell orchestration that *spawns engines* legitimately lives in the parent.

## R2 — Corpus in-repo boundary (`[const §XV.18]` / `no-research.yml`)

- **Decision**: split the corpus across the repo line:
  - **In the submodule (`tests/interop/thorny/`)**: the *executable* scenario fixtures (one test per kept issue, named `<engine>-issue-NNN`) + a terse **`CORPUS-INDEX.md`** provenance index (issue ref, URL, state, category, priority bucket, → test file). These are tests + a test index, allowed.
  - **In the parent (`phase-9-harness/`)**: the raw per-engine sweep files, the consolidated analysis/dedup, and any `*-decisions.md`-style write-up. These are research → forbidden in-repo.
- **Rationale**: `no-research.yml` rejects `^research/`, `^decisions/`, `^book/`, `*-decisions.md`, `opus_plan.md`, `SYNTHESIS.md`. A provenance *index* that is part of a test suite is not "research content"; the *analysis* is. Naming matters — the index must not be `*-decisions.md`.
- **Verification**: a `tests/interop/thorny/CORPUS-INDEX.md` + `<...>_test.cpp` files pass the guard; grep the guard patterns before PR.

## R3 — Production surface: is a library wire-capture tap needed?

- **Decision**: **No new library code for wire capture.** Bytes are captured at the parent's passthrough-proxy layer; fixpp emits real wire over its real transport (012). The library deliverable is tests-only for US1/US2.
- **Rationale**: FR-026 keeps capture in the parent; a library tap (`2l-tap.md` / S-036) would be a new Article-XIV pluggable interface needing a Gate-A justification for ~zero added value here. `[[karpathy-guidelines]]` Simplicity First.
- **Alternatives rejected**: S-036 session tap as a v1.0 capture hook — deferred to its own feature if ever needed for in-process capture; the proxy suffices for the gate.

## R4 — CI tiering + execution environment

- **Decision**: adopt `harness-execution-environment.md` **Pattern A** (GitHub Actions two-tier). **Per-PR smoke** (FR-022): one cell — `HP-QFcpp-init-fix44-logon-hb-logout` — normal build only, on `ubuntu-latest`, gating PRs that touch the transport/session/interop surface. **Release-prep full matrix** (FR-022): all cells × {normal, ASan/UBSan, TSan} (FR-019), counterparty engines built-once-and-cached. Benchmarks are a *separate* on-demand tier (R6), not this gate.
- **Rationale**: correctness interop is noise-tolerant and runs fine in CI / on WSL; only benches need dedicated hardware (`harness-execution-environment.md` §1.1). Pattern A is the documented "enough for now" baseline.
- **Local dev note**: per `[const §XVII.7]`, local matrix runs are `-j2`, one preset at a time, `AskUserQuestion`-gated; the live cells are **counterparty-required** and skip-with-reason when QFC/QFJ aren't running (FR-023).

## R5 — Engine teardown / down-peer landmine under live traffic

- **Decision**: the happy-path fixture **starts the counterparty (or its listener) before** bringing up a fixpp initiator, and bounds the test with an internal deadline. Every **live reconnect cell** additionally MUST (a) use a **finite reconnect policy / max-attempts** (no empty-`ReconnectPolicy` busy-spin) and (b) carry a **`stop()` watchdog** asserting `Engine::stop()` returns within a stated bound — an internal deadline alone only *detects* a hang, it does not make the gate reliable nor prove `stop()` returns. The deliberate down-peer / never-listening case is a **separate regression cell** (FR-028), kept out of the happy-path matrix.
- **Rationale**: the 015 down-peer carry-forward (CLAUDE.md L2) shows a fixpp initiator aimed at a not-yet-listening peer can busy-spin (empty `ReconnectPolicy`) and that `async_connect` isn't promptly total-cancelled — a real interop-fixture hazard. A reconnect cell re-enters that path *after* the listener was initially valid, so the finite policy + watchdog are required, not optional. Cross-ref `[[feedback_engine_stop_must_close_transports_total_cancel_insufficient]]` + `[[feedback_fail_placeholder_red_test]]` (live-I/O probes need an internal self-deadline or `ioc.run()` hangs). **Production prerequisite:** a reliable finite reconnect policy requires the `SessionConfig` reconnect-policy field that does not yet exist (CLAUDE.md L2) — see R-prod.

## R6 — Benchmark scope

- **Decision**: **out of scope** for 016. The `[const §VIII.4]` QuickFIX throughput-parity target is the existing self-test bench (`benchmark-plan.md`, 12 workloads, `harness-bench.yml`), run on-demand on dedicated hardware — not gated per-PR and not part of the correctness gate.
- **Rationale**: `harness-execution-environment.md` §1.1 — bench numbers on WSL/CI are noise; conflating them with the correctness gate produces phantom regressions.

## R7 — `[const §VII.6]` business-message clause disposition (the tension)

- **Decision (proposed; Gate A adjudicates)**: the session-only badge **does not discharge** `[const §VII.6]`. Record the `Logon → NewOrderSingle → ExecutionReport → Logout` interop cell as an **open v1.0-GA item** tracked in the catalogue (A-001/A-006 `backlog`) + `spec/behaviors-and-limitations.md`, satisfied when the app-message layer lands. The matrix carries the cell `deferred:app-messages` (FR-005). A permanent deferral past v1.0 requires an Article XX amendment.
- **Rationale**: user #8 + option (a) defer business messages and explicitly reject building A-001/A-006 now (matrix option (b)). Shipping a stub business path purely to satisfy §VII.6's letter is speculative non-value code (`[[karpathy-guidelines]]` #2). The honest move is to keep the obligation visible, not to claim closure. This is exactly the constitution↔spec drift `/speckit-analyze` (step 6) must flag; Gate A signs off the disposition (or directs option (b)).
- **Alternatives**: (b) implement A-001/A-006 minimal path now — rejected by the user; (c) amend Article XX to move business interop to v1.x — possible if the user wants the tension gone permanently, but that's a constitution change, not a plan decision.

## R8 — Matrix axes + TLS-logon cells for v1.0

- **Decision**: v1.0 live matrix = **{QuickFIX-cpp, QuickFIX-J} × {fixpp-initiator, fixpp-acceptor} × {FIX 4.4} × session-admin chains**, plus the **4 TLS-logon cells** activated by the refresh (FR-025), plus **abnormal-disconnect + reconnect (`ResetSeqNumFlag=N`)** and the **recovery** chain (ResendRequest/SequenceReset, live now via 013). **FIX 5.0 SP2 / FIXT.1.1 cells are placeholder rows tagged `deferred:fixt-routing`, NOT live at v1.0** — fixpp cannot establish a FIXT/5.0SP2 session today (S-020 FIXT half `implementing(4.4 only)`, S-025 `DefaultApplVerID(1137)` `backlog` per `REFRESH-2026-06-01-post-015.md`; `cross-communication-test-plan.md` states the FIXT/5.0SP2 Logon cells require S-020+S-025+S-021 and 005 defers FIXT logon-time semantics; user #8). Business cells present but `deferred:app-messages`. Fix8 cells are placeholder/`deferred:fix8-revisit` (corpus-only, FR-009).
- **Rationale**: matches `cross-communication-test-plan.md` (re-baselined by the refresh) + the clarify-Q3 TLS answer. The refresh's §A.1 moved the recovery + 4 TLS scenarios out of `fixpp gap` into the active matrix. The FIXT/5.0SP2 deferral keeps FR-003/SC-001 realizable and consistent with the parity contract's Bucket-3 `deferred:by-design`; the cells activate when FIXT routing + `DefaultApplVerID(1137)` land.
- **mTLS**: mutual-certificate (client-cert) interop is the v1.1 reach; v1.0 TLS-logon cells use a `SecurityProfile` that the counterparty config can satisfy (server-auth or pinned per fixture).

## R-parity — US3 GAP-closure scope (from `unit-test-parity-matrix.md`)

- **Decision**: the 27-GAP audit's Track-1 set is **already mostly closed in code**: integer-convertor overflow + EncryptMethod=0 (#91), inbound SequenceReset arms (S-023/#90). Remaining concrete 016 work:
  1. **QFJ-646 resend-abort-on-failing-write witness** (Bucket-2, still open) — write the test; plumbing exists.
  2. **Bucket-4 model confirmation** — assert that fixpp's **store-replay** recovery model subsumes QuickFIX's reorder-queue behaviors (simultaneous bidirectional ResendRequests, remove-queued-on-SequenceReset, large-queue) for the **protocol outcome**; default = confirm-via-witness, **no reorder-queue impl** (matrix note: "likely yes for protocol outcome").
  3. **Disposition update** — flip the matrix rows to COVERED with test citations; leave Bucket-3 (`#8`/option-a) + the lower-value rows deferred-by-design.
- **Rationale**: behaviors exist; this is witness-writing + a model assertion, not new production behavior. If the Bucket-4 witness *fails* (replay does NOT subsume some queue behavior), that becomes a scoped session-layer finding surfaced at implement/Gate-B — not pre-built.
- **Open**: the acceptor HeartBtInt-echo assertion (Bucket-1) is low-value; include only if cheap.

## R-scope — bounding the thorny-corpus sweep (avoid runaway scope)

- **Decision**: the v1.0 sweep (FR-010) runs against a **bounded, enumerable worklist** — the pre-seeded list (`phase-9-harness/manifest/scenarios.yaml`) + a **capped per-engine closed-with-fix tail** (sized cap recorded in the corpus manifest). The open-issue `watch:` bucket is **phased as a follow-on sweep**, not folded into this feature as an unbounded all-open-issues triage across three trackers.
- **Rationale**: `phases/phase-9.md` explicitly decomposes the sweep+consolidation into sub-phases 9.A–9.D precisely because it is too large for one effort; an unbounded corpus makes `/speckit-tasks` unable to emit a bounded task list and risks the runaway-scope failure mode (`[[feedback_phase_implementer_sonnet_runaway_scope]]`). The first invocation seeds the corpus; FR-013 append-only governs later releases.
- **Scope-refinement flag**: phasing the open-issue `watch:` bucket as a follow-on sweep is a deliberate **refinement** (a bounding choice) of the phase-9.md (2026-05-22) "closed + ALL open at v1.0" scope decision — recorded here as such, not a silent contradiction; the open-issue tail moves to a follow-on sweep to keep v1.0 `/speckit-tasks` bounded.

## R-prod — production-surface escape hatches + coverage disposition (Article IX §1)

- **Decision**: the feature is **tests-only on the happy path** (R3), but two named paths can touch `src/`/`include/` production code, each pinned with an explicit boundary:
  1. **Reconnect-policy field (FR-004/FR-028 prerequisite)** — the 015 down-peer carry-forward (CLAUDE.md L2) confirms there is **no `SessionConfig` reconnect-policy field** and the connect is not promptly cancellable. Either (a) scope-in a *small, bounded* production change (wire a `SessionConfig` reconnect-policy field + bound/cancel the engine connect) — then its touched lines fall under Article IX §1 95/85 with the standard `verify.md` assessment — or (b) restrict v1.0 reconnect cells to a shape that avoids it (e.g. acceptor-side only) and defer initiator-reconnect cells. The choice is made at `/speckit-tasks`; the "near-zero production surface" claim is bounded, not zero.
  2. **Bucket-4 model confirmation (R-parity)** — default is confirm-via-witness, no impl. If the witness *fails*, the resulting session-layer fix **leaves 016 as a separate feature**, carrying its own Article IX §1 coverage; 016 does not silently absorb a behavior change.
- **Coverage disposition**: if path (1a) is taken, the touched module gets a coverage-index entry + a `verify.md` 95/85 assessment; if 016 ships purely tests-only after scoping (1b + Bucket-4 confirmed), the Article IX §1 touched-module gate is **N/A by construction** and `verify.md` states that explicitly.

---

## Consolidated decisions feeding Phase 1

| # | Decision | Drives |
|---|----------|--------|
| R1 | GoogleTest SUT fixture; parent owns orchestration+capture | data-model `Scenario`; contracts/scenario-descriptor |
| R2 | Executable scenarios + index in-repo; analysis in parent | data-model `CorpusScenario`; contracts/parity-disposition |
| R3 | No library tap; tests-only production surface | Project Structure (tests/interop only) |
| R4 | Pattern-A CI; smoke cell `HP-QFcpp-init-fix44-logon-hb-logout` | quickstart; FR-022 |
| R5 | Counterparty-first ordering + finite reconnect policy + stop watchdog + separate down-peer cell | data-model `Scenario` (`reconnect_policy`); FR-004/FR-028; quickstart |
| R6 | Bench out of scope | (exclusion) |
| R7 | §VII.6 stays open v1.0-GA residual (NORMATIVE: FR-027/SC-008); session-layer reframing | spec FR-027/SC-008; Clarifications |
| R8 | Matrix axes FIX 4.4 LIVE only (5.0SP2/FIXT `deferred:fixt-routing`) + 4 TLS-logon; Fix8 placeholder | data-model `MatrixCell`; FR-003; contracts/scenario-descriptor + parity-disposition |
| R-parity | Witness-only GAP closure + Bucket-4 model confirmation | data-model `ParityRow`; contracts/parity-disposition |
| R-scope | Bounded/capped corpus worklist; open-issue tail phased follow-on | spec FR-010; data-model `CorpusScenario` |
| R-prod | Two production escape hatches pinned (reconnect field / Bucket-4); coverage disposition | plan Production surface; Constitution Check IX.1 |

**All NEEDS CLARIFICATION resolved.** No unresolved unknowns block Phase 1.
