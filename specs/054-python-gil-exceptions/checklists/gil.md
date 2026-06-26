# GIL Discipline Checklist: Systematic GIL Release/Reacquire (PY-002)

**Purpose**: Requirements-quality gate for the GIL-discipline axis — the audit table, the release on blocking wrappers, the trampoline reacquire census, the discriminating release canary, and the `[2m]` reentrancy amendment. Audience: Gate B reviewer.
**Created**: 2026-06-26
**Feature**: [spec.md](../spec.md) · contract: [gil-discipline-contract.md](../contracts/gil-discipline-contract.md) · data-model: [data-model.md](../data-model.md) E-1/E-4/E-5/E-6

## Requirement Completeness

- [ ] CHK001 Is the GIL-discipline audit table required to be EXHAUSTIVE over the `%include`d C-ABI surface (every wrapped function classified release/hold with a one-line justification), not a grouped sample — so a reviewer can mechanically diff it against `fixpp.i`? [Completeness, Spec §FR-001/§SC-007, contract G-3, data-model E-1]
- [ ] CHK002 Are the three blocking wrappers that MUST release the GIL named precisely (`session_close`, `session_send`, `engine_destroy`), with the band covering only `$action` (in-typemap before, out-typemap after)? [Completeness, Spec §FR-002, contract G-1, data-model E-1]
- [ ] CHK003 Is the bound-trampoline census required to state the conclusion explicitly — exactly one bound trampoline (`fixpp_py_recv_trampoline`), the `toApp`/send callback `%ignore`d/unbound, no state callback — so "reacquire on every trampoline" is not read as a gap against an unbound path? [Completeness, Spec §FR-003, contract G-2, data-model E-1]
- [ ] CHK004 Is the `[2m]` reentrancy amendment required to be a CENSUS of all four send-from-callback sites (§1.3 rule 2, §3.12, §6.5 carve-out table, §4.6 `CallbackReentrantClose` docstring), with leaving any site unamended named as making the doc self-contradicting? [Completeness, Spec §FR-005, contract G-5, data-model E-6]

## Requirement Clarity

- [ ] CHK005 Is the release-canary witness specified as TWO-MODE and discriminating — normal build runs the teardown-vs-recv-callback scenario and completes GREEN (in-matrix); the `FIXPP_PY_GIL_RELEASE_CANARY` build runs it in a subprocess and HANGS (RED, hard-timeout) — and is the non-discriminating "two threads both send" substitute explicitly rejected? [Clarity, Spec §FR-004/§SC-003, contract G-4, data-model E-4]
- [ ] CHK006 Is the release canary distinguished from 053's `FIXPP_PY_GIL_CANARY` (reacquire canary → segfault), so the two coexisting macros are not confused? [Clarity, contract G-4, data-model E-4]
- [ ] CHK007 Is L-054-1 characterized precisely — `session_send`-from-callback deadlocks as a strand/io_context reentrancy deadlock (as-built 050 blocking `co_spawn(ioc_,…,use_future)`+`fut.get()`), DISTINCT from and unaffected by the 053 GIL-teardown deadlock, and a current limitation (not permanent-forbidden)? [Clarity, Spec §FR-005, contract G-5, data-model E-6]

## Requirement Consistency

- [ ] CHK008 Are the as-built mechanism anchors consistent across spec/contract/data-model (`src/capi/session.cpp:284-286` send / `:202-205` close; deadlock rule `session.h:255-258`), and do they actually exist in the shipped source? [Consistency, Spec §FR-005, contract G-5, data-model E-6]
- [ ] CHK009 Does the audit table's classification of the three blocking wrappers (release) agree with FR-002's named set and with the macro-guarded bands the canary elides? [Consistency, data-model E-1, contract G-1/G-4]

## Acceptance Criteria Quality

- [ ] CHK010 Is SC-003's "discriminating" requirement objectively checkable — proven RED under the canary AND GREEN without it (the proof, not an assumption), with the RED leg local-only and the GREEN leg in-matrix? [Measurability, Spec §SC-003, contract G-4/G-6]
- [ ] CHK011 Is SC-007 (audit accounts for every wrapped function; census matches `fixpp.i` exactly) verifiable by a mechanical diff, not a subjective judgement? [Measurability, Spec §SC-007, contract G-3]

## Edge Case Coverage

- [ ] CHK012 Is the case of a NEWLY-identified blocking wrapper (beyond the three PY-001 covers) handled — the audit is the census that brings any such function under the same release discipline? [Edge Case, Spec §FR-002, data-model E-1]
- [ ] CHK013 Is `dict_load_from_xml` correctly classified hold-with-note (CPU-bound XML parse, no engine round-trip → no worker-deadlock class; releasing for throughput is a deferred nicety, not a correctness gap)? [Edge Case, data-model E-1]

## Ambiguities & Assumptions

- [ ] CHK014 Is the binding-level guarantee for L-054-1 explicitly scoped as DOCUMENTARY (callback docstring) with active detection (`session._in_callback` + `CallbackReentrantClose`/1204 pre-call) deferred to PY-004 — so no enforcement is silently assumed in 054? [Ambiguity, Spec §FR-005, contract G-5]
- [ ] CHK015 Is the constructibility of the discriminating hang scenario (the PY-001 deadlock shape) stated as the assumption, with FR-004 requiring a documented limitation if it proves NOT constructible (rather than substituting a non-discriminating test)? [Ambiguity, Spec §FR-004]
