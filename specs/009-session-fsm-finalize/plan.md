# Implementation Plan — 009-session-fsm-finalize

**Branch**: `009-session-fsm-finalize` | **Date**: 2026-05-22 | **Spec**: [spec.md](spec.md)
**Design anchors**: this slice consumes [`005-session-establishment-fsm`](../005-session-establishment-fsm/) entire bundle as binding — `spec.md` / `plan.md` / `research.md` / `data-model.md` / `quickstart.md` / `contracts/*.hpp`. Upstream Phase-2 anchors `.specify/2e-msgstore.md` v0.4, `.specify/2d-threading.md` v0.4, `.specify/2f-async-mutex.md` v1.5, plus `[FIX-SL]` / `[FIX-TC]`. **The 005 Gate-A-converged design is unchanged; 009 does not amend any of those documents.** On conflict the 005 anchor wins; a divergence is a defect in this plan.

## Summary

Close the **7 binding-contract drift gaps** identified by PR #81 round-1 Gate B Codex hostile review (P1=7 / P2=0 / P3=0; Opus triage confirmed all seven @ P1) against the Gate-A-converged 005 design. Every fix shape is supplied by the Opus round-1 triage Fix Queue in `research/G19-fix-fpml-iso20022/research/reviews/opus_pr81_1_triage.md`. The slice is **drift closure**, not new design — every FR in `spec.md` traces back to an existing 005 contract anchor (`005/contracts/{session,sending_time,session_fsm,admin_messages}.hpp`) or to a documented constitutional invariant (`[const §XI.1-4]`, `async_mutex.hpp:155-160`). Per `[[project_005_phase8_completeness_false_pass]]` the 005 T067 completeness audit verdict (PASS 18/18 FR + 10/10 SC) was a false-PASS for FR-001 / FR-002 / FR-003 / FR-011 / FR-013 — Codex's hostile review caught what existence-mapping missed.

**Branch base**: `009-session-fsm-finalize` is rooted on `005-session-establishment-fsm` HEAD `4e621e1`, inheriting the 25 prior commits (Phase 1–8 + /simplify + /speckit-verify). At Gate B convergence the slice merges back into `005-session-establishment-fsm` (refreshing PR #81's branch) OR directly to `main` (retiring 005's branch and PR #81) — choice deferred to the user at merge time; this plan does not constrain it.

**Gate A is NOT re-run.** The 005 Gate A converged round 3 on 2026-05-18 (record `library/.specify/decisions/005-session-establishment-fsm-gatea.md`); this slice does not modify the design that Gate A reviewed. Per `[const §XVII.1]`, Gate A is triggered by *design* artifacts; an implementation-drift-closure slice that does not edit `spec.md` / `data-model.md` / `contracts/*.hpp` does not re-trigger.

## Technical Context

**Language/Version:** C++23 (`[const §II.1]`). Same as 005 — no fallback. Coroutines (`asio::awaitable<T>`), `std::expected` (via `core::expected_t`), `std::pmr`, `std::span`, `std::chrono`, deducing `this`.

**Primary Dependencies:** **No new Conan row.** Reuses everything 005 already depends on: `fixpp::core` (`expected_t`, `error`, `Clock`/`mock_clock`, `cancellable_dispatch`, `session_executor`), `fixpp::sync::async_mutex` (`[2f §4.1]`; the 005-touched `SeqnumManager` already consumes it — RC#7 is about calling the documented `drain()`), the `[2e §4.1]` `MessageStore` seam (test-double in 005's `tests/support/`), and the merged `wire/` + `dictionary/` surfaces. GoogleTest 1.17.0 + Google Benchmark 1.9.5 pinned from 005.

**Storage:** N/A as an owned store — slice still **consumes** the `[2e §4.1]` seam via the existing test-double from 005's `tests/support/`. No new storage interface; the slice does not change the `MessageStore::store(seq, committed_span, outbound)` contract — it makes `Session::send` actually CALL it (RC#1 / FR-001).

**Testing:** GoogleTest + GoogleMock (C++), TDD red-green-refactor (`[const §VII.1]`/`[const §VII.3]`). Deterministic time via `fixpp::core::mock_clock`; the existing in-memory transport double + test-double `MessageStore`. The 005 `[FIX-TC]` conformance corpus (in-scope subset) is reused unchanged — this slice adds outbound tag-8 + tag-52 assertions to its `tc_*` tests (FR-013) and adds new tests under `tests/session/` for FR-001..FR-011 runtime-behavior coverage (FR-012). **No new fuzz harness** — session FSM is still not parser-touching (`wire/` owns framing/parse fuzzing). **No new Python pytest seam** — no C-ABI surface added (FR-015 inherited from 005).

**Target Platform:** Same Tier-1 matrix as 005: Linux Clang Debug + Release + ASan + UBSan + **TSan** (critical for FR-010 cross-session race + FR-011 drain teardown) + Coverage; GCC Release sanity. Windows Tier 2 manual/nightly. No C-ABI surface added — `[const §IX.5]` abidiff N/A (cited for explicit non-applicability, same as 005).

**Project Type:** C++23 library, `session/` module — **same module as 005, no new module introduced**. Edits land in existing files (`src/session/session.cpp`, `src/session/admin_messages.cpp`, `include/fixpp/session/session.hpp`, `include/fixpp/session/session_config.hpp`); the only new headers introduced are test-only (`tests/session/*` new files for FR-012 runtime coverage). No new public API symbol beyond `session_role` enum on `SessionConfig` (FR-004) and a `next_test_request_id_` member on `Session` (FR-010).

**Performance Goals:** Same ceilings as 005 (`bench/baselines/session/*_baseline.json`). The slice changes do NOT touch the per-op fast paths in a way that should regress baselines:
- `Session::send` wiring (FR-001) adds the stamp+store+emit pipeline — falls under "outbound admin emit ≤ 400ns" budget; existing `store_then_emit` is reused, so the additional cost is the `assign_outbound` (~50ns budget) + `stamp_sending_time` (~60ns budget). Within envelope.
- Admin-builder threading (FR-002/003) replaces fixed-string copies with already-formatted values — neutral.
- TestReqID per-session counter (FR-010) is a 4-byte member-field load + increment instead of a `static` variable — neutral.
- `SeqnumManager::drain` in `Session::close` (FR-011) is on the close path, not steady-state — outside the bench envelope.

±5% gate vs `bench/baselines/session/*.json` per `[const §VIII.2]`; baselines are re-captured at /speckit-verify if any genuine regression appears.

**Constraints:** All inherited from 005 — none new:

- Zero global `new`/`delete` on steady-state inbound-process / timer-fire paths (`[const §VIII.5]` / `[const §XV.1]`). The new FR-001 `Session::send` path MUST be alloc-free under steady state (the existing `store_then_emit` path used by liveness already satisfies this; the slice reuses it).
- All public session surfaces `noexcept` across the inbound-process / timer-fire window (`[arch §5.3]`, FR-015 from 005). FR-007 + FR-008 + FR-009 inbound-validation guards must preserve the noexcept window.
- Coroutines + ASIO native cancellation slots end-to-end; no parallel `stop_token` (`[const §XI.2]`). FR-001's `Session::send` is a coroutine returning `asio::awaitable<expected_t<void>>`.
- Per-session strand serialization (`[const §XI.4]`); `fixpp::sync::async_mutex` for serialized seqnum-counter mutation (`[const §XI.3]`); no `std::mutex` in awaitable headers (`[const §XV.9]`). FR-010 moves the TestReqID counter from process-global static to a per-session member — `[const §XI.4]` per-session-strand serialisation is satisfied (strand serializes one session's work; the member is touched only by that strand).
- FR-011 `SeqnumManager::drain` satisfies the documented `async_mutex` teardown precondition (`include/fixpp/core/sync/async_mutex.hpp:155-160`); this is **enforcing** an existing constitutional invariant, not introducing a new one.
- Session emits **no** `extern "C"` symbol (`[const §X.2]`); `nm` check inherited from 005's T060.
- No new pluggable interface (`[const §XIV.2]` ≤5 pure-virtual cap is satisfied upstream at 2e/2d).

**Scale/Scope:** Estimated edit footprint:

- `include/fixpp/session/session_config.hpp` — add `session_role` enum + `role` field (~10 lines).
- `include/fixpp/session/session.hpp` — add `next_test_request_id_` member (~3 lines).
- `src/session/session.cpp` — wire `Session::send` (~30 lines for FR-001), branch on role in `open()` (~10 lines for FR-004/005), drop Phase-3 `is_logon` compromise (~-15 lines for FR-006), tighten `SendingTime` guards in Active+LogonReceived+LogonSent paths (~20 lines for FR-007/008/009), drain in close phase 2 (~3 lines for FR-011), per-session TestReqID swap (~3 lines for FR-010).
- `src/session/admin_messages.cpp` — thread `begin_string` + `sending_time` through 9 call sites (~50 lines net for FR-002/003); drop `kBeginStringDefault` + `kSendingTimePlaceholder` constants.
- `include/fixpp/session/admin_messages.hpp` — extend 5 builder signatures (~10 lines for FR-002/003).
- `tests/session/` — new runtime-behavior tests: `send_path_test.cpp` (FR-001), expand `logon_handshake_test.cpp` + `tc_establishment_test.cpp` acceptor scenarios (FR-004/005), flip `fsm_transition_matrix_test.cpp::NotConnected_RefusedLogonByBeginString_*` assertion (FR-006), `sending_time_test.cpp` extensions for missing/malformed (FR-007/008/009), `test_test_request_id_cross_session_race.cpp` (FR-010, TSan), `test_seqnum_drain_on_close.cpp` (FR-011), conformance `tc_*` outbound tag-8/52 assertions (FR-013) — estimated ~600-900 LoC test additions across ~5-8 files.

Total: ~800-1200 LoC across ~10-12 files (implementation + tests). Smaller than a typical Phase-4 feature because the design is fixed and the fix shapes are pre-specified by Opus triage.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.* Canonical citation form `[const §<Roman>.<arabic>]` per `constitution.md:5`. **Mood:** at this `/speckit-plan` gate the rows assert *planned conformance* — the slice plans to satisfy each article through its implementation tasks; delivered/verified evidence is produced by `/speckit-implement` + `/speckit-verify`.

**Inheritance rule:** every article that was satisfied by 005's plan + delivered by 005's implementation remains satisfied here unless this slice's edits perturb it. Rows that explicitly carry forward unchanged are marked **inherited from 005**.

| Article cited | Topic | How this slice satisfies it |
|---|---|---|
| `[const §I.1]`,`[const §I.3]`,`[const §I.4]` | Session layer scope; catalogue tracker; no silent omission | **Inherited from 005.** No new catalogue rows; 005-owned rows S-001/2/3/4/7/8/9/15/16/19/20 are still `implementing` per 005 T066 (final `done` flip at merge bookkeeping per T068 + `[[feedback_pipeline_mark_done_step]]`). The scope-deferral ledger in `spec/coverage-index.md` § "005 session-establishment — scope-deferral ledger" is unchanged. |
| `[const §II.1]` | C++23, no earlier fallback | **Inherited.** No language fallback. |
| `[const §III.2]` | Conan, pinned deps | **Inherited — no new Conan row.** |
| `[const §V.1]`,`[const §V.3]`,`[const §V.4]` | AGPL-3.0 dual; no LGPL; vendored attribution | **Inherited.** No new dependency, no new vendored code. Every edited file retains its existing `SPDX-License-Identifier: AGPL-3.0-or-later` header. New test files added carry the same SPDX header. |
| `[const §VI.4]`,`[const §VI.5]` | Bidirectional traceability + Normative References | **Inherited from 005.** This plan adds a single line to `spec/coverage-index.md` at slice close-out: "009-session-fsm-finalize closed the FR-001/002/003/011/013 drift gaps on 005's rows" (close-out bookkeeping). No new normative reference is added. |
| `[const §VII.1]`,`[const §VII.3]` | GoogleTest + TDD | **Inherited.** The new tests authored for FR-001..FR-011 follow red-green-refactor; written before the implementation flip per `[[feedback_subagent_phase_verification_two_traps]]`. |
| `[const §VII.5]` | Conformance corpus (full TC-001..TC-017 every PR) | **Inherited from 005.** Same recorded Article XVII §1 Gate-A-blocker waiver applies (the deferred TC cases are out of scope for 005, out of scope for 009; this slice does not green any deferred case; the in-scope FIX.4.2/4.4 subset remains green and gets a strict outbound-tag-8/52 assertion upgrade per FR-013). No new waiver; no new debt. |
| `[const §VII.6]` | Interop (QuickFIX Logon→…→Logout) | **Inherited.** Real-socket interop still deferred to `transport/`; this slice does not change that disposition. |
| `[const §VII.7]` | Fuzzing on parser-touching modules | **N/A — inherited.** Session FSM still not parser-touching. |
| `[const §VIII.1]`,`[const §VIII.2]`,`[const §VIII.5]` | Benchmark + ±5% + zero hot-path alloc | **Inherited.** Existing `bench/session/*_bench.cpp` + `bench/baselines/session/*.json` apply. No new benches authored — the slice's edits stay within the existing ceilings per the Technical Context analysis above. If FR-001's `Session::send` wiring genuinely shifts a baseline (unlikely — it just calls the already-baselined `store_then_emit`), `/speckit-verify` re-captures. |
| `[const §VIII.4]` | Session throughput parity vs QuickFIX | **Inherited.** Real-socket parity deferred to `transport/`, v1.0 release gate. |
| `[const §IX.1]` | ≥95% line / ≥85% branch on touched modules | Planned: `linux-clang-coverage` measured at `/speckit-verify`. The slice expects coverage on `src/session/session.cpp` + `src/session/admin_messages.cpp` to RISE into the prior 005 W-1..W-4 waiver envelope or better — the new tests close cascading-defensive-branch arms (the existing `MissingSendingTime` / `MalformedSendingTime` / `RefusedLogon → Disconnected` / `Acceptor open()` branches were previously unreachable and therefore unmeasurable, which is exactly why W-1..W-4 read low). 005's W-1..W-4 waivers carry forward only as fallback if measurement still shows residual gaps after the slice lands. |
| `[const §IX.2]` | Tier-1 sanitizers | **Inherited + critical.** ASan+UBSan on every session test; **TSan** mandatory on `test_test_request_id_cross_session_race` (FR-010) AND `test_seqnum_drain_on_close` (FR-011) — these tests EXIST to fire TSan if the fix is wrong. |
| `[const §IX.4]` | Tier-1 static analysis | **Inherited.** clang-tidy + clang-format + cppcheck + IWYU on all edited files; `[const §XV.9]` mutex-in-awaitable grep gate re-asserted at /speckit-verify. |
| `[const §IX.5]` | abidiff vs last tagged ABI | **N/A — inherited.** No C-ABI surface added. |
| `[const §IX.6]` | Two-tier CI | **Inherited.** Tier 1 every preset; Tier 2 Windows manual/nightly. |
| `[const §X.2]`,`[const §X.5]` | No C++ leakage through C ABI; documented reentrancy | **Inherited.** `nm` confirms no `extern "C"`. The `session_role` enum + `next_test_request_id_` member are C++-only — they never appear in `<fix/c_api.h>`. The per-session-strand reentrancy contract in `005/contracts/session.hpp` is preserved by FR-010 (member instead of static) and FR-011 (drain on close before destructor). |
| `[const §X.4]` | Bounded `fixpp_error_t` + forwards-compat | **N/A new variants.** This slice may surface `Session::send` errors via existing 005-defined slots (`store_seqnum_overflow`, transport-error variants). No new error variant is required. If any genuinely-new error class is needed (e.g. `send_transport_cancelled` if it doesn't already exist), it appends at the next free slot per 005's pinning. Audited at /speckit-tasks. |
| `[const §XI.1]`,`[const §XI.2]`,`[const §XI.3]`,`[const §XI.4]`,`[const §XI.5]`,`[const §XI.7]` | Coroutines; ASIO cancellation; async_mutex; per-session strand; lock policy; threading-affecting controls | **Inherited + tightened.** FR-001 `Session::send` is a coroutine returning `asio::awaitable<expected_t<void>>` (XI.1). FR-010 (per-session TestReqID) + FR-011 (drain on close) are **threading-affecting** fixes — both enforce already-planned XI.4/XI.3/XI.7 invariants that 005 declared but the implementation missed. **`/clarify` is NOT re-run** (no new design question; the threading semantics were already cleared in 005's clarifications + Gate A round 1). `/speckit-analyze` runs post-/plan per `[const §XVI.4]`. Gate A is NOT re-run (see "Gate A NOT re-run" rationale in Summary; the design is 005's, already converged). User /plan sign-off applies. |
| `[const §XIII.3]` | Strand-stored trace context, no `thread_local` | **Inherited from 005.** No change to trace propagation. |
| `[const §XIV.2]` | ≤5 pure-virtual on pluggable interfaces | **Inherited — no new pluggable interface.** |
| `[const §XV.1]`,`[const §XV.3]`,`[const §XV.9]`,`[const §XV.15]` | Banned: per-msg heap; global session lock; `std::mutex` in coroutine; app/session drop-oldest | **Inherited + reinforced.** FR-010 explicitly fixes a XV-class banned-pattern (process-global mutable state accessed without synchronization across strands ≈ XV.3 spirit). FR-011 fixes a teardown hazard documented in `async_mutex.hpp:155-160` (XV.9 enforcement). No new bans introduced. |
| `[const §XVI.3]` | `/clarify` mandatory pre-`/plan` (session FSM + threading + error semantics) | **Inherited from 005's clarifications.** Q1/Q2/Q3 from 005 (re-clarified Session-2026-05-18) cover the design space; this slice closes implementation drift against those clarifications. No new clarification opens. The 005 spec.md `## Clarifications` section is the binding record. |
| `[const §XVI.4]` | `/analyze` mandatory post-`/plan` | `/speckit-analyze 009-session-fsm-finalize` runs after `/speckit-tasks` (canonical order per `[[feedback_speckit_pipeline_order_gate_a_before_tasks]]`), before `/speckit-implement`. |
| `[const §XVII.1]` | Codex Gate A before `/tasks` (session FSM + threading + error-semantics triggers) | **Gate A NOT re-run for 009.** Per `[const §XVII.1]` Gate A is triggered by *design* artifacts (spec.md / data-model.md / contracts/*.hpp); a drift-closure slice that does not edit those does not re-trigger. The 005 Gate A converged round 3 (2026-05-18) is the binding sign-off; this slice's `spec.md` Assumption #1 explicitly asserts "design unchanged"; this plan's Constitution Check Inheritance rule applies. **If `/speckit-analyze` (next phase) detects any FR or task that does in fact require a design change, surface it and re-decide.** Otherwise this row is satisfied transitively. |
| `[const §XVII.2]`,`[const §XVII.3]` | Gate B before merge; author≠reviewer | Standard Gate B precondition for the slice. Codex sessions (review + Codex-fixer if rounds 3-4 are needed) are independent of any prior Codex session by construction. |
| `[const §XVII.7]` | Local pre-PR build gate | Contributor confirms `local build: green on linux-clang-debug @ <sha>` before opening the slice PR. |
| `[const §XVII.8]` | `/speckit-verify` mandatory post-`/implement` | `/speckit-verify 009-session-fsm-finalize` → `.specify/decisions/009-session-fsm-finalize-verify.md`; non-RED required for the Gate B label. The completeness audit (T067-equivalent for 009) MUST adopt the audit-test-bodies rule per spec.md Assumption "Test-quality binding" + `[[project_005_phase8_completeness_false_pass]]`. |

**Gates — PASS, inherited from 005.** No new violations introduced; the recorded `[const §VII.5]` waiver remains as-is (no new deferred TC case). No new pluggable interface, no new C-ABI surface, no new dependency, no new design question. Gate A is NOT re-run (see `[const §XVII.1]` row + Summary). Complexity Tracking below is intentionally empty.

## Project Structure

### Documentation (this feature)

```text
specs/009-session-fsm-finalize/
├── plan.md              # this file (/speckit-plan 2026-05-22)
├── spec.md              # /specify 2026-05-22 — drift closure FRs traced to 005 contracts
├── research.md          # Phase 0 — minimal: design inherited; 3 D-entries confirming Opus-triage micro-decisions
├── data-model.md        # Phase 1 — no new entities; lists edits to existing E1..E9 from 005
├── quickstart.md        # Phase 1 — build / test / TSan / coverage / verify / gate-b instructions for the slice
├── contracts/
│   └── session_role.hpp # the ONLY new shape oracle: the session_role enum (the rest of the contracts are 005's, unchanged)
├── checklists/
│   └── requirements.md  # /specify quality checklist (all pass, no clarifications needed)
└── tasks.md             # Phase 2 (/speckit-tasks — NOT created here)
```

### Source code (library submodule root)

Edits only — no new directory created. Affected files:

```text
include/fixpp/session/
├── session.hpp              # ADD: std::uint32_t next_test_request_id_ = 0; member (FR-010)
├── session_config.hpp       # ADD: session_role enum + role field (FR-004); 005 owns the rest
└── admin_messages.hpp       # MODIFY: extend 5 builder signatures with begin_string + sending_time params (FR-002/003)

src/session/
├── session.cpp              # MODIFY: wire Session::send (FR-001); branch open() on role (FR-004); drop Phase-3 NotConnected-preserve (FR-006); tighten SendingTime guards (FR-007/008/009); drain in close phase 2 (FR-011); swap tr_counter to member (FR-010)
└── admin_messages.cpp       # MODIFY: drop kBeginStringDefault + kSendingTimePlaceholder; thread params through 9 call sites (FR-002/003)

tests/session/
├── send_path_test.cpp                              # NEW: FR-001 runtime test (assign+stamp+store+emit ordering, FIX.4.2+4.4)
├── logon_handshake_test.cpp                        # MODIFY: rewrite acceptor scenarios to exercise NotConnected→LogonReceived→Active (FR-004/005)
├── conformance/tc_establishment_test.cpp           # MODIFY: rewrite Scenario1a/1b acceptor cases (FR-005)
├── conformance/{tc_logout,tc_reject,tc_liveness,tc_sendingtime}_test.cpp  # MODIFY: assert tag 8 + tag 52 on every outbound frame (FR-013)
├── fsm_transition_matrix_test.cpp                  # MODIFY: flip NotConnected_RefusedLogonByBeginString assertion (FR-006)
├── sending_time_test.cpp                           # MODIFY: add MissingSendingTimeInActiveRejects + MalformedSendingTimeInActiveRejects + LogonSent-special variants (FR-007/008/009)
├── test_test_request_id_cross_session_race.cpp     # NEW: TSan two-session concurrent-liveness regression (FR-010, SC-003)
└── test_seqnum_drain_on_close.cpp                  # NEW: acquire-mutex + close + destroy without terminate (FR-011, SC-004)
```

**Structure Decision**: edits to existing `session/` module + a small set of new test files. **No new source-tree directory introduced.** New test files added under `tests/session/` and `tests/session/conformance/` follow the existing 005 naming pattern (e.g., `tc_*_test.cpp` for conformance scenarios). The only new public-API symbol is the `session_role` enum on `SessionConfig`; everything else is private state or test code.

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

Intentionally empty — no constitutional violations introduced. The existing 005 `[const §VII.5]` Article XVII §1 recorded Gate-A-blocker waiver remains in force unchanged; this slice does not green any deferred TC case nor add a new deferred case.

## Citation verification pass

Run against `library/.specify/constitution.md` (v0.1, 2026-05-10) and the 005 Phase-2 anchors. Every `[const §X.Y]` cited above resolves to an extant article/section. The single non-trivial cross-doc citation — `async_mutex.hpp:155-160,683-690` for the `SeqnumManager::drain` teardown precondition — is verified against the live header on this branch. The 005 spec/plan/contracts cross-citations are verified against the merged tree under `specs/005-session-establishment-fsm/`.

## Pipeline progress

| Step | Status | Date | Notes |
|---|---|---|---|
| `/speckit-specify` | DONE | 2026-05-22 | spec.md authored with 13 FRs traced to 7 PR #81 RCs; checklist GREEN no NEEDS CLARIFICATION |
| `/speckit-clarify` | **N/A** | — | No new design question; 005's clarifications cover. |
| `/speckit-plan` | DONE | 2026-05-22 | this file |
| Gate A | **N/A** | — | Inherited from 005 (converged 2026-05-18). See `[const §XVII.1]` row + Summary. |
| `/speckit-tasks` | next | — | Generate dependency-ordered tasks closing FR-001..FR-013 against the existing 005 source tree |
| `/speckit-analyze` | after tasks | — | Mandatory per `[const §XVI.4]`; spec-analyzer subagent per `[[feedback_speckit_analysis_subagents]]` |
| `/speckit-checklist` (domain) | after analyze | — | Per pipeline.md step 7 |
| `/speckit-checklist-audit` | after checklist | — | Mandatory per `[const §XVII.8]` + `[[feedback_follow_pipeline_md_not_plan_summary]]`; checklist-auditor subagent per `[[feedback_speckit_analysis_subagents]]` |
| `/speckit-implement` | after audit | — | Sonnet subagents per phase per `[[feedback_speckit_subagent_phasing]]`; parent re-runs build/test gate between phases per `[[feedback_self_run_build_gate]]` |
| `/simplify` | after implement | — | 3 general-purpose review agents per `[[feedback_speckit_simplify_before_verify]]` |
| `/speckit-verify` | after simplify | — | Mandatory per `[const §XVII.8]`; emits `009-session-fsm-finalize-verify.md` |
| Feature-completeness audit | with verify | — | Mandatory per `[const §XVII.8]` + `[[project_005_phase8_completeness_false_pass]]`; MUST audit test BODIES (not just file-naming) |
| `/gate-b` | after verify+completeness | — | Mandatory per `[const §XVII.2]`; either refreshes PR #81 or opens new PR |

## Next

`/speckit-tasks` (Phase 2). One Sonnet subagent per task-phase per `[[feedback_speckit_subagent_phasing]]`; parent re-runs the build/test gate before the next phase per `[[feedback_self_run_build_gate]]`. After /tasks → `/speckit-analyze` → `/speckit-checklist` → `/speckit-checklist-audit` → `/speckit-implement` → `/simplify` → `/speckit-verify` (+ completeness audit on test bodies) → `/gate-b`.
