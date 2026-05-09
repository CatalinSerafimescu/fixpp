# Master Feature Catalogue

> **Canonical location:** `research/G19-fix-fpml-iso20022/library/spec/feature-catalogue.md`
> **Schema:** ID | Source | Category | Title | FIX version(s) | Spec ref / Lib ref | Status | /specify | PR | Tests | Verified
> **Source** ∈ {OFFICIAL, OSS:<lib>, COMMERCIAL:<vendor>}
> **Category** ∈ {session, wire, dictionary, transport, c-api, python-api, service, tooling, nfr, test}
> **Status** ∈ {backlog, planning, implementing, done, dropped(reason)}
> **Verified** requires: tests green + Codex pass + (where applicable) interop test against QuickFIX

<!-- ═══════════════════════════════════════════════════════════════
     OFFICIAL ROWS — populated by Phase 1.1 spec sweep
     ═══════════════════════════════════════════════════════════════ -->

## Session Layer

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| S-001 | OFFICIAL | session | Logon (35=A) — session initiation | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.3] Establishing a FIX connection | backlog | — | — | — | — |
| S-002 | OFFICIAL | session | Logout (35=5) — orderly session termination | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.6] FIX connection termination | backlog | — | — | — | — |
| S-003 | OFFICIAL | session | Heartbeat (35=0) — keep-alive | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.5.1] FIX connection keep-alive (heartbeat) | backlog | — | — | — | — |
| S-004 | OFFICIAL | session | Test Request (35=1) — heartbeat elicitation | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.5.5] Test Request processing | backlog | — | — | — | — |
| S-005 | OFFICIAL | session | Resend Request (35=2) — gap fill request | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.8.2] Request retransmission of messages | backlog | — | — | — | — |
| S-006 | OFFICIAL | session | Sequence Reset (35=4) — gap fill or hard reset | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.8.5] Gap fill process | backlog | — | — | — | — |
| S-007 | OFFICIAL | session | Reject (35=3) — session-level reject | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.5.4] Rejecting invalid messages | backlog | — | — | — | — |
| S-008 | OFFICIAL | session | Session FSM — state machine (NotConnected → LogonSent → Active → Logout → Disconnected) | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.10] FIX session state matrix | backlog | — | — | — | — |
| S-009 | OFFICIAL | session | Sequence number management — MsgSeqNum (34), tracking, increment, wrap-around | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.1] Sequence numbers | backlog | — | — | — | — |
| S-010 | OFFICIAL | session | PossDupFlag (43) + PossResend (97) — duplicate detection semantics | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.8.4] Possible duplicates | backlog | — | — | — | — |
| S-011 | OFFICIAL | session | Message store interface — persist sent/received messages for recovery | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.8] Message recovery | backlog | — | — | — | — |
| S-012 | OFFICIAL | session | In-memory message store implementation | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.8] Message recovery | backlog | — | — | — | — |
| S-013 | OFFICIAL | session | File-based message store implementation | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.8] Message recovery | backlog | — | — | — | — |
| S-014 | OFFICIAL | session | Session recovery — resend flow, GapFill (123=Y) for admin messages | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.8] Message recovery | backlog | — | — | — | — |
| S-015 | OFFICIAL | session | HeartBtInt (108) negotiation during Logon | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.3.4] Heartbeat interval | backlog | — | — | — | — |
| S-016 | OFFICIAL | session | CompID validation — SenderCompID (49), TargetCompID (56), OnBehalfOfCompID (115), DeliverToCompID (128) | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.2.2] Identification of FIX session peers | backlog | — | — | — | — |
| S-017 | OFFICIAL | session | ResetOnLogon / ResetOnLogout / ResetOnDisconnect session settings | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.4] Extended features for FIX session initiation | backlog | — | — | — | — |
| S-018 | OFFICIAL | session | RefreshOnLogon — reload persisted state on reconnect | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.3.12] Synchronization after successful logon | backlog | — | — | — | — |
| S-019 | OFFICIAL | session | MaxLatency / latency check — reject messages with SendingTime (52) too far from wall-clock | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.2.3] Validation of SendingTime(52) | backlog | — | — | — | — |
| S-020 | OFFICIAL | session | BeginString (8) version negotiation — FIX.4.x vs FIXT.1.1 | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.2.1] The FIX session profile | backlog | — | — | — | — |
| S-021 | OFFICIAL | session | EncryptMethod (98) — no encryption (0) mandatory support; other values reserved | 4.0–5.0SP2 | [FIX-SL §4.3.3] Application layer encryption | backlog | — | — | — | — |
| S-022 | OFFICIAL | session | Username (553) / Password (554) authentication fields in Logon | 4.4–5.0SP2 | [FIX-SL §4.3] Establishing a FIX connection | backlog | — | — | — | — |
| S-023 | OFFICIAL | session | NewSeqNo on SequenceReset-Reset (hard reset) | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.8.6] Sequence reset | backlog | — | — | — | — |
| S-024 | OFFICIAL | session | BeginSeqNo / EndSeqNo on ResendRequest | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.8.2] Request retransmission of messages | backlog | — | — | — | — |
| S-025 | OFFICIAL | session | DefaultApplVerID (1137) on Logon for FIXT.1.1 | 5.0SP2, FIXT.1.1 | [FIX-SL §4.3.7] Specifying application version | backlog | — | — | — | — |
| S-026 | OFFICIAL | session | ApplVerID (1128) on application messages (FIXT.1.1 — decouple session from app version) | 5.0SP2, FIXT.1.1 | [FIX-SL §5.3.5] Explicit application version per message | backlog | — | — | — | — |

## Wire / Encoding

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| W-001 | OFFICIAL | wire | Tag=Value (field) encoding — ASCII digit tag, '=' separator, SOH delimiter | 4.0–5.0SP2, FIXT.1.1 | [FIX50SP2 §3] Tag=Value encoding | backlog | — | — | — | — |
| W-002 | OFFICIAL | wire | Standard header fields — BeginString(8), BodyLength(9), MsgType(35), mandatory ordering | 4.0–5.0SP2, FIXT.1.1 | [FIX50SP2 §3.1] Standard header | backlog | — | — | — | — |
| W-003 | OFFICIAL | wire | Standard trailer fields — CheckSum(10), mandatory last field | 4.0–5.0SP2, FIXT.1.1 | [FIX50SP2 §3.1] Standard trailer | backlog | — | — | — | — |
| W-004 | OFFICIAL | wire | BodyLength(9) computation — byte count from MsgType(35) to delimiter before CheckSum(10) | 4.0–5.0SP2, FIXT.1.1 | [FIX50SP2 §3.1] Standard header | backlog | — | — | — | — |
| W-005 | OFFICIAL | wire | CheckSum(10) computation — sum of all bytes mod 256, formatted as 3-digit zero-padded ASCII | 4.0–5.0SP2, FIXT.1.1 | [FIX50SP2 §3.1] Standard header | backlog | — | — | — | — |
| W-006 | OFFICIAL | wire | Repeating group encoding — NoXxx delimiter tag, known-field ordering per data dictionary | 4.0–5.0SP2, FIXT.1.1 | [FIX50SP2 §3.2] Repeating groups | backlog | — | — | — | — |
| W-007 | OFFICIAL | wire | Nested repeating groups (groups within groups) | 4.0–5.0SP2, FIXT.1.1 | [FIX50SP2 §3.2] Repeating groups | backlog | — | — | — | — |
| W-008 | OFFICIAL | wire | Data (raw bytes) field type — Length tag + Data tag pair (e.g., RawDataLength/RawData) | 4.0–5.0SP2 | [FIX50SP2 §3.3] Field data types | backlog | — | — | — | — |
| W-009 | OFFICIAL | wire | Field data types — INT, FLOAT, CHAR, BOOLEAN, STRING, MULTIPLECHARVALUE, MULTIPLEVALUESTRING, CURRENCY, EXCHANGE, MONTHYEAR, UTCTIMESTAMP, UTCTIMEONLY, UTCDATEONLY, LOCALMKTDATE, TZTIMEONLY, TZTIMESTAMP, LENGTH, SEQNUM, NUMINGROUP, COUNTRY, LANGUAGE, XMLDATA | 4.0–5.0SP2 | [FIX50SP2 §3.3] Field data types | backlog | — | — | — | — |
| W-010 | OFFICIAL | wire | Pipelined message framing — multiple messages on one TCP stream | 4.0–5.0SP2, FIXT.1.1 | [FIX50SP2 §3] Tag=Value encoding | backlog | — | — | — | — |
| W-011 | OFFICIAL | wire | Zero-copy parser — tag/value views into a contiguous buffer without allocation | 4.0–5.0SP2 | [impl] implementation NFR | backlog | — | — | — | — |
| W-012 | OFFICIAL | wire | Offset-table parser approach — index tags by position, O(1) field lookup | 4.0–5.0SP2 | [impl] implementation NFR | backlog | — | — | — | — |
| W-013 | OFFICIAL | wire | Message serializer — write fields in required order, compute BodyLength and CheckSum on the fly | 4.0–5.0SP2, FIXT.1.1 | [FIX50SP2 §3.1] Standard header | backlog | — | — | — | — |
| W-014 | OFFICIAL | wire | Message validator — required fields present, type conformance, enum values, repeating group structure | 4.0–5.0SP2, FIXT.1.1 | [FIX50SP2 §3] Tag=Value encoding | backlog | — | — | — | — |
| W-015 | OFFICIAL | wire | FIXP framing (FIX Performance Session Layer) — binary framing header | FIX Latest / post-1.0 | [impl] FIXP spec (post-1.0) | backlog | — | — | — | — |
| W-016 | OFFICIAL | wire | SOFH (Simple Open Framing Header) framing prefix | post-1.0 v1.1 | [impl] SOFH spec (post-1.0) | backlog | — | — | — | — |

## Data Dictionary

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| D-001 | OFFICIAL | dictionary | FIX 4.2 data dictionary — all standard messages, fields, components, groups | 4.2 | [FIX42] FIX 4.2 application specification | backlog | — | — | — | — |
| D-002 | OFFICIAL | dictionary | FIX 4.4 data dictionary | 4.4 | [FIX44] FIX 4.4 application specification | backlog | — | — | — | — |
| D-003 | OFFICIAL | dictionary | FIX 5.0SP2 + FIXT.1.1 data dictionary | 5.0SP2, FIXT.1.1 | [FIX50SP2] FIX 5.0SP2 application specification | backlog | — | — | — | — |
| D-004 | OFFICIAL | dictionary | FIX 4.0, 4.1 data dictionaries (older, minimal) | 4.0, 4.1 | [FIX40] [FIX41] application specifications | backlog | — | — | — | — |
| D-005 | OFFICIAL | dictionary | FIX 4.3 data dictionary | 4.3 | [FIX43] FIX 4.3 application specification | backlog | — | — | — | — |
| D-006 | OFFICIAL | dictionary | FIX 5.0, 5.0SP1 data dictionaries | 5.0, 5.0SP1 | [FIX50] [FIX50SP1] application specifications | backlog | — | — | — | — |
| D-007 | OFFICIAL | dictionary | XML data dictionary format loader — parse FIX standard XML (QuickFIX-style) at runtime | 4.0–5.0SP2 | [impl] implementation | backlog | — | — | — | — |
| D-008 | OFFICIAL | dictionary | Code-generated constexpr field metadata from data dictionary — zero-runtime-cost field lookup | 4.0–5.0SP2 | [impl] implementation NFR | backlog | — | — | — | — |
| D-009 | OFFICIAL | dictionary | Custom dictionary extension — user-defined fields and messages | 4.0–5.0SP2 | [FIX50SP2 §3.2] Repeating groups | backlog | — | — | — | — |
| D-010 | OFFICIAL | dictionary | Component definition support — reusable field groups (Instrument, Parties, etc.) | 4.4–5.0SP2 | [FIX50SP2 §3.2] Repeating groups | backlog | — | — | — | — |
| D-011 | OFFICIAL | dictionary | FIX Latest / FIX Orchestra repository format | FIX Latest | [impl] FIX Orchestra spec (post-1.0) | backlog | — | — | — | — |

## Application Messages — Order Management

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| A-001 | OFFICIAL | wire | NewOrderSingle (35=D) | 4.0–5.0SP2 | [FIX50SP2] Single General Order Handling | backlog | — | — | — | — |
| A-002 | OFFICIAL | wire | NewOrderList (35=E) | 4.0–5.0SP2 | [FIX50SP2] Program/List Trading | backlog | — | — | — | — |
| A-003 | OFFICIAL | wire | OrderCancelRequest (35=F) | 4.0–5.0SP2 | [FIX50SP2] Single General Order Handling | backlog | — | — | — | — |
| A-004 | OFFICIAL | wire | OrderCancelReplaceRequest (35=G) — amend | 4.0–5.0SP2 | [FIX50SP2] Single General Order Handling | backlog | — | — | — | — |
| A-005 | OFFICIAL | wire | OrderStatusRequest (35=H) | 4.0–5.0SP2 | [FIX50SP2] Single General Order Handling | backlog | — | — | — | — |
| A-006 | OFFICIAL | wire | ExecutionReport (35=8) | 4.0–5.0SP2 | [FIX50SP2] Single General Order Handling | backlog | — | — | — | — |
| A-007 | OFFICIAL | wire | OrderCancelReject (35=9) | 4.0–5.0SP2 | [FIX50SP2] Single General Order Handling | backlog | — | — | — | — |
| A-008 | OFFICIAL | wire | OrderMassCancelRequest (35=q) | 4.4–5.0SP2 | [FIX44] Order Mass Handling | backlog | — | — | — | — |
| A-009 | OFFICIAL | wire | OrderMassCancelReport (35=r) | 4.4–5.0SP2 | [FIX44] Order Mass Handling | backlog | — | — | — | — |
| A-010 | OFFICIAL | wire | OrderMassStatusRequest (35=AF) | 4.4–5.0SP2 | [FIX44] Order Mass Handling | backlog | — | — | — | — |
| A-011 | OFFICIAL | wire | MultilegOrderCancelReplace (35=AC) | 4.4–5.0SP2 | [FIX44] Multileg Orders | backlog | — | — | — | — |
| A-012 | OFFICIAL | wire | CrossOrderCancelReplaceRequest (35=t) | 4.4–5.0SP2 | [FIX44] Cross Orders | backlog | — | — | — | — |
| A-013 | OFFICIAL | wire | CrossOrderCancelRequest (35=u) | 4.4–5.0SP2 | [FIX44] Cross Orders | backlog | — | — | — | — |

## Application Messages — Market Data

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| M-001 | OFFICIAL | wire | MarketDataRequest (35=V) | 4.2–5.0SP2 | [FIX42] Market Data | backlog | — | — | — | — |
| M-002 | OFFICIAL | wire | MarketDataSnapshotFullRefresh (35=W) | 4.2–5.0SP2 | [FIX42] Market Data | backlog | — | — | — | — |
| M-003 | OFFICIAL | wire | MarketDataIncrementalRefresh (35=X) | 4.2–5.0SP2 | [FIX42] Market Data | backlog | — | — | — | — |
| M-004 | OFFICIAL | wire | MarketDataRequestReject (35=Y) | 4.2–5.0SP2 | [FIX42] Market Data | backlog | — | — | — | — |
| M-005 | OFFICIAL | wire | SecurityDefinitionRequest (35=c) / SecurityDefinition (35=d) | 4.2–5.0SP2 | [FIX42] Securities Reference Data | backlog | — | — | — | — |
| M-006 | OFFICIAL | wire | SecurityStatusRequest (35=e) / SecurityStatus (35=f) | 4.2–5.0SP2 | [FIX42] Securities Reference Data | backlog | — | — | — | — |
| M-007 | OFFICIAL | wire | TradingSessionStatusRequest (35=g) / TradingSessionStatus (35=h) | 4.2–5.0SP2 | [FIX42] Market Structure Reference Data | backlog | — | — | — | — |
| M-008 | OFFICIAL | wire | MassQuote (35=i) / MassQuoteAcknowledgement (35=b) | 4.2–5.0SP2 | [FIX42] Quotation Negotiation | backlog | — | — | — | — |
| M-009 | OFFICIAL | wire | Quote (35=S) / QuoteAcknowledgement (35=b) | 4.0–5.0SP2 | [FIX50SP2] Quotation Negotiation | backlog | — | — | — | — |
| M-010 | OFFICIAL | wire | QuoteRequest (35=R) / QuoteRequestReject (35=AG) | 4.0–5.0SP2 | [FIX50SP2] Quotation Negotiation | backlog | — | — | — | — |
| M-011 | OFFICIAL | wire | QuoteCancel (35=Z) | 4.2–5.0SP2 | [FIX42] Quotation Negotiation | backlog | — | — | — | — |
| M-012 | OFFICIAL | wire | QuoteStatusRequest (35=a) | 4.2–5.0SP2 | [FIX42] Quotation Negotiation | backlog | — | — | — | — |

## Application Messages — Allocations & Post-Trade

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| P-001 | OFFICIAL | wire | AllocationInstruction (35=J) | 4.0–5.0SP2 | [FIX50SP2] Allocation | backlog | — | — | — | — |
| P-002 | OFFICIAL | wire | AllocationInstructionAck (35=P) | 4.1–5.0SP2 | [FIX50SP2] Allocation | backlog | — | — | — | — |
| P-003 | OFFICIAL | wire | AllocationReport (35=AS) | 4.4–5.0SP2 | [FIX44] Allocation | backlog | — | — | — | — |
| P-004 | OFFICIAL | wire | AllocationReportAck (35=AT) | 4.4–5.0SP2 | [FIX44] Allocation | backlog | — | — | — | — |
| P-005 | OFFICIAL | wire | Confirmation (35=AK) / ConfirmationAck (35=AU) / ConfirmationRequest (35=BH) | 4.4–5.0SP2 | [FIX44] Confirmation | backlog | — | — | — | — |
| P-006 | OFFICIAL | wire | SettlementInstructions (35=T) | 4.0–5.0SP2 | [FIX50SP2] Settlement Instructions | backlog | — | — | — | — |
| P-007 | OFFICIAL | wire | SettlementInstructionRequest (35=AV) | 4.4–5.0SP2 | [FIX44] Settlement Instructions | backlog | — | — | — | — |
| P-008 | OFFICIAL | wire | TradeCaptureReport (35=AE) / TradeCaptureReportRequest (35=AD) / TradeCaptureReportAck (35=AR) / TradeCaptureReportRequestAck (35=AQ) | 4.4–5.0SP2 | [FIX44] Trade Capture | backlog | — | — | — | — |

## Application Messages — Account & Collateral

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| C-001 | OFFICIAL | wire | CollateralRequest (35=AX) / CollateralAssignment (35=AY) / CollateralResponse (35=AZ) / CollateralReport (35=BA) / CollateralInquiry (35=BB) / CollateralInquiryAck (35=BG) | 4.4–5.0SP2 | [FIX44] Collateral Management | backlog | — | — | — | — |
| C-002 | OFFICIAL | wire | PositionMaintenance (35=AL) / RequestForPositions (35=AN) / RequestForPositionsAck (35=AO) / PositionReport (35=AP) / AdjustedPositionReport (35=BL) | 4.4–5.0SP2 | [FIX44] Position Maintenance | backlog | — | — | — | — |
| C-003 | OFFICIAL | wire | AccountSummaryReport (35=CQ) | 5.0SP2 | [FIX50SP2] Position Maintenance | backlog | — | — | — | — |

## Application Messages — Registration & IOI

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| R-001 | OFFICIAL | wire | RegistrationInstructions (35=o) / RegistrationInstructionsResponse (35=p) | 4.2–5.0SP2 | [FIX42] Registration Instructions | backlog | — | — | — | — |
| R-002 | OFFICIAL | wire | IndicationOfInterest (35=6) | 4.0–5.0SP2 | [FIX50SP2] Indication / Event Communication | backlog | — | — | — | — |
| R-003 | OFFICIAL | wire | Advertisement (35=7) | 4.0–5.0SP2 | [FIX50SP2] Indication / Event Communication | backlog | — | — | — | — |
| R-004 | OFFICIAL | wire | News (35=B) | 4.0–5.0SP2 | [FIX50SP2] Indication / Event Communication | backlog | — | — | — | — |
| R-005 | OFFICIAL | wire | Email (35=C) | 4.0–5.0SP2 | [FIX50SP2] Indication / Event Communication | backlog | — | — | — | — |

## Application Messages — Network & Session Management (FIX 5.0)

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| N-001 | OFFICIAL | session | NetworkCounterpartySystemStatusRequest (35=BC) / NetworkCounterpartySystemStatusResponse (35=BD) | 5.0–5.0SP2 | [FIX50] Network / Counterparty System Status | backlog | — | — | — | — |
| N-002 | OFFICIAL | session | UserRequest (35=BE) / UserResponse (35=BF) | 5.0–5.0SP2 | [FIX50] User Management | backlog | — | — | — | — |
| N-003 | OFFICIAL | session | ApplicationMessageRequest (35=BW) / ApplicationMessageRequestAck (35=BX) / ApplicationMessageReport (35=BY) | 5.0SP2 | [FIX50SP2] Application Sequencing | backlog | — | — | — | — |

## Transport

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| T-001 | OFFICIAL | transport | TCP transport — initiator and acceptor roles | 4.0–5.0SP2 | [FIX-SL §4.3.1] Transport layer requirements | backlog | — | — | — | — |
| T-002 | OFFICIAL | transport | TLS over TCP — OpenSSL on both Linux and Windows (Schannel dropped; ASIO has no Schannel backend) | 4.0–5.0SP2 | [FIX-SL §4.3.1] Transport layer requirements; [FIXS §1.1] Scope | backlog | — | — | — | — |
| T-003 | OFFICIAL | transport | ASIO async I/O layer — non-blocking read/write with back-pressure | 4.0–5.0SP2 | [impl] implementation | backlog | — | — | — | — |
| T-004 | OFFICIAL | transport | Reconnect / exponential back-off — initiator retry on disconnect | 4.0–5.0SP2 | [FIX-SL §4.3.1] Transport layer requirements | backlog | — | — | — | — |
| T-005 | OFFICIAL | transport | Multi-session TCP acceptor — accept multiple connections on one port | 4.0–5.0SP2 | [FIX-SL §4.3.1] Transport layer requirements | backlog | — | — | — | — |
| T-006 | OFFICIAL | transport | FIXS: TLS 1.2 support — ECDHE + AES-GCM cipher suites, forward secrecy | 4.0–5.0SP2 | [FIXS §3.1] Protocol version | backlog | — | — | — | — |
| T-007 | OFFICIAL | transport | FIXS: TLS 1.3 support — preferred; session caching optional | 4.0–5.0SP2 | [FIXS §3.1] Protocol version | backlog | — | — | — | — |
| T-008 | OFFICIAL | transport | FIXS: Mutual TLS — leaf certificate pinning on both ends | 4.0–5.0SP2 | [FIXS §2.2] Mutual and Simple TLS protocol options | backlog | — | — | — | — |
| T-009 | OFFICIAL | transport | FIXS: Mutual TLS — CA pinning (server) + leaf pinning (client) | 4.0–5.0SP2 | [FIXS §2.4] Certificate Validation with CA Pinning | backlog | — | — | — | — |
| T-010 | OFFICIAL | transport | FIXS: Simple TLS — server-only auth (Star topology; client auth deferred to FIXA/FIX session) | 4.0–5.0SP2 | [FIXS §2.2] Mutual and Simple TLS protocol options | backlog | — | — | — | — |
| T-011 | OFFICIAL | transport | FIXS: Certificate pinset API — multiple valid peer certs per counterparty for rotation/DR | 4.0–5.0SP2 | [FIXS §2.3] Leaf Certificate Pinning | backlog | — | — | — | — |
| T-012 | OFFICIAL | transport | FIXS: PSK authentication — pre-shared key (P2P; optional; 32-char min; out-of-band exchange) | 4.0–5.0SP2 | [FIXS §2.5] Pre-shared keys (PSKs) | backlog | — | — | — | — |
| T-013 | OFFICIAL | transport | FIXS: Cipher suite enforcement — disable RC4, DES/3DES, anonymous key exchange, MD5 | 4.0–5.0SP2 | [FIXS §3.3] Cipher suites | backlog | — | — | — | — |
| T-039 | OFFICIAL | transport | FIXS: Certificate parameters — RSA 2048-bit min, ECDSA 256-bit, X.509 v2/v3, expiration validation at handshake | 4.0–5.0SP2 | [FIXS §3.4] Certificate parameters | backlog | — | — | — | — |
| T-040 | OFFICIAL | transport | FIXS: Secrets management — distribute private keys/PSKs/pinned-certs via approved channels (HTTPS, GnuPG, PKCS#12, postal, in-person); store securely; support rotation | 4.0–5.0SP2 | [FIXS §4.1] Sharing secrets | backlog | — | — | — | — |
| T-041 | OFFICIAL | transport | FIXS: Authorization linked to authentication — authenticated TLS identity must map to authorized FIX CompID; per-counterparty TLS tunnel | 4.0–5.0SP2 | [FIXS §4.4] Authorization linked to authentication | backlog | — | — | — | — |

## C ABI

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| CA-001 | OFFICIAL | c-api | Opaque handle types — FixSession, FixMessage, FixDictionary (no C++ symbols in ABI) | all | [2i §4.2] Opaque handle types | backlog | — | — | — | — |
| CA-002 | OFFICIAL | c-api | Error code enum + fixpp_strerror() — all error paths return numeric code | all | [2i §4.3] / [2i §4.4] | backlog | — | — | — | — |
| CA-003 | OFFICIAL | c-api | Thread-safety contract — explicit reentrancy guarantees per function | all | [2i §4.10] | backlog | — | — | — | — |
| CA-004 | OFFICIAL | c-api | Version negotiation — fixpp_version() / ABI version tag in header | all | [2i §4.5] | backlog | — | — | — | — |
| CA-005 | OFFICIAL | c-api | Session lifecycle — fixpp_session_create / connect / disconnect / destroy | all | [2i §7.9] (shape; behaviour owed to 2j + Phase-4) | backlog | — | — | — | — |
| CA-006 | OFFICIAL | c-api | Message send — fixpp_session_send(session, msg) | all | [2i §7.9] (shape; behaviour owed to 2j + Phase-4) | backlog | — | — | — | — |
| CA-007 | OFFICIAL | c-api | Message receive callback — fixpp_session_on_message(session, cb, userdata) | all | [2i §7.9] (shape; behaviour owed to 2j + Phase-4) | backlog | — | — | — | — |
| CA-008 | OFFICIAL | c-api | Field accessor — fixpp_msg_get_string / get_int / get_double by tag | all | [2i §4.6] | backlog | — | — | — | — |
| CA-009 | OFFICIAL | c-api | Field setter — fixpp_msg_set_string / set_int / set_double by tag | all | [2i §4.7] | backlog | — | — | — | — |
| CA-010 | OFFICIAL | c-api | Repeating group accessor — fixpp_msg_get_group / group_get_field | all | [2i §4.8] | backlog | — | — | — | — |

## Python Bindings

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| PY-001 | OFFICIAL | python-api | SWIG interface wrapping C ABI — import fixpp, Session, Message classes | all | [impl] implementation | backlog | — | — | — | — |
| PY-002 | OFFICIAL | python-api | GIL correctness — release GIL during blocking I/O; reacquire in callbacks | all | [impl] implementation | backlog | — | — | — | — |
| PY-003 | OFFICIAL | python-api | Exception translation — C error codes → Python exceptions | all | [impl] implementation | backlog | — | — | — | — |
| PY-004 | OFFICIAL | python-api | Ownership / lifetime — Python objects don't outlive native sessions | all | [impl] implementation | backlog | — | — | — | — |
| PY-005 | OFFICIAL | python-api | pip-installable wheel (Linux x86_64 minimum) via CI | all | [impl] implementation | backlog | — | — | — | — |

## Service (Daemon/Sidecar)

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| SVC-001 | OFFICIAL | service | gRPC control plane — session create/config/teardown/observability over Unix socket / named pipe | all | [2j §4.6] / [2j §4.7] | backlog | `.specify/2j-controlplane.md` v0.3 | — | — | — |
| SVC-002 | OFFICIAL | service | iceoryx2 data plane — zero-copy SHM publish/subscribe for hot-path FIX messages | all | [impl] implementation | backlog | — | — | — | — |
| SVC-003 | OFFICIAL | service | Data plane opt-in — gRPC-only mode when iceoryx2 unavailable | all | [impl] implementation | backlog | — | — | — | — |
| SVC-004 | OFFICIAL | service | Service health / observability — gRPC health check + prometheus-compatible metrics | all | [2j §4.7] / [2j §4.8] | backlog | `.specify/2j-controlplane.md` v0.3 | — | — | — |
| SVC-005 | OFFICIAL | service | Pluggable control plane interface — `fixpp::service::ControlPlane` (3 pure-virtual: `start`, `stop`, `health`; ≤5 cap with 2 slots of headroom for v1.x auth-token rotation + RPC re-mapping per [2j §10] Q5); default impl gRPC over Unix socket (Linux) / named pipe (Windows); alternative impls (JSON-over-Unix-socket sample, ...) link without rebuilding the engine via the AGPL-boundary structural rule per `[const §V.1]` / `[arch §8]`; `EngineConfig::control_plane_factory` engine-anchor per `[2j Appendix D §D.2]`; handlers run on the engine executor per `[2d §7.8]`; `CloseSession` RPC consumes `[2h §7.6]` graceful-drain shape; rotation RPCs (`RotatePinset` / `ReloadCertSource`) deferred to v1.x per `[2j §10]` Q1 + Q9 | all | [2j §4.1] / [arch §4.11] | backlog | `.specify/2j-controlplane.md` v0.3 | — | — | — |

## NFRs (Non-Functional Requirements)

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| NFR-001 | OFFICIAL | nfr | C++23, clang primary, GCC + MSVC secondary | all | [constitution] | backlog | — | — | — | — |
| NFR-002 | OFFICIAL | nfr | ≥90% line coverage / ≥80% branch on touched modules | all | [constitution] | backlog | — | — | — | — |
| NFR-003 | OFFICIAL | nfr | Sanitizers clean (ASan, UBSan, TSan) on hot-path code | all | [constitution] | backlog | — | — | — | — |
| NFR-004 | OFFICIAL | nfr | No exceptions on hot paths | all | [constitution] | backlog | — | — | — | — |
| NFR-005 | OFFICIAL | nfr | Allocator-aware containers, SBO/arena-friendly allocation | all | [constitution] | backlog | — | — | — | — |
| NFR-006 | OFFICIAL | nfr | Perf parity-or-better vs hffix (parse) and QuickFIX (session throughput) | all | [constitution] | backlog | — | — | — | — |
| NFR-007 | OFFICIAL | nfr | Google Benchmark perf gate ±5% | all | [constitution] | backlog | — | — | — | — |
| NFR-008 | OFFICIAL | nfr | clang-tidy clean, clang-format, include-what-you-use | all | [constitution] | backlog | — | — | — | — |
| NFR-009 | OFFICIAL | nfr | libFuzzer corpus for wire/ ≥10 min, nightly longer | all | [constitution] | backlog | — | — | — | — |
| NFR-010 | OFFICIAL | nfr | AGPL-3.0 + commercial dual license | all | [constitution] | backlog | — | — | — | — |
| NFR-015 | OFFICIAL | nfr | Pluggable Clock interface — `fixpp::core::Clock` (4 pure-virtual: `now`, `steady_now`, `sleep_until`, `cancel_sleeps`) carried by `EngineConfig`; default `system_clock_source` (per-session reusable `steady_timer` slots keyed by `Session*` from `session_arena`); test impl `mock_clock` (pimpl per `[const §XI.3]`); `effective_clock = SessionConfig::clock_override ?: EngineConfig::clock` rule routes heartbeat / SendingTime / S-035 scheduling / session-scoped LOG+OBS records through the per-session clock; engine-scope LOG+OBS records read `EngineConfig::clock` directly with a `clock_scope = engine` discriminator | all | [2d §4.1] / [arch §1.1] | backlog | `.specify/2d-threading.md` v0.4 | — | — | — |
| NFR-016 | OFFICIAL | nfr | Awaitable mutex `fixpp::sync::async_mutex` — own implementation (BSL-1.0 algorithm attribution to avast/asio-mutex; cppcoro / Lewis-Baker `std::atomic<uintptr_t>` state with not_locked/locked_no_waiters/pointer-to-LIFO encoding + mutex-owned `std::atomic<async_mutex_awaiter*> next_drain_head_` residual FIFO chain per RC-A v1.1, per-waiter three-state `std::atomic<waiter_phase>` machine `{ queued, granted, cancelled }` for unlock/cancel CAS arbitration with WINNER-ONLY post-CAS holder accounting per v1.3 RC-α and CAS-then-publish `*result_` writer arbitration per v1.4); waiter embedded in the awaiter object inside the caller's coroutine frame with 32-byte inline slot-handler-storage buffer per RC-C v1.1 (zero global-heap on the v1.0 contended path); PMR-aware fallback via the explicit `async_lock(mr)` overload + session-side helper `async_lock_via_session_executor`; ASIO `cancellation_type::total` removes the waiter and completes with `expected_t::unexpected{sync_lock_aborted}` at the 2f boundary (mapped to `FIXPP_ERR_CANCELLED` at the C ABI); per-mutex `dispatch`/`post` completion policy with default `dispatch` and ASIO `running_in_this_thread()` predicate; `std::terminate()` precondition on destruction + explicit mutex-owned `cancel_and_drain()` drain primitive (RC-B v1.1, RC-α + RC-β v1.3 post-cap rewrite + v1.4 deterministic latch publication ordering + v1.4 subscriber-wake-on-reaper-abort) with `std::atomic<bool> draining_` + `std::atomic_flag drain_in_progress_` concurrent-call serialiser + `std::atomic<std::uint32_t> active_holders_count_` post-CAS winner-only holder accounting + `std::atomic<std::uint32_t> active_acquirers_count_` in-flight acquirer epoch + lazy `std::atomic<std::shared_ptr<detail::drain_latch_state>> drain_latch_ptr_` (non-expiring, published before `draining_` per v1.4 / I-1) with state object allocated via `std::make_shared` inside the reaper's coroutine frame and a `asio::experimental::concurrent_channel`-backed multi-waiter latch surface, `signal_release()` + `signal_abort()` + `notify()` (non-terminal wake per v1.5 / I-8) signals; reaper cancellation propagates per the §4.7.3 contract returning `unexpected{sync_lock_aborted}` on `cancellation_type::total` from the caller's parent state; new error variant `sync_lock_drained` per RC-B; the only legal mutex shape in coroutine context per `[const §XI.3]` (CI-enforced via `tools/check_no_std_mutex_in_awaitable_headers.sh` grep gate with post-preprocessing scope per `[const §XV.9]`). | all | [2f §4.1] / [arch §1.1] | backlog | `.specify/2f-async-mutex.md` v1.5 | — | — | — |

<!-- ═══════════════════════════════════════════════════════════════
     OSS ROWS — populated by Phase 1.2 OSS survey
     ═══════════════════════════════════════════════════════════════ -->

## OSS-Derived Features

| ID | Source | Category | Title | FIX version(s) | Lib ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| OSS-001 | OSS:QuickFIX | dictionary | XML data dictionary at runtime — FIX44.xml, FIX50SP2.xml well-known format | 4.0–5.0SP2 | QuickFIX src/DataDictionary | backlog | — | — | — | — |
| OSS-002 | OSS:QuickFIX | session | FileStore / MemoryStore persistence API — seqnum + message body keyed by seqnum | 4.0–5.0SP2 | QuickFIX src/FileStore | backlog | — | — | — | — |
| OSS-003 | OSS:QuickFIX | session | FileLog / ScreenLog — structured session event logging API | 4.0–5.0SP2 | QuickFIX src/FileLog | backlog | — | — | — | — |
| OSS-004 | OSS:QuickFIX | transport | Thread-per-session model — acceptor spawns one I/O thread per session | 4.0–5.0SP2 | QuickFIX src/ThreadedSocketAcceptor | backlog | — | — | — | — |
| OSS-005 | OSS:QuickFIX | session | Application callback interface — onLogon/onLogout/fromApp/toApp/fromAdmin/toAdmin | 4.0–5.0SP2 | QuickFIX include/Application.h | backlog | — | — | — | — |
| OSS-006 | OSS:hffix | wire | Header-only zero-copy parser — single translation unit, no heap allocation in hot path | 4.0–5.0SP2 | hffix include/hffix.hpp | backlog | — | — | — | — |
| OSS-007 | OSS:hffix | wire | Iterator-based field traversal — forward iterator over tag/value spans | 4.0–5.0SP2 | hffix include/hffix.hpp | backlog | — | — | — | — |
| OSS-008 | OSS:hffix | wire | message_writer over caller-supplied buffer — serializer takes external storage | 4.0–5.0SP2 | hffix | backlog | — | — | — | — |
| OSS-009 | OSS:fix8 | session | Async message dispatch via thread pool — decouple session I/O thread from application thread | 4.0–5.0SP2 | fix8 | backlog | — | — | — | — |
| OSS-010 | OSS:fix8 | dictionary | Code-generated message classes from XML — compile-time field name constants | 4.0–5.0SP2 | fix8/compiler | backlog | — | — | — | — |
| OSS-011 | OSS:OpenFAST | wire | FAST encoding (FIX Adapted for Streaming) — template-based binary compression | post-1.0 v1.3 | OpenFAST | backlog | — | — | — | — |
| OSS-012 | OSS:Aeron/SBE | wire | SBE (Simple Binary Encoding) — schema-driven binary encoding, flyweight accessors | post-1.0 v1.2 | Aeron/SBE | backlog | — | — | — | — |
| OSS-013 | OSS:Aeron/SBE | wire | SBE flyweight pattern — accessor objects with no copy, direct-into-buffer read/write | post-1.0 v1.2 | Aeron/SBE | backlog | — | — | — | — |
| OSS-014 | OSS:QuickFIX | session | Session configuration file (CFG) format — Initiator/Acceptor/Session sections | 4.0–5.0SP2 | QuickFIX | backlog | — | — | — | — |

<!-- ═══════════════════════════════════════════════════════════════
     COMMERCIAL ROWS — populated by Phase 1.3 commercial survey
     ═══════════════════════════════════════════════════════════════ -->

## Commercial-Derived Features

| ID | Source | Category | Title | FIX version(s) | Vendor ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| COM-001 | COMMERCIAL:OnixS | session | Session scheduling — configurable trading hours, auto connect/disconnect | 4.0–5.0SP2 | OnixS C++ SDK | backlog | — | — | — | — |
| COM-002 | COMMERCIAL:OnixS | session | FIX Latest / Elastic FIX sessions | FIX Latest | OnixS | backlog | — | — | — | — |
| COM-003 | COMMERCIAL:OnixS | transport | SSL mutual authentication (client certs) on TLS transport | 4.0–5.0SP2 | OnixS | backlog | — | — | — | — |
| COM-004 | COMMERCIAL:CameronTec | session | Multi-session failover / warm standby — seamless switch with no sequence number reset | 4.0–5.0SP2 | CameronTec Catalys | backlog | — | — | — | — |
| COM-005 | COMMERCIAL:CameronTec | session | High-availability session replication — primary/backup sync | 4.0–5.0SP2 | Catalys | backlog | — | — | — | — |
| COM-006 | COMMERCIAL:RapidAddition | wire | Market data throttling / rate limiting at wire layer | 4.2–5.0SP2 | RA FIX | backlog | — | — | — | — |
| COM-007 | COMMERCIAL:B2BITS | session | FIX engine certification test suite compatibility | 4.0–5.0SP2 | B2BITS FIXICC | backlog | — | — | — | — |
| COM-008 | COMMERCIAL:Esprow | tooling | FIX session monitoring / protocol analyzer | 4.0–5.0SP2 | Esprow | backlog | — | — | — | — |

<!-- ═══════════════════════════════════════════════════════════════
     ADDITIONAL ROWS — found during Phase 1 spec sweep (session refinements)
     ═══════════════════════════════════════════════════════════════ -->

## Session Layer (continued — additional rows from spec sweep)

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| S-027 | OFFICIAL | session | LFIXT compatible mode — FIXT.1.1 without session-layer retransmission, interoperable with FIXT | FIXT.1.1 | [FIX-SL §5.4.4] LFIXT compatible mode | backlog | — | — | — | — |
| S-028 | OFFICIAL | session | LFIXT succinct mode — reduced-complexity session, not interoperable with standard FIXT | FIXT.1.1 | [FIX-SL §5.4.5] LFIXT succinct mode | backlog | — | — | — | — |
| S-029 | OFFICIAL | session | TestMessageIndicator(464) — reject session if production flag in test environment (or vice-versa) | 4.4–5.0SP2, FIXT.1.1 | [FIX-SL §4.3.2] Using the TestMessageIndicator(464) | backlog | — | — | — | — |
| S-030 | OFFICIAL | session | MaxMessageSize(383) — negotiated max message size; disconnect if exceeded | 4.4–5.0SP2 | [FIX-SL §4.3.6] Maximum message size | backlog | — | — | — | — |
| S-031 | OFFICIAL | session | NextExpectedMsgSeqNum(789) in Logon — fast session resume without ResendRequest round-trip | 5.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.4.1] Using NextExpectedMsgSeqNum(789) | backlog | — | — | — | — |
| S-032 | OFFICIAL | session | ResetSeqNumFlag(141) — in-session sequence number reset to 1 by mutual agreement | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.4.2] Using ResetSeqNumFlag(141) for 24-hour connectivity | backlog | — | — | — | — |
| S-033 | OFFICIAL | session | OrigSendingTime(122) — required on all PossDupFlag=Y retransmitted messages | 4.0–5.0SP2, FIXT.1.1 | [FIX-SL §4.8.4] Possible duplicates | backlog | — | — | — | — |
| S-034 | OFFICIAL | session | RefTagID(371) / RefMsgType(372) in Reject — identify the offending field and message type | 4.2–5.0SP2, FIXT.1.1 | [FIX-SL §4.5.4] Rejecting invalid messages | backlog | — | — | — | — |
| S-035 | OFFICIAL | session | Session scheduling — configurable active time windows (StartTime/EndTime per session) | 4.0–5.0SP2 | [impl] implementation (QuickFIX CFG + OnixS pattern) | backlog | — | — | — | — |
| S-036 | OFFICIAL | session | Session tap / monitoring hook — pluggable callback to capture all in/out FIX messages for tooling | all | [impl] implementation | backlog | — | — | — | — |
| S-037 | OFFICIAL | session | NoMsgTypes in Logon — advertise supported MsgType values in Logon(A) | 4.4–5.0SP2, FIXT.1.1 | [FIX-SL §4.3.8] Specifying supported message types | backlog | — | — | — | — |
| S-038 | OFFICIAL | session | Application system identification in Logon — ApplicationSystemName(1603), ApplicationSystemVersion(1604), ApplicationSystemVendor(1605) | 5.0SP2, FIXT.1.1 | [FIX-SL §4.3.9] Identification of application system and FIX session processor | backlog | — | — | — | — |

## Application Messages — Additional (missed in initial pass)

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| A-014 | OFFICIAL | wire | BusinessMessageReject (35=j) — application-layer reject of an app message | 4.2–5.0SP2 | [FIX50SP2] Infrastructure / Business Rejects | backlog | — | — | — | — |
| A-015 | OFFICIAL | wire | DontKnowTrade (35=Q) — counter-party disputes a fill | 4.0–5.0SP2 | [FIX50SP2] Single General Order Handling | backlog | — | — | — | — |
| A-016 | OFFICIAL | wire | NewOrderCross (35=s) — cross-order single submission | 4.3–5.0SP2 | [FIX43] Cross Orders | backlog | — | — | — | — |
| A-017 | OFFICIAL | wire | NewOrderMultileg (35=AB) | 4.3–5.0SP2 | [FIX43] Multileg Orders | backlog | — | — | — | — |
| A-018 | OFFICIAL | wire | ExecutionAcknowledgement (35=BN) — ack of execution by buy-side | 5.0SP1–5.0SP2 | [FIX50SP1] Single General Order Handling | backlog | — | — | — | — |
| A-019 | OFFICIAL | wire | ListCancelRequest (35=K) / ListExecute (35=L) / ListStatusRequest (35=M) / ListStatus (35=N) | 4.0–5.0SP2 | [FIX50SP2] Program/List Trading | backlog | — | — | — | — |
| A-020 | OFFICIAL | wire | BidRequest (35=k) / BidResponse (35=l) / ListStrikePrice (35=m) — program trading | 4.2–5.0SP2 | [FIX42] Program/List Trading | backlog | — | — | — | — |
| A-021 | OFFICIAL | wire | QuoteResponse (35=AJ) / QuoteStatusReport (35=AI) / RFQRequest (35=AH) | 4.4–5.0SP2 | [FIX44] Quotation Negotiation | backlog | — | — | — | — |
| A-022 | OFFICIAL | wire | AssignmentReport (35=AW) / ContraryIntentionReport (35=BO) — options | 4.4–5.0SP2 | [FIX44] Position Maintenance | backlog | — | — | — | — |
| A-023 | OFFICIAL | wire | OrderMassActionRequest (35=CA) / OrderMassActionReport (35=BZ) | 5.0SP2 | [FIX50SP2] Order Mass Handling | backlog | — | — | — | — |
| A-024 | OFFICIAL | wire | ExecutionAcknowledgement (35=BN) — duplicate of A-018 | 5.0SP1–5.0SP2 | [FIX50SP1] Single General Order Handling | dropped(duplicate: same MsgType as A-018) | — | — | — | — |
| A-025 | OFFICIAL | wire | SecurityTypeRequest (35=v) / SecurityTypes (35=w) / SecurityListRequest (35=x) / SecurityList (35=y) | 4.3–5.0SP2 | [FIX43] Securities Reference Data | backlog | — | — | — | — |
| A-026 | OFFICIAL | wire | DerivativeSecurityListRequest (35=z) / DerivativeSecurityList (35=AA) / DerivativeSecurityListUpdateReport (35=BR) | 4.4–5.0SP2 | [FIX44] Securities Reference Data | backlog | — | — | — | — |
| A-027 | OFFICIAL | wire | TradingSessionListRequest (35=BI) / TradingSessionList (35=BJ) / TradingSessionListUpdateReport (35=BS) | 5.0SP2 | [FIX50SP2] Market Structure Reference Data | backlog | — | — | — | — |
| A-028 | OFFICIAL | wire | MarketDefinitionRequest (35=BT) / MarketDefinition (35=BU) / MarketDefinitionUpdateReport (35=BV) | 5.0SP2 | [FIX50SP2] Market Structure Reference Data | backlog | — | — | — | — |
| A-029 | OFFICIAL | wire | SecurityListUpdateReport (35=BK) / SecurityDefinitionUpdateReport (35=BP) | 5.0SP1–5.0SP2 | [FIX50SP1] Securities Reference Data | backlog | — | — | — | — |
| A-030 | OFFICIAL | wire | SettlementObligationReport (35=BQ) | 5.0SP1–5.0SP2 | [FIX50SP1] Settlement Instructions | backlog | — | — | — | — |
| A-031 | OFFICIAL | wire | AllocationInstructionAlert (35=BM) | 5.0SP2 | [FIX50SP2] Allocation | backlog | — | — | — | — |
| A-032 | OFFICIAL | wire | UserNotification (35=CB) | 5.0SP2 | [FIX50SP2] User Management | backlog | — | — | — | — |
| A-033 | OFFICIAL | wire | StreamAssignmentRequest (35=CC) / StreamAssignmentReport (35=CD) / StreamAssignmentReportACK (35=CE) | 5.0SP2 | [FIX50SP2] Application Sequencing | backlog | — | — | — | — |
| A-034 | OFFICIAL | wire | XMLnonFIX (35=n) — arbitrary XML payload in FIX envelope | 4.4–5.0SP2 | [FIX44] Infrastructure | backlog | — | — | — | — |

## NFRs (additional)

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| NFR-011 | OFFICIAL | nfr | C++17 PMR / allocator-aware containers — std::pmr::memory_resource throughout | all | [constitution] | backlog | — | — | — | — |
| NFR-012 | OFFICIAL | nfr | mimalloc as default global allocator (pluggable via PMR) | all | [constitution] | backlog | — | — | — | — |
| NFR-013 | OFFICIAL | nfr | ASIO async I/O transport layer (io_uring on Linux opt-in, IOCP on Windows) | all | [constitution] | backlog | — | — | — | — |
| NFR-014 | OFFICIAL | nfr | Session tap / monitoring hook for protocol analysis and conformance testing | all | [constitution] | backlog | — | — | — | — |

## Commercial-Derived Features (additional)

| ID | Source | Category | Title | FIX version(s) | Vendor ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| COM-009 | COMMERCIAL:CameronTec | session | Warm standby failover — MessageStore designed for network replication | 4.0–5.0SP2 | CameronTec Catalys HA | backlog | — | — | — | — |
| COM-010 | COMMERCIAL:OnixS | session | Spinlock vs mutex option — user-selectable hot-path lock type | 4.0–5.0SP2 | OnixS | backlog | — | — | — | — |
| COM-011 | COMMERCIAL:OnixS | session | Per-session FIX dialect — venue-specific field customization without changing global dictionary | 4.0–5.0SP2 | OnixS | backlog | — | — | — | — |
| COM-012 | COMMERCIAL:B2BITS | tooling | Git-backed session configuration — plain-text diffable config format | all | B2BITS FIXICC H2 | backlog | — | — | — | — |

<!-- ═══════════════════════════════════════════════════════════════
     FIX LATEST ROWS — populated by Phase 1.1b-A spec sweep
     New MsgTypes present in FIX Latest but not in FIX 5.0SP2
     These are post-v1.0 targets (no implementation deadline set)
     ═══════════════════════════════════════════════════════════════ -->

## Application Messages — FIX Latest (post-5.0SP2 additions)

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| A-035 | OFFICIAL | wire | CrossRequest (35=DS) — FIX Latest cross order request | FIX Latest | [FIX-Latest] Trade — Cross Orders | backlog | — | — | — | — |
| A-036 | OFFICIAL | wire | CrossRequestAck (35=DT) — FIX Latest cross order ack | FIX Latest | [FIX-Latest] Trade — Cross Orders | backlog | — | — | — | — |
| A-037 | OFFICIAL | wire | MassOrder (35=DJ) — bulk order submission | FIX Latest | [FIX-Latest] Trade — Order Mass Handling | backlog | — | — | — | — |
| A-038 | OFFICIAL | wire | MassOrderAck (35=DK) — bulk order ack | FIX Latest | [FIX-Latest] Trade — Order Mass Handling | backlog | — | — | — | — |
| A-039 | OFFICIAL | wire | TradeMatchReport (35=DC) — post-trade match reporting | FIX Latest | [FIX-Latest] Post-Trade | backlog | — | — | — | — |
| A-040 | OFFICIAL | wire | MarketDataReport (35=DR) — consolidated market data report | FIX Latest | [FIX-Latest] Pre-Trade — Market Data | backlog | — | — | — | — |
| A-041 | OFFICIAL | wire | MarketDataStatisticsRequest (35=DO) | FIX Latest | [FIX-Latest] Pre-Trade — Market Data | backlog | — | — | — | — |
| A-042 | OFFICIAL | wire | MarketDataStatisticsReport (35=DP) | FIX Latest | [FIX-Latest] Pre-Trade — Market Data | backlog | — | — | — | — |
| A-043 | OFFICIAL | wire | PartyDetailsListRequest (35=CF) | FIX Latest | [FIX-Latest] Pre-Trade — Party Details | backlog | — | — | — | — |
| A-044 | OFFICIAL | wire | PartyDetailsListReport (35=CG) | FIX Latest | [FIX-Latest] Pre-Trade — Party Details | backlog | — | — | — | — |
| A-045 | OFFICIAL | wire | PartyDetailsListUpdateReport (35=CK) | FIX Latest | [FIX-Latest] Pre-Trade — Party Details | backlog | — | — | — | — |
| A-046 | OFFICIAL | wire | PartyDetailsDefinitionRequest (35=CX) | FIX Latest | [FIX-Latest] Pre-Trade — Party Details | backlog | — | — | — | — |
| A-047 | OFFICIAL | wire | PartyDetailsDefinitionRequestAck (35=CY) | FIX Latest | [FIX-Latest] Pre-Trade — Party Details | backlog | — | — | — | — |
| A-048 | OFFICIAL | wire | PartyEntitlementsRequest (35=CU) | FIX Latest | [FIX-Latest] Pre-Trade — Party Entitlements | backlog | — | — | — | — |
| A-049 | OFFICIAL | wire | PartyEntitlementsReport (35=CV) | FIX Latest | [FIX-Latest] Pre-Trade — Party Entitlements | backlog | — | — | — | — |
| A-050 | OFFICIAL | wire | PartyEntitlementsUpdateReport (35=CZ) | FIX Latest | [FIX-Latest] Pre-Trade — Party Entitlements | backlog | — | — | — | — |
| A-051 | OFFICIAL | wire | PartyEntitlementsDefinitionRequest (35=DA) | FIX Latest | [FIX-Latest] Pre-Trade — Party Entitlements | backlog | — | — | — | — |
| A-052 | OFFICIAL | wire | PartyEntitlementsDefinitionRequestAck (35=DB) | FIX Latest | [FIX-Latest] Pre-Trade — Party Entitlements | backlog | — | — | — | — |
| A-053 | OFFICIAL | wire | PartyRiskLimitsRequest (35=CL) | FIX Latest | [FIX-Latest] Pre-Trade — Party Risk Limits | backlog | — | — | — | — |
| A-054 | OFFICIAL | wire | PartyRiskLimitsReport (35=CM) | FIX Latest | [FIX-Latest] Pre-Trade — Party Risk Limits | backlog | — | — | — | — |
| A-055 | OFFICIAL | wire | PartyRiskLimitsUpdateReport (35=CR) | FIX Latest | [FIX-Latest] Pre-Trade — Party Risk Limits | backlog | — | — | — | — |
| A-056 | OFFICIAL | wire | PartyRiskLimitsDefinitionRequest (35=CS) | FIX Latest | [FIX-Latest] Pre-Trade — Party Risk Limits | backlog | — | — | — | — |
| A-057 | OFFICIAL | wire | PartyRiskLimitsDefinitionRequestAck (35=CT) | FIX Latest | [FIX-Latest] Pre-Trade — Party Risk Limits | backlog | — | — | — | — |
| A-058 | OFFICIAL | wire | PartyRiskLimitsReportAck (35=DE) | FIX Latest | [FIX-Latest] Pre-Trade — Party Risk Limits | backlog | — | — | — | — |
| A-059 | OFFICIAL | wire | PartyActionRequest (35=DH) | FIX Latest | [FIX-Latest] Pre-Trade — Party Details | backlog | — | — | — | — |
| A-060 | OFFICIAL | wire | PartyActionReport (35=DI) | FIX Latest | [FIX-Latest] Pre-Trade — Party Details | backlog | — | — | — | — |
| A-061 | OFFICIAL | wire | PartyRiskLimitCheckRequest (35=DF) | FIX Latest | [FIX-Latest] Pre-Trade — Party Risk Limits | backlog | — | — | — | — |
| A-062 | OFFICIAL | wire | PartyRiskLimitCheckRequestAck (35=DG) | FIX Latest | [FIX-Latest] Pre-Trade — Party Risk Limits | backlog | — | — | — | — |
| A-063 | OFFICIAL | wire | SecurityMassStatusRequest (35=CN) | FIX Latest | [FIX-Latest] Pre-Trade — Securities Reference Data | backlog | — | — | — | — |
| A-064 | OFFICIAL | wire | SecurityMassStatus (35=CO) | FIX Latest | [FIX-Latest] Pre-Trade — Securities Reference Data | backlog | — | — | — | — |
| A-065 | OFFICIAL | wire | QuoteAck (35=CW) | FIX Latest | [FIX-Latest] Pre-Trade — Quotation Negotiation | backlog | — | — | — | — |

<!-- ═══════════════════════════════════════════════════════════════
     CONFORMANCE TEST ROWS — populated by Phase 1.1b-B spec sweep
     Source: fixtrading.org/standards/fix-session-testcases-online/
     Category: test — official FIX session conformance tests
     These drive the session/ module test suite in Phase 4
     ═══════════════════════════════════════════════════════════════ -->

## Session Conformance Tests (Official FIX Test Suite)

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| TC-001 | OFFICIAL | test | Session conformance: Logon — initiator (1B) and acceptor (1S); CompID, HeartBtInt, credentials | 4.0–5.0SP2, FIXT.1.1 | [FIX-TC §4 scenario 1B/1S] Connect and Send/Receive Logon | backlog | — | — | — | — |
| TC-002 | OFFICIAL | test | Session conformance: Standard header validation (test 2, sub-cases 2a–2t) — MsgSeqNum gap/low, garbled, PossDupFlag, CompID mismatch, SendingTime threshold, MsgType | 4.0–5.0SP2, FIXT.1.1 | [FIX-TC §4 scenario 2/2S] Standard Header Validation | backlog | — | — | — | — |
| TC-003 | OFFICIAL | test | Session conformance: Trailer / CheckSum validation (test 3, sub-cases 3a–3e) | 4.0–5.0SP2, FIXT.1.1 | [FIX-TC §4 scenario 3] Standard Trailer Validation | backlog | — | — | — | — |
| TC-004 | OFFICIAL | test | Session conformance: Heartbeat send/receive and TestRequest lifecycle (tests 4–6) | 4.0–5.0SP2, FIXT.1.1 | [FIX-TC §4 scenarios 4–6] Heartbeat / Test Request | backlog | — | — | — | — |
| TC-005 | OFFICIAL | test | Session conformance: Reject (35=3) handling — log RefSeqNum/SessionRejectReason, continue (test 7) | 4.0–5.0SP2, FIXT.1.1 | [FIX-TC §4 scenario 7] Receive Reject message | backlog | — | — | — | — |
| TC-006 | OFFICIAL | test | Session conformance: ResendRequest response — PossDupFlag=Y resend, GapFill for admin msgs (test 8) | 4.0–5.0SP2, FIXT.1.1 | [FIX-TC §4 scenario 8] Receive Resend Request message | backlog | — | — | — | — |
| TC-007 | OFFICIAL | test | Session conformance: SequenceReset — GapFill (test 10, sub-cases 10a–10e) | 4.0–5.0SP2, FIXT.1.1 | [FIX-TC §4 scenario 10] Receive Sequence Reset (Gap Fill) | backlog | — | — | — | — |
| TC-008 | OFFICIAL | test | Session conformance: SequenceReset — hard Reset (test 11, sub-cases 11a–11c) | 4.0–5.0SP2, FIXT.1.1 | [FIX-TC §4 scenario 11] Receive Sequence Reset (Reset) | backlog | — | — | — | — |
| TC-009 | OFFICIAL | test | Session conformance: Logout — initiation (test 12) and receipt solicited/unsolicited (test 13) | 4.0–5.0SP2, FIXT.1.1 | [FIX-TC §4 scenarios 12–13] Initiate/Receive logout | backlog | — | — | — | — |
| TC-010 | OFFICIAL | test | Session conformance: Message validation (test 14, sub-cases 14a–14o) — SessionRejectReason codes 0–16, BusinessMessageReject | 4.0–5.0SP2, FIXT.1.1 | [FIX-TC §4 scenario 14] Message validation | backlog | — | — | — | — |
| TC-011 | OFFICIAL | test | Session conformance: Outgoing message queuing on disconnect/reconnect — SendingTime = actual send time (test 16) | 4.0–5.0SP2, FIXT.1.1 | [FIX-TC §4 scenario 16] Queue outgoing messages | backlog | — | — | — | — |
| TC-012 | OFFICIAL | test | Session conformance: PossResend duplicate detection (test 19, sub-cases 19a–19b) | 4.0–5.0SP2, FIXT.1.1 | [FIX-TC §4 scenario 19] PossResend handling | backlog | — | — | — | — |
| TC-013 | OFFICIAL | test | Session conformance: Simultaneous resend — fulfill incoming ResendRequest while tracking own pending resend (test 20) | 4.0–5.0SP2, FIXT.1.1 | [FIX-TC §4 scenario 20] Simultaneous Resend request | backlog | — | — | — | — |
| TC-014 | OFFICIAL | test | Session conformance (optional): Sequence number synchronization after failure — Reset or ResetSeqNumFlag=Y (test 9) | 4.0–5.0SP2, FIXT.1.1 | [FIX-TC §4 scenario 9] Synchronize sequence numbers | backlog | — | — | — | — |
| TC-015 | OFFICIAL | test | Session conformance (optional): Message field ordering tolerance (test 15) | 4.0–5.0SP2, FIXT.1.1 | [FIX-TC §4 scenario 15] Message field ordering | backlog | — | — | — | — |
| TC-016 | OFFICIAL | test | Session conformance (optional): Third-party addressing — OnBehalfOfCompID/DeliverToCompID validation (test 18) | 4.0–5.0SP2, FIXT.1.1 | [FIX-TC §4 scenario 18] Third-party addressing | backlog | — | — | — | — |
| TC-017 | OFFICIAL | test | Session conformance (optional): Encryption / legacy EncryptMethod(98) handling (test 17, sub-cases 17a–17j) | 4.0–5.0SP2 | [FIX-TC §4 scenario 17] Support encryption | backlog | — | — | — | — |
