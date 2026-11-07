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
`R {13,14}/35 X` = Reject 371=35, 372 = X (the first 35), 45 = the first 34, **14 when any later 35 lies past the header,
else 13**; the header is the session dictionary's ∪ the FIXT standard header set (research D5). "As X" = the ending of
the same frame with every later 35 removed and re-framed (research D6).

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

| Inbound MsgType | 372 |
|---|---|
| fits the message | verbatim, `=` included (`372=D=X`) — unchanged at the gate; **new at the 092 disposer** for a MsgType longer than the longest shipped one (before: omitted) |
| does not fit | omitted; the Reject is sent (before: nothing sent). For the BusinessMessageReject, where 372 is required, this ending is **provisional** (spec Clarifications, Gate A round 1) |

Text(58) is never dropped to make room. With CompIDs long enough that a gate Reject's fixed fields plus 58 do not fit,
the Reject is not sent, as before this feature for its other fields (disclosed residual, research D7).
