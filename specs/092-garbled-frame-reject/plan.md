# Implementation Plan: The session never acts on a frame it could not parse

**Branch**: `092-garbled-frame-reject` | **Date**: 2026-09-27 (Gate A round 2 rewrite) | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/092-garbled-frame-reject/spec.md` (clarified 2026-09-27;
Gate A round 1 and round 2 owner rulings integrated)

## Summary

Today a frame that passes framing but fails the parse is acted on through its scanned header fields.
A SequenceReset is applied. An application message is persisted and consumed, but neither delivered
nor rejected. A Logon with a malformed tag is accepted.

This feature makes the header scan (`scan_frame_header`) record its **first fault**, stop there,
and identify the header **by position** in the fault record: whether field 3 is 35, its value, and
the first 34. The scan's existing members keep today's last-wins semantics. The scan already
runs in every LogonReceived/Active, LogonSent and LogoutSent frame, and is hoisted to the top of the
NotConnected arm. Right after it, each state arm branches inline on the fault. Only then does it
`co_await dispose_unparseable_`, which decides the frame per the owner ruling of 2026-09-27:
- **Logon refusal** for any faulty frame while awaiting a Logon, and for a faulty Logon while
  Active;
- in LogonReceived/Active, **disregard** when field 3 is not 35 (§4.5.2 criterion 3), or when 34
  was not read before the fault;
- **disregard** in LogoutSent;
- otherwise a **Reject** under #423's seqnum rows. That includes a malformed Reject or Logout,
  which supersedes the no-reject-loop exemption for faulty frames.

Nothing after the fault is read, and nothing except 34, 35 and the field-3 bit is used.

A parse can still fail at a late inbound site on a fault-free frame, through resource exhaustion
(reachable with a large well-formed message) or a scan/parse disagreement. Such a **late parse
failure** closes the session (fail-closed, owner ruling O-2); its proper disposition is fixpp#515. A
Framer failure stays session-fatal (`L-004-4`); its §4.5.2 disregard is fixpp#514 (owner ruling
O-1). A differential corpus and a fuzz arm pin the scan/parse agreement.

`dictionary_driven_validator::validate`'s silent stop becomes a reject through a field-iterator
fault record and two new `core::error` values. The Reject's fixed Text(58) goes through a new
public builder, `build_reject_with_text`, to which `build_reject` delegates. The Logon refusal ships
as **C-ABI 1.10 BREAKING**.

Design decisions: [research.md](./research.md). The behaviour contract:
[contracts/unparseable-frame-disposition.md](./contracts/unparseable-frame-disposition.md).

## Technical Context

**Language/Version**: C++23 (Clang 22 local and CI; GCC and MSVC lanes per Article II)

**Primary Dependencies**: Standalone Asio (coroutines), GoogleTest/GoogleMock, Google Benchmark,
libFuzzer. No new dependency.

**Storage**: The existing MessageStore. Every Reject is persisted like any outbound session message
(FR-013). An advance is persisted through `consume_rejected_seqnum_` (#423, unchanged).

**Testing**:
- GoogleTest in the grouped session bucket (`ctest -L 092`) and the wire bucket for FR-012;
- a differential mutation corpus with accepted controls;
- a fuzz arm in `fuzz_session_recovery_admin_parse`;
- an in-process scripted peer that replays on ResendRequest (SC-007);
- a `tests/capi` Logon-refusal witness and the version pin (FR-017);
- pytest unchanged except version consumers.

**Target Platform**: Linux (clang/gcc, sanitizer lanes), Windows (MSVC), as for the library.

**Project Type**: Library (the fixpp FIX engine): the session layer, a wire-layer fault record, and
the error surface.

**Performance Goals**: No regression above the `[const §VIII.2]` +5% budget on the clean inbound
path. The measurement is paired merge-base A-B-A-B over a scan micro-bench and a clean Active
`on_inbound_frame` bench, both landed bench-only first and listed in `bench/ci-suite.txt` (research
R-9).

**Constraints**:
- `[const §VIII.5]` zero allocation between parse and callback. The fault record is plain members,
  the disposition branch is inline (no coroutine frame on the clean path, R-3), and the Reject Text
  is a compile-time constant built into the existing 512-byte stack buffer.
- `noexcept` on the scan and dispose paths.
- Private headers stay private.

**Scale/Scope**:
- `src/session/scan_frame_header.hpp`;
- `session.cpp`: 4 state arms, 1 new coroutine, the fail-closed close at every late inbound parse
  site, and the replay classification guard (R-12);
- `admin_messages.{hpp,cpp}`: the new `build_reject_with_text` and the no-reject-loop sentence;
- `parser.hpp`, `tag_scan.hpp`, `validator.hpp`, `core/error.hpp`, `reject_reason_map.hpp` and
  `src/capi/error.cpp` (FR-012);
- the C-ABI 1.10 carriers: `version.h`, `session.h` and the freeze manifest.

Plus tests, two benches and docs.

## Constitution Check

*GATE: must pass before Phase 0; re-checked after Phase 1 (below).*

| Article | Requirement | Status |
|---|---|---|
| VII §3–4 TDD | failing test first, no code without a test | Planned. Every C-2 row, every late parse site (C-6) and every C-3 invariant is a RED-first cell, and T076 is the anchor RED. Each RED witness is an input the pre-feature code mishandles (quickstart §1) |
| VII §5 conformance | the FIX-TC corpus must pass | TC 2d / 3b / 3c unchanged: terminal (`L-004-4`, which diverges from TC 2d; FR-008). Their disregard is fixpp#514. TC 14-class Rejects gain siblings (0 / 5). TC2020 17d's 373=8 is a disclosed deviation (contract C-5 L-4). Check for any existing TC cell asserting silence on a parse failure (research R-10) |
| VII §7 fuzz | parser-touching code needs a fuzz harness | The scan is parser-touching, so an equivalence arm goes in the existing harness (R-2). The field iterator's fault record is covered by `fuzz_wire_validator` gaining a fault-kind assertion. #508 caveat disclosed |
| VII §8 test grouping | new isolation-safe tests go into a grouped bucket, selected by label | Planned (`092` label) |
| VIII §2 perf | paired merge-base A-B-A-B, +5% budget | Two benches land bench-only first and are listed in `bench/ci-suite.txt` (R-9) |
| VIII §5 zero-alloc | no heap between parse and callback | Held: plain members, an inline branch, a constant Text |
| IX sanitizers / coverage | per-line coverage assessment | `/speckit-verify` matrix. The fault branches are covered by the corpus and the cells |
| X ABI | C-ABI changes versioned; public C++ API additive | **Yes — `[const §X.7]` BREAKING, C-ABI 1.10** (owner ruling 2026-09-27, FR-017). No symbol, signature or error code is added. The change is in which Logons are accepted, plus D-3's disconnect, observed by the calls research R-8's recipe derives. The procedure is Phase 0b. The C++ additions are source-compatible: `build_reject_with_text` is a new name and `build_reject` keeps its single declaration (contract C-4). `§X.6` Appendix A controls: `/clarify` ✅ (Session 2026-09-27 and Gate A round 1 rulings); `/analyze` ⏳ after `/speckit-tasks`; Gate A ⏳ (round 2 applied, see §Gate A); user `/plan` sign-off ⏳ after Gate A converges |
| XI concurrency | — | Not touched: the disposition runs inside the existing per-session strand coroutine |
| XII security | fail closed | This feature *closes* fail-opens: acting on unparsed bytes, and accepting a Logon with a malformed tag |
| XVI §3–4 | `/clarify` and `/analyze` mandatory (session FSM, error semantics, parser) | `/clarify` done; `/analyze` due after `/tasks` |
| XVI §6 | the orchestrator does not implement | Implementation is delegated to `phase-implementer`, with mutation proofs in a scratch copy |
| XVII §1 Gate A | parser + session FSM + public C++ API + C-ABI → required | **Required before `/tasks`**; round 2 applied |
| XVII §7 | local build gate before PR; builds need owner approval | Planned; ASK before each build |

**Result: PASS**, with the §X.7 BREAKING declaration as a sanctioned pre-release change, not a
violation. `gh release list --exclude-drafts` must be empty at Phase 0b.

## What changes for whom (root cause #3)

| Who | What changes | Declared where |
|---|---|---|
| The FIX counterparty | It receives a Reject (373=0 or 5) where it got silence or a sequence effect. A malformed Reject or Logout is now Rejected. A faulty Logon is refused. A frame faulty before 34, or with field 3 not 35, is disregarded in LogonReceived/Active instead of disconnecting. A well-formed frame that exhausts the parse arena ends the session where it was silently consumed | B&L delta; spec FR-003 to FR-009, FR-016 |
| A C-ABI consumer | A session whose Logon carries a malformed tag never establishes, and a faulty Logon while Active ends an established session. `is_established`, `close`, `send` and both callbacks observe it. A well-formed frame that exhausts the parse arena at a dispatch site now ends the session (contract C-6), a candidate the recipe classifies. `fixpp_session_register_callback`'s "no Reject is sent" text is rewritten | **C-ABI 1.10 BREAKING**: `version.h` and a per-declaration `session.h` clause (research R-8, Phase 0b) |
| A C++ consumer of `build_reject`'s documented rule | "a malformed Reject/Logout is never itself rejected" now holds only for well-formed frames | `admin_messages.hpp` sentence rewritten, with a header comment naming the ruling |
| A C++ consumer of `dictionary_driven_validator` | `validate` returns two new errors where it returned success | contract C-4; B&L |
| A C++ consumer of `field_iterator` | two new accessors; `sizeof` grows; the yield is unchanged | contract C-4 |
| An exhaustive switch over `core::error` | two new enumerators (132, 133) | contract C-4; `[const §X.4]` append-only |
| An operator | disclosed outcomes: the disregard-row resend loop, the malformed-GapFill disconnect, TC 17d, the fail-closed close on a late parse failure | contract C-5 L-1, L-2, L-4, L-6, as B&L rows |

## Project Structure

### Documentation (this feature)

```text
specs/092-garbled-frame-reject/
├── spec.md              # clarified 2026-09-27; Gate A round 1 and round 2 rulings
├── plan.md              # this file
├── research.md          # R-1 … R-13
├── data-model.md        # E-0 fault enum … E-6 error enumerators
├── quickstart.md        # validation guide (baselines first)
├── contracts/
│   └── unparseable-frame-disposition.md   # C-1 order, C-2 table, C-3 invariants, C-4 surface, C-5 disclosures, C-6 late-site close
├── checklists/requirements.md
└── tasks.md             # /speckit-tasks (not created here)
```

### Source Code (repository root)

```text
src/session/
├── scan_frame_header.hpp      # E-1: first fault, positional 35, fault_ref_seq_num / fault_ref_msg_type (R-1)
├── session.cpp                # inline fault branch + dispose_unparseable_ (R-3); NotConnected scan hoist;
│                              #   fail-closed close at every late inbound parse site (C-6); liveness (FR-018);
│                              #   replay gap-fills a stored frame with no 35 (R-12)
└── admin_messages.cpp         # build_reject_with_text; build_reject delegates (R-5)
include/fixpp/session/
├── admin_messages.hpp         # build_reject_with_text declaration; no-reject-loop sentence scoped + ruling-naming comment
└── session.hpp                # private declarations
include/fixpp/wire/
├── tag_scan.hpp               # E-0 field_fault
├── parser.hpp                 # E-4 field_iterator::fault() / fault_length_tag()
├── validator.hpp              # E-5 fault → reject
└── reject_reason_map.hpp      # 132 → 0, 133 → 5
include/fixpp/core/error.hpp   # E-6 enumerators + error_message
src/capi/error.cpp             # translate: both → FIXPP_ERR_WIRE_INVALID_FRAME
include/fix/c_api/version.h    # MINOR 9 → 10 + history (FR-017)
include/fix/c_api/session.h    # BREAKING (1.10) clauses; register_callback text rewrite
include/fix/c_api/error.h      # only if it lists WIRE_INVALID_FRAME's core members (R-7)
tools/capi_freeze.sha256       # re-pinned
tests/session/
├── unparseable_frame_disposition_test.cpp   # NEW: C-2, C-3, C-6, T076 flipped, SC-007 peer
├── scan_frame_header_fault_test.cpp         # NEW: E-1 kinds + the differential corpus (I-4)
├── scan_frame_header_overflow_test.cpp      # R-10
├── length_data_session_scanner_test.cpp     # R-10
├── coverage_adversarial_test.cpp            # R-10: two cells renamed + asserted
├── session_reject_test.cpp                  # + malformed-Reject cell beside the well-formed ones
└── CMakeLists.txt                           # grouped bucket, label 092
tests/wire/                                  # E-4 iterator cells, E-5 validator cells
tests/core/test_092_error_completeness.cpp   # NEW (+ CMakeLists)
tests/core/test_0{17,19,20}_error_completeness.cpp, tests/capi/{error_surface_test.cpp,expected_error_map.csv,abi_symbol_golden_test.cpp}  # R-7 pins
tests/capi/version_test.cpp                  # 1.10, RED first
tests/capi/<malformed-tag Logon refusal>     # mirrors CapiLogonMalformedCount
tests/fuzz/fuzz_session_recovery_admin_parse.cpp   # R-2 arm
bench/session/scan_frame_header_bench.cpp          # NEW, bench-only commit first (R-9)
bench/session/<inbound on_inbound_frame bench>     # NEW, bench-only commit first (R-9)
bench/ci-suite.txt                                 # both benches listed
spec/behaviors-and-limitations.md                  # B-092-* + C-5 L-rows; #423 row 4 revision; row-1 note → L-004-4 / #514
spec/feature-catalogue.md                          # CA rows for 1.10
brain/components/{session,inbound-message-path,wire,errors,c-api}.md   # R-13
```

**Structure Decision**: The single-project library layout above. All changes stay within the
existing session, wire, core-error and C-ABI modules. There is no new module, target or dependency,
apart from new test sources, two bench sources and the fuzz arm.

## Implementation phases (for `/speckit-tasks`)

0. **Baselines (before any production edit)**:
   - the bench-only commit (both benches), run paired against the merge-base (R-9);
   - the T076 RED reproducer as a real test (expected RED against the new assertions);
   - the parse-arena ceiling measurement on the Linux presets the late-site cells run on (R-4).
0b. **C-ABI 1.10 BREAKING (FR-017)**, in the same PR. The procedure is 091's 1.9 procedure,
   re-derived:
   - **Preconditions** (research R-8):
     - `gh release list --exclude-drafts` is empty;
     - after `git fetch --all --prune`, no ref already defines `FIXPP_C_ABI_VERSION_MINOR 10`
       (positive control: `origin/main` shows 9). Re-check right before the bump commit.
   - **Population**: re-run 091's recipe (R-8) at the implementation head, and classify each effect
     in R-8's table.
   - **Witness first**: the `tests/capi` malformed-tag Logon refusal cells, on every observer and
     both roles. They are RED on the unchanged session.
   - **Version pin first** (`[const §VII.3]`): `version_test.cpp`'s exact-version cell and
     `CompositeMacroValue` are set to 1.10 and shown RED against 9.
   - **Bump**: `FIXPP_C_ABI_VERSION_MINOR` 9 → 10, with a history comment naming 092/fixpp#507, every
     observer, and the effects with no carrying declaration (the D-7 reversal; the LogoutSent
     confirmation, if the recipe keeps it additive).
   - **Declarations**: a BREAKING (C-ABI 1.10; 092) clause on each affected `session.h`
     declaration, and a rewrite of `fixpp_session_register_callback`'s 1.9 "no Reject is sent"
     sentence.
   - **Consumers**: `git grep -ln "VERSION_MINOR\|0x010900\|1_9_0\|(9U << 8U)" -- . ':!specs'`,
     each hit classified.
   - **Freeze manifest**: `tools/check_capi_freeze.sh` re-pin after every header edit, including
     Phase 9's.
   - **Mutant**: MINOR back to 9 must turn the version test RED.
   - **Not touched**: `CHANGELOG.md`, which is not a carrier (R-8), and `introducing_minor`, since
     no code is added.
1. **Scan fault record** (E-0, E-1): RED cells per fault kind, positional bit and `fault_ref_*`, plus
   a duplicate-34 cell proving clean frames stay last-wins, then the scan change. Then the differential corpus with its accepted controls (I-4), each mutation
   family proven able to fail with a seeded disagreement.
2. **Disposition** (R-3, contract C-2): RED cells for every row, then the inline branch and
   `dispose_unparseable_` in the 4 arms, and the NotConnected hoist. T076 goes green.
   Mechanism-deletion proofs per arm, run twice: with the late-site close, and with it deleted too
   (SC-006). The D-5 call passes `hdr.fault_ref_msg_type`, with its duplicate-35 cell. The D-5
   persistence case `092 disposer (D-5)` is added to both #423 tables in
   `tests/session/test_persistent_seqnum_hydrate.cpp`, each shown RED in a scratch copy (quickstart
   §1 "D-5 persistence").
3. **Reject Text and 372 bound** (R-5): `build_reject_with_text`, with `build_reject` delegating
   and its output byte-identical (existing goldens unchanged); the dictionary-derived 372 bound and
   its recomputing test.
4. **Late-site close** (R-4, contract C-6): the "parse failed" results of `parse_and_dispatch_` and
   `validate_inbound_`; `close(close_mode::terminal)` at every late inbound site; one RED cell per
   site, using a real well-formed frame above the measured ceiling (no test hook), which also
   reconnects and pins whether NextNumIn includes the closed-on frame. Also the replay
   guard and its cell (R-12).
5. **Validator fault** (R-7, FR-012): the E-0 enum, the E-4 accessors, the E-5 validator change,
   the E-6 errors, the C mapping and every R-7 pin; the cells.
6. **No-reject-loop supersession**: the `admin_messages.hpp` sentence and the malformed Reject/Logout cells;
   the reject-loop bound cell.
7. **SC-007 scripted peer** (with resend replay) and the C-5 disclosed-outcome cells (L-1, L-2).
8. **Fuzz arm** (R-2), with a planted disagreement proving the trap.
9. **Docs** (R-13): B&L (the B-092-* rows, C-5 L-1, L-2, L-4, L-6, the #423 row 4 revision, and a
   note that its row 1 describes an "Ignore" fixpp does not do, citing `L-004-4` and fixpp#514);
   brain pages (the session page's "garbled" passage rewritten, not appended); code comments with ruling-naming header comments where a decision is superseded
   (parent CLAUDE.md); feature-catalogue CA rows.
10. **Bench re-run** (paired, R-9); `/simplify`; `/speckit-verify`.

## Post-design Constitution re-check

Re-run after the Gate A round 2 artifacts: **PASS**.
- **No allocation and no new concurrency** are added. The clean path gains one inline compare.
- **C-ABI 1.10** is a declared BREAKING pre-release change (`[const §X.7]`), and the Appendix A
  controls are tracked in the Constitution Check.
- **The public C++ changes** are source-compatible: `build_reject_with_text` is a new name, and
  `build_reject` keeps its single declaration (contract C-4).

Items still open, and where they are routed:
- **The §4.5.2 disregard of a Framer failure, a fault-free frame whose third field is not 35, and
  the pre-Active establishment timeout**: fixpp#514 (owner ruling O-1).
- **The disposition of a resource failure of a well-formed frame beyond fail-closed**: fixpp#515
  (owner ruling O-2).
- **The two Length+Data readers stay duplicated.** Their agreement is pinned by instruments (R-2).
  Unifying them is a bench-sensitive wire-hot-path change, out of scope.

## Complexity Tracking

No constitution violations to justify. The §X.7 BREAKING declaration is the constitution's own
pre-release procedure, owner-ruled.

## Gate A

- Round 1 applied 2026-09-27: Codex P1=5 P2=6 P3=2; Opus post-judging P1=3 P2=4 P3=13; rewrite addresses root causes #1–#5; owner rulings: C-ABI 1.10 BREAKING, FR-012 kept and fully specified. Reviews: research/reviews/codex_092-garbled-frame-reject_gate_a_review.md, research/reviews/opus_092-garbled-frame-reject_gate_a_adversarial_review.md.
- Round 2 applied 2026-09-27: Codex P1=5 P2=2 P3=1; Opus post-judging P1=2 P2=1 P3=1; rewrite addresses root causes A, B, C and R2-008; owner rulings O-1 (framing disregard → #514, 092 keeps L-004-4) and O-2 (resource case → #515, late sites fail-closed). Reviews: research/reviews/codex_092-garbled-frame-reject_gate_a_2_review.md, research/reviews/opus_092-garbled-frame-reject_gate_a_2_adversarial_review.md.
- Round 3 exhausted 2026-09-27: Codex P1=0 P2=3 P3=3; Opus post-judging P1=0 P2=2 P3=4. Owner chose "re-run /clarify then /plan": spec.md Clarifications "Session 2026-09-27 (after Gate A round 3)" answered R3-001..R3-005; this plan refresh applies them plus R3-006. Reviews: research/reviews/codex_092-garbled-frame-reject_gate_a_3_review.md, research/reviews/opus_092-garbled-frame-reject_gate_a_3_adversarial_review.md. Next: a fresh /gate-a loop.

### Round 1 — how each root cause was addressed

*Historical record. Round 2 reverted or deleted several items below: global first-wins, contract
C-2b and its residual reason 3, the knob-off GapFill deviation, and C-5 L-3 and L-5. See "Round 2"
below.*

- **#1 Positional header identification** (A1, A2, NEW-P3-A):
  - `msg_type_is_third`, first-wins 34/35, and "34 read" = `parse_seqnum > 0` (E-1, R-1);
  - D-8 is now "field 3 not 35 → disregard" in LogonReceived/Active. Before Active such a frame is
    refused, because no pre-Active timer would end a disregarded connection (contract C-2);
  - FR-006 and FR-008 are rewritten to match R-11;
  - I-3 carries the duplicate-34/35 carve-out, and I-4 asserts the selected values.
- **#2 One meaning of "parse failure"** (A3, NEW-P2-A, NEW-P2-B, NEW-P2-C):
  - FR-016 and contract C-2b, with a per-site table derived by grep, a `check_inbound`-already-ran
    column, and terminal handling;
  - residual reason 3, not 99 (valid on FIX.4.2);
  - `validate_inbound_`'s `nullopt` on a parse failure now follows the rows;
  - non-parsing paths are marked I-4-only and accepted, with disclosure C-5 L-3;
  - the arena ceiling is measured, not derived;
  - the knob-off GapFill deviation is decided and recorded (R-4).
- **#3 Declared changes** (NEW-P1-A, A5, NEW-P2-H):
  - C-ABI 1.10 BREAKING (FR-017, R-8, Phase 0b);
  - the no-reject-loop supersession for faulty 3/5 in the spec, contract C-2 and `admin_messages.hpp`, with a
    bounded-loop cell;
  - A-5 narrowed, with C-5 L-1/L-2 as B&L rows with cells;
  - the "What changes for whom" table above.
- **#4 Instruments that can go RED** (A7, A10, A11, NEW-P2-F, NEW-P2-G):
  - exact 373/371 asserted in every Reject cell;
  - a malformed *tag* for D-1/D-2 and a *Logout* for D-9;
  - clean-seed preconditions, accepted controls and per-family planted disagreements in the corpus;
  - a fuzz arm that skips only named resource statuses;
  - a resend-replaying SC-007 peer;
  - deletion proofs with and without the residual path;
  - the paired merge-base bench including `on_inbound_frame`;
  - LogonReceived cells separate from Active.
- **#5 FR-012** (A8, A9): kept and fully specified per the owner ruling.
  - E-0 shared enum;
  - E-4's S0–S4/T1–T3 enumerated from `advance()` with the yield unchanged;
  - the E-5 check before each field and after the loop;
  - E-6 slots 132/133 with the C mapping;
  - R-7's derived pin population and the new exact-set test.
- **Line-level findings.**
  - A4: C-1 step 1b.
  - A6: R-12's argument plus one cell.
  - A12: the checklist.
  - A13: the Terminology section; "garbled" reserved.
  - NEW-P2-D: no log; `fault_offset` kept as an instrument-only member.
  - NEW-P2-E: the inline branch (E-2, R-3).
  - NEW-P3-B: FR-018 liveness.
  - NEW-P3-C: silent Disconnected named per row.
  - NEW-P3-D: the NotConnected hoist, with its cost stated.
  - NEW-P3-E: R-9 premise corrected; ci-suite; fixed Text; FR order.
  - NEW-P3-F: C-5 L-4.
  - NEW-P3-G: the 372 bound and the fixed Text (R-5).
  - NEW-P3-H: R-10 derived.
  - NEW-P3-I: FR-001 scoped to the Session.
  - NEW-P3-J: FR-011 and I-6 name the knob.

### Round 1 — disagreements

*Historical record; G092-A2's first-wins was reverted in round 2.*

The Opus review marks no finding `Disagree`, so no Codex fix was refused outright. Where Opus's fix
shape was applied **instead of** Codex's counter-proposal, the difference is recorded here:

- **G092-A4 (MaxMessageSize).** Codex: move the guard after the disposition, or get an owner
  ruling. Applied instead: Opus's P3 fix. The guard stays first and is named as C-1 step 1b and in
  FR-002. It reads only the Framer-validated frame length and no field, so FR-001 is not violated,
  and size-first precedence needs no ruling.
- **G092-A2 (duplicate 34/35).** Codex's P1 is downgraded to P2 by Opus: pre-existing, and no trust
  boundary. First-wins is adopted (with I-3's carve-out). Codex's "explicit presence bits rather
  than `string_view::empty()`" is not adopted as such: presence is `parse_seqnum > 0` for 34 and the
  positional bit for 35.
- **G092-A6 (non-inbound scan callers).** Codex: separate inbound scanning from
  masking/replay classification, or prove and test. Applied: the proof (R-12) plus one cell, not a
  separate scanner (Opus P3).
- **G092-A8 (iterator).** Codex: align every iterator branch with the `OffsetTable` boundary.
  Applied instead: report T1–T3 as faults **without changing what is yielded**, because
  `scan_slice_for_tag` (C ABI) walks group slices with the iterator (R-7).
- **G092-A9 (errors).** Codex's mapping to `FIXPP_ERR_WIRE_INVALID_FRAME` is adopted. Its
  conclusion that "no C-ABI version bump is required" is overridden by the owner's 1.10 BREAKING
  ruling, which rests on the Logon refusal (NEW-P1-A), not on the error mapping.
- **G092-A5 (malformed Reject).** Codex offered two options. The owner ruling's rows apply to every
  MsgType, so the supersession branch is taken. Opus corrected Codex's evidence:
  `NoRejectLoopOnInboundReject` feeds a well-formed Reject.

### Round 2 — how each root cause was addressed

- **A — §4.5.2 handling described from the ruling, not the source** (R2-001, R2-002, NEW-R2-A),
  with owner ruling O-1:
  - FR-008, C-1 step 1, the Terminology, the Edge case, the data-model preamble, the contract
    definitions and Constitution Check VII §5 now say a Framer failure is session-fatal (`L-004-4`);
    its disregard is fixpp#514;
  - D-1/D-2 are grounded in the pre-Active arms' existing refusal of every non-Logon input (contract
    C-2 cites the source), with the pre-Active disregard deferred to #514; D-8 is unchanged;
  - the B&L docs task gains the row-1 note.
- **B — late parse failures detected after guards and handlers acted** (R2-003, R2-004, R2-007,
  NEW-R2-B, NEW-R2-C), with owner ruling O-2. Deleted: C-2b's per-site table and its
  `persist_inbound_advance_` choreography, the knob-off GapFill +1 deviation, C-5 L-3, FR-011's
  exception, the 373=3 residual Reject and its Text, and the Framer re-feed cause. Added: contract
  C-6, one fail-closed action at every late inbound site, with the population named by a command,
  the effects it cannot undo stated (including the Logout reply), and the cost disclosed as C-5 L-6
  and classified in R-8. The field cap K is not added; it is #515's.
- **C — scan semantics widened beyond need** (R2-005, R2-006): global first-wins reverted; the fault
  record gains `fault_ref_seq_num` and `fault_ref_msg_type`, read only on a faulty frame; I-3's
  carve-out, C-5 L-5 and R-8's first-wins row deleted; I-4 compares against `entries()`; replay
  gap-fills a stored frame with no 35, with an admin-frame cell (R-12).
- **R2-008**: E-6's layer-independence sentence deleted; an over-0xFFFF mapping cell added.

### Round 2 — disagreements

The Opus review marks no round-2 finding `Disagree`. Where a Codex counter-proposal was not taken,
the reason is recorded here:

- **G092-R2-001** (implement a bounded resync, or disclose the fatal deviation): the disclosure
  branch is taken; the resync is fixpp#514 by owner ruling O-1.
- **G092-R2-002** (evaluate criterion 3 before D-1/D-2 and disregard pre-Active): not taken. Owner
  ruling O-1 keeps today's pre-Active refusal and defers the disregard, with the establishment
  timeout it needs, to #514.
- **G092-R2-003** (a synchronous parseability preflight before the guards): not taken. Owner ruling
  O-2 moves the resource case to #515; within 092 every late site closes terminally.
- **G092-R2-004** (move the GapFill check before `check_inbound`, or roll back transactionally): moot.
  The knob-off +1 row is deleted, and a late failure closes the session.
- **G092-R2-005** (an overlay-probe-exhaustion cell, or surface overlay exhaustion as a status): not
  taken. I-4 compares against `entries()`, which is exact, so no `find` equivalence is claimed.
- **G092-R2-006** (a tolerant replay-specific scan, or validating stored frames through the replay
  builder): not taken. A stored frame with no 35 is gap-filled, the fail-safe outcome, with an
  admin-frame cell.
- **G092-R2-007** (a profile-aware fallback reason): moot. The residual Reject is deleted.
- **G092-R2-008** (preserve the overflow-vs-format distinction in `field_fault`): not taken. The
  claim is narrowed instead and the differing C codes are pinned by a cell; the validator has no C
  caller.
