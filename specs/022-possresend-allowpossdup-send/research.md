# Phase 0 Research: PossResend(97) Inbound + AllowPosDup Send-Path Strip

Decisions grounding the design against the two live interop targets (QuickFIX-cpp v1.16.0, QuickFIX-J 3.0.1) and the existing fixpp send path. Each: Decision / Rationale / Alternatives considered.

## D1 — `AllowPosDup` knob name and default

**Decision**: Add one additive `SessionConfig` field `bool allow_pos_dup = false`. Default `false` = **strip** caller-supplied `PossDupFlag(43)` + `OrigSendingTime(122)` from a plain `send`; `true` = **retain** them verbatim. The field is named `allow_pos_dup` to mirror QuickFIX-J's config key `AllowPosDup` (`SETTING_ALLOW_POS_DUP_MESSAGES`, `Session.java:393`) exactly, giving a 1:1 mapping for the 008 `cfg_loader` QuickFIX-config translation path.

**Rationale**: Both reference engines strip by default.
- QuickFIX-cpp `Session::send` (`Session.cpp:533-537`) calls `removeField(PossDupFlag)` + `removeField(OrigSendingTime)` **unconditionally** before `sendRaw` — there is no knob; stripping is always on.
- QuickFIX-J `Session::send(message)` (`Session.java:2769`) delegates to `send(message, this.allowPosDup)`; `send(message, allowPosDup)` (`:2788-2796`) returns `sendRaw` as-is when `allowPosDup` is true, otherwise `removeField(PossDupFlag)` + `removeField(OrigSendingTime)`. The config default is `allowPosDup = false` (`:446`).
So default-strip is the behavior both engines exhibit; QFJ additionally exposes the retain opt-in, which fixpp adopts.

**Alternatives considered**: (a) a fixpp-native name (`strip_caller_poss_dup`) — rejected: loses the QuickFIX-config-key parity that 008's translator depends on (Clarifications Q1). (b) no knob, always strip (QFcpp shape) — rejected: QFJ-compat callers that legitimately manage their own duplicate flags need the retain opt-in; the knob costs one default-valued POD field.

## D2 — The strip is injection-safe by construction (the 020 RC#1 hazard)

**Decision**: Perform the excision in `Session::send_impl` **after** the existing 020 opaque-payload validation and **before** SendingTime-stamp/frame-build, using the same boundary-anchored matcher (`has_boundary_token`, `session.cpp:2917`). Excise the complete field `43=<value>\x01` and `122=<value>\x01` only when the tag token appears at a **field boundary** (offset 0 — impossible for 43/122 since `35=` is field 0 — or immediately after a `\x01`).

**Rationale**: This is the exact class of defect 020 RC#1 surfaced ([[feedback_delimiter_injection_verbatim_field_copy]]): a caller value containing `\x0143=Y` or an embedded `43=` must not let the caller forge or hide a duplicate flag, and an unanchored substring strip would over-/under-excise. The 020 validation (`send_impl`, `session.cpp:2900-2926`) already guarantees, before the strip runs, that the payload (1) begins with `35=`, (2) contains no duplicate `35=`, and (3) is a sequence of SOH-delimited `<tag>=<value>` fields with no banned session header/trailer tag at a boundary — anything else is rejected `app_payload_malformed=131`. Operating the boundary-anchored matcher over that validated framing means a `43=` inside another field's value (no preceding SOH) is never matched, and every excised span is a genuine, complete field. Fail-closed framing is therefore inherited from the 020 substrate at no new cost.

**Alternatives considered**: (a) field-parse the payload into a structured message, strip, re-serialize — rejected: introduces a new app-body parser (the very thing 021 deferred to avoid) and a heap/alloc surface; the boundary-anchored byte excision is sufficient and stays no-heap. (b) reject any plain `send` that contains `43`/`122` (strict) — rejected: diverges from both engines (which silently strip) and breaks callers that harmlessly include them.

## D3 — Auto-resend path independence (FR-007 holds by construction)

**Decision**: Make **no** change to the retransmission path; pin its independence with a witness.

**Rationale**: The ResendRequest reply / gap-fill replay re-serializes stored frames via `build_replay_frame` (`session.cpp:1186-1239`), which re-adds `PossDupFlag(43)=Y` (`:1220`) and `OrigSendingTime(122)` (`:1227`) and **does not call `send_impl`**. The `allow_pos_dup` knob is read only inside `send_impl`'s plain-send pre-pass, so it is structurally impossible for the knob to suppress the resend-path flags. FR-007 ("auto-resend always re-adds 43/122 regardless of the knob") is satisfied without code; a unit/interop witness sends with the default-strip knob, then drives a resend and asserts the replayed frame still carries `43=Y`+`122`.

**Alternatives considered**: routing resend through `send_impl` with a force-retain override — rejected: needless coupling of two independent paths; the current separation is the safer design.

## D4 — Inbound `PossResend(97)` is witness-only (no session-level handling)

**Decision**: Add **no** production code for inbound `PossResend(97)`. Ship US1 as parity/witness tests proving fixpp delivers an in-sequence `97=Y` application message to `fromApp` unchanged (the app reads tag 97 from the delivered `MessageView`), advances the sequence number, and never session-rejects it for `97`.

**Rationale**: Confirmed by the clarify sweep — **neither** engine handles `PossResend` at the session layer. QuickFIX-cpp defines `PossResend` (`FixFieldNumbers.h:37`, value `Y`/`N` in `FixValues.h:179-180`) but **never references it in `Session.cpp`**. QuickFIX-J has **no** `PossResend` reference anywhere in `quickfixj-core` session code. In both, a `97=Y` app message is processed by the normal in-sequence path and delivered to the application, which performs business-level duplicate detection on its own keys. fixpp matches this already: `Application::fromApp` (`application.hpp:84`) receives the full `MessageView<Index>`, and the 021 redeliver path comment (`session.cpp:2019-2021`) confirms `fromApp` "sees" header flags like `43=Y` — so tag `97` is equally visible. Crucially, fixpp's `PossResend` is also **not** subject to the 021 `OrigSendingTime(122)`-required validation, which keys strictly on `PossDupFlag(43)=Y` (FR-003).

**Alternatives considered**: surfacing an explicit `is_poss_resend` signal on the callback (Clarifications Q2 option B) — rejected: diverges from both engines, widens the public `Application` surface for no behavior either engine provides; the app reads tag 97 itself.

## D5 — No new `error::core` enum slot

**Decision**: Reuse `app_payload_malformed = 131` (020) for the fail-closed disposition when a plain-`send` payload's framing cannot be validated; add no new enum slot, no `fixpp_error_t` change.

**Rationale**: The strip never introduces a *new* failure mode — it operates only on payloads that already passed the 020 validation, and the sole framing-failure path is the pre-existing 020 rejection (slot 131). The knob's strip/retain choice is not an error condition. [const §X] ABI stability is preserved (additive POD field only).

**Alternatives considered**: a dedicated `poss_dup_strip_failed` slot — rejected: no distinct failure exists; the only failure is the 020 framing rejection already owned by 131.

## D6 — No-heap on the strip path

**Decision**: Excise into the existing 4096-byte send stack scratch (the framer's buffer), copying surviving fields; no heap allocation.

**Rationale**: `Session::send_impl` is already an [const §VIII.5] / INV-5 no-heap path (it builds the frame into a 4096-byte stack buffer; `session.cpp:2849`). The strip is a linear scan + copy of the surviving fields into that same stack region (or an in-place compaction), staying within the existing buffer budget; an oversized payload already returns `wire_frame_too_large`. The binding no-heap gate remains the mallocnesia LD_PRELOAD run (`/speckit-verify` Step 6, `test_session_alloc_guard`), which the strip witness must pass.

**Alternatives considered**: a `std::string`/`std::vector` scratch — rejected: violates the no-heap send-path rule; the stack scratch is sufficient (payloads are ≤ ~3800 bytes).
