# Requirements-Quality Release Gate Checklist: Application Threading Contract & `fixpp::core::Clock`

**Purpose**: Formal release-gate validation of requirements **quality** (completeness, clarity, consistency, measurability, coverage) across the threading/cancellation, public-API-contract, config/lifecycle, and non-functional domains — before `/speckit-implement`. This tests whether the requirements are *written* correctly, not whether the implementation works.
**Created**: 2026-05-19
**Feature**: [spec.md](../spec.md)
**Rigor**: Formal release gate (Gate-B-mandatory P1 feature; hard correctness obligations)
**Companion**: [requirements.md](requirements.md) (the `/specify` spec-quality checklist — not duplicated here)

> Scope note: 007 is a Phase-4 realization of the **signed-off, Gate-A-converged** `.specify/2d-threading.md` v0.4. Per the 004/006 precedent, design-doc/section anchors are normative traceability references to a frozen authority, not new implementation decisions; items below test whether the *spec's restatement* of those obligations is complete, unambiguous, and internally consistent.

## Requirement Completeness

- [ ] CHK001 - Are all four `Clock` methods' contracts (return type, `noexcept`, awaitable shape, and the explicit "no `expected_t` return — cancellation via `operation_aborted`") fully specified? [Completeness, Spec §FR-001/Key Entities]
- [ ] CHK002 - Is the `effective_clock` resolution rule specified for *every* enumerated session-scoped consumer (heartbeat, SendingTime, S-035, session-scoped LOG/OBS) and distinguished from engine-scope records? [Completeness, Spec §FR-005]
- [ ] CHK003 - Are requirements defined for every callback in the 9-element set the strand must serialise (`onLogon…fromApp`, store op, clock wake, transport completion)? [Completeness, Spec §FR-008]
- [ ] CHK004 - Are the phase-1 `FileStore::flush_for_session_close()` hook's invocation conditions (exactly once, ordering vs Logout `async_write`, idempotency, `terminal`-skip, mid-flush error path) fully specified? [Completeness, Spec §FR-011]
- [ ] CHK005 - Are all 9 error variants (slots 47–55) and their `FIXPP_ERR_THREAD_*`/`FIXPP_ERR_CANCELLED` coalescing groups enumerated as requirements? [Completeness, Spec §FR-018]
- [ ] CHK006 - Are requirements present for the engine-fallback trace-context path when no session domain is in scope? [Completeness, Spec §FR-015/US4 AC-3]
- [ ] CHK007 - Does the spec bound the minimal `Session` skeleton's 2d-owned surface explicitly against what `005` later adds, so the completeness audit has a defined boundary? [Completeness, Spec Assumptions/Clarifications 2026-05-19]
- [ ] CHK008 - Is the `Session::session_arena()` engine-internal accessor's contract (never-null resolution chain, `fixpp::session/`-only) stated as a requirement, given it has no dedicated test seam? [Gap, Spec §FR-019]

## Requirement Clarity & Measurability

- [ ] CHK009 - Is "deterministic scripted test-double FSM" defined precisely enough to bound which properties it may assert (2d-owned) vs FIX FSM correctness (out of scope)? [Clarity, Spec Clarifications 2026-05-19]
- [ ] CHK010 - Is each §6.3 latency ceiling a concrete numeric threshold with an explicit workload and platform qualifier? [Measurability, Spec §FR-021/SC-004]
- [ ] CHK011 - Is "zero global-heap allocation" defined to clearly distinguish flagged global-heap allocation from expected (non-flagged) PMR-arena allocation? [Clarity, Spec §FR-020/SC-003]
- [ ] CHK012 - Is the cross-thread 250 ns row's "bench-soft" status and its precise pass/fail effect on CI unambiguous? [Ambiguity, Spec §SC-004]
- [ ] CHK013 - Can "no exception crosses the parse→`fromApp` window" be objectively verified as written (boundary of the window defined)? [Measurability, Spec §FR-021]
- [ ] CHK014 - Are the three `close()` states (already-closing / never-opened / already-closed-drained) each given a distinct, unambiguous required outcome with no overlap? [Clarity, Spec §FR-013/US3 AC-3]
- [ ] CHK015 - Is the `cancellable_dispatch` "≤5 ns relaxed-atomic check" stated as a measurable bound rather than a vague qualifier? [Measurability, Spec §FR-012]
- [ ] CHK016 - Is the `trace_context` POD's size/trivial-copyability/standard-layout stated as an enforceable contract (e.g., a pinned `static_assert`) rather than prose? [Measurability, Spec Key Entities / data-model E11]

## Requirement Consistency

- [ ] CHK017 - Is the backpressure naming consistent everywhere (closed nested *type* `SessionConfig::backpressure_mode` vs frozen *field* `SessionConfig::app_backpressure`) across FR-010, US5, SC-005, Key Entities? [Consistency, Spec §FR-010]
- [ ] CHK018 - Do US3 acceptance scenarios and FR-011 agree on phase-1↔phase-2 ordering and the "`terminal` skips phase 1 / hook not invoked" rule? [Consistency, Spec §US3/§FR-011]
- [ ] CHK019 - Is SC-006's third-party-`Clock` claim consistently scoped as scripted-test-double (not a real FIX FSM) everywhere it appears (US1/US2 Independent Tests, SC-006, seam-15)? [Consistency, Spec §SC-006]
- [ ] CHK020 - Does the trace-context recovery mechanism (`session_ptr()` member; NOT `any_io_executor::query`; NOT `thread_local`) read consistently across FR-015, US4, and Edge Cases? [Consistency, Spec §FR-015]
- [ ] CHK021 - Are the `clock_not_set` precedence rules consistent between FR-006 ("regardless of session `clock_override`s") and US2 AC-1? [Consistency, Spec §FR-006]
- [ ] CHK022 - Is the `close_timeout` ownership statement consistent (owned by `005`, **not** a `SessionConfig` field; 2d wires mechanism only) across FR-011, Assumptions, and the Key Entities/config description? [Consistency, Spec §FR-011/Assumptions]

## Threading & Cancellation Scenario Coverage

- [ ] CHK023 - Are requirements defined for the cancellation-racing-release/drain case via the deterministic three-case `cancellable_dispatch` contract (exactly one of granted/cancelled per waiter)? [Coverage, Spec §Edge Cases/§FR-012]
- [ ] CHK024 - Are requirements defined for `cancel_sleeps()` invoked re-entrantly from inside a `sleep_until` completion handler (idempotent, no re-entry)? [Coverage, Spec §Edge Cases]
- [ ] CHK025 - Are requirements specified for `direct_executor` attested over a genuinely non-serialised executor, distinguishing debug-assert vs release documented-UB behavior? [Coverage, Edge Case, Spec §Edge Cases/US1 AC-4]
- [ ] CHK026 - Is the `~Engine` teardown ordering with in-flight sleeps/`fromApp` fully specified as an ordered requirement (close-all → drain → `cancel_sleeps` → drain waiter list → clear `clock` last)? [Coverage, Spec §Edge Cases/US2 AC-5]
- [ ] CHK027 - Are abort/forced-disconnect requirements defined for PMR arena exhaustion on the dispatch / `cancellable_dispatch` node? [Coverage, Exception Flow, Spec §Edge Cases]
- [ ] CHK028 - Are requirements defined for a coroutine resuming on a different thread than it suspended on, asserting trace-context correctness via the stable `Session*`? [Coverage, Spec §Edge Cases/US4 AC-1]
- [ ] CHK029 - Are requirements defined for the empty-slot-mid-`Session::open()` case (default `trace_context` + debug assertion)? [Edge Case, Spec §Edge Cases]

## API Contract Quality

- [ ] CHK030 - Is `make_session_executor(...)` specified as the *single* `executor_not_serialised` enforcement point with no alternative construction path? [Clarity, Spec §FR-009]
- [ ] CHK031 - Are `session_executor` value-semantics requirements stated unambiguously (copyable but **NOT** trivially copyable) to prevent a trivial-copy assumption? [Clarity, Spec Key Entities]
- [ ] CHK032 - Is the `Session::open()` 2d-owned shape bounded so it pins only the 2d obligations (executor resolution, `effective_clock`, slot population, rejections) without freezing `005`'s FIX FSM signature? [Clarity, Spec §FR-018]
- [ ] CHK033 - Is the error-slot model specified as additive, non-renumbering at slots 47–55, with the C-ABI-symbol-shape decision explicitly deferred to `2i`? [Clarity, Spec §FR-018/Assumptions]

## Config & Lifecycle Coverage

- [ ] CHK034 - Are all illegal-config rejection cases enumerated with their specific required error (null `dictionary` / null `EngineConfig::executor` / sentinel `security_profile` / `direct_executor`+`spin`)? [Completeness, Spec §FR-018/US5 AC-2]
- [ ] CHK035 - Is the close-and-reopen-only reconfiguration constraint specified to apply to *every* frozen field (no mid-session reconfiguration path)? [Coverage, Spec §US5 AC-1]
- [ ] CHK036 - Is the FIXT.1.1 `ApplVerID(1128)`-miss routing requirement unambiguous that it routes through the `[2c §6.7]` dict-layer error (not a 2d synonym) at **both** dispatch-time and `Engine::open` registry-build? [Clarity, Spec §FR-017/US5 AC-4]
- [ ] CHK037 - Is the uniform `resolved = override.value_or(engine_anchor)` pattern specified consistently across all three axes (executor, clock, dictionary)? [Consistency, Spec §FR-016]

## Non-Functional Requirements Quality

- [ ] CHK038 - Are the allocation-guard requirements scoped to a named platform tier (Linux/Clang Tier-1) and explicit corpus sizes (10⁴ messages / 10⁴ cycles)? [Clarity, Spec §SC-003/Assumptions]
- [ ] CHK039 - Is the per-`Session*` timer-slot rule ("allocated exactly once on first `sleep_until`; an idle session that never sleeps allocates none") stated measurably for **both** threading modes? [Measurability, Spec §FR-020/US6 AC-2]
- [ ] CHK040 - Are the sanitizer obligations specified per-seam (which seams are TSan-mandatory; which additionally ASan-clean) rather than generically? [Completeness, Spec §SC-001]
- [ ] CHK041 - Is the fuzz-harness obligation's scope unambiguous (voluntary, not a `[const §VII.7]` requirement, campaign duration, zero-deadlock/double-free/PMR-leak pass criteria)? [Clarity, Spec §SC-007/Assumptions]

## Dependencies, Assumptions & Conflicts

- [ ] CHK042 - Is the assumption that `005`/`2e` are deferred (Session = minimal skeleton; conformance corpus 2d-scoped) validated and explicitly bounded so the feature-completeness audit passes **without** a waiver? [Assumption, Spec Assumptions/§SC-002]
- [ ] CHK043 - Are the consumed-not-built upstream dependencies (merged `006` `session_executor` backing, merged `003` `version_registry`) documented as requirement-relevant assumptions? [Dependency, Spec Assumptions]
- [ ] CHK044 - Is the NFR-015 catalogue claim stated so that only the Gate-B Status-field promotion is residual (row/coverage-index/`[arch §11]`/`session_local` rename already landed), with this feature editing none of those files? [Assumption, Spec §SC-008]
- [ ] CHK045 - Is there any unresolved conflict between "minimal real `Session`, no FIX FSM" and the seams that drive FIX-shaped sequences, and is it explicitly resolved via the scripted-test-double scoping? [Conflict, Spec Clarifications 2026-05-19]
- [ ] CHK046 - Does the spec unambiguously state that seam 11 / SC-002 claims **no** FIX-TC discharge (to avoid an inadvertent completeness-audit waiver)? [Conflict, Spec §SC-002/Assumptions]

## Notes

- Check items off as completed: `[x]`; record findings inline. An item "fails" if the *requirement text* is missing, ambiguous, unmeasurable, inconsistent, or uncovered — not if implementation is incomplete.
- Traceability: 45/46 items carry a `[Spec §…]` ref or a `[Gap]/[Ambiguity]/[Conflict]/[Assumption]` marker (≥80% satisfied).
- This is a requirements-quality gate, complementary to `/speckit-analyze` (cross-artifact consistency) and `/gate-a` (design-faithfulness). It does not re-litigate the signed-off `2d` v0.4 design decisions.
