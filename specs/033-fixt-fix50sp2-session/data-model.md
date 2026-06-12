# Phase 1 Data Model: FIXT.1.1 / FIX 5.0 SP2 Session Establishment

Entities and state the feature reads, adds, or threads. Existing entities are referenced (not
redefined); **NEW**/**AMENDED** marks the deltas.

## E1 — `version_profile` (EXISTING — reused unchanged)

`include/fixpp/dict/version_profile.hpp:71-79`. The 4-byte descriptor carried per `Dictionary` and the
input to message resolution.

| Field | Meaning |
|-------|---------|
| `session_version session` | transport/session protocol — `vt11` for FIXT.1.1 |
| `application_version default_appl` | the negotiated default application version (e.g. `v50sp2`) |
| `bool has_per_message_override` | whether per-message `ApplVerID(1128)` is permitted |

Helper (reused): `resolve_application_version(profile, appl_ver_id_value)` (`:111`) — wire `1128`/`1137`
value → `application_version`, or `profile.default_appl` when absent.

**033 use**: the negotiated profile (`session=vt11`, `default_appl=`*peer-negotiated version*,
`has_per_message_override=true`) is **recorded + exposed** for reify call-sites — the session itself does
NOT reify inbound application messages (it delivers dict-free wire views to `fromApp`, identical to
FIX.4.x — research.md R4). (S-026 deferred: a present `1128` is tolerated by the dict-free parse but MUST
NOT switch the dictionary — no mid-session routing.)

## E2 — Negotiated application version (NEW — session state)

Strand-confined `Session` member (e.g. `negotiated_appl_version_`), set once at inbound-Logon time from
the peer's `DefaultApplVerID(1137)`.

| Property | Value |
|----------|-------|
| Type | `application_version` (the dict-layer enum) — resolved from the wire `1137` value |
| Lifecycle | unset → set at inbound FIXT Logon parse → immutable for the session lifetime |
| Source | peer's `DefaultApplVerID(1137)` on the inbound Logon (FR-003) |
| Validity | MUST resolve to a serviceable dictionary via `version_registry`; else refuse establishment (FR-004a) |
| Persistence | in-memory only (NOT persisted; orthogonal to the 029/025 store spine) |

## E3 — `SessionConfig` extension (NEW/AMENDED — public surface)

`include/fixpp/session/session_config.hpp` (existing `begin_string` at `:163`).

| Field | New? | Meaning |
|-------|------|---------|
| `begin_string` | existing | set to `"FIXT.1.1"` selects the FIXT transport |
| `default_appl_ver_id` | **NEW** | `std::optional<…>` default application version this side advertises on `1137`. Set + `begin_string=="FIXT.1.1"` ⇒ FIXT session; unset ⇒ FIX.4.x byte-identical |
| `username` / `password` | **NEW (optional)** | optional `Username(553)`/`Password(554)` to emit on Logon (confirm exact names / reuse existing at /tasks) |

**FIXT predicate**: `is_fixt() := (begin_string == "FIXT.1.1") && default_appl_ver_id.has_value()`.
`SessionId::from_config` is **unchanged** (identity already keys on `begin_string`).

## E4 — FIXT Logon (35=A) wire shape (AMENDED — additive, FIXT-only)

| Tag | Field | 033 behaviour |
|-----|-------|---------------|
| 8 | BeginString | `FIXT.1.1` for a FIXT session (else unchanged) |
| 98 | EncryptMethod | unchanged (`0`) |
| 108 | HeartBtInt | unchanged |
| **1137** | DefaultApplVerID | **NEW** — emitted on every outbound FIXT Logon (initiator + acceptor reply); REQUIRED inbound on a FIXT Logon (missing ⇒ Reject 373=1) |
| **553** | Username | **NEW (optional)** — emitted when configured; parsed + surfaced inbound |
| **554** | Password | **NEW (optional)** — emitted when configured; parsed inbound; **redacted in logs/transcripts/goldens** (FR-011) |
| 141 / 789 / … | other Logon fields | unchanged |

Ordering: `1137` after `108`, before `141` (research R3 / contract). FIX.4.x Logon emits **none** of
`1137/553/554` → byte-identical (SC-002).

## E5 — `interpret_logon` return (AMENDED)

`src/session/admin_messages.cpp:183-319` — the dict-free inbound Logon scanner. Today returns the
heartbeat interval (+ a few fields). **Amend** to also return: `default_appl_ver_id` (optional wire
value), `username` (optional), `password` (optional). The session arm consumes these to (a) set E2,
(b) reject on missing `1137` (FR-004), (c) refuse on unserviceable version (FR-004a), (d) surface
credentials to authz (FR-008).

## State transitions (establishment — FIXT acceptor arm)

```
inbound Logon(8=FIXT.1.1)
   │
   ├─ 1137 absent ───────────────► Reject(35=3, 373=RequiredTagMissing=1); NOT Active   (FR-004)
   │
   ├─ 1137 present, version not serviceable ──► refuse establishment; NOT Active          (FR-004a)
   │
   └─ 1137 present + serviceable
          │ set negotiated_appl_version_ (E2)
          │ surface 553/554 to authz seam (FR-008)        [existing authz decision governs]
          ▼
        emit acceptor reply Logon (8=FIXT.1.1, own 1137)  (FR-002)
          ▼
        Active — negotiated version recorded + exposed (E1/E2); inbound app messages delivered
                 dict-free to fromApp as today (no session-layer reify); admin = FIXT session layer  (FR-005)
```

The initiator arm is symmetric on emit (own `1137` + optional creds) and reads the peer's `1137` on the
inbound Logon-ack to set E2. FIX.4.x arms are unchanged.

## Invariants

- **INV-FIXT-1**: a FIX.4.x session (`!is_fixt()`) emits no `1137/553/554` and its Logon wire bytes are
  identical to pre-033 (SC-002/FR-009).
- **INV-FIXT-2**: a FIXT session never reaches Active with an unset/unserviceable `negotiated_appl_version_`
  (FR-004/FR-004a/SC-005).
- **INV-FIXT-3**: inbound `ApplVerID(1128)` never changes the resolved application dictionary from
  `negotiated_appl_version_` (S-026 deferred; FR-010).
- **INV-FIXT-4**: a populated `Password(554)` never appears clear-text in any persisted log/transcript/
  golden (FR-011).
