<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# 016 interop-harness completeness audit

Source of truth: `spec.md` FR-001..FR-028 / SC-001..SC-008 and `tasks.md`
done-notes. This is a disposition ledger only; it does not claim the live
QuickFIX counterparty matrix has run.

## Functional requirements

| Req | Task(s) | Status | Evidence |
|---|---|---|---|
| FR-001 | T003, T010-T014, T028 | done | `tests/interop/happy/MATRIX.md` enumerates QuickFIX-cpp and QuickFIX-J live cells; `cell_results.yaml` records local `skip:counterparty-unavailable` until the parent supplies binaries. |
| FR-002 | T010-T014, T028 | done | The happy drivers are parameterized over fixpp initiator/acceptor roles; reconnect is initiator-only as recorded in `MATRIX.md`. |
| FR-003 | T017, T028 | done | FIX 4.4 is the live version; `MATRIX.md` and `cell_results.yaml` carry `deferred:fixt-routing` for FIXT.1.1 / FIX 5.0 SP2. |
| FR-004 | T007, T008, T010-T014, T016 | done | Session-admin chains are represented by happy drivers; reconnect uses the finite policy from T008 and the down-peer watchdog in `hp_down_peer_stop_watchdog_test.cpp`. |
| FR-005 | T017, T030, T036 | done | Business-message cells are present as `deferred:app-messages`; `KNOWN-LIMITATIONS.md` and the catalogue note record the open Section VII.6 residual. |
| FR-006 | T005, T009, T031 | deferred:parent-harness | `golden_diff` support and `golden/FORMAT.md` are in-repo; byte captures and real goldens require the parent passthrough proxy and first paired run. |
| FR-007 | T004, T010-T014, T031 | deferred:parent-harness | In-repo drivers assert fixpp state/seqnum deltas; counterparty terminal wire behavior is a parent capture obligation recorded in `MATRIX.md`. |
| FR-008 | T006, T017, T028 | done | `MATRIX.md` covers every axis or records a `deferred:*` row; `cell_results_schema_check_test.py` validates per-cell completeness. |
| FR-009 | T017, T018, T030 | done | Fix8 is recorded as corpus-only at v1.0 with `deferred:fix8-revisit` happy rows and no live pairing claim. |
| FR-010 | T018, T019 | done | `tests/interop/thorny/CORPUS-INDEX.md` records the bounded capped worklist and follow-on open-issue sweep. |
| FR-011 | T018-T021 | done | `CORPUS-INDEX.md` records engine/issue provenance for C-001..C-007 and tracked C-101..C-103. |
| FR-012 | T019-T022 | done | Corpus rows are bucketed P1/P2/P3; the open `watch:` bucket is explicitly follow-on. |
| FR-013 | T018, T022, T030 | done | `CORPUS-INDEX.md` declares the corpus append-only across later release sweeps. |
| FR-014 | T022, T030 | done | The corpus disposition rule and known-limitations table require pass or `known-limitation:<open tracking>`. |
| FR-015 | T020, T021 | done | C-004 and C-007 record spec-correct divergences/differentiators in `CORPUS-INDEX.md`. |
| FR-016 | T023-T026, T028 | done | GAP closures are represented by three parity witnesses plus parent parity-matrix flips; `cell_results.yaml` records the parity pass rows. |
| FR-017 | T026 | done | Parent `phases/phase-9/unit-test-parity-matrix.md` was updated by T026; N/A/deferred rows remain auditable there. |
| FR-018 | T006, T010-T014, T020-T025, T028 | done | Scenario descriptors, `MATRIX.md`, corpus rows, parity rows, and `cell_results.yaml` carry FIX spec references. |
| FR-019 | T029 | deferred:/speckit-verify | In-repo target/linkage guard is recorded; full normal/TSan/ASan-UBSan interop matrix run is deferred to `/speckit-verify` and the parent release-prep tier. |
| FR-020 | T029, T030 | deferred:/speckit-verify | Sanitizer-only failures are documented as gate failures; actual sanitizer matrix execution is deferred to `/speckit-verify`. |
| FR-021 | T029, T030 | done | `KNOWN-LIMITATIONS.md` records that the submodule carries no QuickFIX/Fix8 source and only fixpp is sanitizer-instrumented. |
| FR-022 | T027, T028 | done | `.github/workflows/interop-smoke.yml` runs the smoke cell on normal builds for transport/session/interop changes; `cell_results.yaml` feeds the release-prep parent check. |
| FR-023 | T003, T027, T028 | done | `counterparty_probe.hpp` and `cell_results.yaml` use explicit `skip:counterparty-unavailable`, never silent pass. |
| FR-024 | T030, T031 | deferred:GA-release | Badge source inputs are in-repo (`KNOWN-LIMITATIONS.md`, corpus index, cell manifest, golden format); publication and archived links are GA/parent obligations. |
| FR-025 | T010-T015, T017, T030 | done | `MATRIX.md` records all-TLS `one_way_ca` baseline and `deferred:v1.1-mtls` mutual-certificate cells. |
| FR-026 | T001-T006, T010-T031 | done | Committed deliverables live under `tests/interop/**`; parent fork-exec orchestration and counterparty clones remain out-of-repo. |
| FR-027 | T017, T030, T036 | done | Section VII.6 business flow remains open and forward-points to A-001/A-006; no 016 artifact records it as run. |
| FR-028 | T007, T008, T016, T028 | done | `HP-down-peer-stop-watchdog` is a separate non-matrix regression cell with finite reconnect policy and bounded `Engine::stop()`. |

## Success criteria

| SC | Task(s) | Status | Evidence |
|---|---|---|---|
| SC-001 | T010-T014, T017, T028 | deferred:parent-harness | Full live QuickFIX-cpp/QuickFIX-J FIX 4.4 TLS matrix is enumerated, but local cells skip until the parent supplies counterparties and captures. |
| SC-002 | T018-T022, T028, T030 | done | P1 corpus rows C-001..C-007 pass or are covered-by-parity; no silent P1 failure is recorded. |
| SC-003 | T023-T026, T028 | done | US3 parity pass rows are in `cell_results.yaml`; parent parity-row flips were completed in T026. |
| SC-004 | T029 | deferred:/speckit-verify | Sanitizer full-matrix execution is explicitly deferred to `/speckit-verify`; only the in-repo counterparty-source/linkage guard is discharged here. |
| SC-005 | T010, T027 | done | The smoke workflow runs `HP-QFcpp-init-fix44-logon-hb-logout` with a 120 s step budget on the normal build. |
| SC-006 | T006, T010-T014, T020-T025, T028 | done | Every executed or declared scenario row carries a FIX spec reference; corpus language rejects "because engine X" assertions. |
| SC-007 | T030, T031 | deferred:GA-release | Badge source text, exact counterparty versions, limitations, corpus index, and cell manifest are present; release publication is parent/GA work. |
| SC-008 | T017, T030, T036 | done | `KNOWN-LIMITATIONS.md`, `MATRIX.md`, and catalogue notes keep the Section VII.6 business-message flow open with A-001/A-006 forward pointers. |

## Tally

- FRs: 23 done / 5 deferred / 0 waived.
- SCs: 5 done / 3 deferred / 0 waived.
- Parent-side SCs: SC-001 full live matrix green is `deferred:parent-harness`; SC-004 sanitizer matrix is `deferred:/speckit-verify`.

100% of FRs and SCs are dispositioned (done or explicitly deferred/waived) — /gate-b completeness precondition satisfied.
