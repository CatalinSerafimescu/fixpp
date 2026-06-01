# Contract: Interop Scenario Descriptor

**Feature**: `016-interop-harness` | **Date**: 2026-06-01

This is the interface the harness exposes to scenario authors and to the parent orchestration: the shape of a scenario fixture. It is a **test-fixture contract**, not a public library API (R1/R3). Concrete encoding (a C++ fixture struct + an optional checked-in `.toml`/`.yaml` descriptor) is chosen at `/speckit-implement`; this pins the required fields.

## Happy-path cell descriptor (US1)

```
id:            HP-<engine>-<role>-<ver>-<chain>[-tls]    # stable, unique
counterparty:  quickfix-cpp | quickfix-j | fix8          # fix8 ⇒ placeholder, not executed
role:          fixpp-initiator | fixpp-acceptor
fix_version:   FIX.4.4                                    # LIVE at v1.0
               | FIXT.1.1/FIX.5.0SP2                       # placeholder, deferred:fixt-routing, not run at v1.0 (FR-003)
security:      plain-tcp | tls-logon                     # mtls-mutual reserved (v1.1)
event_chain:   logon-hb-logout | testrequest-echo | reject-invalid-admin
               | seqnum-recovery | disconnect-reconnect-noreset
business:      none                                       # A-row cells exist as deferred:app-messages, not run
counterparty_cfg: <path or generated>                     # produced/owned by the parent harness
fixpp_cfg:        { EngineConfig, SessionConfig, store_path }
port:             <int, leased by the parent>
golden_ref:       tests/interop/happy/golden/HP-<id>.fix
pass_criteria:
  fixpp_end_state:      <fsm_state on fixpp side>
  counterparty_terminal: <wire-observed: received Logout(35=5) | orderly socket close>  # FR-007 — wire-observed, NOT an internal counterparty-FSM probe
  seqnum_delta:         { in: <n>, out: <n> }
  golden:               match-after-normalization
deadline_ms:      <internal self-deadline>                # R5; never rely on ioc.run() to end
reconnect_policy: <finite max-attempts>                   # live reconnect cells only — no busy-spin (FR-004)
stop_watchdog_ms: <bound>                                 # asserts Engine::stop() returns within bound (FR-004/FR-028)
spec_ref:         [FIX-SL §x.y] | [FIX-TC TC-NNN] | [FIXS §3.x]   # FR-018: reconcile to spec, not engine
```

**`counterparty_terminal` population (parent-proxy boundary, FR-007)**
The in-repo GoogleTest driver asserts fixpp's own FSM end-state + seqnum deltas directly (it owns the fixpp side). The `counterparty_terminal` evidence (received `Logout(35=5)` / orderly socket close) is **wire-observed from the parent-captured transcript**, not an in-process probe of the counterparty FSM. Population rule: the **parent harness** writes the wire-observed terminal token into the per-cell `cell_result` (contract `parent-harness-gate-contract.md`); the in-repo driver consumes it from the parent-provided capture artifact (file/pipe handed to the fixture) and asserts the expected token, OR — when run standalone without the parent (e.g. the smoke cell in a minimal env) — records `counterparty_terminal: unobserved` and relies on the parent tier for the FR-007 assertion. A driver MUST NOT fabricate the terminal token from fixpp-side state.

**Required invariants**
- Every cell cites a `spec_ref` (FR-018). A cell justified "because QFC/QFJ does X" is invalid.
- `security: tls-logon` cells require a `SecurityProfile` the counterparty config can satisfy (R8).
- Skipped/unavailable counterparty ⇒ `skip_reason`, never silent pass (FR-023).
- Every matrix axis covered ≥ once; gaps carry `deferred:<tag>` + rationale (FR-008). FIX 5.0 SP2/FIXT.1.1 cells carry `deferred:fixt-routing` and are not executed at v1.0 (FR-003).
- Live reconnect cells MUST set a finite `reconnect_policy` (no busy-spin) + a `stop_watchdog_ms` bound (FR-004). The deliberate down-peer/never-listening case is a *separate* cell, out of the happy-path matrix (FR-028).

## Thorny corpus scenario descriptor (US2)

```
id:           thorny-<engine>-issue-<NNN>
provenance:   { engine, ref: issue#NNN|PR#NNN, url, state: closed|open }   # FR-011
category:     <one of the 12 phase-9.md categories>
priority:     P1|P2|P3 (closed) | watch:P1|watch:P2|watch:info (open)        # FR-012
trigger:      <wire bytes to inject> | <session-event sequence to drive>
expected:     <spec-conformant fixpp behavior + expected wire/state>
spec_ref:     [FIX-SL §x.y] | [FIX-TC TC-NNN]
differentiator: <true if fixpp spec-correct while upstream buggy>           # FR-015
disposition:  pass | known-limitation:<tracking-issue>                       # FR-014
```

**Required invariants**
- Append-only across releases (FR-013).
- `P1`/`watch:P1` failing ⇒ tag blocked unless `known-limitation` + open issue (FR-014).
- Executable fixture in-repo; sweep analysis in the parent (`[const §XV.18]`, R2).
