# Phase 0 Research: PossResend(97) Inbound + AllowPosDup Send-Path Strip

Decisions grounding the design against the two live interop targets (QuickFIX-cpp v1.16.0, QuickFIX-J 3.0.1) and the existing fixpp send path. Each: Decision / Rationale / Alternatives considered.

## D1 — `AllowPosDup` knob name and default

**Decision**: Add one additive `SessionConfig` field `bool allow_pos_dup = false`. Default `false` = **strip** caller-supplied `PossDupFlag(43)` + `OrigSendingTime(122)` from a plain `send`; `true` = **retain** them verbatim. The field is named `allow_pos_dup` to mirror QuickFIX-J's config key `AllowPosDup` (`SETTING_ALLOW_POS_DUP_MESSAGES`, `Session.java:393`) exactly, giving a 1:1 mapping for the 008 `cfg_loader` QuickFIX-config translation path.

**Rationale**: Both reference engines strip by default.
- QuickFIX-cpp `Session::send` (`Session.cpp:533-537`) calls `removeField(PossDupFlag)` + `removeField(OrigSendingTime)` **unconditionally** before `sendRaw` — there is no knob; stripping is always on.
- QuickFIX-J `Session::send(message)` (`Session.java:2769`) delegates to `send(message, this.allowPosDup)`; `send(message, allowPosDup)` (`:2788-2796`) returns `sendRaw` as-is when `allowPosDup` is true, otherwise `removeField(PossDupFlag)` + `removeField(OrigSendingTime)`. The config default is `allowPosDup = false` (`:446`).
So default-strip is the behavior both engines exhibit; QFJ additionally exposes the retain opt-in, which fixpp adopts.

**Alternatives considered**: (a) a fixpp-native name (`strip_caller_poss_dup`) — rejected: loses the QuickFIX-config-key parity that 008's translator depends on (Clarifications Q1). (b) no knob, always strip (QFcpp shape) — rejected: QFJ-compat callers that legitimately manage their own duplicate flags need the retain opt-in; the knob costs one default-valued POD field.

## D2 — The strip is fail-closed via a 022-owned field scanner (NOT inherited from 020)

**Decision**: Perform the excision in `Session::send_impl` **after** the existing 020 opaque-payload validation and **before** seqnum peek/SendingTime-stamp/frame-build, behind a **new, 022-owned, no-heap, span-producing field scanner**. The scanner walks the payload field-by-field starting after the leading `35=…\x01`, validating each field is `<non-empty digit-only tag>=<value>\x01` (required `=`, non-empty digit-only tag, non-empty field). On the FIRST malformed field it returns `app_payload_malformed=131` **before** any seqnum peek/assign, stamp, excision, or transmit. Only over a fully-validated field sequence does it excise the complete `43=<value>\x01` / `122=<value>\x01` fields, each span bounded by the SOH terminator (guaranteed present — the 020 floor requires the whole payload to end with SOH, `session.cpp:2951`, so every interior field is SOH-terminated), and only when the tag token sits at a **field boundary** (offset 0 — impossible for 43/122 since `35=` is field 0 — or immediately after a `\x01`).

**Rationale**: The 020 validation is **NOT** the per-field grammar check the design originally claimed. The real validator (`send_impl`, `session.cpp:2932-2990`) is a **six-check denylist**: non-empty; leads `35=`; ends SOH (`session.cpp:2951`); non-empty MsgType; no duplicate `35=`; no banned session tag `8/9/34/49/52/56/10` at a boundary. There is **no** loop asserting every interior field is `<tag>=<value><SOH>`. Because the payload is guaranteed to end with SOH and fields are SOH-delimited, every interior field IS SOH-terminated — there is no missing-SOH / unterminated-field scenario to defend against. What the floor does **NOT** guarantee is that each SOH-delimited field is a well-formed `<non-empty digit-only tag>=<value>`: a **missing `=`** (`35=D\x0111BROKEN\x0143=Y\x01`), an **empty tag** (`35=D\x01=bad\x01122=x\x01`), a **non-digit tag** (`35=D\x014a=x\x0143=Y\x01`), or an **empty field** (a stray `\x01` producing a zero-length field) all pass that floor untouched. This is the exact class of defect 020 RC#1 ([[feedback_delimiter_injection_verbatim_field_copy]]) and 021 RC#1 ([[feedback_conjunctive_parse_guard_tolerates_malformed_field]]) surfaced: a present-but-malformed field tolerated by a denylist, plus a caller value with literal bytes `43=` (no preceding SOH) that must be preserved while a true SOH-boundary `…\x0143=Y\x01` must be excised. Because the floor does not validate per-field grammar, the strip cannot inherit it — it **owns** the scanner that establishes it. Excision is safe not because of any over-excision guard but because it runs only after full per-field validation over SOH-terminated fields: each excised `43=…\x01` / `122=…\x01` is a complete, validated, SOH-terminated field. `has_boundary_token` (`session.cpp:2917-2926`) is **bool-only** (it answers "does this token appear at a boundary" — it cannot identify a field's span or prove a complete-field excision); it is retained only as the **boundary-rule precedent**, not the excision primitive.

**Alternatives considered**: (a) field-parse the payload into a structured message, strip, re-serialize — rejected: introduces a new app-body parser (the very thing 021 deferred to avoid) and a heap/alloc surface; the no-heap span scanner is sufficient. (b) reject any plain `send` that contains `43`/`122` (strict) — rejected: diverges from both engines (which silently strip) and breaks callers that harmlessly include them. (c) borrow the 020 denylist as a per-field grammar proof — rejected: it is not one (above); doing so was the round-1 Gate-A defect.

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

**Rationale**: No new error enum slot. 022 DOES add a new rejection condition (a malformed interior field — missing `=`, non-digit/empty tag, empty field — that passes the 020 denylist) but maps it to the existing `app_payload_malformed=131`; no `fixpp_error_t`/enum change. The knob's strip/retain choice is not an error condition. [const §X] ABI stability is preserved (additive POD field only).

**Alternatives considered**: a dedicated `poss_dup_strip_failed` slot — rejected: no distinct failure exists; the only failure is the 020 framing rejection already owned by 131.

## D6 — No-heap on the strip path

**Decision**: The scanner + excision copies surviving fields into ONE named bounded **strip stack scratch** that the strip declares for itself; no heap allocation.

**Rationale**: `Session::send_impl` is already an [const §VIII.5] / INV-4 no-heap path (it builds the frame into the 4096-byte stack buffer `body_buf` at `session.cpp:3034`). The no-heap claim must be honest about WHERE the stripped payload lives: `body_buf` is built *after* the strip would run, from the *already-stripped* payload — so it is **not** the strip's scratch. Because the input `app_payload` is a `const std::span`, **in-place compaction is not realizable** — there will be exactly one strip scratch copy. The strip therefore declares one bounded stack array (sized to the validated payload, ≤ ~3800 bytes) and copies the surviving fields into it; an oversized payload returns `wire_frame_too_large`. The combined send-path stack budget (strip scratch + framer `body_buf`) stays within limits and remains heap-free. The binding no-heap gate remains the mallocnesia LD_PRELOAD run (`/speckit-verify` Step 6, `test_session_alloc_guard`), which the strip witness must pass.

**Alternatives considered**: (a) an "in-place compaction" into the `const` input span — rejected: **not realizable** (the input is `const std::span`); struck from the design. (b) a `std::string`/`std::vector` scratch — rejected: violates the no-heap send-path rule; the bounded stack scratch is sufficient (payloads are ≤ ~3800 bytes).
