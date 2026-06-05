# Phase 1 Data Model: PossResend(97) Inbound + AllowPosDup Send-Path Strip

No new persistent entity, store, or codegen. This slice adds one configuration field and one pure pre-framing byte transform, plus a witness-only inbound disposition table.

## 1. `SessionConfig` knob (additive, public header)

`include/fixpp/session/session_config.hpp` — one new POD field next to the 021 `redeliver_poss_dup`:

```cpp
// 022 FR-006 / D1 — send-path AllowPosDup knob (QuickFIX-J SETTING_ALLOW_POS_DUP_MESSAGES
// parity, exact key spelling for 008 cfg_loader translation). When false (DEFAULT,
// matches both QFcpp unconditional-strip and QFJ default), a plain Session::send STRIPS
// any caller-supplied PossDupFlag(43) and OrigSendingTime(122) from the opaque app
// payload before framing. When true, they are RETAINED verbatim (QFJ sendRaw-as-is) —
// operator opt-in for callers that manage their own duplicate flags. The automatic
// resend/retransmission path (build_replay_frame) always re-adds 43=Y+122 regardless
// of this knob (FR-007 — structurally independent; build_replay_frame never calls
// send_impl). No C-ABI surface or error-slot change; additive C++ config field ⇒
// struct-layout change requiring a normal source rebuild; default-strip is an
// intentional default wire-behavior change for plain sends containing
// caller-supplied 43/122 (matching QF defaults).
bool allow_pos_dup = false;
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `allow_pos_dup` | `bool` | `false` (strip) | `false`: plain `send` strips caller `43`/`122`. `true`: retains them verbatim. Never affects `build_replay_frame`. |

## 2. The send-path strip transform (`Session::send_impl`)

A pure function of (`app_payload`, `allow_pos_dup`) producing the bytes handed to the framer. Inserted **after** the 020 opaque-payload validation, **before** seqnum peek + SendingTime stamp + frame build. It owns a **new 022 no-heap field scanner** (the 020 floor is only a denylist — D2 — NOT a per-field grammar proof; the scanner is what establishes the per-field guarantee).

**Floor (the 020 validation that ran first — a six-check DENYLIST, NOT a per-field grammar):**
- `app_payload` is non-empty, begins with `35=` (MsgType is field 0), ends with SOH (`session.cpp:2951`), has a non-empty MsgType, no duplicate `35=`, and no banned session tag `8/9/34/49/52/56/10` at a boundary — else already rejected `app_payload_malformed=131`. Because the payload ends with SOH and fields are SOH-delimited, every interior field is SOH-terminated.
- It does **NOT** prove every SOH-delimited field is `<non-empty digit-only tag>=<value>`: e.g. `35=D\x0111BROKEN\x01…` (missing `=`), `35=D\x01=bad\x01…` (empty tag), `35=D\x014a=x\x01…` (non-digit tag), and an empty field all pass the floor.

**Scanner pass (022-owned, runs before any excision/seqnum/stamp):** walk each SOH-terminated field after `35=…\x01`; each MUST be `<non-empty digit-only tag>=<value>\x01` (required `=`, non-empty digit-only tag, non-empty field). On the FIRST malformed field → return `app_payload_malformed=131`, **no** seqnum consumption, **no** stamp, **no** excision, **no** transmit. The scanner also yields each field's span so excision removes complete, SOH-terminated fields.

**Transform (only over a scanner-validated payload):**

| `allow_pos_dup` | Field present | Result |
|-----------------|---------------|--------|
| `false` (default) | `43=…\x01` and/or `122=…\x01` at a field boundary | excise the **complete** field(s) (span-bounded by the scanner's proven terminating SOH); copy surviving fields into the bounded strip stack scratch in original order |
| `false` | neither present | no-op (payload passed through unchanged) |
| `false` | literal bytes `43=`/`122=` **inside** another field's value (not at a boundary) | **not** matched, **not** excised (injection-safe — D2) |
| `false` | any field fails the scanner (missing `=`, empty/non-digit tag, empty field) | fail closed `131` — no excision (these pass the 020 denylist but are malformed interior fields — D2) |
| `true` | any (still scanner-validated first) | passthrough verbatim (no excision) |

**Invariants:**
- INV-1: `35=` (field 0) is never touched (43/122 are never field 0).
- INV-2: Only complete, scanner-validated, boundary-anchored `43=…\x01` / `122=…\x01` fields are removed — never a partial span, never a substring inside a value (a literal `43=` with no preceding SOH is preserved) ([[feedback_delimiter_injection_verbatim_field_copy]] / [[feedback_conjunctive_parse_guard_tolerates_malformed_field]]).
- INV-3: The stripped payload remains a valid `35=`-leading SOH-delimited field sequence (excision removes whole fields, preserves the rest).
- INV-4: No heap allocation. The scanner + excision uses ONE bounded named strip stack scratch (D6); it is NOT the framer's `body_buf` (built later from the stripped payload), and in-place compaction is not realizable against the `const std::span` input.
- INV-5: `build_replay_frame` is not on this path; the resend re-add of `43=Y`+`122` is unaffected (FR-007).

## 3. Inbound `PossResend(97)` disposition (witness-only — no production state)

No code change; the table documents the asserted disposition for the parity/witness tests.

| Inbound message | Disposition | Witness assertion |
|-----------------|-------------|-------------------|
| app msg, `97=Y`, `MsgSeqNum == expected`, no `43=Y` | process once; advance seqnum; deliver to `fromApp` (full `MessageView`, tag 97 readable) | seqnum N→N+1; `fromApp` called; no `Reject`/`Logout`/disconnect; session `Active` |
| same, **no** registered `Application` | byte-identical to the same message without `97` | identical outcome (no-app default preserved) |
| `43=Y` **and** `97=Y` | 021 PossDup arms (A–E) evaluated on `43` only; `97` adds nothing | 021 disposition unchanged; `97` causes no extra reject |
| `97=Y`, **no** `122`, no `43=Y` | NOT rejected for missing `122` (122-required keys on `43=Y` only — FR-003) | no `Reject(371=122)` |
| `97=Y` at too-high seqnum | existing too-high/resend path; `97` has no effect | unchanged |
| admin msg carrying `97` | existing admin path; `97` has no effect | unchanged |

## 4. Reused machinery (no new entities)

- `has_boundary_token` (`session.cpp:2917-2926`) — bool-only boundary-token check (read; cited as the **boundary-rule precedent** only — it cannot produce field spans, so it is NOT the excision primitive; the 022 scanner is new).
- 020 opaque-payload validation (`send_impl`, `session.cpp:2932-2990`) — the six-check **denylist** floor that runs before the strip; owns fail-closed slot 131 but does NOT prove per-field grammar (D2). The 022 scanner reuses slot 131 for malformed interior fields.
- `build_replay_frame` (`session.cpp:1186-1239`) — the resend re-add path (read-only; independence witness target).
- `Application::fromApp` (`application.hpp:84`) — full-`MessageView` delivery (US1 witness target).
- `SessionConfig` additive-field pattern (cf. 021 `redeliver_poss_dup`, `session_config.hpp:293`).
