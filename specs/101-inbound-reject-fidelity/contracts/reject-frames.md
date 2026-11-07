# Contract: the Rejects and dispositions this feature changes

Notation: `R <373>/<371> <372> 58="…"`; `Disc` = disconnect, nothing sent; 45 = the frame's MsgSeqNum unless stated.

## Validating session (FIXT or not), validation gate

| Inbound | Before | After |
|---|---|---|
| undefined MsgType `U1` | `R 2/8 U1` | `R 11/- U1 58="Invalid MsgType"` |
| undefined tag 9999 | `R 2/9999 D` | `R 0/9999 D 58="Invalid tag number"` |
| defined tag 44 on a Heartbeat | `R 2/44 0` | `R 2/44 0 58="Tag not defined for this message type"` |
| required tag missing | `R 1/<tag> …` | `+ 58="Required tag missing"` |
| bad enum / bad value | `R 5/<tag> …` | `+ 58="Value is incorrect (out of range) for this tag"` |
| format (6) | `R 6/<tag> …` | `+ 58="Incorrect data format for value"` |
| header order (14) | `R 14/- …` | `+ 58="Tag specified out of required order"` |
| 099 FR-025 repeat in header / past it | `R 13/35` / `R 14/35` | `+ 58=` 13's / 14's text |
| 099 1128 refusals | `R 4/1128` / `R 5/1128` / `R 14/1128` | `+ 58=` 4's / 5's / 14's text |

## Repeated MsgType (`35=X … 35=Y`, Y ≠ X)

| Session | State | Before | After |
|---|---|---|---|
| validating FIXT | establishment | `Disc` | `Disc` (unchanged) |
| validating FIXT | LogonReceived / Active | `R 13|14/35 X` | unchanged (+ 58) |
| validating non-FIXT | establishment | establishes if `Y=A` with Logon fields; else per last value | `Disc` |
| validating non-FIXT | LogonReceived / Active | validated as X, dispatched as Y | `R 13|14/35 X`, consumed as X |
| validation off, any | establishment | establishes on `X=D, Y=A` with Logon fields | `Disc` |
| validation off, any | LogonReceived / Active / LogoutSent | handled as Y | handled as X, no Reject |

A frame whose 35 repeats with the **same** value is not a conflict: unchanged.

## RefMsgType echo

| Inbound MsgType | 372 on any inbound-fed Reject / BMR |
|---|---|
| fits the message | verbatim, `=` included (`372=D=X`) — unchanged |
| does not fit | omitted; the Reject is sent (before: nothing sent) |
