# Data Model: Validation-compat toggles (028)

Phase 1 output. No new types — two additive `SessionConfig` bool fields plus the
behavioural invariants that govern the two relaxed validation predicates. No wire
field, no error slot, no store/SeqnumManager API change.

## Entities

### Config fields (additive, `SessionConfig`)

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `check_comp_id` | `bool` | `true` | When `true` (strict, current behaviour) the steady-state CompID gate enforces peer `49 == target_comp_id` AND peer `56 == sender_comp_id`. When `false` that equality match is skipped for post-Logon traffic (QuickFIX `CheckCompID=N`). |
| `validate_sequence_numbers` | `bool` | `true` | When `true` (strict, current behaviour) the steady-state seqnum gate runs the full validation (too-high → ResendRequest; too-low → fatal/PossDup). When `false` out-of-order frames are accepted without the gap dance (QuickFIX `ValidateSequenceNumbers=N`). |

Both are EXPLICIT per-field defaults ([const §XII.5]). Primitive `bool` ⇒ no new
include in `session_config.hpp` ([const §XV.9] N/A). `SessionConfig` stays
copy-constructible (010 W-5 static_assert preserved).

## Touched sites (all in `src/session/session.cpp`, `LogonReceived/Active` handler)

| # | Site | Strict (default) | Relaxed |
|---|------|------------------|---------|
| S1 | CompID/BeginString gate `:1869-1874` | `begin_string` + `49`/`56` mismatch → Disconnected | `begin_string` mismatch → Disconnected (unchanged); `49`/`56` mismatch tolerated when `!check_comp_id` |
| S2 | too-high arm `:2037` | `seq>next_expected` → AwaitingResend + ResendRequest, return | guarded `&& validate_sequence_numbers`; off ⇒ skip (fall through, no ResendRequest) |
| S3 | PossDup Stage-1 `:2087` / Stage-2 `:2243` | validate `122`; admin-ignore / app-redeliver disposition | **unchanged** (clarify Q4 — PossDup handling retained) |
| S4 | `check_inbound` failure Arm B `:2273-2276` | non-Heartbeat non-PossDup too-low → Disconnected | when `!validate_sequence_numbers`: deliver-without-advance — **discriminate via the existing `is_admin_msgtype` helper** (`src/session/msgtype_classifier.hpp:43`, as the in-seq path does): admin msgtype → `fromAdmin`, app msgtype → `fromApp`; reuse `parse_and_dispatch_`; no advance, stay Active. **Carve-out (pre-existing, unchanged):** a too-low `Heartbeat(35=0)` is silently dropped at `session.cpp:2234` BEFORE reaching this else, so it is NOT delivered even with the knob off (exception to "the frame is delivered" — N3). |
| S5 | `check_inbound` success `:2232` | exact-expected → advance + dispatch | **unchanged** — counter advances on exact match only (QFJ-parity) |
| S6 | reset-mode `SequenceReset(35=4)` intercept `:1966` (BEFORE the gate) | `co_return apply_inbound_sequence_reset(NewSeqNo, MsgSeqNum)` — `set_next_inbound(NewSeqNo)` regardless of the frame's own MsgSeqNum | when `!validate_sequence_numbers`: BYPASS the intercept so `apply_inbound_sequence_reset` is NOT called — deliver the frame to `fromAdmin` without advancing/jumping the counter (deliver-without-advance, FR-013). The shared `apply_inbound_sequence_reset` is unchanged; only whether it is CALLED is gated. |
| S7 | gapfill-mode `SequenceReset(35=4, 123=Y)` intercept `:2294` (AFTER the gate) | applies `NewSeqNo` via the gap-fill handler | when `!validate_sequence_numbers`: BYPASS so `NewSeqNo` is NOT applied — an out-of-order gapfill `35=4` is delivered-without-advance via `fromAdmin` (FR-013). (Note: with S2 guarded off, a too-high gapfill `35=4` already falls to S4 deliver-without-advance and never reaches `:2294`; an exact-match gapfill `35=4` keeps the apply path only at strict default.) |

**Untouched (steady-state-only scope, D-4):** `NotConnected` Logon establishment
(`:1524`, `interpret_logon` CompID + first-Logon seqnum), `LogonSent` initiator
Logon-ack (`:2688`, `check_inbound` `:2775`, 013/024 `141` reset), the 013 CompID
authorization allow-list (`:1678`).

## Invariants

- **I-VCT-1** (CompID match-only): `check_comp_id=false` skips ONLY the `49`/`56` equality compare at S1; `BeginString(8)` is always enforced; the header fields are still parsed (not made optional). [FR-003; D-2]
- **I-VCT-2** (authz independence): `check_comp_id=false` does NOT bypass the 013 `compid_authorization_policy` allow-list (a different site on the Logon path). A non-allow-listed principal is still refused. [FR-003; SC-004; clarify Q1]
- **I-VCT-3** (gap suppression): `validate_sequence_numbers=false` suppresses the too-high ResendRequest/AwaitingResend (S2) and the too-low fatal (S4). [FR-006]
- **I-VCT-4** (exact-match advance): with validation off, for ordinary (non-`35=4`) inbound frames the counter advances ONLY when `seq == next_inbound_unsafe()` (via `check_inbound` success, S5); an out-of-order frame is delivered but does NOT advance the counter. The inbound `SequenceReset(35=4)` admin paths (S6/S7) — which advance the counter via `apply_inbound_sequence_reset` outside the seqnum gate at strict default — are gated by I-VCT-11 so they ALSO do not advance with the knob off, making exact-match-advance true for ALL inbound paths. [FR-006; clarify Q2; QFJ-parity D-0; see I-VCT-11]
- **I-VCT-11** (knob-off `SequenceReset(35=4)` does not apply `NewSeqNo`): with validation off, an inbound `SequenceReset(35=4)` in BOTH reset-mode (S6, `:1966`) and gapfill-mode (S7, `:2294`) does NOT call `apply_inbound_sequence_reset`; its `NewSeqNo(36)` is NOT applied and the counter is unchanged; the frame is delivered to `fromAdmin` (deliver-without-advance). The shared `apply_inbound_sequence_reset` is UNCHANGED (still used verbatim by the strict 013/024/027 paths) — only whether it is invoked is gated on the knob. This holds **regardless of the `35=4` frame's own MsgSeqNum — including an exact-match `35=4`**: QFJ gates `NewSeqNo` application purely on the flag (`Session.java:1550`), not on a seqnum match, so under knob-off even an in-sequence `35=4` does NOT apply `NewSeqNo` (it is delivered to `fromAdmin` as a normal in-sequence admin frame and the counter advances by one via the ordinary exact-match path; `NewSeqNo` is still not applied). At strict default the `35=4` applies `NewSeqNo` as today. [FR-013; SC-008; clarify Gate A round 1; QFJ Session.java:1543/1550]
- **I-VCT-5** (PossDup retained): with validation off, PossDup(43)-flagged frames still flow through Stage-1/Stage-2 (S3); only the gap checks are skipped. [FR-006; clarify Q4]
- **I-VCT-6** (steady-state only): both relaxations apply ONLY in `LogonReceived/Active`; Logon establishment (CompID + seqnum + 013/024 reset) is byte-identical regardless of either knob. [FR-012; clarify Q3; D-4]
- **I-VCT-7** (byte-identical default): both at default `true` ⇒ S1/S2/S4 take the existing strict branch verbatim; no wire delta, no new alloc/suspension. [FR-009; SC-003; D-6]
- **I-VCT-8** (independence): the two `if (cfg_.<knob>)` guards are independent; all four combinations behave per their axis. [FR-007]
- **I-VCT-9** (inbound-only): neither knob is read on any outbound construction path; our own CompIDs/seqnums are unchanged. [FR-008]
- **I-VCT-10** (parse-fatal preserved): `seq==0` (unparseable MsgSeqNum, `:2017`) stays fatal even with `validate_sequence_numbers=false` — a frame with no parseable sequence number is malformed, not merely out-of-order. [D-3]

## State transitions

No FSM state change. The relaxations alter only the disposition WITHIN the
`LogonReceived/Active` handler: paths that previously transitioned to
`Disconnected` (S1 CompID mismatch, S4 too-low) instead stay in the current state
and deliver/ignore the frame when the corresponding knob is `false`. The too-high
arm (S2) previously entered AwaitingResend (tracked by `reconnect_fsm_`); with
validation off it never does, so AwaitingResend is never entered via this path. The
`SequenceReset(35=4)` intercepts (S6/S7) previously jumped the inbound counter via
`apply_inbound_sequence_reset` (no FSM state change, only a counter jump); with
validation off they are bypassed so the counter is unchanged (I-VCT-11). No site
modifies the shared `apply_inbound_sequence_reset` — only whether it is reached.
