# Implementation Plan — 004-wire-codec

**Branch**: `004-wire-codec` | **Date**: 2026-05-16 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/004-wire-codec/spec.md`
**Design anchor**: `.specify/2b-wire.md` **Draft v0.2 — Gate A round 1 converged** (the authoritative surface; on conflict the design doc wins).

## Summary

Deliver the `fixpp::wire` module — the five public primitives `Framer`, `Parser`, `OffsetTable`, `Writer`, `Validator` plus the shared `View` flyweight base — implementing catalogue rows **W-001..W-014** (+ OSS-006/008/013) per `[2b §4]`. Zero-copy parse into an offset-table-indexed `MessageView`, multi-message TCP framing with mandatory BodyLength/CheckSum verification, a serializer that auto-computes digit-only BodyLength + byte-sum-mod-256 CheckSum, and a runtime-virtual `Validator` (exactly 5 pure-virtual, `dictionary_driven_validator` default) doing full per-version required/type/enum/group validation. The hybrid eager/lazy offset table is compile-time selected via `Parser<access_mode>` (no hot-path branch). All five primitives are `noexcept` end-to-end with zero `new`/`delete` between parse and `fromApp` (`[const §VIII.5]`), backed by the three-arena pinning model (`[2b §6.6]`/`[2b §8]`).

Per the `/clarify` 2026-05-16 session, this feature also performs the **2b cutover in its own PR**: it removes the vendored frozen `wire::MessageView` stub shipped by 003 (R6 deferral), rewires the `001-core-decimal` wire FLOAT accessor and the `003-dictionary-codegen` typed-read / `dict::reify` round-trip onto the real `MessageView`, and ships those previously-2b-gated tests green. It also executes the `[arch §11 row 1]` eager-vs-lazy offset-table footprint spike as an in-PR decision artifact.

## Technical Context

**Language/Version:** C++23 (`[const §II.1]`). Free use of concepts, ranges, `std::expected` (via `core::expected_t`), `std::pmr`, `std::span`, deducing `this`. No fallback to earlier standards. `Parser<access_mode Mode>` is a template (compile-time mode selection — no runtime dispatch on the hot path, `[2b §6.3]`).

**Primary Dependencies:** GoogleTest 1.17.0 + GoogleMock, Google Benchmark 1.9.5, libFuzzer (Clang) — all pinned via Conan from Phase 3 CI. **No new Conan row.** Wire reuses `fixpp::core` (`expected_t`, `error`, `decimal<T>` traits from 2a/001) and the `dict::table_view` value-typed metadata contract owned by 2c/003 — no new external dependency admitted (`[const §III.2]`).

**Storage:** N/A on the runtime hot path. All wire allocations are PMR-arena-confined across three pinned lifetime classes (`[2b §6.6]`/`[2b §8]`): the per-message arena (`SessionConfig::message_arena`, reset after `fromApp`), the session-lifetime framer carry arena (`SessionConfig::framer_carry_arena`), and the writer scratch arena (constructor parameter). Zero `new`/`delete` between parse and `fromApp` (`[const §VIII.5]`). Iter mode is zero-allocation end-to-end.

**Testing:** GoogleTest + GoogleMock (C++), TDD red-green-refactor per `[const §VII.1]`/`[const §VII.3]`. New libFuzzer harnesses required — wire is parser-touching (`[const §VII.7]`): `fuzz_wire_framer.cpp`, `fuzz_wire_parser.cpp`, `fuzz_wire_validator.cpp` (ASan+UBSan, `[2b §9]` seam #11). Test seams `[2b §9]` #1..#14 map to named on-disk files (see Test-seam mapping below). No new Python pytest seam — wire is C++-only with no C-ABI surface (`[2b §5]` / `[const §X.2]` / spec FR-014; `fixpp_msg_t` accessors owned by 2i).

**Target Platform:** Linux primary (Tier 1: Clang 22 Debug + Release + ASan + UBSan + TSan + Coverage; GCC Release sanity). Windows Tier 2 (manual/nightly) per `[const §IX.6]`. **No C-ABI surface added** in this PR (`[2b §5]` — wire is invisible to `<fix/c_api.h>`, `[const §X.2]`); `[const §IX.5]` abidiff is **N/A here** (recorded for explicit non-applicability, research D-13).

**Project Type:** C++23 library, `wire/` module per `[arch §4.3]`. Header-mostly (`Parser`/`View`/`group_view`/`unknown_fields_view` are header-only templates per OSS-006); out-of-line `.cpp` for `Framer`, `OffsetTable` build, `dictionary_driven_validator`, `Writer::commit`. No build-only tool, no SWIG/Python bindings, no C-ABI in this PR.

**Performance Goals (Linux/Clang/x86_64, warm cache, release `-O2`):** per `[2b §6.6]` v0.2 latency table; CI fails on >5% regression vs `bench/baselines/` per `[const §VIII.2]`:

| Operation | Workload | Ceiling |
|---|---|---|
| `Framer::feed` | 1 frame, no carry, 80-byte body | ≤ 30 ns |
| `Parser<Iter>::parse_iter` | 20-tag message | ≤ 80 ns |
| `Parser<Index>::parse` | 20-tag message | ≤ 400 ns |
| `Parser<Index>::parse` | 200-tag Instrument-heavy | ≤ 4 µs |
| `OffsetTable::find` | after build, 32-slot hash | ≤ 15 ns |
| `Validator::validate` | 20-tag message, full pass | ≤ 200 ns |
| `Writer::commit` | 20-tag message | ≤ 80 ns |
| `Writer::commit` | 200-tag Instrument-heavy | ≤ 800 ns |

Bench harnesses `bench/wire/{framer,parser,offset_table,validator,writer}_bench.cpp` + the offset-table footprint micro-bench enforce the bars via Google Benchmark (`[const §VIII.1]`); ±5% budget (`[const §VIII.2]`). Parser parity-or-better vs `hffix` is a v1.0 release gate (`[const §VIII.4]`), measured here but not blocking this PR.

**Constraints:**

- Zero `new`/`delete` between parse and `fromApp` (`[const §VIII.5]`); three-arena pinning (`[2b §6.6]`/`[2b §8]`). `tools/check_alloc.py` under `mallocnesia` (Linux) verifies (`[2b §9]` seam #10).
- All public surfaces `noexcept`; throwing trait wrappers trap via `core::detail::trap_throw` (`[2b §6.6]`; `[arch §5.3]`).
- All view-returning accessors carry `[[clang::lifetimebound]]`; all `expected_t<T>`-returning calls carry `[[nodiscard]]` (`[2b §4]`; `[arch §5.5]`). Debug-build `detail::generation_token` traps use-after-buffer-reuse; release strips it (`[[no_unique_address]]` empty member).
- `sizeof(OffsetTable::entry) == 12`, `alignof == 4`, pinned by co-located `static_assert` (`[2b §1.2]`/`[2b §4.4]`).
- DoS caps: `default_max_frame_bytes = 256 KiB`, `default_max_offset_entries = 4096` (occurrence space), `default_max_group_entries_per_instance = 4096`, tag range `uint16_t` 0..65535 — caller-tunable, enforced with bounded memory (`[2b §1.2]`).
- `Validator` is a runtime-virtual plugin with **exactly 5 pure-virtual methods** (`[const §XIV.2]` ≤5 cap satisfied directly, `[2b §4.6]`); holds `dict::table_view` by value — no virtual `wire/`→`dict/` runtime edge.
- CheckSum verification mandatory, no production bypass (`[2b §2]`); digit-only BodyLength, no space padding (`[FIX50SP2 §3.3]`; `[2b §4.5]`).
- Wire is clock-free (`[2b §7.3]`) and emits no C-ABI symbols (`[const §X.2]`; `[2b §5]`).

**Scale/Scope:** ~10 public headers under `include/fixpp/wire/` (`view`, `framer`, `parser`, `offset_table`, `writer`, `validator`, `group_view`, `unknown_fields`, `errors`, plus `message_view_contract.hpp` **surface-migrated** from the R6 frozen-thin stub to a thin re-export of the real `[2b §4.3]` surface — D-15/RC#1) + ~4 out-of-line `.cpp` under `src/wire/` (`framer.cpp`, `offset_table.cpp`, `validator.cpp` [`dictionary_driven_validator`], `writer.cpp`) + ~14 test files mapping `[2b §9]` seams #1..#14 + 3 fuzz harnesses + ~6 bench harnesses + the W-001..W-014 conformance corpus + the cutover edits to `include/fixpp/dict/reify.hpp`/`field_traits.hpp` (003) and the reconciliation of 003's `tests/codegen/flyweight_shape_test.cpp` drift guard (seam #18/I-12). The 001 wire FLOAT-field accessor is **004-authored net-new** wire code (no 001 file to repoint — D-17). 13 new `fixpp::core::error` variants (`[2b §6.7]`; slot 41 `wire_field_value_truncated` is a specified re-map of 2a `decimal_precision_loss`, not deleted — design doc wins). Estimate ~5500 LOC hand-written (impl + tests + bench). Migrates the vendored frozen stub surface shipped by 003 (R6 closed here).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-evaluated post-Phase 1 design.* All citations use canonical form `[const §<Roman>.<arabic>]` per `constitution.md:5`. **Mood:** at this `/specify`-stage gate the rows below assert *planned conformance* (the plan reserves the artifacts/structure that will satisfy each article); delivered/verified evidence is produced by `/speckit-implement` + `/speckit-verify` post-`/implement`, not here. The citation-verification pass (round 1) at the end of this file was actually run for this revision (Gate A round 1, Root cause #2).

| Article cited | Topic | How this feature satisfies it |
|---|---|---|
| `[const §II.1]` | C++23, no earlier fallback | `wire/` is C++23 only; concepts (`dict::table_view`), `std::pmr`, `std::span`, `core::expected_t`, deducing `this`. |
| `[const §III.2]` | Conan, pinned deps | **No new Conan row.** Reuses `fixpp::core` + `dict::table_view` contract; GTest/Benchmark/libFuzzer already pinned. |
| `[const §V.1]`,`[const §V.3]` | AGPL-3.0 dual; no LGPL | No new dependency. Every new header carries `SPDX-License-Identifier: AGPL-3.0-or-later`. |
| `[const §VI.4]`,`[const §VI.5]` | Traceability + Normative References | Owns W-001..W-014 + OSS-006/008/013 (inherited, no new catalogue row per `[2b §11]`); spec Authority anchor + Normative References (`[2b App-B]`) list exact refs. |
| `[const §VII.1]`,`[const §VII.3]` | GoogleTest + TDD | `tasks.md` ordered red-green-refactor per `[2b §9]` seam; every C++ test target is GoogleTest. |
| `[const §VII.5]`,`[const §VII.6]` | Conformance corpus + interop | W-001..W-014 conformance corpus (`tests/wire/conformance/`, seam #2). Interop (QuickFIX Logon→…→Logout) is a session-layer v1.0 gate — wire supplies the parse/serialize substrate, full interop deferred to the session feature. |
| `[const §VII.7]` | Fuzzing on parser-touching modules | **New harnesses required; planned, not yet delivered** — Project Structure reserves `tests/fuzz/fuzz_wire_{framer,parser,validator}.cpp` (seam #11); they land at `/implement` and run ASan+UBSan ≥10-min Tier-1, verified at `/speckit-verify`. |
| `[const §VIII.1]`,`[const §VIII.2]` | Google Benchmark + ±5% budget | `bench/wire/*` harnesses + `bench/baselines/wire/*.json`; Tier-1 regression gate on the `[2b §6.6]` ceilings. |
| `[const §VIII.4]` | Parser parity-or-better vs hffix | Measured in `bench/wire/parser_bench.cpp` + reported in `bench/REPORT.md`; v1.0 release gate, not a this-PR blocker (recorded, research D-14). |
| `[const §VIII.5]` | Zero alloc parse→`fromApp` | Three-arena pinning (`[2b §6.6]`/`[2b §8]`); Iter mode zero-alloc; Index/Validator/Writer arena-confined and bounded by `[2b §1.2]` caps; `tools/check_alloc.py` seam #10. |
| `[const §IX.1]` | ≥90% line / ≥80% branch on touched modules | Planned: `linux-clang-coverage` will measure `include/fixpp/wire/*`, `src/wire/*` as the Tier-1 gate; the threshold is asserted/enforced at `/speckit-verify`, not claimed met here. |
| `[const §IX.2]` | Tier-1 sanitizers | ASan+UBSan on every wire test; TSan on the three-arena + view-escape tests; fuzz harnesses ASan+UBSan. |
| `[const §IX.4]` | Tier-1 static analysis | clang-tidy + clang-format + cppcheck + IWYU on all wire headers/sources; pre-commit + Tier-1. |
| `[const §IX.5]` | abidiff vs last tagged ABI | **N/A this PR** — no C-ABI surface (`[2b §5]`; `[const §X.2]`). Cited for explicit non-applicability (research D-13). |
| `[const §IX.6]` | Two-tier CI | Tier 1: every preset from quickstart §3. Tier 2: Windows manual/nightly. |
| `[const §X.2]` | No C++ leakage through C ABI | Wire emits **no** `extern "C"` symbols; no wire type appears in `<fix/c_api.h>`. `nm` check (Linux) confirms (`[2b §5]`; spec FR-014). |
| `[const §X.4]` | Bounded `fixpp_error_t` + forwards-compat | 13 new `wire_*` `core::error` variants appended at unused slots, non-renumbering (`[2b §6.7]`; data-model "Error mapping"). C-ABI coalescing target documented for 2i (`FIXPP_ERR_WIRE_*`); the abi_history audit-trail entry is deferred to 2i under the same time-bounded waiver as 002/003 (no C-ABI surface here). |
| `[const §XI.4]`,`[const §XI.7]` | Concurrency/threading | Wire is clock-free and single-thread-per-session — runs on the per-session strand (`[const §XI.4]`); `Framer` owned by the session I/O strand; no awaitable/mutex introduced (so `[const §XI.3]` does not bind). Wire is not a threading-affecting feature in the `[const §XI.7]` sense (no new threading primitive); cross-strand escape is via `MessageView::reify` (owned by 2c) — a documented contract whose threading semantics are owned at `[2b §6.6]` / 2c, not introduced here. |
| `[const §XIV.2]` | ≤5 pure-virtual on pluggable interfaces | `Validator` is runtime-virtual with **exactly 5** pure-virtual (`[2b §4.6]`); cap satisfied directly, no justification paragraph needed. |
| `[const §XV.1]`,`[const §XV.7]`,`[const §XV.8]` | Banned: per-msg heap; linear-find-only; cache-hostile map | Planned design: zero-copy views + arena (XV.1); offset-table O(1) hash mandatory for Index path (XV.7); `entry[]` vector + open-address overlay, no `std::multimap` (XV.8). This feature is the planned discharge site for the `[const §XV.7]` offset-table mandate (discharge proven at `/implement`/`/speckit-verify`, not asserted complete here). |
| `[const §XVI.3]` | `/clarify` mandatory pre-`/plan` | Ran 2026-05-16 (3 questions: cutover/validator/spike scope). Recorded in spec `## Clarifications`. |
| `[const §XVI.4]` | `/analyze` mandatory post-`/plan` | Runs after Gate A converges, before `/tasks` (`/speckit-analyze` pass). |
| `[const §XVII.1]` | Codex Gate A before `/tasks` (wire format/parser trigger) | `gate_a_required: yes` (Appendix A "Wire format / parser"). The design doc `2b-wire.md` already passed Phase-2 Gate A r1; the Phase-4 bundle Gate A (`/gate-a 004-wire-codec`) runs before `/tasks`; both Codex passes per `feedback_gate_a_codex_dual_pass`. Wire is non-ABI-touching so `[const §X.6]` does not apply (`[2b App-B]`). |
| `[const §XVII.2]`,`[const §XVII.3]` | Gate B before merge; author≠reviewer | Standard Gate B precondition; Opus `/plan` author independent of Codex Gate A reviewer per `/gate-a`. |
| `[const §XVII.7]` | Local pre-PR build gate | Contributor confirms `local build: green on linux-clang-debug @ <sha>`; agent surfaces `AskUserQuestion` before any local Conan/CMake build. |
| `[const §XVII.8]` | `/speckit-verify` mandatory post-`/implement` | `/speckit-verify 004-wire-codec` → `.specify/decisions/004-wire-codec-verify.md`; `GREEN` required for `gate-b-done`. |

**Gates — PASS (planned-conformance; no violation requiring justification); Complexity Tracking empty.** All cited articles resolve under canonical form (verified — see Citation verification pass round 1). `Validator`'s exactly-5 pure-virtual is **within** the `[const §XIV.2]` cap (no justification paragraph owed). The cutover is a **surface migration** from the R6 frozen-thin contract to the `[2b §4.3]` real `MessageView : View` surface (D-15 / RC#1) — it changes 003's vendored-stub surface and reconciles 003's `flyweight_shape_test.cpp` drift guard, but introduces no design-doc/constitution amendment (the `[2b §4.3]` target surface and `arch §2.4` v0.3 already define it; design doc wins over the under-specified frozen stub). `[const §XV.7]` (offset-table mandate) and the `[const §XV.6]`-adjacent typed-access path are *planned to be discharged* by this feature (proven at `/implement`/`/speckit-verify`), not violated.

## Project Structure

### Documentation (this feature)

```text
specs/004-wire-codec/
├── plan.md              # this file (/speckit-plan 2026-05-16)
├── spec.md              # /specify + /clarify 2026-05-16 (cutover/validator/spike scope)
├── research.md          # Phase 0 — design decisions D-1..D-15 (anchored to 2b-wire.md v0.2)
├── data-model.md        # Phase 1 — wire entities, invariants, error mapping, PMR/arena accounting
├── quickstart.md        # Phase 1 — build / test / fuzz / bench / sanitizer / coverage / footprint-spike / verify / gate
├── contracts/
│   ├── view.hpp                     # [2b §4.1] View flyweight base
│   ├── framer.hpp                   # [2b §4.2] Framer + pmr_carry_buffer + frame_view
│   ├── parser.hpp                   # [2b §4.3] Parser<Mode> + MessageView<Mode> + field_iterator
│   ├── field_view.hpp               # [2b §4.3] field_view : View shape oracle (cutover surface — D-16, RC#1)
│   ├── offset_table.hpp             # [2b §4.4] OffsetTable + entry (sizeof==12 pinned)
│   ├── writer.hpp                   # [2b §4.5] Writer + group_writer
│   ├── validator.hpp                # [2b §4.6] Validator (5 pure-virtual) + dictionary_driven_validator
│   ├── group_view.hpp               # [2b §4.7] group_view<GroupT> + iterator
│   ├── unknown_fields.hpp           # [2b §4.8] unknown_fields_view
│   └── wire_errors.hpp              # [2b §6.7] the 13 wire_* core::error variants
├── checklists/
│   └── requirements.md  # /specify quality checklist (all pass)
└── tasks.md             # Phase 2 (/speckit-tasks — NOT created here)
```

### Source Code (library submodule root)

```text
include/fixpp/wire/
├── view.hpp                    # [2b §4.1] header-only flyweight base + detail::generation_token
├── framer.hpp                  # [2b §4.2] Framer / pmr_carry_buffer / frame_view (decl; impl in src/)
├── parser.hpp                  # [2b §4.3] Parser<Mode>, MessageView<Mode>, field_iterator (header-only template, OSS-006)
├── offset_table.hpp            # [2b §4.4] OffsetTable + entry; static_assert(sizeof(entry)==12)
├── writer.hpp                  # [2b §4.5] Writer / group_writer (decl; commit in src/)
├── validator.hpp              # [2b §4.6] Validator interface + dictionary_driven_validator decl
├── group_view.hpp              # [2b §4.7] group_view<GroupT> header-only template
├── unknown_fields.hpp          # [2b §4.8] unknown_fields_view header-only
├── errors.hpp                  # [2b §6.7] wire_* error helpers (variants live in core/error.hpp)
└── message_view_contract.hpp   # CUTOVER (surface migration): frozen R6 thin stub SURFACE REPLACED by a thin re-export of the real [2b §4.3] parser.hpp MessageView:View (include path preserved, surface changed — D-15/RC#1)

src/wire/
├── CMakeLists.txt              # fixpp_wire target + bench/test/fuzz wiring
├── framer.cpp                  # [2b §6.1] framing algorithm, carry handling, checksum verify
├── offset_table.cpp            # [2b §6.2] eager build + open-address overlay + lazy group sub-index
├── validator.cpp               # [2b §6.5] dictionary_driven_validator (5 overrides)
└── writer.cpp                  # [2b §4.5] commit: digit-only BodyLength memmove backpatch + byte-sum CheckSum

include/fixpp/core/error.hpp    # ADDITIVE EDIT: append 13 wire_* variants at unused slots (non-renumbering, [const §X.4])

# Cutover — SURFACE MIGRATION (2b-gated work closed in this PR — /clarify Q1; D-15/D-16/D-17, RC#1):
include/fixpp/dict/reify.hpp                 # rewire dict::reify onto the real wire::MessageView<Index>/field_view (arch §2.4 v0.3 bridge surface)
include/fixpp/dict/field_traits.hpp          # rewire decode_field<T> onto the real View-derived field_view
tests/codegen/flyweight_shape_test.cpp       # 003 R6 DRIFT GUARD (003 seam #18 / I-12): RECONCILE to the migrated MessageView:View surface — retire the stub's own sizeof(MessageView<Index>)==pointer assertion (does not survive : public View); 003's I-1 sizeof(<Msg>)==sizeof(MessageView<Index> const*) is PRESERVED (generated msg holds a pointer)
# (001 leg = 004-AUTHORED net-new wire code, NOT a 001 file to repoint — D-17):
#   the wire FLOAT-field accessor path field_view::bytes() -> fixpp::decimal_t::parse(span, mr)
#   lives in include/fixpp/wire/parser.hpp + the FR-006 trait-decode boundary (001 deferred the
#   wire FLOAT parser/serializer to 2b per 001 spec.md:176 "Blocks: 2b" — no 001 file exists to repoint)

tests/wire/
├── CMakeLists.txt
├── conformance/                # seam #2 — w001..w014_*.csv + parameterized GTest ([FIX50SP2 §3] oracle)
├── framer_partial_read_test.cpp        # seam #4
├── parser_index_test.cpp / parser_iter_test.cpp
├── offset_table_test.cpp
├── round_trip_property_test.cpp        # seam #3 (10^4 samples; parse→reify→Writer→commit→re-parse)
├── lifetime_trap_test.cpp              # seam #7 (debug trap + -Wdangling smoke)
├── repeating_group_equivalence_test.cpp# seam #8 (iter() vs operator[])
├── unknown_fields_test.cpp             # seam #9 (dict-missing vs dict-known-invalid split)
├── checksum_bodylength_corruption_test.cpp # seam #12 (XOR + space-padded BodyLength rejection)
├── three_arena_pinning_test.cpp        # seam #13
├── validator_domain_test.cpp           # seam #14 (unconditional validate, not per-accessor)
├── validator_per_version_test.cpp      # spec SC-005 — v42/v44/v50sp2/vt11 conforming/non-conforming corpus
└── cutover_2b_gated_test.cpp           # spec SC-006 — 001 FLOAT accessor + 003 reify round-trip GREEN on real MessageView

tests/support/
├── mock_dict_table.hpp         # seam #1 — dict::table_view test specialization
└── mock_validator.hpp          # seam #1 — value-substituted virtual Validator

tests/fuzz/
├── fuzz_wire_framer.cpp / fuzz_wire_parser.cpp / fuzz_wire_validator.cpp   # seam #11

bench/wire/
├── framer_bench.cpp / parser_bench.cpp / offset_table_bench.cpp / validator_bench.cpp / writer_bench.cpp  # seam #5
└── offset_table_footprint_bench.cpp    # seam #6 — feeds the [arch §11 row 1] spike (spec SC-008)
bench/baselines/wire/*.json             # ±5% regression baselines
```

**Structure Decision:** Single-project library layout (`[arch §4.3]` wire module). Header-mostly: `view`/`parser`/`group_view`/`unknown_fields` are header-only templates (OSS-006); `framer`/`offset_table`/`validator`/`writer` split out-of-line `.cpp` for compile-time and the `dictionary_driven_validator` vtable. **Cutover = surface migration (D-15/RC#1), not a body-only swap.** The R6 frozen stub `include/fixpp/wire/message_view_contract.hpp` pins a thinner surface than `[2b §4.3]` mandates (no `View` base; `access_mode{Index}` only; `field_view` non-`View`). Per the design-doc-wins rule the migration target is the `[2b §4.3]` real `MessageView<Mode> : public View` surface (`arch §2.4` v0.3 confirms 2b owns the body; the frozen stub was a 003 under-specification). The `<fixpp/wire/message_view_contract.hpp>` include path is **preserved** (a thin re-export of the real `parser.hpp` `MessageView`), so 003's `dict/reify.hpp` include path keeps working — but the **surface changes**, so 003's `tests/codegen/flyweight_shape_test.cpp` drift guard (seam #18/I-12) is reconciled in this PR: the stub's own `sizeof(MessageView<Index>)==pointer` `static_assert` is retired (a `: public View` `MessageView` is no longer pointer-sized), while 003's I-1 `sizeof(<Msg>)==sizeof(MessageView<Index> const*)` is **preserved** (a generated message holds a *pointer*). `contracts/field_view.hpp` (D-16) pins the single authoritative `field_view : View` shape the cutover depends on.

### Test-seam → file mapping (every `[2b §9]` seam + the cutover reconciliation bound to explicitly named on-disk files — no globs)

| `[2b §9]` seam | File | Spec link |
|---|---|---|
| #1 mock_dict_table / mock_validator | `tests/support/mock_dict_table.hpp`, `mock_validator.hpp` | FR-010/011 |
| #2 W-001..W-014 conformance corpus | 14 keyed corpus files `tests/wire/conformance/w001_*.csv` … `w014_*.csv` (one per W-001..W-014) + the parameterized driver `tests/wire/conformance/conformance_test.cpp` (`[FIX50SP2 §3]` oracle) | FR-001..009, SC-001 |
| #3 round-trip property | `tests/wire/round_trip_property_test.cpp` | FR-008, SC-001 |
| #4 multi-message partial reads | `tests/wire/framer_partial_read_test.cpp` | FR-009, SC-004 |
| #5 latency regression | `bench/wire/{framer,parser,offset_table,validator,writer}_bench.cpp` | Perf table, SC-002 |
| #6 offset-table footprint | `bench/wire/offset_table_footprint_bench.cpp` | SC-008 |
| #7 lifetime-trap (debug) + -Wdangling | `tests/wire/lifetime_trap_test.cpp` | FR-016 |
| #8 repeating-group equivalence | `tests/wire/repeating_group_equivalence_test.cpp` | FR-004 |
| #9 unknown-fields preservation (2-case split) | `tests/wire/unknown_fields_test.cpp` | FR-008 edge cases |
| #10 allocation guard (mallocnesia) | `tools/check_alloc.py` invocation + CI | FR-012, SC-002 |
| #11 fuzzers | `tests/fuzz/fuzz_wire_framer.cpp`, `tests/fuzz/fuzz_wire_parser.cpp`, `tests/fuzz/fuzz_wire_validator.cpp` | SC-003 |
| #12 CheckSum/BodyLength corruption corpus | `tests/wire/checksum_bodylength_corruption_test.cpp` | FR-017, SC-003 |
| #13 three-arena pinning | `tests/wire/three_arena_pinning_test.cpp` | FR-012 |
| #14 validator domain (unconditional) | `tests/wire/validator_domain_test.cpp` | FR-010, SC-005 |
| (cutover) 2b-gated GREEN | `tests/wire/cutover_2b_gated_test.cpp` | FR-018, SC-006 |
| (cutover) 003 drift-guard reconcile | `tests/codegen/flyweight_shape_test.cpp` (003 seam #18/I-12 — updated to the migrated `MessageView:View` surface; stub-`sizeof==pointer` assertion retired, 003 I-1 preserved) | FR-018, SC-006 (D-15/RC#1) |
| (cutover) field_view shape oracle | `contracts/field_view.hpp` (`[2b §4.3]` `field_view:View` pin — D-16) | FR-018, SC-006 (RC#1) |
| (per-version) validator corpus | `tests/wire/validator_per_version_test.cpp` | SC-005 |

## Complexity Tracking

> Empty — no justified constitution violation. `Validator`'s 5 pure-virtual is within the `[const §XIV.2]` cap; the cutover is additive rewiring; no `[const §XX]` amendment required.

## Gate A

`gate_a_required: yes` — `[const §XVII.1]` / Appendix A "Wire format / parser". The Phase-2 design doc `2b-wire.md` already converged Phase-2 Gate A round 1 (v0.1→v0.2, Appendix C). This Phase-4 bundle runs `/gate-a 004-wire-codec` (Codex review → Opus adversarial → Opus rewrite) **before `/tasks`**; both Codex passes (rescue + `/codex:adversarial-review`) per `feedback_gate_a_codex_dual_pass`. Open cross-doc items inherited from `[2b §10]` (Q2 HALO@2d, Q4 arena cadence@2d, Q5 Length+Data dialect@2c, Q6 MessageStore raw-frame@2e) are *cross-doc confirmations*, not 004 blockers — tracked in research D-9..D-12; Q1 (footprint spike) and Q3 (debug generation-counter cost) are **in-PR deliverables** (research D-7/D-8).

### Round 1 — applied (pre-`/tasks`)

- Round 1 applied 2026-05-16: Codex P1=1 P2=3 P3=1; Opus post-judging P1=2 P2=4 P3=4; rewrite addresses Root cause #1 (cutover=surface migration: +field_view oracle, +003 drift-guard reconciliation, +concrete 001 leg) and Root cause #2 (re-tense Constitution Check, real citation pass). Reviews: research/reviews/codex_004-wire-codec_gate_a_review.md, research/reviews/opus_004-wire-codec_gate_a_adversarial_review.md.

### Round 1 — disagreements

- Codex 004-GA-01 (P1) — DISAGREE per Opus adversarial: gate-label evidence rule is PR-scoped at merge (`[const §XVII.6]`/§XVII.8 `/gate-b` precondition), not a pre-`/tasks` Gate A blocker; Gate A review record alone unblocks `/tasks`. No blocker rewrite applied; clarifying sentence added to quickstart §8.

## Gate B

### Round 1 — TBD (post-`/implement`, pre-merge; `/speckit-verify` GREEN precondition per `[const §XVII.8]`)

## Citation verification pass (round 1 — actually run at Gate A round 1, 2026-05-16, RC#2)

All `[const §...]`, `[arch §...]`, `[2b §...]`, `[FIX50SP2 §...]`, `[SYN §...]` citations in this plan resolve against: `.specify/constitution.md` (v0.1), `.specify/architecture.md` (v0.3), `.specify/2b-wire.md` (v0.2 Gate-A-r1-converged), spec `feature-catalogue.md` W-001..W-014/OSS-006/008/013.

Defects found and fixed this round:
- `spec §C-ABI` (was in Technical Context / Testing) — **dead anchor** (no `§C-ABI` section exists in spec.md). Replaced with the controlling cites `[2b §5]` / `[const §X.2]` / spec FR-014.
- `[const §XI.x]` (was in the Constitution Check concurrency row) — **non-canonical placeholder** violating `constitution.md:5`'s own canonical-form rule *inside* a row asserting canonical re-verification. Replaced with the specific clauses relied on: `[const §XI.4]` (per-session strand) + `[const §XI.7]` (threading-affecting-feature test — wire is not one). Article XI is verified to have numbered clauses 1–7.

Verified resolutions (sampled): `[const §XIV.2]` (≤5 pluggable pure-virtual — Article XIV); `[const §XV.7]` (offset-table mandate — Article XV); `[const §X.2]`/`[const §X.4]` (Article X); `[2b §4.3]` (`MessageView : public View`, `access_mode{Iter,Index}` — design doc lines ~256-300); `[2b §4.6]` (`validate(msg, scratch_mr)` — design doc §4.6); `[2b §6.5 rule 3]`/`[2b §6.7]` (slot-41 precision-loss re-map, 13-variant list); `[2b §1.2]` (256 KiB / 4096-occurrence caps, "rejects conformant venue traffic" caveat); `[arch §2.4]` v0.3 (dictionary↔wire bridge carve-out, 2b owns the body); `core/error.hpp` current max occupied slot = 29 (`dict_reify_wire_body_not_ready`); `decimal_precision_loss = 12` (slot-41 re-map source). No remaining vague or dead refs (`[const §VI.2]` / `[const §VI.5]` satisfied).

## Phase-2 input checklist (for `/tasks`)

- [x] Spec FR-001..018 + SC-001..008 ↔ `[2b §4]`/`[2b §6]`/`[2b §9]` mapped
- [x] All 14 test seams + cutover reconciliation bound to explicitly named files (Test-seam mapping; no globs)
- [x] Cutover scope fixed (`/clarify` Q1 + Gate A r1 RC#1): **surface migration** — frozen-stub surface → `[2b §4.3]` real `MessageView:View`; 003 `flyweight_shape_test.cpp` drift-guard reconciled; `field_view` oracle added (D-16); 001 leg = 004-authored net-new (D-17)
- [x] Validator depth fixed (`/clarify` Q2): full per-version default, exactly 5 pure-virtual
- [x] Footprint spike fixed (`/clarify` Q3): in-PR decision artifact (seam #6 → SC-008)
- [x] Constitution Check PASS, Complexity Tracking empty
- [ ] Gate A converged (runs before `/tasks`)
- [ ] `/analyze` drift check (post-Gate-A, pre-`/tasks`)
