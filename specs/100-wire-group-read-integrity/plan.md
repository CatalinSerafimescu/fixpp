# Implementation Plan: Wire group-read integrity

**Branch**: `100-wire-group-read-integrity` | **Date**: 2026-11-07 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/100-wire-group-read-integrity/spec.md`

## Summary

Four inbound read-path defects (B81: fixpp#645, #644, #647; B42: #541) end in a present field or group read as absent or
empty, or in a `std::bad_alloc` leaving `extern "C"`. The plan gives each one observable ending (research D1–D11):

- the index build's probe bound becomes a per-build budget, and exceeding it fails the build (`wire_offset_table_full`);
- "is this tag a group?" becomes one registration predicate keyed as QuickFIX keys its group lookups (D2), asked
  before position, value, the next field or an allocation's outcome, and carried as `is_group` on the read result (D3);
- the read result carries a status (`ok`, `alloc_failed`, `limit_exceeded`) on both read paths, the typed top-level
  read passes it, and `group_view` gains `read_failed()` (D5, D6);
- the group cache records only completed reads (D10), and a failed nested read always means a present, registered tag
  (D11);
- both C-ABI getters catch the cursor-shell allocation and answer from one ordered table (contract
  `contracts/capi-group-getters.md`).

C-ABI 1.15, BREAKING (`[const §X.7]`), covering the two getters and the session observers' 1.11 "always parses"
clause; no code, symbol or struct added to the C ABI. Two findings beyond the four issues, folded in: a top-level group
read stopped by `wire_group_too_large` (D5) and a nested sub-table over its entry cap (D7) are both read as empty today.

## Technical Context

**Language/Version**: C++23 (clang, gcc, MSVC per Article II).

**Primary Dependencies**: none new.

**Storage**: N/A.

**Testing**: GoogleTest; RED-first per issue, a mutation per production change (research D9).

**Target Platform**: Linux (clang, gcc) and Windows (MSVC).

**Project Type**: C++ library: wire codec, dictionary view, C ABI.

**Performance Goals**: no measurable change on the read path. D2 adds one registration-callback call per group read
(a bit test, then one context-store probe); D1 adds a counter to the build loop; D3/D5 add by-value bytes; D11's scan
runs only on a failed nested read.

**Constraints**: zero allocation per message on the inbound path (`[const §XV.1]`); every touched wire function stays
`noexcept`, and the new callback and accessor are `noexcept`; result structs stay trivially copyable; `group_span` stays
within its asserted 16 bytes. `sizeof(dict_hooks)` and `sizeof(OffsetTable)` grow by one function pointer (D2); the
arena's size and upstream do not change (073), and the census in research.md re-checks every band tuned to the old size.

**Scale/Scope**: production `src/wire/offset_table.cpp`, `include/fixpp/wire/offset_table.hpp`,
`include/fixpp/wire/dict_hooks.hpp`, `include/fixpp/wire/parser.hpp` (`for_table_view`, `MessageView::group<>()`),
`include/fixpp/wire/group_view.hpp`, `include/fixpp/dict/table_view.hpp`, `src/capi/message_read.cpp`,
`include/fix/c_api/message.h`, `include/fix/c_api/session.h` (doc), `include/fix/c_api/version.h`,
`tools/codegen/fixpp-codegen/emit_messages.cpp` (one token in the nested accessor, D5), and the checked-in goldens it
regenerates (research census). Tests in `tests/wire/`, `tests/capi/`, `tests/session/`, `tests/codegen/`,
`tests/dictionary/`. Records: see Phase 6.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Article | Gate | Disposition |
|---|---|---|
| I / VI — scope, spec coverage | core tag-value FIX | PASS. #647 brings the C-ABI read in line with FIX's zero count and TC-018 (ruling note, parent repo). |
| VII — testing | RED first, mutation-proven; `ctest -L` selection (§VII.8) | PASS. Each cell RED on `fa778a8e` where behaviour differs, each production change mutated (D9); quickstart selects by label and by `--gtest_filter` with an executed-count floor. |
| VIII — performance | no regression | PASS by construction (Technical Context); the read path's bench set runs at verify. |
| IX — coverage, sanitizers, static | touched modules ≥ 95/85 or per-line assessment | PASS planned: `/speckit-verify`; ASan on the cursor-shell path. |
| **X — ABI** | §X.7: a breaking change before the first release bumps MINOR, declared BREAKING on each affected declaration, every in-repository consumer updated | **1.15 BREAKING** (D8): getters and session observers; freeze manifest and version test updated; Python consumes only the version macros (research census). |
| XI — concurrency | — | N/A, no concurrency surface. The process-global seed hook is restored by RAII in every cell that sets it (D9). |
| **XV.1** — no per-message heap | inbound path | PASS: no new allocation; the cursor shell keeps its arena allocation, now caught; D10 allocates less than today; D11's scan allocates nothing; the registration bitset is built with the dictionary view, never on the read path. |
| XVII — gates | Gate A on public C-ABI behaviour | Gate A required: §XVII.1's public-API trigger fires. |

**Initial gate: PASS.** **Post-design re-check: PASS.**

## Project Structure

### Documentation (this feature)

```text
specs/100-wire-group-read-integrity/
├── plan.md
├── research.md          # survey, census, decisions D1–D11
├── data-model.md        # result structs, status, cache row, registration
├── quickstart.md        # targeted runs
├── contracts/
│   └── capi-group-getters.md   # C-ABI 1.15 endings, typed endings, parse ending
├── checklists/
│   └── requirements.md
└── tasks.md             # /speckit-tasks
```

### Source Code (library root)

```text
include/fixpp/dict/table_view.hpp     # D2 context-record bitset + registration accessor
include/fixpp/wire/dict_hooks.hpp     # D2 group_registered_fn
include/fixpp/wire/offset_table.hpp   # D3 is_group; D5 group_read_status; group_span: + is_group
src/wire/offset_table.cpp             # D1 per-build budget; D2 group() order; D5/D7 statuses;
                                      # D10 cache rule; D11 nested registration, then presence
include/fixpp/wire/parser.hpp         # D2 for_table_view fills the callback; D6 MessageView::group<>()
include/fixpp/wire/group_view.hpp     # D5 read_failed(); status ctors beside the kept bool ctors
tools/codegen/fixpp-codegen/emit_messages.cpp  # D5 nested accessor passes r.status
src/capi/message_read.cpp             # D4 both getters; catch around the cursor shell; nested row 0
include/fix/c_api/message.h           # D8 CA-010 table, per-function codes, steady-state note
include/fix/c_api/session.h           # D8 1.11 "always parses" clauses amended (1.15)
include/fix/c_api/version.h           # D8 1.15 BREAKING; 1.11 entry amended
tools/capi_freeze.sha256              # D8 refresh
tests/wire/  tests/dictionary/        # D1, D2, D3, D5, D7, D10, D11 cells; flipped hostile-input cell
tests/capi/                           # D4 table cells; b28-repro port; flipped Q33 and version cells
tests/session/                        # D1 inbound ending
tests/codegen/                        # D5 pinned accessor literal
specs/003-dictionary-codegen/contracts/golden/       # D5 regenerated by the emitter, never hand-edited
specs/076-fix-latest-typed-codegen/contracts/golden/ # D5 likewise
```

**Structure Decision**: existing layout; no new module or target, except new test files registered in the existing test
binaries.

## Phases (for /speckit-tasks)

1. **RED** — one cell per issue and per D2/D5/D7/D10/D11 delta on today's tree, plus controls; record each RED reason
   and the lanes that show RED or skip.
2. **Dictionary + wire** — D2 (bitset set in all three `_ctx` setters, the three-arm accessor, callback, `group()`
   order), D1, D3, D5, D7, D10, D11 in `table_view` / `dict_hooks` / `offset_table`; the size census (research.md)
   re-checked, each `dict_hooks_test_access::make` caller given its registration stub. In the same change as D1's
   comment deletion: the "Updated (100-wire-group-read-integrity)" notes in `.specify/2b-wire.md` (research census
   recipe), and deletion of `for_table_view`'s refuted "unreachable HERE" membership-lambda comment, not restated
   (research D2). Wire cells GREEN.
3. **Typed** — D5's `group_view` status constructors and emitter change, D6; regenerate, including the 003 and 076
   goldens (research census), and run `ctest -L codegen`; codegen pin updated; typed cells GREEN.
4. **C ABI** — D4, D8; C-ABI cells GREEN; flipped cells rewritten with the superseding requirement named.
5. **Mutations** — each production change reverted alone, its cell RED.
6. **Records** —
   - B&L: L-458-1, L-073-1, L-051-3 closed; L-093-17 retired; L-093-5, L-065-2 amended; B-093-6, B-092-6, L-092-6
     amended in place for the crafted-frame case; a B-row for the 1.15 endings marked BREAKING; a B-row for the body
     group read in a context its dictionary does not declare (`TYPE_MISMATCH`, was a group through the bare fallback;
     QuickFIX `getGroup`), marked BREAKING; and the C++ source change of D5 (the result structs' `alloc_failed` field
     renamed to `status`).
   - 093 contract C-3 I-1, I-4 and I-5 (the "C cursor shells are the exception" clause) amended in place, naming 100.
   - `.specify/2i-capi.md`: §4.8 / CA-010 endings; §5.2, §5.4 and §6.2's steady-state exception text for the two
     getters.
   - `brain/components/wire.md` (its L-458-1 paragraph and "Every admitted frame parses" section) and
     `brain/components/c-api.md`: name 100 as superseding them.
   - `brain/components/wire.md`: flag `specs/004-wire-codec/contracts/group_view.hpp` and
     `specs/073-nested-read-arena-failloud/contracts/nested_slices_result.md` (which defines `bool alloc_failed` and
     the mode semantics) as superseded by 100, in place. Neither shipped bundle is edited.
   - Catalogue and coverage-index entries for the delivering witnesses; C-ABI history comment.
7. **Verify** — targeted runs per quickstart, ASan/UBSan on the C-ABI targets, MSVC build of the touched targets,
   `/speckit-verify`.

## Complexity Tracking

| Item | Why | Simpler alternative rejected because |
|---|---|---|
| D2 adds a `table_view` bitset and a `dict_hooks` callback | no existing accessor answers "context key here, header group at the root, or no context key anywhere" in O(1); `group_bit` is set by both setter families | the delimiter oracle's bare fallback fabricates cross-context groups (802 at the root); exact-only makes 099's NoHops not a group |
| D5 replaces `alloc_failed` with a status, changes one emitter token | typed nested readers must see a limit failure without `alloc_failed()` lying | widening `alloc_failed()`'s meaning makes a public accessor lie; two flags must be OR'd by every reader |
| D1 changes the probe bound's unit | the per-insert cap is reached by non-crafted frames (measured) | the per-insert cap with a failing ending closes sessions on legitimate traffic, repeatedly while the seed lives |
| D11 asks registration, then scans the instance, on a failed nested read | 073 FR-007: failure only for a present group; one ending for a present scalar | a contract note "exhausted + absent = `WIRE_LIMIT_EXCEEDED`" writes a 073 violation into the contract and depends on residual bytes |
| FR-003 changes the nested getter's scalar answer | one rule for both getters (B42's "consistent across both getters") | keeping `OK`/0 leaves a scalar indistinguishable from a zero-count group on the nested getter |

## Gate A

Open items carried into Gate A round 1, with their dispositions:

1. D2's oracle → the registration predicate (context record, or none anywhere plus bare registration); neither the
   delimiter oracle with its fallback nor `group_first_field_exact` alone.
2. D5's scope → approved; `read_failed()` beside an unchanged `alloc_failed()`, via a status enum.
3. D8 → 1.15 BREAKING confirmed; population widened to the session observers.
4. D9 #645 inbound cell → required: a real session cell with the window-cluster frame under the seed hook.

- Round 1 applied 2026-11-07: Codex P1=4 P2=6 P3=2; Opus post-judging P1=5 P2=10 P3=6; rewrite addresses root causes
  #1 (group-ness as one context-exact predicate: D2, D3, contract rows 3–4, FR-001–FR-003), #2 (cache records only
  completed reads: D10, data-model `group_span`), #3 (nested presence decided before failure is reported: D11, one
  ordered contract table, FR-004), #4 (per-build probe budget: D1, FR-008, D8's widened population), plus Codex #6
  (D5 status + `read_failed()`), #7 (D7/D5 nested witnesses, D9), #8 (census in research.md), #9 (quickstart), #10
  (Phase 6 records), #11 (nested row 0), and Opus's new P3s (lanes and typed witness in SC-001/D9, Python wording,
  count-last condition, refuted comments in D1, seed RAII in D9). Reviews:
  research/reviews/codex_100-wire-group-read-integrity_gate_a_review.md,
  research/reviews/opus_100-wire-group-read-integrity_gate_a_adversarial_review.md.
- Round 2 applied 2026-11-07: Codex P1=1 P2=6 P3=1; Opus post-judging P1=1 P2=3 P3=8; rewrite addresses Codex #1
  (registration before presence at both nested exits, with the call's `ctx` and the caller's `hooks`; invariant
  `status != ok ⇒ present ∧ registered`; contract row 2: D11, data-model `nested_slices_result`), Opus's new P2
  (undeclared contexts: D2's header arm, the declared body-group delta with its B&L row and US1.9 witness, the D9
  sweep's undeclared-MsgType arm and its header-arm mutant, the trailer and nested-header conditions), Codex #6
  (repeat-read twins and the `is_group = false` mutant: D9, US1.11), Codex #7 (nested `wire_group_too_large`
  defensive; the nested limit witness moved to D7 with two constructions: D5, D7, FR-011, contract row 2), and the
  P3s: Codex #2 (callback decided, holder census completed with a recipe that reports `Parser`'s member, test-seam
  stubs, FR-009 qualified), #3 (`bool` constructors kept, status constructors added, D5's rationale corrected, field
  rename declared), #4 (003 and 076 goldens in the census and SC-003; 004 and 073 contracts flagged in the brain at
  Phase 6), #5 (2b-wire.md "Updated" notes, Phase 2), #8 (push before allocate, pop on failure: D10, FR-007), and
  Opus's new P3s (bitset set where a context key is created; FR-007 scoped to `group_index_` plus the nested
  re-resolve condition; the typed table's zero-instance row). Reviews:
  research/reviews/codex_100-wire-group-read-integrity_gate_a_2_review.md,
  research/reviews/opus_100-wire-group-read-integrity_gate_a_2_adversarial_review.md.
- Round 2 rewriter notes: (1) Codex #8's push-before-allocate rule was extended to the nested cache (its row is pushed
  before the sub-table is built, research D10), because FR-007's cursor-shell clause is false without it; neither
  reviewer saw this extension. (2) Facts stated twice were given one home: the predicate's arms in research D2, the
  failure invariants and `is_group` in data-model `group_slices_result` / `nested_slices_result`, the cache rule in
  research D10.

### Round 1 — disagreements

The first three entries are findings the Opus review marks "Disagree". The rest are confirmed findings whose Codex fix
shape was not applied, recorded so a later round does not re-raise them.

- **Codex #8, sub-point "`MessageReadGroup.NonGroupTagReturnsTypeMismatch` flips".** Not applied. The root table is
  built over the whole frame, so 49 is followed by CheckSum(10), not last; `group()` answers not-a-group and the cell
  stays `TYPE_MISMATCH`. It stays a control (research census).
- **Codex #8, sub-point "a Python group witness is owed".** Not applied. `bindings/python/fixpp.i` exposes no group
  getter; `version.h` reaches Python only as macros checked by name. §X.7's consumer update is the SWIG regeneration.
- **Codex #12 (references to a nonexistent `tasks.md`).** Not applied. Gate A runs before `/speckit-tasks`; the
  references are forward references the next pipeline step resolves, and the plan tree annotates `tasks.md` as
  `# /speckit-tasks`.
- **Codex #2's fix shape** (exact registration with a fallback only when the table has no contextual group records;
  the same rule applied to the validator). Finding applied, fix replaced by Opus's predicate: Codex's rule makes 099's
  NoHops not a group on route tables, and widening the validator changes session acceptance (FR-009, out of scope).
- **Codex #4's fix shape** (separate tables; "nested exhausted + absent = `WIRE_LIMIT_EXCEEDED`" stated as normative).
  Finding applied at P2, fix replaced by D11: Codex's note would write a violation of 073 FR-007 into the contract.
- **Codex #5's fix shape** (a strict-zero count parser). Finding applied, fix moot: under D2 the count's value never
  decides group-ness, so no zero parser exists.
- **Codex #1's fix shape** (a five-state internal result). Finding applied; the states fall out of `is_group` plus the
  read status plus the slice count, so no separate enum is introduced.

### Round 2 — disagreements

The first entry is the fix shape Opus marks "not the smaller design". The rest are confirmed or narrowed findings whose
Codex fix shape was not applied as written.

- **Codex #2's counter-proposal** (fold registration into `group_delim_fn`'s result, keep `sizeof(dict_hooks)` and
  `sizeof(OffsetTable)` unchanged, add a near-cap boundary test). Not applied. Three shipped fixtures pass a null
  delimiter callback with a non-null membership callback through `dict_hooks_test_access::make` (research census);
  under the fold a null delimiter means "no registration answer", so they would decline group reads and their pinned
  outcomes would flip. The FR-009 violation is not upheld: the per-message exhaustion point is not an observable
  contract (the bands tuned to it say in their headers that they do not port across toolchains), and 093 E-2's
  `kCallbackReadHeadroom` condition compares room left after a parse with the arena's whole size, which one function
  pointer per sub-table does not falsify. The census gap was applied.
- **Codex #3 at P2.** Narrowed to P3 and applied at that size. Article X governs the C ABI; before the first public
  release no article covers C++ source compatibility, so the field rename is declared in the B&L delta, not versioned.
- **Codex #4's fix shape for the 004 contract** (add it to the implementation inventory and edit it) and its mutant (one
  emitted accessor left on `alloc_failed`). Not applied. Shipped feature bundles are history; `brain/components/wire.md`
  flags them in place (Phase 6). The mutant is a compile error once the field is renamed, so it is already loud.
- **Codex #5's scope** (amend 2b-wire.md for the registration predicate, the cache, `group_read_status` and
  `read_failed()`). Narrowed: 2b-wire.md never described those at this grain (its §4.7 shows only `size()`,
  `operator[]` and `iter()`), so only the sentences D1 falsifies get notes (research census recipe).
- **Codex #8's reserve/commit alternative.** Not applied: push-then-pop gives the same guarantee without reserve state
  (research D10).
