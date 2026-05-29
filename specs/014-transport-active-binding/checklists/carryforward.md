# Error-Taxonomy & Carry-Forward Requirements Checklist: Live Transport Wiring (014-transport-active-binding)

**Purpose**: Validate that the requirements for the error-slot append / slot-74 cleanup (FR-016) and the five 013/012 Gate-B carry-forward witnesses (FR-012..016) are complete, unambiguous, consistent, and measurable BEFORE implementation. Tests the requirements, not the witnesses.
**Created**: 2026-05-29
**Feature**: [spec.md](../spec.md) · Gate-A trigger: Error semantics

## Requirement Completeness

- [x] CHK001 Are all five carry-forward obligations enumerated with their source waiver (013 item 2/3, 012 RC#C/RC#G/RC#I, 013 slot-74)? [Completeness, Spec §FR-012..016] — PASS: FR-012 = "013 item 3" (sigalg cell); FR-013 = "013 item 2 / 012 RC#G" (counter + bench); FR-014 = "012 RC#C" (PMR-OOM depth); FR-015 = "012 RC#I" (fuzz re-label); FR-016 = "013 slot-74 cleanup." All five are enumerated in the spec with their source waivers. Research R5 cross-references each waiver. The Carry-forward hardening US4 description and FR-012..016 requirements enumerate all five.
- [x] CHK002 Is the slot-74 cleanup decomposed into all its sub-deliverables — new code, the seqnum-manager comment, the return value, the test assertion, and the contract note? [Completeness, Spec §FR-016] — PASS: FR-016 says "Replace the vestigial too-high branch in the seqnum manager that returns the slot-74 `session_test_request_unanswered` stand-in with a dedicated, semantically-correct seqnum-too-high error code (next free error slot; the retired slot remains a permanent numeric hole), updating the comments, the seqnum-manager test assertion, and the contract note." Tasks T026/T027 decompose this into: new enum variant, comment in `seqnum_manager.cpp`, return value change at `:71-78`, assertion at `seqnum_manager_test.cpp:145-150`, and contract note. All sub-deliverables are covered.
- [x] CHK003 Are the fixture requirements specified for FR-012 (Ed25519/Ed448 / unknown-`EVP_PKEY` leaf) and FR-014 (multi-SAN leaf) with WHAT path each must exercise? [Completeness, Spec §FR-012/§FR-014] — PASS: FR-012 says "exercised with an Ed25519/Ed448 (or unknown-`EVP_PKEY`) certificate fixture" and the plan specifies the path: "bringing the 013 US3 `sub_reason` coverage to its full set" — the `sigalg_disallowed` sub_reason cell in `test_tls_validation_failed_taxonomy.cpp`. FR-014 says "a multi-SAN certificate fixture plus a trampoline-targeted fault-injection cell exercising mid/tail allocation sites." Data-model E-5 specifies the fixture names. Path is stated.

## Requirement Clarity

- [x] CHK004 Is the new error slot specified with an exact value and boundary (`session_seqnum_too_high = 120`, after `session_invalid_argument = 119`) and the append-only / no-renumber constraint? [Clarity, Spec §FR-016/§Assumptions] — PASS: FR-016 says "next free error slot." The spec Assumptions section says "Error-slot allocation continues the existing envelope — 013 occupies session slots 116..119; any new code (e.g. the seqnum-too-high replacement for FR-016) takes the next free slot; retired slots remain permanent numeric holes." The contracts/error_slots.hpp explicitly specifies `session_seqnum_too_high = 120` after `session_invalid_argument = 119`, with the append-only and no-renumber constraints. Research R6 states the exact value. All three artifacts agree.
- [x] CHK005 Is "slot 74 keeps its real meaning; only the too-high MISUSE is removed" stated unambiguously (vs retiring slot 74 entirely)? [Clarity, Spec §FR-016] — PASS: FR-016 says "the retired slot remains a permanent numeric hole" for slot 70, and separately for slot 74: the contracts/error_slots.hpp comments say "slot 74 — session_test_request_unanswered: KEEPS its real meaning (FR-004 inbound-liveness window). 014 stops MISUSING it as the seqnum-too-high stand-in…slot 74 itself is unchanged and still emitted for genuine liveness timeouts." The plan's shipped-reality table confirms "slot 74 = `session_test_request_unanswered` real meaning + too-high stand-in." Unambiguous distinction between retiring (slot 70) vs preserving-real-meaning (slot 74).
- [x] CHK006 Is FR-015 scoped clearly as a catalogue/scope **re-label only** — no harness body change, no new parser-touching code? [Clarity, Spec §FR-015] — PASS: FR-015 says "Re-label the fuzz-scope catalogue entry to reflect its actual post-MVP scope." The plan item 7 says "catalogue/scope label re-label only (no code change to the harness body)." Research R5 says "catalogue/scope-label edit on `tests/fuzz/fuzz_transport_handshake.cpp`'s entry only — **no harness body change, no new parser-touching code** (so `[const §VII.7]` needs no new harness)." Clear.
- [x] CHK007 Is FR-013b's handshake bench stated as establishing a NEW baseline (not a ±5% regression gate against a prior number this PR)? [Clarity, Spec §FR-013] — PASS: FR-013 says "wire the `bench/transport/bench_tls_handshake_loopback.cpp` scaffold (012 RC#G; T029 TODO)." The plan §Performance Goals says "This **establishes the first real baseline** (012 RC#G only scaffolded it); it is a measurement baseline, **not** a regression gate against a prior number this PR. Future PRs gate ±5% against it." Clear.

## Requirement Consistency

- [x] CHK008 Is the error-slot envelope consistent across spec, `contracts/error_slots.hpp`, and plan (012 → 94..115, 013 → 116..119, next free = 120; slot 70 permanent hole; slot 74 meaning preserved)? [Consistency, Spec §Assumptions/§FR-016] — PASS: Verified across all three artifacts. Spec Assumptions: "013 occupies session slots 116..119; any new code…takes the next free slot." contracts/error_slots.hpp: documents 012 boundary = 115, 013 = 116-119, new = 120; slot 70 permanent hole; slot 74 meaning preserved. Plan shipped-reality table: "max = `session_invalid_argument = 119`; slot 115 = `transport_accept_cancelled` (012 boundary); slot 70 = permanent hole; slot 74 = `session_test_request_unanswered`." CodeGraph confirms: `session_invalid_argument = 119` is the boundary (error.hpp:627). Consistent across all three.
- [x] CHK009 Is FR-016's "zero behavioural change" claim consistent with the stated fact that all 3 `check_inbound` callers discard the returned code? [Consistency, Spec §FR-016/§Edge Cases] — PASS: The contracts/error_slots.hpp consumer-impact note says "All THREE production callers of `check_inbound` discard the returned code and simply disconnect: `session.cpp:904`, `session.cpp:1261`, `session.cpp:1703`. So swapping 74 → 120 changes no observable session output today." The plan shipped-reality table confirms the 3 callers at those lines "discard the code & disconnect (zero behavioural change on the 74→120 swap)." CodeGraph verified: `session.cpp:904`/`:1261`/`:1703` are confirmed discard sites. Consistent.

## Acceptance Criteria Quality / Measurability

- [x] CHK010 Can SC-006 be objectively verified per-item — each of the five witnesses passes AND the fuzz-scope entry is re-labelled? [Measurability, Spec §SC-006] — PASS: SC-006 says "All five carry-forward witnesses (sigalg cell, once-per-handshake counter, handshake bench, PMR-OOM depth, seqnum too-high code) pass, and the fuzz-scope catalogue entry is re-labelled." Each witness maps to a specific named test/task: T020 (sigalg cell), T021 (counter), T022 (bench), T024 (PMR-OOM), T027 (seqnum assertion flip), T025 (fuzz re-label). Each is independently verifiable (test PASS / enum value present / label text changed). SC-006 is a disjunction of six verifiable sub-criteria.
- [x] CHK011 Is FR-014's requirement to exercise the MID and TAIL allocation sites (not only the boundary) expressed measurably (multi-SAN forces the deeper sites)? [Measurability, Spec §FR-014] — PASS: FR-014 says "a multi-SAN certificate fixture plus a trampoline-targeted fault-injection cell exercising mid/tail allocation sites, not only the boundary site." The measurable criterion is: `throw_on_nth_resource` fires at `N > 1` (mid-SAN construction) and at `N = total_alloc_count` (tail). The test file `test_verify_peer_pmr_oom.cpp` uses `throw_on_nth_resource` parameterized with `N` (task T024). Measurable by N value and sanitizer witness.
- [x] CHK012 Is FR-013a's once-per-handshake invariant (`load_credentials()` == 1) expressed as an observable counter assertion? [Measurability, Spec §FR-013] — PASS: FR-013 says "(a) the `cert_source::load_credentials()` once-per-handshake counter witness." Task T021 says "re-target `test_session_invariant_counter_witness.cpp`…so `cert_source::load_credentials()` == 1 per handshake is genuinely asserted." The test uses a counting wrapper around `cert_source`; the assertion is `calls == 1` after a handshake. Measurable.

## Scenario & Edge-Case Coverage

- [x] CHK013 Is the seqnum-too-high branch's reachability scoped (LogonSent/LogonReceived handshake states; Active intercepted earlier) so the FR-016 witness targets the right path? [Coverage, Spec §Edge Cases/§FR-016] — PASS: The contracts/error_slots.hpp consumer-impact note says "The too-high branch is reachable only in LogonSent / LogonReceived handshake states (Active-state too-high is intercepted earlier → AwaitingResend per 013 FR-009)." The plan shipped-reality table says "Slot-74 too-high callers: 3 sites `co_await seqnum_mgr_.check_inbound(...)` at `session.cpp:904` (LogonSent), `:1261` (Active warm-up), `:1703` (LogonReceived)." Task T027 targets `seqnum_manager.cpp:71-78` and `seqnum_manager_test.cpp:145-150`. The witness targets the right path.
- [x] CHK014 Are the carry-forward witnesses' previously-blocked status documented (each was waived precisely because it needed the live TLS handshake this feature provides)? [Coverage, Spec §FR-013/US4] — PASS: US4 "Why this priority" says "these are explicit waivers blocking catalogue closure." Research R5 documents each carry-forward's blocked status: "FR-012 `sigalg_disallowed`: today noted 'not available in fixtures'"; "FR-013a counter: today records `load_credentials()` as **infeasible/zero** under `mock_transport`"; "FR-013b bench: wire the scaffold…012 RC#G scaffold"; "FR-014: extend `tests/transport/test_verify_peer_pmr_oom.cpp`…012 RC#C"; "FR-015: fuzz re-label." All five are documented with their blockage reason.

## Dependencies & Assumptions

- [x] CHK015 Is the dependency of FR-013a/b on the live loopback fixture (vs the prior infeasibility under `mock_transport`) documented? [Dependency, Spec §FR-013/US4 Independent Test] — PASS: US4 Independent Test says "the once-per-handshake load counter witness; the handshake benchmark fixture" are the witnesses now feasible. Research R5 says "FR-013a counter: `test_session_invariant_counter_witness.cpp` currently records `load_credentials()` as **infeasible/zero** under `mock_transport` (`:22-35`); re-target it at the live fixture." Tasks T021/T022 both have `[P]` markers and note the live-harness dependency (T005). The plan §Technical Context §Testing section explicitly distinguishes live-vs-mock fixture needs. Documented.
- [x] CHK016 Is the assumption that the new slot triggers no abidiff (C++ enum value, not a `fixpp_error_t` C-ABI symbol) documented? [Assumption, Spec §FR-016/plan §Target Platform] — PASS: The plan §Target Platform says "The new `session_seqnum_too_high = 120` is a **C++ enum** value, not a `fixpp_error_t` C-ABI symbol (the 2i C-ABI error mapping is a later feature); `[const §IX.5]` abidiff N/A." The contracts/error_slots.hpp says "C++ enum value ONLY — it is NOT a `fixpp_error_t` C-ABI symbol…so it triggers no abidiff and emits no extern 'C' symbol (`[const §IX.5]`/§X.2)." Explicitly documented.

## Notes

- Check items off as completed: `[x]`; record disposition (PASS / SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>) inline for the step-9 audit.
- FR-015 is doc-only; ensure the audit does not demand a code witness for it (CHK006).

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 16 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **16** |

### SPEC-FIXED items
*(none)*

### DD-DECIDED items
*(none)*

### WAIVED items
*(none)*

Anchors spot-verified:
- `[const §X.4]` — resolves in constitution.md Article X §4 (ABI stability rule: once a numeric value is published in a tagged C ABI release, it never changes meaning; new variants append at unused numeric slots; see also the append-only error slot rule in the spec).
- `[const §IX.5]` — resolves in constitution.md Article IX §5 (abidiff / no `extern "C"` — N/A for C++ enum values only).
- `012 RC#C / RC#G / RC#I` — waiver records cited are in the 012 Gate-B waiver history per `[[project_013_carryforwards_to_014]]` (memory note); individual records not re-verified in detail, but the carry-forward items are enumerated in FR-012..015 with source attribution.
- `session_invalid_argument = 119` — verified present in `include/fixpp/core/error.hpp:627` (boundary confirmed, no drift).
