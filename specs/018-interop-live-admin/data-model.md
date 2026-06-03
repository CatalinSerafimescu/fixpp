# Phase 1 Data Model: Live Session-Admin Interop Round-Trips (gap-fill G1)

**Feature**: `018-interop-live-admin` | **Date**: 2026-06-03
**Input**: [`spec.md`](./spec.md) Key Entities + [`research.md`](./research.md) R1/R5/R6/R7.

These are **test-harness** entities (no production types). They extend the 016 data-model (`AdminScenarioCell` ⊃ 016 `Scenario`/`Cell`; `GoldenAdminTranscript` ⊃ 016 `Golden`; `CellResultRow` reused verbatim).

---

## E1 — `AdminScenarioCell`

One named, value-parameterized live cell asserting a specific admin round-trip for one `(Counterparty × Role)` pair.

| Field | Type | Notes |
|---|---|---|
| `cell_id` | string | e.g. `HP-QFj-init-fix44-testreq-echo`, `HP-QFj-acc-fix44-recovery-outbound` |
| `scenario_group` | enum | `testrequest_echo` \| `idle_cadence` \| `recovery_inbound` \| `recovery_outbound` \| `session_reject` |
| `counterparty` | enum | `quickfix-j` (v1.0 G1); `quickfix-cpp` optional |
| `role` | enum | `fixpp-initiator` \| `fixpp-acceptor` (both required per FR-005a) |
| `fix_version` | const | `FIX.4.4` (LIVE) |
| `security_profile` | const | `one_way_ca` (TLS) |
| `direction(s)` | set | which way(s) the round-trip is asserted (e.g. fixpp→peer and peer→fixpp for US1) |
| `golden_ref` | path | `happy/golden/<cell_id>.fix` |
| `inproc_witness` | descriptor | the fixpp-state assertion (E2) |
| `disposition` | enum | `live` \| `skip:counterparty-unavailable` \| `deferred:<axis>` (a `n/a` cell; closed `deferred:*` tag set — pairs with `status n/a` + a `deferred_reason`, E4) |

**Validation**: every `scenario_group × role` combination present (per-cell completeness, no silent absence — 016 rule); **AND** for **every** `(scenario_group, role)` cell, its `acceptance_ids == union(AdminRoundTrip.ac_ref) == expected_ac_ids_for_group` as an **exact set** (per-`(group,role)`, per the descriptor contract rule 8 table) — so each role independently covers every AC of its group; a coarse global-union check is satisfiable while one role silently omits an AC (e.g. fixpp-acceptor `recovery_inbound` missing US3-2), half-meeting FR-005a (`[[feedback_completeness_gate_exact_set_not_subset]]`, applied one axis too coarse). The whole-feature exact set `{covered across all (group,role)} == {US1-1,US1-2,US1-3, US2-1,US2-2, US3-1,US3-2,US3-3,US3-4, US4-1,US4-2}` follows as a **derived consequence**. Each live cell cites a `[FIX-SL]` spec_ref.

**State**: a cell resolves to exactly one of `pass` / `skip:<reason>` / `n/a` per run (SC-002).

---

## E2 — `AdminRoundTrip` (the send/await unit)

The atomic assertion within a cell. Per R1, the **observe** half is split across in-process (fixpp state) and golden (wire frames).

| Field | Type | Notes |
|---|---|---|
| `originator` | enum | `fixpp` \| `quickfix-j` (who emits the prompting frame) |
| `prompt_frame` | descriptor | e.g. TestRequest(112=ID), ResendRequest(7/16), malformed-admin, induced-gap |
| `expected_reply` | descriptor | e.g. Heartbeat(112=ID), GapFill(123=Y)/replay(43=Y), Reject(35=3,45/373) |
| `correlation_key` | tag(s) | `112` (echo), `45`/`371` (reject ref), seqnum range (recovery) |
| `inproc_assert` | predicate | fixpp `state()` / `fsm_visit_history()` / seqnum-delta witness via the pre-existing tests-only `seqnum_mgr_test_access()` seam (NOT public API, NOT a new seam — R2/New-4) |
| `golden_assert` | frame match | the prompt+reply frames present in `golden_ref` (normalized under the explicit `{52,10}` admin profile) |
| `ac_ref` | acceptance-id | the spec acceptance scenario(s) this round-trip discharges, e.g. `US1-1`, `US3-2` (exact-set coverage — see E1 Validation) |
| `self_deadline` | duration | internal bound; missing/late frame → deterministic FAIL, never hang (FR-010) |
| `tolerance` | optional | cadence: ±1 beat over ~5s window (US2) |

**Note**: `prompt_frame` origination for fixpp-as-originator is **induced** (liveness loop / FSM emits it with an engine-chosen id), not chosen by a public admin-send API — none exists (R1). The `112` echo is correlated by the **observed** value, not a fixture-chosen id. Counterparty-origination + injection (gap, malformed-in-flight, QFJ-resend) is parent-driven (R3); malformed frames are corrupted in flight at the proxy, never originated by fixpp.

---

## E3 — `GoldenAdminTranscript`

The captured, canonicalized QuickFIX-J `toAdmin`/`fromAdmin` frame sequence for a cell, **including the new admin frames**.

| Field | Type | Notes |
|---|---|---|
| `path` | file | `happy/golden/<cell_id>.fix` |
| `frames` | ordered list | each a FIX admin frame (TestRequest/Heartbeat/ResendRequest/SequenceReset/Reject), both legs |
| `normalized_tags` | const set | `{52, 10}` only — an **explicit** admin profile passed into `diff_transcripts`' `excluded_tags` parameter; NOT the 016 default `{9,10,34,52,60,112,122}`, which would drop `112`/`34`/`122`/`123` (R6). No library change. |
| `capture_origin` | const | engine-log seam (no MITM); captured at first paired run, never hand-fabricated |

**Validation**: a deliberate single-tag mutation of a **compared** tag (`112`/`34`/`7`/`16`/`122`/`123` — NOT one of the canonicalized `{52,10}`) must make the drift gate FAIL (gate-bite verification, SC-004).

---

## E4 — `CellResultRow` (reused verbatim from 016)

The `cell_results.yaml` entry validated by the in-repo `interop_cell_results_schema_check`.

| Field | Type | Notes |
|---|---|---|
| `cell_id` | string | matches E1 |
| `status` | enum | `pass` \| `fail` \| `skip:<reason>` \| `known-limitation:<tracking-issue>` \| `n/a` (aligned verbatim to the 016 `cell_results_schema_check` enum, `cell_results_schema_check_test.py:67`) |
| `matrix_disposition` | enum | `live` (for `pass`/`fail`/`skip:<reason>`) \| `deferred:*` (closed tag set, for `status n/a`) — `status n/a ⇔ matrix_disposition deferred:*` (the schema-check core invariant, `:71-84`) |
| `tier` | const | `release-prep` (G1 cells are not per-PR, R7) |
| `spec_ref` | string | `[FIX-SL §…]` |
| `deferred_reason` | string | required when `matrix_disposition` is `deferred:*` (i.e. `status n/a`); `skip:<reason>`/`known-limitation:<tracking>` carry their reason inline in the status |

**Validation**: round-trips through the **same** schema-check rules as the 016 matrix (no schema change); a live `skip:<reason>`/`fail` ⇒ cell badge-ineligible (FR-009). A by-design deferral is `status n/a` + `matrix_disposition deferred:*` (closed tag set) + a `deferred_reason` — never `skip` (`skip:<reason>` is strictly live counterparty-unavailable, `:87-97`).

---

## Relationships

```text
AdminScenarioCell (1) ──< (1..n) AdminRoundTrip          # a cell may assert >1 round-trip (e.g. US1 both directions)
AdminScenarioCell (1) ──  (1)   GoldenAdminTranscript    # one golden per cell
AdminScenarioCell (1) ──  (1)   CellResultRow            # one result row per cell per run
AdminScenarioCell      ──  reuses InteropEngineFixture   # run_until / stop_within (no change)
```

No production entity is created or modified. All four entities are test-fixture/data-file shapes under `tests/interop/`.
