# Research: 093-inbound-frame-dispositions

`/speckit-plan` extends this file. R-1 was done at specify time because the owner made the FR-004
ruling conditional on it.

## R-1: a frame whose third field is not MsgType(35)

**Question.** A correctly framed frame (8= first, 9= second with a correct count, 10= last and correct)
whose third field is not 35, e.g. `8=FIX.4.2|9=..|34=2|35=0|…|10=..|`. Must it be disregarded as
garbled, Rejected with 373=14, accepted, or met with a disconnect? Does the answer differ before and
after Logon?

**Decision.** Disregard it as garbled: no Reject, NextNumIn unchanged, continue. This holds in every
state and in both validation modes (FR-004). It confirms the owner's ruling of 2026-10-02.

### Primary documents

The local copies are under the parent's `research/G19-fix-fpml-iso20022/research/`. Text was extracted
with pypdf; page numbers are the PDF's own.

- **FIX Session Layer 2020 §4.5.2 (pp. 26–27).**
  - The definition: "A message shall be considered garbled if … MsgType(35) is not the third tag in a
    message."
  - The rule: "The receiving FIX session processor must disregard the garbled message and not
    increment NextNumIn."
  - §4.5.4 scopes Reject to a message that is "not garbled".
  - §8.5 StandardHeader (p. 62): "MsgType(35) must be the third field in the message."
  - Code 14 ("Tag specified out of required order") appears only in the code list (p. 80).
- **FIX Session Test Cases 2020.**
  - Scenario 2 row t (p. 11): "BeginString(8), BodyLength(9), and MsgType(35) are not the first three
    fields of message. 1. Consider garbled and ignore message. 2. Do not increment NextNumIn.
    3. Continue accepting messages." It carries no state qualifier.
  - The only 373=14 row is Scenario 14 row g: header, then body, then trailer interleaving. That is a
    different defect.
- **FIX 4.4 with 2003-06-18 errata.**
  - Vol 1, p. 18: "The first three fields in the standard header are BeginString … BodyLength …
    MsgType".
  - Vol 2, p. 30, "What constitutes a garbled message": "MsgType (tag #35) is not the third tag in a
    message."
  - Vol 2, p. 43, rows 2s and 2t: "Consider garbled and ignore message (do not increment inbound
    MsgSeqNum)".
- **FIX 4.2:** not obtained (fixtrading.org did not serve the document). The 2020 Session Layer's
  BeginString row lists FIX.4.2.

### Engines

Shallow clones of each default branch, 2026-10-02: QuickFIX/C++ `386ce46e`, QuickFIX/J `db43e4d4`,
QuickFIX/Go `eff945dd`, QuickFIX/n `03081287`.

| Engine | After Logon | Before Logon | Notes |
|---|---|---|---|
| QuickFIX/C++ | ignored (logged) | **disconnect** (`SocketConnection.cpp` drops the socket when not logged on) | The check is `Message.cpp` `headerOrder[]` and throws `InvalidMessage("Header fields out of order")`. It is skipped when `ValidateLengthAndChecksum=N`. 373=14 comes from `DataDictionary` `hasValidStructure`, which never covers the first three fields |
| QuickFIX/J | ignored (logged) | ignored, unless the frame is itself a Logon (disconnect) | `Message.java` preamble check, guarded by having a data dictionary. `RejectGarbledMessage=Y` (not the default) sends a text-only Reject with no 373 |
| QuickFIX/Go | ignored (`OnEventf`, no state change) | **disconnect** on the acceptor's first frame | `message.go` extracts 8, 9 and 35 in order. There is no toggle |
| QuickFIX/n | ignored | ignored, unless the frame is a Logon (disconnect) | `Message.cs` mirrors C++, and so does its toggle |

The shared acceptance test is `test/definitions/server/fix42/2t_FirstThreeFieldsOutOfOrder.def`. It sends
fixpp#514's exact shape after Logon (`34=3^35=0`), then resends 34=3 as a TestRequest, and expects the
Heartbeat reply at outbound 34=3. So the engine sent nothing in response and did not consume the
number. No repo excludes the test.

### Consequences for the spec

- No source supports 373=14 for this shape. B-005-7's description of a QuickFIX divergence, "QF emits
  373=14", is wrong for field 3. QuickFIX emits 373=14 for header, body and trailer interleaving.
- **Before Logon the engines split.** C++ and Go disconnect, while J and n ignore the frame unless it is
  a Logon. No spec text requires a disconnect. FR-004 and FR-005 disregard the frame, and FR-006's
  establishment timeout bounds the wait. Gate A may revisit this. A disconnect is defensible under TC
  Scenario 2S ("first message received is not a Logon → disconnect"), but it is not mandated.
- **Validation off.** C++, J and n skip the check when validation is off and then accept the frame.
  fixpp has no equivalent toggle for framing checks, and FR-004 applies in both of fixpp's validation
  modes.
