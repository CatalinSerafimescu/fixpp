---
id: 002-dictionary-xml-loader
title: Research — XML data dictionary loader design decisions
spec_kit_step: /plan Phase 0
last_updated: 2026-05-14
status: drafted (round 1)
inherits_design: .specify/2c-codegen.md v1.3 (signed off 2026-05-10; Gate A converged)
inherits_spec: specs/002-dictionary-xml-loader/spec.md (carries /clarify Q&A 2026-05-14 — Q1→B, Q2→A, Q3→A)
---

# Phase 0 Research — 002-dictionary-xml-loader

All shape decisions for `fixpp::dict::FieldRef`, `ComponentRef`, `GroupRef`, `Dictionary`, and `XmlLoader` are **inherited verbatim from `.specify/2c-codegen.md` v1.3** (signed off after Gate A convergence). This document does not re-litigate them; it records every load-bearing decision in canonical `/plan` Phase 0 format so reviewers, `/tasks`, and Gate A round 1 have one place to look. Three follow-ups carried over from `/specify` Clarifications 2026-05-14 (F1, F2, F3 in `spec.md §10`) are resolved or scheduled here.

Citation form: `[const §<Roman>.<arabic>]` per `constitution.md:5`; `[2c §X.Y]`, `[2a §X.Y]`, `[arch §X.Y]` against the docs in `.specify/`. All cited articles were re-verified against the constitution at the bottom of this file.

## D-1: Third-party XML parser = **pugixml** (resolves /clarify Q3 → A / spec.md F3)

- **Decision:** vendor **pugixml** (MIT) as the underlying XML DOM parser. Pinned at the latest tagged release (currently `v1.14`; final tag confirmed at /tasks time and recorded in `conanfile.py` + `dictionaries/README.md`). Added to `conanfile.py` as a new Conan row; transitive deps resolved via the Phase 3 lockfile mechanism.
- **Rationale:**
  - **Exception discipline.** pugixml's parse entry points return `pugi::xml_parse_result` by value; they do **not** throw on malformed input. This makes the translation `pugi::xml_parse_result → dict::xml_parse_error` a single `if (!result) throw dict::xml_parse_error(result.description())` site — no try/catch around the parser — which fits the spec's exception-API carve-out per `[2c §4.5]` cleanly.
  - **PMR-friendly allocation accounting.** pugixml's default allocator goes through `malloc/free`, not the global `operator new`. That keeps NFR-002-2 ("zero allocation against the global `new` for the entire `load*` call") satisfied by construction for the parser's transient DOM, since the test seam tracks the `new` path. The `Dictionary`'s *output* metadata is what AC-P1 audits — it lives entirely on `mr` (see D-5).
  - **Single-header build.** No CMake-generated config; vendor as one `.hpp` + one `.cpp` under `_third_party/pugixml/` (Conan-managed dep, not source-included), keeps the engine binary small and Tier-1 link times unaffected.
  - **License.** MIT; compatible with AGPL-3.0 + commercial dual per `[const §V.1]`; no LGPL viral concern per `[const §V.3]` / `[const §XV.12]`.
- **Alternatives considered:**
  - **libexpat (Apache-Public).** SAX-style streaming, mature, C-only API. Rejected: (a) SAX requires hand-rolling a state machine to assemble `<field>` / `<component>` / `<message>` records, doubling the code surface; (b) error reporting is via callback + return code, harder to translate uniformly to `dict::xml_parse_error`; (c) per-load allocator override needs `XML_ParserCreate_MM`, doable but more plumbing than pugixml's hook.
  - **tinyxml2 (zlib).** Simple, header-light. Rejected: (a) uses global static state for internal memory pool defaults; (b) some paths throw internally (rare, but breaks the noexcept-by-default story); (c) the DOM is mutable, which doesn't pay off here since we only read.
  - **rapidxml (MIT/Boost-1.0).** Fast but **destructively modifies the input buffer** and is effectively abandoned (last release 2009). Rejected on maintenance grounds.
- **Compile-time / runtime cost:** pugixml at `v1.14` adds ≈10 KLOC, builds in under 1 s on Clang 22 (one TU); FIX44.xml is ≈170 KB and parses in <50 ms on a developer machine per pugixml's published benchmarks — well under the NFR-002-1 ≤500 ms budget.
- **Where the choice gets re-evaluated:** Gate A round 1 (Codex review of this plan). If Codex raises a P1 against pugixml the round-2 redraft picks libexpat as the fallback and updates D-1.

## D-2: QuickFIX-XML source-of-truth pin (resolves spec.md §11 R2)

- **Decision:** XML data files for FIX 4.2 / 4.4 / 5.0 SP2 / FIXT.1.1 come from the upstream QuickFIX C++ repository (`quickfix/quickfix` on GitHub), `spec/FIX42.xml` / `FIX44.xml` / `FIX50SP2.xml` / `FIXT11.xml`. Pinned to the most recent release tag at the time `/tasks` runs (provisional target: `v1.15.1`; final SHA recorded both in `dictionaries/README.md` and in a `dictionaries/UPSTREAM.txt` line of the form `quickfix/quickfix @ <sha> tag=<tag> date=<YYYY-MM-DD>`). The four XML files are checked in to `dictionaries/` as data, not regenerated at build time, so a `git clone` produces a buildable engine without network access.
- **Rationale:**
  - **One canonical source.** QuickFIX's `spec/*.xml` is the most widely-used QuickFIX-format dictionary, and the loader is defined as "QuickFIX-XML compatible" per `[2c §4.5]`; using QuickFIX/N's variants or vendor forks would risk silent schema divergence that the AC-D6 / AC-D7 headline checks wouldn't catch.
  - **Hash pinning over branch pinning.** A SHA pin survives upstream branch renames; the tag is recorded for human readability only.
- **Alternatives considered:**
  - QuickFIX/N (.NET fork) — same XML format but separate maintenance cadence; not chosen since the C++ repo is our schema reference for OSS-001.
  - Generated at build time via `tools/codegen/fixpp-codegen` — out of scope here; codegen (D-008) consumes these files, doesn't produce them.
- **Licence:** QuickFIX is BSD-style; verbatim copy of the XML data files into this repo is licence-compatible. The four files plus the README pin add ≈3.5 MB to the repo; acceptable.

## D-3: `core/` surface changes admitted by this PR (resolves spec.md §7 "C++ headers (core)")

- **Decision (rewritten in Gate A round 1 per Opus root cause #1).** This PR is the **second consumer of `core/`** after 001-core-decimal, and it **does** touch the `core/` surface. The disk state on `main` after 001 is: `include/fixpp/core/` contains `decimal.hpp`, `decimal_alias.hpp`, `decimal_helpers.hpp`, `error.hpp`, `version.hpp` (verified by `ls`). 001 did **not** ship `include/fixpp/core/expected.hpp` or `include/fixpp/core/pmr.hpp` as standalone headers — `expected_t<T>` is declared inline inside `core/error.hpp`, and `std::pmr` aliases are consumed by including `<memory_resource>` directly. The earlier draft of this research note claimed both headers were present; that was false. Three `core/` changes land in this PR:
  1. **`include/fixpp/core/error.hpp` — MODIFIED (additive).** Three new variants appended to the `fixpp::core::error` enum at unused slots: `dict_xml_parse_failed = 20`, `dict_unknown_version = 21`, `dict_xml_oom = 22`. Non-renumbering; no collision with the existing `out_of_memory = 1` and `decimal_* = 10..13` slots. Forwards-compatible per `[const §X.4]`. Audit-trail update to `tools/abi_history/error_codes_v1.txt` is deferred to 2i per D-10 (no C-ABI surface in this PR; cited there as the waiver rationale).
  2. **`include/fixpp/core/decimal_helpers.hpp` — MODIFIED (additive).** A sibling template `detail::trap_throw_or_throw<E, F>` is added next to the existing `detail::trap_throw<F>`. The existing `trap_throw` returns `expected_t<T>` and never throws (it converts `std::bad_alloc` into `expected_t::unexpected{error::out_of_memory}`); the loader's exception-API carve-out per `[arch §5.3]` needs a different shape — catch `std::bad_alloc` and re-throw as `E{}` (typically `dict::xml_oom_error`); rethrow any other exception unchanged. The new helper has that shape:
     ```cpp
     template <class E, class F>
     auto trap_throw_or_throw(F&& fn) -> std::invoke_result_t<F>;
     ```
     Header name choice — extend `decimal_helpers.hpp` rather than create `trap_throw_to_exception.hpp` — minimizes the public-header churn (the helper is closely related to the existing one and the file is already a `detail::` helper-cluster).
  3. **No new `core/expected.hpp` or `core/pmr.hpp` header lands.** `expected_t<T>` is already public via `<fixpp/core/error.hpp>` (the loader includes that header transitively through `<fixpp/dict/error.hpp>`); `std::pmr` aliases are consumed directly via `<memory_resource>`. The spec §7 line referencing `core/expected.hpp` and `core/pmr.hpp` is dropped (see spec.md §7 round-1 edit).
- **Rationale for the admission.** Codex Gate A round 1 (adversarial P1.2, P1.3, P1.4, P2.6) and Opus root cause #1 verified that the previous "no `core/` changes" stance was structurally inconsistent: `contracts/error.hpp` consumes `fixpp::core::error::dict_*` variants that did not exist on disk; `data-model.md` describes a `trap_throw → xml_oom_error` translation path that the shipped `trap_throw` cannot perform (it returns `expected_t`, doesn't throw); and constructors marked `noexcept` forward `std::string` into `std::runtime_error` which may allocate. The three additive `core/` changes above plus the `noexcept` removal in `contracts/error.hpp` close those gaps with a coherent surface admission.
- **Variant numbering and audit trail.** Variants live at slots 20–22 (unused; the existing block ended at decimal slot 13). Per `[const §X.4]`'s "once a numeric value is published in a tagged C ABI release it never changes meaning" rule, the slot assignment is the irreversible commitment; the audit-trail file `tools/abi_history/error_codes_v1.txt` is updated by the 2i feature that ships the C-ABI mapping — not by this PR, per the same article's "consumer of the C ABI tracks the mapping" division of labour (002 has no C-ABI surface per spec §5). See new P2.1 in plan.md's Gate A § disagreements / acknowledgements (audit-trail waiver rationale).
- **Risk reassessed.** spec.md §11 R5 ("`core/` trivial-fold drift") **does** fire on this PR (it was always going to), but the drift is purely additive: existing slots and existing variants are preserved verbatim; existing `decimal_*` consumers compile unchanged; existing `trap_throw` callers compile unchanged. The 001 ABI golden is unaffected because no `core/` *type* changes shape and the new variants don't surface through the C ABI in this PR (spec §5).

## D-4: Exception-API carve-out for `XmlLoader::load*` (resolves spec.md §1 style note)

- **Decision:** `XmlLoader::load(path, mr)` and `XmlLoader::load_from_string(xml_text, mr)` are **`Dictionary`-by-value returning + construction-time exception throwing** per `[2c §4.5]` and `[arch §5.3]`. The three thrown exception types are (extract from `[2c §4.5]`):
  - `fixpp::dict::xml_parse_error` — derived from `std::runtime_error`. Thrown on AC-L2 (unreadable path / nonexistent file), AC-L3 (malformed XML), AC-L5 (missing/non-numeric `<field number>`), AC-L6 (duplicate field number), AC-L7 (component cross-reference dangling), AC-L8 (unknown FIX type).
  - `fixpp::dict::unknown_version_error` — derived from `std::runtime_error`. Thrown on AC-L4 (FIX major/minor outside the v1.0 supported nine).
  - `fixpp::dict::xml_oom_error` — derived from `std::bad_alloc` (so a caller catching `std::bad_alloc` still gets it; common `catch (std::exception&)` paths also still get it). Thrown on AC-L9 (PMR allocation failure inside the load path); the construction-time translation goes through `fixpp::core::detail::trap_throw_or_throw<dict::xml_oom_error>` (the new exception-API sibling of `[2a §4.2]`'s `trap_throw`, added in this PR per D-3 / Gate A round 1 root cause #1).
- **Rationale:** matches the design doc verbatim and matches `[arch §5.3]`'s "exceptions are reserved for construction-time configuration errors". An `expected_t<Dictionary>` return shape was the spec's triggering-description preference but the canonical design doc (`[2c §4.5]`) overrides — design-doc-over-triggering-description is the project's convention for Phase 4 features that inherit from a signed-off `.specify/2c-*.md` (signed off via Gate A in Phase 2; see `phases/phase-4/dictionary/README.md`). The earlier draft cited `[const §XX.1]` here as a "Spec-Kit hierarchy" anchor, but `[const §XX.1]` is the amendment process, not a precedence rule — the cite was repaired in Gate A round 1 per Opus root cause #3.
- **Alternative considered (`expected_t<Dictionary>`-return):** rejected. The spec's §1 style-note explicitly defers to `[2c §4.5]`'s ratified design; revisiting that decision is an Article XX amendment, not a `/plan` choice.
- **Hot-path side.** Every public method of `Dictionary` (AC-D8) is `noexcept`. `XmlLoader` is not on the hot path — it runs once at engine init / session open per `[2c §4.5]`.

## D-5: PMR allocator policy and `mr == nullptr` handling

- **Decision:** every `XmlLoader::load*` overload takes a non-null `std::pmr::memory_resource* mr`. Null is a **caller precondition violation, not a runtime-checked error**:
  - In debug builds: `assert(mr != nullptr)` at the entry point.
  - In release builds: undefined; the very first `mr->allocate(...)` traps with a deref and the load aborts. No `expected_t` channel for this case.
- **Rationale:** `[2c §6.1.1]` mandates a non-null PMR for `with_overlay`; the same discipline applies here. Surfacing a null check as a runtime error would (a) inflate the AC-D8 noexcept surface, (b) cost a branch in the loop, (c) duplicate `[arch §5.2]`'s implicit precondition that callers pass a valid `mr`. The spec's "Edge Cases" line (§3 last entry) ratified this disposition at `/clarify` (it was not raised as a `[NEEDS CLARIFICATION]` because the design doc had already settled it).
- **AC-P1 accounting.** "Every byte of the resulting `Dictionary`'s metadata storage" — the test seam (`pmr_allocation_tracking_resource`) counts only the **output** `Dictionary`'s storage. pugixml's transient DOM lives outside that accounting (it goes through `malloc/free`, not `new`, so NFR-002-2's "zero allocation against the global `new`" remains satisfied — see D-1 rationale).
- **AC-L9 / AC-P2 translation.** A PMR `allocate` that throws `std::bad_alloc` from inside `XmlLoader::load*` is wrapped in `fixpp::core::detail::trap_throw_or_throw<dict::xml_oom_error>` (NEW helper added in this PR per D-3) and re-thrown as `dict::xml_oom_error`. The existing `trap_throw` (which returns `expected_t<T>`) is the wrong shape for the exception-API carve-out — it would swallow `bad_alloc` into an `expected_t` rather than translate it into the typed exception the spec promises. The new `trap_throw_or_throw<E,F>` sits next to `trap_throw` in `include/fixpp/core/decimal_helpers.hpp`; semantics: catch `std::bad_alloc` → throw `E{}`; rethrow any other exception unchanged.

## D-6: Determinism — sorted `FieldRef` storage (resolves NFR-002-4)

- **Decision:** the loader emits `FieldRef[]` arrays sorted **first by `MsgType` (bytewise lexicographic order over the raw `unsigned char` bytes of the FIX `MsgType` string — explicitly `std::ranges::lexicographical_compare` over `unsigned char`, NOT locale-aware or case-insensitive), then by `tag` ascending** in the merged metadata block. The `MessageEntry` comparator that drives `Dictionary::messages()` uses the same bytewise ordering. `ComponentRef[]` is sorted by component name (same bytewise UTF-8 byte comparison). `GroupRef[]` is sorted by `no_tag` ascending. `Dictionary::messages()` iteration walks the MsgType-sorted view, producing a deterministic order across runs **and** across machines (the bytewise rule is locale-independent — Gate A round 1 P2.4 closed the gap where the earlier "lexicographic" phrasing left room for locale-aware comparators that would have broken NFR-002-4 cross-machine determinism under e.g. `tr_TR.UTF-8`).
- **Rationale:** an unordered-by-insertion approach would mean two loads of the same XML in one process produce structurally equal but iteration-order-different `Dictionary` values, breaking NFR-002-4 (determinism) and AC-D5's expected sorted-by-MsgType promise. Sorting at load time is a one-shot O(N log N) on values whose count is in the hundreds (FIX 5.0 SP2 has ≈1700 fields, ≈100 message types, ≈30 components) — far under the NFR-002-1 ≤500 ms budget.
- **Alternative considered:** preserve XML declaration order. Rejected: QuickFIX-XML files in upstream have organic / historical orderings (component declarations interleaved with messages); preserving that means an unreviewed XML edit upstream silently shuffles iteration order in our `Dictionary`. Determinism wins.

## D-7: Threading posture for `XmlLoader` and `Dictionary` (loader-MVS subset)

- **Decision:** `XmlLoader` is stateless (no member state per `data-model.md` Entity 5), so calling `load` from N threads on the same `XmlLoader` value is **safe** — there is no shared mutable state to race on. Two threads each holding their own `XmlLoader{}` value are also safe (no global / static state per spec §3 "Edge Cases" #1). The returned `Dictionary` is **frozen-after-return**: every public method is `const` and `noexcept` (AC-T1, AC-D8); a single `Dictionary` can be shared read-only across N threads without locking (AC-T2). (Gate A round 1 P2.3: the earlier draft claimed same-instance `load` was UB; that claim had no basis for a stateless value type and was withdrawn.)
- **Rationale:** matches `[2c §6.1.1]`'s "move-only-on-init, frozen-after-first-handoff, thread-safe-on-read" discipline. TSan harness `tests/dictionary/concurrent_readers_test.cpp` (seam #6) verifies the read-side invariant by spawning N reader threads against one shared `Dictionary`. The construction-side discipline is verified by inspection (no `mutable`, no static state in `XmlLoader`).
- **Out of scope here.** `Dictionary::with_overlay` concurrency rules per `[2c §6.1.1]` and N-P2-3 are deferred with F2 (overlay deferred entirely per /clarify Q2).

## D-8: Loader scope cuts ratified at /clarify (F1, F2 named here; F3 closed by D-1)

- **F1 — five runtime-XML-only FIX versions (FIX40 / FIX41 / FIX43 / FIX50 / FIX50SP1).** XML data files and per-version headline tests deferred. The loader code path covers **all nine v1.0-supported versions structurally** per AC-L4 — the parser, version string parsing, error reporting, and `dict::unknown_version_error` translation already accept the full nine-version set. F1 ships ~1 PR per version (XML data + parameterized headline tests; no new loader code).
- **F2 — `XmlLoader::load_overlay*` and `DialectOverlay` end-to-end.** Absent from the public header in this PR per `/clarify` Q2 → A. `Dictionary::with_overlay`, the `[2c §4.4.1]` grammar closure, the `[2c §6.4]` additive-merge contract, and the overlay allocation-timing seam all defer to a dedicated D-009 feature. Adding `load_overlay` + `load_overlay_from_string` later is **source-compatible by C++ language rule** (added member functions can never invalidate existing call sites) and stays within the `[arch §9.3]` "Stable from v1.0" tier. (The earlier draft cited `[arch §9.2]` here for "additive method addition = non-breaking ABI", but `[arch §9.2]` covers SemVer macro emission, not C++ ABI additivity; repaired in Gate A round 1 per Opus root cause #3.) Per user direction (2026-05-14): the gap is explicitly named in `spec.md §10 F2` so any future review of `XmlLoader`'s public surface sees the unfinished extension.
- **F3 — XML parser selection.** **Closed by D-1 (pugixml).** No follow-up needed; the choice is reviewed at Gate A round 1.

## D-9: Test seam → file map (10 seams; closes spec.md §9)

This is the analogue of 001-core-decimal's plan.md "Test seam → file mapping" table, which was the answer to a Gate A round-1 root cause. Closing the same gap here pre-emptively avoids the same finding.

| Seam # | spec.md §9 description | On-disk path(s) | NFR / AC linkage |
|---|---|---|---|
| 1 | `XmlSource` mock — `load_from_string` covers in-memory testing without on-disk fixtures | (no dedicated test file; surface is `xml_loader.hpp::load_from_string`; exercised by seam #7) | AC-L10; used by every AC-L3..L8 negative-path test |
| 2 | `pmr_allocation_tracking_resource` — counts allocations against the global `new` for AC-P1 + AC-P2 | `tests/dictionary/pmr_allocation_test.cpp` + `tests/support/pmr_allocation_tracking_resource.hpp` (NEW header; reused by future dict tests) | AC-P1, AC-P2, NFR-002-2 |
| 3 | Clock seam — N/A on the load path | (no file — documented for completeness per spec.md §9) | — |
| 4 | `FieldRef` / `ComponentRef` / `GroupRef` shape static_assert seam | `tests/dictionary/ref_shape_test.cpp` | AC-F1, AC-F2, AC-F3, AC-F4, AC-F5 (`[2c §4.1]` / `[2c §4.2]` static_asserts) |
| 5 | Determinism oracle — load FIX44.xml twice, hash iteration order, assert equal | `tests/dictionary/determinism_test.cpp` | NFR-002-4 |
| 6 | Concurrent-reader TSan harness | `tests/dictionary/concurrent_readers_test.cpp` | AC-T1, AC-T2, NFR-002-3 |
| 7 | Negative-path XML samples — one per AC-L2..L8 / L10 | `tests/dictionary/negative_paths_test.cpp` (XML literals inline as `load_from_string` arguments — no fixture-file proliferation per spec.md §3.2 user-story rationale) | AC-L2, AC-L3, AC-L5, AC-L6, AC-L7, AC-L8, AC-L10 |
| 8 | Round-trip seam — load FIX44.xml, iterate every `(MsgType, tag)`, look up by `field_ref(MsgType, tag)` (canonical) AND its AC-D2 `std::optional<FieldRef>` alias `field(MsgType, tag)`, assert idempotent | `tests/dictionary/round_trip_test.cpp` | AC-D1, AC-D2, AC-D5 (carries the §3.3 user-story 3 "codegen consumer" check) |
| 9 | Allocator-failure injection — `pmr::memory_resource` that throws `std::bad_alloc` on Nth allocate | `tests/dictionary/oom_injection_test.cpp` + `tests/support/failing_pmr_resource.hpp` (NEW header; reused by future dict tests) | AC-L9, AC-P2 |
| 10 | XML-parser-error injection — crafted XML triggering the underlying parser's error path | `tests/dictionary/parser_error_test.cpp` (subset of seam #7's negative-path file — kept separate so the parse-translation contract is testable in isolation) | AC-L3 (translation: pugixml's `xml_parse_result` → `dict::xml_parse_error`) |

Cross-cutting per-AC unit tests (not "seam files" in §9's sense, but bind one AC family to one file per the 001 plan exemplar):

| File | ACs covered |
|---|---|
| `tests/dictionary/xml_loader_test.cpp` | AC-L1 (positive-path FIX44 load) — the MVP test; smoke-replaces the existing `dictionary_smoke_test.cpp`. |
| `tests/dictionary/dictionary_lookup_test.cpp` | AC-D1..D7 (parameterized over the four codegen-target versions per /clarify Q1 → B). |

Per `tasks.md`-input convention from the 001 exemplar: every row above becomes a TDD task in `/tasks`.

## D-10: Error variants added to `fixpp::core::error` (resolves §6.7 alignment with spec naming)

- **Decision:** three new variants added to `fixpp::core::error` (the existing 001-shipped enum in `include/fixpp/core/error.hpp`):
  - `dict_xml_parse_failed` — the enum-side mate of `dict::xml_parse_error` exception.
  - `dict_unknown_version` — the enum-side mate of `dict::unknown_version_error`.
  - `dict_xml_oom` — the enum-side mate of `dict::xml_oom_error`.
- **Relation between enum variants and exception types.** Per `[2c §4.5]`'s exception-API carve-out, the loader's user-visible failure surface is the three exception types. The enum-side variants exist for two reasons: (a) `[2c §6.7]` lists them as additive entries to the engine-wide error vocabulary so 2i has a stable C-ABI mapping target later; (b) future `noexcept`-API entry points that internally route through `XmlLoader` (none in this PR, but reserved per `[2c §6.1.1]` table row 5) would surface the enum form via `expected_t`. Each exception's constructor stores the matching enum variant in a `fixpp::core::error code() const noexcept` accessor so a top-level handler can route by `code()` instead of by `dynamic_cast`.
- **Naming reconciliation (spec ↔ design).** The spec.md §1 paragraph uses the **exception** names (`dict::xml_parse_error` / `dict::unknown_version_error` / `dict::xml_oom_error`). `[2c §6.7]` uses the **enum-variant** names (`dict_xml_parse_failed` / `dict_unknown_version` / `dict_xml_oom`). Both are correct in their context; the exception types **carry** the enum variants. The header `include/fixpp/dict/error.hpp` declares both forms. No spec amendment needed.
- **`[const §X.4]` forwards-compat.** Additive enum extensions inside a published `FIXPP_C_ABI_VERSION_MAJOR == 1` need the engine-side downgrade to `FIXPP_ERR_UNKNOWN` for consumers built against an older minor — owned by 2i, not by this PR. We add the three values to the audit-trail file `tools/abi_history/error_codes_v1.txt` per `[const §X.4]` when 2i lands; in the interim they live only on the C++ side, so audit-trail update is deferred and not Gate-B-blocking for this feature.
- **Audit-trail waiver rationale (Opus Gate A round 1 P2.1).** `[const §X.4]` describes the audit-trail file `tools/abi_history/error_codes_v1.txt` as the mechanism preventing accidental re-definitions across C-ABI minor versions; reading literally, audit-trail update should land *with* the variants. The waiver here is narrow and time-bounded: (a) **no C-ABI surface lands in this PR** (spec §5 — `fixpp_dict_t` is owned by 2i), so the variants are C++-only enum values whose numeric slots (20–22) have no C-ABI consumer to track; (b) **slot assignment is irreversible** — once recorded in `core/error.hpp` on `main`, the slots are pinned, and the 2i audit-trail file picks up the existing pins verbatim when it lands; (c) **no Gate-B blocker today** because no `abidiff` check fires (`[const §IX.5]`) — the C-ABI release tag does not exist yet. The waiver auto-expires at the first commit that adds a C-ABI surface consuming `dict_*` variants (2i landing), at which point `tools/abi_history/error_codes_v1.txt` must include them.

## D-11: Field-name lookup case-sensitivity (resolves spec.md §A2)

- **Decision:** `Dictionary::field_by_name(std::string_view)` per AC-D3 is **case-sensitive exact match** against the XML `<field name="...">` attribute. Callers normalize upstream if they need case-insensitive matching.
- **Rationale:** matches QuickFIX-format convention. The XML files in `dictionaries/` use Pascal-case (`ClOrdID`, `Symbol`); a case-insensitive search would mask typos rather than catch them at integration time. A future v1.x case-insensitive overload is additive (D-10-style) and non-breaking.

## D-12: Layer-edge discipline — `dictionary → core` only (closes NFR-002-6)

- **Decision:** the only new edge in `tools/check_layers.py`'s ALLOWED map is `"dictionary": {"core"}` — and that edge is **already present** on `main` (the existing `src/dictionary/CMakeLists.txt` declares `target_link_libraries(fixpp_dictionary INTERFACE fixpp_core)`). **No `tools/check_layers.py` change is required by this PR.** NFR-002-6 is satisfied by inspection.
- **Verification:** CI's `linux-clang-debug` preset runs `tools/check_layers.py` automatically; a regression that adds `#include <fixpp/wire/...>` from a dictionary header would fail Tier-1.

## D-13: No mid-session swap of any dictionary state

- **Decision:** the loaded `Dictionary` is the entire mutation surface this PR exposes; there is no in-place mutator, no overlay-merge entry point (F2), and no version-registry shape (`[2c §4.9]` deferred). The `Dictionary` is value-typed and **move-only** per spec.md §A3 (Gate A round 1 P-rescue-1.3: the earlier "copies are deep" wording contradicted `contracts/dictionary.hpp:79-80` which deletes both copy ctor and copy-assign; spec §A3 was rewritten to match the contracts).
- **Rationale:** matches `[2c §7.2]` and `[arch §5.6]`'s mid-session-swap rejection. Out of scope; recorded so a future reviewer doesn't expect a swap path.

## D-14: Header surface published in this PR

Eight header files (per spec.md §7), all under `include/fixpp/dict/`:

| Header | Source | Status |
|---|---|---|
| `field_ref.hpp` | literal extract from `[2c §4.1]` | NEW |
| `component_ref.hpp` | literal extract from `[2c §4.2]` `ComponentRef` | NEW |
| `group_ref.hpp` | literal extract from `[2c §4.2]` `GroupRef` | NEW |
| `dictionary.hpp` | subset of `[2c §4.3]` — the loader-output `Dictionary` surface only (no `with_overlay`, no `as_table_view` until F2 / `dict::table_view` ship); plus `dict::field_data_type` (renamed from `[2c §4.1]` `data_type` to avoid polluting `fixpp::dict::` with a generic name) and `dict::field_presence` (renamed from `[2c §4.1]` `presence`) | NEW |
| `xml_loader.hpp` | literal extract from `[2c §4.5]` — `load` + `load_from_string` only; `load_overlay*` deferred per F2 | NEW |
| `error.hpp` | exception types (`xml_parse_error`, `unknown_version_error`, `xml_oom_error`) + enum-variant accessors per D-10 | NEW |
| `version_profile.hpp` | subset of `[2c §4.3]` — just `session_version` and `application_version` enums; full `version_profile` struct + `resolve_application_version` deferred to the wire/session integration feature | NEW |
| (header-only; no separate `dictionary_fwd.hpp` in v1.0) | — | — |

Three `include/fixpp/core/` changes are admitted in this PR (per D-3 — both additive enum variants in `core/error.hpp` and an additive helper template `trap_throw_or_throw<E,F>` in `core/decimal_helpers.hpp`; no new core/ header files); no `include/fix/c_api/dict_*.h` is added (per spec §5 "C ABI surface for `Dictionary` … not part of this PR").

## D-15: pugixml integration mechanism

- **Decision:** add pugixml via Conan (`pugixml/1.14`) as a normal third-party dependency; pull `pugixml.hpp` only from the loader's `.cpp` (`src/dictionary/xml_loader.cpp`) — **not** from any public header in `include/fixpp/dict/`. The dependency is a strict implementation detail.
- **Public-header consequence.** `include/fixpp/dict/xml_loader.hpp` declares `XmlLoader` as a forward-declaration / opaque-by-value type whose only state is `std::pmr::memory_resource*` (passed at `load*` call time). Downstream TUs `#include <fixpp/dict/xml_loader.hpp>` without ever pulling pugixml. This keeps the library's *transitive* dependency surface at zero per `[arch §7.3]`.
- **Conan profile update.** `conanfile.py` gains a `requires("pugixml/1.14")` line; lockfiles regenerated under all Phase-3 profiles. Per `[const §III.2]` Conan rows are declared with a pinned version.
- **`[const §V.3]` licence anchor.** `[const §V.3]` reads (verbatim): "No LGPL dependencies. Viral linkage is incompatible with the dual-license model." That is a licence rule, not an admission procedure. pugixml is MIT, so the rule is satisfied. The admission *procedure* (user sign-off at /plan + Codex Gate A review of the choice) is project convention, not a constitution clause; earlier drafts of this note cited `[const §V.3]` as if it specified a procedure — that was a broken citation, repaired in Gate A round 1 per Opus root cause #3.

## D-16: pugixml allocator hook scoping (resolves AC-P1 transient-DOM accounting)

- **Decision:** **do not** route pugixml's transient DOM allocations through `mr`. pugixml's default uses `malloc/free`; we leave it as-is. AC-P1 (`every byte of the resulting Dictionary's metadata storage from mr`) is satisfied by walking the parsed DOM and emitting the `FieldRef[]` / `ComponentRef[]` / `GroupRef[]` output arrays into `mr->allocate(...)`-backed `std::pmr::vector`s. The parser's transient buffers are released when `xml_document` goes out of scope at the bottom of `load`.
- **Rationale for not hooking pugixml:** `pugi::set_memory_management_functions` is **process-global**; we'd need a thread-local current-`mr` pointer to route per-load. `thread_local` is banned per `[const §XV]`. A global mutex + static `current_mr_for_loader_` is awful — serialises loads needlessly and produces test-pessimal seams. The transient cost is small (XML byte size, not output metadata size) and is *not* what AC-P1 tracks.
- **NFR-002-2 ("zero allocation against the global `new`") accounting.** pugixml goes through `malloc/free`, not `operator new`, so the test seam (which intercepts `new`) reports 0 for the parser's transient DOM by construction. The seam catches an accidental `auto* buf = new char[...]` inside our loader code or inside accidentally-pulled-in third-party. This is the desired invariant.
- **Future revisit.** If a future operator pushes for full-arena discipline (every load byte through `mr`), the path is: subclass `pugi::xml_document`, override `_alloc` / `_free`, route through a stack-stored `xml_loader_state.mr_`. Tracked as an internal hardening followup, **not** Gate-B-blocking.

## D-17: Tier-1 CI presets this PR is exercised on

- **Decision:** AC-* and NFR-002-* are gated on the Tier-1 preset matrix per `[const §IX.6]`:
  - `linux-clang-debug` — every test target.
  - `linux-clang-release` — every test target except TSan-only (concurrent-readers); plus the bench harness regression bar per NFR-002-1.
  - `linux-clang-asan` — every test target.
  - `linux-clang-ubsan` — every test target.
  - `linux-clang-tsan` — concurrent-readers test target specifically (AC-T2 / NFR-002-3).
  - `linux-clang-coverage` — coverage threshold ≥90 % line / ≥80 % branch on the new `src/dictionary/*` and `include/fixpp/dict/*` files per `[const §IX.1]`.
  - `linux-gcc-release` — sanity build (GCC tolerates our header surface).
- **Tier-2 (`windows-msvc-debug` / `windows-msvc-release`):** manual / nightly per `[const §IX.6]`. The XmlLoader does not exercise C-ABI surface (spec §5), so no abidiff golden in this PR — `[const §IX.5]` ABI check is N/A here.
- **Pre-PR local gate per `[const §XVII.7]`.** Contributor confirms `local build: green on linux-clang-debug @ <git-sha>` in the PR body before opening.

## D-18: Bench harness shape (resolves NFR-002-1 verification)

- **Decision:** `bench/dictionary/xml_loader_bench.cpp` (NEW) uses Google Benchmark per `[const §VIII.1]`; benches three loads — FIX44 (the canonical reference), FIX42 (smallest), FIX50SP2 (largest of the four). Median across 100 iterations on warm filesystem cache (the first load primes the page cache). Failure budget: **regression > 5 %** vs `bench/baselines/dictionary/xml_loader.json` per `[const §VIII.2]`; absolute ceiling **1 s** on the slowest preset (FIX50SP2 / Debug / WSL2) per spec.md §6 NFR-002-1.
- **Baseline first-cut.** The baseline file is written on the first green CI run that includes this bench; subsequent PRs compare against it. Same protocol as 001-core-decimal's bench.

## D-19: Gate A / Gate B precondition mapping (resolves spec.md §12 DoD)

- **Gate A trigger (`[const §XVII.1]`):** touches the public C++ API. `gate_a_required: yes` already declared in `spec.md` front-matter. Gate A round 1 runs after this `/plan` lands; both Codex passes (rescue + `/codex:adversarial-review`) per `feedback_gate_a_codex_dual_pass.md` (auto-memory).
- **Gate B trigger (`[const §XVII.2]`):** every PR. Independence rule per `[const §XVII.3]` — author (Opus / Sonnet) ≠ reviewer (Codex), in fresh sessions.
- **`/speckit-verify` precondition (`[const §XVII.8]`):** runs after `/speckit-implement` produces `tasks.md` `[X]` rows; verdict GREEN required to apply the `gate-b-done` label. Tier-1 preset matrix per D-17. Decision record at `.specify/decisions/002-dictionary-xml-loader-verify.md`.
- **Trigger-set evaluation per `[const §XVI.3]` / `[const §XVI.4]`:** the loader **touches "Wire format / parser"** in the Appendix A trigger set (it parses XML, a structured wire-adjacent format) **and** the public C++ API. `/clarify` ran 2026-05-14 (3 questions answered); `/analyze` is the user-visible next step after Gate A round 1 passes per `[const §XVI.4]`.

## D-20: spec ↔ design naming alignment notes (no spec amendment needed)

Three accessor-name divergences between `spec.md §4.2` (AC-D*) and `[2c §4.3]`. Both forms are kept in this PR — the **design-doc names** are the canonical method signatures emitted in `contracts/dictionary.hpp`; the **spec-language names** are descriptive aliases the AC tests target through a thin facade in `dictionary.hpp` (one-line `[[nodiscard]] auto field(...) const noexcept { return field_ref(...); }`-style wrappers). Both surfaces ship in v1.0; the descriptive aliases match the user-visible ergonomics in the spec's narrative, the canonical names match `[2c §4.3]`.

| Spec AC name (spec.md §4.2) | `[2c §4.3]` canonical name | Disposition |
|---|---|---|
| `field(MsgType, uint16_t tag)` (AC-D2 spec-language alias) | `field_ref(std::string_view msg_type, std::uint16_t tag)` | Both names ship; canonical is `field_ref` (returns `FieldRef` with `rule == NotDeclared` when absent); the AC-D2 spec-language alias wraps it in `std::optional<FieldRef>` for descriptive ergonomics. AC-D1 (spec.md round-1) canonicalizes on `field_ref(msg_type, tag)`: there is no context-free `field(tag)` — one `FieldRef` exists per `(MsgType, tag)` pair per `[2c §4.1]`, so a global-tag lookup has no canonical answer. The earlier "synthetic '*' msg_type slot" idea was withdrawn in Gate A round 1 (Opus root cause #2). |
| `field_by_name(string_view name)` | (not in `[2c §4.3]`) | New aliased method, lives in `dictionary.hpp`; supplements the design doc. |
| `component(string_view name)` / `group(uint16_t no_tag)` | (the design doc exposes `component_ref` / `group_ref` indirectly via `field_ref`'s `component_index` / `group_no_tag`; `group_first_field` and `length_pair_data_tag` are the closer canonical methods) | New aliased methods; thin wrapper over the canonical accessors. |
| `messages()` | (not in `[2c §4.3]`) | New aliased method; returns a span into the metadata block's MsgType list. |

Documenting the bridge here so Gate A round 1 reviews can confirm the additions are non-shape-changing and forwards-compatible with the rest of the `[2c §4.3]` surface (`with_overlay`, `as_table_view`, `resolve_application_version`) when F2 lands.

## Citation verification pass (round 1)

| Cite | Resolves to | OK |
|---|---|---|
| `[const §I.1]` | `constitution.md:11` — "fixpp is a modern C++23 implementation of the FIX protocol." (v1.0 version surface) | ✅ |
| `[const §II.1]` | `constitution.md:26` — "Language standard: C++23." | ✅ |
| `[const §III.2]` | `constitution.md:41` — "Dependency manager: Conan." | ✅ |
| `[const §V.1]` | `constitution.md:66` — "fixpp library: AGPL-3.0 + commercial dual." | ✅ |
| `[const §V.3]` | `constitution.md:68` — "No LGPL dependencies." (the third-party-deps procedure cited in `spec.md §11 R1`) | ✅ |
| `[const §VIII.1]` | `constitution.md:99` — "Bench framework: Google Benchmark." | ✅ |
| `[const §VIII.2]` | `constitution.md:100` — "Regression budget: ±5 %." | ✅ |
| `[const §VIII.5]` | `constitution.md:106` — "Allocator policy on the hot path: zero new/delete." | ✅ |
| `[const §IX.1]` | `constitution.md:113` — "Coverage thresholds." | ✅ |
| `[const §IX.4]` | `constitution.md:119` — "Static analysis — Tier 1." | ✅ |
| `[const §IX.5]` | `constitution.md:124` — "ABI check (from the first tagged C ABI release onward)." | ✅ (cited only to note it is N/A for this PR) |
| `[const §IX.6]` | `constitution.md:125` — "Two-tier CI." | ✅ |
| `[const §X.4]` | `constitution.md:136` — "Error reporting at the C ABI." | ✅ |
| `[const §XV]` | `constitution.md:203` — "Banned patterns." (cited for `thread_local` ban) | ✅ |
| `[const §XV.12]` | `constitution.md:218` — "LGPL dependencies." | ✅ |
| `[const §XVI.3]` | `constitution.md:234` — "/clarify is MANDATORY before /plan for any feature that touches: ABI, threading, error semantics, wire format, codegen, session FSM, or security." | ✅ |
| `[const §XVI.4]` | `constitution.md:235` — "/analyze is MANDATORY for the same trigger set as /clarify." | ✅ |
| `[const §XVII.1]` | `constitution.md:245` — "Gate A — Design review." | ✅ |
| `[const §XVII.2]` | `constitution.md:255` — "Gate B — PR review." | ✅ |
| `[const §XVII.3]` | `constitution.md:257` — "Independence rule." | ✅ |
| `[const §XVII.7]` | `constitution.md:265` — "Local pre-PR build gate." | ✅ |
| `[const §XVII.8]` | `constitution.md:270` — "Verification gate (/speckit-verify)." | ✅ |

> Cites repaired in Gate A round 1 per Opus root cause #3:
> - `[const §V.3]` — was cited as a "third-party-deps procedure"; the constitution text is only "No LGPL dependencies." Cite repaired to the licence anchor only; the admission procedure is project convention (user sign-off at /plan + Codex Gate A review).
> - `[arch §9.2]` — was cited for "additive method addition = non-breaking ABI"; `[arch §9.2]` covers SemVer macro emission, not C++ ABI additivity. Replaced with the C++ language-rule justification + `[arch §9.3]` "Stable from v1.0" tier.
> - `[const §XX.1]` — was cited for a "Spec-Kit hierarchy"; `[const §XX.1]` is the amendment process, not a precedence rule. Replaced with the project-convention reference (`phases/phase-4/dictionary/README.md`).

Cross-doc cites (`[2a §4.2]`, `[2c §1.3]`, `[2c §4.1]`, `[2c §4.2]`, `[2c §4.3]`, `[2c §4.4.1]`, `[2c §4.5]`, `[2c §6.1.1]`, `[2c §6.4]`, `[2c §6.7]`, `[2c §7.2]`, `[2c §9]`, `[arch §4.2]`, `[arch §5.2]`, `[arch §5.3]`, `[arch §5.6]`, `[arch §7.3]`, `[arch §9.3]`, `[FIX50SP2 §3.3]`, `[FIXT §5.1]`, `[FIXT §5.3]`) inherited verbatim from `spec.md §13` References and the design docs themselves.

All `[const §X.Y]` citations in this Phase 0 document resolve under canonical form. No Constitution Check violations; no Complexity Tracking entries to justify.
