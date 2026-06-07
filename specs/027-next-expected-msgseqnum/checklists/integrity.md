# Sequence-Integrity-&-Security Checklist: NextExpectedMsgSeqNum(789) fast session resume

**Purpose**: Requirements-quality gate ("unit tests for English") for the sequence-integrity error dispositions, the amplification guard, and the off-by-one / counter-disambiguation requirements. Tests whether the *requirements* are complete, clear, consistent, and measurable — NOT whether the implementation works.
**Created**: 2026-06-07
**Feature**: [spec.md](../spec.md) · [data-model.md](../data-model.md) · [contracts/next-expected-msgseqnum.md](../contracts/next-expected-msgseqnum.md)

## Requirement Completeness

- [x] CHK001 Is the X>N disposition fully specified (send a `Logout` with explanatory text, THEN disconnect, no silent accept)? [Completeness, Spec §FR-005, data-model I-NEX-4] — PASS: FR-005 states "the session MUST send a `Logout` carrying explanatory text (e.g. 'NextExpectedMsgSeqNum too high, expecting N but received X') and then disconnect; it MUST NOT silently treat the session as in sync"; data-model I-NEX-4 repeats this; contracts C6 names it; research D-6 confirms QFcpp/QFJ parity. Fully specified with the "MUST NOT silent accept" constraint and example text.
- [x] CHK002 Is a present-but-invalid 789 (empty / non-digit / overflow) given an explicit disposition, distinct from "field absent"? [Completeness, Spec §Edge Cases "Invalid inbound 789", data-model I-NEX-9] — PASS: spec Edge Cases "Invalid inbound 789 (present-but-unparseable)" is a named edge case with its own disposition (Logout+disconnect); data-model I-NEX-9 defines "present 789 whose parse_seqnum yields 0" as distinct from absent (which has no disposition). "Field absent" → no proactive resend; "Field present-but-invalid" → Logout+disconnect. Explicitly distinct.
- [x] CHK003 Are all three invalid-input classes enumerated in the requirements (empty `789=`, non-numeric `789=abc`, overflow)? [Completeness, research D-10] — PASS: spec Edge Cases "Invalid inbound 789" states "empty, non-numeric, or overflows (the parser yields 0)"; data-model I-NEX-9 says "(empty/non-digit/overflow)"; research D-10 enumerates "empty, non-digit, AND overflow" and further states that `parse_seqnum` returns 0 for all three; RED witness `Honor_Invalid789_LogoutThenDisconnect` lists "789=` / `789=abc` / overflow". All three classes named.
- [x] CHK004 Does a requirement state the amplification risk being guarded against (a malformed value driving a `[1, N-1]` full-history replay)? [Completeness, research D-10, data-model I-NEX-9] — PASS: data-model I-NEX-9 explicitly states "a present 789 whose parse_seqnum yields 0 … evaluated BEFORE the `X<N` compare so a malformed value can never drive a `[1, N-1]` full-history replay via the clamp at `:2560`"; research D-10 explains the amplification mechanism precisely ("a remote-triggerable amplification") and the clamp that would activate it; spec Edge Cases "Invalid inbound 789" states "evaluated BEFORE the `X<N` comparison, so a malformed 789 can never drive a `[1, N-1]` full-history replay". The risk and the guard are both named in requirements artifacts.

## Requirement Clarity

- [x] CHK005 Is the EVALUATION ORDER specified — invalid-789 checked BEFORE the `X<N` comparison — rather than left implicit? [Clarity, Spec §Edge Cases "Invalid inbound 789", contracts C4] — PASS: spec Edge Cases states "evaluated BEFORE the `X<N` comparison"; contracts C4 states "X == 0 (present-but-invalid: empty / non-digit / overflow) → C6 (Logout+disconnect). Evaluated FIRST, before the `X<N` compare"; data-model I-NEX-9 states "evaluated BEFORE the `X<N` compare"; research D-10 states the guard must be "evaluated BEFORE the `X<N` compare". Evaluation order is explicitly stated in all four artifacts.
- [x] CHK006 Is the advertised-value off-by-one resolved unambiguously to plain `next-expected-inbound` (NO `+1`), with the reason (acceptor reply built after the inbound is counted) stated? [Clarity, Spec §FR-002 / §Clarifications, data-model E-OBO] — PASS: spec FR-002 states "last-received + 1" (i.e. next-expected-inbound); spec clarifications Gate A round 1 Q "Acceptor advertised 789 off-by-one — next_inbound_unsafe() or +1?" → answer "Plain next_inbound_unsafe()" with the explicit reason ("check_inbound(:1571) … advances next_inbound_ BEFORE the reply Logon is built (:1745)"); data-model E-OBO section is titled "RESOLVED: plain next_inbound_unsafe() (no +1)" and states the reason + QFcpp divergence; research D-4 derives it from the increment ordering. Unambiguous with reason.
- [x] CHK007 Is the comparison BASIS specified as next-outbound (N), and is it distinguished from the inbound counter used elsewhere? [Clarity, Spec §FR-003, data-model I-NEX-11] — PASS: FR-003 states "compare X to its own next-outbound sequence number N"; data-model I-NEX-11 is titled "two distinct counters" and states "the 789 comparison uses N = peek_outbound() (OUTBOUND), while the existing Active too-high arm computes next_expected = next_inbound_unsafe() (INBOUND) at :1968. These are different counters; an implementer must NOT compare X against next_inbound_unsafe()"; contracts C5 repeats the distinction. Clear and explicitly guarded.
- [x] CHK008 Is the rationale for X>N being unrecoverable ("peer claims receipt of messages we never sent") stated, so the disposition is justified rather than arbitrary? [Clarity, Spec §US3 / §FR-005] — PASS: spec US3 Why-this-priority states "A safety/correctness guard on the new inbound path … silently accepting an impossible expectation would corrupt the session"; FR-005 states "the peer is claiming to have received messages we never sent — an unrecoverable sequence-integrity violation"; research D-6 states "The peer is claiming receipt of messages we never sent — an unrecoverable integrity violation." Rationale is explicitly stated.

## Requirement Consistency

- [x] CHK009 Do the X>N and present-but-invalid dispositions resolve to the SAME action (Logout+disconnect), consistently across spec, data-model (I-NEX-4/9), and contract C6? [Consistency, cross-artifact] — PASS: spec Edge Cases "Invalid inbound 789" states "treated as a sequence-integrity error: `Logout`+disconnect (parity with X>N)"; data-model I-NEX-9 states "build_logout+disconnect (parity with I-NEX-4)"; data-model I-NEX-4 is the X>N definition; contracts C6 groups both cases under the same action ("build_logout … then disconnect. MUST NOT advance to established … Both handlers"). All three artifacts consistently equate the two dispositions.
- [x] CHK010 Are the two distinct counters (outbound `N` for the 789 compare vs inbound for the steady-state too-high arm) kept consistent everywhere, with no requirement comparing X against the inbound counter? [Consistency, data-model I-NEX-11] — PASS: data-model I-NEX-11 defines the distinction as an invariant; spec FR-003 uses "next-outbound sequence number N"; contracts C4/C5 both use peek_outbound() for N; plan notes "Watch the two-counter trap (I-NEX-11)"; tasks Notes section repeats the warning. No artifact conflates the two counters; the distinction is enforced as a named invariant with a NOT-compare-against-inbound statement.
- [x] CHK011 Is the disposition specified identically for both handlers (acceptor + initiator), with no role asymmetry in the integrity error? [Consistency, Spec §FR-007, contracts C6/C8] — PASS: FR-005 states "Applies in both handlers" (not explicitly, but FR-007 states symmetric across both roles); data-model I-NEX-4 states "Applies in both handlers"; contracts C6 ends "Both handlers"; contracts C8 states "C2–C7 apply to both inbound-Logon handlers". No role asymmetry in the error disposition. Consistent.

## Acceptance Criteria Quality

- [x] CHK012 Is the X>N outcome given an objective acceptance criterion (Logout present + disconnect + NOT established)? [Measurability, Spec §SC-004] — PASS: SC-004 states "An inbound `789` above our next-outbound produces a `Logout` (with explanatory text) followed by disconnect (no silent accept), verified by a negative test"; US3 Acceptance Scenario 1 states "the session is terminated with the defined sequence-integrity error disposition (no silent accept)"; RED witness `Honor_XgtN_LogoutTextThenDisconnect` is listed in data-model. Three objective criteria: Logout emitted, disconnect, NOT established.
- [x] CHK013 Is the invalid-789 outcome objectively verifiable as Logout+disconnect with NO `[1,N-1]` replay emitted? [Measurability, data-model RED witness `Honor_Invalid789_LogoutThenDisconnect`] — PASS: data-model RED witness `Honor_Invalid789_LogoutThenDisconnect` asserts "789=` / `789=abc` / overflow ⇒ Logout+disconnect, NO `[1,N-1]` replay (I-NEX-9)"; tasks T020 names this witness; SC-004 covers this class of outcome. The "NO [1,N-1] replay" assertion makes it objectively measurable (assert no resend frames emitted).
- [x] CHK014 Can the advertised-value correctness be objectively checked (acceptor's 789 equals the peer's actual next send)? [Measurability, data-model E-OBO witness] — PASS: data-model E-OBO section states "RED-witness it: a both-role round-trip where the acceptor's advertised 789 is checked against the peer's actual next send (Emit_AcceptorReply_AdvertisesNextInboundNoPlusOne)"; this witness is in the RED-witness table; tasks T007 names it. The peer's actual next send is the measurable comparand.

## Edge-Case Coverage

- [x] CHK015 Are the boundary conditions of `parse_seqnum` returning 0 (the value that would otherwise satisfy `X<N`) addressed as a requirement rather than an implementation accident? [Coverage, research D-10] — PASS: data-model I-NEX-9 makes the parse_seqnum→0 sentinel a named invariant: "present 789 whose parse_seqnum yields 0 (empty/non-digit/overflow) ⇒ build_logout+disconnect (parity with I-NEX-4), evaluated BEFORE the X<N compare so a malformed value can never drive a [1, N-1] full-history replay via the clamp at :2560"; contracts C4 explicitly lists "X == 0 (present-but-invalid)" as a named case. Stated as a requirement with the amplification rationale, not an implicit implementation note.
- [x] CHK016 Is the X==0-from-valid-input case (if any) disambiguated from X==0-from-parse-failure in the requirements? [Coverage, Ambiguity, data-model I-NEX-9] — PASS: FIX SEQNUM is always ≥1 (sequence numbers start at 1); a literal "789=0" is an invalid value, and parse_seqnum parsing the digit "0" yields 0, which falls under the I-NEX-9 invalid-parse branch (Logout+disconnect). There is no valid-input case that produces X==0 per the FIX standard. The requirement at I-NEX-9 handles all X==0 outcomes identically (Logout+disconnect regardless of whether 0 came from parse failure or a literal "789=0"), which is the correct disposition either way. No disambiguation is needed because the dispositions are identical; no ambiguity exists for the implementer. DD-DECIDED research D-10: the parse_seqnum sentinel value (0) is defined to cover both empty/non-digit/overflow AND the literal digit-zero case, and both are treated as integrity errors — settled in the converged research-D anchor.

## Notes

- Requirements-quality only. The integrity behaviours are RED-witnessed by tasks T020 (`Honor_XgtN_*`, `Honor_Invalid789_*`) and implemented by T021; the fuzz seed for the new parser arm is T026.
- Feeds the mandatory `/speckit-checklist-audit` gate (pipeline step 9).

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 15 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 1 |
| WAIVED | 0 |
| **Total** | **16** |

### SPEC-FIXED items
(none)

### DD-DECIDED items
- CHK016 — anchor research D-10 (converged Gate A round 2); rationale: FIX SEQNUM ≥1 means X==0 from literal "789=0" is also an invalid value; parse_seqnum returning 0 covers all cases uniformly with identical Logout+disconnect disposition — no disambiguation needed or possible.

### WAIVED items
(none)

Anchors spot-verified: research D-10 (parse_seqnum sentinel definition); data-model I-NEX-4/I-NEX-9/E-OBO; contracts C4/C6/C8; spec FR-005/FR-007/SC-004/Edge Cases "Invalid inbound 789" — all resolve in the Gate-A-converged bundle (plan.md `## Gate A` round 2 applied 2026-06-07).
