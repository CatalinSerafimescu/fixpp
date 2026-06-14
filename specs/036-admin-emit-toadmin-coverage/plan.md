# Implementation Plan: toAdmin/toApp observation coverage for engine-originated Reject and Logout emits

**Branch**: `036-admin-emit-toadmin-coverage` | **Date**: 2026-06-14 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/036-admin-emit-toadmin-coverage/spec.md`

## Summary

Close the FR-008 observation gap: 10 of 26 engine-originated admin-builder emit sites in
`src/session/session.cpp` bypass the `Application` observation callback. Wire the **9 administrative
sites** (the `Reject(35=3)` family + the initiator Guard-3 `Logout`) through the existing
`fire_to_admin_` helper, and route the **1 application site** (`BusinessMessageReject(35=j)`) through
the existing `send_impl`-style `toApp` path — `35=j` is an application message, so `toAdmin` would be
wrong. The fix is exhaustive in one pass (enumerated from assessment 2.4 §2; the half-pass lesson —
012 RC#B — costs extra Gate B rounds), behind an exact-count `toAdmin_calls == admin-frames-on-wire`
witness that fails today and pins the coverage forever.

This is a **defect / parity fix** realizing the *already-stated* FR-008 contract at full coverage; it
amends FR-008's scope wording but introduces **no new wire field, error slot, config knob, codegen,
or C-ABI surface**. A throwing callback reuses the existing `app_callback_threw` error. When no
`Application` is registered, every site is a byte-for-byte no-op.

### The two routing arms (and why their ordering differs)

- **ARM 1 — 9 administrative sites → `fire_to_admin_`**, inserted **after `assign_outbound`,
  before `store_then_emit`** — byte-identical to the canonical wired pattern (the 033 1137-Reject,
  `session.cpp:2113-2122`). `toAdmin` is **inspect-only / not vetoable**; `fire_to_admin_` returns
  false **only** if the callback threw, and the caller records `Disconnected` + returns
  `app_callback_threw`. Because a throw always disconnects, the relative order of `assign_outbound`
  vs `fire_to_admin_` is immaterial for seqnum accounting — so we match the existing sites exactly
  (surgical consistency).
- **ARM 2 — 1 application site (`35=j`) → `toApp`**, inserted **before `assign_outbound`**. This reuses
  the originate-path `toApp` *callback + veto/throw outcome* (`send_impl`, `session.cpp:4149-4164`) but
  **NOT its control flow**: `send_impl` early-returns on a veto because it has no downstream work; the
  BMR site does — its `fromApp`-reject branch falls through to the inbound-seqnum durable persist
  `persist_inbound_advance_()` at `:3279`. `toApp` is **vetoable**: an `app_do_not_send` drops the
  frame (no store, no emit, **no outbound seqnum consumed** — originate-path INV-5, so `toApp` fires
  *before* `assign_outbound`, exactly QuickFIX-cpp `sendRaw` DoNotSend → `return false` before
  `persist`) **but the session MUST still fall through to `:3279`** and persist the inbound advance —
  a `co_return` on the veto path would leave durable < in-memory and reprocess the message on restart
  (under-persist silent loss, inverted [[feedback_unconditional_persist_at_multiexit_gate_breaks_lowerbound]]).
  Only the **throw** path early-returns (`app_callback_threw` + terminal close — persist is moot under
  terminal close). The inbound seqnum was advanced in-memory by `check_inbound` before the reject is
  built, so the *durable* persist at `:3279` is the part the veto must not skip.

### Both callbacks are already noexcept-safe at the boundary

`fire_to_admin_` and the `send_impl` `toApp` block both invoke the user callback **inside**
`parse_and_dispatch_`, which converts a throw into `app_callback_threw` *within* the noexcept body —
so reusing them carries no terminate risk ([[feedback_noexcept_boundary_user_callback_terminate]] is
already discharged by the existing helpers; we add no new bare call site).

## Technical Context

**Language/Version**: C++23 (Clang 22 local == CI per `[const Art.II §2]`)
**Primary Dependencies**: existing `Session::fire_to_admin_` (`session.cpp:331`), `parse_and_dispatch_`, the `send_impl` `toApp` block (`session.cpp:4149-4164`), the `Application` pluggable interface (`[const Art.XIV]`)
**Storage**: none — observation-only; `store_then_emit` ordering unchanged (callback fires before store/emit, as on the 16 wired sites)
**Testing**: GoogleTest; a counting/throwing `Application` test double; per-site cells asserting the exact-count invariant + throw-handling; ASan/TSan/UBSan over the session suite as regression
**Target Platform**: Linux (Tier-1); role-symmetric (initiator + acceptor sites both covered)
**Project Type**: single library (`fixpp`)
**Performance Goals**: no change — `fire_to_admin_` already runs on the 16 wired sites; the 10 new sites add one bounded stack-arena parse per emit (no heap; `[const §XV.5]`), only when an `Application` is registered
**Constraints**: no public signature / error variant / config / wire / codegen / C-ABI change (FR-007); no-`Application` path byte-identical (FR-006); admin arm stays not-vetoable (FR-003); BMR veto reuses originate-path semantics without consuming a seqnum (FR-004)
**Scale/Scope**: in-place edits to `session.cpp` — 1 helper (covers 2 caller flows) + 7 inline Reject sites + 1 Guard-3 Logout (ARM 1) + 1 BMR site (ARM 2) + invert one stale comment; 1 new test file; B&L + catalogue rows. Est. ~60-90 LoC production, observation-only.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Article | Relevance | Disposition |
|---|---|---|
| **XIV — Pluggable Interfaces** (`Application` is the host callback interface) | Core of the change | The feature completes the `Application::toAdmin` / `toApp` observation contract to full coverage of engine-originated frames. No interface signature change — only more call sites invoke the existing callbacks. PASS. |
| **XIII — Observability & Logging** | Observation surface | Restores the host's ability to observe every engine-originated admin frame (the FR-008 contract). No trace-context / `thread_local` involvement; callbacks fire on the session strand (`[const §XI.4]`). PASS. |
| **XI §4 — callbacks on the session strand** | Where the callback runs | `fire_to_admin_` / `toApp` are invoked on the engine executor under single-thread confinement (015 E-5), identical to the 16 wired sites — see `session.cpp:327-328`. No new threading. PASS. |
| **XV §5 — zero `new`/`delete` between parse and the app callback** | The callback parse arena | `fire_to_admin_` uses the stack-local `kAdminParseArena` (8 KiB, `session.cpp:333-334`); the BMR `toApp` reuses the `send_impl` stack arena. No heap on any new path. PASS. |
| **X — ABI Policy** | Public surface | **No** new public type/signature; **no** new error variant (a throwing callback reuses `app_callback_threw`). PASS. |
| **VII — Testing (≥ seams)** | New behavior | New seam: a counting + throwing `Application` double; per-site exact-count + throw-handling cells. The exact-count invariant is the durable regression ([[feedback_half_restructure_symmetric_api]] §4, [[feedback_completeness_gate_exact_set_not_subset]]). |
| **IX §1 — coverage / sanitizers** | New branches | The 10 new `fire_to_admin_`/`toApp` invocations + their throw arms reach 100% DA/BRDA; full ASan/TSan/UBSan over the session suite. |
| **VI — 100% FIX rule / catalogue** | No new FIX message/field | No new message type or field; `Reject(35=3)`, `Logout(35=5)`, `BusinessMessageReject(35=j)` all already exist. Catalogue gets a traceability row only; B&L gains a coverage row (FR-008). |
| **XVII — Codex Review Gates** | Gate A/B | Gate A after this plan (before `/tasks`); Gate B before merge. |
| **Dependencies / Version Management** | None added | No new third-party dependency. |

**Surface delta**: no wire field, no `SessionConfig`/`EngineConfig` field, no codegen, no C-ABI, no new error variant, no new public signature. The change is **observation-only** call-site wiring plus one inverted in-source comment and doc rows. Gate-clean.

## Project Structure

### Documentation (this feature)

```text
specs/036-admin-emit-toadmin-coverage/
├── plan.md              # This file
├── research.md          # Phase 0 — the emit-site inventory + the two routing-arm ordering decisions + witness design
├── data-model.md        # Phase 1 — the admin-emit coverage matrix (site → msgtype → callback → ordering)
├── quickstart.md        # Phase 1 — the counting/throwing Application witness recipe (per-site cells + exact-count)
├── contracts/
│   └── admin-emit-coverage.md   # internal contract: which engine emits fire toAdmin vs toApp; the exact-count invariant; throw/veto semantics
├── checklists/
│   └── requirements.md  # spec-quality checklist (done; clarification resolved)
└── tasks.md             # /speckit-tasks output (NOT created here)
```

### Source Code (repository root)

```text
src/session/
└── session.cpp          # ARM 1 (9 admin → fire_to_admin_ after assign_outbound, before store_then_emit):
                         #   - emit_session_reject_ helper  :1728  (build_reject :1736; insert between assign_outbound :1742 and store_then_emit :1747) — covers callers :3026 + :3215
                         #   - :2400  build_reject  established Q3 SendingTime-accuracy Reject (paired Logout :2421 already fires)
                         #   - :2484  build_reject  fromAdmin-veto on inbound SequenceReset
                         #   - :2599  build_reject  021 Arm C malformed-122
                         #   - :2644  build_reject  021 RC#1 malformed-122
                         #   - :2675  build_reject  021 Arm D (paired Logout :2696 already fires)
                         #   - :2946  build_reject  fromAdmin-veto on inbound Logout
                         #   - :4576  build_reject  SequenceReset NewSeqNo-too-low
                         #   - :3368  build_logout  initiator LogonSent Guard-3 (Logon-ack SendingTime failure)
                         # ARM 2 (1 app → toApp before assign_outbound; veto-aware):
                         #   - :3249  build_business_message_reject  35=j (toApp veto → drop+stay-Active+no-seqnum-consume; throw → app_callback_threw+terminal close)
                         # COMMENT INVERSION: :2096-2098 — the 033 warning "do NOT route through the
                         #   fire_to_admin_-less emit_session_reject_ helper" is now stale (the helper
                         #   fires toAdmin after this feature) → rewrite to reflect that the helper is now covered

tests/session/
└── test_admin_emit_toadmin_coverage.cpp   # NEW — counting + throwing Application double:
                         #   per-site cell: provoke the emit, assert toAdmin (or toApp for 35=j) fired for that frame
                         #     AND exact-count toAdmin_calls == admin-frames-on-wire within the cell (35=j on the toApp side);
                         #   throwing variant per site: app_callback_threw + terminal close;
                         #   BMR-specific: toApp veto (app_do_not_send) → 35=j NOT on wire, session stays Active, no outbound seqnum consumed

spec/
├── behaviors-and-limitations.md          # FR-008: new row scoping WHICH engine emits fire toAdmin vs toApp (full coverage post-036)
└── feature-catalogue.md / coverage-index.md   # traceability row for 036

specs/019-app-callbacks/   (or the FR-008 anchor feature)
└── spec.md / tasks.md      # dated note: FR-008 coverage extended to the full Reject family + Guard-3 Logout + BMR-via-toApp (no history rewrite)
```

**Structure Decision**: Single-library, in-place. Every change is internal to `session.cpp`'s emit
sites plus tests and docs; no new module, no header, no header-graph change (`tools/check_layers.py`
unaffected). Both reused helpers (`fire_to_admin_`, the `send_impl` `toApp` block) already exist —
this feature only adds call sites, so it exports no new surface (Art. X).

## Phase 0 — Research

See [research.md](./research.md). Key decisions:

1. **Site inventory re-verified at current HEAD (post-035).** The assessment was at post-033; line numbers shifted ~+16. The 10 sites are re-confirmed against `session.cpp` HEAD (table in research.md). No site disappeared or merged.
2. **ARM 1 ordering = match the wired pattern** (`fire_to_admin_` after `assign_outbound`, before `store_then_emit`). Admin is not vetoable; a throw disconnects regardless, so this is the surgical, consistent choice.
3. **ARM 2 ordering = `toApp` before `assign_outbound`** to preserve the originate-path "veto does not consume a seqnum" invariant (INV-5). Grounded against QuickFIX-cpp `sendRaw` (DoNotSend → return false before persist; inbound seqnum advanced before the reject build → no desync). Resolved in `/clarify` (Session 2026-06-14): full `toApp` parity, veto honoured.
4. **BMR veto safety.** The `35=j` is the engine's reaction to a `fromApp` veto (`session.cpp:3232-3267`); the inbound message's seqnum is advanced by `check_inbound`/`persist_inbound_advance_` independently of the outbound BMR, so suppressing the BMR via `toApp` veto leaves the session consistent — confirmed against the QFcpp `incrNextTargetMsgSeqNum()`-before-`sendRaw` ordering.
5. **Witness design = per-site cells, not one mega-session.** The 10 sites span mutually-exclusive FSM states/roles (Guard-3 is initiator LogonSent; the rejects are Active/NotConnected/various), so a single linear session cannot reach all 10. The witness is a suite where **each cell** asserts the exact-count equality for the frames it provokes (collectively covering all 10), plus a throwing-callback variant per site. This is the faithful form of the assessment's exact-count invariant.
6. **No new surface confirmed** — `app_callback_threw` already exists; `Reject`/`Logout`/`BusinessMessageReject` builders already exist; no config/wire/codegen/C-ABI change.

## Phase 1 — Design & Contracts

- [data-model.md](./data-model.md) — the admin-emit **coverage matrix**: every engine-originated admin-builder emit site (all 26) × its message type × its callback (`toAdmin` / `toApp` / none-by-design) × ordering vs `assign_outbound` × wired-before-036 / wired-by-036; the invariant that the matrix has **zero** `none` rows for engine-originated admin frames after 036.
- [contracts/admin-emit-coverage.md](./contracts/admin-emit-coverage.md) — the observation contract: (a) every engine-originated administrative frame fires `toAdmin` before store/emit (inspect-only, throw → `app_callback_threw` + Disconnected); (b) `35=j` fires `toApp` before `assign_outbound` (veto → drop+stay-Active+no-seqnum-consume, throw → terminal close); (c) the exact-count invariant `toAdmin_calls == admin-frames-on-wire` (BMR excluded, counted on `toApp`).
- [quickstart.md](./quickstart.md) — the counting + throwing `Application` test-double recipe; per-site provocation cells; the exact-count assertion; the BMR-veto cell.

## Complexity Tracking

No constitution violations to justify. The feature *reuses* two existing, already-blessed helpers
(`fire_to_admin_`, the `send_impl` `toApp` block) and only adds call sites; it removes an
observation gap rather than adding machinery. The single non-mechanical choice — BMR `toApp` *before*
`assign_outbound` (vs the admin arm's *after*) — is forced by the originate-path veto-no-consume
invariant, not added speculatively, and is documented in research.md decision 3.

## Gate A

| Round | Outcome |
|---|---|
| _pending_ | Gate A runs after this plan, before `/speckit-tasks`. |
