---
description: "Task list — 004-wire-codec (Wire Codec: Framer / Parser / OffsetTable / Writer / Validator)"
---

# Tasks: Wire Codec — Framer, Parser, Offset Table, Writer, Validator

**Input**: Design documents from `specs/004-wire-codec/` (library submodule)
**Prerequisites**: plan.md, spec.md, research.md (D-1..D-17), data-model.md (E1..E10), contracts/ (10 shape oracles)
**Design anchor**: `.specify/2b-wire.md` v0.2 (Gate A r1 converged) — on conflict the design doc wins.

**Tests**: REQUIRED — TDD red-green-refactor per `[const §VII.1]`/`[const §VII.3]`; `[2b §9]` seams #1..#14 + cutover reconciliation are bound to explicitly named files (plan.md Test-seam mapping, no globs). Test tasks precede implementation within each story and MUST FAIL (red) before the implementation task makes them GREEN.

**Organization**: Grouped by user story (US1 P1 → US2 P2 → US3 P2 → US4 P3). All paths are relative to the library submodule root `research/G19-fix-fpml-iso20022/library/`.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: parallelizable (different files, no dependency on an incomplete task)
- **[Story]**: US1/US2/US3/US4 (Setup/Foundational/Polish carry no story label)

## Remediation decisions (post-`/analyze` 2026-05-16 — applied)

The `/speckit-analyze` cross-artifact pass produced 7 findings; all are resolved here:

- **I1 (HIGH) — US1 MVP independent-testability vs `frame_view` producer.** Decision: the `framer.hpp` **surface declaration** (`frame_view : View`, `Framer`, `pmr_carry_buffer`) is **relocated from US1 into Foundational** (T006) — it depends only on `View` and is already `#include`d by `parser.hpp`; only the framing *algorithm* (`src/wire/framer.cpp`) stays in US3. A test-only friended `tests/support/frame_view_factory.hpp` (T008) lets US1 parser tests mint a verified `frame_view` from a buffer without the US3 algorithm. US1 is now genuinely independently testable and shippable as the MVP.
- **I2 (MED) — conformance-driver cross-story coupling.** Decision: the seam #2 parameterized driver `conformance_test.cpp` scaffold is **moved to Foundational (T009) as shared infra**; each story task only adds its W-row corpora referencing the shared driver (documented dependency, not a story-independence defect).
- **C1 (MED) — FR-013 throwing-trait trap untasked.** Decision: explicit US1 test `tests/wire/noexcept_trap_test.cpp` (T020) added, and a `core::detail::trap_throw` boundary clause added to the parser/writer/validator impl tasks (T024/T035/T046).
- **U1 (LOW) — FR-006 general trait-decode boundary.** Decision: clause added to T024 (`parser.hpp`) making the 2a/2c `field_traits` decode/encode boundary explicit for all W-009 types (wire does not decode).
- **U2 (LOW) — FR-017 tests-only CheckSum hook.** Decision: clause added to T039 noting the permitted tests-only bypass hook so seam #12 can deterministically inject corrupt frames.
- **D1 (LOW) — `core::error` additions' ABI-audit deferral.** Decision: **accepted, no spec/tasks structural change** — `core::error` is C++ (not C-ABI `fixpp_error_t`); the `tools/abi_history` + `FIXPP_ERR_WIRE_*` coalescing deferral to 2i is a recorded waiver (plan.md:77 / D-13). Action recorded on T003: confirm the deferral wording matches the 002/003 time-bounded waiver shape at `/implement`.
- **G1 (LOW) — local-build approval gate.** Decision: **accepted, no structural change** — the GREEN/Tier-1 tasks (T031/T036/T041/T047/T055) imply local Conan/CMake builds; recorded in Notes that the `/implement` executor MUST surface the `[const §XVII.7]` `AskUserQuestion` build-approval prompt before running them (consistent with project memory `feedback_self_run_build_gate`).

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Module skeleton and build wiring so every later target compiles and `ctest -R '^wire_'` resolves.

- [X] T001 Create the wire-module file skeleton under `include/fixpp/wire/` and `src/wire/` (empty SPDX-headed `AGPL-3.0-or-later` headers/sources per plan.md Project Structure) and author `src/wire/CMakeLists.txt` defining the `fixpp_wire` target (header-mostly: `view`/`parser`/`field_view`/`group_view`/`unknown_fields` header-only per OSS-006; out-of-line `framer.cpp`/`offset_table.cpp`/`validator.cpp`/`writer.cpp`) linking `fixpp::core` + the `dict::table_view` value contract — **no new Conan row** (`[const §III.2]`).
- [X] T002 [P] Author `tests/wire/CMakeLists.txt`, `bench/wire/` and `tests/fuzz/` build wiring registering every wire test under the `wire_` CTest name prefix (so `ctest --preset linux-clang-debug -R '^wire_'` selects the full seam set per quickstart §2), with ASan/UBSan/TSan/coverage preset hooks per quickstart §3.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The error vocabulary, the `View` flyweight base, the `framer.hpp` type surface, the shared conformance driver, and test doubles every primitive and every story depends on.

**⚠️ CRITICAL**: No user-story work can begin until this phase is complete — every wire surface returns `core::expected_t<T>` over these error variants, every view type derives from `View`, and US1 parser tests require the `frame_view` type + the test factory delivered here (resolves analyze finding I1).

- [X] T003 Re-confirm the current max occupied slot in `include/fixpp/core/error.hpp` is 29 (`dict_reify_wire_body_not_ready`), then append the 13 `wire_*` variants at slots 30..42 per `contracts/wire_errors.hpp` / data-model "Error mapping" — non-renumbering (`[const §X.4]`); keep slot-29 and comment-annotate it cutover-obsolete; do NOT re-introduce v0.1's deleted `wire_tag_count_exceeded`. **(D1)** confirm the `tools/abi_history` + `FIXPP_ERR_WIRE_*` coalescing deferral-to-2i wording matches the 002/003 time-bounded waiver shape (recorded; no C-ABI surface added here — D-13).
- [X] T004 [P] Implement `include/fixpp/wire/view.hpp` — the `View` flyweight base + `detail::generation_token` (4-byte POD, `[[no_unique_address]]` + `#ifndef NDEBUG` so release strips it to zero size and stays trivially copyable), `bytes()`/`empty()`/protected ctor/`check_alive()`, all per `contracts/view.hpp`.
- [X] T005 [P] Implement `include/fixpp/wire/errors.hpp` — the `wire_*` `core::error` helper wrappers (`[2b §6.7]`); variants themselves live in `core/error.hpp` (T003).
- [X] T006 Implement `include/fixpp/wire/framer.hpp` — the **surface declarations** `frame_view : public View` (`body()`/`bytes()`), `pmr_carry_buffer`, `Framer`/`Config` per `contracts/framer.hpp` (depends only on `View`/T004; `parser.hpp` `#include`s this). **Relocated to Foundational per analyze I1.** The framing **algorithm** is `src/wire/framer.cpp` in US3/T040.
- [X] T007 [P] Author `tests/support/mock_dict_table.hpp` and `tests/support/mock_validator.hpp` (seam #1) — a `dict::table_view` test specialization and a value-substituted virtual `Validator` for US1/US4 tests.
- [X] T008 [P] Author `tests/support/frame_view_factory.hpp` — a **test-only**, `friend`-of-`frame_view` factory that mints a verified `frame_view` over a caller buffer (computes the `9=…|` SOH and `10=` boundaries) so US1 parser tests can go red→green without the US3 `Framer` algorithm. **New per analyze I1.** Depends on T004, T006.
- [X] T009 [P] Author the seam #2 conformance driver scaffold `tests/wire/conformance/conformance_test.cpp` — parameterized GTest, `[FIX50SP2 §3]` version oracle, CSV-keyed corpus loader; **shared infra, corpora added per story (T011/T032/T037/T042). Moved to Foundational per analyze I2.**
- [X] T010 [P] Author `tests/wire/view_test.cpp` — `static_assert`/runtime checks that release `View` is trivially copyable and `sizeof` excludes the token, debug embeds the token; pins the `[2b §4.1]` base shape.

**Checkpoint**: Error vocabulary + `View` base + `framer.hpp` surface + `frame_view` factory + shared conformance driver + test doubles ready — user stories can begin and US1 is independently testable.

---

## Phase 3: User Story 1 — Parse an inbound frame into a zero-copy indexed view (Priority: P1) 🎯 MVP

**Goal**: A contiguous buffer holding one structurally-valid FIX message parses into a non-owning, offset-table-indexed `MessageView<Mode>` with O(1)-by-tag lookup, every repeated/group occurrence independently addressable, zero heap allocation parse→`fromApp`; and the 2b cutover (the critical-path unblocker) ships green.

**Independent Test**: Using the Foundational `frame_view_factory` (T008), feed a known-good FIX 4.4 / 5.0SP2 buffer; assert each tag resolves to the correct `(offset,length)`, repeated tags and group occurrences are addressable in document order, no heap allocation occurs between parse and simulated `fromApp`, and the previously 2b-gated 001-FLOAT / 003-reify tests are GREEN on the real surface. (No dependency on the US3 `Framer` algorithm — resolves analyze I1.)

### Tests for User Story 1 (write FIRST — must FAIL before implementation) ⚠️

- [X] T011 [P] [US1] Author the parse-domain keyed corpora `tests/wire/conformance/{w001,w002,w003,w006,w007,w008,w009,w012}_*.csv` (W-001 tag=value/SOH, W-002 header, W-003 trailer, W-006/W-007 nested groups, W-008 Length+Data, W-009 field types, W-012 offset index) feeding the shared seam #2 driver (T009).
- [X] T012 [P] [US1] Author `tests/wire/parser_index_test.cpp` — `Parser<Index>::parse` random-access, `get<Tag>`/`get(tag)`, `offsets()`, repeated-tag occurrence addressing, via `frame_view_factory` (red).
- [X] T013 [P] [US1] Author `tests/wire/parser_iter_test.cpp` — `Parser<Iter>::parse_iter` streaming, dict-free `field_iterator`, zero-alloc end-to-end, static `constexpr` Length+Data table (red).
- [X] T014 [P] [US1] Author `tests/wire/offset_table_test.cpp` — `entry` `sizeof==12`/`alignof==4` static_assert, document-order entries, O(1) `find`, DoS caps (`wire_offset_table_full` at 4096 occ, `wire_tag_out_of_range`, `wire_group_too_large`) (red).
- [X] T015 [P] [US1] Author `tests/wire/lifetime_trap_test.cpp` (seam #7) — debug generation-token trap on use-after-buffer-reuse + `-Wdangling` smoke; release strips the token (red).
- [X] T016 [P] [US1] Author `tests/wire/repeating_group_equivalence_test.cpp` (seam #8) — `group_view::iter()` and `operator[]` enumerate identical entries/order incl. nested groups (red).
- [X] T017 [P] [US1] Author `tests/wire/unknown_fields_test.cpp` (seam #9) — dictionary-missing vs dictionary-known-invalid split; no vector materialization; round-trip byte order (red).
- [X] T018 [P] [US1] Author `tests/wire/three_arena_pinning_test.cpp` (seam #13) — per-message arena holds offset table/overlay/sub-indices; framer-carry arena is session-lifetime; zero `new`/`delete` parse→`fromApp` (red).
- [X] T019 [P] [US1] Author `tests/wire/cutover_2b_gated_test.cpp` (SC-006) — 003 `dict::reify` round-trip + 004-authored 001 wire FLOAT accessor (`field_view::bytes()` → `decimal_t::parse(span, mr)`, allocation-free for `pod_decimal`) on the real `MessageView`/`field_view` (red).
- [X] T020 [P] [US1] Author `tests/wire/noexcept_trap_test.cpp` (FR-013, `[arch §5.3]`) — a throwing 2a/2c trait-wrapper **traps** (does not propagate) across the parse→`fromApp` `noexcept` window; debug + release. **New per analyze C1** (red).

### Implementation for User Story 1

- [X] T021 [P] [US1] Implement `include/fixpp/wire/field_view.hpp` — `field_view : public View` with inherited `bytes()`/`empty()` + retained `as_string()` per `contracts/field_view.hpp` (D-16, the cutover-load-bearing shape).
- [X] T022 [US1] Implement `include/fixpp/wire/offset_table.hpp` — `OffsetTable` + `entry` with co-located `static_assert(sizeof(entry)==12 && alignof(entry)==4)`, `find`/`entries`/`size`/lazy `group` surface per `contracts/offset_table.hpp`.
- [X] T023 [US1] Implement `src/wire/offset_table.cpp` (`[2b §6.2]`) — eager `entry[]` build + open-address robin-hood overlay (cap = next-pow2 ≥ 1.25·n), lazy group sub-index on first `group(no_tag)`, all storage from the captured per-message `mr`, DoS caps enforced with bounded memory (`wire_offset_table_full`/`wire_tag_out_of_range`/`wire_group_too_large`). Depends on T022.
- [X] T024 [US1] Implement `include/fixpp/wire/parser.hpp` — header-only template `Parser<access_mode Mode>` + `MessageView<Mode> : public View` + `field_iterator`; Index builds the offset table eagerly, Iter skips it (zero-alloc, dict-free static Length+Data table); mode resolved at compile time, **no runtime branch on the hot path** (`[2b §6.3]`, FR-003). **(U1)** all W-009 field types decode/encode strictly via the 2a `decimal<T>` / 2c `dict::field_traits<...>` boundary — the wire layer performs **no** field decoding (FR-006). **(C1)** every call into a (potentially throwing) 2a/2c trait wrapper is fenced by `core::detail::trap_throw` so no exception escapes the `noexcept` parse→`fromApp` window (FR-013, `[arch §5.3]`). Depends on T021, T022, T023, and the Foundational `framer.hpp` (T006).
- [X] T025 [P] [US1] Implement `include/fixpp/wire/group_view.hpp` — `group_view<GroupT> : View` with `size()`/`operator[]`(lazy sub-index)/`iter()` (streaming opt-out walking raw bytes), nested-group correct (`contracts/group_view.hpp`). Depends on T024.
- [X] T026 [P] [US1] Implement `include/fixpp/wire/unknown_fields.hpp` — `unknown_fields_view : View` yielding dictionary-missing tags only, no vector materialization, in-place two-pointer round-trip merge (`contracts/unknown_fields.hpp`). Depends on T024.
- [X] T027 [US1] Implement the 004-authored 001 wire FLOAT-field accessor leg (D-17, FR-006/`[2b §7.1]`): the path `field_view::bytes()` → `fixpp::decimal_t::parse(span, mr)` through the trait-decode boundary in `include/fixpp/wire/parser.hpp` (net-new wire code — no 001 file to repoint). Depends on T021, T024.

### Cutover — surface migration (D-15/D-16/D-17, FR-018, SC-006)

- [ ] T028 [US1] Surface-migrate `include/fixpp/wire/message_view_contract.hpp` — replace the R6 frozen-thin stub with a thin re-export of the real `parser.hpp` `MessageView` (include path preserved, **surface changed** to `[2b §4.3]` `MessageView<Mode> : public View`). Depends on T024.
- [ ] T029 [US1] Rewire `include/fixpp/dict/reify.hpp` and `include/fixpp/dict/field_traits.hpp` (003, the `arch §2.4` v0.3 bridge surface) onto the real `wire::MessageView<Index>` / `field_view`; the decimal arm reads `fv->bytes()` then `decimal_t::parse(span, mr)` (003 I-1/RC#2). Depends on T024, T027, T028.
- [ ] T030 [US1] Reconcile `tests/codegen/flyweight_shape_test.cpp` (003 seam #18 / I-12) to the migrated `MessageView : public View` surface — retire the stub's own `sizeof(MessageView<Index>)==pointer` `static_assert` (does not survive `: public View`); **preserve** 003's I-1 `sizeof(<Msg>)==sizeof(MessageView<Index> const*)` (a generated message holds a pointer). Depends on T028.
- [ ] T031 [US1] Make all US1 tests GREEN (T011–T020); run the quickstart §9 cutover sanity — `grep -rn 'message_view_contract' include/fixpp/dict tests/` shows only the kept re-export shim and **zero references to the frozen-stub surface** remain anywhere in the tree (SC-006). Depends on T023–T030.

**Checkpoint**: US1 fully functional & **independently testable via the `frame_view_factory`** — real `MessageView`/`OffsetTable`/`field_view`, the 2b cutover green, 001/003 deferred work unblocked. **This is the MVP and the critical-path deliverable.**

---

## Phase 4: User Story 2 — Serialize a message with automatic framing fields (Priority: P2)

**Goal**: Compose a message field-by-field into a caller buffer; at `commit()` auto-compute digit-only `BodyLength(9)` and 3-digit zero-padded byte-sum-mod-256 `CheckSum(10)`; round-trip is byte-identical including opaque unknown fields.

**Independent Test**: Build a message via `Writer`, `commit()`, assert bytes are byte-identical to a known-good golden frame (correct `BodyLength`, 3-digit `CheckSum`); too-small buffer returns a defined error with no OOB write.

### Tests for User Story 2 (write FIRST — must FAIL) ⚠️

- [ ] T032 [P] [US2] Author the serialize-domain keyed corpora `tests/wire/conformance/{w004,w005,w013}_*.csv` (W-004 BodyLength, W-005 CheckSum byte-sum, W-013 serialize) feeding the shared seam #2 driver (T009).
- [ ] T033 [P] [US2] Author `tests/wire/round_trip_property_test.cpp` (seam #3) — 10⁴-sample parse→`reify`→`Writer`→`commit`→re-parse byte-identical incl. unknown/custom fields, all four versions (red).

### Implementation for User Story 2

- [ ] T034 [US2] Implement `include/fixpp/wire/writer.hpp` — `Writer` + `group_writer` surface per `contracts/writer.hpp` (`append_raw`/`append<T>`, `open_group` LIFO, `commit() &&`, `bytes_written`).
- [ ] T035 [US2] Implement `src/wire/writer.cpp` — `append`/`append_raw` via 2a/2c trait `to_chars`, LIFO nested `group_writer` with RAII `close()` sealing `NoXxx`, `commit()` digit-only `BodyLength` (`memmove` backpatch, bounded one frame-sized move, no space padding) + byte-sum-mod-256 `CheckSum` (3 zero-padded ASCII digits); too-small `dst` → defined wire error, no OOB write; zero alloc for group-free messages. **(C1)** throwing trait `to_chars` calls fenced by `core::detail::trap_throw` (FR-013). Depends on T034.
- [ ] T036 [US2] Make US2 tests GREEN (T032–T033) — golden-frame byte-identical + round-trip fidelity incl. opaque unknown-field preservation. Depends on T035, US1 parse path (T024/T031).

**Checkpoint**: US1 + US2 independently functional — inbound parse and outbound serialize with verified round trip.

---

## Phase 5: User Story 3 — Frame a multi-message TCP byte stream (Priority: P2)

**Goal**: `Framer::feed` consumes arbitrary byte chunks, emits zero or more complete frames per feed, carries partial trailing bytes (session-lifetime carry arena) into the next feed, and verifies `BodyLength` + `CheckSum` before any parser sees a frame.

**Independent Test**: Feed a stream split at adversarial boundaries (mid-tag, mid-`BodyLength`, between messages, one byte/feed); assert exactly the complete frames emit in order, partials carry over, each emitted frame is BodyLength/CheckSum-verified; a corrupt or oversized frame is rejected pre-parser.

### Tests for User Story 3 (write FIRST — must FAIL) ⚠️

- [X] T037 [P] [US3] Author the framing-domain keyed corpora `tests/wire/conformance/{w010}_*.csv` (W-010 multi-message framing, OSS-013) feeding the shared seam #2 driver (T009).
- [X] T038 [P] [US3] Author `tests/wire/framer_partial_read_test.cpp` (seam #4) — split at every boundary down to one byte per feed; exactly the original frame sequence reassembles in order, no lost/duplicated frames (red).
- [X] T039 [P] [US3] Author `tests/wire/checksum_bodylength_corruption_test.cpp` (seam #12) — XOR-corrupted CheckSum + space-padded/inconsistent BodyLength rejected with the defined wire error before any parser is exposed; oversized frame → `wire_frame_too_large` no deadlock. **(U2)** drive corruption via the permitted **tests-only** CheckSum-bypass hook (FR-017 — no production bypass; the hook is tests-only) (red).

### Implementation for User Story 3

- [X] T040 [US3] Implement `src/wire/framer.cpp` (`[2b §6.1]`) — multi-message framing, partial-read carry into the session-lifetime `pmr_carry_buffer` (never reallocates; growth past `max_frame_bytes` → `wire_frame_too_large`), mandatory byte-sum-mod-256 CheckSum + BodyLength verification before any frame is exposed (no production bypass, `[2b §2]`; tests-only hook per T039/U2), inter-frame garbage → `wire_framing_resync`. Depends on the Foundational `framer.hpp` surface (T006).
- [X] T041 [US3] Make US3 tests GREEN (T037–T039) — one-byte-per-feed reassembly fidelity; corrupt/oversized frame rejected pre-parser, no deadlock. Depends on T040.

**Checkpoint**: US1 + US2 + US3 independently functional — realistic TCP ingestion with verified framing.

---

## Phase 6: User Story 4 — Validate a parsed message against dictionary metadata (Priority: P3)

**Goal**: A runtime-virtual `Validator` plugin (exactly 5 pure-virtual) with a full per-version `dictionary_driven_validator` default doing required-field + type + enum + repeating-group-structure checks for v42/v44/v50sp2/vt11, holding `dict::table_view` by value (no virtual `wire/`→`dict/` edge), drawing its ≤~600 B working set from a caller-supplied scratch arena.

**Independent Test**: Run the default validator over messages with (a) missing required field, (b) type violation, (c) out-of-range enum, (d) malformed repeating-group count; assert each is reported with the correct error and conforming messages pass — across all four versions, zero false accepts.

### Tests for User Story 4 (write FIRST — must FAIL) ⚠️

- [ ] T042 [P] [US4] Author the validation-domain keyed corpora `tests/wire/conformance/{w014}_*.csv` (W-014) feeding the shared seam #2 driver (T009).
- [ ] T043 [P] [US4] Author `tests/wire/validator_domain_test.cpp` (seam #14) — unconditional validate over every dictionary-known field present (not per-accessor); required/type/enum/group rules; `≤5 pure-virtual` + by-value `dict::table_view` static checks (red).
- [ ] T044 [P] [US4] Author `tests/wire/validator_per_version_test.cpp` (SC-005) — labelled conforming/non-conforming corpus across v42/v44/v50sp2/vt11, zero false accept (red).

### Implementation for User Story 4

- [ ] T045 [US4] Implement `include/fixpp/wire/validator.hpp` — `Validator` interface with **exactly 5** pure-virtual (`validate`, `validate_field`, `required_fields`, `field_valid_for`, `group_first_field`) + `dictionary_driven_validator final` decl per `contracts/validator.hpp` (`[const §XIV.2]` cap satisfied directly).
- [ ] T046 [US4] Implement `src/wire/validator.cpp` (`[2b §6.5]`) — `dictionary_driven_validator` 5 overrides holding `dict::table_view` **by value**, per-version required/type/enum/group, scratch (`seen[]` bitmap + `required_remaining`) ≤~600 B from `scratch_mr` (no `new`/`delete`), and the `[2b §6.5 rule 3]` type-check site that **re-maps** 2a/001 `decimal_precision_loss` onto `wire_field_value_truncated` (slot 41) so Session-Reject carries a `wire_*` code. **(C1)** throwing trait type-check calls fenced by `core::detail::trap_throw` (FR-013). Depends on T045.
- [ ] T047 [US4] Make US4 tests GREEN (T042–T044); assert SC-007 — `Validator` exposes ≤5 pure-virtual and the layering check finds no `wire/`→`dict/` virtual runtime edge. Depends on T046.

**Checkpoint**: All four user stories independently functional.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Performance gates, fuzzing, the in-PR footprint-spike decision artifact, hygiene, and the mandatory verify gate spanning all stories.

- [ ] T048 [P] Author `bench/wire/{framer,parser,offset_table,validator,writer}_bench.cpp` (seam #5) + `bench/baselines/wire/*.json`; enforce the `[2b §6.6]` latency ceilings at ±5% (`[const §VIII.1]`/`[const §VIII.2]`); record parse/sec vs `hffix` into `bench/REPORT.md` (D-14 — measured, not a this-PR blocker).
- [ ] T049 [P] Author `bench/wire/offset_table_footprint_bench.cpp` (seam #6); run the `[arch §11 row 1]`/`[2b §10 Q1]` eager-vs-lazy footprint spike in occurrence space over Logon / NewOrderSingle / NewOrderList×{1,10,100} / MDIR×{10,100,1000} / SecurityList×{1000,3000,5000} (over-4096 corpora with the cap **raised** per D-7); record the table + hybrid-confirmed verdict in `.specify/decisions/004-wire-codec-verify.md` closing `[arch §11 row 1]` (SC-008).
- [ ] T050 [P] Author `tests/fuzz/{fuzz_wire_framer,fuzz_wire_parser,fuzz_wire_validator}.cpp` (seam #11); run the ≥10-min Tier-1 ASan+UBSan campaign — adversarial corpus rejected with the defined wire error, bounded memory, zero crash/OOB (SC-003).
- [ ] T051 Wire the `tools/check_alloc.py` allocation guard under `mallocnesia` (seam #10) into CI — assert **zero** heap allocation between start-of-parse and `fromApp` return on the full parse+serialize path (FR-012, SC-002).
- [ ] T052 [P] Micro-bench the debug `View::check_alive()` generation-counter cost on a 200-tag read loop (D-8); if >2× release, fall back to per-N-access sampling; record in the verify/bench record.
- [ ] T053 [P] clang-tidy + clang-format + cppcheck + IWYU clean on all `include/fixpp/wire/*` + `src/wire/*` (`[const §IX.4]`).
- [ ] T054 [P] `nm` check: wire emits **no** `extern "C"` symbols and no wire type appears in `<fix/c_api.h>` (`[const §X.2]`/`[2b §5]`, FR-014); abidiff explicitly N/A (D-13).
- [ ] T055 Run the Tier-1 serial sanitizer/coverage matrix per quickstart §3 — ASan/UBSan/TSan green, coverage ≥90% line / ≥80% branch on `include/fixpp/wire/*`+`src/wire/*` (`[const §IX.1]`/`[const §IX.2]`), GCC release sanity.
- [ ] T056 Run `/speckit-verify 004-wire-codec` → `.specify/decisions/004-wire-codec-verify.md` must be **GREEN** (the `gate-b-done` precondition, `[const §XVII.8]`); validate quickstart.md end-to-end.
- [ ] T057 **Catalogue + coverage-index update (`[const §VI.4]`/`[const §VI.6]`/`[const §I.3]`).** In `spec/feature-catalogue.md` flip every 004-owned OFFICIAL row to `done` with the 004 feature ref + PR#: W-001..W-008, W-010..W-014 (`backlog`→`done`), W-009 (`in-progress`→`done`, 001 wire-FLOAT deferral now closed by T027), and the inherited OSS rows delivered here per `[2b §11]` — OSS-006 + OSS-008 (`backlog`→`done`). **OSS-013 stays `post-1.0 v1.2` (SBE) — NOT flipped** (004 applies the flyweight *pattern* only; the SBE row is roadmap-deferred per `[const §XVIII.2]`). Add/refresh the matching `spec/coverage-index.md` entries (bidirectional `[FIX50SP2 §3/§3.1/§3.2/§3.3]` ↔ row mapping) **before** the rows are declared done. Per-row evidence (`/specify` artifact, verifying test, Gate A, Gate B) per `[const §VI.6]`.
- [ ] T058 **Feature completeness audit — mandatory `/gate-b` precondition, runs LAST before declaring 004 complete.** Mechanically diff three sets and assert 100% (or an explicit, recorded waiver per item): (a) every `tasks.md` row is `[X]` (or waived with rationale); (b) every spec FR-001..FR-018 + SC-001..SC-008 maps to a landed test **and** implementation (the `/analyze` coverage table re-run against the merged tree, not the plan); (c) every 004-owned catalogue row (W-001..W-014, OSS-006, OSS-008) is `done` in `spec/feature-catalogue.md` with a `coverage-index.md` entry (T057). Write the result into `.specify/decisions/004-wire-codec-verify.md` (or a sibling `-completeness.md`); a non-100%, non-waived result **blocks `/gate-b`** exactly like a `RED` verify verdict (`[const §XVII.8]` `/gate-b`-precondition shape). This closes the 001-class end-gap where `/speckit-taskstoissues` silently dropped rows.

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup (P1)** → no deps.
- **Foundational (P2)** → after Setup; **blocks all user stories** (error enum + `View` base + `framer.hpp` surface + `frame_view` factory + shared conformance driver + test doubles). T006/T008/T009 resolve analyze I1/I2 — US1 is now self-contained.
- **US1 (P3, P1-priority)** → after Foundational. The MVP and critical-path unblocker; the cutover (T028–T031) lives here. Independently testable via `frame_view_factory` (T008) — **no US3 dependency**.
- **US2 (P4)** → after Foundational; its round-trip test (T033/T036) consumes the US1 parse path.
- **US3 (P5)** → after Foundational; implements the `framer.cpp` algorithm (T040) against the Foundational `framer.hpp` surface (T006).
- **US4 (P6)** → after Foundational; `validator.hpp` includes `parser.hpp` so it needs US1/T024.
- **Polish (P7)** → after all targeted stories.

### Critical path

T001→T002 → T003/T004/T005/T006/T007/T008/T009/T010 → **US1** (T011–T020 red → T021→T022→T023→T024→T025/T026/T027 → cutover T028→T029/T030→T031) → US2/US3/US4 (parallelizable post-Foundational, modulo the US1-surface deps noted) → Polish.

### Within each story

- Test tasks (red) precede implementation; verify they FAIL first.
- Headers/types before the `.cpp` that implements them; `offset_table` + `field_view` + the Foundational `framer.hpp` surface before `parser.hpp`; `parser.hpp` before the cutover.
- Story GREEN task closes the phase before the next priority.

### Parallel opportunities

- Setup: T002 ∥ T001-tail.
- Foundational: T004 ∥ T005 ∥ T007 ∥ T010 (after T003); T006 before T008; T008 ∥ T009.
- US1 tests T011–T020 all [P] (distinct files); impl T021 ∥ (T022→T023), then T025 ∥ T026 after T024.
- Post-Foundational, US2/US3/US4 test authoring can proceed in parallel with US1 implementation; their GREEN tasks gate on the noted US1 surfaces.
- Polish T048/T049/T050/T052/T053/T054 are mutually [P].

---

## Implementation Strategy

### MVP first (US1 only)

1. Setup → Foundational (incl. T006 `framer.hpp` surface + T008 `frame_view_factory` + T009 shared driver — US1 self-containment).
2. US1 (T011–T031) — real `MessageView`/`OffsetTable`/`field_view`, hybrid eager/lazy parse, **the 2b cutover green** (001 FLOAT + 003 reify unblocked).
3. **STOP & VALIDATE**: US1 independent test (via `frame_view_factory`) + quickstart §9 cutover sanity. This alone discharges the critical-path 2b unblock and SC-006 — with **no** US3 `Framer` algorithm dependency.

### Incremental delivery

US1 (MVP, parse + cutover) → US2 (serialize + round trip) → US3 (TCP framing algorithm) → US4 (validator) → Polish (perf/fuzz/footprint-spike artifact/verify). Each story is independently testable and adds value without breaking the prior.

---

## Notes

- `[P]` = different files, no incomplete-task dependency.
- Every test seam is bound to an explicitly named on-disk file (plan.md Test-seam mapping; no globs).
- Zero `new`/`delete` parse→`fromApp` (`[const §VIII.5]`); three-arena pinning is verified by seam #10 + #13.
- The cutover is a **surface migration**, not a body-only swap — the include path is preserved, the surface changes, 003's drift guard is reconciled (not deleted).
- **(G1)** the GREEN/Tier-1 tasks (T031/T036/T041/T047/T055) and any task implying a local Conan/CMake build MUST be preceded by the `[const §XVII.7]` `AskUserQuestion` build-approval prompt at `/implement` time (the agent never auto-runs `conan install`/`cmake --build`); consistent with project memory `feedback_self_run_build_gate`.
- Commit after each task or logical group; stop at any checkpoint to validate a story independently.
- `/speckit-verify` GREEN (T056) **and** the T058 feature-completeness audit at 100% (or recorded waivers) are both mandatory `/gate-b` preconditions (`[const §XVII.8]`); T057 catalogue/coverage-index update is required by `[const §VI.4]`/`[const §VI.6]` before any owned row is declared `done`. T058 exists to mechanically prevent the 001-class end-gap (tasks/features silently dropped before "feature complete").
