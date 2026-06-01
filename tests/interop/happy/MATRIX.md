# US1 happy-path interop matrix manifest

Feature `016-interop-harness`, User Story 1. Enumerates **every** matrix cell —
live and deferred — so no cell is silently absent (per-cell completeness rule,
`contracts/parent-harness-gate-contract.md`). Each live cell cites a FIX spec
section (FR-018/SC-006); each deferred row carries a rationale + tag (FR-008).

## Axes (v1.0)

`counterparty ∈ {quickfix-cpp, quickfix-j}` × `role ∈ {fixpp-initiator, fixpp-acceptor}`
× `fix_version = FIX.4.4 (LIVE)` × `{session-admin event chains}` — **all over TLS**.

**All-TLS baseline (FR-025, reconciled 2026-06-01):** fixpp ships TLS-only (no
plaintext transport; `SecurityProfile` ∈ {`mtls_ca`,`mtls_pinned`,`one_way_ca`}).
Every live cell runs over TLS with the server-auth **`one_way_ca`** baseline
profile (the counterparty presents a server cert fixpp's CA trusts; fixpp-as-
acceptor presents its leaf and requires no client cert). App-layer client-cert
identity binding (mutual mTLS) is `deferred:v1.1-mtls` (see below).

## Live cells

Drivers value-parameterize over `(counterparty × role)`, so one file covers 4
cells (T014 = initiator-only → 2). Live cells are counterparty-required: absent
⇒ `GTEST_SKIP` with reason (FR-023), never silent-pass. The byte-level golden
match + wire-observed counterparty terminal behavior are asserted by the PARENT
gate against the proxy capture (research R1); the in-repo driver asserts fixpp's
FSM end-state (reaches `Active`) + outbound seqnum delta + bounded graceful stop.

| Event chain | Driver | Cells | spec_ref | Disposition |
|-------------|--------|-------|----------|-------------|
| Logon → idle Heartbeat → Logout (**smoke**, FR-022) | `hp_fix44_logon_hb_logout_test.cpp` (T010) | QFcpp/QFj × init/acc (4) | [FIX-SL §4.3/§4.5.1/§4.6] | live |
| Active+idle Heartbeat + TestRequest echo | `hp_fix44_testrequest_echo_test.cpp` (T011) | QFcpp/QFj × init/acc (4) | [FIX-SL §4.5.1/§4.5.5] | live |
| Invalid admin → Reject(35=3) + RefTagID/RefMsgType | `hp_fix44_reject_invalid_admin_test.cpp` (T012) | QFcpp/QFj × init/acc (4) | [FIX-SL §4.5.4] | live |
| Seqnum gap → ResendRequest / SequenceReset-GapFill | `hp_fix44_seqnum_recovery_test.cpp` (T013) | QFcpp/QFj × init/acc (4) | [FIX-SL §4.5.3/§4.8.1/§4.8.2/§4.8.5] | live |
| Abrupt disconnect → reconnect (ResetSeqNumFlag=N) | `hp_fix44_disconnect_reconnect_noreset_test.cpp` (T014) | QFcpp/QFj × init (2) | [FIX-SL §4.4.2/§4.4.3/§4.8.6] | live (finite reconnect policy + stop watchdog, FR-004) |

**18 live matrix cells.** The smoke cell id is `HP-QFcpp-init-fix44-logon-hb-logout`.

### Separate (non-matrix) regression cell

| Cell | Driver | spec_ref | Note |
|------|--------|----------|------|
| Down-peer `stop()` watchdog (FR-028) | `hp_down_peer_stop_watchdog_test.cpp` (T016) | [FIX-SL §4.4] + FR-004/028 | **Not** in the matrix; no counterparty (deliberate never-accepting peer) → runs green locally. Guards the 015 down-peer L2 carry-forward / T008 fix. The watchdog IS the assertion. |

## Deferred rows (present, NOT executed — `status: n/a`)

| Row | Tag | FR | Rationale |
|-----|-----|----|-----------|
| Business-message cells (Logon → NewOrderSingle → ExecutionReport → Logout) | `deferred:app-messages` | FR-005 | v1.0 scope is session-only; the §VII.6 business flow is an open v1.0-GA residual (FR-027/SC-008), not discharged by this badge. Forward pointer: A-001/A-006. |
| FIX 5.0 SP2 / FIXT.1.1 cells (both counterparties, both roles) | `deferred:fixt-routing` | FR-003 | fixpp cannot establish a FIXT.1.1 / 5.0SP2 session today (S-020 FIXT half `implementing(4.4 only)`, S-025 `DefaultApplVerID(1137)` backlog; 005 defers FIXT logon-time semantics). Activate when FIXT routing + 1137 land. |
| Fix8 happy-path cells | `deferred:fix8-revisit` | FR-009 | Fix8 is corpus-only at v1.0; its live disposition is revisited later from corpus findings. |
| Mutual-certificate (client-cert) mTLS cells | `deferred:v1.1-mtls` | FR-025 | App-layer client-cert identity binding (013/014 fail-closed CompID↔cert, session profile `mtls_ca`) is the v1.1 reach. The v1.0 baseline is server-auth `one_way_ca`. |

Axis coverage (FR-008): every `counterparty × role` is covered ≥ once by the live
chains; the FIX-version axis beyond 4.4 and the business/Fix8/mTLS axes each carry
a deferred row above. No axis is silently absent.

## Parent-harness obligations (out-of-repo, gitignored `../phase-9-harness/`)

The in-repo drivers declare the env contract the parent MUST satisfy:

1. **Counterparty SSL config.** QuickFIX-cpp built `--with-openssl`; QFC/QFJ
   configured as SSL acceptors/initiators presenting a server cert the fixture's
   `one_way_ca` CA (`tests/tls/fixtures/ca.pem`) trusts.
2. **Endpoint lease.** `INTEROP_<TOKEN>_PORT` / `_HOST` for fixpp-initiator cells
   (the counterparty's SSL acceptor). For fixpp-acceptor cells, fixpp binds
   `INTEROP_FIXPP_PORT` (or OS-assigns); the parent reads the bound port via
   `Engine::acceptor_bound_endpoint()` and points its counterparty-initiator
   there (the **acceptor rendezvous**).
3. **Wire capture + golden diff.** The parent's passthrough proxy captures the
   wire and diffs it against `golden/HP-*.fix` (research R1: no in-library
   capture). The goldens are captured at the first paired run (T009, deferred —
   see `golden/FORMAT.md`); they MUST NOT be hand-fabricated.
4. **Counterparty bring-up FIRST** (R5) so a fixpp initiator never aims at a
   not-yet-listening peer (the down-peer hazard T016 guards).

## ctest

```bash
cmake --build build/linux-clang-debug --target interop_happy -j2
ctest --test-dir build/linux-clang-debug -L interop-happy --output-on-failure
```

Locally (no counterparty): the matrix cells skip-with-reason; the down-peer
watchdog (T016) runs green. Live green requires the parent harness + a running
SSL counterparty.
