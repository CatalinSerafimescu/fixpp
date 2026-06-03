# Contract: Admin Scenario Descriptor (per-cell)

**Feature**: `018-interop-live-admin` | **Date**: 2026-06-03
Extends the 016 `tests/interop/support/scenario_descriptor.hpp`. Defines the in-repo descriptor a G1 admin cell carries. **No production type.**

## Shape (conceptual — realized as a `scenario_descriptor.hpp` extension)

```text
AdminScenarioDescriptor {
  cell_id            : string            # E1.cell_id
  scenario_group     : { testrequest_echo, idle_cadence, recovery_inbound,
                         recovery_outbound, session_reject }
  role               : { fixpp-initiator, fixpp-acceptor }     # both required (FR-005a)
  counterparty       : { quickfix-j }                          # v1.0 G1
  security_profile   : one_way_ca                              # TLS, const
  spec_ref           : "[FIX-SL §...]"                         # mandatory, per cell
  golden_ref         : "happy/golden/<cell_id>.fix"
  round_trips        : [ AdminRoundTrip ]                      # E2; >=1; each carries ac_ref
  acceptance_ids     : [ "US1-1", ... ]                        # per-(scenario_group,role) exact-set coverage target (rule 7)
  inproc_witness     : { fsm_end_state, history_contains[], seqnum_delta_min }  # seqnum via seqnum_mgr_test_access() (tests-only seam, not public/new)
  induction          : scenario-specific                       # value REQUIRED by scenario_group (rule 8 table); one mechanism, pinned per cell
  self_deadline_ms   : int                                     # FR-010; per round-trip override allowed
}
```

## Contract rules

1. **Completeness**: for every `scenario_group`, BOTH `role` values MUST have a descriptor (per-cell no-silent-absence, 016 rule + `[[feedback_completeness_gate_exact_set_not_subset]]`). The matrix manifest asserts `{present cells} == {expected cells}` (exact-set equality, not subset).
2. **Spec reconciliation**: `spec_ref` cites the FIX session-layer section the cell proves; assertions reconcile to spec, not to QuickFIX-J behaviour (engine-drift rule, `[const §VI]`).
3. **In-process witness is fixpp-state-only** (R1/R2): `inproc_witness` MUST be expressible via the public `state()` / `fsm_visit_history()` / `stop_within`, plus the **pre-existing tests-only** outbound-seqnum delta via `seqnum_mgr_test_access().peek_outbound()` (used by the 016 `hp_fix44_testrequest_echo_test.cpp`; NOT public API, NOT a new seam — New-4). The design depends on **no** private-state friend seam beyond this established one. It MUST NOT assert counterparty-emitted frame contents (those are golden-only).
4. **Self-deadline mandatory** (FR-010): every live-I/O wait carries an internal deadline; a missing/late frame fails deterministically — `ioc.run()` is never relied on to terminate (`[[feedback_fail_placeholder_red_test]]`).
5. **Graceful degradation** (FR-009): when `counterparty_probe` reports QFJ absent, the cell resolves `skip:counterparty-unavailable` (GTEST_SKIP with reason), never silent-pass.
6. **No production origination dependency** (R-prod): a descriptor MUST be realizable with the public post-015 surface + parent orchestration. fixpp-originated admin frames are **FSM/liveness-induced** (engine-chosen id, correlated by the observed value), never fixture-chosen; if realizing a descriptor would need a production admin-originate API, that descriptor is a Gate-A-re-triggering finding, not an implicit production change.
7. **AC-level exact-set coverage, per-`(scenario_group, role)`** (Codex#5, Codex#2): for **every** required `(scenario_group, role)` descriptor, that descriptor's `acceptance_ids` MUST equal the union of its own `round_trips[].ac_ref` **and** MUST equal that group's expected AC subset (rule 8 table column) as an **exact set** — `acceptance_ids == union(round_trips[].ac_ref) == expected_ac_ids_for_group` (missing/unexpected diff reported). So **each role independently** covers every AC of its group; a global-union check is satisfiable while one role silently omits an AC (e.g. fixpp-acceptor `recovery_inbound` missing US3-2 while the initiator covers it), which would silently half-meet FR-005a (`[[feedback_completeness_gate_exact_set_not_subset]]`, applied one axis too coarse). The whole-feature exact set — `{covered across all (group,role)} == {US1-1,US1-2,US1-3, US2-1,US2-2, US3-1,US3-2,US3-3,US3-4, US4-1,US4-2}` — is then a **derived consequence** of the per-`(group,role)` checks, not a substitute for them.
8. **Per-`scenario_group` induction + AC coverage table** (New-3, Codex#1, Codex#2): `induction` is **scenario-specific** — its required value is fixed BY `scenario_group` per the table below, covering all five groups (the round-1 three-value `{withhold_frame|qfj_restart_resend|proxy_corrupt}` domain omitted the two non-induced groups). A cell MUST NOT mix mechanisms — different mechanisms yield different golden frame sets, defeating the capture-once golden's determinism. The same table fixes each group's `expected_ac_ids` subset (rule 7's coverage target) and asserts BOTH `role`s are required (rule 1):

   | `scenario_group`   | expected `induction` | expected `acceptance_ids` (the AC subset) | roles (both required) |
   |---|---|---|---|
   | `testrequest_echo`  | `inbound_silence`   | `{US1-1, US1-2, US1-3}`          | `fixpp-initiator`, `fixpp-acceptor` |
   | `idle_cadence`      | `idle_observation`  | `{US2-1, US2-2}`                 | `fixpp-initiator`, `fixpp-acceptor` |
   | `recovery_inbound`  | `withhold_frame`    | `{US3-1, US3-2, US3-4}`          | `fixpp-initiator`, `fixpp-acceptor` |
   | `recovery_outbound` | `qfj_restart_resend`| `{US3-3, US3-4}`                 | `fixpp-initiator`, `fixpp-acceptor` |
   | `session_reject`    | `proxy_corrupt`     | `{US4-1, US4-2}`                 | `fixpp-initiator`, `fixpp-acceptor` |

   `idle_cadence` is **passive idle observation** with no active induction (`idle_observation`); `testrequest_echo`'s fixpp→peer leg is induced by **inbound silence** (`inbound_silence`), not a fixture-chosen frame. `US3-4` (both peers back at `Active`, no prefix loss) is the shared recovery-completion AC asserted by **both** recovery groups; the per-`(group,role)` checks of rule 7 union to the whole-feature 11-element set.
