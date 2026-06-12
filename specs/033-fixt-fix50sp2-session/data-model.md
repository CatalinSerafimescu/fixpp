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
| `bool has_per_message_override` | documentation-only descriptor; **not consulted by resolution** (`resolve_application_version` honors a present `1128`/`1137` regardless of this flag — `src/dictionary/version_profile.cpp`). It does not gate `1128` handling. |

Helper (reused): `resolve_application_version(profile, appl_ver_id_value)` (`:111`) — wire `1128`/`1137`
value → `application_version`, or `profile.default_appl` when absent.

**033 use**: the negotiated profile (`session=vt11`, `default_appl=`*peer-negotiated version*) is
**recorded + exposed** for reify call-sites — the session itself does NOT reify inbound application
messages (it delivers dict-free wire views to `fromApp`, identical to FIX.4.x — research.md R4).
(S-026 deferred: because the session never reifies, it never switches the application dictionary on a
present `1128` — it never selects one. A present `1128` is tolerated by the dict-free parse. Per-message
override on a downstream consumer's own reify is the consumer's choice, outside 033 session scope.)

## E2 — Negotiated application version (NEW — session state)

Strand-confined `Session` member (e.g. `negotiated_appl_version_`), set once at inbound-Logon time from
the peer's `DefaultApplVerID(1137)`.

| Property | Value |
|----------|-------|
| Type | `application_version` (the dict-layer enum) — resolved from the wire `1137` value |
| Lifecycle | unset → set at inbound FIXT Logon parse → immutable for the session lifetime |
| Source | peer's `DefaultApplVerID(1137)` on the inbound Logon (FR-003) |
| Validity | MUST resolve to a serviceable dictionary via the engine-built `version_registry` (`registry.get(resolved)` ≠ `dict_no_dictionary_for_application_version`); else refuse with `Reject(35=3, 371=1137, 373=ValueIsIncorrect(5))` (FR-004a — distinct from the missing-tag `RequiredTagMissing(1)`) |
| Persistence | in-memory only (NOT persisted; orthogonal to the 029/025 store spine) |
| Exposure | NEW `Session::negotiated_version_profile() const → dict::version_profile` (`{vt11, default_appl=this}`; `has_per_message_override` is the documentation-only descriptor, not a 1128 gate), reachable from `fromApp` via `Engine::lookup(SessionId)→shared_ptr<Session>` (FR-005; SC-006/W5) |

## E3 — `SessionConfig` extension (NEW/AMENDED — public surface)

`include/fixpp/session/session_config.hpp` (existing `begin_string` at `:163`).

| Field | New? | Meaning |
|-------|------|---------|
| `begin_string` | existing | set to `"FIXT.1.1"` selects the FIXT transport |
| `default_appl_ver_id` | **NEW** | **`std::optional<dict::application_version>`** (the dict-layer enum — PINNED, not a raw wire string) default application version this side advertises on `1137`. Set + `begin_string=="FIXT.1.1"` ⇒ FIXT session; unset ⇒ FIX.4.x byte-identical. An invalid / `Unknown` configured value fails before Logon. |
| `username` / `password` | **NEW (optional)** | optional `Username(553)`/`Password(554)` to emit on Logon (confirm exact names / reuse existing at /tasks) |

**FIXT predicate**: `is_fixt() := (begin_string == "FIXT.1.1") && default_appl_ver_id.has_value()`.
`SessionId::from_config` is **unchanged** (identity already keys on `begin_string`).

**NEW inverse render helper (a /tasks deliverable — ABSENT today).** Because wire `ApplVerID(1137)` ≠ the
C++ `application_version` enum index in general (`version_profile.hpp:117-132`; e.g. enum `v40`→wire `"2"`,
enum `v50`→wire `"7"` — some coincide, e.g. `v44`→`"6"`, `v50sp2`→`"9"`, but MUST NOT be relied on), and
the existing `resolve_application_version` is **wire→C++ only**, 033 adds an inverse
`application_version → wire 1137 string` render helper. Emit tests: `v50sp2`→`1137=9`, `v44`→`1137=6`,
`v50`→`1137=7` (divergent — proves the helper), invalid/`Unknown`→fail before Logon (research R3).

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
(b) reject on missing `1137` → `RequiredTagMissing(1)` (FR-004), (c) refuse on unserviceable version →
`Reject(35=3, 371=1137, 373=ValueIsIncorrect(5))` (FR-004a), (d) surface credentials as `logon_credentials`
to the NEW `authorize_logon(compid, creds)` default-accept seam (FR-008; the existing
`authorize(peer_identity, compid)` is mTLS-gated and takes no credentials — research R6).

## State transitions (establishment — FIXT acceptor arm)

```
inbound Logon(8=FIXT.1.1)
   │
   ├─ 1137 absent ───────────────► Reject(35=3, 373=RequiredTagMissing=1); NOT Active   (FR-004)
   │
   ├─ 1137 present, version not serviceable ──► Reject(35=3, 371=1137, 373=ValueIsIncorrect=5); NOT Active (FR-004a)
   │
   └─ 1137 present + serviceable
          │ set negotiated_appl_version_ (E2)
          │ surface 553/554 as logon_credentials to authorize_logon(compid, creds) (FR-008) [default-accept; mTLS-independent]
          ▼
        emit acceptor reply Logon (8=FIXT.1.1, own 1137)  (FR-002)
          ▼
        Active — negotiated version recorded + exposed (E1/E2); inbound app messages delivered
                 dict-free to fromApp as today (no session-layer reify); admin = FIXT session layer  (FR-005)
```

The initiator arm is symmetric on emit (own `1137` + optional creds) and reads the peer's `1137` on the
inbound Logon-ack to set E2. FIX.4.x arms are unchanged.

## Invariants

- **INV-FIXT-1**: for a FIX.4.x session (`!is_fixt()`), the Logon/establishment frame built by
  `build_logon` appends no FIXT-only `1137/553/554` field and its wire bytes are identical to pre-033
  (SC-002/FR-009).
- **INV-FIXT-2**: a FIXT session never reaches Active with an unset/unserviceable `negotiated_appl_version_`
  (FR-004/FR-004a/SC-005).
- **INV-FIXT-3**: the fixpp **session layer** never reifies inbound application messages, so an inbound
  `ApplVerID(1128)` never causes the session to switch the application dictionary from
  `negotiated_appl_version_` — the session never selects an application dictionary at all (research R4).
  (Not a claim that reify ignores `1128`: a downstream consumer reifying the exposed profile on a
  `1128`-bearing message would honor it — per-message override is the consumer's choice, S-026 deferred; FR-010.)
- **INV-FIXT-4**: a populated `Password(554)` never appears clear-text in any persisted log/transcript/
  golden (FR-011).
