# Phase 0 Research — Resend-reply PossDup wire conformance (037)

All line numbers are submodule HEAD (post-036, branch `037-resend-reply-possdup-tags`). Reference-engine source is `reference-engines/` (QuickFIX-cpp v1.16.0, QuickFIX-J 3.0.1), verified 2026-06-14.

## D-1 — OrigSendingTime(122) value for an administratively-generated GapFill = the GapFill's own SendingTime(52)

**Decision**: The GapFill's `122` is byte-equal to the `52` the same frame stamps. No new timestamp is generated; the builder's existing `sending_time` parameter is reused.

**Rationale**: A GapFill covers a *range* of skipped administrative messages — there is no single "original message" with a single original `52` to copy. Both reference engines resolve this the same way:
- **QFcpp** `Session::generateSequenceReset` (`src/C++/Session.cpp`): `insertOrigSendingTime(header, header.getField<SendingTime>())` — `122` is set to the `SendingTime` just stamped by `fill()`.
- **QFJ** `Session.generateSequenceReset` (`quickfixj-core/.../Session.java:~1410`): `header.setUtcTimeStamp(OrigSendingTime.FIELD, header.getUtcTimeStamp(SendingTime.FIELD), getTimestampPrecision())` — `122 = 52`.

**Alternatives considered**: (a) a stored original `52` — rejected, no single original exists for a range; (b) a fresh `clock.now()` distinct from `52` — rejected, diverges from both engines and would need a new parameter + a clock dependency the builder does not have. Because `122 == 52`, precision is automatically matched (same string), so the 026 nanos precision work needs no involvement.

## D-2 — Emitting PossDupFlag(43)=Y REQUIRES emitting OrigSendingTime(122)

**Decision**: `43=Y` and `122` are emitted together, never `43` alone.

**Rationale**: FIX makes `122` conditionally required when `43=Y`. QFJ enforces this on inbound (`Session.java:2580-2592`): inside the possdup branch, if `122` is present it must be `≤ 52` (so `122 == 52` passes the boundary), and if `122` is **absent** with `RequiresOrigSendingTime=Y` the message is **rejected** (`BAD_TIME_REJ_REASON`, tag 122). So a hypothetical "add `43` only" change would be *less* conformant than today against a strict peer. The pair is mandatory.

## D-3 — Field placement: append at end (after 123), order-safe

**Decision**: Append `43` then `122` at the end of the GapFill's field list (after `123=Y`), mirroring `build_replay_frame`'s append-at-end pattern.

**Rationale**: Both reference engines parse the header into a field map (`isSetField`/`getField` / `getUtcTimeStamp`) — field order is irrelevant to inbound validation. `build_replay_frame` already appends `43`/`122` after all copied fields (including body fields) and ships live against both engines, so the precedent is proven. Strict standard header ordering (43 after 34, 122 after 52) is *not* required for interop and would mean threading the fields into the middle of the writer sequence for no behavioral gain. The live QFcpp/QFJ re-run is the final proof.

**Alternatives considered**: strict header-position insertion — rejected as unnecessary complexity (research D-3 order-safety) and divergent from the sibling emitter's shipped pattern.

## D-4 — DEFECT 2 fix preserves replay semantics (skip stored 43/122, keep the 52 capture)

**Decision**: In `build_replay_frame`'s copy loop, widen the existing `if (tag == 9 || tag == 10) continue;` skip to also skip `43` and `122`. The `if (tag == 52) orig_sending_time = …;` capture stays **before** the skip, so the engine's unconditional `122` append still uses the stored `52`.

**Rationale**: Today the loop copies every field (including a retained caller `43`/`122`) then unconditionally appends the engine's `43=Y` + `122=<stored 52>` → duplicate tags when the stored frame already had them. Skipping the stored `43`/`122` makes the engine-appended pair authoritative and the output single-valued. This matches QFJ's resend, which does `header.setField(PossDupFlag)` / `setField(OrigSendingTime)` on a parsed message — **replace**, not duplicate — and sources `122` from the message's own `SendingTime`, **not** any caller-supplied `122`. So after the fix the replayed `122` is the stored `52` (FR-005), exactly as QFJ. Ordering of the capture vs the skip is load-bearing: capturing `52` after a `52`-skip would be fine (`52` is not skipped), but capturing must precede the *use*; current code already captures inside the loop before the append, so only the skip predicate widens.

**Alternatives considered**: (a) conditionally append `43`/`122` only if absent in the stored frame — rejected: more branches, and a stored caller `122` could differ from the stored `52`, leaving a non-QFJ value; the skip-then-append form is simpler and engine-faithful. (b) Strip in the store path instead — rejected: out of scope (the retain knob deliberately stores verbatim, 022 FR-006), and the defect is a *replay* serialization bug.

## D-5 — Scope boundaries (non-expansions)

1. **The GapFill builder is the only SequenceReset emitter.** `build_sequence_reset_gapfill` hardcodes `123=Y`; there is no reset-mode (`123=N`) SequenceReset builder in `admin_messages.cpp`. fixpp therefore only ever emits GapFill-mode resets, all of which are legitimately possdup — so adding `43`/`122` cannot wrongly stamp a non-duplicate reset. No scope ambiguity.
2. **Single production caller.** `replay_outbound_range_` (`session.cpp:4848` GapFill, `:4921` replay) is the only production caller of either builder. No other emit path is affected.
3. **Inbound validation untouched.** This feature changes only *emitted* bytes; fixpp's inbound `43`/`122` admission (021 Stage-1, the `35=4`→122-exempt carve-out at `session.cpp:~2561`) is unchanged.
4. **C-103 chunked-resend stays deferred.** The whole `[next_expected, 0]`-in-one-pass resend behavior is unchanged; only the per-frame tags change.
5. **Store bytes untouched.** Stored frames are not rewritten; only re-serialization output changes. No `MessageStore` interface or on-disk format change. The 034 at-rest 554-redaction boundary (`store_then_emit`) is unrelated and untouched.

## D-6 — No new surface (Article X / FR-007)

`fixpp::wire::Writer::append_raw`, `build_sequence_reset_gapfill`, and `build_replay_frame` all pre-exist with their current signatures; `sending_time` is already a `build_sequence_reset_gapfill` parameter. No new public type, signature, error variant, config field, codegen output, or C-ABI symbol. Confirmed against `include/fixpp/session/admin_messages.hpp:168` (signature unchanged — only the doc-comment field list updates).

## D-7 — Golden + live impact (DEFECT 1 is a default-path wire change)

**Default-path wire change**: every fixpp-emitted GapFill gains `43`/`122`. Banked goldens that capture a **fixpp-emitted** GapFill must be re-baked; goldens capturing a **received** (peer-emitted) GapFill do not change.

Candidate goldens containing `35=4` (to inspect during implement — re-bake only those where fixpp is the GapFill sender): `HP-{QFcpp,QFj}-init-fix44-disconnect-reconnect-noreset.fix`, `RL-{QFcpp,QFj}-init-fix44-reset-on-logon.fix`. The 030 `RR-*` received-reset cells and any in-repo `diff_golden_or_skip` resend goldens must also be checked.

**Live re-run**: the QFcpp/QFJ resend / received-reset cells (both roles) re-run to confirm peers *accept* the now-`43=Y` GapFill — "peers tolerate absence today" is not evidence they accept presence (both engines send it themselves, so acceptance is expected, but it is verified, not assumed). SC-004.

DEFECT 2 is **inert on the default path** (default-strip ⇒ clean stored frames), so it touches no default golden; its witness sets `allow_pos_dup=true` explicitly.
