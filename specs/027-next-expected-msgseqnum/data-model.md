# Data Model — NextExpectedMsgSeqNum(789) fast session resume

No new persistent entities. The feature adds one config field, one inbound-header field capture, and a set of invariants over the existing sequence-number state. Sequence numbers use the existing `seqnum_t`.

## Entities

### E1 — `SessionConfig::enable_next_expected_msg_seq_num` (config)
- Type: `bool`, default **`false`** (explicit, [const §XII.5]).
- Additive primitive field in `include/fixpp/session/session_config.hpp`. No new include (§XV.9 N/A).
- Semantics: a single knob controlling BOTH emitting our 789 in Logon AND honoring a peer's inbound 789 (D-2).

### E2 — `NextExpectedMsgSeqNum(789)` Logon field (wire)
- Standard FIX tag 789, type SEQNUM. Already defined in `dictionaries/FIX44.xml:6128` and permitted (optional) in the `Logon` message (`:284`) — no codegen change (D-1).
- Emitted in `build_logon` (conditional append after the `141` block); read on the inbound-Logon header scan.

### E3 — inbound `LogonHeader.next_expected_msg_seq_num` (parse capture)
- A `std::string_view` added to the inbound-Logon header struct (`session.cpp:~1167`), captured by a tag-789 case in the header scan (`:~1253`-style). Empty view ⇒ field absent.
- Parsed to `seqnum_t X` only when present AND the knob is on.

## Key quantities (read-only from the existing `SeqnumManager`)

- **N = next-outbound** = `seqnum_mgr_.peek_outbound()` (= QFcpp `getExpectedSenderNum()`). Messages with seqnum `[1, N-1]` have been sent; `N` is the next to assign.
- **next-expected-inbound** = `seqnum_mgr_.next_inbound_unsafe()` (= the value we advertise as our 789).

## Invariants

- **I-NEX-1 (advertise = next-expected-inbound)**: when the knob is on, every outbound Logon carries `789 = ` our next-expected-inbound. Initiator's own Logon: `next_inbound_unsafe()`. Acceptor reply: see **E-OBO**.
- **I-NEX-2 (comparison basis)**: an inbound `789 = X` is compared to `N = peek_outbound()`. `X < N` ⇒ resend `[X, N-1]`; `X == N` ⇒ no resend; `X > N` ⇒ error (I-NEX-4). The resend range is `[X, N-1]` inclusive — NOT `[X, N]` (N is the next-to-assign, not yet sent).
- **I-NEX-3 (resend reuse + semantics)**: the `X<N` proactive resend reuses the existing `ResendRequest`-reply walk (`session.cpp:2485+`): stored app messages replayed with `PossDupFlag(43)=Y`+`OrigSendingTime(122)` keeping their original `MsgSeqNum`; admin/absent runs collapsed into one `SeqReset`-`GapFill(123=Y, 36=<next live>)`. Transmit-only — does NOT advance the live outbound counter, not re-stored.
- **I-NEX-4 (integrity error)**: `X > N` ⇒ `build_logout("NextExpectedMsgSeqNum too high, expecting N but received X")` then disconnect; MUST NOT advance to established as in-sync (FR-005, D-6).
- **I-NEX-5 (no double recovery / suppression)**: when the knob is on, the at-logon too-high arm (`:1964-1991`) MUST NOT also emit `ResendRequest` for the gap; it relies on the peer's 789-driven proactive resend (FR-004). When the knob is OFF, the `ResendRequest` path is unchanged (013 recovery intact).
- **I-NEX-6 (both-peers-required, no fallback)**: there is no automatic `ResendRequest` fallback. If the knob is on but the peer doesn't advertise/act on 789, our own gap does not recover (the ResendRequest was suppressed per I-NEX-5). Documented limitation L-027-1 (D-7).
- **I-NEX-7 (default-off byte-identity)**: with the knob off, the outbound Logon contains no 789 field and is byte-identical to the pre-feature baseline; an inbound 789 is ignored and standard recovery applies (FR-006/SC-002).
- **I-NEX-8 (reset consistency)**: when a Logon also carries `141=Y` (024 reset), the advertised 789 reflects post-reset next-expected-inbound = 1, read from the already-reset `seqnum_mgr_` state (D-8).

## E-OBO — acceptor-reply off-by-one (the highest off-by-one risk)

The acceptor's *reply* Logon (`session.cpp:1745`) is built in response to the peer's inbound Logon. Whether its advertised 789 is `next_inbound_unsafe()` or `next_inbound_unsafe() + 1` depends on **whether the inbound Logon's own seqnum has already incremented `next_inbound_` at the point the reply is built**.

- QFcpp's analogue (`generateLogon(aLogon)`) uses `getExpectedTargetNum() + 1` "because incoming Logon did not increment the target SeqNum yet".
- **fixpp pin**: determine fixpp's increment order by reading where the inbound Logon advances `next_inbound_` relative to the `:1745` reply build, and set the advertised value so it equals "the seqnum we next expect from the peer AFTER having received this Logon". **RED-witness it** (a both-role round-trip where the acceptor's advertised 789 is checked against the peer's actual next send) — do NOT copy QFcpp's literal `+1` without confirming fixpp's timing.

## RED witnesses (TDD targets)

| Witness | Asserts |
|---|---|
| `Emit_Initiator_AdvertisesNextExpectedInbound` | initiator Logon `789 == next_inbound_unsafe()` (I-NEX-1) |
| `Emit_AcceptorReply_OffByOneCorrect` | acceptor reply `789` equals the peer's actual next send (E-OBO) |
| `Honor_XltN_ResendsExactRange_NoResendRequest` | `X<N` ⇒ resend exactly `[X, N-1]` (PossDup app + GapFill admin), zero ResendRequest on the wire (I-NEX-2/3/5) |
| `Honor_XeqN_NoResend` | `X==N` ⇒ no resend (I-NEX-2) |
| `Honor_XgtN_LogoutTextThenDisconnect` | `X>N` ⇒ Logout with text + disconnect, not established (I-NEX-4) |
| `DefaultOff_ByteIdenticalLogon_InboundIgnored` | knob off ⇒ no 789 emitted (byte-identical) + inbound 789 ignored + ResendRequest still used (I-NEX-7) |
| `Suppression_KnobOn_NoResendRequest_KnobOff_Yes` | knob-on at-logon gap emits no ResendRequest; knob-off still does (I-NEX-5, 013 regression guard) |
| `Reset141Plus789_AdvertisesOne` | 141=Y + knob ⇒ advertised 789 == 1 (I-NEX-8) |
| `NoHeap_EmitAndResendPath` | emit append + proactive resend allocate zero heap (mallocnesia) |
