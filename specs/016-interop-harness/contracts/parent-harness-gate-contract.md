# Contract: Parent-Harness Gate + Badge Obligations (US4/US5)

**Feature**: `016-interop-harness` | **Date**: 2026-06-01

The library deliverable is the SUT-side artifacts (`tests/interop/`, FR-026). Enforcement and publication (US4/US5) need real artifacts that are split between **in-repo CI** and the **gitignored parent harness** (`phase-9-harness/`). This contract names each obligation, where it lives, and which FR it maps to — so the parent-harness boundary is a *contract*, not a silence (Gate A round-1 root-cause #2).

## In-repo deliverables (committed to the submodule)

| Deliverable | File | FR |
|-------------|------|----|
| **Per-PR smoke CI workflow** | `.github/workflows/interop-smoke.yml` | FR-022 |

- Runs the single smoke cell `HP-QFcpp-init-fix44-logon-hb-logout` on the **normal build only**, `ubuntu-latest`, gating PRs that touch the transport/session/interop surface. Counterparty unavailable ⇒ skip-with-reason (FR-023), never silent pass. Maps to **SC-005** (per-PR smoke), NOT SC-001.

## Parent-harness gate-contract (named checks the library PR depends on)

These live in the gitignored parent `phase-9-harness/` (orchestration is out-of-repo per FR-026 + `[const §XV.18]`). The library PR depends on them as named CI checks; the parent MUST provide:

| Named check | Obligation | FR |
|-------------|------------|----|
| `interop-full-matrix` | Run every happy-path + thorny-corpus cell across **{normal, ASan/UBSan, TSan}**; a sanitizer-only failure blocks the GA tag identically to a normal-build failure (fix or written-justification suppression only). fixpp-process-only instrumentation. | FR-019, FR-020, FR-021 |
| `interop-gate-evaluator` | Evaluate matrix + corpus results against the committed **result schema** below (per-cell `cell_result` incl. `kind`/`matrix_disposition`/`priority`/`tracking_issue_state`); 100% non-deferred green (`matrix_disposition == live` only) required to pass; P1/`watch:P1` corpus failure blocks unless `known-limitation` + open `tracking_issue_state`. | FR-019, FR-020, FR-014 |
| `interop-release-prep` | The full matrix + corpus runs **at release-prep** and blocks the GA tag (the heavy tier; the per-PR `interop-smoke.yml` is the light tier). | FR-022 |
| `interop-badge-emit` | On a green full run: archive per-scenario captures + goldens; emit the **interop badge** naming exact counterparty `{name, version, commit}`, link the archived transcripts, list every documented known-limitation with its tracking issue. | FR-024 |

### Result schema (the evaluator's input/output shape)

The evaluator's input is one `cell_result` row **per matrix/corpus cell** — including every `deferred:*` cell (see the per-cell completeness rule below). The schema below is the authoritative shape the gate is computed from; every field the evaluator rule reads is present here.

```
cell_result:
  id:        <scenario id>
  config:    normal | asan-ubsan | tsan
  kind:      happy | thorny | parity              # row class
  status:    pass | fail | skip:<reason> | known-limitation:<tracking-issue> | n/a
                                                  # n/a ⇒ deferred:* row (not executed)
  matrix_disposition:  live | deferred:<tag>      # by-design scope disposition (FR-003/005/008/009)
                                                  # tags: deferred:fixt-routing | deferred:app-messages
                                                  #       | deferred:fix8-revisit | deferred:v1.1-mtls
  deferred_reason:     <free text>                # REQUIRED when matrix_disposition == deferred:*
  spec_ref:  <cited FIX section>                 # FR-018 / SC-006
  # corpus (thorny) rows only:
  priority:            P1 | P2 | P3 | watch:P1 | watch:P2 | watch:info   # FR-012
  tracking_issue_state: <open-issue ref>          # REQUIRED when status == known-limitation:* — the open tracking issue
gate_verdict:
  tier:                    smoke | release-prep        # FR-022 — skip handling is tier-dependent (see rule)
  non_deferred_green_pct:  <must be 100 for SC-001>   # computed over matrix_disposition == live cells ONLY
  blocking_failures:       [<cell ids>]               # see block rule below
  badge_eligible:          <bool>                      # release-prep ONLY; false if any required live cell is skip:* (FR-023/SC-001)
```

**`skip:<reason>` vs `deferred:<tag>` — strict disambiguation (do NOT conflate):**
- `status: skip:<reason>` is reserved **strictly** for FR-023 counterparty-unavailable (the binary isn't installed/reachable in *this* environment) — a transient, environment-dependent miss. It MUST NOT be overloaded to mean by-design deferral.
- By-design out-of-v1.0-scope deferral is expressed **only** by `matrix_disposition: deferred:<tag>` (with `status: n/a`), never by `skip:`. A `deferred:*` cell is not a runtime miss; it is a scope decision.

**Per-cell completeness rule (deferred cells are not silently absent):** the evaluator's input MUST contain a `cell_result` row for **every** cell in the matrix/corpus — including each `deferred:*` cell (carrying `status: n/a`). A *missing* row is a contract violation (a forgotten/dropped cell), distinct from a *present deferred* row. Deferred rows do NOT contribute to `non_deferred_green_pct` and do NOT gate green, but their presence is mandatory so a dropped cell is detectable rather than indistinguishable from a by-design deferral.

**Evaluator rule (tier-aware; over non-deferred live rows only):**

The evaluator takes a `tier` input — `smoke` (per-PR, FR-022) or `release-prep` (the full-matrix tag gate). `skip:` handling is **tier-dependent**: a required live cell skipped because the counterparty binary is absent is tolerable on a light per-PR job, but it MUST NOT count toward a GA badge (a `skip:` is never a pass — FR-023 "never silently reported as passed").

- `non_deferred_green_pct` (SC-001) is computed across `matrix_disposition == live` cells **only**; `deferred:*` rows are always excluded from the denominator.
- **`tier: release-prep`** — a `live` cell with `status: skip:*` is a **blocking miss**: it lands in `blocking_failures` and forces `badge_eligible: false`. The GA badge mints only when every required live cell actually ran (no required-cell `skip:` survives) — FR-023 + SC-001 ("both live counterparties, both roles, 100% non-deferred green"). The badge gate is the release-prep tier only.
- **`tier: smoke`** — a `live` cell with `status: skip:*` (counterparty absent in this PR job) is excluded from the denominator and does NOT block the PR; the smoke tier is a cheap regression catch (FR-022), not the badge gate.
- Corpus block rule (FR-014/SC-002): evaluated over corpus rows with `priority ∈ {P1, watch:P1}` — each MUST be `status: pass` **OR** `status: known-limitation:*` **with an open `tracking_issue_state`**. Any such row that fails without an open tracking issue lands in `blocking_failures`. `P2`/`P3`/`watch:P2`/`watch:info` rows do not block the tag.
- `blocking_failures` = any `matrix_disposition == live` cell with `status: fail` (any tier), **plus — at `release-prep` only — any `matrix_disposition == live` cell with `status: skip:*`**, plus any `P1`/`watch:P1` corpus row violating the block rule.

**FR mapping (what the extended schema now enforces):**

| Field / rule | Enforces |
|--------------|----------|
| `matrix_disposition: deferred:fixt-routing` | FR-003 (5.0SP2/FIXT.1.1 not live at v1.0) |
| `matrix_disposition: deferred:app-messages` | FR-005 (application-message cells deferred) |
| `matrix_disposition: live` filter for `non_deferred_green_pct` | FR-008 (axis coverage), SC-001 |
| `matrix_disposition: deferred:fix8-revisit` | FR-009 (Fix8 revisit deferred) |
| `kind: thorny` + `priority` + `tracking_issue_state` + corpus block rule | FR-012 (corpus priority buckets), FR-014 (block rule), SC-002 |
| per-cell completeness rule | FR-008/FR-012 (no cell silently absent) |
| tier-aware `skip:` rule (release-prep skip of a required live cell ⇒ blocking + `badge_eligible: false`) | FR-023 (skip never silently passed), SC-001 (no false-green GA badge) |

## Counterparty-state evidence rule (FR-007)

The parent capture provides the **wire transcript**; the gate asserts fixpp's FSM end-state + the **wire-observed counterparty terminal behavior** (received `Logout(35=5)` or orderly socket close in the capture). It does **not** probe the counterparty's internal FSM state — a transcript is terminal-behavior evidence, not counterparty-FSM-state observation.
