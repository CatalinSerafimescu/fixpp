# Phase 1 — Data Model: admin-emit observation coverage matrix

No new data entities, fields, or state transitions. This feature wires call sites; the "model" is the
**observation coverage matrix** — the source of truth for the exhaustive single-pass (FR-005) and for
the exact-count invariant (SC-001/SC-002).

## Coverage matrix — all engine-originated admin-builder emit sites

Legend: **callback** = which `Application` hook fires before store/emit · **order** = callback
position relative to `assign_outbound` · **036** = wired by this feature (✱) or pre-existing (·).

> **[PR #136 amendment — 2026-06-18]** The inbound-Heartbeat echo emit site (old `session.cpp:3045`/`:3088`,
> listed at `:3045`, `:3088` in the "Already observed before 036" row below) was **retired** by
> PR #136 (`fix/no-heartbeat-echo-on-inbound-heartbeat`). Post-PR #136 the live census is
> **14 wired / 24 `build_*`** (14 wired + 10 unwired = 24). The surviving Heartbeat `build_*` sites
> are the TestRequest(35=1)→Heartbeat reply and the liveness Heartbeat. The table below is the
> 036-era point-in-time record; row anchors are historical and are retained as-is.

### Already observed before 036 (15 — unchanged)

| Site (HEAD) | Frame | Callback | 036 |
|---|---|---|---|
| `:833` | `Logon(35=A)` initiator | `toAdmin` | · |
| `:2118` | `Reject(35=3)` FIXT-1137 | `toAdmin` | · |
| `:2244` | `Logon(35=A)` acceptor reply | `toAdmin` | · |
| `:2421` | `Logout(35=5)` Q3 (paired) | `toAdmin` | · |
| `:2535` | `ResendRequest(35=2)` | `toAdmin` | · |
| `:2696` | `Logout(35=5)` 021 Arm D (paired) | `toAdmin` | · |
| `:2889` | `Logout(35=5)` confirming | `toAdmin` | · |
| `:3045`, `:3088` | `Heartbeat(35=0)` | `toAdmin` | · <!-- [PR #136] retired — inbound-Heartbeat echo removed --> |
| `:4271` | `Heartbeat(35=0)` liveness | `toAdmin` | · |
| `:4326` | `TestRequest(35=1)` | `toAdmin` | · |
| `:4627` | `Logout(35=5)` graceful close | `toAdmin` | · |
| `:4694` | `SequenceReset-GapFill(35=4)` | `toAdmin` | · |
| `:4890`, `:4934` | `Logout(35=5)` 789 | `toAdmin` | · |

### Newly observed by 036 (10 — the scope)

| Site (HEAD) | Frame | Callback | Order vs `assign_outbound` | Vetoable | 036 |
|---|---|---|---|---|---|
| `emit_session_reject_` `:1728` (callers `:3026`, `:3215`) | `Reject(35=3)` | `toAdmin` | after | no (throw→Disconnected) | ✱ |
| `:2400` | `Reject(35=3)` | `toAdmin` | after | no | ✱ |
| `:2484` | `Reject(35=3)` | `toAdmin` | after | no | ✱ |
| `:2599` | `Reject(35=3)` | `toAdmin` | after | no | ✱ |
| `:2644` | `Reject(35=3)` | `toAdmin` | after | no | ✱ |
| `:2675` | `Reject(35=3)` | `toAdmin` | after | no | ✱ |
| `:2946` | `Reject(35=3)` | `toAdmin` | after | no | ✱ |
| `:4576` | `Reject(35=3)` | `toAdmin` | after | no | ✱ |
| `:3368` | `Logout(35=5)` | `toAdmin` | after | no | ✱ |
| `:3249` | `BusinessMessageReject(35=j)` | **`toApp`** | **before** | **yes** (`app_do_not_send`→drop, stay Active, no seqnum consumed) | ✱ |

## Invariants

- **INV-COV-1** (FR-005 / SC-002): after 036, the number of engine-originated **administrative**
  emit sites whose `callback` column is empty is **zero**. (Pre-036 it is 9.)
- **INV-COV-2** (FR-004): the one application-message site (`35=j`) routes through `toApp`, never
  `toAdmin`. It is **excluded** from the administrative `toAdmin` count.
- **INV-COV-3** (SC-001): with a **registered, non-throwing** `Application`, on the **non-veto** path,
  `toAdmin_calls == count(administrative frames emitted on the wire in that scenario)` — exact equality
  (not subset). Carve-outs (stated correctly in INV-COV-5 and the quickstart cell table): the BMR veto
  path is governed by INV-COV-5 (see there), and the no-`Application` helper caller (`:3215`,
  structurally reachable only when `application == nullptr`) is witnessed as an FR-006 byte-identity
  no-op, not a `toAdmin` count. This invariant does NOT contradict INV-COV-5 — it is scoped to exclude
  exactly the cases INV-COV-5 governs.
- **INV-COV-4** (FR-003): a `toAdmin` throw on any administrative site → `app_callback_threw` +
  `Disconnected`; `toAdmin` never suppresses an administrative frame (inspect-only).
- **INV-COV-5** (FR-004): a `toApp` `app_do_not_send` on the `35=j` site → frame not stored/emitted,
  session **stays Active**, **no outbound seqnum consumed** (originate-path INV-5 parity), **and the
  inbound-seqnum advance is STILL durably persisted** (`persist_inbound_advance_()` at `:3279` — the
  veto suppresses only the BMR emit, it must NOT early-return; an early return would under-persist →
  restart reprocesses, inverted [[feedback_unconditional_persist_at_multiexit_gate_breaks_lowerbound]]).
  A `toApp` throw → `app_callback_threw` + terminal close (persist moot under terminal close).
- **INV-COV-6** (FR-006): with no `Application` registered, every row is a byte-for-byte no-op
  (`fire_to_admin_` returns true immediately at `session.cpp:332`; the `toApp` block is gated on
  `engine_.application != nullptr`).
