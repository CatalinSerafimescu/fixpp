# Contract: Send-Path AllowPosDup Strip + Inbound PossResend(97)

The public/behavioral contract this slice establishes. No new C-ABI surface; one additive C++ `SessionConfig` field + one behavioral change in the send path + one witness-confirmed inbound disposition.

## C1 — `SessionConfig::allow_pos_dup` (public C++ surface)

- **Field**: `bool allow_pos_dup` in `include/fixpp/session/session_config.hpp`, default `false`.
- **Additive, default-valued** ⇒ no ABI break, no behavior change for existing callers that don't set it (they get default-strip, which matches both reference engines).
- **Config-key parity**: the name mirrors QuickFIX-J `AllowPosDup` (`SETTING_ALLOW_POS_DUP_MESSAGES`) for 008 `cfg_loader` translation.

## C2 — Plain `send` strip behavior (FR-006 / FR-008 / FR-009)

For `Engine::send` / `Session::send` of an opaque application `app_payload`:

- **C2.1 (default strip, `allow_pos_dup == false`)**: any caller-supplied `PossDupFlag(43)` and `OrigSendingTime(122)` are removed from the outbound application bytes before framing. The framed wire message contains neither tag 43 nor tag 122 in the application portion.
- **C2.2 (retain, `allow_pos_dup == true`)**: caller-supplied `43`/`122` are passed through unmodified.
- **C2.3 (boundary-anchored, injection-safe)**: only a complete `43=<value>\x01` / `122=<value>\x01` field at a field boundary (start-of-payload or immediately after `\x01`) is excised. A `43=`/`122=` substring embedded inside another field's value is never matched. (INV-2; [[feedback_delimiter_injection_verbatim_field_copy]].)
- **C2.4 (fail-closed)**: a payload whose `<tag>=<value>\x01` framing cannot be validated is rejected `app_payload_malformed=131` by the pre-existing 020 validation (which runs before the strip) — the strip never excises bytes from an unvalidated payload.
- **C2.5 (no-op when absent)**: a payload with no `43`/`122` is emitted unchanged under either knob setting.
- **C2.6 (no heap)**: the strip allocates nothing (INV-4); it operates within the existing send stack scratch.

## C3 — Auto-resend independence (FR-007)

- The automatic resend / gap-fill replay path (`build_replay_frame`) re-adds `PossDupFlag(43)=Y` + `OrigSendingTime(122)` to replayed messages **regardless** of `allow_pos_dup`. The knob governs only the plain `send_impl` path; it is structurally impossible for it to affect resend (`build_replay_frame` does not call `send_impl`).

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
