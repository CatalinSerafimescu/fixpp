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
  round_trips        : [ AdminRoundTrip ]                      # E2; >=1
  inproc_witness     : { fsm_end_state, history_contains[], seqnum_delta_min }
  self_deadline_ms   : int                                     # FR-010; per round-trip override allowed
}
```

## Contract rules

1. **Completeness**: for every `scenario_group`, BOTH `role` values MUST have a descriptor (per-cell no-silent-absence, 016 rule + `[[feedback_completeness_gate_exact_set_not_subset]]`). The matrix manifest asserts `{present cells} == {expected cells}` (exact-set equality, not subset).
2. **Spec reconciliation**: `spec_ref` cites the FIX session-layer section the cell proves; assertions reconcile to spec, not to QuickFIX-J behaviour (engine-drift rule, `[const §VI]`).
3. **In-process witness is fixpp-state-only** (R1/R2): `inproc_witness` MUST be expressible via `state()` / `fsm_visit_history()` / outbound-seqnum delta / `stop_within`. It MUST NOT assert counterparty-emitted frame contents (those are golden-only).
4. **Self-deadline mandatory** (FR-010): every live-I/O wait carries an internal deadline; a missing/late frame fails deterministically — `ioc.run()` is never relied on to terminate (`[[feedback_fail_placeholder_red_test]]`).
5. **Graceful degradation** (FR-009): when `counterparty_probe` reports QFJ absent, the cell resolves `skip:counterparty-unavailable` (GTEST_SKIP with reason), never silent-pass.
6. **No production origination dependency** (R-prod): a descriptor MUST be realizable with the public post-015 surface + parent orchestration. If realizing it would need a production admin-originate API, that descriptor is a Gate-A-re-triggering finding, not an implicit production change.
