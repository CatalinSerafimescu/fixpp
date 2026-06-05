# Contract: Send-Path AllowPosDup Strip + Inbound PossResend(97)

The public/behavioral contract this slice establishes. No new C-ABI surface; one additive C++ `SessionConfig` field + one behavioral change in the send path + one witness-confirmed inbound disposition.

## C1 — `SessionConfig::allow_pos_dup` (public C++ surface)

- **Field**: `bool allow_pos_dup` in `include/fixpp/session/session_config.hpp`, default `false`.
- **Additive, default-valued** ⇒ **no C-ABI surface or error-slot change**. Adding a public `SessionConfig` data member does change the C++ struct layout, so a **normal source rebuild is required** (it is ABI-additive, not a C-ABI break).
- **Intentional default wire-behavior change**: default-strip is an **intentional** change for any existing caller currently passing `43`/`122` into a plain `send` — pre-022 those bytes were emitted opaquely (020 only validates, never strips); post-022 they are removed by default. This matches the QuickFIX-cpp/QuickFIX-J defaults (D1). It is NOT "no behavior change."
- **Config-key parity**: the name mirrors QuickFIX-J `AllowPosDup` (`SETTING_ALLOW_POS_DUP_MESSAGES`) for 008 `cfg_loader` translation.

## C2 — Plain `send` strip behavior (FR-006 / FR-008 / FR-009)

For `Engine::send` / `Session::send` of an opaque application `app_payload`:

- **C2.1 (default strip, `allow_pos_dup == false`)**: any caller-supplied `PossDupFlag(43)` and `OrigSendingTime(122)` are removed from the outbound application bytes before framing. The framed wire message contains neither tag 43 nor tag 122 in the application portion.
- **C2.2 (retain, `allow_pos_dup == true`)**: caller-supplied `43`/`122` are passed through unmodified.
- **C2.3 (boundary-anchored, injection-safe)**: only a complete, scanner-validated `43=<value>\x01` / `122=<value>\x01` field at a field boundary (start-of-payload or immediately after `\x01`) is excised (its terminating SOH is guaranteed present — 020 requires the payload to end with SOH). The two cases are materially different at the raw-payload layer and split cleanly:
  - **(a)** literal bytes `43=`/`122=` **inside** another field's value, with NO preceding SOH (e.g. `11=ORD43=Y\x01`) → **preserved** (not a field boundary);
  - **(b)** a real SOH-boundary `…\x0143=Y\x01` / `…\x01122=…\x01` → **excised** under default-strip, **retained** under `allow_pos_dup=true` (operator opt-in);
  - **(c)** a malformed adjacent field → see C2.4 (fail closed BEFORE any excision).
  (INV-2; [[feedback_delimiter_injection_verbatim_field_copy]].)
- **C2.4 (fail-closed via the 022-owned scanner)**: the 020 floor is a six-check **denylist** — it requires the payload to end with SOH (`session.cpp:2951`), so every interior field is SOH-terminated, but it does NOT prove every field is `<non-empty digit-only tag>=<value>` (e.g. `35=D\x0111BROKEN\x0143=Y\x01` missing `=`, an empty-tag `35=D\x01=bad\x01…`, or a non-digit tag `35=D\x014a=x\x01…` all pass it). The 022 strip therefore owns a no-heap field scanner that validates every post-`35=` field is `<non-empty digit-only tag>=<value>\x01`; on the FIRST malformed field it returns `app_payload_malformed=131` **before** seqnum peek/assign, SendingTime stamp, excision, or transmit. The excised span is `[boundary-of-43= , inclusive-of-its-terminating SOH]`, where that terminating SOH is guaranteed present (every interior field is SOH-terminated, since 020 requires the payload to end with SOH). The scanner's job is to reject interior fields malformed in ways the 020 denylist admits (missing `=`, non-digit/empty tag, empty field) BEFORE excision, so excision runs only over a proven-well-formed sequence — not to prevent run-off-the-end, which cannot occur behind the trailing-SOH floor ([[feedback_conjunctive_parse_guard_tolerates_malformed_field]]). The strip never excises bytes from an unvalidated payload.
- **C2.5 (no-op when absent)**: a payload with no `43`/`122` is emitted unchanged under either knob setting.
- **C2.6 (no heap)**: the scanner + excision allocates nothing (INV-4); it copies surviving fields into ONE bounded named strip stack scratch (distinct from the framer's `body_buf`, which is built later from the stripped payload; in-place compaction is not realizable against the `const std::span` input). An oversized payload returns `wire_frame_too_large` — the EXISTING send-path oversize disposition the framer's `body_buf` overflow already owns (`session.cpp:3034`); the strip scratch is sized identically, so 022 adds no new oversize path. Binding gate: mallocnesia LD_PRELOAD.

## C3 — Auto-resend independence (FR-007)

- The automatic resend / gap-fill replay path (`build_replay_frame`, `session.cpp:1186-1239`) re-adds `PossDupFlag(43)=Y` + `OrigSendingTime(122)` to replayed messages **regardless** of `allow_pos_dup`. The knob governs only the plain `send_impl` path; it is structurally impossible for it to affect resend (`build_replay_frame` does not call `send_impl`).
- **Witness must exercise the knob**: the FR-007 witness MUST (a) set `allow_pos_dup=false`, (b) send a payload that **contained** `43`/`122` (so the strip provably ran and removed them from the stored frame), then (c) drive the resend and assert the replayed frame re-adds `43=Y`+`122`. A witness whose original send carried no 43/122 passes trivially and would not detect a (hypothetical) knob-leak into the resend path.

## C4 — Inbound PossResend(97) (FR-001..FR-005, witness-only)

- **C4.1**: an in-sequence application message with `PossResend(97)=Y` is processed once, advances the expected inbound sequence number, and is delivered to the registered `Application::fromApp` (full `MessageView`, tag 97 readable). No `Reject`/`Logout`/disconnect results from `97`.
- **C4.2**: `97=Y` triggers **no** session-level reject of its own and is **not** subject to the `OrigSendingTime(122)`-required rule (that keys on `43=Y` only).
- **C4.3**: a message carrying both `43=Y` and `97=Y` is dispositioned by the 021 PossDup arms on `43`; `97` adds nothing.
- **C4.4**: with no registered `Application`, a `97=Y` message is handled byte-identically to the same message without `97`.

## C5 — Interop (FR-010 / SC-005)

- Both behaviors interop cleanly with live QuickFIX-cpp v1.16.0 and QuickFIX-J 3.0.1 in initiator and acceptor roles:
  - an `allow_pos_dup`-default plain `send` produces captured outbound bytes free of 43/122 (engine-log-seam capture, 016 P4 discipline);
  - a counterparty-app `97=Y` application message is delivered to fixpp's `fromApp` and the session stays established.

## C6 — Catalogue (FR-011)

- Completing C2–C5 flips catalogue row **S-010** `backlog → done` (the 021 partial-delivery note is superseded). Applied at Polish per the 020/021 precedent.
