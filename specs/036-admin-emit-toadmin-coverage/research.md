# Phase 0 — Research: toAdmin/toApp observation coverage

All file:line cites are into `src/session/session.cpp` at branch `036-admin-emit-toadmin-coverage`
HEAD (post-035). The Fable assessment 2.4 §2 was at post-033; line numbers shifted ~+16.

## Decision 1 — Emit-site inventory re-verified at current HEAD

Reproducible census (run at branch HEAD, cwd = library submodule):

```
grep -nc 'fixpp::session::build_' src/session/session.cpp   → 25 build-call sites
grep -n  'fire_to_admin_(' src/session/session.cpp          → 15 call sites
   (excluding the function definition at :331; call sites :833 :2118 :2270 :2428
    :2561 :2701 :2896 :3071 :3114 :4297 :4352 :4638 :4765 :4897 :4940)
```

So **10 of 25 engine-originated `build_*` emit sites bypass observation; 15 already fire
`fire_to_admin_`** (unchanged by this feature). Of the 25 total: 24 admin-set frames + 1 BMR (an
application frame — `35=j` — NOT counted as admin). Of the 10 unwired: 9 administrative (8
`build_reject` sites incl. the shared helper + the Guard-3 `build_logout`) + 1 BMR. **15 wired + 10
unwired = 25.** (An earlier draft inherited "26 / 16" verbatim from the Fable assessment without
re-counting; the grep above is the authoritative count.) The **10 unobserved** sites are the scope:

| # | Site (HEAD) | Frame | Trigger / context | Arm | Fix |
|---|---|---|---|---|---|
| 1 | `emit_session_reject_` helper `:1728` (build_reject `:1736`; callers `:3026`, `:3215`) | `Reject(35=3)` | fromAdmin-veto reject **and** no-`Application` unknown-MsgType reject | ADMIN | `fire_to_admin_` between `assign_outbound` `:1742` and `store_then_emit` `:1747` |
| 2 | `:2400` | `Reject(35=3)` 371=52 373=10 | established-session Q3 SendingTime accuracy (paired `Logout` `:2421` **already** fires) | ADMIN | `fire_to_admin_` after `assign_outbound`, before `store_then_emit` |
| 3 | `:2484` | `Reject(35=3)` | fromAdmin-veto on inbound `SequenceReset` | ADMIN | same |
| 4 | `:2599` | `Reject(35=3)` 371=122 373=1 | 021 Arm C malformed-`OrigSendingTime(122)` | ADMIN | same |
| 5 | `:2644` | `Reject(35=3)` 371=122 373=1 | 021 RC#1 malformed-122 | ADMIN | same |
| 6 | `:2675` | `Reject(35=3)` 371=122 373=10 | 021 Arm D (paired `Logout` `:2696` **already** fires) | ADMIN | same |
| 7 | `:2946` | `Reject(35=3)` | fromAdmin-veto on inbound `Logout` | ADMIN | same |
| 8 | `:4576` | `Reject(35=3)` 371=36 373=5 | `SequenceReset` `NewSeqNo`-too-low | ADMIN | same |
| 9 | `:3368` | `Logout(35=5)` | initiator LogonSent Guard-3 (Logon-ack SendingTime failure) — the ONLY Logout of 7 not firing | ADMIN | `fire_to_admin_` after `assign_outbound`, before `store_then_emit` |
| 10 | `:3249` | `BusinessMessageReject(35=j)` | fromApp-reject reaction (`:3232-3267`) | **APP** | `toApp` **before** `assign_outbound` (veto-aware) |

**Rationale**: the inventory is authoritative for the exhaustive single-pass (FR-005) — the
half-pass anti-pattern (012 RC#B, [[feedback_half_restructure_symmetric_api]]) is precisely what
produced this gap (019 skipped Reject; 027 wired only the 789 Logouts; 033 stepped *around* the
helper). **Alternatives considered**: incremental per-site wiring — rejected (the cause of the bug).

## Decision 2 — ARM 1 ordering: `fire_to_admin_` after `assign_outbound`

The contract is **callback before store/transmit**. For seqnum-assignment ordering specifically, the
new admin sites adopt the **1137-Reject `assign`-then-`fire` shape** (033, `:2106` build / `:2113`
`assign_outbound` / `:2118` `fire_to_admin_` / `:2126` `store_then_emit`):
`assign_outbound` → `fire_to_admin_` (false → `record_state_transition_(Disconnected)` +
`co_return std::unexpected(app_callback_threw)`) → `store_then_emit`.

The 15 already-wired sites have **mixed** assign-vs-callback ordering and are **unchanged**: the
1137-Reject is the lone assign-then-fire site (fires AFTER `assign_outbound`) verified in review; the
wired Logouts/Heartbeats fire BEFORE it (e.g. the Q3
Logout `:2421` build / `:2428` fire / `:2433` assign; the ArmD Logout `:2696`/`:2701`/`:2706`; the
Heartbeat echo `:3064`/`:3071`/`:3076`). So this is NOT a "byte-identical to all 15 wired sites"
match — it matches exactly the one 1137-Reject site.

**Decision**: adopt the 1137-Reject `assign`-then-`fire` shape at all 9 admin sites. The choice is
**behaviourally immaterial** for the admin arm: `toAdmin` is **inspect-only / not vetoable**
(`session.cpp:323`; `fire_to_admin_` returns false only on a throw), and a throw always disconnects
→ Disconnected regardless of whether the seqnum was already assigned, so seqnum-assignment order does
not affect accounting. We pick the 1137-Reject shape for surgical consistency with the most-recent
admin reject pattern. **Alternative**: fire before `assign_outbound` (like the app arm / the wired
Logouts) — rejected: behaviourally equivalent, but the assign-then-fire shape keeps the new reject
sites uniform with the 1137-Reject and its already-blessed Disconnected handling.

## Decision 3 — ARM 2 ordering: `toApp` before `assign_outbound` (veto-no-consume)

The originate-path `toApp` block (`send_impl`, `:4149-4164`) fires `toApp` **before**
`assign_outbound` (`:4166-4167`) precisely so "a vetoed send does NOT consume a seqnum" (INV-5,
`:4147`). A veto returns `app_do_not_send` → drop, **session stays Active**; a throw →
`app_callback_threw` → terminal close.

**Decision**: at the BMR site, insert the same `toApp` block **before** `assign_outbound` (`:3256`).
Grounded against QuickFIX-cpp `Session::sendRaw` (`reference-engines/quickfix-cpp/src/C++/Session.cpp:580-599`):
`isAdminMsgType("j")` is false (`"0A12345"` only, `Message.h:295-300`) → the app branch wraps `toApp`
in `catch (DoNotSend&) { return false; }` (suppress before `persist`). Resolved in `/clarify`
(Session 2026-06-14): full `toApp` parity, veto honoured. **Alternative**: observe-only, no veto
(spec's original tentative default) — rejected by the reference-engine sweep; it would also require a
*new* observe-only `toApp` variant rather than reusing the existing path (more code, not less).

## Decision 4 — BMR veto safety: suppress the emit, but DON'T skip the inbound persist

The `35=j` is the engine's reaction to a `fromApp` veto (`session.cpp:3232-3267`). That whole branch
**falls through** to `persist_inbound_advance_()` at `:3279` — the **durable** persist of *this* in-seq
inbound message's seqnum advance (the in-memory advance already happened in `check_inbound`). On the
current (non-veto) path the rejected message is BMR-sent **and** inbound-persisted.

**Therefore the `toApp` veto must suppress only the BMR's `assign_outbound` + `store_then_emit` and
FALL THROUGH to `:3279` — it must NOT early-return.** An early `co_return` on the veto path would skip
the durable persist → durable < in-memory → the message is **reprocessed on restart** (under-persist
silent loss; the inverted twin of [[feedback_unconditional_persist_at_multiexit_gate_breaks_lowerbound]]).
This hazard is unique to the BMR arm — it is the only one of the 10 sites with a *non-fatal "suppress
but keep going"* outcome (the 9 admin sites are not vetoable; their only non-OK exit is a throw →
`Disconnected`, which has no downstream persist obligation). Only the BMR **throw** path early-returns
(terminal close → persist moot). Outbound side: a veto consumes **no** outbound seqnum (originate-path
INV-5; `toApp` before `assign_outbound`), confirmed against QFcpp — `incrNextTargetMsgSeqNum()` is the
*inbound* counter and runs *before* `sendRaw` (`Session.cpp:906`); DoNotSend returns false before the
outbound `persist`. **Implementation shape** (a local `suppressed` flag gating only the emit, never a
`co_return`) is pinned in [contracts/admin-emit-coverage.md](./contracts/admin-emit-coverage.md) C2.
**No new state, no new error.**

## Decision 5 — Witness design: per-site cells, not one mega-session

The 10 sites occupy mutually-exclusive FSM states/roles: Guard-3 (`:3368`) is initiator **LogonSent**;
the Q3 SendingTime reject (`:2400`) is **established/Active**; the 021 arms are **Active** with
specific malformed-122 inbound; the helper's two callers are fromAdmin-veto / no-`Application`; the
BMR is **Active** fromApp-reject. A single linear session cannot reach all 10.

**Decision**: the witness is a **suite of per-site cells**. Each cell (a) provokes exactly its emit
and (b) asserts the callback fired for that frame (`toAdmin` for admin; `toApp` for `35=j`). The
exact-count invariant — `toAdmin_calls == count(admin frames on the wire)`, with any `35=j` counted
on the `toApp` side and excluded from the admin count — applies to the **callback-reachable,
registered-`Application`** cells; the no-`Application` helper cell is **excluded** and tested as an
FR-006 byte-identity no-op, and the BMR veto cell is governed by INV-COV-5 (see `quickstart.md:54-58`
/ C3). A **throwing-callback variant** per site asserts `app_callback_threw` + terminal close. The BMR adds a
**veto cell**: `app_do_not_send` → `35=j` absent from the wire, session stays Active, **no outbound
seqnum consumed**. This is the faithful realization of the assessment's exact-count invariant
([[feedback_completeness_gate_exact_set_not_subset]] — assert the equality, not subset-presence;
[[feedback_witness_asserts_named_postcondition_not_proxy]] — assert the named postcondition directly).
**Alternative**: one end-to-end session emitting all frames — rejected (unreachable; would force
contrived state machinery and still miss the role-split sites).

## Decision 6 — No new surface

`app_callback_threw` already exists (`error.hpp`); `build_reject` / `build_logout` /
`build_business_message_reject` already exist; `fire_to_admin_` and the `send_impl` `toApp` block
already exist. The feature adds **call sites only** + inverts one stale comment (`:2096-2098`) +
doc rows. No wire field, error variant, config knob, codegen, or C-ABI change (FR-007).
