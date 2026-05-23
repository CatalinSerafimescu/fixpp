# 010-session-cfg-lifetime — /simplify fix plan

**Status:** active — execution in progress (reverse-order: P3/P2 → P1 → final cosmetic sweep)
**Authorship:** Opus orchestrator triage of /simplify round (3 agents: A simplification, B correctness, C test-body discipline) 2026-05-23
**User directive (2026-05-23):** "the aim is to fix as much as possible in the correct way, not to waive so we can close the phase … The only acceptable reason to waive something is if the benefits are very low and irrelevant compared with effort … start with P3 and P2, then come back to P1s"

## Burn-class context

010 was authored to close the 12th instance of the **completeness-PASS-as-hypothesis** burn class — `[[project_005_phase8_completeness_false_pass]]` + `[[feedback_simplify_pass_catches_9th_burn]]`. /simplify on 010 found the **13th instance** recurring inside this very slice: matrix witness covers 46/57 cells but SC-002 says 100%; 6+ tests assert FSM end-state only without observing the Reject/Logout emissions that constitute the binding contract. Fix-queue priority therefore favors the burn-class P1s (F2/F3/F4) — these are not polish; they are the same defect this slice exists to close.

## Fix queue (execution order)

### Wave 1 — P2 quick wins (no design dependencies)

| # | Tag | Agents | Action | Files |
|---|-----|--------|--------|-------|
| W1.1 | F9 / C-5 | C-only | Rename matrix test prefix `LS_` → `LO_` for LogoutSent tests (collision with LogonSent). | `tests/session/fsm_matrix_witness_test.cpp` |
| W1.2 | F10 / C-6 | C-only | Verify record says "6 permutations"; spec SC-005 says "4 mixed-success-mode permutations". Reword to match. | `.specify/decisions/010-session-cfg-lifetime-verify.md` |
| W1.3 | F8 / B-7+C-11 | B+C | Ring-buffer comment says "oldest first" but wrap-around is modular not chronological. Either fix the comment (simplest) OR rotate at read. **Choice: fix the comment** — chronological at-read costs `O(N)` per accessor call for negligible UX win; saturation behavior is documented as design-bounded per D-2. | `include/fixpp/session/session.hpp` (the `fsm_visit_history()` doc comment block) |

### Wave 2 — P2 substantive

| # | Tag | Agents | Action | Files |
|---|-----|--------|--------|-------|
| W2.1 | F7 / B-6 | B-only | T022 UBSan-fix commit (`SessionConfig cfg;` → `cfg{};`) rationale may be wrong. Default-init vs value-init shouldn't matter for a class-type with member shared_ptr (the shared_ptr default ctor zero-inits). Investigate the real root cause — possibly an alignment issue in `std::function transport_send` capture OR a build/cmake artifact. Either way the fix is benign; the goal is accurate diagnosis recorded in the verify record. | `tests/session/test_file_store_flush_for_session_close.cpp` + investigation notes appended to verify record |
| W2.2 | F6 / C-2 | C-only | FR-007 promises distinct-now witness for `Heartbeat/TestRequest/Logout/Reject` (4 types). Ship has `Heartbeat/Reject/Logout/Logon` — TestRequest substituted by Logon (which isn't an admin-builder per FR-007). Add a real TestRequest-distinct-now test exercising the liveness loop. ~40 LoC. | `tests/session/admin_builder_distinct_now_test.cpp` |

### Wave 3 — P1 burn-class (REAL DEFECTS)

| # | Tag | Agents | Action | Files |
|---|-----|--------|--------|-------|
| W3.1 | F1 / B-1 | B-verified | **UB-class.** Session ctor `noexcept` + throwing SessionConfig copy = `std::terminate`. **VERIFIED:** `static_assert(std::is_nothrow_copy_constructible_v<SessionConfig>)` FAILED. Fix: drop `noexcept` from `Session::Session` (matches `close()` precedent at session.hpp:120 — explicitly NOT noexcept due to alloc). 1-line production change. Add a static_assert documenting the WHY in the header. | `include/fixpp/session/session.hpp`, `src/session/session.cpp` |
| W3.2 | F3 / B-2+C-3 | B+C cross-confirmed | Matrix witness covers **46 out of 57 claimed cells**. SC-002 says 100% coverage. Two paths: (a) add 11 missing per-cell tests, (b) reclassify the gap as design-forbidden/unobservable with a row-by-row rationale comment table + spec amend. **Path decision:** prefer (a) — adding the missing rows IS the burn-closure work; reclassification is what the false-PASS pattern looks like. Tag any genuinely-unobservable cells with explicit `// design-forbidden because …` comment. | `tests/session/fsm_matrix_witness_test.cpp` + `specs/010-session-cfg-lifetime/spec.md` Edge Cases + `checklists/coverage.md` CHK005 + verify record completeness section |
| W3.3 | F2 / B-3+B-4+B-5+C-1 | 4-way cross-confirmed | 6 admin_emit_mixed_path tests + multiple fsm_matrix_witness tests assert FSM end-state ONLY; don't observe Reject/Logout emission. 13th-burn pattern. Fix: wire `cfg.transport_send` capture in fixture; per permutation assert which MsgType frames emitted in which order (and `==N` not `>=N`); for Active×DupLogon (B-4) and Active×OutOfScopeAdmin (B-3) either tighten assertions OR amend spec to "silently ignored" + drop Reject obligation. | `tests/session/admin_emit_mixed_path_test.cpp` + `tests/session/fsm_matrix_witness_test.cpp` fixture; possibly `specs/010-session-cfg-lifetime/spec.md` FR-008 if Reject obligation is reclassified |
| W3.4 | F5 / B-8 | B-only | FR-009 spec says "symmetric to acceptor witness"; initiator-throw test asserts FSM=**LogonSent**; acceptor send-throw asserts FSM=**Disconnected**. Per `[FIX-SL §4.3]` initiator handshake failure → Disconnected. **Design call needed:** check 005 data-model + research the engineering source-of-truth on what initiator should do when its first Logon emit throws. If symmetric: add `record_state_transition_(fsm_state::Disconnected)` in `Session::open()` emit-error branches (session.cpp:292-307) + flip the test. If asymmetric (e.g., "no transition since we never sent → caller retries"): amend FR-009 to drop "symmetric" framing + record rationale. | Either (impl): `src/session/session.cpp:292-307` + `tests/session/initiator_transport_throw_test.cpp`, or (spec amend): `specs/010-session-cfg-lifetime/spec.md` FR-009 + `data-model.md` E1 |

### Wave 4 — re-verify

Re-run `/speckit-verify` to update the verify decision record post-fixes. Expected impact: SC-002 100% coverage now real; FR-008 mixed-path tests now exercise emission contract; FR-009 either matches impl or matches amended spec; FR-007 includes real TestRequest test.

### Wave 5 — final cosmetic sweep (C-7..C-10)

Stale line numbers in spec/plan/research/tasks/contracts/verify after Wave 1-3 shifted lines further. One sed pass — must be LAST.

- `session.cpp:1151 → 1170` (Session::send error_code site)
- `session.hpp:281 → 290` (cfg_ member declaration)
- `session.hpp:353 → 363` (fsm_visit_history accessor)
- `session_config.hpp:168 → 171` (static_assert location)

Confirm exact post-fix line numbers before committing the sweep.

## Items DEFERRED (not waived — moved to follow-up)

| # | Tag | Reason | Defer to |
|---|-----|--------|----------|
| D1 | A-1 (test fixture promotion ~500-700 LoC saving) | Genuine refactor, not a defect. Per `[const §XVII.1]` would warrant its own Gate A pass. Out of scope for this waiver-closure slice. | Backlog row "test-scaffolding-consolidation" — open as a fresh feature slice when next session/ work touches these fixtures. |
| D2 | A-2 (inline `record_state_transition_` in header) | Micro-perf hypothesis without measurement; not a defect; would force more files to include session.hpp transitively. | Skip unless future profiling motivates it. |
| D3 | Agent A's A-3..A-6 | Minor style/LoC wins; not defects; cumulatively <50 LoC. | Skip. |

## Codex angle (per user directive — available if needed)

If W3.4 (B-8 FR-009 design call) becomes contested between 005-source-of-truth and impl pragmatism, queue a `codex:codex-rescue` triage with the question framed as: *"Initiator first-Logon-emit throws via transport_send. Should the session transition to Disconnected (matching acceptor symmetry) or stay LogonSent (caller-retryable / handshake-uninitiated)? Cite [FIX-SL §4.3] + 005 data-model precedent."* — keep the angle decision-only, not implementation.

## Provenance — full /simplify agent findings (verbatim severities)

### Agent A (simplification, 6 findings)

- A-1 (HIGH): Test fixture duplication ~500-700 LoC across cfg_lifetime_safety_test / logon_received_observability_test / session_send_invalid_state_test / fsm_matrix_witness_test / admin_emit_mixed_path_test
- A-2 (MED): `record_state_transition_` could be inlined in header for hot-path perf
- A-3 (MED): visit_history `std::array<fsm_state,16>` could share alignment storage with adjacent members
- A-4 (LOW): contracts/session_error_state_for_send.hpp shape-oracle has redundant include
- A-5 (LOW): SessionConfig copy contract could static_assert at member-defn site (now W3.1 covers this)
- A-6 (LOW): plan.md "Scale/Scope" section has trailing whitespace

### Agent B (correctness/bugs/tests-miss, 9 findings)

- **B-1 (P1, VERIFIED):** Session ctor noexcept + throwing SessionConfig copy → std::terminate (W3.1)
- **B-2 (P1):** Matrix witness 46/57 cells (W3.2; cross-confirmed by C-3)
- **B-3 (P1):** Active×InboundOutOfScopeAdmin asserts state==Active without Reject-emission check (W3.3)
- **B-4 (P1):** Active×InboundDupLogon test explicitly relaxes to "with or without Reject" (W3.3)
- **B-5 (P1):** admin_emit_mixed_path 6 tests assert end-state only, no emission observation (W3.3; cross-confirmed by C-1)
- B-6 (P2): UBSan-fix diagnosis re-investigation (W2.1)
- B-7 (P2): Ring-buffer "oldest first" claim incorrect post-wrap (W1.3)
- **B-8 (P1):** FR-009 asymmetric initiator vs acceptor throw-witness (W3.4)
- B-9 (P3): Stale comment in session_config.hpp line 9 "unique_ptr" (post-FR-001a still says unique_ptr in historical comment block — that's intentionally historical per FR-001a addendum; **dismissed**)

### Agent C (test-body/doc discipline, 11 findings)

- **C-1 (P1):** admin_emit_mixed_path no transport_send capture in fixture (W3.3)
- **C-2 (P2):** TestRequest substitution in FR-007 distinct-now (W2.2)
- **C-3 (P1):** Matrix witness 46/57 cells (W3.2 cross-confirms B-2)
- C-4 (P2): cfg_lifetime_safety_test uses raw ASan trap; could use death-test framing
- **C-5 (P3):** LS_/LO_ prefix collision in matrix test names (W1.1)
- **C-6 (P2):** Verify-record permutation miscount (W1.2)
- C-7 (P3): spec.md line ref `session.cpp:1151` stale → 1170 (W5)
- C-8 (P3): plan.md ref `session.hpp:281` stale → 290 (W5)
- C-9 (P3): research.md ref `session.hpp:353` stale → 363 (W5)
- C-10 (P3): contracts/* ref `session_config.hpp:168` stale → 171 (W5)
- **C-11 (P2):** Ring buffer chronological semantics (W1.3 cross-confirms B-7)

## Open items (post-Wave-3 / pre-Wave-4)

- [x] W3.4 design call resolved (impl fix — Disconnected on emit-failure)
- [x] W3.3 / FR-008 — assertions tightened with transport_send capture (no spec amend needed)
- [x] W3.2 — spec accurately enumerates LR row as synchronous-transient (subsumed by FR-004), no Edge Cases amend
- [x] **F4 / W3.3-final (post-codex-Position-A + QuickFIX survey 2026-05-23)** — Active×DupLogon + Active×OOSA(RR/SeqReset) cells now emit Reject per 005 FR-017; `is_session_admin` at `src/session/session.cpp` excludes `"A"`, `"2"`, `"4"`. Pre-existing 005 spec-vs-impl gap (previously tracked in verify-record line 134-135 as out-of-010-scope) is CLOSED IN-SLICE. Codex review: `specs/010-session-cfg-lifetime/codex_f4_review.md`.
- [ ] Wave 4 re-verify reads: SC-002 100% real; SC-005 4 permutations matched; FR-007 4 admin types; FR-008 emission-contract evidence; FR-009 Disconnected end-state evidence; F4 Reject(35=3) emission evidence for DupLogon + OOSA cells

## Forward upgrade obligations (handed off to future slices)

- **2e-recovery / session-recovery feature** (catalogue row 400 in `library/spec/coverage-index.md` — "ResendRequest / SequenceReset-GapFill / SequenceReset-Reset / synchronize-seqnums ... Recovery-dependent ... discharged by later session-recovery feature"): when implemented, the `Active×OOSA(RR)` cell and the `Active×SeqReset` cell must UPGRADE from `Reject` → `Process` (gap-fill via the message store retrieve API). The dup-Logon-in-Active cell stays as `Reject` per 005's intentional defensive divergence from QuickFIX refresh-on-dup-Logon convention. Reference engines for the Process upgrade: QuickFIX-cpp `Session::nextResendRequest` (`Session.cpp:364`) and `Session::nextSequenceReset` (`Session.cpp:339`); QuickFIX/J `Session.nextResendRequest` (`Session.java:1325`) and `Session.nextSequenceReset` (`Session.java:1539`). Traceability surfaces (all carry TODO(2e-recovery) markers):
  - `src/session/session.cpp` — comment block at the Active-state `is_session_admin` declaration
  - `tests/session/fsm_matrix_witness_test.cpp` — `Active_InboundOutOfScopeAdmin_SessionReject_StaysActive` test body
  - `library/spec/coverage-index.md` — 010 /simplify close-out paragraph + row 400 of the deferral table
  - `.specify/decisions/010-session-cfg-lifetime-verify.md` — SC-002 row
