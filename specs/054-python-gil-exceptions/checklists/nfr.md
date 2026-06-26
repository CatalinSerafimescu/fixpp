# NFR & Verification Checklist: Sanitizers, Witnesses, Freeze (PY-002 + PY-003)

**Purpose**: Requirements-quality gate for the non-functional surface — the Tier-1 `python-bindings` sanitizer matrix, the subprocess-watchdog robustness witness, the `0→1` C-ABI freeze, and the Article XX design-doc amendment. Audience: Gate B reviewer.
**Created**: 2026-06-26
**Feature**: [spec.md](../spec.md) · contracts: [gil-discipline-contract.md](../contracts/gil-discipline-contract.md) G-6, [python-exception-surface.md](../contracts/python-exception-surface.md) · plan.md Constitution Check

## Requirement Completeness

- [x] CHK001 Is the freeze constraint stated absolutely — NO `include/fix/c_api.h` or `fix/c_api/*.h` edit, all work confined to `bindings/python/` + pytest + the `.specify/2m-pybind.md` note — with the escalation path (a real C-ABI gap → STOP, raise a separate additive feature) named? [Completeness, Spec §FR-012] — PASS: spec §FR-012 (line 103) states absolute prohibition "MUST NOT modify `include/fix/c_api.h` or any `fix/c_api/*.h` header"; escalation path ("a real C-ABI gap → STOP, raise a separate additive feature") is named; tasks.md scope guard + plan.md Constraints echo both.
- [x] CHK002 Is the watchdog test fully specified — subprocess + hard timeout, raising callback STAGED concurrently against a blocking teardown (recv callback blocks on a `threading.Event`, then raises; main thread enters blocking `engine_destroy`/`session_close` and sets the Event) — with the rationale that a bare raising callback does NOT discriminate? [Completeness, Spec §FR-011/§SC-004, data-model E-5] — PASS: spec §FR-011 (line 99) + E-5 fully specify the watchdog: subprocess + hard timeout; threading.Event staging; blocking teardown trigger; discriminating rationale ("a bare raising callback with no concurrent teardown just prints and returns, exiting cleanly with OR without the release, and therefore does NOT pin the fix") explicit.
- [x] CHK003 Is the as-built containment behavior specified (raise caught + `PyErr_Print`'d at the flat trampoline, execution continues, NOT propagated into the C++ worker) AND the divergence noted (no 1200 engine-translation — deferred to PY-004, the class still exists and 1200 still maps)? [Completeness, Spec §FR-011, data-model E-5, research D-8] — PASS: spec §FR-011 (line 99) + E-5 + research D-8 all state: raise caught + PyErr_Print'd at flat trampoline + execution continues; divergence note ("does NOT translate into FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED/1200 ... deferred to the director slice / PY-004") explicit; "the class still exists and 1200 still maps" stated.

## Requirement Clarity

- [x] CHK004 Is the Tier-1 matrix scope unambiguous — the new typed-exception + watchdog + the GIL-release GREEN leg run under `none`/`asan`/`tsan` and stay green; ONLY the `FIXPP_PY_GIL_RELEASE_CANARY` deliberate-deadlock RED build is local-only? [Clarity, Spec §FR-013/§SC-005, contract G-6, data-model E-4] — PASS: spec §FR-013 (line 104) + E-4 + G-6 all state matrix = `none`/`asan`/`tsan`; "GREEN (pass-without-canary) leg ... is in-matrix" explicit; "Only the `FIXPP_PY_GIL_RELEASE_CANARY` build ... is local-only" explicit.
- [x] CHK005 Is the UBSan-leg gap explicitly handled — the `python-bindings` matrix has no UBSan lane, 054 carries forward the 053 D-9 waiver (NOT silently dropped), recorded as a verify-risk note? [Clarity, plan.md Constitution Check IX §2] — PASS: plan.md Constitution Check IX §2 records the 054-carried-forward 053 D-9 waiver; verify-risk note present; not silently dropped; not WAIVED under uncertainty.
- [x] CHK006 Is the bounded-deadline discipline clear — all live-I/O / canary / watchdog tests use bounded, test-failing deadlines or a subprocess hard timeout, never an unbounded wait in the in-matrix suite (CI-hang risk)? [Clarity, plan.md Constraints, data-model E-4/E-5] — PASS: E-4 specifies subprocess hard timeout for canary RED; E-5 specifies subprocess hard timeout for watchdog child; plan.md Constraints prohibit unbounded waits in in-matrix suite; "CI-hang risk" referenced.

## Requirement Consistency

- [x] CHK007 Is the Article XX routing consistent — the `[2m]` census amendment (FR-005) AND the `AppError` extension (FR-006/D-5) are both additive/limitation amendments to a signed-off Phase-2 doc, reviewed at Gate A, and committed in THIS PR (Gate A has no PR)? [Consistency, plan.md Constitution Check XX, Spec §FR-005] — PASS: plan.md Constitution Check XX records both FR-005 and FR-006/D-5 as additive/limitation amendments; Gate A Round 1 (RC-1..RC-4) + Round 2 (RC-A, RC-B) reviewed the amendment sites; Article XX §1 commits the edit at /implement in-PR; routing is consistent.
- [x] CHK008 Does the "no new error code / no C-ABI symbol / no codegen / no wire surface" claim hold — `AppError` maps EXISTING 051 codes 1400–1405 (no new `fixpp_error_t`), and the hierarchy is a pure SWIG/Python-layer construct? [Consistency, Spec §FR-012, data-model E-2] — PASS: error.h verified: codes 1400–1405 were minted in 051 (comment at error.h:12 + 2i-capi.md:590+); no new `fixpp_error_t`; `AppError` hierarchy is a pure `%pythoncode` / SWIG-layer construct; spec §FR-012 freeze verified by no c_api.h edit.

## Acceptance Criteria Quality

- [x] CHK009 Is SC-004 measurable — the watchdog child completes within its hard timeout across REPEATED runs (not a single pass), and would time out if the GIL-release bands were removed? [Measurability, Spec §SC-004] — PASS: spec §SC-004 (line 122) explicitly says "completes within its hard timeout across **repeated runs**"; E-5 defines the fail-path "WITHOUT the release … drain never completes → timeout → engine deadlocked"; both "repeated" and "would timeout if bands removed" are positive assertions.
- [x] CHK010 Is SC-005 measurable — the FULL `python-bindings` matrix (`none`/`asan`/`tsan`) stays green with the new tests, with the sanitizer instrumentation confirmed present (not a false-green)? [Measurability, Spec §SC-005, contract G-6] — PASS: spec §SC-005 (line 123) requires the full matrix to stay green; G-6 + plan.md Constitution Check SC-005 confirm sanitizer instrumentation; `none` lane cross-checks that the test itself passes independently of sanitizer overhead.

## Edge Case Coverage

- [x] CHK011 Is the subprocess isolation rationale captured — a hung worker in the child cannot wedge the parent pytest, and the hard timeout NAMES the failure (per the 6h-burn lesson)? [Edge Case, data-model E-5] — PASS: E-5 states "Isolation: child process (a hung worker can't wedge the parent pytest)"; hard timeout named as the failure signal; implicit reference to the CI-hang lesson (cross-referenced in plan.md Constraints as "CI-hang risk").
- [x] CHK012 Is the sanitizer-finding discipline upheld — any ASan/TSan finding on the trampoline/worker path is treated as a real defect until disproven (no "benign test artifact" without source-verified proof)? [Edge Case, constitution Testing & Verification] — PASS: plan.md Constitution Check (Testing & Verification) records the default-real discipline; constitution §Testing & Verification §1 requires source-verified proof before downgrading; no carve-out for trampoline/worker path findings.

## Ambiguities & Assumptions

- [x] CHK013 Is the canary RED-leg CI-automation waiver (no deliberate-hang job in the matrix) explicitly tied to the 053 SC-004 precedent, not silently omitted? [Ambiguity, Spec §FR-013/§SC-003, data-model E-4] — PASS: spec §FR-013 (line 104) says "CI-automation waiver under the **same rationale as 053's SC-004 canary**"; E-4 says "waiver (053 SC-004 precedent)"; G-6 records the same; not silently omitted.
- [x] CHK014 Is the completeness-audit obligation (T021) named as the hard `/gate-b` precondition (Article XVII §8 / pre-flight 4d), recording the verdict + the UBSan carry-forward waiver in the verify decision doc? [Ambiguity, tasks.md T021, plan.md Constitution Check] — PASS: tasks.md T021 is marked as a "HARD gate-b precondition (Article XVII §8 / pre-flight 4d)"; plan.md Constitution Check records the obligation; verdict + UBSan carry-forward waiver to be recorded in the verify decision doc at /gate-b.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 14 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 14 |

### SPEC-FIXED items
*(none)*

### DD-DECIDED items
*(none)*

### WAIVED items
*(none)*

Anchors spot-verified: `[2m §6.1]` (line 939 in 2m-pybind.md), `[2m §3.21]` (line 284 in 2m-pybind.md), `[2m §6.7]` (line 1218 in 2m-pybind.md), `[2m §4.6]` mapping table (line 815), `[2m §3.21/§6.1]` composite cite in spec §FR-011, plan.md Constitution Check XX (Article XX routing), tasks.md T021 (gate-b precondition) — all resolve in signed-off revision `.specify/2m-pybind.md` v0.3 (Gate A round 2 converged).
