# Spec Coverage Index

> Phase 1.6 output — 2026-05-07.
> Maps every normative FIX spec section → catalogue row IDs (bidirectional traceability).
> Format: one sub-table per document. Zero-coverage sections are flagged as gaps.
> Canonical Spec ref format: `[DocAbbrev §X.Y.Z] Section title`

---

## DocAbbrev Registry

| Abbrev | Document | URL |
|---|---|---|
| `FIX-SL` | FIX Session Layer (classic, online ed.) | fixtrading.org/standards/fix-session-layer-online/ |
| `FIXT` | FIXT.1.1 Transport Independence (sections §5.3 of FIX-SL) | embedded in FIX-SL §5 |
| `FIXS` | FIXS RC1 — FIX over TLS (online ed., v1.1 RC1) | fixtrading.org/standards/fixs-online/ |
| `FIX40`…`FIX50SP2` | FIX application spec per version (classic tag=value) | fixtrading.org/standards/ |
| `FIX-TC` | FIX Session Layer Test Cases (online ed.) | fixtrading.org/standards/fix-session-testcases-online/ |
| `FIX-Latest` | FIX Latest (living online standard) | fixtrading.org/standards/fix-latest-online/ |

**Note on FIXT:** FIXT.1.1 is not a separate online document; its normative content is embedded in `FIX-SL §4` (session rules apply to all profiles) and described in `FIX-SL §5.3` (FIXT session profile). References use `[FIX-SL §5.3.x]` for FIXT-specific items and `[FIX-SL §4.x]` for shared session rules.

---

## FIX Session Layer (FIX-SL)

Section structure sourced from fixtrading.org/standards/fix-session-layer-online/ (confirmed 2026-05-07).

| Section | Title | Normative? | Catalogue IDs | Gap note |
|---|---|---|---|---|
| §1 | Scope | N (informative framing) | — | covered by impl/constitution |
| §2 | Normative references | N (bibliography) | — | covered by impl/constitution |
| §3 | Terms and definitions | N (definitions) | — | covered by impl/constitution |
| §3.1 | General terms and definitions (23 terms incl. NextNumIn/Out, TestRequestThreshold, etc.) | N | S-008, S-009 | covered by impl/constitution; threshold values are rules-of-engagement |
| §4 | FIX session (root) | Y | S-001–S-036 (aggregate) | — |
| §4.1 | Sequence numbers | Y | S-009 | — |
| §4.2 | Identifying the FIX session | Y | S-016, S-020 | — |
| §4.2.1 | The FIX session profile | Y | S-020 | — |
| §4.2.2 | Identification of FIX session peers (CompID) | Y | S-016 | — |
| §4.2.3 | Validation of SendingTime(52) | Y | S-019 | — |
| §4.2.4 | Additional fields available for peer identification (SubID, LocationID) | Y | S-016 | — |
| §4.3 | Establishing a FIX connection | Y | S-001, S-015, S-021, S-022 | — |
| §4.3.1 | Transport layer requirements (TCP/IP, FIXS mandatory) | Y | T-001, T-002 | — |
| §4.3.2 | Using the TestMessageIndicator(464) | Y | S-029 | — |
| §4.3.3 | Application layer encryption (deprecated EncryptMethod) | Y | S-021 | — |
| §4.3.4 | Heartbeat interval | Y | S-015 | — |
| §4.3.5 | Heartbeat interval determination | Y | S-015 | — |
| §4.3.5.1 | Acceptor requires specific heartbeat interval | Y | S-015 | — |
| §4.3.5.2 | Acceptor requires initiator specify range value | Y | S-015 | — |
| §4.3.5.3 | Acceptor accepts initiator specified interval | Y | S-015 | — |
| §4.3.6 | Maximum message size (MaxMessageSize 383) | Y | S-030 | — |
| §4.3.7 | Specifying application version (DefaultApplVerID 1137 / FIXT) | Y | S-025, S-026 | — |
| §4.3.8 | Specifying supported message types (NoMsgTypes in Logon) | Y | S-037 | MISSING → row added (see Gap Summary) |
| §4.3.9 | Identification of application system and FIX session processor | Y | — | MISSING → row added (S-038) |
| §4.3.10 | Responding to FIX session establishment request (acceptor Logon ack / Logout reject) | Y | S-001 | — |
| §4.3.11 | Initial synchronization of messages (Logon seqnum check, ResendRequest on gap) | Y | S-014 | — |
| §4.3.12 | Synchronization after successful logon | Y | S-014, S-031 | — |
| §4.4 | Extended features for FIX session and connection initiation | Y | S-031, S-032 | — |
| §4.4.1 | Using NextExpectedMsgSeqNum(789) | Y | S-031 | — |
| §4.4.2 | Using ResetSeqNumFlag(141) for 24-hour connectivity | Y | S-032 | — |
| §4.4.3 | Using ResetSeqNumFlag(141) during connection establishment | Y | S-032 | — |
| §4.4.4 | Using initiator state to restore acceptor session state | Y | S-014 | — |
| §4.5 | Message exchange during a FIX connection | Y | S-003, S-004, S-007 | — |
| §4.5.1 | FIX connection keep-alive (heartbeat) | Y | S-003, S-004 | — |
| §4.5.2 | Garbled message processing | Y | TC-003, S-009 | — |
| §4.5.3 | Missing sequence number (gap detection → ResendRequest) | Y | S-005, S-014 | — |
| §4.5.4 | Rejecting invalid messages (Reject 35=3) | Y | S-007, S-033, S-034 | — |
| §4.5.5 | Test Request processing | Y | S-004, S-003 | — |
| §4.6 | FIX connection termination | Y | S-002 | — |
| §4.6.1 | Normal logout processing | Y | S-002 | — |
| §4.6.2 | Logout without acknowledgement (timeout → force disconnect) | Y | S-002 | — |
| §4.6.3 | Logout with retransmission of missed messages | Y | S-002, S-014 | — |
| §4.6.4 | When to terminate without Logout(35=5) (invalid BeginString/CompID cases) | Y | S-016, S-020 | — |
| §4.7 | Extended features for FIX connection termination | Y | S-031 | — |
| §4.7.1 | Using NextExpectedMsgSeqNum(789) on invalid MsgSeqNum(34) | Y | S-031 | — |
| §4.8 | Message recovery | Y | S-014 | — |
| §4.8.1 | Ordered message processing | Y | S-009, S-014 | — |
| §4.8.2 | Request retransmission of messages (ResendRequest) | Y | S-005, S-024 | — |
| §4.8.3 | Responding to ResendRequest(35=2) | Y | S-005, S-014 | — |
| §4.8.4 | Possible duplicates (PossDupFlag semantics) | Y | S-010, S-033 | — |
| §4.8.5 | Gap fill process (SequenceReset-GapFill) | Y | S-006 | — |
| §4.8.5.1 | Example using SequenceReset(35=4) | Y | S-006 | — |
| §4.8.6 | Sequence reset (hard reset, GapFillFlag=N) | Y | S-006, S-023 | — |
| §4.8.7 | Processing inbound possible duplicate messages | Y | S-010 | — |
| §4.8.8 | Processing gaps for session layer messages (admin msg gap-fill) | Y | S-014 | — |
| §4.9 | Resending unacknowledged application message (PossResend 97) | Y | S-010 | — |
| §4.9.1 | Difference between application resend and session retransmission | Y | S-010 | — |
| §4.10 | FIX session state matrix | Y | S-008 | — |
| §4.10.1 | FIX logon process state transition diagram | Y | S-008 | — |
| §4.10.2 | FIX logout process state transition diagram | Y | S-008 | — |
| §5 | FIX session profiles (informative) | N | — | informative; normative requirements embedded in §4 |
| §5.1 | FIX.4.2 session profile | N | S-020, D-001 | informative profile description |
| §5.2 | FIX4 session profile | N | S-020 | informative profile description |
| §5.3 | FIXT session profile | N | S-025, S-026 | informative; normative FIXT rules in §4.3.7 |
| §5.3.1 | Profile identification (BeginString=FIXT.1.1) | N | S-020 | informative |
| §5.3.2 | Multiple application version support | N | S-026 | informative |
| §5.3.3 | Session default application version identification (DefaultApplVerID 1137) | N | S-025 | informative |
| §5.3.4 | Message type default application version | N | S-026 | informative |
| §5.3.5 | Explicit application version per message (ApplVerID 1128) | N | S-026 | informative |
| §5.3.6 | Use of extension packs | N | — | out-of-scope → dropped(post-1.0: EP-level field additions) |
| §5.3.7 | Use of custom application version | N | — | out-of-scope → dropped(post-1.0: custom app versions) |
| §5.4 | LFIXT session profile | N | S-027, S-028 | informative profile description |
| §5.4.1 | LFIXT profile identification | N | S-027, S-028 | informative |
| §5.4.2 | LFIXT application version identification | N | S-027, S-028 | informative |
| §5.4.3 | LFIXT transport layer requirements | N | S-027 | informative |
| §5.4.4 | LFIXT compatible mode | N | S-027 | informative |
| §5.4.5 | LFIXT succinct mode | N | S-028 | informative |
| §6 | FIX message routing | Y | S-016 | — |
| §6.1 | Message routing — point-to-point | Y | S-016 | — |
| §6.2 | Message routing — third-party routing (OnBehalfOf/DeliverTo) | Y | S-016, TC-016 | — |
| §7 | Transmitting alternatively encoded messages | Y | — | out-of-scope → dropped(post-1.0: alt encoding framing; SOFH W-016 tracks SOFH framing) |
| §7.1 | Use of Attachment group | Y | — | out-of-scope → dropped(post-1.0: attachment group) |

---

## FIXT.1.1 (FIXT)

FIXT.1.1 is not a standalone online document; its normative session rules are unified into FIX-SL §4. FIXT-specific application version negotiation sections are §4.3.7, §4.3.8, §5.3. Key FIXT rows are indexed under FIX-SL above.

| FIXT Concept | FIX-SL Section | Catalogue IDs | Gap note |
|---|---|---|---|
| BeginString=FIXT.1.1 — session profile identification | §5.3.1 | S-020 | — |
| DefaultApplVerID(1137) on Logon — default app version | §4.3.7, §5.3.3 | S-025 | — |
| ApplVerID(1128) per message — per-message app version override | §4.3.7, §5.3.5 | S-026 | — |
| NoMsgTypes on Logon — supported message types advertisement | §4.3.8 | S-037 | MISSING → row added (S-037) |
| Multiple application version support | §5.3.2 | S-026 | — |
| Extension pack precedence rules | §5.3.6 | — | out-of-scope → dropped(post-1.0: EP precedence) |
| Custom application version | §5.3.7 | — | out-of-scope → dropped(post-1.0) |

---

## FIXS RC1 (FIXS)

Section structure sourced from fixtrading.org/standards/fixs-online/ (v1.1 RC1, confirmed 2026-05-07).

| Section | Title | Normative? | Catalogue IDs | Gap note |
|---|---|---|---|---|
| §1 | Introduction | N | — | informative |
| §1.1 | Scope | N | T-002 | informative |
| §1.2 | An overview of TLS | N | — | informative background |
| §1.3 | Network topologies and perspectives | N | T-008, T-009, T-010 | informative; normative rules in §2 |
| §1.4 | When and where to use FIXS | N | T-002 | informative |
| §1.5 | References | N | — | bibliography |
| §2 | Authentication Methods | Y | T-008, T-009, T-010, T-011, T-012 | — |
| §2.1 | Recommended authentication and key exchange methods | Y | T-008, T-009 | — |
| §2.2 | Mutual and Simple TLS protocol options | Y | T-008, T-009, T-010 | — |
| §2.3 | Leaf Certificate Pinning | Y | T-008, T-011 | — |
| §2.4 | Certificate Validation with CA Pinning | Y | T-009 | — |
| §2.5 | Pre-shared keys (PSKs) | Y | T-012 | — |
| §2.6 | FIX authentication (FIXA) | Y | S-022 | FIXA details not yet public; S-022 covers Logon credentials |
| §3 | Protocol Parameters | Y | T-006, T-007, T-013 | — |
| §3.1 | Protocol version (TLS 1.2 / 1.3 only; prohibit TLS 1.0/1.1/SSL) | Y | T-006, T-007 | — |
| §3.2 | Protocol features (compression disabled, renegotiation disabled, session caching) | Y | T-006, T-007 | — |
| §3.3 | Cipher suites (AES-GCM, CHACHA20, ECDHE; prohibit RC4, DES, anon, MD5) | Y | T-013 | — |
| §3.4 | Certificate parameters (RSA 2048-bit min, ECDSA 256-bit, X.509, expiration) | Y | T-039 | MISSING → row added (T-039) |
| §3.5 | PSK properties (32-char min, out-of-band exchange, multiple simultaneous PSKs) | Y | T-012 | — |
| §3.6 | Application specific TLS (ALPN / SNI hooks) | Y | — | out-of-scope → dropped(post-1.0: ALPN/SNI application TLS) |
| §4 | Policies and Management | Y | T-011, T-012 | — |
| §4.1 | Sharing secrets (approved channels: HTTPS, GnuPG, PKCS#12, postal, in-person) | Y | T-040 | MISSING → row added (T-040) |
| §4.2 | Storing secrets (private keys, PSKs, pinned certs) | Y | T-040 | covered by T-040 |
| §4.3 | Renewing secrets (rotation support; multiple simultaneous during rotation) | Y | T-011 | — |
| §4.4 | Authorization linked to authentication (auth'd TLS identity ↔ FIX CompID) | Y | T-041 | MISSING → row added (T-041) |
| Appendix A | Cipher Suites (reference table) | N | T-013 | informative |
| Appendix B | Relevant RFCs | N | — | informative |
| Appendix C | Known Vulnerabilities | N | — | informative |
| Appendix D | TLS Implementations | N | — | informative |

---

## FIX Application Layer (FIX40–FIX50SP2)

Indexed at message granularity (one row per MsgType) plus general rules sections.
The application spec is version-specific (FIX 4.0 through FIX 5.0SP2); sections referenced by functional area as common across versions.

### General Encoding Rules (common to all app versions)

| Section | Title | Normative? | Catalogue IDs | Gap note |
|---|---|---|---|---|
| §3 (wire) | Tag=Value encoding — field format, SOH delimiter, ASCII | Y | W-001 | — |
| §3.1 | Standard header (BeginString/8, BodyLength/9, MsgType/35, mandatory ordering) | Y | W-002, W-004, W-005 | — |
| §3.1 | Standard trailer (CheckSum/10 mandatory last) | Y | W-003 | — |
| §3.2 | Repeating groups (NoXxx delimiter, ordered field list, nested groups) | Y | W-006, W-007, D-010 | — |
| §3.3 | Field data types (28 types: STRING, CHAR, INT, FLOAT, BOOLEAN, UTCTIMESTAMP, etc.) | Y | W-009 | — |
| §3.3 | Data (raw bytes) field pairs (Length+Data atomicity) | Y | W-008 | — |
| §3 | Message framing — pipelined messages on TCP stream | Y | W-010 | — |
| §3 | Message serializer — BodyLength + CheckSum computation | Y | W-013 | — |
| §3 | Message validator — required fields, type conformance, enum values, group structure | Y | W-014 | — |

### Session-Layer Messages (MsgType catalogue, all versions)

| MsgType | Message | Catalogue ID | Gap note |
|---|---|---|---|
| 0 | Heartbeat | S-003 | — |
| 1 | TestRequest | S-004 | — |
| 2 | ResendRequest | S-005 | — |
| 3 | Reject | S-007 | — |
| 4 | SequenceReset | S-006 | — |
| 5 | Logout | S-002 | — |
| A | Logon | S-001 | — |

### Application Messages — Pre-Trade (FIX 4.0+)

| MsgType | Message | Catalogue ID | Gap note |
|---|---|---|---|
| 6 | IndicationOfInterest | R-002 | — |
| 7 | Advertisement | R-003 | — |
| B | News | R-004 | — |
| C | Email | R-005 | — |
| R | QuoteRequest | M-010 | — |
| S | Quote | M-009 | — |
| Z | QuoteCancel | M-011 | — |
| a | QuoteStatusRequest | M-012 | — |
| b | MassQuoteAcknowledgement | M-008 | — |
| i | MassQuote | M-008 | — |
| AG | QuoteRequestReject | M-010 | — |
| AH | RFQRequest | A-021 | — |
| AI | QuoteStatusReport | A-021 | — |
| AJ | QuoteResponse | A-021 | — |

### Application Messages — Market Data (FIX 4.2+)

| MsgType | Message | Catalogue ID | Gap note |
|---|---|---|---|
| V | MarketDataRequest | M-001 | — |
| W | MarketDataSnapshotFullRefresh | M-002 | — |
| X | MarketDataIncrementalRefresh | M-003 | — |
| Y | MarketDataRequestReject | M-004 | — |
| e | SecurityStatusRequest | M-006 | — |
| f | SecurityStatus | M-006 | — |
| g | TradingSessionStatusRequest | M-007 | — |
| h | TradingSessionStatus | M-007 | — |

### Application Messages — Reference Data

| MsgType | Message | Catalogue ID | Gap note |
|---|---|---|---|
| c | SecurityDefinitionRequest | M-005 | — |
| d | SecurityDefinition | M-005 | — |
| v | SecurityTypeRequest | A-025 | — |
| w | SecurityTypes | A-025 | — |
| x | SecurityListRequest | A-025 | — |
| y | SecurityList | A-025 | — |
| z | DerivativeSecurityListRequest | A-026 | — |
| AA | DerivativeSecurityList | A-026 | — |
| BK | SecurityListUpdateReport | A-029 | — |
| BP | SecurityDefinitionUpdateReport | A-029 | — |
| BR | DerivativeSecurityListUpdateReport | A-026 | — |
| BI | TradingSessionListRequest | A-027 | — |
| BJ | TradingSessionList | A-027 | — |
| BS | TradingSessionListUpdateReport | A-027 | — |
| BT | MarketDefinitionRequest | A-028 | — |
| BU | MarketDefinition | A-028 | — |
| BV | MarketDefinitionUpdateReport | A-028 | — |

### Application Messages — Trade (FIX 4.0+)

| MsgType | Message | Catalogue ID | Gap note |
|---|---|---|---|
| D | NewOrderSingle | A-001 | — |
| E | NewOrderList | A-002 | — |
| F | OrderCancelRequest | A-003 | — |
| G | OrderCancelReplaceRequest | A-004 | — |
| H | OrderStatusRequest | A-005 | — |
| 8 | ExecutionReport | A-006 | — |
| 9 | OrderCancelReject | A-007 | — |
| K | ListCancelRequest | A-019 | — |
| L | ListExecute | A-019 | — |
| M | ListStatusRequest | A-019 | — |
| N | ListStatus | A-019 | — |
| Q | DontKnowTrade | A-015 | — |
| k | BidRequest | A-020 | — |
| l | BidResponse | A-020 | — |
| m | ListStrikePrice | A-020 | — |
| q | OrderMassCancelRequest | A-008 | — |
| r | OrderMassCancelReport | A-009 | — |
| s | NewOrderCross | A-016 | — |
| t | CrossOrderCancelReplaceRequest | A-012 | — |
| u | CrossOrderCancelRequest | A-013 | — |
| AB | NewOrderMultileg | A-017 | — |
| AC | MultilegOrderCancelReplace | A-011 | — |
| AF | OrderMassStatusRequest | A-010 | — |
| BN | ExecutionAcknowledgement | A-018, A-024 | note: A-018 and A-024 are duplicates (same MsgType BN); see gap note |
| BZ | OrderMassActionReport | A-023 | — |
| CA | OrderMassActionRequest | A-023 | — |

### Application Messages — Post-Trade (FIX 4.0+)

| MsgType | Message | Catalogue ID | Gap note |
|---|---|---|---|
| J | AllocationInstruction | P-001 | — |
| P | AllocationInstructionAck | P-002 | — |
| T | SettlementInstructions | P-006 | — |
| o | RegistrationInstructions | R-001 | — |
| p | RegistrationInstructionsResponse | R-001 | — |
| AD | TradeCaptureReportRequest | P-008 | — |
| AE | TradeCaptureReport | P-008 | — |
| AK | Confirmation | P-005 | — |
| AL | PositionMaintenanceRequest | C-002 | — |
| AM | PositionMaintenanceReport | C-002 | — |
| AN | RequestForPositions | C-002 | — |
| AO | RequestForPositionsAck | C-002 | — |
| AP | PositionReport | C-002 | — |
| AQ | TradeCaptureReportRequestAck | P-008 | — |
| AR | TradeCaptureReportAck | P-008 | — |
| AS | AllocationReport | P-003 | — |
| AT | AllocationReportAck | P-004 | — |
| AU | ConfirmationAck | P-005 | — |
| AV | SettlementInstructionRequest | P-007 | — |
| AW | AssignmentReport | A-022 | — |
| AX | CollateralRequest | C-001 | — |
| AY | CollateralAssignment | C-001 | — |
| AZ | CollateralResponse | C-001 | — |
| BA | CollateralReport | C-001 | — |
| BB | CollateralInquiry | C-001 | — |
| BG | CollateralInquiryAck | C-001 | — |
| BH | ConfirmationRequest | P-005 | — |
| BL | AdjustedPositionReport | C-002 | — |
| BM | AllocationInstructionAlert | A-031 | — |
| BO | ContraryIntentionReport | A-022 | — |
| BQ | SettlementObligationReport | A-030 | — |

### Application Messages — Infrastructure (FIX 4.2+)

| MsgType | Message | Catalogue ID | Gap note |
|---|---|---|---|
| j | BusinessMessageReject | A-014 | — |
| n | XMLnonFIX | A-034 | — |
| BC | NetworkCounterpartySystemStatusRequest | N-001 | — |
| BD | NetworkCounterpartySystemStatusResponse | N-001 | — |
| BE | UserRequest | N-002 | — |
| BF | UserResponse | N-002 | — |
| BW | ApplicationMessageRequest | N-003 | — |
| BX | ApplicationMessageRequestAck | N-003 | — |
| BY | ApplicationMessageReport | N-003 | — |
| CB | UserNotification | A-032 | — |
| CC | StreamAssignmentRequest | A-033 | — |
| CD | StreamAssignmentReport | A-033 | — |
| CE | StreamAssignmentReportACK | A-033 | — |
| CQ | AccountSummaryReport | C-003 | — |

### Duplicate MsgType A-018 / A-024

Both A-018 and A-024 in the catalogue reference MsgType BN (ExecutionAcknowledgement). A-024 is a duplicate entry from a later pass. A-018 is the canonical row; A-024 is a dropped duplicate. See Gap Summary.

---

## FIX Session Test Cases (FIX-TC)

20 test scenarios defined (15 mandatory, 5 optional). All confirmed mapped to TC-001–TC-017.

| Scenario | Title | Mandatory? | Catalogue IDs | Gap note |
|---|---|---|---|---|
| §4 | FIX session layer test cases (container section) | Y | TC-001–TC-017 | — |
| 1B | Connect and Send Logon (Initiator) | Y | TC-001 | — |
| 1S | Receive Logon (Acceptor) | Y | TC-001 | — |
| 2S | Receive any message other than Logon | Y | TC-002 | — |
| 2 | Receive Message Standard Header | Y | TC-002 | — |
| 3 | Receive Message Standard Trailer | Y | TC-003 | — |
| 4 | Send Heartbeat message | Y | TC-004 | — |
| 5 | Receive Heartbeat message | Y | TC-004 | — |
| 6 | Send Test Request | Y | TC-004 | — |
| 7 | Receive Reject message | Y | TC-005 | — |
| 8 | Receive Resend Request message | Y | TC-006 | — |
| 9 | Synchronize sequence numbers | Optional | TC-014 | — |
| 10 | Receive Sequence Reset (Gap Fill) | Y | TC-007 | — |
| 11 | Receive Sequence Reset (Reset) | Y | TC-008 | — |
| 12 | Initiate logout process | Y | TC-009 | — |
| 13 | Receive Logout message | Y | TC-009 | — |
| 14 | Receive application or session layer message | Y | TC-010 | — |
| 15 | Send application or session layer messages (field ordering) | Optional | TC-015 | — |
| 16 | Queue outgoing messages | Y | TC-011 | — |
| 17 | Support encryption (legacy EncryptMethod) | Optional | TC-017 | — |
| 18 | Support third-party addressing | Optional | TC-016 | — |
| 19 | PossResend handling | Y | TC-012 | — |
| 20 | Simultaneous Resend request | Y | TC-013 | — |

**Conclusion:** All 20 FIX-TC scenarios map to TC-001–TC-017. TC-001 covers 1B+1S; TC-002 covers 2S+2; TC-004 covers 4+5+6; TC-009 covers 12+13. No FIX-TC gaps.

---

## FIX Latest — MsgType entries only (FIX-Latest)

Scope: confirm A-035–A-065 account for all new MsgTypes in FIX Latest not present in FIX 5.0SP2. EP-level field additions to existing messages are a post-1.0 gap (see Post-1.0 Gap Registry).

| MsgType | Message | Catalogue ID | Gap note |
|---|---|---|---|
| CF | PartyDetailsListRequest | A-043 | — |
| CG | PartyDetailsListReport | A-044 | — |
| CK | PartyDetailsListUpdateReport | A-045 | — |
| CL | PartyRiskLimitsRequest | A-053 | — |
| CM | PartyRiskLimitsReport | A-054 | — |
| CN | SecurityMassStatusRequest | A-063 | — |
| CO | SecurityMassStatus | A-064 | — |
| CR | PartyRiskLimitsUpdateReport | A-055 | — |
| CS | PartyRiskLimitsDefinitionRequest | A-056 | — |
| CT | PartyRiskLimitsDefinitionRequestAck | A-057 | — |
| CU | PartyEntitlementsRequest | A-048 | — |
| CV | PartyEntitlementsReport | A-049 | — |
| CW | QuoteAck | A-065 | — |
| CX | PartyDetailsDefinitionRequest | A-046 | — |
| CY | PartyDetailsDefinitionRequestAck | A-047 | — |
| CZ | PartyEntitlementsUpdateReport | A-050 | — |
| DA | PartyEntitlementsDefinitionRequest | A-051 | — |
| DB | PartyEntitlementsDefinitionRequestAck | A-052 | — |
| DC | TradeMatchReport | A-039 | — |
| DE | PartyRiskLimitsReportAck | A-058 | — |
| DF | PartyRiskLimitCheckRequest | A-061 | — |
| DG | PartyRiskLimitCheckRequestAck | A-062 | — |
| DH | PartyActionRequest | A-059 | — |
| DI | PartyActionReport | A-060 | — |
| DJ | MassOrder | A-037 | — |
| DK | MassOrderAck | A-038 | — |
| DO | MarketDataStatisticsRequest | A-041 | — |
| DP | MarketDataStatisticsReport | A-042 | — |
| DR | MarketDataReport | A-040 | — |
| DS | CrossRequest | A-035 | — |
| DT | CrossRequestAck | A-036 | — |

**Conclusion:** All 31 FIX Latest new MsgTypes (A-035–A-065) are accounted for. No FIX-Latest MsgType gaps.

---

## Gap Summary

All gaps identified during Phase 1.6 top-down pass, with resolution.

| Gap | Section | Catalogue resolution |
|---|---|---|
| NoMsgTypes in Logon — no row for §4.3.8 Specifying supported message types | FIX-SL §4.3.8 | **Added S-037** (see catalogue) |
| Application system identification in Logon — no row for §4.3.9 | FIX-SL §4.3.9 | **Added S-038** (see catalogue) |
| Certificate parameters — no row for FIXS §3.4 | FIXS §3.4 | **Added T-039** (see catalogue) |
| Secrets management / distribution — no row for FIXS §4.1–§4.2 | FIXS §4.1, §4.2 | **Added T-040** (see catalogue) |
| Authorization linked to authentication — no row for FIXS §4.4 | FIXS §4.4 | **Added T-041** (see catalogue) |
| FIX-SL §5.3.6 Extension pack precedence | FIX-SL §5.3.6 | dropped(post-1.0: EP precedence rules; no v1.0 implementation) |
| FIX-SL §5.3.7 Custom application version | FIX-SL §5.3.7 | dropped(post-1.0: custom app versions) |
| FIX-SL §7 Attachment group for alt-encoded messages | FIX-SL §7.1 | dropped(post-1.0: SOFH framing tracked in W-016; attachment group not in v1.0 scope) |
| FIXS §3.6 Application-specific TLS (ALPN/SNI) | FIXS §3.6 | dropped(post-1.0: ALPN/SNI hooks) |
| A-018 / A-024 duplicate (both = ExecutionAcknowledgement BN) | App catalogue | **Dropped A-024**: Status=dropped(duplicate: same as A-018) |

---

## Catalogue ID supplemental notes

Notes that supplement specific catalogue rows (`feature-catalogue.md`) without rewriting the row text. These record dispositions that emerged from Phase 2 design decisions and provide the bidirectional-traceability anchor per `[const §VI.4]`.

**D-008 supplemental:** Codegen scope for v1.0 = FIX 4.2, FIX 4.4, FIX 5.0 SP2, FIXT.1.1. Runtime-XML-only scope = FIX 4.0, FIX 4.1, FIX 4.3, FIX 5.0, FIX 5.0 SP1. The row title in `feature-catalogue.md` covers the broader 4.0–5.0 SP2 surface; codegen vs runtime-XML disposition lives here, in the coverage index. Per `[2c §1.3]` and `[2c Appendix A]`. Source: 2c v1.3 sign-off (2026-05-08); see `[2c Appendix D §2]`.

**NFR-015 supplemental:** Pluggable Clock interface — `fixpp::core::Clock` (4 pure-virtual methods: `now`, `steady_now`, `sleep_until`, `cancel_sleeps`) carried by `EngineConfig`. Source spec sections: `[arch §1.1] Goals` (pluggable clocks promise) and `[2d §4.1] fixpp::core::Clock — interface, lifetime, threading`. Default impl `fixpp::core::system_clock_source` per `[2d §4.2]` (per-session reusable `steady_timer` slot keyed by `Session*` from `session_arena`); test impl `fixpp::core::mock_clock` per `[2d §4.3]` (pimpl per `[const §XI.3]`). The `effective_clock = SessionConfig::clock_override ?: EngineConfig::clock` rule (per `[2d §7.9]`) routes heartbeat (S-003 / S-004), SendingTime, S-035 session scheduling, and session-scoped LOG/OBS records through the per-session clock; engine-scope LOG/OBS records read `EngineConfig::clock` directly and carry a `clock_scope = engine` discriminator. NFR-015 covers the **clock seam only**; the consuming-row owners (the session-module Phase-4 spec for S-003/S-004/S-035, **2k** for LOG-001..004 + OBS-001..003) discharge their own rows. Source: 2d v0.4 sign-off (2026-05-08); see `[2d §11]` drop-in language and `[2d Appendix A]`.

**NFR-016 supplemental:** Awaitable mutex `fixpp::sync::async_mutex` — own implementation per `[SYN §3.2 Q6b]` (BSL-1.0 algorithm attribution to avast/asio-mutex; cppcoro / Lewis-Baker `std::atomic<uintptr_t>` state with three-state `not_locked`/`locked_no_waiters`/pointer-to-LIFO encoding + mutex-owned `next_drain_head_` residual FIFO chain; per-waiter three-state `std::atomic<waiter_phase>` machine `{ queued, granted, cancelled }` arbitrating unlock/cancel CAS with WINNER-ONLY post-CAS `*result_` writes per v1.4 CAS-then-publish). Source spec sections: `[arch §1.1] Goals` (concurrency primitives promise) and `[2f §4.1] fixpp::sync::async_mutex class — public surface`. The six-item design list per `[SYN §3.2 Q6b]` is delivered via `[2f §4.2]` (waiter embedded in awaiter inside the caller's coroutine frame), `[2f §4.3]` (PMR-aware fallback via explicit `async_lock(mr)` overload + session-side helper `async_lock_via_session_executor`), `[2f §4.5]` (ASIO `cancellation_type::total` honoured via per-waiter `phase_` CAS to `cancelled`; awaitable completes with `expected_t::unexpected{sync_lock_aborted}` at the 2f boundary, mapped to `FIXPP_ERR_CANCELLED` at the C ABI per `[2d §6.7]`), `[2f §4.6]` (per-mutex `dispatch`/`post` policy with default `dispatch` and ASIO `running_in_this_thread()` predicate), `[2f §4.7]` (`std::terminate()` precondition on destruction + explicit mutex-owned `cancel_and_drain()` drain primitive with lazy `std::atomic<std::shared_ptr<detail::drain_latch_state>> drain_latch_ptr_` non-expiring during the drain epoch, published before `draining_` per v1.4 / I-1; `signal_release()` + `signal_abort()` + `notify()` per v1.5 / I-7 / I-8), and `[2f §9]` test seams (≥ 30 covering FIFO fairness across drain cycles, cancellation mid-wait, destructor-with-waiters, contention stress, TSan + ASan clean, plus the v1.4 / v1.5 seams covering CAS-then-publish arbitration, deterministic latch publication, subscriber-wake-on-reaper-abort, and `notify()` non-terminal wake). Locked executor-compat surface per `[2d §7.4]` (completion on awaiter's bound executor; honours `cancellation_type::total`; default `dispatch`); cross-doc amendments to `[2d §4.5]` (engine-internal `Session::session_arena()` accessor), `[2d §4.7]` (per-mode effect-table row + paragraph contract on `expected_t::unexpected{sync_lock_aborted}` cancellation outcome), and `[2d §7.4]` (locked surface bullet rewording) applied at sign-off per `[2f Appendix D §D.1–§D.3]`. Direct consumers: `MessageStore` writer mutex per `[2e §6.4]` (the named hard hand-off gate from `[2e §3.1]` last bullet), pinset rotation per `[2g]`, seqnum counter per Phase-4 session-module spec. CI enforcement of `[const §XV.9]` `std::mutex`-in-coroutine-context ban via `tools/check_no_std_mutex_in_awaitable_headers.sh` grep gate (clang-tidy custom check is post-v1). NFR-016 is the **primitive seam**; no other catalogue rows discharge through it (the consuming-row owners discharge their own rows). Source: 2f v1.5 sign-off (2026-05-08); see `[2f §11]` drop-in language and `[2f Appendix A]`.

---

## Post-1.0 Gap Registry

Items that are normative in the spec but explicitly deferred from fixpp v1.0. These do NOT block Phase 2 but must be visible for future planning.

| Item | Spec section | Reason deferred | Target version |
|---|---|---|---|
| EP-level field additions to existing FIX 4.x/5.x messages (Extension Packs EP001 onward) | FIX-SL §5.3.6; FIX Latest EPs | FIX Latest EP fields in existing messages require dynamic dictionary extension beyond v1.0 scope; FIX Latest new MsgTypes (A-035–A-065) are already tracked | v1.2 (FIX Latest app messages milestone) |
| FIXT custom application version identifiers | FIX-SL §5.3.7 | Non-standard; negligible demand vs complexity | post-v1.2 |
| FIX-SL §7 Attachment group (alt-encoded payloads over FIX envelope) | FIX-SL §7 | SOFH framing (W-016) is the practical path; attachment group is legacy | post-v1.1 (SOFH milestone) |
| FIXS §3.6 Application-specific TLS (ALPN / SNI extensions) | FIXS §3.6 | Operational/infrastructure concern; not required by any known FIX venue for v1.0 | v1.3 (if requested) |
| LFIXT succinct mode interoperability (S-028) | FIX-SL §5.4.5 | Non-interoperable with standard FIXT; no known production demand for v1.0 | v1.x (on demand) |
| FIX Orchestra / Rules of Engagement machine-readable format (D-011) | FIX Orchestra spec | Future direction; QuickFIX XML sufficient for v1.0 | v1.2+ |
| FIXP binary session layer framing (W-015) | FIXP spec | Separate protocol; addressed in v1.4 | v1.4 |
| SBE (Simple Binary Encoding) wire format (OSS-012, OSS-013) | Aeron SBE spec | Separate encoding; addressed in v1.3 | v1.3 |
| FAST encoding (OSS-011) | OpenFAST spec | Separate encoding; addressed in v1.5 | v1.5 |
