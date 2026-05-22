# Quickstart — 009-session-fsm-finalize

**Feature:** 009-session-fsm-finalize (drift closure against 005 design)
**Status:** /speckit-plan complete; ready for /speckit-tasks.

This quickstart inherits the toolchain + preset matrix from [`005-session-establishment-fsm/quickstart.md`](../005-session-establishment-fsm/quickstart.md). Only steps that differ in the 009 cadence are spelled out below; for unchanged build/test mechanics, defer to 005's quickstart.

## 1. Build (unchanged from 005)

```bash
cd research/G19-fix-fpml-iso20022/library
conan install . --build=missing --output-folder=build/
cmake --preset linux-clang-debug
cmake --build build/linux-clang-debug
```

## 2. Run the new 009 runtime-behavior tests (NEW for this slice)

These are the FR-012 / SC-001 tests that close the 005 false-PASS pattern. Each must be a runtime-behavior test (NOT a `static_assert` / file-existence stub) — see `[[project_005_phase8_completeness_false_pass]]`.

```bash
# FR-001 send pipeline (assign + stamp + store + emit, FIX.4.2 + FIX.4.4)
ctest --test-dir build/linux-clang-debug -V -R '^session_send_path_test'

# FR-004/005 acceptor role (NotConnected -> LogonReceived -> Active from production-shaped open())
ctest --test-dir build/linux-clang-debug -V -R 'session_logon_handshake_test.Acceptor'
ctest --test-dir build/linux-clang-debug -V -R 'session_tc_establishment_test.Scenario1[ab]_'

# FR-006 refused-Logon matrix row
ctest --test-dir build/linux-clang-debug -V -R 'fsm_transition_matrix_test.NotConnected_RefusedLogon'

# FR-007/008/009 missing/malformed SendingTime
ctest --test-dir build/linux-clang-debug -V -R 'sending_time_test.(MissingSendingTime|MalformedSendingTime).+'

# FR-010 per-session TestReqID cross-session race (TSan)
ctest --test-dir build/linux-clang-tsan -V -R '^test_test_request_id_cross_session_race$'

# FR-011 drain on close (no terminate)
ctest --test-dir build/linux-clang-debug -V -R '^test_seqnum_drain_on_close$'

# FR-013 outbound tag-8 / tag-52 assertions in conformance suite
ctest --test-dir build/linux-clang-debug -V -R 'session_tc_(logout|reject|liveness|sendingtime|establishment)_test'
```

## 3. Tier-1 sanitizer matrix (unchanged from 005 — re-run at /speckit-verify)

```bash
for preset in linux-clang-debug linux-clang-release linux-clang-asan linux-clang-ubsan linux-clang-tsan linux-clang-coverage linux-gcc-release; do
    cmake --preset "$preset" && cmake --build "build/$preset" && \
        ctest --test-dir "build/$preset" -V -R 'session_|tc_'
done
```

**TSan focus for 009:** the new `test_test_request_id_cross_session_race` + `test_seqnum_drain_on_close` MUST run clean under TSan. A pass in `linux-clang-debug` alone is NOT evidence — TSan must fire (or, more precisely, NOT fire) on these specific tests.

## 4. Bench gate (unchanged from 005)

```bash
cmake --build build/linux-clang-release --target fsm_bench seqnum_bench fix_time_bench heartbeat_bench
./build/linux-clang-release/bench/session/fsm_bench --benchmark_out=current.json
python3 tools/compare_baselines.py bench/baselines/session/fsm_baseline.json current.json
```

The slice should fit within the existing ±5% gate per `[const §VIII.2]`. If FR-001's `Session::send` wiring genuinely shifts a baseline, /speckit-verify re-captures (see Technical Context in plan.md — wiring stays within the existing "outbound admin emit ≤ 400ns" envelope).

## 5. Coverage gate (NEW expectation for 009)

```bash
cmake --preset linux-clang-coverage && cmake --build build/linux-clang-coverage
ctest --test-dir build/linux-clang-coverage -V
llvm-cov export --format=lcov --instr-profile=build/linux-clang-coverage/default.profdata \
    build/linux-clang-coverage/tests/session/* > coverage.lcov
genhtml coverage.lcov -o coverage/
```

**Expectation:** coverage on `src/session/session.cpp` + `src/session/admin_messages.cpp` rises into the 005 W-1..W-4 waiver envelope OR BETTER. The new FR-007/008/009 missing/malformed branches were previously unreachable (the implementation was lenient → no test could hit them); the new tests close those defensive arms. If residual gaps remain, they carry forward as 005 W-1..W-4-style Article IX §1 waivers, NOT new waivers.

## 6. /speckit-verify

```bash
# Will emit library/.specify/decisions/009-session-fsm-finalize-verify.md
/speckit-verify 009-session-fsm-finalize
```

Per `[const §XVII.8]` non-RED required for the Gate B label.

## 7. Feature-completeness audit (T067-equivalent for 009)

**This is the most-changed step from 005's quickstart** — per `[[project_005_phase8_completeness_false_pass]]` the audit MUST verify test BODIES match contract assertions, not just file-naming / FR-listing / task-marker existence. Concretely, for each of FR-001..FR-013:

1. Locate the named test file(s).
2. Grep the test body for the contract assertion content (e.g., FR-001 requires assertions on `34` increment AND `52` matching mock-clock AND `8` matching configured BeginString AND store-before-transport ordering — all four must appear in the test body).
3. A `static_assert` / `noexcept` attestation / `SUCCEED()` placeholder is INSUFFICIENT for any FR whose primary deliverable is a runtime behavior. Such mappings fail the audit and the FR must be re-implemented.
4. The audit verdict is PASS only when ALL 13 FRs have test BODIES matching the contract; partial PASS is not a category.

The audit emits a section in `009-session-fsm-finalize-verify.md` (`## Feature completeness audit (T067-equivalent)`) OR a sibling `009-session-fsm-finalize-completeness.md`. Either location satisfies the `/gate-b` pre-flight per `[const §XVII.8]`.

## 8. /gate-b

```bash
# Either refresh PR #81 (if merging back into 005) or open a new PR.
/gate-b <PR-number>
```

Per `[[project_005_phase8_completeness_false_pass]]` precedent, if round 1 Codex finds ≥5 P1 with completeness-PASS contradiction again, **pause and respec** rather than burning fixer rounds. The audit-bodies discipline above is supposed to prevent recurrence.

## 9. Merge bookkeeping

At Gate B convergence + user sign-off:

- If merging into 005's branch (refreshing PR #81): push 009 to 005 (`git push origin 009-session-fsm-finalize:005-session-establishment-fsm` via fast-forward) OR open a PR `009 → 005` first if a Codex review of the merge itself is desired. PR #81 is then re-marked ready-for-review.
- If merging directly to main: open a PR `009 → main`; close PR #81 with a comment linking to the 009 PR.

Either way, at the merge bookkeeping commit on `main`:

- Flip 005-owned catalogue rows (`S-001/2/3/4/7/8/9/15/16/19/20` + folded `core/` row #4) from `implementing → done` per `[[feedback_pipeline_mark_done_step]]`.
- Apply both `gate-a-done` + `gate-b-done` (or `*-waived` per outcome) labels on the merged PR via `bash .claude/scripts/gh-pr-meta.sh label-add ...` per `[[feedback_gate_precheck_heading_match_contract]]`.
- Bump the submodule pointer in the parent repo (separate commit per established practice).
- Update project memory: append a `project_009_session_fsm_finalize_closed.md` entry mirrored from the 008 close-out pattern per `[[project_008_message_store_closed]]`.

## 10. Module-exit bookkeeping (deferred to merge)

The 005 T068 module-exit checklist (`spec/feature-catalogue.md` row flips, `core/` time-helper #4 row entry, `spec/coverage-index.md` ledger confirmation, submodule bump, gate labels, phase-4 Track Log) is REFRESHED — not RE-RUN — against the 009 merge tip. The 005 T068 checklist items remain authoritative for the merge bookkeeping commit.

---

**Pipeline status snapshot:**

| Step | Status |
|---|---|
| /speckit-specify | DONE (2026-05-22) |
| /speckit-clarify | N/A (no new design questions) |
| /speckit-plan | DONE (2026-05-22) |
| Gate A | N/A (inherited from 005 converged 2026-05-18) |
| /speckit-tasks | NEXT |
| /speckit-analyze | after tasks |
| /speckit-checklist (domain) | after analyze |
| /speckit-checklist-audit | after checklist (MANDATORY) |
| /speckit-implement | after audit |
| /simplify | after implement |
| /speckit-verify | after simplify |
| Feature-completeness audit (test-bodies) | with verify |
| /gate-b | after verify+completeness |
