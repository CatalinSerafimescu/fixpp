# Implementation Plan: Resend-reply PossDup wire conformance (GapFill 43/122 + replay dup-tag suppression)

**Branch**: `037-resend-reply-possdup-tags` | **Date**: 2026-06-14 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/037-resend-reply-possdup-tags/spec.md`

## Summary

Close the two resend-reply wire-conformance defects from Fable **F-e** (assessments `2.4` §3 / `1.6` §5), both in the resend-reply path (`replay_outbound_range_`):

- **DEFECT 1 (default path) — `build_sequence_reset_gapfill` (`admin_messages.cpp:905`) omits `PossDupFlag(43)=Y` and `OrigSendingTime(122)`.** A GapFill's `MsgSeqNum(34)` sits at/below the peer's expected number — exactly the too-low position where `43=Y` suppresses a too-low-seqnum kill. `122` is emitted alongside `43` by **emit-parity** (both reference engines stamp `122 = SendingTime` on every GapFill they generate) and FIX grammar correctness, NOT to dodge a reject — a SequenceReset(`35=4`) is exempt from the inbound `122`-required check (QFJ `validatePossDup` guards it behind `if (!MsgType.SEQUENCE_RESET…)`; research D-2). Fix: append `43=Y` and `122 = <the GapFill's own 52>` (the `sending_time` parameter the builder already receives — no new parameter). Both reference engines (QFcpp `Session::generateSequenceReset`, QFJ `Session.generateSequenceReset`) do exactly this.
- **DEFECT 2 (non-default path) — `build_replay_frame` (`session.cpp:1625`) unconditionally appends `43`/`122`**, so a stored frame that already carries them (the `allow_pos_dup=true` retain case) replays with **duplicate** `43`/`122` tags → strict-peer reject. Fix: widen the copy-loop skip from `{9,10}` to `{9,10,43,122}`. The `52 → orig_sending_time` capture is a separate `if (tag==52)` that runs during normal iteration (52 is never in the skip set), so it is unaffected — the engine's appended `122` still uses the captured stored `52`.

Both builders are `noexcept`, stack-only (`fixpp::wire::Writer` over `std::pmr::null_memory_resource()`), and emit through the same resend reply. This is a **pure wire-output correction**: no new public signature, error slot, config field, codegen, or C-ABI surface (FR-007). It amends the *emitted-bytes* (re-serialization) disposition of the resend-reply path — catalogue **S-006** (`build_sequence_reset_gapfill` GapFill emit, §4.8.5), **S-010** (022 `allow_pos_dup` / `build_replay_frame`, §4.8.4), and **S-005** (013 resend-reply store-walk replay, §4.8.2); it leaves stored bytes untouched. It is **not** C-103 chunked-resend (deferred).

### Why one feature, not two

The two defects are the two halves of the same resend-reply emitter pair (DRIFT #2 in the half-restructure audit pairs them explicitly). Fixing one without the other leaves the audit pair half-closed — the exact half-restructure trap (012 RC#B) the project closes in one pass. They share the witness harness (a resend reply that emits both a GapFill and a replayed app frame) and the same live re-run.

### Default vs non-default wire impact (drives verification)

- **DEFECT 1 changes the DEFAULT-path bytes** — every GapFill on every resend reply gains two tags. Verification MUST re-bake any banked golden containing a fixpp-emitted GapFill and re-run the live **QFJ** resend / received-reset cells (both roles) to confirm the peer *accepts* the now-marked GapFill (peers tolerate *absence* today; that is not evidence they accept *presence*). The QFcpp live arm is waived (L-021-3; the in-process witness skips non-QFJ) — covered by unit cells + golden.
- **DEFECT 2 is inert on the default path** — default config strips caller `43`/`122` on send, so stored frames are already clean; the suppression only bites under `allow_pos_dup=true` with caller-supplied tags. Its witness MUST set the knob explicitly. Default-path replayed frames stay byte-identical (FR-006).

## Technical Context

**Language/Version**: C++23 (Clang 22 local == CI per `[const Art.II §2]`)
**Primary Dependencies**: `fixpp::session::build_sequence_reset_gapfill` (`admin_messages.cpp:905`), `build_replay_frame` (`session.cpp:1625`), `fixpp::wire::Writer` (`append_raw` / `commit`), the `replay_outbound_range_` resend reply (`session.cpp:4848` GapFill call / `:4921` replay call), the `allow_pos_dup` send knob (022, `session_config.hpp:380`)
**Storage**: none — re-serialization of already-stored frames; no `MessageStore` change. The stored bytes are untouched; only the emitted (replayed) bytes change
**Testing**: GoogleTest unit cells parsing the emitted frame fields (count `43`/`122` occurrences, assert `122 == 52`); golden re-bake for the default-path GapFill; live **QFJ** resend cells re-run (both roles; QFcpp live arm waived per L-021-3 — covered by unit cells + golden); ASan/UBSan over the session + admin-message suites as regression
**Target Platform**: Linux (Tier-1); role-symmetric (the resend reply path is shared by initiator + acceptor)
**Project Type**: single library (`fixpp`)
**Performance Goals**: no change — two extra `append_raw` calls per GapFill (bounded, no heap; the writer is `null_memory_resource`-backed, `[const §XV.1]` per-message heap ban preserved by construction); the replay skip removes work
**Constraints**: no public signature / error variant / config / codegen / C-ABI change (FR-007); default-path replayed frames byte-identical (FR-006); exactly one `43` + one `122` on every replayed frame (FR-004); `122` sourced from the stored `52` (FR-005)
**Scale/Scope**: ~6–10 LoC production across two functions (2 `append_raw` blocks added to the GapFill builder; one widened skip condition in the replay copy loop); 1 new/extended test file; golden re-bake; B&L + catalogue rows. No header-graph change

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Article | Relevance | Disposition |
|---|---|---|
| **VI — Spec Coverage Discipline (100% FIX Rule)** | Wire fields `43`/`122` on `35=4`; Normative References (§VI.5) | `PossDupFlag(43)` and `OrigSendingTime(122)` are existing catalogued header fields; `SequenceReset(35=4)` already exists. No new message/field — this corrects which fields an existing frame carries. Catalogue gains a traceability row; B&L gains a conformance row. **PASS — the `## Normative References` section now exists in `spec.md` per §VI.5 (cites `[FIX-SL §4.8.5]`/`§4.8.4`/`§4.8.2` paired to catalogue rows S-006/S-010/S-005/S-033); the round-1 Gate A finding that it was missing is resolved.** |
| **VII — Testing Requirements (≥ seams)** | New wire behavior | New seam: a resend-reply witness asserting the GapFill carries `43=Y`+`122==52` and a replayed frame (under `allow_pos_dup`) carries exactly one each. Golden re-bake + live re-run. PASS. |
| **IX §1 — Coverage / Sanitizers** | New + changed branches | The two `append_raw` blocks (GapFill) and the widened skip predicate (replay) reach 100% DA/BRDA; the `append_raw` error arms reuse the existing `std::unexpected` pattern. Full ASan/UBSan over the touched suites. PASS. |
| **X — ABI Policy** | Public surface | `build_sequence_reset_gapfill` signature **unchanged** (`sending_time` already a parameter); `build_replay_frame` signature unchanged; no new error variant, type, or public symbol. PASS. |
| **XV §1 / VIII §5 — banned per-message heap** | Builder allocation | Both builders construct `Writer` over `std::pmr::null_memory_resource()` (no heap); the added `append_raw(43,…)`/`append_raw(122,…)` write into the caller's stack `out` span. No per-message `new`/`delete`. PASS by construction. |
| **XIII — Observability & Logging** | None | No trace-context / logging change. PASS. |
| **XII — Security & TLS** | Mild anti-replay flavor | `122` (OrigSendingTime) on a possdup is the protocol's resend-staleness signal; emitting it *improves* conformance. No TLS/identity surface. PASS. |
| **XVII — Codex Review Gates** | Gate A/B | Gate A after this plan (before `/tasks`); Gate B before merge. |
| **Dependencies / Version Management** | None added | No new third-party dependency. |

**Surface delta**: no wire field added to the catalogue as *new* (`43`/`122` pre-exist), no `SessionConfig`/`EngineConfig` field, no codegen, no C-ABI, no new error variant, no public-signature change. The change is **emitted-bytes correction** in two existing resend-reply builders plus tests, a golden re-bake, and doc rows. Gate-clean.

## Project Structure

### Documentation (this feature)

```text
specs/037-resend-reply-possdup-tags/
├── plan.md              # This file
├── research.md          # Phase 0 — reference-engine ground truth (122=52, 43-requires-122, map-based order-safety); call-site + golden inventory; scope non-expansions
├── data-model.md        # Phase 1 — the resend-reply frame field matrix (GapFill before/after; replay default vs retain) + invariants
├── quickstart.md        # Phase 1 — the resend-reply witness recipe (field-occurrence counting, 122==52, allow_pos_dup retain cell) + golden re-bake + live re-run steps
├── contracts/
│   └── resend-reply-wire.md   # internal contract: the exact field set + PossDup invariants of a GapFill and a replayed app frame
├── checklists/
│   └── requirements.md  # spec-quality checklist (done; clarify resolved)
└── tasks.md             # /speckit-tasks output (NOT created here)
```

### Source Code (repository root)

```text
src/session/
├── admin_messages.cpp   # DEFECT 1 — build_sequence_reset_gapfill (:905):
│                        #   after the 123=Y block (:964-970), before commit (:972), append:
│                        #     append_raw(43, "Y")            # PossDupFlag
│                        #     append_raw(122, sending_time)  # OrigSendingTime = the GapFill's own 52 (param already present)
│                        #   update the field-list comment (:900-901) to include 43/122
└── session.cpp          # DEFECT 2 — build_replay_frame (:1625):
                         #   widen the copy-loop skip (:1652) from {9,10} to {9,10,43,122};
                         #   the 52→orig_sending_time capture (:1653-1656) is a SEPARATE if(tag==52) that runs
                         #   during normal iteration (52 not in the skip set) — unaffected; it already runs
                         #   before the appended 122 (:1666-1671), so the engine's 122 still uses the stored 52.
                         #   The unconditional append of 43=Y (:1659-1665) + 122 (:1666-1671) is unchanged.

include/fixpp/session/admin_messages.hpp   # update the build_sequence_reset_gapfill doc-comment field list (no signature change)

tests/session/
└── test_resend_reply_possdup.cpp   # NEW (or extend an existing resend test) — resend-reply witness:
                         #   - GapFill cell: drive a resend reply that produces a GapFill; parse the emitted frame;
                         #     assert exactly one 43=Y AND exactly one 122 AND 122-value == 52-value AND every
                         #     pre-existing field (8/35/34/49/52/56/36/123) unchanged.
                         #   - Replay-retain cell: allow_pos_dup=true; store an app frame already carrying 43=Y/122=<t>;
                         #     replay it; assert exactly one 43 AND exactly one 122 AND 122 == stored 52 (NOT caller t).
                         #   - Default-replay non-regression: allow_pos_dup=false; assert replayed bytes byte-identical to pre-feature.

tests/interop/happy/golden/   # LIBRARY-LOCAL golden for the in-process witness (the load-bearing re-bake):
                         #   hp_fix44_recovery_outbound_answer resolves golden_ref = "happy/golden/" + cell_id + ".fix"
                         #   (test :148) — re-bake this one under the {52,10,122} profile.
                         #
                         # ../phase-9-harness/golden/  # SEPARATE live-cell golden dir (a SIBLING of library/, NOT under the
                         #   library root). Inspect candidates containing a fixpp-emitted 35=4:
                         #   HP-{QFcpp,QFj}-init-fix44-disconnect-reconnect-noreset, RL-{QFcpp,QFj}-init-fix44-reset-on-logon.
                         #   Only fixpp-emitted GapFills change; received GapFills do not.

spec/
├── behaviors-and-limitations.md          # new conformance row: resend GapFill carries 43=Y/122; replay dedups 43/122 under allow_pos_dup.
                                          #   + limitation row: GapFill 43/122 appended after body (header-after-body shape) — interoperable with
                                          #     QuickFIX map-based parsers + the live QFcpp/QFJ targets, NOT strict positional-header-order canonical (research D-3).
└── feature-catalogue.md / coverage-index.md   # traceability row for 037

specs/013-*  +  specs/022-*   # dated notes: 013 resend-reply GapFill now stamps 43/122 (amends the S-005/S-006 emit disposition; stored bytes untouched);
                              # 022's build_replay_frame INV-5 (S-010) now dedups stored 43/122 on replay (no history rewrite)
```

**Structure Decision**: Single-library, in-place. Two builder bodies change; no new module, header, or header-graph edge (`tools/check_layers.py` unaffected). The `build_sequence_reset_gapfill` signature is unchanged because `sending_time` is already a parameter — so no caller (`session.cpp:4848`) changes. No new exported surface (Art. X).

## Phase 0 — Research

See [research.md](./research.md). Key decisions:

1. **`122` value = the GapFill's own `52`** — verified against both reference engines (QFcpp `insertOrigSendingTime(hdr, hdr.getField<SendingTime>())`; QFJ `setUtcTimeStamp(OrigSendingTime, getUtcTimeStamp(SendingTime))`). The builder's existing `sending_time` parameter is exactly this value → no new parameter.
2. **`43=Y` carries `122` by emit-parity (NOT a strict-peer reject)** — a SequenceReset(`35=4`) is exempt from QFJ's inbound `122`-required check (`validatePossDup`, `Session.java:~2575`, guarded by `if (!MsgType.SEQUENCE_RESET…)`), so a strict peer does *not* reject a GapFill missing `122`. `122=52` is warranted instead by emit-parity (both engines stamp it on every GapFill) + FIX grammar correctness (`122` is `43=Y`'s conditionally-required companion). Both tags emit together; emitting `43` alone would be *less* well-formed. (research D-2.)
3. **Append-at-end ordering is order-safe** — both engines parse the header into a field map (`isSetField`/`getField`), so field order is irrelevant to inbound validation. Matches the proven append-at-end pattern `build_replay_frame` already ships live.
4. **DEFECT 2 fix preserves replay semantics** — widening the skip to `{9,10,43,122}` then re-appending the engine's `43=Y` + `122=<stored 52>` matches QFJ's resend (`setField` replaces rather than duplicates; `122` = the stored original `52`, not any caller-supplied `122`). The `52` capture is a separate `if (tag==52)` unaffected by the widened skip (52 is not skipped).
5. **Scope non-expansions** — (a) the GapFill builder is the *only* SequenceReset emitter (`123=Y` hardcoded), so there is no non-possdup reset-mode frame to wrongly stamp; (b) `replay_outbound_range_` is the single production caller of both builders; (c) inbound `43`/`122` validation is untouched; (d) C-103 chunked-resend stays deferred.
6. **No new surface confirmed** — `append_raw` and both builder signatures pre-exist; no config/wire/error/codegen/C-ABI change.

## Phase 1 — Design & Contracts

- [data-model.md](./data-model.md) — the resend-reply **frame field matrix**: GapFill field set before/after (adds `43`/`122`); replayed-frame field set on default vs `allow_pos_dup` retain (exactly-one-each invariant); the `122 == 52` invariant for both frame kinds.
- [contracts/resend-reply-wire.md](./contracts/resend-reply-wire.md) — the wire contract: (a) every emitted GapFill carries exactly one `43=Y` and one `122` equal to its `52`; (b) every replayed app frame carries exactly one `43` and one `122` regardless of stored content; (c) `122` is always the stored/own `52`; (d) default-path replay byte-identity.
- [quickstart.md](./quickstart.md) — the resend-reply witness recipe (field-occurrence counting + `122==52`), the `allow_pos_dup` retain cell, the default-path byte-identity check, the golden re-bake list, and the live QFJ re-run (QFcpp arm waived per L-021-3).

## Complexity Tracking

No constitution violations to justify. The feature *removes* a conformance gap (and, for DEFECT 2, *removes* duplicate-tag work) rather than adding machinery; it touches two existing blessed builders, adds no surface, and reuses the existing `append_raw`/`Writer` path. The one non-mechanical choice — `122 = own 52` for the GapFill — is forced by reference-engine parity (research decision 1), not added speculatively.

## Gate A

- Round 1 applied 2026-06-14: Codex P1=1 P2=5 P3=2; Opus post-judging P1=0 P2=3 P3=5; rewrite addresses root causes (Normative-References false-PASS, SC-004 QFcpp waiver, inverted 122-reject rationale, capture-order/append-order/path/checklist/profile-name doc fixes). Reviews: research/reviews/codex_037-resend-reply-possdup-tags_gate_a_review.md, research/reviews/opus_037-resend-reply-possdup-tags_gate_a_adversarial_review.md.
- Round 2 (2026-06-14): Codex CONVERGES P1=0 P2=0 P3=0; Opus post-judging confirms P1=0 P2=0 P3=0 — **CONVERGED** (no rewrite). Opus independently re-derived every round-1 fix from primary source (QFJ `validatePossDup` SequenceReset exemption; S-005/006/010/033 catalogue rows; L-021-3 waiver; GTEST_SKIP non-QFJ) and found no rewrite-introduced drift. Reviews: research/reviews/codex_037-resend-reply-possdup-tags_gate_a_2_review.md, research/reviews/opus_037-resend-reply-possdup-tags_gate_a_2_adversarial_review.md.
