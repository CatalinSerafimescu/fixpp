# Implementation Plan — 010-session-cfg-lifetime

**Branch**: `010-session-cfg-lifetime` | **Date**: 2026-05-23 | **Spec**: [spec.md](spec.md)
**Design anchors**: this slice consumes [`005-session-establishment-fsm`](../005-session-establishment-fsm/) entire bundle as binding — `spec.md` / `plan.md` / `research.md` / `data-model.md` / `quickstart.md` / `contracts/*.hpp`. Upstream Phase-2 anchors `.specify/2e-msgstore.md` v0.4, `.specify/2d-threading.md` v0.4, `.specify/2f-async-mutex.md` v1.5, plus `[FIX-SL]` / `[FIX-TC]`. The bundled deferrals (F-04 / F-05 / F-06 / F-07 + E1 / F-11 / RC#G mixed-path) are PR #82 Gate B waivers recorded against the 005 design; the closing tactic for each is implementation + test, **never** a design amendment. **The 005 Gate-A-converged design is unchanged; 010 does not amend any of those documents.** On conflict the 005 anchor wins; a divergence is a defect in this plan.

## Summary

Close the **6 residual P2/P3 + 4 coverage-carry-forward waivers** recorded against PR #82 Gate B (`library/.specify/decisions/009-session-fsm-finalize-gateb.md`, tracked audit `phases/phase-4/session/009-session-fsm-finalize.md`) **plus** the load-bearing **W-5 pre-existing 005-baseline ASan use-after-scope** on the `cfg_` reference at `src/session/session.cpp:116` (declaration `include/fixpp/session/session.hpp:281`, `const SessionConfig& cfg_;`).

W-5 is the primary subject — every other bundled item lives at the same touch surface (`session.cpp` / `session.hpp` / `tests/session/`) and rides along to avoid a second slice. The W-5 mechanism was settled at `/speckit-clarify` (2026-05-23, see `spec.md ## Clarifications`): **by-value `SessionConfig cfg_;`** — the `Session` constructor copies the caller's config into a same-typed member. No sharing across sessions; caller may freely drop or mutate their config after the ctor returns. Each `Session` owns its own snapshot.

**Branch base**: `010-session-cfg-lifetime` is rooted on the post-PR-#82-merge `main` of the library submodule (`ba2222d`). No 009-branch carry-overs; the 005 / 009 work is fully merged. At Gate B convergence the slice merges directly to `main` and the PR closes the W-5 waiver row in the 009 Gate B decision record (SC-009).

**Gate A NOT re-run.** The 005 Gate A converged round 3 (2026-05-18); the W-5 implementation choice is an implementation-level decision settled at `/speckit-clarify` (no new design space opened); the bundled deferrals are coverage / observability ride-alongs that do not amend `spec.md` / `data-model.md` / `contracts/*.hpp` of 005. Gate A is **inherited from 005 with a 010-specific addendum** (per FR-012 option ii, identical pattern to 009; addendum lives in the local-only `library/.specify/decisions/010-session-cfg-lifetime-gatea.md` evidence file produced at the Gate B precondition step, citing the /clarify decision + the three /plan micro-decisions D-1/D-2/D-3 below). Per `[const §XVII.1]` Gate A is triggered by *design* artifacts; an implementation-drift-closure slice that does not edit those does not re-trigger.

## Technical Context

**Language/Version:** C++23 (`[const §II.1]`). Same as 005 / 009 — no fallback. Coroutines (`asio::awaitable<T>`), `std::expected` (via `core::expected_t`), `std::pmr`, `std::span`, `std::chrono`, deducing `this`.

**Primary Dependencies:** **No new Conan row.** Reuses everything 005 / 009 already depend on: `fixpp::core` (`expected_t`, `error`, `Clock`/`mock_clock`, `cancellable_dispatch`, `session_executor`), `fixpp::sync::async_mutex` (`[2f §4.1]`), the `[2e §4.1]` `MessageStore` seam (test-double in `tests/support/`), the merged `wire/` + `dictionary/` surfaces. GoogleTest 1.17.0 + Google Benchmark 1.9.5 pinned.

**Storage:** N/A as an owned store. Slice still consumes the `[2e §4.1]` seam via the existing test-double from `tests/support/`.

**Testing:** GoogleTest + GoogleMock (C++), TDD red-green-refactor (`[const §VII.1]` / `[const §VII.3]`). Deterministic time via `fixpp::core::mock_clock`; existing in-memory transport double + test-double `MessageStore`. The 005 `[FIX-TC]` conformance corpus is reused unchanged — this slice does not add new TC cases; the FSM matrix witness (FR-006) covers cells from the existing 005 data-model state set. Sanitizer matrix per `[const §IX.2]` (ASan + UBSan + **TSan** + GCC release sanity) — ASan is **critical** because the W-5 exit criterion is exactly the ASan-clean re-enablement of `session_coverage_adversarial` (FR-003).

**Target Platform:** Same Tier-1 matrix as 005 / 009: Linux Clang Debug + Release + ASan + UBSan + TSan + Coverage; GCC Release sanity. Windows Tier 2 manual/nightly. No C-ABI surface added — `[const §IX.5]` abidiff N/A (same as 005 / 009).

**Project Type:** C++23 library, `session/` module — **same module as 005 / 009, no new module introduced**. Edits land in existing files (`src/session/session.cpp`, `include/fixpp/session/session.hpp`, `include/fixpp/core/error.hpp`); the only new headers introduced are test-only (`tests/session/*` new files for FR-006 + FR-008 + FR-009). One new public symbol on `Session`: a read-only `fsm_visit_history()` accessor (FR-004 D-2 ring-buffer mechanism) that is always-on, zero-cost-when-unread, populated synchronously on every transition. One new `error` enum variant at slot 77 (FR-005 D-3).

**Performance Goals:** Same ceilings as 005 / 009 (`bench/baselines/session/*_baseline.json`). The slice changes do NOT touch the per-op fast paths in a way that should regress baselines:

- **FR-001 by-value `cfg_`** — the ctor copy of `SessionConfig` happens **once** at `Session` ctor; not on any steady-state path. `SessionConfig` is a value-type aggregate of small POD-ish members (executor handle, role, BeginString, heartbeat interval, comp ID short strings, policy enums) — copy is ≤300 bytes, ~50ns at the ctor. Outside every bench envelope. Read sites change from `(&cfg_)->member` to `cfg_.member` — identical addressing post-inline.
- **FR-004 FSM visit history ring buffer** — 16-entry fixed `std::array<fsm_state, 16>` + a `std::uint8_t` count. Push = 1 store + 1 modular-increment. Cost ~1ns per transition. Transition count per session lifetime is bounded by `[FIX-SL §4.10]` cell count (≤30 visits in any practical scenario). Outside every bench envelope.
- **FR-005 dedicated error variant** — adds one enum value; the call sites in `Session::send` change `error::session_invalid_logon` → `error::session_invalid_state_for_send`. Single-byte enum value swap; neutral.
- **FR-007 admin-builder distinct now_str** — already implemented (PR #82 RC#G covers it); this slice adds the **test** that exercises the per-message-distinct branch. No production code change.
- **FR-008 mixed-path bookkeeping witnesses** — tests-only, no production code change.
- **FR-009 initiator transport-throw witness** — tests-only.

±5% gate vs `bench/baselines/session/*.json` per `[const §VIII.2]`; baselines are re-captured at /speckit-verify if any genuine regression appears.

**Constraints:** All inherited from 005 / 009 — none new:

- Zero global `new`/`delete` on steady-state inbound-process / timer-fire paths (`[const §VIII.5]` / `[const §XV.1]`). The by-value `cfg_` copy is one-time at ctor — outside the steady-state envelope. The visit-history ring buffer is on the FSM-transition path; pushing to a `std::array` is alloc-free.
- All public session surfaces `noexcept` across the inbound-process / timer-fire window (`[arch §5.3]`). The new `fsm_visit_history()` accessor returns `std::span<const fsm_state>` — `noexcept`. The new `error::session_invalid_state_for_send` variant flows through `co_return std::unexpected(...)` exactly like the existing variants.
- Coroutines + ASIO native cancellation slots end-to-end; no parallel `stop_token` (`[const §XI.2]`). Unchanged.
- Per-session strand serialization (`[const §XI.4]`); `fixpp::sync::async_mutex` for serialized seqnum-counter mutation (`[const §XI.3]`); no `std::mutex` in awaitable headers (`[const §XV.9]`). Unchanged. The visit-history ring buffer is touched only on the FSM-transition path which runs on the per-session strand — single-writer, single-reader (the test) only AFTER the operation completes. No new sync primitive required.
- Session emits **no** `extern "C"` symbol (`[const §X.2]`); `nm` check inherited.
- No new pluggable interface (`[const §XIV.2]`).

**Scale/Scope:** Estimated edit footprint:

- `include/fixpp/session/session.hpp` — flip `const SessionConfig& cfg_;` (line ~281) to `SessionConfig cfg_;` (~1 line); add `std::array<fsm_state, 16> fsm_visit_history_; std::uint8_t fsm_visit_count_;` member (~3 lines); add public `fsm_visit_history() const noexcept` accessor returning `std::span<const fsm_state>` (~5 lines). **Total ~9 lines.**
- `include/fixpp/session/session_config.hpp` — verify copyability; no expected edit. **0 lines.**
- `include/fixpp/core/error.hpp` — add `session_invalid_state_for_send = 77,` variant after `session_invalid_config = 76,` with comment block in the existing format (~6 lines including comment).
- `src/session/session.cpp` — ctor: drop the reference-binding pattern (line ~116 and the initializer list), instantiate `cfg_` by copy from the ctor param (~3 lines edited). Add a small inline `record_state_transition_(fsm_state)` helper called from each `fsm_state_ = X;` site (~10 sites identified at lines 239, 292, 376, 388, 630, 641, 648, 659, 690, 696, 702, 712, …) — net ~15 lines including the helper definition. Replace `co_return std::unexpected(error::session_invalid_logon);` at line 1151 (and the symmetric site) with `error::session_invalid_state_for_send` (~2 lines).
- `tests/session/CMakeLists.txt` — remove the `if(FIXPP_ENABLE_ASAN) set_tests_properties(session_coverage_adversarial PROPERTIES DISABLED TRUE) endif()` block added in PR #82 (~10 lines deleted including the comment).
- `tests/session/fsm_matrix_witness_test.cpp` — **NEW**. One witness per cell of the 6-state × N-event matrix. ~400-500 LoC.
- `tests/session/admin_builder_distinct_now_test.cpp` — **NEW**. `clock->advance(...)` between two emits across Heartbeat / TestRequest / Logout / Reject; assert distinct `SendingTime`. ~80-120 LoC.
- `tests/session/admin_emit_mixed_path_test.cpp` — **NEW**. Four mixed-success-mode permutations at sites 1+2 (Reject-ok/Logout-ok, Reject-fail/Logout-skip, Reject-ok/Logout-fail, Reject-fail/Logout-fail). ~150-200 LoC.
- `tests/session/initiator_transport_throw_test.cpp` — **NEW**. RED: `transport.send` throws during initiator Logon emit on `Session::open()`. ~60-80 LoC.
- `tests/session/<existing files>` — update assertions at the two `Session::send` non-Active sites from `session_invalid_logon` to `session_invalid_state_for_send` (FR-005 AC3). Grep at /speckit-tasks to pin the file list; estimated ≤5 LoC across 1-2 files.
- `tests/session/cfg_lifetime_safety_test.cpp` — **NEW**. Construct `SessionConfig` as a local in a nested scope, pass to `Session`, drop the config, exercise the session, assert ASan clean. ~80 LoC.

Total: ~800-1100 LoC across ~12 files (mostly tests). The production edits are surgical (≈30 net lines).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.* Canonical citation form `[const §<Roman>.<arabic>]` per `constitution.md:5`. **Mood:** at this `/speckit-plan` gate the rows assert *planned conformance* — the slice plans to satisfy each article through its implementation tasks; delivered/verified evidence is produced by `/speckit-implement` + `/speckit-verify`.

**Inheritance rule:** every article that was satisfied by 005's plan + delivered by 005's implementation + 009's drift closure remains satisfied here unless this slice's edits perturb it. Rows that explicitly carry forward unchanged are marked **inherited from 005/009**.

| Article cited | Topic | How this slice satisfies it |
|---|---|---|
| `[const §I.1]`, `[const §I.3]`, `[const §I.4]` | Session layer scope; catalogue tracker; no silent omission | **Inherited from 005/009.** No new catalogue rows; 005-owned rows are now `done` post-009-merge. No scope deferral added. SC-009 explicitly closes the W-5 row in 009 Gate B. |
| `[const §II.1]` | C++23, no earlier fallback | **Inherited.** No language fallback. |
| `[const §III.2]` | Conan, pinned deps | **Inherited — no new Conan row.** |
| `[const §V.1]`, `[const §V.3]`, `[const §V.4]` | AGPL-3.0 dual; no LGPL; vendored attribution | **Inherited.** No new dependency, no new vendored code. Every edited file retains its existing `SPDX-License-Identifier: AGPL-3.0-or-later` header; new test files added carry the same SPDX header. |
| `[const §VI.4]`, `[const §VI.5]` | Bidirectional traceability + Normative References | **Inherited.** This plan adds a single line to `spec/coverage-index.md` at slice close-out noting the W-5 + bundled-deferral closure. No new normative reference. |
| `[const §VII.1]`, `[const §VII.3]` | GoogleTest + TDD | **Inherited.** The new tests authored for FR-001..FR-009 follow red-green-refactor; written before the implementation flip per `[[feedback_subagent_phase_verification_two_traps]]`. |
| `[const §VII.5]` | Conformance corpus (full TC-001..TC-017 every PR) | **Inherited from 005/009.** Same recorded Article XVII §1 Gate-A-blocker waiver applies (no new deferred TC case). The in-scope FIX.4.2/4.4 subset remains green; the FSM matrix witness (FR-006) is internal coverage, not a new TC case. |
| `[const §VII.6]` | Interop (QuickFIX Logon→…→Logout) | **Inherited.** Real-socket interop still deferred to the separate per-release interop gate. |
| `[const §VII.7]` | Fuzzing on parser-touching modules | **N/A — inherited.** Session FSM still not parser-touching. |
| `[const §VIII.1]`, `[const §VIII.2]`, `[const §VIII.5]` | Benchmark + ±5% + zero hot-path alloc | **Inherited.** Existing `bench/session/*_bench.cpp` + `bench/baselines/session/*.json` apply. No new benches authored — the by-value cfg copy is a one-time ctor cost (≤50ns, outside every envelope); the visit-history push is ~1ns per transition. If a baseline genuinely shifts, `/speckit-verify` re-captures. |
| `[const §VIII.4]` | Session throughput parity vs QuickFIX | **Inherited.** Real-socket parity deferred to v1.0 release gate. |
| `[const §IX.1]` | ≥95% line / ≥85% branch on touched modules | Planned: `linux-clang-coverage` measured at `/speckit-verify`. The slice expects coverage on `src/session/session.cpp` + `include/fixpp/session/session.hpp` to RISE into the prior PR #82 W-1..W-4 envelope or better — FR-006 (matrix per-cell) + FR-007 (admin distinct now_str) + FR-008 (mixed-path) + FR-009 (initiator throw) close cascading-defensive branches that previously had no test reach. W-1..W-4 from 009 carry forward as **fallback only**; the expected outcome is automatic clearance, recorded in `.specify/decisions/010-session-cfg-lifetime-verify.md`. |
| `[const §IX.2]` | Tier-1 sanitizers | **Inherited + critical.** ASan+UBSan on every session test; the W-5 acceptance criterion (FR-003 / SC-001) is the ASan-clean re-enablement of `session_coverage_adversarial`. TSan unchanged (no new threading contract introduced). |
| `[const §IX.4]` | Tier-1 static analysis | **Inherited.** clang-tidy + clang-format + cppcheck + IWYU on all edited files; `[const §XV.9]` mutex-in-awaitable grep gate re-asserted at /speckit-verify. |
| `[const §IX.5]` | abidiff vs last tagged ABI | **N/A — inherited.** No C-ABI surface added. The new `fsm_visit_history()` accessor and `error::session_invalid_state_for_send` variant are C++-only — they never appear in `<fix/c_api.h>`. |
| `[const §IX.6]` | Two-tier CI | **Inherited.** Tier 1 every preset; Tier 2 Windows manual/nightly. |
| `[const §X.2]`, `[const §X.5]` | No C++ leakage through C ABI; documented reentrancy | **Inherited.** `nm` confirms no `extern "C"`. The per-session-strand reentrancy contract in `005/contracts/session.hpp` is preserved by the by-value `cfg_` (the cfg lives in the Session, accessed only on its strand). |
| `[const §X.4]` | Bounded `fixpp_error_t` + forwards-compat | **One new variant** `session_invalid_state_for_send = 77` (FR-005, D-3). Appended at the next free slot after `session_invalid_config = 76`; pre-D-12 C-ABI prefix-group coalescing maps it to `FIXPP_ERR_SESSION_REJECT` (it is a state-mismatch reject for an outbound send, semantically analogous to `session_msg_type_invalid_for_state`). Within constitution-permitted forwards-compat envelope; documented in `contracts/session_error_state_for_send.hpp` (the only contract artifact this slice introduces). |
| `[const §XI.1]`, `[const §XI.2]`, `[const §XI.3]`, `[const §XI.4]`, `[const §XI.5]`, `[const §XI.7]` | Coroutines; ASIO cancellation; async_mutex; per-session strand; lock policy; threading-affecting controls | **Inherited.** The by-value `cfg_` change does not perturb the threading model — the session ctor still copies the cfg on the constructor's caller-thread; subsequent reads of `cfg_` happen on the per-session strand exactly as before. The visit-history ring buffer is single-writer (the FSM transition site on the per-session strand); reads by tests happen *after* the operation, with strand-completion as the happens-before edge. No new sync primitive; no `std::mutex` in awaitable headers introduced. **Threading-affecting controls (XI.7) — N/A:** the slice introduces no new threading-affecting public knob; the visit-history accessor is read-only and observation-only. |
| `[const §XIII.3]` | Strand-stored trace context, no `thread_local` | **Inherited.** No change to trace propagation. |
| `[const §XIV.2]` | ≤5 pure-virtual on pluggable interfaces | **Inherited — no new pluggable interface.** The visit-history is a concrete in-class member, not a hook. |
| `[const §XV.1]`, `[const §XV.3]`, `[const §XV.9]`, `[const §XV.15]` | Banned: per-msg heap; global session lock; `std::mutex` in coroutine; app/session drop-oldest | **Inherited + reinforced.** The by-value `cfg_` change eliminates a UAF (= XV-spirit memory-safety regression) without introducing any banned pattern. Visit-history is `std::array`-based (no heap). |
| `[const §XVI.3]` | `/clarify` mandatory pre-`/plan` | **Inherited + satisfied.** `/speckit-clarify` ran 2026-05-23 and resolved the FR-001 W-5 mechanism question; the resulting `## Clarifications` section is the binding record. The deferred items (F-04 seam mechanism, F-07/E1 variant name, Gate A inheritance) are **plan-level** micro-decisions resolved in `research.md` D-1/D-2/D-3 below — not /clarify questions. |
| `[const §XVI.4]` | `/analyze` mandatory post-`/plan` | `/speckit-analyze 010-session-cfg-lifetime` runs after `/speckit-tasks` (canonical order per `[[feedback_speckit_pipeline_order_gate_a_before_tasks]]`), before `/speckit-implement`. Spec-analyzer subagent per `[[feedback_speckit_analysis_subagents]]`. |
| `[const §XVII.1]` | Codex Gate A before `/tasks` (session FSM + threading + error-semantics triggers) | **Gate A NOT re-run for 010.** Per `[const §XVII.1]` Gate A is triggered by *design* artifacts (spec.md / data-model.md / contracts/*.hpp); a waiver-closure slice that does not edit 005's design does not re-trigger. The 005 Gate A converged round 3 (2026-05-18) is the binding sign-off; this slice's `spec.md` Assumption "Binding 005 design unchanged" explicitly asserts it; FR-010 mandates it. **Inheritance + addendum** (FR-012 option ii): produce `library/.specify/decisions/010-session-cfg-lifetime-gatea.md` (local-only, gitignored) at the Gate B precondition step, citing the /clarify decision + research D-1/D-2/D-3. If `/speckit-analyze` (next phase) detects any FR or task that does in fact require a design change, surface it and re-decide. |
| `[const §XVII.2]`, `[const §XVII.3]` | Gate B before merge; author≠reviewer | Standard Gate B precondition. Codex sessions (review + Codex fixer if rounds 3-4 needed) are independent of any prior Codex session by construction (`[[feedback_gate_a_codex_dual_pass]]` + `[[feedback_codex_rescue_readonly_mount]]`). |
| `[const §XVII.7]` | Local pre-PR build gate | Contributor confirms `local build: green on linux-clang-debug @ <sha>` before opening the slice PR. |
| `[const §XVII.8]` | `/speckit-verify` mandatory post-`/implement` | `/speckit-verify 010-session-cfg-lifetime` → `.specify/decisions/010-session-cfg-lifetime-verify.md`; non-RED required for Gate B label. The completeness audit (per `[[project_005_phase8_completeness_false_pass]]` + `[[feedback_simplify_pass_catches_9th_burn]]`) MUST audit test BODIES — every FR maps to a test that BINDS a runtime contract, not a SUCCEED-placeholder. |

**Gates — PASS, inherited from 005/009.** No new violations introduced; the existing `[const §VII.5]` Article XVII §1 recorded Gate-A-blocker waiver remains as-is. No new pluggable interface, no new C-ABI surface, no new dependency, no new design question. Gate A is NOT re-run (see `[const §XVII.1]` row + Summary). Complexity Tracking below is intentionally empty.

## Project Structure

### Documentation (this feature)

```text
specs/010-session-cfg-lifetime/
├── plan.md              # this file (/speckit-plan 2026-05-23)
├── spec.md              # /speckit-specify 2026-05-23 + /speckit-clarify 2026-05-23 (FR-001 = Option A)
├── research.md          # Phase 0 — D-1 (by-value SessionConfig copy) / D-2 (FSM visit-history seam) / D-3 (variant name + slot)
├── data-model.md        # Phase 1 — no new entities; lists edits to E1 Session, E2 SessionConfig, E3 error enum
├── quickstart.md        # Phase 1 — build / test / sanitizer / coverage / verify / gate-b for the slice
├── contracts/
│   └── session_error_state_for_send.hpp  # the ONLY new shape oracle: session_invalid_state_for_send variant at slot 77
├── checklists/
│   └── requirements.md  # /speckit-specify quality checklist (all pass, no clarifications remain after /clarify)
└── tasks.md             # Phase 2 (/speckit-tasks — NOT created here)
```

### Source code (library submodule root)

Edits only — no new directory created. Affected files:

```text
include/fixpp/session/
└── session.hpp            # MODIFY: `const SessionConfig& cfg_;` (~L281) → `SessionConfig cfg_;` (FR-001/FR-002);
                           # ADD: std::array<fsm_state,16> fsm_visit_history_; std::uint8_t fsm_visit_count_;
                           #      member + fsm_visit_history() const noexcept accessor (FR-004 D-2)

include/fixpp/core/
└── error.hpp              # ADD: session_invalid_state_for_send = 77 variant + doc comment (FR-005 D-3)

src/session/
└── session.cpp            # MODIFY: ctor — drop ref-binding pattern (~L116, initializer list); copy cfg into cfg_ (FR-001);
                           # MODIFY: every `fsm_state_ = X;` site (~10 sites; lines 239, 292, 376, 388, 630, 641, 648,
                           #         659, 690, 696, 702, 712, …) calls a new private record_state_transition_(state)
                           #         that pushes to the ring buffer (FR-004 D-2);
                           # MODIFY: two `co_return std::unexpected(error::session_invalid_logon);` sites in Session::send
                           #         (~L1151 and the symmetric site) → `session_invalid_state_for_send` (FR-005)

tests/session/
├── CMakeLists.txt                                    # MODIFY: remove the FIXPP_ENABLE_ASAN guard block on
│                                                     #         session_coverage_adversarial (FR-003)
├── cfg_lifetime_safety_test.cpp                      # NEW: nested-scope SessionConfig drop; session continues to
│                                                     #      operate; ASan clean (FR-001 / FR-002 / SC-001)
├── fsm_matrix_witness_test.cpp                       # NEW: one witness per cell of the 6-state × N-event FSM matrix
│                                                     #      `[FIX-SL §4.10]` using fsm_visit_history() (FR-006 / SC-002)
├── admin_builder_distinct_now_test.cpp               # NEW: clock->advance(...) between 2 emits of each admin builder;
│                                                     #      assert distinct SendingTime (FR-007 / SC-006)
├── admin_emit_mixed_path_test.cpp                    # NEW: 4 permutations at sites 1+2 (Reject-ok/Logout-ok,
│                                                     #      Reject-fail/Logout-skip, Reject-ok/Logout-fail,
│                                                     #      Reject-fail/Logout-fail) (FR-008 / SC-005)
├── initiator_transport_throw_test.cpp                # NEW: transport.send throws during initiator Logon emit on
│                                                     #      Session::open() (FR-009 / SC-007)
└── <existing>                                        # MODIFY: 1-2 files asserting session_invalid_logon at the two
                                                      #         Session::send non-Active sites → session_invalid_state_for_send
                                                      #         (FR-005 AC3) — grep at /speckit-tasks pins the file list
```

**Structure Decision**: edits to existing `session/` module + a small set of new test files. **No new source-tree directory introduced.** New test files added under `tests/session/` follow the existing 005 / 009 naming pattern. The only new public-API symbols are `Session::fsm_visit_history() const noexcept` (read-only observation accessor) and `error::session_invalid_state_for_send` (the new enum variant).

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

Intentionally empty — no constitutional violations introduced. The existing 005 `[const §VII.5]` Article XVII §1 recorded Gate-A-blocker waiver remains in force unchanged; this slice does not green any deferred TC case nor add a new deferred case.

## Citation verification pass

Run against `library/.specify/constitution.md` (v0.1, 2026-05-10) and the 005 / 008 Phase-2 anchors. Every `[const §X.Y]` cited above resolves to an extant article/section. The new `error::session_invalid_state_for_send` variant slot (77) is one past the highest occupied session-class slot (`session_invalid_config = 76`) per `include/fixpp/core/error.hpp:310`. The cross-doc citations (PR #82 W-5 location in `src/session/session.cpp:116` declaration `include/fixpp/session/session.hpp:281`) are verified against the live tree at `ba2222d`.

## Pipeline progress

| Step | Status | Date | Notes |
|---|---|---|---|
| `/speckit-specify` | DONE | 2026-05-23 | spec.md authored with 12 FRs + 9 SCs; checklist GREEN; 1 [NEEDS CLARIFICATION] on FR-001 (deliberate). |
| `/speckit-clarify` | DONE | 2026-05-23 | Q1 resolved: FR-001 W-5 mechanism = Option A (by-value `SessionConfig cfg_;`). Q1 only; Q2-Q5 unnecessary. |
| `/speckit-plan` | DONE | 2026-05-23 | this file |
| Gate A | **N/A — inherited from 005 + addendum** | — | See `[const §XVII.1]` row + Summary; addendum produced at Gate B precondition step. |
| `/speckit-tasks` | next | — | Generate dependency-ordered tasks closing FR-001..FR-009 + FR-010..FR-012 hygiene. |
| `/speckit-analyze` | after tasks | — | Mandatory per `[const §XVI.4]`; spec-analyzer subagent per `[[feedback_speckit_analysis_subagents]]`. |
| `/speckit-checklist` (domain) | after analyze | — | Per pipeline.md step 7. |
| `/speckit-checklist-audit` | after checklist | — | Mandatory per `[const §XVII.8]` + `[[feedback_follow_pipeline_md_not_plan_summary]]`; checklist-auditor subagent. |
| `/speckit-implement` | after audit | — | Sonnet subagents per phase per `[[feedback_speckit_subagent_phasing]]`; parent re-runs build/test gate between phases per `[[feedback_self_run_build_gate]]`. |
| `/simplify` | after implement | — | 3 general-purpose review agents per `[[feedback_speckit_simplify_before_verify]]` + `[[feedback_simplify_pass_catches_9th_burn]]`. |
| `/speckit-verify` | after simplify | — | Mandatory per `[const §XVII.8]`; emits `010-session-cfg-lifetime-verify.md`. **W-1..W-4 auto-revisit happens here** (FR-011). |
| Feature-completeness audit | with verify | — | Mandatory per `[const §XVII.8]` + `[[project_005_phase8_completeness_false_pass]]`; MUST audit test BODIES (test-quality discipline). |
| `/gate-b` | after verify+completeness | — | Mandatory per `[const §XVII.2]`; opens PR; closes W-5 row in 009 Gate B decision record (SC-009). |

## Next

`/speckit-tasks` (Phase 2). One Sonnet subagent per task-phase per `[[feedback_speckit_subagent_phasing]]`; parent re-runs the build/test gate before the next phase per `[[feedback_self_run_build_gate]]`. After /tasks → `/speckit-analyze` → `/speckit-checklist` → `/speckit-checklist-audit` → `/speckit-implement` → `/simplify` → `/speckit-verify` (+ completeness audit on test bodies) → `/gate-b`.
