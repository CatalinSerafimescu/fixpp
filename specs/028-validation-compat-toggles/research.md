# Research: Validation-compat toggles — CheckCompID & ValidateSequenceNumbers (028)

Phase 0 output. Resolves every Technical-Context unknown. Grounded against the
source tree (`src/session/session.cpp`, `include/fixpp/session/session_config.hpp`)
and the reference engines (QuickFIX-cpp v1.16.0, QuickFIX/J 3.0.1) cloned at
`<parent>/reference-engines/`.

## D-0 — Reference-engine sweep (authority for the relaxation semantics)

- **QFcpp** `Session::isCorrectCompID` (`Session.h:257`): `if (!m_checkCompId) return true;` — when off the per-message `49`/`56` equality match is skipped; the header fields are still parsed. QFcpp has **no** `ValidateSequenceNumbers` knob (`isTargetTooHigh`/`isTargetTooLow` are always evaluated in `verify`).
- **QFJ** has both, default `Y` (`DefaultSessionFactory.java:211` CheckCompID default true; `:226` ValidateSequenceNumbers default true). In `Session.verify(msg, checkTooHigh, checkTooLow)` (`Session.java:1800`): the `MsgSeqNum` is read only `if (checkTooHigh || checkTooLow)` (`:1812`); the too-high/too-low blocks are guarded by those flags (`:1832`/`:1835`); `isCorrectCompID(msg)` is unconditional inside `verify` (`:1826`) — so QFJ relaxes CompID at Logon too (`verify` is called for Logon at `:2208`). The inbound counter advances via the uniform rule `if (getExpectedTargetNum() == msgSeqNum) state.incrNextTargetMsgSeqNum();` repeated across every handler (`:1319`, `:1384`, `:1481`, …) — **out-of-order frames are delivered but do NOT advance the counter**. The PossDup check `if (isPossibleDuplicate(msg) && !validatePossDup(msg)) return false;` (`:1843`) runs **regardless** of the seqnum flags.

**Conclusion**: the QuickFIX semantic for both knobs is "skip the check, deliver the message"; the QFJ counter rule is "advance only on exact match". fixpp adopts both behaviours, with two **deliberate divergences from QFJ** decided at clarify (D-4).

## D-1 — Config surface: two independent additive bools, default `true` (strict)

**Decision**: add two `SessionConfig` fields, `bool check_comp_id = true;` and
`bool validate_sequence_numbers = true;`. Default `true` = strict = byte-identical
no-op. Note the **polarity inverts** versus the false-default additive knobs
(021/022/024/026/027): here strict IS the current default, so the bool defaults
`true` and `false` is the opt-in relaxation. Both are EXPLICIT per-field defaults
([const §XII.5], no-implicit-default).

**Rationale**: matches the established additive-knob idiom (024 reset knobs); the
fields are primitive `bool` ⇒ **no new include** in `session_config.hpp`, so
[const §XV.9] (no `std::mutex` in awaitable headers) is N/A — same as 027.
`static_assert(is_copy_constructible_v<SessionConfig>)` is preserved (bool is copyable).

**Alternatives considered**: (a) a single enum/bitmask knob — rejected: the two
relaxations are orthogonal (FR-007 independence) and the QuickFIX keys are
separate. (b) defaulting `false` to match the other knobs' polarity — rejected:
that would flip current behaviour and break every existing session (violates
FR-009 byte-identity).

## D-2 — CheckCompID relaxation site: the steady-state CompID/BeginString gate

**Decision**: relax only the two CompID clauses of the Active/LogonReceived
CompID/BeginString gate at `session.cpp:1869-1874`:

```cpp
if (hdr.begin_string != cfg_.begin_string ||
    hdr.sender_comp_id != cfg_.target_comp_id ||
    hdr.target_comp_id != cfg_.sender_comp_id) { …Disconnected… }
```

becomes (semantically): keep `begin_string` always; gate the two CompID clauses
behind `cfg_.check_comp_id`. When `check_comp_id == false`, a `49`/`56` mismatch
no longer disconnects; `begin_string` mismatch still does.

**Rationale**: this is the single steady-state CompID match site (the `case
fsm_state::LogonReceived: case fsm_state::Active:` fall-through handler). It is
distinct from (a) the Logon-establishment CompID check inside `interpret_logon`
(NotConnected `:1530`, LogonSent `:2705`), which stays strict per D-4, and (b) the
013 CompID **authorization** allow-list (`compid_authorization_policy.authorize`
at the Logon path `:1678`), which is a separate security control the knob MUST NOT
touch (FR-003 / clarify Q1).

**Alternatives considered**: relaxing inside `interpret_logon` too (QFJ parity) —
rejected by clarify Q3 (steady-state only, D-4).

## D-3 — ValidateSequenceNumbers relaxation sites: two arm-guards + two SequenceReset(35=4) intercept gates + one deliver-without-advance branch

The steady-state seqnum gate in the Active/LogonReceived handler runs:
1. **reset-mode `SequenceReset(35=4)` intercept** (`:1966`, BEFORE the gate): `co_return apply_inbound_sequence_reset(NewSeqNo, MsgSeqNum)` — jumps the inbound counter via `set_next_inbound(NewSeqNo)` regardless of the frame's own MsgSeqNum.
2. parse `seq`; `seq==0` → fatal (`:2017`).
3. **too-high arm** (`:2037`): `seq > next_expected && !is_awaiting_resend()` → enter AwaitingResend + emit `ResendRequest`, return.
4. **PossDup Stage-1** (`:2087`): `43=Y` non-`35=4` → validate `OrigSendingTime(122)`.
5. **`check_inbound(seq)`** (`:2232`): success (exact-expected) advances the counter; failure (too-low) → Heartbeat-ignore / PossDup Stage-2 disposition (`:2243`) / **Arm B fatal disconnect** (`:2273`).
6. **gapfill-mode `SequenceReset(35=4, 123=Y)` intercept** (`:2294`, AFTER the gate): applies `NewSeqNo` via the gap-fill handler.

**Decision** — when `validate_sequence_numbers == false`, the relaxation touches **five sites** (S2/S4/S6/S7 below, with S5's exact-match advance reused unchanged):
- **(S2) Guard the too-high arm** (`:2037`) with `&& cfg_.validate_sequence_numbers`. Off ⇒ no `ResendRequest`, no AwaitingResend; the too-high frame falls through to the post-gate processing.
- **(S3) Keep PossDup Stage-1 + Stage-2 unchanged** (clarify Q4) — only the gap checks are skipped, not the duplicate handling.
- **(S4) Replace the Arm B fatal** (`:2273-2276`): when `validate_sequence_numbers == false`, a non-Heartbeat, non-PossDup too-low (or fallen-through too-high) frame is **delivered without advancing** the counter (reuse the `parse_and_dispatch_` → `fromApp`/`fromAdmin` pattern used by Arm A row 8 at `:2256`), staying Active — instead of `record_state_transition_(Disconnected)`. (A too-low `Heartbeat(35=0)` is still silently dropped at `:2234` before this else — pre-existing carve-out.)
- **(S6) Gate the reset-mode `SequenceReset(35=4)` intercept** (`:1966`, BEFORE the gate): when `validate_sequence_numbers == false`, BYPASS the intercept so `apply_inbound_sequence_reset` is NOT called — the `35=4` is delivered to `fromAdmin` without applying `NewSeqNo` / jumping the counter (FR-013 / I-VCT-11).
- **(S7) Gate the gapfill-mode `SequenceReset(35=4, 123=Y)` intercept** (`:2294`, AFTER the gate): same knob gate — `NewSeqNo` is NOT applied when off (FR-013 / I-VCT-11). The shared `apply_inbound_sequence_reset` itself is UNCHANGED in both cases; only whether it is reached is gated.
- **(S5) Counter advance** remains via `check_inbound` success only (exact-expected match) — this realises the QFJ "advance only on exact match" rule (D-0). Crucially, exact-match advance holds for **ALL inbound paths only because S6/S7 are ALSO gated** (I-VCT-11): the `SequenceReset(35=4)` family is the one inbound path that advances the counter *outside* the seqnum gate, so without the S6/S7 bypass a relaxed-mode `35=4` would still jump the counter via `apply_inbound_sequence_reset` and break the exact-match invariant. Out-of-order ordinary frames delivered above do NOT advance.

**Rationale**: surgical — two `&& cfg_.validate_sequence_numbers` arm-guards (S2,
too-high; the S4 Arm-B fatal-vs-deliver decision) plus two knob gates on the
`SequenceReset(35=4)` intercepts (S6 reset-mode, S7 gapfill-mode) plus one new
deliver-without-advance else-branch; the default-true path takes the existing code
verbatim (byte-identical, D-6). The counter mechanic falls out of the existing
`check_inbound` contract, so no SeqnumManager API change. PossDup retention (Q4) is
free — those arms are untouched.

**Alternatives considered**: (a) follow-the-peer counter (`next = seq+1`) or
unconditional `+1` — rejected by clarify Q2 (QFJ-parity exact-match). (b) routing
out-of-order frames through `check_inbound` with a relaxed return — rejected:
`check_inbound` is shared with the strict path and the Logon handlers; changing its
contract would ripple into 013/024/027. Keeping every edit in the steady-state
Active handler isolates the change: the change is confined to the steady-state
handler and does NOT modify the shared `apply_inbound_sequence_reset` — but note it
spans more than the seqnum-gate region proper, since the S6 reset-mode intercept at
`:1966` sits *before* the seqnum gate and the S7 gapfill intercept at `:2294` sits
*after* it; both are gated in place without touching the shared reset routine.

## D-4 — Scope: steady-state only (deliberate QFJ divergence)

**Decision**: both relaxations apply ONLY in the `case fsm_state::LogonReceived:
case fsm_state::Active:` handler (post-Logon traffic). The Logon-establishment
paths — `NotConnected` (`:1524`, `interpret_logon` + first-Logon seqnum) and
`LogonSent` (`:2688`, the initiator's Logon-ack handling incl. `check_inbound`
`:2775` and the 013/024 `ResetSeqNumFlag(141)` reset interaction) — are
**unchanged**.

**Rationale** (clarify Q3): QFJ evaluates both checks inside `verify`, which it
also calls for Logon — so QFJ relaxes at Logon. fixpp deliberately diverges: a
relaxed Logon CompID/seqnum would let an acceptor admit a wrong-CompID or
wrong-seqnum peer at establishment, and would entangle the knobs with the 013/024
reset FSM. Restricting to steady-state keeps establishment safe and leaves the
Logon FSM + 013/024 reset path entirely untouched — a materially smaller,
lower-risk change. Documented as a limitation (L-028-x).

## D-5 — Knob independence + outbound untouched

**Decision**: the two guards are independent `if (cfg_.check_comp_id)` /
`if (cfg_.validate_sequence_numbers)` checks; all four combinations are valid
(FR-007). Neither knob touches any outbound construction — our own `49`/`56` and
our outgoing seqnums are produced by `build_*` / `assign_outbound` paths that read
neither field (FR-008).

## D-6 — Default-true ⇒ byte-identical no-op

**Decision**: across the five edit sites (S1 CompID gate + S2 too-high arm-guard +
S4 Arm-B relaxed dispatch + S6/S7 `SequenceReset(35=4)` intercept gates) the changes
are `if (cfg_.<knob>) { <existing strict branch> }` wrappers (CompID) /
`&& cfg_.<knob>` arm-guards + intercept knob-gates (seqnum) plus an else reached only
when the knob is `false`. With both at default `true`, control flow is the existing
code path verbatim — no new allocation, no new suspension, no wire delta
(FR-009 / SC-003). Witnessed by a byte-identity + full-regression test.

## D-7 — Catalogue / B&L delta (net-new rows; FR-011)

**Decision** (applied at Polish):
- `spec/feature-catalogue.md`: **two net-new rows** — **`S-040`** (`CheckCompID — skip steady-state SenderCompID/TargetCompID match`) and **`S-041`** (`ValidateSequenceNumbers — accept out-of-order inbound without gap-fill recovery`). Both status `done` (FIX 4.4), evidence_pr `(pending merge)`, Tests `tests/session/test_validation_compat_toggles.cpp` + interop cell. Normative refs = QuickFIX `CheckCompID` / `ValidateSequenceNumbers` + `[FIX-SL §4.2.2]` (Identification of FIX session peers / CompID) / `[FIX-SL §4.8]` (Message recovery the knob suppresses — `§4.8.2` ResendRequest, `§4.8.5` Gap fill, `§4.8.6` Sequence reset).
- `spec/coverage-index.md`: add the two entries (exact-set diff at Polish — [[feedback_completeness_gate_exact_set_not_subset]]).
- `spec/behaviors-and-limitations.md`: **B-028-1** (steady-state CompID match skip when `check_comp_id=false`; Logon-time match + 013 authz + BeginString still enforced). **B-028-2** (steady-state out-of-order tolerance when `validate_sequence_numbers=false`: no ResendRequest on a gap, no fatal on too-low; counter advances on exact match only; PossDup handling retained). **L-028-1** (`validate_sequence_numbers=false` disables gap detection and recovery — a real gap is silently accepted and messages may be processed out of order; operator opt-in for compat counterparties only). **L-028-2** (`check_comp_id=false` removes the steady-state mis-routing guard — a message addressed to a different CompID pair is accepted; rely on the 013 authz allow-list + transport binding for security). **L-028-3** (both relaxations are steady-state only — Logon establishment is unaffected; a counterparty needing relaxed Logon-time checks is unsupported, diverging from QFJ).

## D-8 — Interop cell

**Decision**: a live both-role ctest cell (skip-without-counterparty) configuring
fixpp with each knob relaxed against a QFJ/QFcpp counterparty driving mismatching
CompIDs / out-of-order sequence numbers, asserting fixpp accepts (no
reject/disconnect, no ResendRequest). Grounds the parity claim against the live
reference engines ([const §VII.6]).
