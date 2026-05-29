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
| §4.8 | Message recovery | Y | S-011, S-012, S-013, S-014 | — |
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
| §6.2 | Message routing — third-party routing (OnBehalfOf/DeliverTo) | Y | S-016, TC-016 | 005-defer: `OnBehalfOfCompID(115)`/`DeliverToCompID(128)` third-party addressing is the non-49/56 portion of S-016 — deferred-with-traceability to a later third-party-addressing feature; 005 owns only the 49/56 point-to-point portion (see 005 ledger below) |
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
| §3.4 | Certificate parameters (RSA 2048-bit min, ECDSA 256-bit, X.509, expiration) | Y | T-039 | covered by `[2g §4.5]` verify_peer (cross-cut with 2h per `[2g §7.1]` / `[2g §A.2]`) |
| §3.5 | PSK properties (32-char min, out-of-band exchange, multiple simultaneous PSKs) | Y | T-012 | — |
| §3.6 | Application specific TLS (ALPN / SNI hooks) | Y | — | out-of-scope → dropped(post-1.0: ALPN/SNI application TLS) |
| §4 | Policies and Management | Y | T-011, T-012 | — |
| §4.1 | Sharing secrets (approved channels: HTTPS, GnuPG, PKCS#12, postal, in-person) | Y | T-040 | covered by `[2g §4.1]` cert_source::load_credentials + `[2g §4.2]` file_cert_source |
| §4.2 | Storing secrets (private keys, PSKs, pinned certs) | Y | T-040 | covered by T-040 |
| §4.3 | Renewing secrets (rotation support; multiple simultaneous during rotation) | Y | T-011 | — |
| §4.4 | Authorization linked to authentication (auth'd TLS identity ↔ FIX CompID) | Y | T-041 | covered by `[2g §4.5]` peer_identity (cross-cut with session-module Phase-4 per `[2g §7.2]` / `[2g §A.2]`) |
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

<!-- W-001..W-014 supplemental note (T057, 2026-05-17): the wire-encoding rows above
     are delivered by 004-wire-codec (PR #68). Bidirectional traceability:
     [FIX50SP2 §3]   ↔ W-001 (Tag=Value), W-010 (framing), W-013 (serializer), W-014 (validator);
     [FIX50SP2 §3.1] ↔ W-002 (header), W-003 (trailer), W-004 (BodyLength), W-005 (CheckSum);
     [FIX50SP2 §3.2] ↔ W-006/W-007 (repeating + nested groups);
     [FIX50SP2 §3.3] ↔ W-008 (Length+Data), W-009 (field data types).
     W-011 (zero-copy parser) / W-012 (offset-table) are `[impl] implementation NFR` rows —
     no FIX-spec section maps to them by design; their verifying tests are recorded in
     feature-catalogue.md. W-009's wire FLOAT-accessor leg (the 001-core-decimal 2b deferral)
     is closed by 004 T027. Verified GREEN per .specify/decisions/004-wire-codec-verify.md
     (run-2 FINAL @ ce1d4d2); Gate A converged r1; Gate B PR #68 converged 2026-05-17 (gate-b-done, HEAD 8253ef7, P1=0/P2=0/P3=0). -->

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
| 8 | Receive Resend Request message | Y | TC-006 | 005-defer: recovery-dependent — deferred-with-traceability to the later session-recovery feature (see 005 ledger below) |
| 9 | Synchronize sequence numbers | Optional | TC-014 | 005-defer: recovery-dependent — deferred-with-traceability to the later session-recovery feature (see 005 ledger below) |
| 10 | Receive Sequence Reset (Gap Fill) | Y | TC-007 | 005-defer: recovery-dependent — deferred-with-traceability to the later session-recovery feature (see 005 ledger below) |
| 11 | Receive Sequence Reset (Reset) | Y | TC-008 | 005-defer: recovery-dependent — deferred-with-traceability to the later session-recovery feature (see 005 ledger below) |
| 12 | Initiate logout process | Y | TC-009 | — |
| 13 | Receive Logout message | Y | TC-009 | — |
| 14 | Receive application or session layer message | Y | TC-010 | 005-partial: scenario-14 corpus is exactly `14a`–`14j`; 005 ships `14a`–`14g` (session-layer reject taxonomy) in scope; `14h_RepeatedTag`/`14i_RepeatingGroupCountNotEqual`/`14j_OutOfOrderRepeatingGroupMembers` are repeating-group/repeated-tag dictionary-validation cases deferred-with-traceability (see 005 ledger below) |
| 15 | Send application or session layer messages (field ordering) | Optional | TC-015 | — |
| 16 | Queue outgoing messages | Y | TC-011 | — |
| 17 | Support encryption (legacy EncryptMethod) | Optional | TC-017 | — |
| 18 | Support third-party addressing | Optional | TC-016 | — |
| 19 | PossResend handling | Y | TC-012 | 005-defer: PossDup/PossResend (S-010) recovery-dependent — deferred-with-traceability to the later session-recovery feature (see 005 ledger below) |
| 20 | Simultaneous Resend request | Y | TC-013 | — |

**Conclusion:** All 20 FIX-TC scenarios map to TC-001–TC-017. TC-001 covers 1B+1S; TC-002 covers 2S+2; TC-004 covers 4+5+6; TC-009 covers 12+13. No FIX-TC catalogue-mapping gaps. **Per-feature delivery scope is not full per PR:** feature `005-session-establishment-fsm` ships only the capability-partitioned in-scope subset green and records the rest deferred-with-traceability — see the **005 session-establishment — scope-deferral ledger** below (these are recorded, traceable scope deferrals, not silent omissions).

---

## 005 session-establishment — scope-deferral ledger

> Recorded for `[const §I.4]` (no silent omission) and the `[const §VII.5]` Gate-A blocker waiver (Art XVII §1) carried by feature `005-session-establishment-fsm`. Feature `005` ships only the in-scope `[FIX-TC]` subset green this PR; the entries below are deferred-with-traceability to the named successor work. `[const §VII.5]` (full TC corpus per PR) is NOT satisfied by 005 and proceeds under an explicit recorded Gate-A blocker waiver (`[const §XVII.1]`, `constitution.md:255`); see `specs/005-session-establishment-fsm/plan.md` Constitution Check + Complexity Tracking.

| Deferred item | Catalogue / scenario | Reason | Discharged by |
|---|---|---|---|
| Too-high `MsgSeqNum` oracle cases (`1a_ValidLogonMsgSeqNumTooHigh`, `2b_MsgSeqNumTooHigh`) | TC-001/TC-002 (QFJ `fix42`/`fix44`) | Require the deferred `ResendRequest(35=2)` to pass the QFJ comparison; 005 treats too-high as session-fatal (Logout-with-text → disconnect), recovery is out of scope | later session-recovery feature |
| ResendRequest / SequenceReset-GapFill / SequenceReset-Reset / synchronize-seqnums | scenarios 8, 10, 11, 9 → TC-006/TC-007/TC-008/TC-014 | Recovery-dependent (gap-fill / store-recovery) | later session-recovery feature |
| PossDup / PossResend duplicate semantics (S-010) | scenario 19 → TC-012 | Recovery-dependent duplicate handling | later session-recovery feature |
| Scenario-14 repeating-group/repeated-tag cases `14h`/`14i`/`14j` | scenario 14 → TC-010 | Repeating-group / repeated-tag dictionary-validation territory, not session-layer reject taxonomy; 005 ships `14a`–`14g` | later dictionary-validation / wire follow-up |
| S-016 third-party addressing `OnBehalfOfCompID(115)`/`DeliverToCompID(128)` | `[FIX-SL §6.2]`, scenario 18 → S-016/TC-016 | Separable session-routing work; 005 owns only the 49/56 point-to-point portion of S-016 | later third-party-addressing feature |
| Version-scope: FIX.4.0/4.1/4.3/5.0 establishment + FIXT.1.1/5.0SP2 (no oracle dir; `DefaultApplVerID(1137)`/`[FIX-SL §4.4]`) | S-001/S-008/S-009/S-015/S-016/S-019/S-020 | QFJ oracle has no `fixt11`/`fix50sp2` dir; 4.0/4.1/4.3/5.0 are runtime-XML-only with no typed namespace in v1.0 (`[const §I.1]`); 005 validates FIX.4.2/4.4 only | later version-coverage / FIXT work |

> **Per-row delivery scope (delivered slice).** Catalogue rows `S-001`, `S-002`, `S-003`, `S-004`, `S-007`, `S-008`, `S-009`, `S-015`, `S-016`, `S-019`, `S-020` — and the folded `core/` time-helper row #4 (`utc_time_to_fix_string` / `fix_string_to_utc_time` for `SendingTime(52)`) — are delivered by 005 as the **FIX.4.2/4.4 point-to-point establishment slice only**, not their full version span. `S-016` is delivered as the 49/56 portion only; the `OnBehalfOfCompID(115)`/`DeliverToCompID(128)` portion is the third-party-addressing deferral above. FIXT.1.1 / 5.0SP2 establishment is **explicitly NOT claimed** by 005 (FR-017 + SC-001 — the QFJ oracle has no `fixt11`/`fix50sp2` directory and FIXT logon-time semantics, including `DefaultApplVerID(1137)`, are deferred).
>
> **Ledger close-out (2026-05-22, Phase 8 T065).** The entries above are complete against the Session-2026-05-17 Q2 → Session-2026-05-18 re-scope and the Gate-A `[const §VII.5]` Article-XVII §1 recorded waiver carried by 005. No further deferrals are introduced by 005 Phases 1–8; any successor session-recovery / version-coverage / third-party-addressing feature discharges its rows by amending this ledger at merge.
>
> **009-session-fsm-finalize close-out (2026-05-23, T029).** Closed the FR-001 / FR-002 / FR-003 / FR-011 / FR-013 binding-contract drifts on 005's rows per `[[project_005_phase8_completeness_false_pass]]` (PR #81 round-1 hostile-review findings). 005 deferral ledger unchanged: no new deferral row, no green-via-009 of a deferred case. Slice scope was implementation-drift closure against the Gate-A-converged 005 design (Gate A not re-run; the design is unchanged). See `.specify/decisions/009-session-fsm-finalize-verify.md` for the YELLOW non-RED verify verdict; one pre-existing 005-baseline ASan UAF (`src/session/session.cpp:116`, `Session::open` config-ref lifetime) recorded as W-5 for a follow-on slice.
>
> **010-session-cfg-lifetime close-out (2026-05-23, T029).** Closed the W-5 + F-04 + F-05 + F-06 + F-07/E1 + F-11 + RC#G-mixed-path waivers on PR #82 Gate B (009 decision record). W-5 = pre-existing 005-baseline ASan stack-use-after-scope on `Session::cfg_` (declared `const SessionConfig& cfg_` at `include/fixpp/session/session.hpp:312`, dangling-dereferenced at `src/session/session.cpp:116`); resolved at `/speckit-clarify` 2026-05-23 with Option A (by-value `SessionConfig cfg_;`) — implementation discovery (mid-/implement T006) required FR-001a amendment: `SessionConfig::store_factory` flipped from `unique_ptr<MessageStoreFactory>` to `shared_ptr<MessageStoreFactory>` to make `SessionConfig` copy-constructible (the factory is a stateless interface; sharing across Sessions is meaningful; per-Session MessageStore uniqueness preserved). Gate A NOT re-run — inherited from 005 with a 010-specific addendum (`.specify/decisions/010-session-cfg-lifetime-gatea.md`, local-only). The `session_coverage_adversarial` ASan-only test-skip added in PR #82 as the W-5 carry-forward is now REMOVED. Adds one new public C++ symbol (`Session::fsm_visit_history()`) and one new error variant slot (`error::session_invalid_state_for_send = 77`). 005 design unchanged otherwise. See `.specify/decisions/010-session-cfg-lifetime-verify.md` for the verify verdict; the 009 W-1..W-4 coverage carry-forwards re-waived per `[[feedback_codecov_patch_vs_lcov_da_brda_gate]]` + PR #73 precedent.
>
> **010 /simplify Wave 1-3 + F4 close-out (2026-05-23).** A second pass (`/simplify` with 3 review agents) caught the 13th instance of the completeness-PASS-as-hypothesis burn class (`[[project_005_phase8_completeness_false_pass]]` + `[[feedback_simplify_pass_catches_9th_burn]]`) recurring inside this very slice. All 11 findings remediated in-slice; see `specs/010-session-cfg-lifetime/simplify-fix-plan.md` for the full execution log. Notable in-slice closures: (a) **B-1** Session ctor `noexcept` removed — UB-class trap (copy ctor can throw via `std::string` / `std::function` / `std::shared_ptr` allocations → `std::terminate`); matches `close()` precedent. (b) **B-8 / FR-009** initiator handshake symmetry — `Session::open()`'s 3 emit-failure branches now `record_state_transition_(Disconnected)` matching the acceptor send-throw witness + the liveness-loop assign-failure precedent. (c) **F4 / W3.3-final** — the pre-existing 005 spec-vs-impl gap on `"A"` (dup-Logon-in-Active) + `"2"` (RR) + `"4"` (SeqReset) in `is_session_admin` is CLOSED in-slice (Codex 2nd-opinion + QuickFIX-cpp + QuickFIX/J survey converged on Position A — see `specs/010-session-cfg-lifetime/codex_f4_review.md`). All three MsgTypes now route through the Reject branch per 005 FR-017 "never silent no-op". **Forward upgrade obligation:** when the deferred session-recovery feature (catalogue row 400 above) lands, the RR/SeqReset cells UPGRADE Reject→Process (gap-fill via the message store), matching QuickFIX-cpp `Session::nextResendRequest` and QuickFIX/J `Session.nextResendRequest`; the dup-Logon cell stays as Reject per 005's intentional defensive divergence from QuickFIX refresh-on-dup-Logon convention.

---

## 013-session-reconnect-binding — Merged (PR #86 squash `bd84e08`, 2026-05-29; Gate B converged 3 rounds, Sonnet 2/2, gate-b-waived)

> Session-Phase-4 surface: reconnect FSM driver + recovery sub-protocol + CompID↔TLS-identity binding + TLS-validation-outcome SessionEvent + in-process credential rotation. Ships catalogue rows **S-005 / S-006 / S-014 / S-024 → `done`** (recovery sub-protocol: ResendRequest issue+reply, SequenceReset-GapFill, ResetSeqNumFlag(141) 3-mode matrix, recovery FSM). Completes co-owned **T-039 / T-040 → `done`** (012 shipped the wiring half; 013 binds TLS outcome → `SessionEvent` and adds in-process `reload_credentials`). **T-041** stays `implementing` — policy/extraction/fail-CLOSED-when-mTLS Logon gate SHIPPED; production `handshake_result.peer_id → Session` source wiring needs the live `TlsTransport` → 014.
>
> **C++ surface published.** `fixpp::session::` additions: `ReconnectFsm` (recovery FSM driver on 005's 6-state `fsm_state` — AwaitingResend transient sub-state per D-1), `ResendState`, `CompIdAuthorizationPolicy` (allow-list, default-deny) + `extract_principal` (canonical CN→SAN-DNS→SAN-URI→SHA-256), `SessionEvent` variant union (5 alternatives: peer_identity_bound / compid_authorization_failed / tls_validation_failed / credentials_rotated / sequence_numbers_reset) + `Session::recent_events()` 16-entry ring, `SeqnumManager::reset_to_one()` (production 141=Y reset), `Session::reload_credentials` forwarder; new pure-virtual `TransportFactory::reload_credentials` (count 1→2, under 5/5 cap). 4 new `error::session_*` slots 116..119 + dual-emission of 005-era slots 73/74.
>
> **Error envelope.** `session_seqnum_reset_mismatch = 116` (FR-017 bilateral_strict), `session_compid_unauthorized = 117` (FR-021), `session_testreqid_mismatch = 118` (FR-006), `session_invalid_argument = 119` (FR-033). Slots 73/74 reused for FR-008/FR-004 FSM-driven timeouts (reference-engine sweep: zero precedent for typed discrimination). **Known carry-forward**: slot-74 `session_test_request_unanswered` is a documented stand-in for the seqnum-too-high path → dedicated `session_seqnum_too_high` deferred to v1.0 taxonomy gate.
>
> **Test surface.** 19 test files in `tests/session/` + `tests/transport/` + `tests/perf/` + `tests/fuzz/`. Highlights: `fsm_matrix_witness_test.cpp` (49 cells / 133 asserts), `test_reset_seqnum_policy_matrix.cpp` (15 cells incl. counter-reset + reply-frame-141Y assertions, false-pass closures), `test_recovery_store_horizon.cpp` (>1024B replay-intact), `test_compid_binding_mtls_fail_closed.cpp` (RC#A fail-CLOSED-when-mTLS), `test_tls_validation_failed_taxonomy.cpp`. All drive bytes through the FSM via `mock_transport::Script` (production-shaped, FR-038). CI matrix GREEN across debug/release/asan/ubsan/tsan/coverage/gcc/python (codecov/patch soft-gate waived per `[[feedback_codecov_patch_vs_lcov_da_brda_gate]]`).
>
> **Gate B (3 rounds, Sonnet 2/2).** RC#A fail-CLOSED mTLS CompID footgun; RC#B >1024B resend-replay truncation; RC#C ResetSeqNumFlag(141) full FR-017/FR-018 conformance (incl. round-2 "lying event" — reset event emitted while counters never reset → production `reset_to_one()`); RC#D logout-timeout config wiring. Post-merge fix `8e2d362`: `[const §XV.9]` corpus regression (session.hpp awaitable closure dragged tls/pinset.hpp std::shared_mutex via reconnect_fsm.hpp → fixed by fwd-decl). 4 waivers (ReconnectFsm stubs → 014; fabricated-payload P3; slot-74 stand-in + doc drift; T-041 full wiring → 014). Record: `.specify/decisions/013-session-reconnect-binding-gateb.md` + phase-4 lifecycle doc.

---

## 012-2h-transport — Merged (PR #85 squash `53e25b1`, 2026-05-27; Gate B converged 4 rounds with 3 carry-forward waivers)

> Established the new `include/fixpp/transport/` + `src/transport/` module. Ships catalogue rows **T-001 / T-002 / T-003 / T-004 / T-005 / T-009 / T-010 → `done`** (post-MVP US2/US3/US4 + Gate B RC#A/RC#B closure landed everything originally deferred). Co-owns **T-006 / T-007 / T-008 / T-011 / T-013** (TLS protocol + cipher + pinset; 011 shipped surface, 012 ships wire) — flipped from `implementing` to `done`. **T-039 / T-040** stay `implementing` as "2h-owned wiring half SHIPPED" — full row pending session-Phase-4 binding into `SessionEvent`. **T-041** stays `backlog` pending session-Phase-4.
>
> **Final scope (cumulative across MVP + post-MVP slices + Gate B).** US1 (P1 TLS-encrypted FIX session end-to-end) + US2 (ReconnectPolicy schedule-array shape Q2=C + `delay_for_attempt(n)` + `defaults_quickfix_compat()`) + US3 (Listener + asio_listener with listener-owned cached SSL_CTX factory per RC#B Codex r3) + US4 (mock_transport test seam) + Phase 7 011 F-1 carryover discharged (8-cell × 4-sanitizer = 32 PASS witness) + Appendix D §D.1..§D.8 cross-doc amendments (T046-T048 in commit `81ab659`) + operator-quickstart `docs/src/transport-quickstart.md` (T051) + RC#A live TLS handshake Cells 7-8 + RC#D live truncated-close witness + RC#E FR-007 exclusivity Cells 1-4 + RC#F seam-13 D-17 reset + shared `tests/transport/loopback_tls_fixture.hpp`.
>
> **C++ surface published (MVP slice).** Six public types under `fixpp::transport::`:
> - `Transport` (abstract; 5 pure-virtuals — `async_connect` / `async_read_some` / `async_write` / `cancel` / `close`) + nested `Transport::Config` + `ConnectInfo` POD per `[2h §4.1]`.
> - `TlsTransport` (virtually inheriting `Transport`; 1 additional pure-virtual `async_handshake`) + `handshake_result` owning POD (peer_id by value, captured_pinset shared_ptr, negotiated_cipher pmr::string) per `[2h §4.2]`.
> - `Endpoint` value type — `{host, port, backlog=128}`, IPv6 zone-id admitted per FR-018 + `[2h §4.3]`.
> - `TransportFactory` (abstract; 1 pure-virtual `make(exec, ssl_cfg, mr) noexcept -> expected_t<unique_ptr<Transport>>`) per `[2h §4.7]`.
> - `asio_tls_transport` v1.0 reference impl (1099 LoC `src/transport/asio_tls_transport.cpp`) — OpenSSL `SSL_CTX` configured at construction (`SSL_CTX_set_min/max_proto_version` TLS 1.2/1.3 + ciphersuites + curves + sigalgs + `SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION | SSL_OP_NO_TICKET`; SSL_OP_NO_EARLY_DATA waiver pending — see below); `verify_peer_trampoline` extracts captured pinset via `SSL_set_ex_data` per Appendix D §D.7 and dispatches to `fixpp::tls::verify_peer`; ASIO awaitable surface with `cancellation_type::total` total-reset per D-17; in-flight exclusivity per FR-007; FR-026 factory caching of `SslCtxConfig` + `SSL_CTX*` + PMR root + engine clock; FR-028 destroy-before-mint reconnect contract.
> - `asio_tls_transport_factory` default impl (`src/transport/transport_factory.cpp`) + free `make_asio_tls_transport(exec, cfg, ssl_cfg, mr) noexcept -> expected_t<unique_ptr<Transport>>` wrapper for non-construction-time callers (2i C-ABI / runtime reconnect mint).
>
> **Error envelope.** 22 new `error::transport_*` variants at slots **94..115** per `[2h §6.6]:1167-1204`, partitioned into 5 coalescing groups for C-ABI: **LIFECYCLE** (8 — resolve_failed, connect_refused, connect_timeout, already_connected, already_closed, read_in_progress, write_in_progress, reconnect_limit_exceeded), **IO** (5 — read_eof, read_truncated, read_error, write_short, write_error), **HANDSHAKE** (2 — handshake_failed grouping variant carrying OpenSSL error + tls_* sub-reason per FR-034a, handshake_timeout), **CONFIG** (2 — factory_failed, psk_unsupported), **CANCELLED-reuse** (5 — connect/read/write/handshake/accept_cancelled → reuse `FIXPP_ERR_CANCELLED`). C-ABI coalescing emission is owned by 2i.
>
> **Test surface (MVP slice).** 14 new test binaries in `tests/transport/` + `tests/perf/` + `tests/conformance/` + 3 bench binaries (scaffold bodies — Tier-1 fill-in deferred) + 2 fuzz harnesses (libFuzzer 30s smoke under ASan, no crashes). All 8 wired Phase 3a binaries + 1 Phase 7 binary GREEN under Clang Debug + ASan + UBSan + TSan + GCC Release sanity. Highlights:
> - `tests/transport/test_load_credentials_seam13_witness.cpp` — **8-cell × 4-sanitizer = 32 PASS** witness discharging 011 Gate B F-1 carryover per FR-033 + SC-008 + Clarifications Q3=C. Cases 1-4 (cached-fast-path / cancel-before-pickup / cancel-during-handler / happy) × executor modes (per_session_strand / direct_executor). Cell-by-cell record at `.specify/decisions/012-2h-transport-verify.md §T044`.
> - `tests/transport/test_tls_handshake_pinset_rotation.cpp` — 4 cells (snapshot-stable-after-rotation, two-snapshots-independent, empty-snapshot, handshake_result field check).
> - `tests/transport/test_inflight_exclusivity.cpp` — 3 cells (FR-007 in-flight exclusivity error codes + namespace alias consistency).
> - `tests/perf/test_transport_read_alloc_guard.cpp` — DUAL gate (`counting_resource` PMR routing + mallocnesia weak symbols) per `[[feedback_tracking_pmr_resource_false_pass]]`.
> - `tests/perf/test_socket_option_defaults.cpp` — FR-029 / FR-029a initiator-leg socket option defaults (TCP_NODELAY=true, SO_LINGER disabled, tcp_keepalive=false); acceptor-leg cell deferred to US3.
> - `tests/transport/test_cancellation_propagation.cpp` — 3 runnable error-code cells + 8 DISABLED_ integration cells (4 async methods × 2 executor modes) pending live-wire fixture.
>
> **Waivers (final — applied at Gate B convergence per `[const §IX.1]` rationale-backed):**
> - **W-1 (12) SSL_OP_NO_EARLY_DATA omission** — constant absent in pinned OpenSSL 3.6.2 headers; 0-RTT off by ASIO default. FR-016 + `[FIXS §3.2]` + `[const §XII.3]` anchor.
> - **RC#C depth (Gate B carry-forward)** — `test_verify_peer_pmr_oom.cpp` witness exhausts boundary only; mid/tail SAN-DNS/SAN-URI/peer_identity + `verify_peer_trampoline` 7-PMR-container surface unwitnessed. Test cert has 1 SAN-DNS + 0 SAN-URI. Per `[[feedback_trap_throw_pmr_witness_enumerate_sites]]` + 011 PR #84 W-5/W-6 precedent.
> - **RC#G bench (Gate B carry-forward)** — `bench/transport/bench_tls_handshake_loopback.cpp` is scaffold; SetUp/TearDown TODOs; counters 0. Bench-body fill-in deferred until post-cache shape stable (which round-3 RC#B accept-path close just landed). Per 011 PR #84 W-2 cppcheck-bench precedent.
> - **RC#I fuzz doc (Gate B carry-forward)** — `fuzz_transport_read_path.cpp` documents reduced scope honestly (Framer::feed boundary, not asio_tls_transport::async_read_some); catalogue line label re-classification follow-on slice.
> - **W-4 (12) `asio_free` namespace alias** — `asio::async_connect` (free function) collides with `asio_tls_transport::async_connect` (member); resolved via local `namespace asio_free = asio` alias inside `asio_tls_transport.cpp`. Transparent — no behavior change; cosmetic only.
> - **Inherited from 011 / prior PRs:** W-2 cppcheck + W-3 iwyu + W-format (425 repo-wide) + W-tidy (75 diagnostics) — pre-existing carry-forwards per `[[project_011_tls_policy_closed]]` + 011 PR #84. Repo-wide cleanup tracked as a separate `chore/` follow-on branch off post-012 main.
>
> **011 F-1 closure landed.** Phase 7 `tests/transport/test_load_credentials_seam13_witness.cpp` + Gate B verify record satisfy SC-008 binding. 011's deferred F-1 row flipped at 012 Gate B convergence.
>
> **Cross-cuts forwarded.** T-039 / T-040 wiring half SHIPPED; full rows pending session-Phase-4 (CompID-to-TLS-identity binding + 2j `ReloadCertSource` control-plane). T-041 waits for session-Phase-4.

---

## 011-tls-policy — Active feature (2026-05-24, /speckit-implement Phases 1-6)

> Establishes the new `include/fixpp/tls/` + `src/tls/` module from scratch. Ships catalogue rows T-006 / T-007 / T-008 / T-011 / T-013 as `implementing` (await 2h-transport for `done`). T-039 / T-040 / T-041 stay `backlog` with explicit C++ surface-contract forwarding notes (see `feature-catalogue.md`).
>
> **C++ surface published.** Five public types under `fixpp::tls::`:
> - `cert_source` (abstract; 2 pure-virtuals — `load_credentials` awaitable + `load_trust_anchors`) + `file_cert_source` (PEM/DER default impl with encrypted-PEM passphrase support) + `make_file_cert_source(Config, mr) -> expected_t<shared_ptr<cert_source>>` factory.
> - `Pinset` (mid-session-mutable, `shared_mutex` writer + lock-free `atomic<shared_ptr<const pin_snapshot>>` reader per `[2g §6.2]` / §6.5.2) + `make_pinset(Config, mr) -> expected_t<shared_ptr<Pinset>>` factory.
> - `CipherPolicy` (compile-time allow-list per `[const §XII.3]` / `[2g §4.4]` — 3 TLS-1.3 suites + 6 TLS-1.2 suites + 3 kx_groups + 4 sig_algs + 12 banned_tokens; `static_assert(!any_banned(...))`; runtime `is_allowed(string_view) constexpr noexcept` for 2i C-ABI).
> - `SecurityProfile` (4 enumerators incl. `unset = 0` sentinel + `one_way_ca [[deprecated]] = 3`) + `SslCtxConfig` (carries `pinset_snapshot` per NEW-P1-1 BINDING CONTRACT — verify_peer scans the captured snapshot, never calls `cfg.pinset->find/contains`) + `make_ssl_ctx_config(profile, cs, clock, pinset=nullptr, mr=nullptr) -> expected_t<SslCtxConfig>` factory + `verify_peer(SslCtxConfig const&, span<const Certificate>) noexcept -> expected_t<peer_identity>` predicate (10-step short-circuit per `[2g §6.5.1]` / FR-020a).
> - `Certificate` + `peer_identity` value types (view + owning; `[[clang::lifetimebound]]` at every view accessor's declaration site per `[arch §5.5]` + `[2b §6.4]` precedent).
>
> **Error envelope.** 16 new `error::tls_*` variants at slots 78..93 per `[2g §6.6]` + `/clarify` Q2 amendment (`tls_pin_empty_at_open`). C-ABI coalescing groups owned by 2i: `FIXPP_ERR_TLS_CONFIG` (5 variants) / `FIXPP_ERR_TLS_PINSET` (3) / `FIXPP_ERR_TLS_RUNTIME` (1) / `FIXPP_ERR_TLS_HANDSHAKE` (5 — grouping variant `tls_handshake_failed` + 4 DoS-cap / pinning variants) / `FIXPP_ERR_CANCELLED` (1 — reused for `tls_load_cancelled`).
>
> **Test surface.** 21 new test binaries (14 named in `tests/tls/*` + 2 fuzz/conformance + 5 negative-compile / cancellation / per-counterparty / lifetimebound / pmr-fail witnesses) + 3 bench binaries — all green under Clang Debug + ASan + UBSan + TSan + GCC Release. Dual-gate alloc (`counting_resource` + `mallocnesia` LD_PRELOAD) on `Pinset::find` / `snapshot` / `verify_peer` per `[[feedback_tracking_pmr_resource_false_pass]]`.
>
> **Cross-cuts forwarded.** T-039 / T-040 wait for 2h-transport (handshake wiring) to flip from `backlog` → `implementing` → `done`. T-041 waits for session/ Phase-4 (CompID-to-TLS-identity binding) to consume `peer_identity` on `SessionEvent`. The deferred session-recovery feature's catalogue row 400 is NOT touched by 011.
>
> **Pending Phase 6 close-out (this slice).** T051 `/simplify` and T053 `/speckit-verify` (Tier-1 mirror) remain to run per `[[feedback_speckit_simplify_before_verify]]` ordering — `/simplify` first, then `/speckit-verify`, then `/gate-b`.

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

**NFR-016 supplemental:** Awaitable mutex `fixpp::sync::async_mutex` — own implementation per `[SYN §3.2 Q6b]` (BSL-1.0 algorithm attribution to avast/asio-mutex; cppcoro / Lewis-Baker `std::atomic<uintptr_t>` state with three-state `not_locked`/`locked_no_waiters`/pointer-to-LIFO encoding + mutex-owned `next_drain_head_` residual FIFO chain; per-waiter three-state `std::atomic<waiter_phase>` machine `{ queued, granted, cancelled }` arbitrating unlock/cancel CAS with WINNER-ONLY post-CAS `*result_` writes per v1.4 CAS-then-publish). Source spec sections: `[arch §1.1] Goals` (concurrency primitives promise) and `[2f §4.1] fixpp::sync::async_mutex class — public surface`. The six-item design list per `[SYN §3.2 Q6b]` is delivered via `[2f §4.2]` (waiter embedded in awaiter inside the caller's coroutine frame), `[2f §4.3]` (PMR-aware fallback via explicit `async_lock(mr)` overload + session-side helper `async_lock_via_session_executor`), `[2f §4.5]` (ASIO `cancellation_type::total` honoured via per-waiter `phase_` CAS to `cancelled`; awaitable completes with `expected_t::unexpected{sync_lock_aborted}` at the 2f boundary, mapped to `FIXPP_ERR_CANCELLED` at the C ABI per `[2d §6.7]`), `[2f §4.6]` (per-mutex `dispatch`/`post` completion policy; E-3 errata: waiter resumption always uses `asio::post` — never `dispatch` — so the completion always occurs on a freshly-posted task, eliminating the pre-E-3 `running_in_this_thread()` predicate for resumption scheduling; `dispatch` is retained for the no-waiter uncontended grant path only), `[2f §4.7]` (`std::terminate()` precondition on destruction + explicit mutex-owned `cancel_and_drain()` drain primitive with lazy `std::atomic<std::shared_ptr<detail::drain_latch_state>> drain_latch_ptr_` — on the abort path the latch stays published with `aborted_==true` per I-5/I-7 (E-4 / gate-b/r1 fix) so reentrant callers subscribe and observe the abort outcome rather than false success; on the release path `drain_latch_ptr_` is cleared after `signal_release()` per I-7; published before `draining_` per v1.4 / I-1; `signal_release()` + `signal_abort()` + `notify()` per v1.5 / I-7 / I-8; E-4: `cancellation_slot` has no allocator-binding hook — per-thread recycler owns the slot-closure and drives zero-alloc steady-state), and `[2f §9]` test seams (≥ 32 covering FIFO fairness across drain cycles, cancellation mid-wait, destructor-with-waiters, contention stress, TSan + ASan clean, plus the v1.4 / v1.5 seams covering CAS-then-publish arbitration, deterministic latch publication, subscriber-wake-on-reaper-abort, `notify()` non-terminal wake, reentrant-drain-after-abort false-success regression lock, and in-flight-acquirer/reentrant-drain UAF window). Locked executor-compat surface per `[2d §7.4]` (completion on awaiter's bound executor; honours `cancellation_type::total`; always-post resumption per E-3); cross-doc amendments to `[2d §4.5]` (engine-internal `Session::session_arena()` accessor), `[2d §4.7]` (per-mode effect-table row + paragraph contract on `expected_t::unexpected{sync_lock_aborted}` cancellation outcome), and `[2d §7.4]` (locked surface bullet rewording) applied at sign-off per `[2f Appendix D §D.1–§D.3]`. Direct consumers: `MessageStore` writer mutex per `[2e §6.4]` (the named hard hand-off gate from `[2e §3.1]` last bullet), pinset rotation per `[2g]`, seqnum counter per Phase-4 session-module spec. CI enforcement of `[const §XV.9]` `std::mutex`-in-coroutine-context ban via `tools/check_no_std_mutex_in_awaitable_headers.sh` grep gate (clang-tidy custom check is post-v1). NFR-016 is the **primitive seam**; no other catalogue rows discharge through it (the consuming-row owners discharge their own rows). Source: 2f v1.6 sign-off (errata E-1..E-4; gate-b/r1 F-2/F-4 fix 2026-05-19); see `[2f §11]` drop-in language and `[2f Appendix A]`.

**CA-002 supplemental:** `fixpp_error_t` numeric-block layout per `[2i §4.3]` v0.2: cross-cutting block `[0, 99]` (2i-owned, 11 codes — `FIXPP_ERR_OK` / `_CANCELLED` / `_UNKNOWN` per `[arch §5.3]` plus 8 2i-introduced variants `_NULL_HANDLE` through `_CAPI_CONFIG_INVALID`); WIRE `[100, 199]` (2b-owned, 13 occupied per `[2b §6.7]`); DICT `[200, 299]` (2c-owned, 20 occupied per `[2c §6.7]`); THREAD `[300, 399]` (2d-owned, 9 occupied per `[2d §6.7]` — count includes `dispatch_aborted` which still maps to `FIXPP_ERR_CANCELLED` at the C ABI per `[2i §4.9]`); STORE `[400, 499]` (2e-owned, 10 occupied per `[2e §6.7]`); SYNC `[500, 599]` (2f-owned, 4 occupied per `[2f §6.5]`); TLS `[600, 699]` (2g-owned, 15 occupied per `[2g §6.6]`); TRANSPORT `[700, 799]` (2h-owned, 22 occupied per `[2h §6.6]`); DECIMAL `[800, 899]` (2a-owned, 4 occupied per `[2a §7.4]`); reserved `[900, 1399]` for 2j / 2k / 2l / 2m / post-v1 growth; `[1400+]` reserved for future expansion. Live total of prior-doc variants = 4 + 13 + 20 + 9 + 10 + 4 + 15 + 22 = 97. Stability rule per `[SYN §3.5 #19]` / `[const §X.4]`: once a numeric value is published in a tagged C ABI release, it never changes meaning. Audit trail via `tools/abi_history/error_codes_v1.txt` (append-only); CI verifies no re-definitions. Occupancy drift gate `tools/check_capi_occupancy.sh` mechanically counts sibling `[2X §6.X]` rows and asserts the published counts match. Per-block growth is a domain-doc amendment; cross-block growth is a 2i amendment per `[const §XX]`. Source: 2i v0.3 (2026-05-09); see `[2i §4.3]` numeric-block table and `[2i §4.4]` `fixpp_strerror()` lookup discipline.

**SVC-005 supplemental:** Pluggable control-plane interface — `fixpp::service::ControlPlane` (3 pure-virtual methods: `start`, `stop`, `health`; under the `[const §XIV.2]` ≤ 5 cap with 2 slots reserved for v1.x `RotateAuthToken` / `RemapRpcs` per `[2j §10]` Q5). Source spec sections: `[arch §4.11] service` (the surface inventory) and `[2j §4.1] fixpp::service::ControlPlane — abstract interface`. Default impl `fixpp::service::grpc_control_plane` per `[2j §4.6]` (Unix domain socket on Linux / named pipe on Windows; TCP opt-in per `[arch §8.1]`). The proto schema `service/proto/fixpp_control.proto` per `[2j §4.7]` is on the `[arch §9.3]` "Stable from v1.0" tier; proto-evolution rules pinned in `[2j §4.7.1]` (additive-only expansion via MINOR bumps; removals are MAJOR breaks). v1.0 RPC surface: `OpenSession`, `CloseSession`, `Configure` (reserved-empty per `[2j §4.7.1]` additive expansion path), `StreamMetrics`, `StreamLogs`, `StreamSessionEvents`, `Health` (gRPC standard health-check). `RotatePinset` and `ReloadCertSource` are deferred to v1.x per `[2j §10]` Q1 + Q9 (the v1.0 cross-doc state has no AGPL-legal path: `[2i §2]` non-goal #6 declines the C-ABI rotation surface; `service/grpc/*.cpp` cannot include `<fixpp/tls/...>` per `[arch §8]`). AGPL-boundary structural enforcement per `[2j §4.4]` / `[2j §4.6]` + `tools/check_layers.py` lint per `[arch §8]` enforcement bullet (first-landing tracked at `[2j §10]` Q10). Stream backpressure: close-on-overflow with `control_plane_stream_overflow` per `[2j §4.8]` / `[2j §6.4]` (consistent with `[const §XV.15]` no-drop-oldest-on-app-paths; `[const §XIII.2]` permits but does not require drop-oldest on observability paths — v1.0 picks close-on-overflow for visibility). The proto-stability audit-trail file `tools/abi_history/proto_v1.txt` (NEW at 2j sign-off per `[2j App D §D.3]`) mirrors the `tools/abi_history/error_codes_v1.txt` precedent. Source: 2j v0.3 (2026-05-09); see `[2j §11]` drop-in language and `[2j Appendix A]`.

**PY-bindings supplemental:** Python `fixpp` package — SWIG-generated CPython extension wrapping the C ABI per `[arch §4.12]` / `[arch §8]` AGPL boundary; consumes only `<fix/c_api.h>` (no engine-internal C++ headers per `[arch §9.1]`). Source spec sections: `[arch §1.1] Goals` (Python wheel mandatory) and `[2m §1] Goals` + `[2m §4.1–§4.7] Public Python API surface` + `[2m §6.1–§6.7] Behavioral contract` + `[2m §11] Hand-off`. PY-001 (SWIG / `import fixpp`): `[2m §4.1]` package layout + `[2m §4.2]` Engine + `[2m §4.3]` Session + `[2m §4.4]` Message + `[2m §4.5]` Application + `[2m §5]` wrapped C-ABI symbols. PY-002 (GIL discipline): `[2m §6.1]` per-call release/acquire + `[2m §1.3]` rule (4) GIL-protected session-local strand markers + `[2m §6.5]` reentrancy carve-outs. PY-003 (exception translation): `[2m §4.6]` `FixppError` block-mapped hierarchy + `[2m §6.3]` translation boundaries + `[2m §6.7]` 5 new `FIXPP_ERR_BINDING_*` variants in `[2i §1.1]` `[1200, 1299]` (`PYTHON_CALLBACK_RAISED = 1200`, `SUBINTERPRETER = 1201`, `OBJECT_LIFETIME = 1202`, `WHEEL_ABI_MISMATCH = 1203`, `CALLBACK_REENTRANT_CLOSE = 1204` per `[2m App D §D.1, §D.3]`). PY-004 (lifetime / ownership): `[2m §6.2]` Python objects don't outlive native sessions per `[const §X.5]` opaque-handle uniform-destroy + `[2m §6.7]` `OBJECT_LIFETIME` (1202) enforcement. PY-005 (manylinux wheel): `[2m §1.1]` platform matrix (CPython 3.10–3.13 single-interpreter, manylinux 2_28, x86_64) + `[2m §11]` Hand-off CI workflow per `[arch §7.1]` mandatory wheel name `fixpp-<ver>-cp310-cp310-manylinux_2_28_x86_64.whl` + `cibuildwheel` + `auditwheel repair` per `[const §IV.3]`. v1.0 binding consumption surface is **C-ABI-only** per `[arch §8]` structural enforcement; `tools/check_layers.py` lint extended at 2m sign-off to scan `bindings/python/` for any `#include <fixpp/X/...>` violation (mirrors the 2j precedent at `[arch §8]` enforcement bullet). 2m amends `[2j §11]` hand-off via `[2m App D §D.4]` to declare `fixpp_session_post` as the v1.0 strand-post primitive owed to 2m for outbound `Message.__init__`. Source: 2m v0.3 sign-off (2026-05-10); see `[2m §11]` drop-in language and `[2m Appendix A]`.

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
