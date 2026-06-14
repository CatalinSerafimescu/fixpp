# Phase 0 Research — Resend-reply PossDup wire conformance (037)

All line numbers are submodule HEAD (post-036, branch `037-resend-reply-possdup-tags`). Reference-engine source is the cloned `reference-engines/` tree (QuickFIX-cpp v1.16.0, QuickFIX-J 3.0.1), verified 2026-06-14. **Resolvable paths** (these live at the PARENT repo root, gitignored, OUTSIDE the `library` submodule per memory `project_reference_engines_setup` — invisible to a submodule-cwd read-only sandbox, so a reviewer needs the parent tree to re-derive):

- QFcpp: `reference-engines/quickfix-cpp/src/C++/Session.cpp` — `Session::generateSequenceReset` (`:739`), `Session::insertOrigSendingTime` (`:106`; the `122=52` stamp at `:747`).
- QFJ: `reference-engines/quickfixj/quickfixj-core/src/main/java/quickfix/Session.java` — `generateSequenceReset` (`:1407`; the `122=52` stamp at `:1414`), `validatePossDup` (`:~2575`; SequenceReset exemption guard at `:~2580`).

## D-1 — OrigSendingTime(122) value for an administratively-generated GapFill = the GapFill's own SendingTime(52)

**Decision**: The GapFill's `122` is byte-equal to the `52` the same frame stamps. No new timestamp is generated; the builder's existing `sending_time` parameter is reused.

**Rationale**: A GapFill covers a *range* of skipped administrative messages — there is no single "original message" with a single original `52` to copy. Both reference engines resolve this the same way:
- **QFcpp** `Session::generateSequenceReset` (`src/C++/Session.cpp`): `insertOrigSendingTime(header, header.getField<SendingTime>())` — `122` is set to the `SendingTime` just stamped by `fill()`.
- **QFJ** `Session.generateSequenceReset` (`quickfixj-core/.../Session.java:~1410`): `header.setUtcTimeStamp(OrigSendingTime.FIELD, header.getUtcTimeStamp(SendingTime.FIELD), getTimestampPrecision())` — `122 = 52`.

**Alternatives considered**: (a) a stored original `52` — rejected, no single original exists for a range; (b) a fresh `clock.now()` distinct from `52` — rejected, diverges from both engines and would need a new parameter + a clock dependency the builder does not have. Because `122 == 52`, precision is automatically matched (same string), so the 026 nanos precision work needs no involvement.

## D-2 — Emitting PossDupFlag(43)=Y on the GapFill carries OrigSendingTime(122) by emit-parity

**Decision**: `43=Y` and `122` are emitted together on the GapFill, never `43` alone.

**Rationale (re-grounded — the earlier "strict peer rejects a 43=Y GapFill missing 122" claim is FALSE for a SequenceReset)**: A `SequenceReset(35=4)` is **exempt** from QFJ's inbound `122`-required / `122 ≤ 52` check. `validatePossDup` (`Session.java:~2575`) guards the *entire* OrigSendingTime block — both the `122 ≤ 52` boundary and the `RequiresOrigSendingTime=Y` → reject arm — behind `if (!MsgType.SEQUENCE_RESET.equals(msgType))`. So a strict `RequiresOrigSendingTime=Y` QFJ peer does **not** reject a `43=Y` GapFill whose `122` is absent (this mirrors fixpp's own inbound `35=4`→`122`-exempt carve-out, D-5.3). The correct justification for `122 = 52` on the GapFill is therefore:

1. **Emit-parity** — both reference engines unconditionally stamp `43=Y` + `122 = SendingTime` on *every* GapFill they generate (D-1: QFcpp `Session.cpp:747`, QFJ `Session.java:1414`). fixpp emitting the same makes its GapFill byte-shaped like the engines'.
2. **FIX grammar correctness** — when `43=Y` is present, `122` is its conditionally-required companion; a `43=Y` frame lacking `122` is malformed in the general possdup grammar even where the SequenceReset inbound path happens not to police it. So emitting `43` without `122` would be *less* well-formed than today, not more conformant.

The `122 ≤ 52` boundary remains the general possdup inbound rule (it applies to replayed *application* frames, which are not `35=4`-exempt — see C-2); it is **not** a GapFill-reject trigger. `122 == 52` trivially satisfies that boundary where it does apply.

## D-3 — Field placement: append at end (after 123), order-safe

**Decision**: Append `43` then `122` at the end of the GapFill's field list (after `123=Y`), mirroring `build_replay_frame`'s append-at-end pattern.

**Rationale**: The **primary** proof of order-safety is the live-shipped precedent: `build_replay_frame` already appends `43`/`122` after *all* copied fields (including body fields, `session.cpp:1657-1671`) and ships LIVE green against both QFcpp and QFJ today — so a header-tag-after-body-tag shape is already proven interoperable with both live targets. Map-based parsing is the **secondary** explanation for *why* it works (both engines parse the header into a field map — `isSetField`/`getField`/`getUtcTimeStamp` — so field order is irrelevant to inbound validation). Strict standard header ordering (43 after 34, 122 after 52) is *not* required for interop and would mean threading the fields into the middle of the writer sequence for no behavioral gain. The live QFcpp/QFJ re-run is the final confirmation.

**Limitation**: fixpp appends these StandardHeader tags (`43`/`122`) *after* the GapFill's body fields (header-after-body shape) — interoperable with QuickFIX's map-based parsers and the live QFcpp/QFJ targets, but **not** strict-header-order canonical. A strict positional-header-order validator is out of scope. (Recorded as a B&L conformance row at Polish.)

**Alternatives considered**: strict header-position insertion (`43` after `34`, `122` after `52` in the header region) — rejected as unnecessary complexity (order-safe per the live precedent above) and divergent from the sibling emitter's shipped pattern.

## D-4 — DEFECT 2 fix preserves replay semantics (skip stored 43/122, keep the 52 capture)

**Decision**: In `build_replay_frame`'s copy loop, widen the existing `{9,10}` skip (`if (tag == 9 || tag == 10) continue;`) to `{9,10,43,122}`. The `if (tag == 52) orig_sending_time = …;` capture is a *separate* `if` that runs during normal iteration — `52` is never in the skip set, so the capture is unaffected by the widening and needs no move. The engine's unconditional `122` append still uses the captured stored `52`.

**Rationale**: Today the loop copies every field (including a retained caller `43`/`122`) then unconditionally appends the engine's `43=Y` + `122=<stored 52>` → duplicate tags when the stored frame already had them. Skipping the stored `43`/`122` makes the engine-appended pair authoritative and the output single-valued. This matches QFJ's resend, which does `header.setField(PossDupFlag)` / `setField(OrigSendingTime)` on a parsed message — **replace**, not duplicate — and sources `122` from the message's own `SendingTime`, **not** any caller-supplied `122`. So after the fix the replayed `122` is the stored `52` (FR-005), exactly as QFJ. The only edit is widening the skip predicate; the `52` capture is a distinct `if (tag == 52)` that fires during normal iteration (52 is not skipped) and already runs before the `122` is appended, so it is untouched.

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

**Live re-run**: the **QFJ** in-process recovery-outbound cell + any live QFJ resend / received-reset cell (both roles) re-run to confirm the peer *accepts* the now-`43=Y` GapFill — "peers tolerate absence today" is not evidence they accept presence (QFJ sends it itself, so acceptance is expected, but it is verified, not assumed). The **QFcpp arm is waived** (L-021-3): the only in-process witness `GTEST_SKIP()`s non-QFJ counterparties and QFcpp cannot be induced into the fixpp-emitted-GapFill path on command; QFcpp byte-level conformance is covered by the unit cells + the re-baked golden. SC-004.

DEFECT 2 is **inert on the default path** (default-strip ⇒ clean stored frames), so it touches no default golden; its witness sets `allow_pos_dup=true` explicitly.

## D-8 — Existing-test impact survey (pre-Gate-A; closes the self-check-vs-real-verification gap)

Surveyed every test referencing the GapFill / `35=4` outbound emit (`grep tests/`). Three findings that feed `/tasks`:

1. **No existing test asserts the *absence* of `43`/`122` on a GapFill** — so fix #1 inverts no assertion. `tests/session/test_recovery_admin_span_gapfill.cpp` parses the emitted GapFill via `is_sequence_reset_gapfill`, which checks only `35=4` + `123=Y` + `36=<newseqno>` (substring presence) — adding `43`/`122` leaves it green. (It *may* warrant a strengthening task to also assert the new tags, but it is not broken.)
2. **`build_sequence_reset` (with a `gap_fill` bool) is a TEST-LOCAL helper, not production** — `grep src/ include/` finds no such production symbol; `build_sequence_reset_gapfill` is the sole production SequenceReset emitter. Confirms D-5.1.
3. **The fixpp-emits-GapFill witness is `tests/interop/happy/hp_fix44_recovery_outbound_answer_test.cpp`** (`49=FIXPP_INIT` on the `35=4` reply, via 013's `build_sequence_reset_gapfill` path), golden-compared. It currently compares the GapFill under the **`{52,10}` admin profile** (`golden_diff.hpp:54` `admin_profile_excluded_tags()`). Because the new `122` **equals `52` (a live wall-clock timestamp)**, comparing it verbatim would make the golden non-deterministic across runs. **The required change is already supported by existing infrastructure**: `golden_diff.hpp:69` already defines `poss_dup_profile_excluded_tags()` — the `{52,10,122}` profile (composed from `admin_profile_excluded_tags()` + `122`, for the replay path, which already carries `122`). The GapFill comparison must switch from `admin_profile_excluded_tags()` (`{52,10}`) to `poss_dup_profile_excluded_tags()` (`{52,10,122}`), and the `hp_fix44_recovery_outbound_answer` golden(s) re-baked. `43=Y` is deterministic (always `Y`) and is correctly compared verbatim. The synthetic gate-bite cells (`hp_fix44_recovery_outbound_answer_test.cpp:95-107`, which mutate `123`) stay valid under the extended profile.

**Net**: SC-004's live/in-process GapFill witness already exists (QFJ-only — it `GTEST_SKIP()`s non-QFJ counterparties, `:166-171`; the QFcpp arm is waived per L-021-3); the only new verification work is (a) the `admin_profile_excluded_tags()`→`poss_dup_profile_excluded_tags()` (`{52,10}`→`{52,10,122}`) profile switch on the GapFill comparison + golden re-bake, and (b) the new unit cells (data-model INV-1..4) + the live QFJ recovery re-run. No undiscovered test inversion.
