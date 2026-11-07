# Contract: the Rejects and dispositions this feature changes

Notation: `R <373>/<371> <372> 58="…"`; `Disc` = disconnect, nothing sent; 45 = the frame's MsgSeqNum unless stated.
Decisions and the merged clauses each row supersedes: [research.md](../research.md) (D1–D10, Supersessions).

## Validating session (FIXT or not), validation gate

Texts are QuickFIX C++'s, in the FIX.4.2-and-later form, on every BeginString (research D3).

| Inbound | Before | After |
|---|---|---|
| undefined MsgType `U1` | `R 2/8 U1` | `R 11/- U1 58="Invalid MsgType"` |
| undefined tag 9999 | `R 2/9999 D` | `R 0/9999 D 58="Invalid tag number"` |
| defined tag 44 on a Heartbeat | `R 2/44 0` | `R 2/44 0 58="Tag not defined for this message type"` |
| required tag missing | `R 1/<tag> …` | `+ 58="Required tag missing"` |
| bad enum / bad value | `R 5/<tag> …` | `+ 58="Value is incorrect (out of range) for this tag"` |
| format (6) | `R 6/<tag> …` | `+ 58="Incorrect data format for value"` |
| header order (14) | `R 14/- …` | `+ 58="Tag specified out of required order"` |
| repeated MsgType (FR-005) | `R 13/35` / `R 14/35` (FIXT only) | see the next table, `+ 58=` 13's / 14's text |
| 099 1128 refusals | `R 4/1128` / `R 5/1128` / `R 14/1128` | `+ 58=` 4's / 5's / 14's text |

## Repeated MsgType (a later 35 whose value differs from the first)

"Differ" is sticky: `D…A…D` and `A…D…A` differ. A repeat with the **same** value only is not a conflict: unchanged.
`R {13,14}/35 X` = Reject 371=35, 372 = X (the first 35), 45 = the first 34, 373 by research D5's three-clause rule
(**14 when any later 35 lies past the header, else 13 when one lies in it, else 14**; a walk that faults answers 14);
the header is the session dictionary's ∪ the FIXT standard header set, and the field that ends the header and every
later field are read with the route's hooks (research D5). "As X" = the ending of the same frame with every later 35
removed and re-framed (research D6).

| Session | State | Before | After |
|---|---|---|---|
| validating FIXT | establishment | `Disc` (last ≠ third only) | `Disc` |
| validating FIXT | LogonReceived / Active | `R {13,14}/35 X` (last ≠ third; 13 if any later 35 in the header) | `R {13,14}/35 X`, consumed as X |
| validating non-FIXT | establishment | establishes if the last is `A` with Logon fields; else per the last value | `Disc` |
| validating non-FIXT | LogonReceived / Active | validated as X, dispatched as the last | `R {13,14}/35 X`, consumed as X |
| validation off, any | establishment | establishes on `X=D`, last `A`, with Logon fields | `Disc` |
| validation off, any | LogonReceived / Active | handled as the last | as X, no Reject for the repeat |
| any (validating or not, FIXT or not) | LogoutSent | the reply when the **last** 35 is 5 | the reply when the **first** 35 is 5; otherwise as X; no Reject for the repeat |

With exactly two 35s the validating FIXT rows answer as 099 did. They differ on three or more: `0,D(hdr)…0(past)` →
`R 14/35 0` (before: no conflict), `0,0(hdr)…D(past)` → `R 14/35 0` (before: 13).

## RefMsgType echo

Every inbound-fed Reject / BMR builder research D7's census recipe finds (at round 1: both `emit_session_reject_`
forms, `inbound_reject_leaf_`, `inbound_reject_best_effort_leaf_`, `inbound_business_reject_leaf_`); so the validation
gate and the 092 disposer too.

One rule for 372 on both messages (research D7): verbatim whenever the message fits the engine's outbound ceiling
`Session::kSendFrameBytes`. "Site buffer" is the site's own fixed buffer; an overflow of it is rebuilt into the
session's inbound reply slot, before any callback and before `assign_outbound`, with no allocation.

| Message with the inbound MsgType in 372 | Session Reject (35=3) | BusinessMessageReject (35=j) |
|---|---|---|
| fits the site buffer | 372 verbatim, `=` included (`372=D=X`): unchanged at the gate; **new at the 092 disposer** for a MsgType longer than the longest shipped one (before: omitted) | 372 verbatim: unchanged |
| overflows the site buffer, fits `kSendFrameBytes` | 372 verbatim, rebuilt in the slot (before: nothing sent) | 372 verbatim, rebuilt in the slot; resent with 372 on a later ResendRequest (before: nothing sent) |
| exceeds `kSendFrameBytes` | sent **without 372** (before: nothing sent) | **not sent**: no callback, no outbound number, the inbound consumed as today, the session in its state, one rate-bounded session log record (research D7, "Outbound-ceiling ending") (before: nothing sent, no record) |

Text(58) is never dropped to make room. The slot rebuild also recovers a Reject built through the helper whose own
buffer overflowed because of long CompIDs or 58. A Reject that exceeds `kSendFrameBytes` even without 372 is not sent:
no callback, no outbound number, and the same log record (disclosed residual, research D7). Both not-sent endings
share one rate state and one suppressed count: at most one record per max(HeartBtInt, 1 s) on `effective_clock_`
(093's garbled-frame rule), each naming the message type not sent and the count suppressed since the previous record.
*(Narrowed after tasks by orchestrator ruling, at `/speckit-analyze`: helper-built sites only;
`apply_inbound_sequence_reset`'s literal-372 Reject, built in its own fixed buffer, is dropped with no record when long
CompIDs overflow it — the CompID residual.)*
