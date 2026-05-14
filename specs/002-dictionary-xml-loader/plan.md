---
id: 002-dictionary-xml-loader
title: Implementation Plan — XML data dictionary loader (`fixpp::dict::XmlLoader` + `Dictionary`)
module: dictionary/
phase: 4
status: drafted
verdict: TBD
spec_kit_step: /plan
gate_a_round: pending (round 1 runs after this plan lands)
gate_b_round: pending
last_updated: 2026-05-14
inherits_design: .specify/2c-codegen.md (v1.3, signed off 2026-05-10)
inherits_spec: specs/002-dictionary-xml-loader/spec.md (carries /clarify Q&A 2026-05-14 — Q1→B, Q2→A, Q3→A)
catalogue_rows: D-007 (XML data dictionary format loader — discharged), D-010 (Component definition support — implicit, since `ComponentRef` ships), OSS-001 (QuickFIX-XML compatible loader reference — discharged)
---

# Implementation Plan — 002-dictionary-xml-loader

**Branch:** `002-dictionary-xml-loader` | **Date:** 2026-05-14 | **Spec:** [`spec.md`](spec.md)
**Input:** Feature specification at `specs/002-dictionary-xml-loader/spec.md`.

## Summary

Ship the runtime XML data-dictionary loader — `fixpp::dict::XmlLoader`, `fixpp::dict::Dictionary`, `fixpp::dict::FieldRef`, `fixpp::dict::ComponentRef`, `fixpp::dict::GroupRef` — as the **first Phase 4 feature of the `dictionary/` module**. Reads a QuickFIX-XML-format FIX data-dictionary file and returns a populated, immutable `Dictionary` by value (per `[2c §4.5]`'s exception-API carve-out — construction-time failures throw `dict::xml_parse_error` / `dict::unknown_version_error` / `dict::xml_oom_error`; hot-path APIs on the resulting `Dictionary` are `noexcept`). Unblocks `wire::Validator` (**2b**), `fixpp-codegen` (D-008), `session/` (uses `Dictionary` per `[2c §4.3]`), and downstream features in `capi/` and `pybind/`.

Technical approach is locked by `[2c]` v1.3:
- `XmlLoader` is a stateless value with two methods — `load(path, mr)` and `load_from_string(text, mr)`. `load_overlay*` is **absent** per /clarify Q2 → A.
- `Dictionary` is move-only, value-typed, and frozen-after-handoff per `[2c §4.3]` / `[2c §6.1.1]`. The heap-pinned metadata-handle is allocated via `std::allocate_shared` over a `std::pmr::polymorphic_allocator` so the control block returns memory to the originating `mr`. Every public accessor is `const` and `noexcept`.
- `FieldRef` (`sizeof == 16, alignof == 2`), `ComponentRef` (`sizeof == 12`), `GroupRef` (`sizeof == 12`) are POD value types with `_reserved` discipline matching `[2a §4.2]` (zero on emit; ignored on read in v1.0).
- Three exception types (`dict::xml_parse_error : std::runtime_error`, `dict::unknown_version_error : std::runtime_error`, `dict::xml_oom_error : std::bad_alloc`) carry matching `fixpp::core::error` enum variants accessible via `.code()` (research.md D-10).
- XML parser: **pugixml** at `v1.14`, vendored via Conan; visible only from `src/dictionary/xml_loader.cpp` (research.md D-1 / D-15).
- QuickFIX-XML source: upstream `quickfix/quickfix` repo at a pinned SHA; four XML files (FIX42, FIX44, FIX50SP2, FIXT11) checked in under `dictionaries/` (research.md D-2).

## Technical Context

**Language/Version:** C++23 (`[const §II.1]`). Free use of concepts, ranges, `std::expected`, `std::pmr`, deducing `this`. No fallback to earlier standards.

**Primary Dependencies:** GoogleTest 1.17.0, Google Benchmark 1.9.5 (already pinned via Conan from Phase 3 CI), **pugixml 1.14** (NEW in this PR — research.md D-1), `std::pmr` (libc++). The pugixml addition is a new Conan row; transitive deps resolve through the existing Phase-3 lockfile mechanism.

**Storage:** N/A on the hot path; PMR-backed metadata storage owned by `Dictionary` is per-load (research.md data-model §"PMR allocation accounting").

**Testing:** GoogleTest + GoogleMock (C++) for AC-L*, AC-D*, AC-F*, AC-T*, AC-P* per `[const §VII.1]`. No Python pytest seam in this PR (the SWIG bindings for `Dictionary` are owned by **2m**, out of scope per spec §5). No libFuzzer harness in this PR — the design doc's seam #8 (XML fuzzer) is a `wire::*` follow-up because v1.0's `[const §VII.7]` parser-touching fuzz threshold is owned by the wire-layer feature, not the loader; the loader's exception discipline (research.md D-4) plus the AC-L3..L8 negative-path corpus (seam #7) covers the same defensive surface for the loader.

**Target Platform:** Linux primary (Tier 1: Clang 22 Debug + Release + ASan + UBSan + TSan + Coverage; GCC Release sanity). Windows Tier 2 (manual / nightly) per `[const §IX.6]`. No C-ABI surface in this PR (spec §5), so no abidiff golden — `[const §IX.5]` ABI check is N/A here (research.md D-17).

**Project Type:** C++23 library, dictionary module per `[arch §4.2]`. No SWIG / Python bindings in this PR (deferred to 2m).

**Performance Goals (Linux/Clang/x86_64, warm filesystem cache, four shipped XMLs):**

- `XmlLoader::load(FIX44.xml, mr)`: ≤ 500 ms wall-clock (user-facing target, NFR-002-1).
- `XmlLoader::load(FIX50SP2.xml, mr)`: ≤ 1 s wall-clock (CI regression gate per `[const §VIII.2]`).
- `Dictionary::field_ref(...)`, `field_valid_for(...)`, `field(...)`, `group_first_field(...)`, `length_pair_data_tag(...)`: ≤ 30 ns median per `[2c §6.2]` row 1 (binary search over per-MsgType `FieldRef` array). Not gated in this PR's NFRs because Tier-1 perf bars on `Dictionary` lookup belong to the codegen-version path (D-008); the runtime-XML path inherits the same algorithmic shape (sorted-array binary search).

Bench harness `bench/dictionary/xml_loader_bench.cpp` (seam-of-record for NFR-002-1) enforces the load-time bar via Google Benchmark per `[const §VIII.1]`; ±5 % regression budget per `[const §VIII.2]`.

**Constraints:**

- Zero allocation against the global `operator new` for the entire `load*` call (NFR-002-2). pugixml's transient DOM goes through `malloc/free`, not `operator new` — research.md D-1, D-16. Output `Dictionary` metadata lives entirely on `mr` (AC-P1).
- `noexcept` on every public function of `Dictionary` (AC-D8). Hot-path APIs allocate nothing.
- `XmlLoader::load*` throws `dict::xml_*` exception types only (research.md D-4); no other exception escapes the loader call.
- Determinism: byte-identical XML input → byte-identical `Dictionary` iteration order across runs and across machines (NFR-002-4; research.md D-6 sorted storage).
- Layer edge: `dictionary → core` only (NFR-002-6; research.md D-12). `tools/check_layers.py` clean.
- `thread_local` is banned per `[const §XV]`; no `thread_local`, no global mutable state in `XmlLoader` (research.md D-7).

**Scale/Scope:** ~6 new public headers + ~2 source files + 9 test files + 1 bench harness + 4 XML data files. Roughly ~3000 LOC across implementation and tests (estimate; FIX44.xml alone is ≈170 KB of data, but the loader code is compact). First Phase-4 feature of `dictionary/`; opens the module after 001-core-decimal opened `core/`.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-evaluated post-Phase 1 design.*

All citations below use canonical form `[const §<Roman>.<arabic>]` per `constitution.md:5`. Every cite was re-verified against the constitution after Phase 1 (`research.md` Citation verification pass).

| Article cited | Topic | How this feature satisfies it |
|---|---|---|
| `[const §I.1]` | v1.0 surface — FIX 4.0..5.0SP2 + FIXT.1.1 | This PR ships XML for the four codegen-target versions (`v42, v44, v50sp2, vt11` per `[2c §1.3]`); loader structurally accepts the full nine (AC-L4). F1 tracks the runtime-XML-only remainder. |
| `[const §II.1]` | C++23, no earlier fallback | Plan targets C++23 only; uses `std::pmr`, `std::span`, `std::filesystem`, `std::expected` (indirectly via `core::expected_t`). |
| `[const §III.2]` | Conan dependency manager | pugixml added as a new Conan row at pinned version (`pugixml/1.14`); per `[const §V.3]`. |
| `[const §V.1]` | AGPL-3.0 + commercial dual; no LGPL deps | pugixml is MIT — compatible. Headers carry `SPDX-License-Identifier: AGPL-3.0-or-later`. |
| `[const §V.3]` | LGPL-free + third-party-deps procedure | pugixml is MIT (not LGPL). User sign-off on `/plan` is the third-party admission step; Gate A reviews the choice. |
| `[const §VI.4]`, `[const §VI.5]` | Bidirectional spec traceability + Normative References | Inherits D-007, D-010, OSS-001 from spec.md front-matter; spec.md §13 References lists `[2c §1.3]`, `[2c §4.1]`, `[2c §4.2]`, `[2c §4.3]`, `[2c §4.5]`, `[2c §6.1.1]`, `[2c §6.7]`, `[2c §9]`, `[FIX44]`, `[FIX50SP2 §3.3]`, `[FIXT §5.1]`, `[arch §4.2]`, `[arch §5.2]`, `[arch §5.3]`, `[arch §10]`, `[const §V.3]`, `[const §X]`, `[const §VIII.5]`, `[const §VII]`, `[const §XVII.1]`, `[const §XVII.8]`, `[const §XVIII.2]`. |
| `[const §VII.1]`, `[const §VII.3]` | GoogleTest + TDD | `tasks.md` ordered red-green-refactor per seam; every test target is GoogleTest. |
| `[const §VII.7]` | Fuzzing on parser-touching modules | **Not in scope this PR** — the loader's exception discipline (research.md D-4) plus the AC-L3..L8 negative-path corpus (seam #7) cover the defensive surface; v1.0's full XML fuzz harness `tests/fuzz/fuzz_dict_xml_loader.cpp` (per `[2c §9]` seam #8) lands with the D-009 / `[2c]`-internal overlay-grammar feature, which is the design-doc-canonical first attack-surface entry point. Tracked as a near-term follow-up; **NOT** Gate-B-blocking for this PR — Gate A round 1 reviews the deferral. |
| `[const §VIII.1]`, `[const §VIII.2]` | Google Benchmark + ±5 % perf budget | Bench harness `bench/dictionary/xml_loader_bench.cpp` runs in Tier 1 with `bench/baselines/`. |
| `[const §VIII.5]` | Zero allocation on hot path | `Dictionary` hot-path accessors allocate nothing (AC-D8); `XmlLoader::load*` is NOT on the hot path (one-shot at engine init). NFR-002-2 enforces zero `operator new` calls during the entire `load*` call. |
| `[const §IX.1]` | ≥ 90 % line / ≥ 80 % branch on touched modules | `linux-clang-coverage` preset measures `src/dictionary/*` + `include/fixpp/dict/*`; Tier-1 gate. |
| `[const §IX.2]` | Tier-1 sanitizers (ASan + UBSan + TSan) | ASan + UBSan: every `dictionary_*_test`. TSan: `dictionary_concurrent_readers_test` specifically (AC-T2 / NFR-002-3). |
| `[const §IX.4]` | Tier-1 static analysis clean | clang-tidy + clang-format + cppcheck + IWYU; pre-commit + Tier-1. |
| `[const §IX.5]` | abidiff against last tagged ABI | **N/A this PR** — no C-ABI surface added; the C-ABI accessor for `fixpp_dict_t` is owned by 2i (spec §5). Re-validated post-Phase-1; cited only to record the explicit non-applicability. |
| `[const §IX.6]` | Two-tier CI | Tier 1: every preset matrix from §quickstart §3. Tier 2: Windows manual / nightly per the standard schedule. |
| `[const §X.4]` | Bounded `fixpp_error_t` + forwards-compat | Three new `fixpp::core::error` variants added (`dict_xml_parse_failed`, `dict_unknown_version`, `dict_xml_oom`) per research.md D-10. C-ABI mapping deferred to 2i; audit-trail file `tools/abi_history/error_codes_v1.txt` updated when 2i lands (not Gate-B-blocking for this PR). |
| `[const §XV]` | Banned patterns — `thread_local` | Loader uses no `thread_local`, no global mutable state, no `mutable` member. Verified by inspection + by the existing `tools/` checks. |
| `[const §XV.12]` | No LGPL deps | pugixml is MIT. |
| `[const §XVI.3]` | `/clarify` MANDATORY pre-`/plan` (wire format trigger) | Ran 2026-05-14; three questions answered (Q1→B, Q2→A, Q3→A). Recorded in spec.md `Clarifications`. |
| `[const §XVI.4]` | `/analyze` MANDATORY post-`/plan` | Runs after this plan lands; before `/tasks`. |
| `[const §XVII.1]` | Codex Gate A before `/tasks` (touches public C++ API + wire-adjacent format) | `gate_a_required: yes` in spec front-matter. Round 1 runs after this plan; Codex review + Opus adversarial pass per `feedback_gate_a_codex_dual_pass.md`. |
| `[const §XVII.2]` | Gate B before every merge | Standard Gate B precondition. |
| `[const §XVII.3]` | Independence between author and reviewer | Opus author (`/plan`) + Codex reviewer (Gate A) are independent agents per `/gate-a` skill. |
| `[const §XVII.7]` | Local pre-PR build gate | Contributor confirms `local build: green on linux-clang-debug @ <git-sha>` in PR body. |
| `[const §XVII.8]` | `/speckit-verify` mandatory after `/speckit-implement` | `/speckit-verify 002-dictionary-xml-loader` produces `.specify/decisions/002-dictionary-xml-loader-verify.md`; `GREEN` required for `gate-b-done` label. |

**Gates pass ✅** — all cited articles resolve under canonical form to actual constitution text (verified in research.md "Citation verification pass"). No violations require justification, so `Complexity Tracking` is empty.

## Project Structure

### Documentation (this feature)

```text
specs/002-dictionary-xml-loader/
├── plan.md              # this file (/speckit-plan output, round 1 — 2026-05-14)
├── spec.md              # /specify output 2026-05-14; carries /clarify Q&A
├── research.md          # Phase 0 output — 20 decisions (D-1..D-20)
├── data-model.md        # Phase 1 output — 7 entities, invariants, error mapping
├── quickstart.md        # Phase 1 output — build / test / bench / sanitizer / coverage
├── contracts/
│   ├── field_ref.hpp           # Phase 1 — literal extract from [2c §4.1]
│   ├── component_ref.hpp       # Phase 1 — literal extract from [2c §4.2]
│   ├── group_ref.hpp           # Phase 1 — literal extract from [2c §4.2]
│   ├── dictionary.hpp          # Phase 1 — loader-MVS subset of [2c §4.3]
│   ├── xml_loader.hpp          # Phase 1 — load + load_from_string from [2c §4.5]
│   ├── version_profile.hpp     # Phase 1 — enums subset of [2c §4.3]
│   └── error.hpp               # Phase 1 — three exception types + enum mates
└── tasks.md             # Phase 2 output (/speckit-tasks, NOT created by /speckit-plan)
```

### Source Code (library submodule root)

```text
include/
└── fixpp/
    └── dict/
        ├── field_ref.hpp                  # NEW — FieldRef + field_data_type + field_presence (§4.1)
        ├── component_ref.hpp              # NEW — ComponentRef (§4.2)
        ├── group_ref.hpp                  # NEW — GroupRef (§4.2)
        ├── version_profile.hpp            # NEW — session_version + application_version enums (§4.3 subset)
        ├── error.hpp                      # NEW — xml_parse_error / unknown_version_error / xml_oom_error
        ├── dictionary.hpp                 # NEW — Dictionary (loader-MVS subset of §4.3)
        └── xml_loader.hpp                 # NEW — XmlLoader (load + load_from_string)

include/fixpp/core/
└── error.hpp                              # MODIFIED — three new enum variants appended
                                            #   (dict_xml_parse_failed, dict_unknown_version, dict_xml_oom)
                                            # Additive per [const §X.4] forwards-compat.

src/
└── dictionary/
    ├── CMakeLists.txt                     # MODIFIED — switch fixpp_dictionary from INTERFACE
                                            #            to STATIC; add xml_loader.cpp + dictionary.cpp;
                                            #            add Conan pugixml link.
    ├── xml_loader.cpp                     # NEW — pugixml-backed XmlLoader impls; pulls pugixml.hpp
                                            #       ONLY in this TU per research.md D-15.
    └── dictionary.cpp                     # NEW — Dictionary accessor impls + dict_metadata_handle ctor
                                            #       (calls std::allocate_shared over PMR allocator).

tests/
├── dictionary/
│   ├── CMakeLists.txt                     # MODIFIED — add per-test executables; label them
                                            #            with set_tests_properties(... LABELS dictionary).
│   ├── xml_loader_test.cpp                # NEW — AC-L1 positive-path FIX44 load (replaces
                                            #       dictionary_smoke_test.cpp)
│   ├── dictionary_lookup_test.cpp         # NEW — AC-D1..D7 parameterized over v42/v44/v50sp2/vt11
│   ├── ref_shape_test.cpp                 # NEW — seam #4 — AC-F1..F5 static_asserts
│   ├── negative_paths_test.cpp            # NEW — seam #7 — AC-L2..L8, AC-L10
│   ├── round_trip_test.cpp                # NEW — seam #8 — AC-D1, AC-D2, AC-D5 round-trip
│   ├── determinism_test.cpp               # NEW — seam #5 — NFR-002-4
│   ├── concurrent_readers_test.cpp        # NEW — seam #6 — AC-T1, AC-T2, NFR-002-3 (TSan target)
│   ├── pmr_allocation_test.cpp            # NEW — seam #2 — AC-P1, AC-P2, NFR-002-2
│   ├── oom_injection_test.cpp             # NEW — seam #9 — AC-L9 + AC-P2 translation
│   └── parser_error_test.cpp              # NEW — seam #10 — AC-L3 translation (pugixml's
                                            #       xml_parse_result → dict::xml_parse_error)
└── support/
    ├── pmr_allocation_tracking_resource.hpp   # NEW — reusable PMR resource that counts `new` calls
                                                #       (seam #2 implementation header; carried as
                                                #       shared infra for future dict tests)
    └── failing_pmr_resource.hpp               # NEW — reusable PMR resource that throws `bad_alloc`
                                                #       on the Nth allocate (seam #9 implementation
                                                #       header; reused by future dict tests)
    # tests/support/ already exists from 001; only the two new headers are added here.

bench/
└── dictionary/
    ├── CMakeLists.txt                     # NEW — wire xml_loader_bench into Tier-1 release preset
    └── xml_loader_bench.cpp               # NEW — NFR-002-1 — Google Benchmark on load(FIX44), load(FIX42), load(FIX50SP2)

dictionaries/
├── README.md                              # NEW — pin record (which QuickFIX SHA; how to refresh)
├── UPSTREAM.txt                           # NEW — single-line `quickfix/quickfix @ <sha> tag=<tag> date=<YYYY-MM-DD>`
├── FIX42.xml                              # NEW — verbatim copy from upstream QuickFIX repo
├── FIX44.xml                              # NEW — verbatim copy
├── FIX50SP2.xml                           # NEW — verbatim copy
└── FIXT11.xml                             # NEW — verbatim copy

conanfile.py                               # MODIFIED — requires("pugixml/1.14") added per research.md D-1 / D-15
```

**Structure Decision:** single library, no web/mobile/cli split. Follows the Phase-3 layout (`include/fixpp/dict/`, `src/dictionary/`, `tests/dictionary/`, `bench/dictionary/`, `dictionaries/`). The CMake target shape switches from the existing INTERFACE-library placeholder to a STATIC library carrying `xml_loader.cpp` + `dictionary.cpp`; the `INTERFACE_INCLUDE_DIRECTORIES` and `INTERFACE_LINK_LIBRARIES` (`fixpp::core`) discipline from the existing scaffolding is preserved per `[arch §7.3]`. No `core/` header surface changes (research.md D-3); only `include/fixpp/core/error.hpp` gains three additive enum variants.

### Test seam → file mapping (10/10 — closes spec.md §9)

This sub-section answers the same root-cause class that closed 001-core-decimal Gate A round 1 ("seam→file map partial"). Every one of the 10 test seams in `spec.md §9` is bound to a named on-disk file; cross-cutting per-AC tests get their own per-section files in addition.

| Seam # | spec.md §9 description | On-disk path | NFR / AC linkage |
|---|---|---|---|
| 1 | Mock `XmlSource` — `load_from_string` covers in-memory testing | (no dedicated file; exercised by `negative_paths_test.cpp`) | AC-L10; underpins every AC-L3..L8 test |
| 2 | `pmr_allocation_tracking_resource` — count `new` calls | `tests/dictionary/pmr_allocation_test.cpp` + `tests/support/pmr_allocation_tracking_resource.hpp` | AC-P1, AC-P2, NFR-002-2 |
| 3 | Clock seam — N/A on load path | (none) | — |
| 4 | `FieldRef`/`ComponentRef`/`GroupRef` shape static_assert | `tests/dictionary/ref_shape_test.cpp` | AC-F1, AC-F2, AC-F3, AC-F4, AC-F5 |
| 5 | Determinism oracle — load FIX44.xml twice, hash iteration order | `tests/dictionary/determinism_test.cpp` | NFR-002-4 |
| 6 | TSan concurrent-reader harness | `tests/dictionary/concurrent_readers_test.cpp` | AC-T1, AC-T2, NFR-002-3 |
| 7 | Negative-path XML samples — one per AC-L2..L8 / L10 | `tests/dictionary/negative_paths_test.cpp` | AC-L2, AC-L3, AC-L5, AC-L6, AC-L7, AC-L8, AC-L10 |
| 8 | Round-trip — load FIX44.xml, iterate every `(MsgType, tag)`, look up by both forms | `tests/dictionary/round_trip_test.cpp` | AC-D1, AC-D2, AC-D5 |
| 9 | Allocator-failure injection — PMR throws `bad_alloc` on Nth allocate | `tests/dictionary/oom_injection_test.cpp` + `tests/support/failing_pmr_resource.hpp` | AC-L9, AC-P2 |
| 10 | XML-parser-error injection — `pugi::xml_parse_result → dict::xml_parse_error` translation | `tests/dictionary/parser_error_test.cpp` | AC-L3 (translation isolation) |

**Cross-cutting per-AC tests** (not "seam files" per §9, but binding one AC family to one file):

| File | ACs covered |
|---|---|
| `tests/dictionary/xml_loader_test.cpp` | AC-L1 (positive-path FIX44 load — the MVP smoke; supersedes the existing `dictionary_smoke_test.cpp`) |
| `tests/dictionary/dictionary_lookup_test.cpp` | AC-D1..D7 parameterized over the four shipped versions (v42 / v44 / v50sp2 / vt11) |

**Rule:** no seam may map to "the existing `dictionary_*_test.cpp`s collectively". Each seam has at least one dedicated named file. The two cross-cutting per-AC test files supplement the seam files for per-AC verification.

## Complexity Tracking

> No Constitution Check violations. Section intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |

## Gate A

### Round 1 — 2026-05-14 (pending Codex review)

Gate A round 1 runs after this plan lands. Both Codex passes per `feedback_gate_a_codex_dual_pass.md` (auto-memory):
- **Codex rescue agent** (`/codex:rescue`) — full Phase-4 bundle review.
- **`/codex:adversarial-review`** — adversarial pass challenging design choices.

Followed by Opus post-judging (P1/P2/P3 triage and verdict). The /gate-a skill is the canonical orchestrator. Expected verdict shapes per the 001 exemplar:
- **Round 1 converged (P1 ≤ 0, P2 ≤ small):** proceed to `/tasks`.
- **Full bundle redraft needed:** round-2 redraft of `plan.md` + `research.md` + `data-model.md` + `contracts/` from a literal re-read of `.specify/2c-codegen.md` v1.3; spec.md preserved verbatim (it carries the /clarify Q&A).
- **Hard reset (Round 3):** rewrite from clean context (rare; per `[const §XVII.1]` resets).

Reviews land under `research/G19-fix-fpml-iso20022/research/reviews/` per the 001 convention; full /gate-a decision record at `.specify/decisions/002-dictionary-xml-loader-gatea.md`.

## Gate B

### Round 1 — TBD (post-`/implement`, pre-merge)

Gate B procedure: `.specify/codex-review.md` §6 (prompt) and §7 (recording). Independence rule per `[const §XVII.3]` — each round uses a separate Codex session from the implementer. Mandatory before merge per `[const §XVII.2]`. `/speckit-verify` precondition per `[const §XVII.8]` — record at `.specify/decisions/002-dictionary-xml-loader-verify.md` must be `GREEN` for `gate-b-done` label, `YELLOW` with paired waivers for `gate-b-waived`.

## Citation verification pass (round 1)

| Cite | Resolves to | OK |
|---|---|---|
| `[const §I.1]` | `constitution.md:11` — v1.0 surface | ✅ |
| `[const §II.1]` | `constitution.md:26` — C++23 | ✅ |
| `[const §III.2]` | `constitution.md:41` — Conan | ✅ |
| `[const §V.1]` | `constitution.md:66` — AGPL + commercial dual | ✅ |
| `[const §V.3]` | `constitution.md:68` — No LGPL deps | ✅ |
| `[const §VI.4]` | `constitution.md:79` — Bidirectional traceability | ✅ |
| `[const §VI.5]` | `constitution.md:80` — Normative References | ✅ |
| `[const §VII.1]` | `constitution.md:87` — GoogleTest | ✅ |
| `[const §VII.3]` | `constitution.md:89` — TDD | ✅ |
| `[const §VII.7]` | `constitution.md:93` — Fuzzing (cited only to record the deferral) | ✅ |
| `[const §VIII.1]` | `constitution.md:99` — Google Benchmark | ✅ |
| `[const §VIII.2]` | `constitution.md:100` — ±5 % perf budget | ✅ |
| `[const §VIII.5]` | `constitution.md:106` — Hot-path zero-alloc | ✅ |
| `[const §IX.1]` | `constitution.md:113` — Coverage thresholds | ✅ |
| `[const §IX.2]` | `constitution.md:117` — Tier-1 sanitizers | ✅ |
| `[const §IX.4]` | `constitution.md:119` — Static analysis | ✅ |
| `[const §IX.5]` | `constitution.md:124` — ABI check (N/A here) | ✅ |
| `[const §IX.6]` | `constitution.md:125` — Two-tier CI | ✅ |
| `[const §X.4]` | `constitution.md:136` — Bounded error enum | ✅ |
| `[const §XV]` | `constitution.md:203` — Banned patterns | ✅ |
| `[const §XV.12]` | `constitution.md:218` — LGPL ban | ✅ |
| `[const §XVI.3]` | `constitution.md:234` — /clarify mandatory | ✅ |
| `[const §XVI.4]` | `constitution.md:235` — /analyze mandatory | ✅ |
| `[const §XVII.1]` | `constitution.md:245` — Gate A | ✅ |
| `[const §XVII.2]` | `constitution.md:255` — Gate B | ✅ |
| `[const §XVII.3]` | `constitution.md:257` — Independence rule | ✅ |
| `[const §XVII.7]` | `constitution.md:265` — Local pre-PR gate | ✅ |
| `[const §XVII.8]` | `constitution.md:270` — /speckit-verify | ✅ |

All 28 citations in this plan resolve under canonical form. Cross-doc cites (`[2a §4.2]`, `[2c §X.Y]`, `[arch §X.Y]`, `[FIX44]`, `[FIX50SP2 §3.3]`, `[FIXT §5.1]`) are inherited verbatim from `spec.md §13` References and the design docs themselves.

## Phase-2 input checklist (for `/tasks`)

When `/speckit-tasks` runs after Gate A converges, it consumes this plan plus `data-model.md` + `research.md` + `contracts/` to produce `tasks.md`. Pre-binding the per-task shape here so `/tasks` has one clean input:

- **One task per row of the "Test seam → file mapping" table** above (10 seam rows + 2 cross-cutting AC rows = 12 test-target tasks).
- **One task per source file** in `src/dictionary/` (3 files counting the modified `CMakeLists.txt`).
- **One task per public header** in `include/fixpp/dict/` (7 headers).
- **One task** for the `include/fixpp/core/error.hpp` additive edit.
- **One task** for the `conanfile.py` pugixml addition.
- **One task** for the bench harness (`bench/dictionary/xml_loader_bench.cpp` + its `CMakeLists.txt`).
- **One task** for the four XML data files + the `dictionaries/README.md` + `UPSTREAM.txt` (sourcing from upstream QuickFIX at the pinned SHA).
- **Polish tasks** (sanitizer presets verification, coverage threshold check, layer-edge lint, bench-baseline seeding, `/speckit-verify` invocation, `/gate-a` invocation, `/gate-b` invocation) — each a row in `tasks.md` so `/speckit-verify` has a one-to-one mapping per `[const §XVII.8]`.

Expected total: ~30 tasks. TDD red-green-refactor ordering per `[const §VII.3]` — test files lead, source files follow.
