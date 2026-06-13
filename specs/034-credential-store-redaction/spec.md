# Feature Specification: Credential redaction at the message-store boundary

**Feature Branch**: `034-credential-store-redaction`
**Created**: 2026-06-13
**Status**: Draft
**Input**: User description: "Redact the cleartext Password(554) before an outbound FIXT Logon is persisted to the message store."

## User Scenarios & Testing *(mandatory)*

Context: when a FIXT session is configured with logon credentials, the outbound Logon
(`35=A`) frame carries `Username(553)` and `Password(554)`. The engine persists every
outbound frame to the configured message store *before* transmitting it (durable-before-send),
so when that store is a **persistent** one, the session's own `Password(554)` is written to disk
verbatim and remains readable for the life of the store file. Application logs, telemetry spans,
and metrics already carry no credential material — this feature concerns **only** the message-store
persistence boundary. The counterparty still requires the real password on the wire to authenticate.

### User Story 1 - Session password is not recoverable from the store file (Priority: P1)

An operator runs a FIXT session with a configured password and a persistent message store. After
the session logs on, anyone who can read the store file (operator, backup, forensic reader) must
**not** be able to recover the session's password from it.

**Why this priority**: This is the entire point of the feature — removing an at-rest cleartext-secret
exposure. Without it, the feature delivers nothing.

**Independent Test**: Configure a session with a known password and a persistent (on-disk) store,
drive a logon so the outbound Logon is persisted, then read the raw store-file bytes and assert the
literal password string is absent and a same-length masked placeholder is present in the stored
Logon record. Verified directly against the persisted bytes, not via any retrieval API.

**Acceptance Scenarios**:

1. **Given** a session configured with `Password(554)` = a known secret and a persistent store, **When** the outbound Logon is persisted, **Then** the store file contains no occurrence of the cleartext secret and the stored Logon's 554 value is a masked placeholder of identical length.
2. **Given** the same session, **When** the stored Logon is read back through the store's retrieval path, **Then** the returned frame is structurally valid (offsets and record integrity intact) and its 554 value is masked.
3. **Given** an **acceptor** that replies to an inbound Logon with its own configured `Password(554)`, **When** that reply Logon is persisted, **Then** its stored 554 value is masked identically (both roles are covered).

### User Story 2 - The counterparty still authenticates (Priority: P2)

The masking must affect only what is stored, never what is sent. The peer must receive the real
credential and the session must establish exactly as it does today.

**Why this priority**: A fix that protected the store but broke authentication would be a regression;
this story guards the wire-side invariant.

**Independent Test**: Capture the transmitted Logon bytes (or run a live logon against a reference
counterparty) and assert the on-the-wire `Password(554)` equals the configured cleartext value and
the session reaches an established state.

**Acceptance Scenarios**:

1. **Given** a session with a configured password, **When** the Logon is transmitted, **Then** the wire frame's `Password(554)` is the original unmasked value.
2. **Given** a live counterparty that validates credentials, **When** the session logs on, **Then** establishment succeeds (no authentication failure introduced by masking).

### User Story 3 - Zero observable change when there is no secret (Priority: P3)

When there is nothing to protect — a credential-free Logon or the default (no-credential)
configuration — behavior is byte-for-byte unchanged.

**Why this priority**: Guards against scope creep and accidental regressions in the common
default-configuration path; lets the change ship as a safe, opt-out-free no-op for existing users.

**Independent Test**: For (a) a credential-free Logon, (b) the default (no-credential) configuration,
and (c) any non-Logon outbound frame, compare the persisted/recorded byte stream against the
pre-change baseline and assert it is identical.

**Acceptance Scenarios**:

1. **Given** a Logon with no `Password(554)` field, **When** it is persisted, **Then** the stored bytes are identical to current behavior (no masking, no length change).
2. **Given** any non-Logon outbound frame, **When** it is persisted, **Then** the stored bytes are unchanged.

> Note (per Clarifications): masking is applied **unconditionally at the store-entry boundary** for
> every store backend, so a credentialed Logon's stored copy is masked even in an **in-memory** store
> (a safe, invisible change — stored admin frames are never replayed). The "no observable change"
> guarantee therefore scopes to the **credential-free / default** cases above, not to a credentialed
> Logon on a non-persistent store.

### Edge Cases

- **Both handshake Logons carry credentials**: the initiator's own Logon and the acceptor's reply Logon each advertise their own configured credential — both stored copies must be masked.
- **Logon without a password (username-only or credential-free)**: no 554 field to mask → stored unchanged.
- **Non-Logon outbound frames**: must never be altered, even if they were to contain a `554`-looking byte sequence — only a genuine SOH-delimited `Password(554)` field on a `35=A` Logon is masked.
- **Password value at the framing boundary**: the 554 value cannot contain a field delimiter (SOH) or `=` (already enforced when the Logon is built); same-length masking therefore cannot corrupt framing.
- **Maximum-size frame**: masking must work without growing the frame or allocating, up to the maximum supported frame size.
- **Resend of a gap that spans the stored Logon**: administrative frames are folded into a `SequenceReset-GapFill` and never replayed verbatim, so the masked stored copy is never transmitted — masking cannot cause a wire/stored mismatch a peer could observe.

## Clarifications

### Session 2026-06-13

- Q: For a session WITH a configured password using a non-persistent (in-memory) store, should the stored Logon copy still be masked? → A: Yes — mask **unconditionally at the store-entry boundary** for every store backend (in-memory and null included), not only persistent stores. Simpler (no persistence-discriminator branch) and defense-in-depth (no store's retrieval path ever yields the secret). The "byte-identical no-op" guarantee consequently scopes to credential-free Logons / default configuration, not to a credentialed Logon on a non-persistent store.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: When persisting an **outbound** administrative **Logon (`35=A`)** frame that contains a genuine SOH-delimited `Password(554)` field, the system MUST write to the store a copy whose 554 **value** is replaced with a masking placeholder — never the cleartext password.
- **FR-002**: The frame **transmitted** to the counterparty MUST contain the original, unmasked `Password(554)`. Persistence masking MUST NOT alter the wire frame.
- **FR-003**: The masked stored frame MUST have the **same byte length** as the original (same-length substitution), so all field offsets and the store's per-record integrity remain valid.
- **FR-004**: Masking MUST apply on **both** roles: the initiator's own Logon and the acceptor's reply Logon.
- **FR-005**: Only a genuine SOH-delimited `Password(554)` field is masked. A Logon without a 554 field MUST be persisted unchanged. `Username(553)` MUST NOT be masked (it is an identity, not a secret).
- **FR-006**: Scope is restricted to the **outbound direction** and the **Logon (`35=A`)** message type. Inbound/peer frames and all non-Logon outbound frames MUST be persisted unchanged.
- **FR-007**: For a **credential-free Logon** (no `Password(554)` field) and for the default no-credential configuration, the persisted/recorded bytes MUST be **byte-identical** to current behavior. (A credentialed Logon is masked regardless of store backend — see FR-009.)
- **FR-008**: Masking MUST NOT allocate heap memory on the store-persistence path (consistent with the zero-allocation persistence constraint, `[const §VIII.5]`).
- **FR-009**: The masking MUST be applied **unconditionally at the single point where outbound frames enter the store**, so it applies uniformly to **every** store backend (in-memory, file, null) and any future store implementation inherits it (no per-backend duplication, no persistent-vs-non-persistent branch). A credentialed Logon's stored copy is therefore masked even in a non-persistent store.
- **FR-010** *(documentation)*: The behaviors-and-limitations catalogue MUST be updated to record the mitigation (superseding limitation **L-033-6**), and the stale 033 disposition claim that "no production frame persistence exists" MUST be corrected via a dated note — **without** rewriting merged history.

### Key Entities

- **Outbound Logon frame (`35=A`)**: the administrative handshake message that optionally carries `Username(553)` / `Password(554)`; the only frame type in scope.
- **Password(554)**: the secret credential field whose value is masked at rest.
- **Persistent message store**: the durable record of outbound frames retained for resend; the surface where the at-rest exposure lives.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: After a credentialed logon against a persistent store, the literal configured password appears **zero** times anywhere in the store file.
- **SC-002**: 100% of logons that authenticate today continue to authenticate — the wire-side `Password(554)` is unchanged for every transmitted Logon.
- **SC-003**: For credential-free Logons, the default no-credential configuration, and non-Logon outbound frames, the persisted byte stream is **identical** (byte-for-byte) to the pre-change baseline. (A credentialed Logon's stored copy is masked on every backend, including in-memory — so it is intentionally *not* byte-identical to the unmasked baseline.)
- **SC-004**: The store-persistence path performs the **same number of heap allocations** as the pre-change baseline (no new allocation), verified under the allocation-tracking gate.
- **SC-005**: The masked stored Logon record is the **same length** as the unmasked original and remains retrievable as a structurally valid frame (offsets and record integrity intact).

## Assumptions

- **Only `Password(554)` is a secret.** `Username(553)` is an identity and stays in the clear, matching the scope of the existing redaction utility.
- **The mask is a fixed same-length placeholder run** (e.g., a run of `*`); the exact placeholder character is an implementation detail, not a requirement.
- **Stored Logon frames are never retransmitted verbatim.** Administrative frames fold into a `SequenceReset-GapFill` on resend, so masking the stored copy can never produce a peer-observable wire/stored mismatch. This is a relied-upon invariant of the existing resend design; **if a future feature ever introduces verbatim admin-frame replay, it MUST re-derive credentials from configuration rather than from the (now-masked) store** — a forward constraint to record as a limitation.
- **The 554 value cannot contain a field delimiter or `=`** (enforced when the Logon is built), so same-length masking cannot corrupt framing.
- **Industry parity is not the bar.** QuickFIX-cpp / QuickFIX-J persist the sent Logon password identically (verified: their FileStore writes the raw Logon string with no redaction); this feature is deliberate hardening **beyond** reference-engine parity, not a conformance fix — so it must remain a strict no-op **on the wire** to avoid diverging observable protocol behavior. (At-rest masking is uniform across store backends per the Clarifications — it is not gated on persistence.)
- **No new configuration knob.** Masking is unconditional at the persistence boundary (there is no legitimate reason to persist the cleartext secret), so no opt-in/opt-out setting is introduced.
