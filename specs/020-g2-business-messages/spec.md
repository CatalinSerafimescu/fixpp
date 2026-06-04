# Feature Specification: G2 Business-Message Interop — typed NewOrderSingle + ExecutionReport

**Feature Branch**: `020-g2-business-messages`  
**Created**: 2026-06-04  
**Status**: Draft  
**Input**: User description: "G2 business-message interop: typed NewOrderSingle (35=D, catalogue A-001) and ExecutionReport (35=8, catalogue A-006) business-message support — MINIMAL required-tag field set for v1.0 — built as typed builders+accessors on the existing wire/dictionary machinery, then driven end-to-end across live QuickFIX-J and QuickFIX-cpp counterparties in BOTH roles (fixpp-initiator and fixpp-acceptor), with a responding counterparty Application that emits an ExecutionReport for each NewOrderSingle. Discharges the open [const §VII.6] v1.0-GA business-message interop clause (FR-027/SC-008). Builds on 019's public Application callback layer (fromApp/toApp) + any-thread Engine::send, and on the 016/018 live-interop harness. DEFERRED: full FIX 4.4 field/group coverage; all-protocol-version coverage (FIX 4.2 / FIX 5.0 SP2 / FIXT.1.1) — scheduled post-v1.0. Fix8 stays corpus-only."

## Context

After `019-app-callbacks`, fixpp has a public `Application` callback layer (`fromApp`/`toApp`/…) and a public any-thread `Engine::send(SessionId, payload)` entry point. The 019 G2-enablement witness (`tests/session/test_019_g2_enablement_witness.cpp`) already proves that an **opaque** application-message round-trip — `NewOrderSingle (35=D) → ExecutionReport (35=8)` — is drivable through that public surface using hand-built byte payloads. What it does **not** provide is a **typed** business-message surface: today a user must hand-assemble the wire bytes (`"35=D\x01""11=…\x01"…`) and hand-parse inbound bytes, with no schema, no field-name accessors, and no per-field type/required-tag validation.

This feature delivers that typed surface for the two messages at the heart of every FIX order workflow — **NewOrderSingle (35=D)** and **ExecutionReport (35=8)**, catalogue rows **A-001** and **A-006** (both `backlog`) — and then drives them end-to-end across **live** QuickFIX-J and QuickFIX-cpp counterparties, in both roles. This discharges the open `[const §VII.6]` v1.0-GA business-message interop clause that `016`/`018` carried as a deferred item (the interop roadmap's **FR-027 / SC-008**, "G2").

**Scope is deliberately MINIMAL for v1.0.** The typed messages cover only the required/near-required field set needed for a single-order round-trip that interops cleanly with the reference engines (enumerated in Key Entities). Two explicit forward obligations are tracked, not implemented here (see Out of Scope): (1) **full FIX 4.4 field/group coverage** for these two messages, and (2) **all-protocol-version coverage** (FIX 4.2 / FIX 5.0 SP2 / FIXT.1.1) — the latter MUST be scheduled as a post-v1.0 obligation. **Fix8 stays corpus-only** (no live pairing) per the v1.0 interop scope (interop roadmap FR-009).

**Build-on, not rebuild.** The typed messages are thin builders/accessors over the merged wire/ codec (PR #68) and dictionary/ surface (PR #66/#67), following the exact shape of `session/admin_messages.hpp` (span-in, `noexcept`, `expected_t` out). The outbound builder produces the **application body** (including `35=D`/`35=8` and the business fields) that `Engine::send` appends after the session-stamped header (`8=`/`9=`/`34=`/`49=`/`52=`/`56=`); the inbound accessor reads typed fields from the `MessageView` delivered to `fromApp`. Malformed/incomplete inbound business messages reuse 019's `BusinessMessageReject (35=j)` reject path.

Like `019` (and unlike the tests-only `016`/`018`), this feature **adds public production surface** (new typed message types + builders/accessors), so it carries a real Gate-A adjudication and a production-behavior Gate B.

## Clarifications

### Session 2026-06-04

- Q: NewOrderSingle order-type scope for the v1.0 minimal message? (FIX 4.4 dict marks Price=44 `required='N'` — conditionally required.) → A: **Limit-only** — the builder fixes OrdType to Limit (`40=2`) and always requires Price (`44`). Market/other order types fold into the deferred full-coverage obligation (FR-015a).
- Q: What execution semantics should the responding ExecutionReport carry for the round-trip? → A: **Filled (one fully-filled ExecutionReport per NewOrderSingle)** — ExecType=Filled (`150=F`, FIX 4.4), OrdStatus=Filled (`39=2`), LeavesQty=0, CumQty=OrderQty, AvgPx=Price. Exercises every minimal numeric field with a non-trivial value.
- Q: Input type for the typed numeric fields (Price, OrderQty, LeavesQty, CumQty, AvgPx)? → A: **`fixpp::decimal_t` (001)** — type-safe, canonical locale-independent formatting (directly satisfies FR-007); no new formatting code. (`fixpp::decimal_t` = `core::decimal<FIXPP_DECIMAL_T>`, `include/fixpp/core/decimal_alias.hpp`; builder serializes via `decimal_t::format(std::span<std::byte>)`, the generated read accessors return `decimal_t` via `decimal_t::parse(span, mr)`.)

### Session 2026-06-04 (Gate A round 1)

- **Numeric type is `fixpp::decimal_t`, not the non-existent `core::Decimal`.** The builder/accessor numeric type is `fixpp::decimal_t` (= `core::decimal<FIXPP_DECIMAL_T>`); the only construction path is `decimal_t::parse(std::span<const std::byte>, std::pmr::memory_resource*) -> expected_t<decimal_t>` and serialization is `.format(std::span<std::byte>) -> expected_t<std::size_t>`. There is no `parse(string-literal)` overload. (FR-007, Key Entities, data-model E1/E2, contract, quickstart.)
- **Send-path framing has two defects, not one.** Beyond MsgType landing in wire field-7 (D1), `send_impl` backpatches BodyLength zero-padded (`9=000045`), violating fixpp's own digit-only BodyLength contract (`.specify/2b-wire.md`, `wire::Writer::commit()`). The app-send path MUST emit MsgType in wire field-3 AND a digit-only (unpadded) `9=` AND a valid checksum. (FR-004a, FR-016, INV-1.)
- **Opaque app payloads are validated fail-closed before seqnum/store.** `Engine::send` accepts arbitrary 019 opaque payloads; the send path MUST validate exactly one leading `35=`, reject empty payload / duplicate `35=` / any embedded session header-or-trailer tag (8/9/34/49/52/56/10) with the new `error::app_payload_malformed` (slot 131) before consuming a seqnum or transmitting. (FR-016.)
- **The responding fixpp-acceptor `Application` is test/harness-side.** Both the QFJ/QFcpp responders and the fixpp-acceptor responder are test fixtures, not shipped production examples. The fixpp responder replies by calling `Engine::send` from inside `fromApp` (re-entrancy), which relies on 019's any-thread re-entrant `Engine::send` exec-hop; a named loopback test proves no deadlock/UAF before the live cells. (E3, FR-010, Assumptions.)
- **Catalogue: A-001/A-006 stay `backlog`.** They are codegen-owned all-version (4.0–5.0SP2) official rows; this minimal-FIX-4.4 hand-written slice does not close them. A gap-note records partial G2 interop evidence (this feature); the closure surface is a coverage-index partial-evidence note, not a row flip. (FR-014, Constitution-Check §VI.)

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Typed NewOrderSingle & ExecutionReport surface (Priority: P1)

A library user can **construct** a `NewOrderSingle` (35=D) and an `ExecutionReport` (35=8) from named, typed fields — without hand-assembling wire bytes — and originate them on an established session via `Engine::send`. Symmetrically, when such a message arrives at `fromApp`, the user can **read** its minimal fields through named, typed accessors rather than scanning raw tags. The builder enforces the minimal required-tag set fail-closed on **invalid input** (empty string for a required string field, an out-of-range enum char, an invalid/unformattable decimal, an ill-formed UTCTimestamp, or a too-small output buffer): it returns a typed error and never emits a malformed frame. (A field cannot be *omitted* — the free-function signatures are positional, so omission is a compile error; the runtime guarantee is fail-closed-on-invalid-value.) The accessor surfaces a missing/ill-typed required field as a typed error.

**Why this priority**: The typed message surface is the irreducible library deliverable of this feature and the prerequisite for every interop story — you cannot assert a *typed* business round-trip against a reference engine until the types exist. It is independently demonstrable entirely in-process (build → parse round-trip; round-trip through a local loopback engine exactly as the 019 witness does, but typed) and is a viable MVP on its own (it records partial G2 interop evidence against catalogue A-001/A-006 — FR-014, not a closure).

**Independent Test**: Build a `NewOrderSingle` from the minimal field set, assert the produced body is a wire-conformant `35=D` frame whose fields parse back to the input values; do the same for `ExecutionReport (35=8)`; then drive both through a local loopback engine (one acceptor + one initiator, TLS) via `Engine::send` and assert each is delivered to the peer's `fromApp` and reads back through the typed accessors with field fidelity. Assert a build with an invalid required field (empty string / out-of-range enum / invalid decimal / ill-formed UTCTimestamp / too-small buffer) returns a typed error and emits nothing.

**Acceptance Scenarios**:

1. **Given** the minimal NewOrderSingle field set (ClOrdID, Symbol, Side, OrderQty, OrdType, Price, TransactTime), **When** the user builds a `NewOrderSingle`, **Then** the result is a wire-conformant `35=D` body whose fields parse back to exactly the supplied values.
2. **Given** the minimal ExecutionReport field set (OrderID, ExecID, ExecType, OrdStatus, Symbol, Side, LeavesQty, CumQty, AvgPx), **When** the user builds an `ExecutionReport`, **Then** the result is a wire-conformant `35=8` body whose fields parse back to exactly the supplied values.
3. **Given** an established loopback session with a registered `Application`, **When** the user sends a typed `NewOrderSingle` via `Engine::send`, **Then** the peer's `fromApp` receives a `35=D` message and the typed accessors return the originated field values.
4. **Given** an inbound `NewOrderSingle`/`ExecutionReport` at `fromApp`, **When** the user reads it through the typed accessors, **Then** each minimal field is returned with its correct type and value.
5. **Given** a build request with an invalid required field (empty `cl_ord_id`/`symbol`/`order_id`/`exec_id`, an out-of-range `side`/`exec_type`/`ord_status` char, an unformattable decimal, an ill-formed `transact_time` UTCTimestamp, or a too-small `out` buffer), **When** the user invokes the builder, **Then** it returns a typed error and produces no usable frame (the returned span is absent; `out` contents must not be inspected).

---

### User Story 2 - Live business-message interop vs QuickFIX-J, both roles (Priority: P2)

The full order round-trip — `Logon → NewOrderSingle (35=D) → ExecutionReport (35=8) → Logout` — is exercised over a real TLS link against a **live QuickFIX-J** counterparty, in **both roles**: fixpp-initiator sending `NewOrderSingle` to a QuickFIX-J acceptor that replies with `ExecutionReport`, **and** fixpp-acceptor receiving `NewOrderSingle` from a QuickFIX-J initiator and replying with `ExecutionReport`. The QuickFIX-J counterparty runs a responding `Application` that emits one `ExecutionReport` for each inbound `NewOrderSingle`. The exchange is asserted to cross the wire (engine-log seam, as in 016/018) and the session survives to a clean `Logout`.

**Why this priority**: This is the externally meaningful deliverable — wire-level proof that fixpp's typed business messages interoperate with the most widely deployed open-source FIX engine, in both directions. It directly discharges the `[const §VII.6]` business-message interop clause (FR-027/SC-008) that has been carried as an open v1.0-GA residual since 016. It depends on US1 (the types must exist) so it follows P1.

**Independent Test**: Run the live cell pair (fixpp-initiator × QFJ-acceptor and fixpp-acceptor × QFJ-initiator) over `one_way_ca` TLS; assert fixpp originates/receives `35=D`, the QFJ `Application` emits `35=8`, fixpp receives/originates `35=8`, both seams capture the `35=D`/`35=8` frames, and both sessions reach a clean `Logout`. Skip cleanly when no counterparty is provisioned (existing harness skip-without-counterparty contract).

**Acceptance Scenarios**:

1. **Given** a live QuickFIX-J acceptor with a responding `Application`, **When** fixpp (initiator) sends a `NewOrderSingle`, **Then** QuickFIX-J replies with an `ExecutionReport` that fixpp receives at `fromApp` and reads with field fidelity, and both sides log out cleanly.
2. **Given** a live QuickFIX-J initiator that sends a `NewOrderSingle`, **When** fixpp (acceptor) receives it at `fromApp`, **Then** fixpp's `Application` replies with an `ExecutionReport` that QuickFIX-J accepts, and both sides log out cleanly.
3. **Given** either live role, **When** the round-trip completes, **Then** the engine-log seam captures both the `35=D` and `35=8` frames and the captured frames match the expected business-message goldens (modulo non-deterministic fields).

---

### User Story 3 - Live business-message interop vs QuickFIX-cpp, both roles (Priority: P3)

The same `Logon → NewOrderSingle → ExecutionReport → Logout` round-trip is exercised against a **live QuickFIX-cpp** counterparty in both roles, with a responding QuickFIX-cpp `Application` emitting an `ExecutionReport` per `NewOrderSingle`.

**Why this priority**: A second independent reference engine strengthens the interop evidence (QuickFIX-cpp and QuickFIX-J make different freedom-of-interpretation choices), but the core `[const §VII.6]` discharge is already achieved by US2; QuickFIX-cpp is additive assurance, so it is lowest priority.

**Independent Test**: Run the live cell pair (fixpp-initiator × QFcpp-acceptor and fixpp-acceptor × QFcpp-initiator) over `one_way_ca` TLS and assert the same round-trip + clean logout + seam capture as US2. Skip cleanly when no counterparty is provisioned.

**Acceptance Scenarios**:

1. **Given** a live QuickFIX-cpp acceptor with a responding `Application`, **When** fixpp (initiator) sends a `NewOrderSingle`, **Then** QuickFIX-cpp replies with an `ExecutionReport` that fixpp receives and reads, and both sides log out cleanly.
2. **Given** a live QuickFIX-cpp initiator that sends a `NewOrderSingle`, **When** fixpp (acceptor) receives it, **Then** fixpp replies with an `ExecutionReport` that QuickFIX-cpp accepts, and both sides log out cleanly.

---

### Edge Cases

- **Invalid required field on build** — the typed builder returns a typed error and emits no usable frame (US1 AS5; the returned span is absent and `out` contents are unspecified — the caller must not inspect them); it never produces a partial/malformed `35=D`/`35=8`. (Omission is impossible — positional signatures make it a compile error; the runtime guard is on invalid values: empty string, out-of-range enum char, invalid decimal, ill-formed UTCTimestamp, too-small buffer.)
- **Malformed / required-field-missing inbound business message** — when the user's `Application`, reading an inbound `35=D`/`35=8` through the typed accessors, gets an `expected_t` error and therefore returns a reject from `fromApp`, the engine path reuses 019's `BusinessMessageReject (35=j)` (catalogue A-014) so the peer is told why; the session survives. (The engine does not run the v44 validator on inbound app messages before `fromApp` — the typed-validation→reject decision is user `fromApp` code wired by 019, not automatic engine validation.)
- **Malformed opaque `Engine::send` payload** — because `Engine::send` still accepts arbitrary 019 opaque byte payloads (not only the typed builders' output), the send path validates the payload fail-closed BEFORE assigning a seqnum or transmitting: exactly one leading `35=` is required; an empty payload, a duplicate `35=`, or any embedded session header/trailer tag (8/9/34/49/52/56/10) is rejected with `error::app_payload_malformed` (slot 131) — no transmit, no seqnum consumption.
- **Counterparty rejects fixpp's NewOrderSingle** — if the live counterparty's `Application` rejects (e.g. unknown symbol) and replies with a `BusinessMessageReject`/`ExecutionReport(150=8 Rejected)`, fixpp surfaces it to `fromApp` without crashing and the session survives.
- **Decimal/qty/price formatting** — Price (44), OrderQty (38), LeavesQty (151), CumQty (14), AvgPx (6) are typed as `fixpp::decimal_t` and serialize via its canonical `format(span)` (no locale-dependent or scientific notation); round-trip fidelity is asserted by `decimal_t` value-equality (counterparties may emit different trailing-zero forms, e.g. `190.5` vs `190.50`).
- **Timestamp fields** — TransactTime (60) MUST be a valid UTCTimestamp the counterparties accept (same caller-pre-formatted contract as SendingTime(52), which 016 F1 proved real engines validate); the builder validates its length+shape fail-closed and returns a typed error for a malformed value.
- **Field ordering** — the body field order MUST satisfy both reference engines' tolerance (header tags first where required); ordering is fixed by the builder, not the caller. On the wire, MsgType(35) MUST be the third field (after 8=/9=) and BodyLength MUST be digit-only (unpadded).
- **Engine-stamped header vs body** — the typed builder MUST NOT emit session header tags (`8`/`9`/`34`/`49`/`52`/`56`) that `Engine::send`/`Session::send_impl` already stamps, to avoid duplicate-tag rejection by the peer.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST provide a typed representation of **NewOrderSingle (35=D)** (catalogue A-001) covering the minimal field set: ClOrdID (11), Symbol (55), Side (54), OrderQty (38), OrdType (40), Price (44), TransactTime (60). For v1.0 the order type is **Limit-only** — OrdType is fixed to Limit (`40=2`) and Price (44) is therefore always required (other order types are deferred per FR-015a).
- **FR-002**: The system MUST provide a typed representation of **ExecutionReport (35=8)** (catalogue A-006) covering the minimal field set: OrderID (37), ExecID (17), ExecType (150), OrdStatus (39), Symbol (55), Side (54), LeavesQty (151), CumQty (14), AvgPx (6). The round-trip's responding ExecutionReport carries **fully-filled** semantics: ExecType=Trade (`150=F`, the FIX 4.4 wire value for a fill) with OrdStatus=Filled (`39=2`), LeavesQty=0, CumQty=OrderQty, AvgPx=Price.
- **FR-003**: The system MUST provide a builder that produces, from typed minimal fields, a wire-conformant application body (including `35=D`/`35=8`) suitable as the `Engine::send` payload, following the `admin_messages.hpp` builder contract (span-in, `noexcept`, `expected_t` out).
- **FR-004**: The builder MUST NOT emit session header tags that the engine send path already stamps (`8`/`9`/`34`/`49`/`52`/`56`) nor the `10=` trailer, and MUST emit business fields in an order both reference engines accept.
- **FR-004a**: The application-message send path (`Engine::send` → `Session::send_impl`) MUST produce a wire frame in which (i) **MsgType(35) is the third field** (immediately after `8=BeginString` and `9=BodyLength`), (ii) **BodyLength(9) is digit-only / unpadded** (no leading zeros — fixpp's own wire contract, `.specify/2b-wire.md`, which `wire::Writer::commit()` already honors), and (iii) the **CheckSum(10) is valid** over the emitted bytes. (Today `send_impl` emits MsgType 7th and zero-pads `9=000045`; both are non-conformant. Recommended implementation for `/implement` — not done here: route app sends through `wire::Writer`, or correct the manual framer to hoist the payload's leading `35=` to field-3 and write digit-only BodyLength.) This touches the proven 015/019 send path and carries a real Gate B (Complexity Tracking).
- **FR-005**: The builder MUST reject a build with any invalid required minimal field — empty string for a required string field, an out-of-range enum char (`side`/`exec_type`/`ord_status`), an invalid/unformattable decimal, an ill-formed UTCTimestamp (TransactTime), or a too-small output buffer — returning a typed error and producing no usable frame. On failure the returned span is absent and the caller MUST NOT inspect `out` (fail-closed; no partial/malformed emission). (A required field cannot be *omitted*: the positional free-function signatures make omission a compile error.)
- **FR-006**: The system MUST provide typed accessors that read each minimal field — with its correct type — from an inbound `NewOrderSingle`/`ExecutionReport` `MessageView` as delivered to `fromApp`, returning a typed error for a missing/ill-typed required field.
- **FR-007**: Numeric fields (Price, OrderQty, LeavesQty, CumQty, AvgPx) MUST be typed as **`fixpp::decimal_t`** (001; `= core::decimal<FIXPP_DECIMAL_T>`) on the public builder/accessor API. The builder MUST serialize them via `decimal_t::format(std::span<std::byte>)` in `decimal_t`'s canonical, locale-independent decimal form that both reference engines parse; on read the generated accessors return `decimal_t` via `decimal_t::parse(span, mr)`. Numeric fidelity is asserted by `decimal_t` **value-equality** (not byte-match), round-tripping to the originated value.
- **FR-008**: TransactTime (60) is a caller-pre-formatted `std::string_view` (same contract as SendingTime); the builder MUST **validate it fail-closed** as a well-formed UTCTimestamp (length + shape) and return a typed error for a malformed value, and MUST serialize a value the reference engines accept.
- **FR-009**: When the user's `Application`, having read an inbound business message through the typed accessors and obtained an `expected_t` validation error, returns a reject from `fromApp`, the engine MUST drive the existing `BusinessMessageReject (35=j)` path (catalogue A-014, wired by 019) so the peer receives a business-level reject and the session survives. (This is user-`fromApp` responsibility — the engine does not auto-run the v44 validator on inbound app messages before `fromApp`.)
- **FR-010**: The system MUST demonstrate a live `Logon → NewOrderSingle → ExecutionReport → Logout` round-trip against **QuickFIX-J** in **both roles** (fixpp-initiator↔QFJ-acceptor and fixpp-acceptor↔QFJ-initiator) over `one_way_ca` TLS, with a responding QuickFIX-J `Application` emitting one ExecutionReport per NewOrderSingle.
- **FR-011**: The system MUST demonstrate the same live round-trip against **QuickFIX-cpp** in both roles.
- **FR-012**: Live interop assertions MUST capture the `35=D` and `35=8` frames via the engine-log seam (per 016/018) and compare against business-message goldens (modulo non-deterministic fields: seqnum, sending/transact time, IDs as applicable).
- **FR-013**: Live interop cells MUST skip cleanly (not fail) when no reference-engine counterparty is provisioned, per the existing harness skip-without-counterparty contract.
- **FR-014**: The feature MUST record the discharge of the `[const §VII.6]` business-message interop clause (interop roadmap FR-027/SC-008) WITHOUT flipping catalogue rows **A-001** / **A-006** to `done`. A-001/A-006 are codegen-owned **all-version (4.0–5.0SP2) official** rows; this hand-written minimal-FIX-4.4 slice does not deliver their full semantics. The catalogue update is: (a) A-001/A-006 stay `backlog` with a **gap-note** recording partial G2 interop evidence from this feature (cite `020-g2-business-messages`, minimal FIX-4.4 NOS→ExecRpt live interop, both roles); (b) the coverage-index records the same as a **partial-evidence note** (not a closure) against the A-001/A-006 application-layer rows. (Closing the official rows requires the deferred codegen writer-emitter + full-field + all-version coverage — FR-015a/FR-015b.)
- **FR-015**: The feature MUST record the two forward obligations as tracked deferrals (NOT implemented in v1.0): (a) full FIX 4.4 field/group coverage for NewOrderSingle/ExecutionReport beyond the minimal set; (b) all-protocol-version coverage (FIX 4.2 / FIX 5.0 SP2 / FIXT.1.1) of these business messages, scheduled as a post-v1.0 obligation — captured in `spec/behaviors-and-limitations.md` and the deferred-work registry.
- **FR-016**: The application-message send path MUST validate the opaque `Engine::send` payload **fail-closed before** assigning a seqnum or transmitting (this applies to all 019 opaque callers, not only the typed builders): exactly **one** leading `35=` is required; an empty payload, a duplicate `35=`, or any embedded session header/trailer tag (`8`/`9`/`34`/`49`/`52`/`56`/`10`) MUST be rejected with the new `error::app_payload_malformed` (slot **131** — next free after 019's 129/130); on rejection there is **no transmit and no seqnum consumption**.

### Key Entities *(include if feature involves data)*

- **NewOrderSingle (35=D, A-001)** — a client order instruction. Minimal fields: **ClOrdID (11)** client order id (string); **Symbol (55)** instrument (string); **Side (54)** buy/sell (enum char); **OrderQty (38)** quantity (`fixpp::decimal_t`); **OrdType (40)** order type (fixed to Limit `2` for v1.0); **Price (44)** limit price (`fixpp::decimal_t`, always required given Limit); **TransactTime (60)** order time (UTCTimestamp).
- **ExecutionReport (35=8, A-006)** — an execution/ack of an order. Minimal fields: **OrderID (37)** exchange order id (string); **ExecID (17)** execution id (string); **ExecType (150)** execution event type (enum char; round-trip emits Trade `F`, the 4.4 wire value for a fill); **OrdStatus (39)** order status (enum char; round-trip emits Filled `2`); **Symbol (55)** instrument (string); **Side (54)** buy/sell (enum char); **LeavesQty (151)** open quantity (`fixpp::decimal_t`); **CumQty (14)** filled quantity (`fixpp::decimal_t`); **AvgPx (6)** average fill price (`fixpp::decimal_t`).
- **Responding counterparty Application** — a test/harness-side `Application` that, on receiving a `NewOrderSingle`, emits one `ExecutionReport`. There are two kinds, **both test fixtures, not shipped fixpp production entities**: (a) the reference-engine responders (QuickFIX-J and QuickFIX-cpp Applications); (b) the **fixpp-acceptor responder** for the both-role cells — a test-fixture fixpp `Application` (built on 019 + the E2 builder) that replies by calling `Engine::send` from inside its own `fromApp` (send-from-callback re-entrancy; see Assumptions + the named loopback test).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A user can construct and send a NewOrderSingle and an ExecutionReport using only named typed fields — zero hand-assembled wire bytes — and read both back through typed accessors (records partial G2 interop evidence against catalogue A-001/A-006; does NOT close those all-version official rows — FR-014).
- **SC-002**: Every minimal field on both messages round-trips build→wire→parse with 100% value fidelity (no loss/format drift on numeric or timestamp fields).
- **SC-003**: A typed `NewOrderSingle → ExecutionReport` round-trip completes against a **live QuickFIX-J** counterparty in **both roles**, with both messages captured on the seam and matching goldens — discharging the `[const §VII.6]` v1.0-GA business-message interop clause.
- **SC-004**: The same round-trip completes against a **live QuickFIX-cpp** counterparty in both roles.
- **SC-005**: A build missing a required field, and an inbound business message failing typed validation, are each handled fail-closed (typed error / `BusinessMessageReject`) with no malformed emission and no session crash.
- **SC-006**: The two forward obligations (full-field coverage; all-protocol-version coverage) are recorded as tracked, scheduled deferrals — discoverable in the behaviors-and-limitations catalogue and the deferred-work registry.

## Out of Scope (deferred — tracked, NOT implemented in v1.0)

- **Full FIX 4.4 field/group coverage** for NewOrderSingle/ExecutionReport beyond the minimal set (e.g. TimeInForce, Account, Currency, ExecInst, party/allocation repeating groups). Tracked as a forward obligation (FR-015a).
- **All-protocol-version coverage** of these business messages — **FIX 4.2 / FIX 5.0 SP2 / FIXT.1.1**. **MUST be scheduled as a post-v1.0 obligation** (FR-015b). This is the interop roadmap's G4 axis for business messages.
- **Live Fix8 pairing** — Fix8 stays **corpus-only** at v1.0 (interop roadmap FR-009); no live business-message cells.
- **Additional business message types** beyond NewOrderSingle/ExecutionReport (e.g. OrderCancelRequest 35=F, OrderCancelReject 35=9) — second-tier, deferred.
- **In-place outbound message modification** (mutating fields mid-emit) — already deferred by 019; not introduced here.
- **C ABI surface** for the typed messages — Phase-5 later slices; out of scope (consistent with 019).

## Assumptions

- The minimal field sets enumerated above are sufficient for a clean single-order round-trip against QuickFIX-J and QuickFIX-cpp on FIX 4.4 (the negotiated wire version in the live cells); any additional tag a reference engine demands for acceptance will be added to the minimal set during interop bring-up (treated as part of "minimal", not as full-coverage scope creep).
- The 019 `Application` + `Engine::send` surface and its strand/keepalive/lifetime contract (L-019-3, L-015-4) are sufficient to originate and observe these messages; no *new* engine concurrency surface is introduced by this feature. **This RELIES on 019's any-thread re-entrant `Engine::send`** (exec-hop + registry keepalive + RAII send-drain): the fixpp-acceptor responder calls `Engine::send` from inside `fromApp` (send-from-callback re-entrancy), a pattern 019's single-threaded harness never exercised (L-019-3). Before the live cells, a named loopback test ("send-from-inside-fromApp" under a multi-threaded `io_context`) MUST prove no deadlock/UAF on this re-entrant path; if it cannot, the reply is hoisted off-strand (explicit waiver).
- The 016/018 live-interop harness (engine-log seam goldens, both-role orchestration, `one_way_ca` TLS, skip-without-counterparty) is reused; the new work is business-message cells + responding counterparty `Application`s, not new harness infrastructure.
- The typed messages are thin builders/accessors over the existing wire/ + dictionary/ surfaces; no new wire codec or dictionary-codegen capability is required for the minimal set.
- FIX 4.4 is the live wire version (matching 016/018); FIX 4.2 used by some existing unit fixtures is incidental and does not change the live-interop scope.

## Dependencies

- **019-app-callbacks** (MERGED) — public `Application` (`fromApp`/`toApp`), any-thread `Engine::send`, and the `BusinessMessageReject (35=j)` builder (A-014).
- **016-interop-harness** / **018-interop-live-admin** (MERGED) — live both-role orchestration, engine-log seam goldens, `one_way_ca` TLS counterparty provisioning, skip-without-counterparty contract.
- **wire/** (PR #68) + **dict/** (PR #66/#67) — the codec + dictionary surfaces the typed builders/accessors are built on.
- **Reference engines** — QuickFIX-J 3.0.1 and QuickFIX-cpp v1.16.0 (already provisioned per the 016 live harness); Fix8 corpus-only.
