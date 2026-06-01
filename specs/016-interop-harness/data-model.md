# Phase 1 Data Model: Interop Harness

**Feature**: `016-interop-harness` | **Date**: 2026-06-01

The "entities" here are **test/corpus artifacts**, not runtime types — this feature ships no production data model. They define the shape of the checked-in fixtures and the in-memory objects the GoogleTest fixture manipulates. Field names are descriptive, not a frozen API.

---

## E1 — Scenario (abstract base)

A single named, reproducible interop test.

| Field | Meaning |
|-------|---------|
| `id` | stable unique name (e.g. `HP-QFcpp-init-fix44-logon-hb-logout`, `thorny-qfj-issue-646`) |
| `kind` | `happy` \| `thorny` \| `parity` |
| `preconditions` | counterparty config + port + fixpp `EngineConfig`/`SessionConfig` + role; for live cells: **counterparty started first** (R5) |
| `driven_sequence` | the session-event / message sequence to drive (or the wire bytes to inject for a corpus replay) |
| `pass_criteria` | fixpp FSM end-state + seqnum deltas + the **wire-observed counterparty terminal behavior** (received Logout / orderly socket close), NOT an internal counterparty-FSM probe (FR-007) + (live) golden diff (FR-006) |
| `deadline` | internal self-deadline so a hung live I/O probe fails instead of wedging `ioc.run()` (`[[feedback_fail_placeholder_red_test]]`) |
| `reconnect_policy` | for live reconnect cells: a **finite max-attempts** policy (no busy-spin) + a **`stop()` watchdog** bound asserting `Engine::stop()` returns; the deliberate down-peer/never-listening case is a *separate* cell (FR-004/FR-028, R5) |
| `skip_reason` | set when a required counterparty binary is unavailable (FR-023) — skip, never silent-pass |

State (per run): `pending → running → {passed, failed, skipped:<reason>, known-limitation:<issue>}`.

## E2 — MatrixCell : Scenario (US1)

A happy-path cell keyed by the matrix axes (R8).

| Field | Meaning |
|-------|---------|
| `counterparty` | `quickfix-cpp` \| `quickfix-j` \| `fix8`(placeholder) |
| `role` | `fixpp-initiator` \| `fixpp-acceptor` |
| `fix_version` | `FIX.4.4` (LIVE at v1.0) \| `FIXT.1.1/FIX.5.0SP2` (placeholder, `deferred:fixt-routing` — not executed at v1.0, FR-003) |
| `security` | `plain-tcp` \| `tls-logon` (FR-025); `mtls-mutual` reserved for v1.1 |
| `event_chain` | e.g. `logon-hb-logout`, `testrequest-echo`, `reject-invalid-admin`, `seqnum-recovery`, `disconnect-reconnect-noreset` |
| `business` | `none` for v1.0 cells; A-row cells exist tagged `deferred:app-messages` (FR-005) and are not executed |
| `golden_ref` | path to `tests/interop/happy/golden/HP-<id>.fix` |

Disposition tags: `live` \| `deferred:app-messages` \| `deferred:fixt-routing` (FIX 5.0 SP2/FIXT.1.1 cells, FR-003) \| `deferred:fix8-revisit` \| `deferred:v1.1-mtls`. Every axis covered ≥ once or carries a deferred-to-vN.x note (FR-008).

## E3 — CorpusScenario : Scenario (US2)

A thorny-corpus item derived from an upstream bug.

| Field | Meaning |
|-------|---------|
| `provenance` | `{engine, issue_or_pr_ref, url, state: closed|open}` (FR-011) |
| `category` | one of the 12 phase-9.md categories (SequenceReset/GapFill, ResendRequest edges, repeating-group, Logon/Logout race, Heartbeat, BodyLength/checksum, field-validation, encoding, reject, persistence/recovery, TLS, high-volume) |
| `priority` | closed: `P1`\|`P2`\|`P3`; open: `watch:P1`\|`watch:P2`\|`watch:info` (FR-012) |
| `differentiator` | true when fixpp is spec-correct while upstream is buggy → release-notes positive (FR-015) |
| `disposition` | `pass` \| `known-limitation:<tracking-issue>` (FR-014) |

Rules: **append-only** across releases (FR-013); a failing `P1`/`watch:P1` blocks the tag unless `known-limitation` with an open issue (FR-014). The executable scenario lives in-repo; the sweep analysis stays in the parent (R2). The v1.0 sweep is the **initial population** of the corpus (a bounded, capped worklist — FR-010, not an unbounded all-open-issues triage); FR-013's append-only rule governs **subsequent** releases (they add, never remove) — the first bulk construction is not forbidden by it.

## E4 — ParityRow (US3)

One reference-engine unit-test behavior and its fixpp disposition.

| Field | Meaning |
|-------|---------|
| `source` | reference test (e.g. `SessionTest::SequenceResetGapFill`, `quickfix-j#646`) |
| `behavior` | the protocol behavior asserted |
| `disposition` | `COVERED` (cite fixpp test) \| `GAP` (→ closure task / deferral) \| `N/A` (architecture-specific rationale) |
| `citation` | for COVERED: the `tests/**` file; for GAP: the closure test or tracking entry |

Closure rule (FR-016/FR-017): every `GAP` → COVERED-with-citation, implemented, or explicitly deferred with a tracking entry; `N/A` rows unchanged with rationale. The live worklist (R-parity): QFJ-646 resend-abort witness, the Bucket-4 replay-subsumes-reorder-queue confirmation, the disposition flips.

## E5 — GoldenTranscript

The expected byte-level wire dialogue for a `MatrixCell` (FR-006).

| Field | Meaning |
|-------|---------|
| `raw` | byte-exact captured wire (no canonicalization) — what the proxy recorded |
| `normalization_rules` | fields excluded from the diff: timestamps (`52`,`60`,`122`...), sequence numbers (`34`), and other non-deterministic fields |
| `diff_result` | `match` \| `mismatch:<field-path>` after normalization |

Discipline mirrors `tests/abi/golden/`: one file per scenario, diffed at test time; a wire-format change is a deliberate golden update with rationale.

## E6 — Counterparty

A reference FIX engine and its v1.0 disposition.

| Field | Meaning |
|-------|---------|
| `name` / `version` | `quickfix-cpp@v1.16.0`, `quickfix-j@3.0.1`, `fix8@1.4.3` |
| `disposition` | `live+corpus` (QFC, QFJ) \| `corpus-only` (Fix8, FR-009) |
| `spawn_recipe` | parent-harness build/spawn commands (out-of-repo) |
| `availability` | probed at test start → drives `skip_reason` (FR-023) |

## E7 — InteropBadge (US5)

The release artifact (FR-024).

| Field | Meaning |
|-------|---------|
| `counterparties` | exact `{name, version, commit}` paired against |
| `transcript_links` | archived per-scenario goldens/captures |
| `known_limitations` | each documented limitation + its tracking issue |

---

## Relationships

```
Counterparty ──< MatrixCell >── GoldenTranscript
CorpusScenario ──> Counterparty (provenance)
ParityRow ──> (cites) fixpp test files
InteropBadge ──< summarizes >── {MatrixCell results, CorpusScenario results}
```

All are checked-in fixtures or run-time aggregates; **none cross the C ABI or add a production type** (R3).
