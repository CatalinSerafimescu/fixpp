# Feature Specification: PossResend(97) Inbound + AllowPossDup Send-Path Strip Knob

**Feature Branch**: `022-possresend-allowpossdup-send`
**Created**: 2026-06-05
**Status**: Draft
**Input**: User description: "PossResend(97) inbound application-resend semantics + the deferred AllowPossDup send-path strip knob (021 FR-008), finishing catalogue row S-010 (flip backlog→done). Grounded vs the live interop targets QuickFIX-cpp v1.16.0 and QuickFIX-J 3.0.1."

## Context & Background

This is the second slice of interop gate **G3** and the direct continuation of `021-inbound-possdup-origsendingtime`, which delivered the session-level `PossDupFlag(43)` / `OrigSendingTime(122)` inbound half of catalogue row **S-010** and explicitly deferred two pieces to a follow-up slice. This feature delivers both deferred pieces so S-010 can flip `backlog → done`:

1. **`PossResend(97)` inbound application-resend semantics.** `PossResend(97)` is an **application-level** duplicate indicator, distinct from the session-level `PossDupFlag(43)`. A counterparty sets `97=Y` on a business message when it believes it may be re-sending business content (e.g. after an application-layer retry), but the message carries a *new, in-sequence* `MsgSeqNum` — it is **not** a session-level retransmission. The receiving engine must therefore process it normally (the sequence number advances) and deliver it to the registered application so the application can make the business-level duplicate decision against its own keys (e.g. `ClOrdID`). The session must **not** reject, drop, or disconnect merely because `97=Y` is present. fixpp has no explicit `PossResend(97)` handling today (verified — no `src`/`include` reference to tag 97); this slice makes the disposition explicit and wire-conformant, and proves it against the two live interop engines.

2. **The `AllowPossDup` send-path strip knob (021 FR-008).** 021 recorded the intended behavior — a session-configuration knob that controls whether a plain caller `send` retains or strips caller-supplied `PossDupFlag(43)` / `OrigSendingTime(122)`, **default strip**; the automatic resend/retransmission path always (re)adds them — but deferred the implementation because the plain-`send` path is **opaque-payload** (it copies the business body verbatim and does not field-parse it), so stripping requires a *new* boundary-anchored field-excision over the opaque payload. That excision carries the same delimiter/SOH-injection hazard fixpp hit in 020 RC#1 (verbatim caller-field copy), so it must be implemented fail-closed against malformed framing.

Scope is FIX 4.4 session-layer only (interop matrix option (a)); FIXT.1.1 / FIX 5.0 SP2 routing remains in G4.

## User Scenarios & Testing *(mandatory)*

### User Story 1 — Accept and deliver an application possible-resend (Priority: P1)

A counterparty (QuickFIX-cpp or QuickFIX-J) sends an in-sequence business message carrying `PossResend(97)=Y` because it is re-issuing business content after an application-level retry. fixpp must process the message normally — advancing its expected inbound sequence number — and deliver it to the registered application so the application can decide, on its own business keys, whether it has already acted on it. fixpp must not session-reject, drop, or disconnect the message for being a possible resend.

**Why this priority**: This is the interop-correctness core of the inbound half. A live counterparty that performs application-level resends with `97=Y` is a normal, spec-sanctioned flow; mis-rejecting or silently dropping it would break business-message interop and is the exact class of gap G3 exists to close.

**Independent Test**: Feed an established session an in-sequence application message with `97=Y`; assert the expected inbound sequence number advances by one, the message is delivered to the application's `fromApp` callback (with tag 97 readable in the delivered message), no `Reject`/`Logout`/disconnect is emitted, and the session stays `Active`.

**Acceptance Scenarios**:

1. **Given** an established session whose next expected inbound `MsgSeqNum` is N, **When** an application message arrives with `MsgSeqNum = N` and `PossResend(97)=Y` (and no `PossDupFlag(43)`), **Then** fixpp processes it once, advances the expected inbound sequence number to N+1, delivers it to the registered application, and emits no `Reject`, `Logout`, or disconnect.
2. **Given** an established session with **no** registered application, **When** an in-sequence message with `97=Y` arrives, **Then** fixpp's disposition is byte-identical to its handling of the same message without `97` (PossResend carries no session-level side effect; the no-application default of 019 is preserved).
3. **Given** an inbound message carrying **both** `PossDupFlag(43)=Y` and `PossResend(97)=Y`, **When** it is processed, **Then** the 021 session-level PossDup disposition (Arms A–E, including the `OrigSendingTime(122)` requirement keyed on `43=Y`) is applied independently of `97`, and `97` adds no further session-level reject of its own.

---

### User Story 2 — Strip caller-supplied duplicate flags on a plain send (Priority: P2)

An operator wants fixpp to own the session-level duplicate flags. By default, when application code calls a plain `send`, fixpp removes any caller-supplied `PossDupFlag(43)` and `OrigSendingTime(122)` from the outgoing business payload, so those tags are emitted only by fixpp's own automatic retransmission path. An operator who manages duplicate flags themselves can opt to retain caller-supplied values.

**Why this priority**: Wire-conformant ownership of `43`/`122` keeps fixpp's outbound flags trustworthy and matches the QuickFIX-J `AllowPossDup` convention, but it is secondary to the inbound accept/deliver behavior in US1 (an interop counterparty must first not be broken on receive). It is also the more invasive half (a new opaque-payload excision), so it is sequenced after US1.

**Independent Test**: With the knob at its default, call a plain `send` whose payload contains `43=Y` and `122=<ts>`; capture the framed outbound bytes and assert neither tag 43 nor tag 122 appears in the application portion. Then flip the knob to retain and assert both are passed through unmodified. Then drive a retransmission and assert fixpp's auto-resend re-adds `43=Y` + `122` regardless of the knob.

**Acceptance Scenarios**:

1. **Given** the `AllowPossDup` knob at its default (strip), **When** application code calls a plain `send` with a payload containing `PossDupFlag(43)` and/or `OrigSendingTime(122)`, **Then** fixpp removes the complete `43=…`<SOH> and `122=…`<SOH> fields from the outbound application payload before framing, leaving all other fields intact.
2. **Given** the `AllowPossDup` knob set to retain, **When** the same `send` is made, **Then** the caller-supplied `43`/`122` are passed through unmodified.
3. **Given** any value of the knob, **When** fixpp's automatic resend/retransmission path replays a stored message, **Then** it emits `PossDupFlag(43)=Y` and `OrigSendingTime(122)` (the knob governs only the plain caller-send path, never the resend path).
4. **Given** a plain `send` whose payload framing cannot be validated as well-formed `<tag>=<value><SOH>` fields, **When** the strip is attempted, **Then** fixpp fails the send closed (the existing opaque-payload validation disposition) rather than excising bytes by an unanchored match — no caller field may inject or forge a `43`/`122` boundary.

### Edge Cases

- **`97=Y` at a too-high sequence number**: handled by the existing too-high / resend-request path; `PossResend` does not alter the too-high disposition.
- **`97=Y` at a too-low sequence number without `43=Y`**: this is a malformed/ambiguous combination (a true retransmission would carry `43=Y`); it follows the existing too-low disposition for a non-PossDup message (the 021 Arm-B fatal too-low path) — `97` alone does not grant too-low tolerance.
- **`97=Y` on an administrative message**: PossResend is an application-level flag; an admin message bearing `97` is processed on the existing admin path and `97` has no effect.
- **Strip when the field is absent**: a plain `send` with no `43`/`122` in the payload is emitted unchanged under either knob setting (no-op strip).
- **Partial / repeated `43`/`122`**: the strip removes every well-formed occurrence of tags 43 and 122 in the application payload; a value containing an embedded `=` or no trailing `<SOH>` must not cause over- or under-excision (fail-closed on malformed framing).
- **Both roles**: both behaviors must hold whether fixpp is the initiator or the acceptor.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: An inbound **application** message carrying `PossResend(97)=Y` at the expected inbound `MsgSeqNum` MUST be processed normally — the expected inbound sequence number advances — and MUST be delivered to the registered application for business-level duplicate determination; the session MUST NOT reject, drop, or disconnect it because `97=Y` is present.
- **FR-002**: `PossResend(97)=Y` MUST carry **no** session-level duplicate semantics of its own and MUST NOT, by itself, trigger any session-level `Reject`, `Logout`, or disconnect (it is distinct from `PossDupFlag(43)`).
- **FR-003**: `PossResend(97)=Y` MUST NOT trigger the `OrigSendingTime(122)`-required validation — that requirement (021 Arms C/D) is keyed on `PossDupFlag(43)=Y` only. A `97=Y` message without `122` (and without `43=Y`) MUST NOT be rejected for a missing `122`.
- **FR-004**: When an inbound message carries **both** `PossDupFlag(43)=Y` and `PossResend(97)=Y`, the 021 session-level PossDup disposition (Arms A–E) MUST be evaluated on `43` independently of `97`; `97` MUST add no additional session-level reject.
- **FR-005**: When no application is registered, an inbound `97=Y` message MUST be handled byte-identically to the same message without `97` (PossResend has no session-level effect; preserves the no-application default established by 019).
- **FR-006**: A session-configuration knob (`AllowPossDup`) MUST control whether a plain caller `send` retains or strips caller-supplied `PossDupFlag(43)` and `OrigSendingTime(122)` from the outbound application payload. The default MUST **strip** both tags.
- **FR-007**: The automatic resend / retransmission path MUST always (re)add `PossDupFlag(43)=Y` and `OrigSendingTime(122)` to replayed messages, **regardless** of the `AllowPossDup` knob — the knob governs only the plain caller-send path.
- **FR-008**: The strip MUST be a **boundary-anchored field excision** over the opaque application payload that removes only complete `43=…`<SOH> and `122=…`<SOH> fields, and MUST be safe against delimiter/SOH injection in adjacent caller fields. A payload whose `<tag>=<value><SOH>` framing cannot be validated MUST fail the send **closed** (consistent with the 020 opaque-payload validation floor, slot 131 `app_payload_malformed`, and INV-2 / FR-004 — see `[[feedback_delimiter_injection_verbatim_field_copy]]`); the strip MUST NOT excise bytes by an unanchored substring match.
- **FR-009**: With `AllowPossDup` set to **retain**, caller-supplied `43`/`122` MUST be passed through unmodified (operator opt-in for callers that manage their own duplicate flags).
- **FR-010**: Both the inbound `PossResend(97)` disposition and the `AllowPossDup` send-path knob MUST interop cleanly with live QuickFIX-cpp v1.16.0 and QuickFIX-J 3.0.1 counterparties in both initiator and acceptor roles.
- **FR-011**: This feature MUST complete catalogue row **S-010**, flipping it `backlog → done` once `PossResend(97)` inbound semantics and the `AllowPossDup` send-path knob ship; the 021 partial-delivery note is superseded.

### Key Entities

- **PossResend disposition** (inbound): the classification of an inbound message bearing `PossResend(97)=Y` — at expected seqnum and an application message → process-once + deliver-to-application; admin or out-of-sequence → existing path with no `97` effect. No new persistent store; no session-level reject is introduced by `97`.
- **AllowPossDup knob** (send): an additive `SessionConfig` setting (default strip) governing whether a plain caller `send` retains or removes caller-supplied `43`/`122` from the opaque application payload, never affecting the automatic resend path.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In 100% of cases where a live counterparty sends an in-sequence application message with `PossResend(97)=Y`, the fixpp session processes it, advances its sequence number, delivers it to the application, and stays established (no spurious `Reject`/`Logout`/disconnect).
- **SC-002**: With the `AllowPossDup` knob at its default, 100% of plain `send` calls whose payload contains `43`/`122` emit outbound application bytes free of tags 43 and 122; with the knob set to retain, both tags are passed through unchanged.
- **SC-003**: The automatic resend path emits `PossDupFlag(43)=Y` + `OrigSendingTime(122)` on 100% of replayed messages irrespective of the knob (zero regression in retransmission flags).
- **SC-004**: A malformed/unframeable plain-`send` payload fails closed (no byte excised by an unanchored match), matching the 020 opaque-payload validation floor.
- **SC-005**: fixpp interops cleanly with QuickFIX-cpp v1.16.0 and QuickFIX-J 3.0.1 across the `PossResend` and `AllowPossDup` scenarios in both initiator and acceptor roles (live interop cells green under normal + sanitizer builds), and catalogue row S-010 is flipped `done`.

## Normative References

Per `[const §VI.5]` (exact coverage-index refs matching the catalogue S-010 row):

- `[FIX-SL §4.8.4] Possible duplicates` — `PossDupFlag(43)` + `PossResend(97)` + `OrigSendingTime(122)` retransmission / resend semantics (the S-010 anchor; `feature-catalogue.md:30`). PossResend is the application-level resend indicator; OrigSendingTime is required only for `PossDupFlag=Y`.
- `[FIX-SL §4.5.4] Rejecting invalid messages` — session-level `Reject(35=3)` field set, referenced only to confirm that `97=Y` alone produces **no** such reject.

## Assumptions

- **FIX 4.4 session-only scope** (interop matrix option (a)). FIXT.1.1 / FIX 5.0 SP2 parse routing and `DefaultApplVerID(1137)` remain deferred to G4 and are out of scope here.
- **No explicit `PossResend(97)` handling exists today** (verified — zero `src`/`include` reference to tag 97). The inbound half therefore makes an implicit disposition explicit and wire-conformant; the code delta on the inbound side may be witness-dominant (confirming the generic in-sequence app path already delivers `97=Y` to the application and that no guard mis-rejects it) plus any guard correction the reference-engine sweep surfaces. The send-path strip is the substantial half.
- **Canonical disposition follows QuickFIX-cpp / QuickFIX-J**, the two live interop targets; the `/speckit-clarify` step will ground the exact `PossResend` disposition and `AllowPossDup` default against a CodeGraph sweep of both engines (as 021 did for PossDup), and confirm the 021-recorded **default = strip** intent for the send-path knob.
- **The send-path strip reuses the 020 opaque-payload framing discipline** — it does not introduce a new app-message parsing pipeline; it adds a fail-closed, boundary-anchored excision over the existing opaque send payload, governed by an additive `SessionConfig` knob.
- **Existing engine machinery is reused**: the 019 `Application`/`fromApp` delivery path, the 021 inbound PossDup arms, the 020 send framing + opaque-payload validation (slot 131), the automatic resend path, and the sequence-number state already exist; this feature wires the two deferred behaviors into them rather than introducing new infrastructure. **No new persistent store, codegen, or error slot is anticipated.**
- **Scope-size risk (noted for Gate A)**: this slice bundles two surfaces (inbound `PossResend` + the send-path excision) that 021's Gate A split apart once. If the send-path excision proves larger than the inbound half can share a slice with, the pipeline may split US2 into its own follow-up; both are required to flip S-010 `done`.
