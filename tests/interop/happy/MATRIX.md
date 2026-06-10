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

## G1 admin round-trip cells (018-interop-live-admin)

G1 enriches the live session-admin cells to assert **real bidirectional FIX 4.4
session-admin traffic on the established session** (beyond the Logon/Logout
handshake the 016 cells captured). **QuickFIX-J only** at v1.0 G1; **both fixpp
roles**. Goldens compared under the explicit `{52,10}` admin profile (NOT the
016 default — it would drop `112`/`34`/`122`/`123`). Per-cell completeness:
every `(scenario_group × role)` present; each cell's `acceptance_ids` is the
exact set for its group (descriptor rule 7/8). Goldens captured at first paired
run (parent harness + live QFJ); absent ⇒ `skip:golden-not-yet-captured`.

| scenario_group | Driver | Cells (QFj × role) | acceptance_ids | spec_ref | Naming |
|----------------|--------|--------------------|----------------|----------|--------|
| `testrequest_echo` | `hp_fix44_testrequest_echo_test.cpp` (T007) | init/acc (2) | {US1-1, US1-2, US1-3} | [FIX-SL §4.5.5] | reuse-and-enrich `…-testrequest-echo` |
| `idle_cadence` | `hp_fix44_idle_heartbeat_cadence_test.cpp` (T016) | init/acc (2) | {US2-1, US2-2} | [FIX-SL §4.5.1] | **new** `…-idle-cadence` |
| `recovery_inbound` | `hp_fix44_seqnum_recovery_test.cpp` (T011) | init/acc (2) | {US3-1, US3-2, US3-4} | [FIX-SL §4.8.2/§4.8.5/§4.5.3] | reuse-and-enrich `…-seqnum-recovery` |
| `recovery_outbound` | `hp_fix44_recovery_outbound_answer_test.cpp` (T012) | init/acc (2) | {US3-3, US3-4} | [FIX-SL §4.8.2/§4.8.5/§4.8.6] | **new** `…-recovery-outbound` |
| `session_reject` | `hp_fix44_reject_invalid_admin_test.cpp` (T019) | init/acc (2) | {US4-1, US4-2} | [FIX-SL §4.5.4] | reuse-and-enrich `…-reject-invalid-admin` |

**10 G1 base cells** (5 scenario_groups × 2 roles). Each cell carries an
SC-004 gate-bite negative test (mutate a **compared** tag — `112`/`7`/`16`/`123`/`45`/`373`,
never the canonicalized `{52,10}`) proving the drift gate bites; the gate-bite
runs self-contained (no counterparty). The whole-feature AC exact set
`{US1-1..US4-2}` follows as a derived consequence of the per-`(group,role)` checks.

### Separate (non-matrix) regression cell

| Cell | Driver | spec_ref | Note |
|------|--------|----------|------|
| Down-peer `stop()` watchdog (FR-028) | `hp_down_peer_stop_watchdog_test.cpp` (T016) | [FIX-SL §4.4] + FR-004/028 | **Not** in the matrix; no counterparty (deliberate never-accepting peer) → runs green locally. Guards the 015 down-peer L2 carry-forward / T008 fix. The watchdog IS the assertion. |

## G3 slice 3 — ResetOnLogon interop cells (024-reset-refresh-on-logon)

Extends the 018 fixture with two new scenario groups covering both-role live
`reset_on_logon` interop (SC-005 / FR-010 / C6.1, C6.2). Value-parameterized
over `counterparty ∈ {quickfix-cpp, quickfix-j}` × role (initiator / acceptor) in
one file; 4 cells total.

| Scenario | Driver | Cells | Clause | In-process witnesses |
|----------|--------|-------|--------|---------------------|
| `reset_on_logon_initiator` — fixpp initiator with `reset_on_logon=true` sends `141=Y` + `34=1`; live acceptor accepts; both sides resync from 1 | `hp_fix44_reset_on_logon_test.cpp` (T016) | QFcpp/QFj × init (2) | C6.1 / FR-010 | (a) FSM Active; (b) outbound seqnum ≥ 2 after Active (Logon accepted) |
| `reset_on_logon_acceptor` — live initiator sends `141=Y` + `34=1`; fixpp acceptor with `reset_on_logon=true` resets before `check_inbound`, admits `34=1`, reaches Active; no ResendRequest | `hp_fix44_reset_on_logon_test.cpp` (T017) | QFcpp/QFj × acc (2) | C6.2 / FR-010 | (a) FSM Active (no disconnect); (b) outbound > 1 (reply Logon sent); (c) next\_inbound == 2 (no ResendRequest issued) |

Golden artifact names (captured by parent harness at first paired live run):

| Golden file | Cell | Note |
|-------------|------|------|
| `happy/golden/RL-QFcpp-init-fix44-reset-on-logon.fix` | QFcpp initiator T016 | 141=Y + 34=1 verbatim; admin profile {52,10} |
| `happy/golden/RL-QFj-init-fix44-reset-on-logon.fix` | QFj initiator T016 | 141=Y + 34=1 verbatim; admin profile {52,10} |
| `happy/golden/RL-QFcpp-acc-fix44-reset-on-logon.fix` | QFcpp acceptor T017 | peer 34=1 Logon + fixpp reply Logon; admin profile {52,10} |
| `happy/golden/RL-QFj-acc-fix44-reset-on-logon.fix` | QFj acceptor T017 | peer 34=1 Logon + fixpp reply Logon; admin profile {52,10} |

### 030-received-reset-inbound-advance — received-141 acceptor cell (SC-001)

The live close-out of the conformance defect that motivated 030 (found via a failed live
acceptor cell vs QFcpp/QFJ). **Distinct from the 024 `reset_on_logon`-ON acceptor cell
above**: here fixpp's OWN `reset_on_logon` stays **OFF** so it takes the 013-only
received-141 path (reset AFTER `check_inbound`). 1 scenario × `counterparty ∈ {QFcpp, QFj}`
= 2 cells.

| Scenario | Driver | Cells | Clause | In-process witnesses |
|----------|--------|-------|--------|---------------------|
| `received_reset_acceptor` — live initiator (`ResetOnLogon=Y`) sends `141=Y` + `34=1`; fixpp acceptor with `reset_on_logon=FALSE` takes the received-141 path → next-expected-inbound nets 2, peer's `34=2` accepted with no spurious ResendRequest | `hp_fix44_received_reset_test.cpp` (T028) | QFcpp/QFj × acc (2) | SC-001 / FR-001 / FR-002 | (a) FSM Active (no disconnect on 141=Y+34=1); (b) outbound > 1 (reply Logon sent); (c) next\_inbound == 2 (the 030 correction — pre-030 this read 1 → spurious ResendRequest on 34=2) |

| Golden file | Cell | Note |
|-------------|------|------|
| `happy/golden/RR-QFcpp-acc-fix44-received-reset.fix` | QFcpp acceptor T028 | peer 141=Y+34=1 Logon + fixpp reply (34=1, 789=2 if 027-on) + peer 34=2 accepted, NO fixpp 35=2 ResendRequest; admin profile {52,10} |
| `happy/golden/RR-QFj-acc-fix44-received-reset.fix` | QFj acceptor T028 | peer 141=Y+34=1 Logon + fixpp reply + peer 34=2 accepted, NO fixpp ResendRequest; admin profile {52,10} |

Golden absent → `skip:golden-not-yet-captured` (per `diff_golden_or_skip` convention).
MUST NOT be hand-fabricated.

**Parent-harness obligations for T016/T017:**
- T016 (initiator): run QFcpp/QFJ as TLS acceptors tolerating `141=Y` Logons. No
  special initiator config on the counterparty side (they receive 141=Y, which
  both QFcpp and QFJ accept under the `bilateral_lenient` / default policy).
  Set `INTEROP_<TOKEN>_PORT` + optional `INTEROP_<TOKEN>_HOST`.
- T017 (acceptor): configure the counterparty INITIATOR with `ResetOnLogon=Y`
  (QFcpp: `[SESSION] ResetOnLogon=Y`; QFJ: `ResetOnLogon=Y` in `quickfix.properties`)
  so it sends `141=Y` + `34=1` on each Logon. Set `INTEROP_FIXPP_PORT` (or let
  OS-assign; bound port readable via `Engine::acceptor_bound_endpoint()`).

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
