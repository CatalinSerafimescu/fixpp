---
id: 003-dictionary-codegen
title: Research — Dictionary codegen design decisions
spec_kit_step: /plan Phase 0
last_updated: 2026-05-15
status: drafted (round 1)
inherits_design: .specify/2c-codegen.md v1.3 (signed off 2026-05-10; Gate A converged)
inherits_spec: specs/003-dictionary-codegen/spec.md (carries /clarify Q&A 2026-05-15)
---

# Phase 0 Research — 003-dictionary-codegen

All shape decisions for the typed-message class (`[2c §4.7]`), the `owning_message_t<>` / `dict::reify` bridge (`[2c §4.8]`), `dict::version_registry` (`[2c §4.9]`), the CMake target graph (`[2c §7.6]`), latency ceilings (`[2c §6.2]`), and the codegen pipeline (`[arch §4.2]`) are **inherited verbatim from `.specify/2c-codegen.md` v1.3** (signed off after Gate A convergence). This document does not re-litigate them; it records every load-bearing `/plan` decision in canonical Phase 0 format. The two `/specify`-deferred items requiring `/plan` resolution (spec §10 F1, spec §11 R6) are resolved here with user sign-off **2026-05-15**.

Citation form `[const §<Roman>.<arabic>]` per `constitution.md:5`. All cited articles re-verified against the constitution (see plan.md Citation verification pass).

## D-1: `fixpp-codegen` host tool = **C++23, reuse 002's `Dictionary` IR** (resolves spec §10 F1 — user sign-off 2026-05-15)

- **Decision:** `tools/codegen/fixpp-codegen` is a **C++23 build-only host executable** (`[const §III.5]`) that links the merged `fixpp::dict` runtime (002, PR #66 on `main`), calls `XmlLoader::load(path, mr)` to parse each checked-in `dictionaries/<VER>.xml` into a `Dictionary`, walks the resulting metadata (`FieldRef`/`ComponentRef`/`GroupRef` arrays + `version_profile`), and emits the per-version header packs (`Messages.hpp`, `Fields.hpp`, `Validator.hpp`, `Reify.hpp`, `NormativeReferences.md`) plus the two shared `_dispatch/` switch headers, through a small in-tool deterministic C++ string-templating layer (`template_writer.hpp`).
- **Rationale:**
  - **One XML truth.** The XML is interpreted exactly once, through the **already-fuzzed, already-tested** 002 loader (`tests/fuzz/fuzz_dict_xml_loader.cpp`, `[2c §9]` seam #8, shipped PR #66). No second QuickFIX-XML parser exists to drift from the runtime path — a divergence the conformance corpus (seam #1) could otherwise miss. This is the load-bearing argument.
  - **Determinism for free.** 002 already emits sorted, locale-independent (bytewise `unsigned char` `std::ranges::lexicographical_compare`) metadata (002 research D-6). The codegen tool iterates that already-sorted IR, so byte-stable header emission (NFR-003-7 / AC-T1 / AC-T2) follows by construction — no separate sort/locale discipline to re-implement and re-review.
  - **Single toolchain, zero new deps.** C++23/Clang/Conan is already mandated (`[const §II.1]` / `[const §III.2]`). The tool links `fixpp::dict` (which already transitively pulls `pugixml/1.14` from 002); **no new Conan row, no new build-time dependency, no `[const §V.3]` licence surface added.**
  - **No bootstrap cycle.** The tool depends only on the **merged 002 runtime**, never on the headers it generates. Build order: `fixpp::dict` (002) → `fixpp-codegen` (host exe) → configure-time `fixpp::dict::generate-vXX` runs the exe → per-version `INTERFACE` targets expose the generated tree. Acyclic.
- **Alternatives considered:**
  - **Python 3 + Jinja2.** Terse templating; Python 3.12 already present and used in `tools/` (`check_layers.py`, `bench_compare.py`). **Rejected:** (a) introduces a *second* XML interpretation path distinct from the 002 loader — exactly the schema-divergence risk Candidate A eliminates; (b) determinism discipline (locale-independent sort/format) must be re-implemented and re-reviewed rather than inherited; (c) adds a configure-time Python + Jinja2 dependency and supply-chain surface (Jinja2 is BSD — licence-clean per `[const §V.3]`, but it is still a new build-time requirement contrary to the "zero new deps" property of Candidate A).
  - **C++ tool + embedded template DSL (inja/{fmt}, MIT), parsing XML via pugixml directly.** Single toolchain and cleaner templating than raw string-building. **Rejected:** re-parses XML via pugixml *directly* rather than through the tested `Dictionary` IR — reintroduces the second-interpretation risk Candidate A avoids; adds a templating dependency (inja is header-only MIT — clean, but unnecessary given the templating need is small and mechanical).
- **Compile/runtime cost:** the tool is a one-shot configure-time executable; its own build is one small C++ TU plus the already-built `fixpp::dict` link. Per-version emission walks hundreds of `FieldRef`s and ~50–118 messages — sub-second per version, far under any configure-time concern.
- **Where the choice gets re-evaluated:** Gate A round 1 (Codex review of this plan), explicitly flagged in plan.md Gate A §. If Codex raises a P1 against Candidate A, the round-2 redraft falls back to Candidate C (C++ + inja, pugixml-direct) and updates D-1.

## D-2: `wire::MessageView<Index>` build-ordering = **vendor a frozen contract stub in this PR** (resolves spec §11 R6 — user sign-off 2026-05-15)

- **Decision:** ship `include/fixpp/wire/message_view_contract.hpp` — a minimal, **frozen** header providing exactly the `[2b §4.3]` / `[2b §4.7]`-locked surface that generated typed messages and `owning_<Msg>` classes compile against: `wire::MessageView<wire::access_mode::Index>` with `get<Tag>() -> expected_t<field_view>`, `get(std::uint16_t) -> expected_t<field_view>`, `group<NoTag, T>() -> group_view<T>`, `unknown_fields()`, the `wire::field_view` / `wire::group_view<T>` value types, and the `[2b §6.4]` debug generation-counter trap. `include/fixpp/wire/` currently holds only `.gitkeep` (verified on disk 2026-05-15); the 2b wire feature is downstream of `dictionary/` in module order.
- **Rationale:** the DoD (spec §12) requires **all AC-G\*/AC-V\*/AC-R\*/AC-D\*/AC-C\*/AC-T\* tests + the conformance corpus to pass on every CI preset in this PR**. A `FIXPP_WIRE_PRESENT` build guard (the rejected alternative) would emit headers but leave the entire typed-accessor/reify/conformance suite unrunnable until 2b lands — the feature would ship structurally untested, contradicting the DoD and `[const §VII.4]` ("no untested code on `main`"). Vendoring the contract is the only option that lets the feature be built and tested now.
- **Drift control (the mitigation under Gate A review):**
  1. The stub header is marked `// FROZEN CONTRACT — surface locked by [2b §4.3]/[2b §4.7]. Do not extend. 2b replaces the body, not the surface.`
  2. A contract test (`tests/codegen/wire_contract_test.cpp` — folded into `flyweight_shape_test.cpp` per the seam map) `static_assert`s the exact member signatures and the `sizeof(MessageView<Index>) == sizeof(pointer-or-handle)` invariant the typed flyweight depends on (AC-G7).
  3. When 2b lands, it replaces the **body** (real `OffsetTable`-backed implementation) against the **same** header surface; the contract test is the regression guard that catches any signature drift at compile time, in 2b's own Gate B.
- **Alternative considered (`FIXPP_WIRE_PRESENT` guard):** rejected — see Rationale; defeats the DoD.
- **Layer-edge classification (feeds D-12).** The vendored header lives under `include/fixpp/wire/` but is *consumed by generated dictionary headers as a compile-time template argument*, not via a runtime `dictionary → wire` link edge. `[2c §1.1]` / `[arch §4.2]` confirm typed messages "reuse 2b's primitives" as a header contract. Confirmed at /plan as **not** a new runtime layer edge; flagged for Gate A.
- **Re-evaluated at:** Gate A round 1 (highest-risk decision in the bundle — ships a header ahead of its owning feature). Explicitly flagged in plan.md Gate A §.

## D-3: Codegen output location = **build tree only, configure-time** (locks spec AC-T2 / AC-C4 against `[arch §4.2]` step 3)

- **Decision:** `fixpp-codegen` writes **only** under `build/<preset>/_codegen/include/fixpp/...`; never under the source tree. The CMake custom target `fixpp::dict::generate-vXX` runs at **configure time** (one invocation per codegen version), before the per-version `INTERFACE` targets' `INTERFACE_INCLUDE_DIRECTORIES` are consumed.
- **Rationale:** `[arch §4.2]` step 3 is explicit ("outputs go into the build tree, not the source tree, so a dirty checkout never carries stale codegen"). Configure-time (not build-time custom command) keeps the generated headers present before the first compile of any consumer TU and keeps the dependency graph free of build-order races (AC-C4). A dirty checkout never carries stale codegen because nothing is ever written to the source tree (AC-T2).
- **Verification:** `tests/codegen/determinism_test.cpp` asserts (a) two runs produce byte-identical output (NFR-003-7/AC-T1), and (b) `git status --porcelain` over the source tree is unchanged after a configure (AC-T2).

## D-4: Determinism mechanism = **inherited 002 sorted bytewise IR** (resolves NFR-003-7 / AC-T1)

- **Decision:** byte-stable emission is achieved by iterating the already-sorted 002 `Dictionary` IR (per-MsgType `FieldRef` arrays sorted bytewise; `ComponentRef` by name; `GroupRef` by `no_tag` — 002 research D-6) and emitting through a templating helper that performs **no locale-dependent formatting** (no `std::locale`, no `printf` with locale, integers via `std::to_chars`, fixed `\n` line endings). No post-emit sort step is needed.
- **Rationale:** reuses 002's locale-independence guarantee (the same invariant that closed 002 Gate A round 1 P2.4); the codegen tool adds no new ordering source. One checked-in golden header per codegen version (4 total — spec /clarify Q-golden → A) anchors the byte-for-byte determinism assertion; regenerating the goldens is a deliberate, Gate-A-reviewed step on any codegen-template change (NFR-003-7).

## D-5: Reify-bridge source split = **header-mostly; out-of-line `.cpp` only if required** (locks spec §7)

- **Decision:** `<fixpp/dict/reify.hpp>` and `<fixpp/dict/version_registry.hpp>` are header-mostly. `dict::reify_as<Msg>` / `dict::reify` are free function templates (template → header). `owning_message_handle`'s type-erasure vtable and any non-template helpers may need a single `src/dictionary/reify.cpp`; `version_registry`'s non-template `get` body may need `src/dictionary/version_registry.cpp`. The exact split is locked at `/tasks` after the contract headers are pinned; the `[2c §7.6]` `fixpp::dict::runtime` target absorbs whichever `.cpp` exist (it is already a compiled target from 002).
- **Rationale:** `[2c §4.8]` declares `reify_as`/`reify` as free function templates (no method added to `wire::MessageView` — AC-R1); templates must be header-visible. `owning_message_handle::as<Msg>()` and the type-erased dispatch are the only candidates for out-of-line code. Keeping the decision open to `/tasks` avoids over-committing the file list before the contract headers are reviewed at Gate A (same discipline 002 used for its `.cpp` split).

## D-6: `dict::reify` runtime-dispatch generation = **two shared `_dispatch/` headers, included once per all-versions TU** (locks AC-D1)

- **Decision:** the dispatch switches are **not** per-version. `fixpp-codegen` emits `_codegen/include/fixpp/_dispatch/reify_dispatch_fixt.hpp` (exactly the 7 FIXT admin MsgTypes `0/1/2/3/4/5/A`) and `_codegen/include/fixpp/_dispatch/reify_dispatch_application.hpp` (one case per (codegen `application_version`, MsgType) pair across v42/v44/v50sp2 — ~470 cases). They are owned by the `fixpp::dict::dispatch` CMake target which depends on `fixpp::dict::all_versions` (it references all four versions' `owning_<Msg>` types) and is included **once** per all-versions/dispatch-consuming TU, not four times (`[2c §1.2]` / `[2c §7.6]`).
- **Rationale:** matches `[2c §4.8]` / `[2c §1.2]` per-N-P2-6 sizing exactly (~50 KiB dispatch-shared, not ~370 KiB × 4). The `dict_reify_unknown_msg_type` default arm is **fail-loud** (R3 mitigation, spec §11) — a missing/wrong case returns the documented error, never misdispatches silently. Exhaustiveness is the conformance corpus's job (seam #15b: AC-G12 curated subset in CI, exhaustive nightly).

## D-7: FIXT cross-vocabulary resolution = **consume 002's `resolve_application_version`** (locks AC-D3 / AC-D4)

- **Decision:** `dict::reify` peeks `MsgType(35)`; on a FIXT-admin hit dispatches via the FIXT switch; on miss reads `ApplVerID(1128)` (mapping `dict_field_not_present` → empty `string_view`) and calls the **002-shipped** free function `dict::resolve_application_version(profile, appl_ver_id_value)` — this feature does **not** re-derive the resolution algorithm (spec §A6). The `[2c §6.3]` worked example (Logon→vt11, NOS ApplVerID=9→v50sp2, NOS ApplVerID=6→v44 override, OCR no ApplVerID→v50sp2 default, Heartbeat→vt11) is the AC-D4 oracle in `tests/integration/fixt_cross_vocabulary.cpp` (seam #10b).
- **Rationale:** `version_profile` and `resolve_application_version` are 002-shipped (spec §A6); reusing them keeps one resolution truth and avoids a divergent second algorithm. The `dict_unresolved_application_version` (default_appl == Unknown + no ApplVerID) and `dict_unknown_appl_ver_id` (non-empty, non-parsing ApplVerID) error paths are mapped per AC-D6/AC-D7 (RC#1 misdiagnosis path closed).

## D-8: Conformance corpus selection = **curated must-include manifest, checked in, Gate-A-reviewed** (resolves spec /clarify Q-subset → A; AC-G12)

- **Decision:** the CI conformance subset (seam #1 / #15b) is a **checked-in manifest** `tests/codegen/conformance/must_include_manifest.txt`, not an ad-hoc ~20-message sample. Per version it MUST list: every P1 headline message, every message declaring a repeating group, all 7 FIXT.1.1 admin MsgTypes, every message in the `[2c §6.3]` AC-D4 worked example, and msgtype-boundary probes (a filtered FIX-Latest A-035..A-065 message and a codegen-deferred A-014..A-034 message per AC-G9/AC-G10). The nightly run is exhaustive over the full emitted set. A CI subset missing any must-include class fails the conformance test.
- **Rationale:** spec /clarify 2026-05-15 → A ("Curated must-include"). A checked-in, Gate-A-reviewed manifest makes the subset auditable and prevents silent coverage erosion (R3 mitigation). Sample data is public only (QuickFIX `examples/*.dat`, public exchange specs, ICAP regression set per `[2c §9]` seam #1 / spec §A5) — no proprietary message data checked in.

## D-9: No new fuzz harness required (resolves `[const §VII.7]` trigger evaluation)

- **Decision:** this PR ships **no new libFuzzer harness**. `[const §VII.7]` ("new parser-touching code without a fuzz harness is a Gate B blocker") does not fire: F1 Candidate A introduces **no new parser** — `fixpp-codegen` consumes 002's already-fuzzed `XmlLoader::load_from_string` parse path (`tests/fuzz/fuzz_dict_xml_loader.cpp`, shipped PR #66, `[2c §9]` seam #8). The codegen emitters operate on the already-parsed, validated `Dictionary` IR (structured C++ values), not on untrusted bytes.
- **Rationale:** the only untrusted input in the codegen path is the XML, which is fuzzed at the 002 boundary. The emitters' input is a well-typed `Dictionary`; there is no byte-parsing surface in 003 to fuzz. `[2c §9]` seam #9 (overlay-merge fuzz) defers with the DialectOverlay scope (002 F2 / D-009, spec §5). This non-trigger is recorded so Gate A confirms it rather than reading the absence as a gap (the same root-cause class that bit 002's earlier draft).

## D-10: Error variants added to `fixpp::core::error` (resolves spec §4.3/§4.4/§4.5 error taxonomy)

- **Decision:** append the reify/dispatch/registry error variants to the `fixpp::core::error` enum (002 ended at slot 22 — `dict_xml_oom = 22` per 002 D-10) at unused slots ≥ 23, non-renumbering: `dict_reify_msg_type_mismatch`, `dict_reify_unknown_msg_type`, `dict_reify_oom`, `dict_unresolved_application_version`, `dict_unknown_appl_ver_id`, `dict_no_dictionary_for_application_version` (final slot numbers locked at `/tasks` after a fresh `core/error.hpp` read; the *count* and *names* are fixed here). `<fixpp/dict/reify.hpp>` and `<fixpp/dict/version_registry.hpp>` reference these variants; the exceptions/`expected_t` mapping follows the spec §4.3–§4.5 tables.
- **`[const §X.4]` forwards-compat / audit trail.** Additive only; existing slots (`out_of_memory=1`, `decimal_*=10..13`, 002's `dict_xml_*=20..22`) preserved verbatim — the 001 ABI golden and existing consumers compile unchanged. The C-ABI mapping and `tools/abi_history/error_codes_v1.txt` update are owned by **2i** (no C-ABI surface in this PR — spec §5); the audit-trail update is deferred under the **same narrow, time-bounded waiver shape as 002 D-10**: (a) no C-ABI surface lands here; (b) slot assignment is the irreversible commitment, picked up verbatim by 2i; (c) no `abidiff` check fires (`[const §IX.5]` N/A). Waiver auto-expires at the first commit adding a C-ABI surface consuming `dict_reify_*`.
- **Rationale:** mirrors 002 D-10's additive discipline exactly; keeps the engine-wide error vocabulary the single source `[2c §6.7]` mandates.

## D-11: `owning_<Msg>` move semantics = **custom `noexcept` move, caches reset both sides** (locks AC-R4 against R4)

- **Decision:** each generated `owning_<Msg>` is move-only (copy deleted), with a **custom `noexcept` move ctor/assign** (explicitly **not** `= default`) that resets *both* source and destination `frame_cache_`/`view_cache_` to `std::nullopt`; the moved-to instance rebuilds its lazy `view()` against post-move `bytes_.data()` on first access. No reference members. `static_assert`: `is_nothrow_move_constructible_v`, no reference members, move ctor not trivial/defaulted (`tests/dictionary/reify_move_test.cpp`, seam #14).
- **Rationale:** `[2c §4.8]` / N-P1-3 / R4 (spec §11): a defaulted move on `std::optional`-typed caches would leave a stale cache aliasing pre-move `bytes_`. The custom move is the documented mitigation; the static-asserts catch a future codegen-template regression that accidentally reintroduces a defaulted move.

## D-12: Layer-edge discipline (closes NFR-003-8)

- **Decision:** **no new runtime layer edge.** (a) `dictionary → core` already exists from 002 — unchanged. (b) Generated typed messages consume the vendored `wire::MessageView<Index>` *contract header* as a **compile-time template argument** (D-2), not via a runtime link to a `wire` library — `[2c §1.1]`/`[arch §4.2]` confirm "typed messages reuse 2b's primitives" as a header contract. (c) The `fixpp-codegen` host tool links `fixpp::dict` — a **host-side build edge** (`[const §III.5]` build-only), not a library layer edge; `tools/check_layers.py` scopes the library, not `tools/`. **No `tools/check_layers.py` change required**; the file is *reviewed* for the host-edge classification and the contract-header classification, both flagged for Gate A.
- **Verification:** CI's `linux-clang-debug` runs `tools/check_layers.py`; a regression adding a real `#include <fixpp/wire/...>` runtime edge from a non-generated dictionary header would fail Tier-1.

## D-13: No mid-session dictionary swap; no DialectOverlay path (locks spec §5)

- **Decision:** this PR ships only the `field_value(uint16_t)` forwarder (AC-G6) so overlay-promoted tags are *reachable*, not *typed*. No `DialectOverlay`/`with_overlay`/overlay-regen codegen path (`[2c §4.4]`/`[2c §6.4]`/`[2c §4.7.1]` path 2) — owned by D-009 (002 follow-up F2, spec §5). Recorded so a future reviewer does not expect an overlay merge here.
- **Rationale:** matches `[2c §7.2]`/`[arch §5.6]` mid-session-swap rejection and spec §5 scope cut; the overlay surface itself defers with 002 F2.

## D-14: `dict::version_registry` ships shape only (locks AC-X3 against spec §5 / `[2c §10]` Q10)

- **Decision:** `<fixpp/dict/version_registry.hpp>` declares `class version_registry` with `[[nodiscard]] expected_t<Dictionary const*> get(application_version) const noexcept [[clang::lifetimebound]]` and the `dict_no_dictionary_for_application_version` error (distinct from `dict_unknown_appl_ver_id`, a wire-string parse failure). It is a **concrete value type, not a virtual interface** (`[const §XIV.2]` N/A — recorded). The ownership/construction model (engine-owned-by-value vs session-borrowed; `EngineConfig::dictionaries`) is **deferred to 2d** (`[2c §10]` Q10; spec §10 F3). A minimal in-test harness exercises `get` against a hand-constructed registry; no engine wiring (`tests/dictionary/version_registry_test.cpp`).
- **Rationale:** `[2c §4.9]` / `[2c §10]` Q10 explicitly defers ownership to 2d threading + EngineConfig design; v1.0/v1.1 publishes only the `get` shape (AC-X1..X3).

## D-15: Test seam → file map (closes spec.md §9)

The analogue of 001/002's plan.md "Test seam → file mapping" table — the answer to a recurring Gate A round-1 root cause. The full table is in `plan.md` ("Test seam → file mapping"). Every `[2c §9]`-derived seam in spec §9 binds to a named on-disk file; the absent seams (#4/#8/#9/#11/#13/#17/#20) are the deliberate DialectOverlay / already-shipped-XmlLoader-fuzz / table_view exclusions documented in spec §9, not omissions. Per the `tasks.md`-input convention, every row becomes a TDD task in `/tasks`.

## D-16: Golden-header anchoring = **one per codegen version (4 total)** (resolves spec /clarify Q-golden → A; NFR-003-7 / R5)

- **Decision:** a checked-in golden header per `v42`/`v44`/`v50sp2`/`vt11` lives under `specs/003-dictionary-codegen/contracts/golden/` (`<vXX>_Messages.golden.hpp`); `tests/codegen/determinism_test.cpp` asserts byte-identical re-emission against all four. Regenerated as a deliberate, Gate-A-reviewed step on any codegen-template change (the regeneration diff is the review surface).
- **Rationale:** spec /clarify 2026-05-15 → A ("one per version, 4 total"). `Messages.hpp` is the most accessor-shape-sensitive artifact, so it is the golden of record; a template regression that shifts a single accessor body shows up as a golden diff in review (R5 mitigation, spec §11).

## D-17: Tier-1 CI presets this PR is exercised on

- **Decision:** AC-* / NFR-003-* gated on the Tier-1 matrix per `[const §IX.6]`:
  - `linux-clang-debug` — every test target; `tools/check_layers.py`.
  - `linux-clang-release` — every test except TSan-only; the three bench harnesses' regression bars.
  - `linux-clang-asan` / `linux-clang-ubsan` — every test target.
  - `linux-clang-tsan` — `reify_cross_strand_test` specifically (AC-R5 / AC-T3 / seam #12).
  - `linux-clang-coverage` — ≥90 % line / ≥80 % branch on `tools/codegen/*`, `include/fixpp/dict/reify.hpp`/`version_registry.hpp`, the vendored wire contract header, any `src/dictionary/*.cpp` added.
  - `linux-gcc-release` — sanity build (generated headers + tool compile under GCC).
- **Tier-2** (`windows-msvc-*`): manual / nightly. No C-ABI surface (spec §5) → no abidiff golden; `[const §IX.5]` N/A.
- **Pre-PR local gate** (`[const §XVII.7]`): contributor confirms `local build: green on linux-clang-debug @ <git-sha>`; the agent surfaces `AskUserQuestion` before any local Conan/CMake build (`[const §XVII.7]` resource gate).

## D-18: Bench harness shape (resolves NFR-003-1/2/3 verification)

- **Decision:**
  - `bench/codegen/typed_accessor_bench.cpp` — `cl_ord_id`(string)/`side`(char)/`order_qty`(decimal)/`price`(decimal)/`field_value`(runtime-keyed) on a warm-cache 20-tag `NewOrderSingle`; targets per `[2c §6.2]` (string/int/char ≤20 ns, decimal ≤75 ns, `field_value` ≤25 ns); >5 % regression vs `bench/baselines/codegen/typed_accessor.json` fails CI (NFR-003-1).
  - `bench/codegen/compile_time_bench/` — wall-clock to compile a single-version `Messages.hpp`+`Reify.hpp` TU (≤3 s, load-bearing) and an all-versions TU (≤15 s soft, `FIXPP_BENCH_ALL_VERSIONS_CEILING` knob); per-header preprocessor-expansion size tracked (NFR-003-2; F4 spike).
  - `bench/dictionary/reify_bench.cpp` — `reify_as<NewOrderSingle>` 20-tag (≤1 µs) / 200-tag (≤10 µs); `reify` runtime-dispatch (≤1.2 µs); plus the codegen-lookup arm (`field_ref`/`required_fields`/… on the codegen-emitted tables, seam #5).
- **Baseline first-cut:** baseline files written on the first green CI run that includes each bench; same protocol as 001/002.

## D-19: Gate A / Gate B precondition mapping (resolves spec §12 DoD)

- **Gate A trigger (`[const §XVII.1]`):** public C++ API + codegen layout + wire-adjacent contract. `gate_a_required: yes` in spec front-matter. Round 1 after this `/plan`; both Codex passes (rescue + `/codex:adversarial-review`) per `feedback_gate_a_codex_dual_pass.md`. F1 and R6 explicitly flagged for Codex review (plan.md Gate A §).
- **Gate B trigger (`[const §XVII.2]`):** every PR. Independence per `[const §XVII.3]`.
- **`/speckit-verify` precondition (`[const §XVII.8]`):** runs after `/speckit-implement`; verdict GREEN required for `gate-b-done`. Tier-1 matrix per D-17. Record at `.specify/decisions/003-dictionary-codegen-verify.md`. Gate-records layout per auto-memory `project_gate_records_layout` (decisions/ gitignored local-only; tracked record = phase-4 doc + research/reviews/).
- **Trigger-set eval (`[const §XVI.3]`/`[const §XVI.4]`):** touches "codegen" and the public C++ API in the Appendix A trigger set. `/clarify` ran 2026-05-15; `/analyze` is the user-visible next step after Gate A round 1 converges.

## D-20: spec ↔ design naming alignment (no spec amendment needed)

The spec uses the design-doc-canonical names throughout (`dict::reify_as` / `dict::reify` / `owning_message_handle` / `owning_<Msg>` / `version_registry::get`); `[2c §4.7]`/`[2c §4.8]`/`[2c §4.9]` are the canonical signatures emitted in `contracts/`. No divergence of the 002-D-20 kind exists here (002's `field`↔`field_ref` aliasing was a runtime-Dictionary concern; the codegen surface is generated verbatim from the design doc's class shape). Recorded so Gate A can confirm the codegen template's accessor naming derives mechanically from the XML `<field name>` (snake_cased) with no hand-maintained alias layer.

## Citation verification pass (round 1)

| Cite | Resolves to | OK |
|---|---|---|
| `[const §II.1]` | `constitution.md:26` — C++23 | ✅ |
| `[const §III.2]` | `constitution.md:41` — Conan | ✅ |
| `[const §III.5]` | `constitution.md:50` — `tools/` build-only | ✅ |
| `[const §V.1]` | `constitution.md:66` — AGPL + commercial dual | ✅ |
| `[const §V.3]` | `constitution.md:68` — No LGPL deps | ✅ |
| `[const §VI.5]` | `constitution.md:80` — Normative References | ✅ |
| `[const §VII.7]` | `constitution.md:93` — Fuzzing (cited to record non-trigger) | ✅ |
| `[const §VIII.5]` | `constitution.md:106` — Hot-path zero-alloc | ✅ |
| `[const §IX.5]` | `constitution.md:124` — ABI check (N/A here) | ✅ |
| `[const §IX.6]` | `constitution.md:125` — Two-tier CI | ✅ |
| `[const §X.4]` | `constitution.md:136` — Bounded error enum | ✅ |
| `[const §XIV.2]` | `constitution.md:197` — ≤5 pure-virtual (N/A here) | ✅ |
| `[const §XV]` | `constitution.md:203` — Banned patterns | ✅ |
| `[const §XV.6]` | `constitution.md:212` — Runtime-only validation banned | ✅ |
| `[const §XV.13]` | `constitution.md:219` — Hybrid codegen+runtime mandate | ✅ |
| `[const §XVI.3]` | `constitution.md:234` — /clarify mandatory | ✅ |
| `[const §XVI.4]` | `constitution.md:235` — /analyze mandatory | ✅ |
| `[const §XVII.1]` | `constitution.md:245` — Gate A | ✅ |
| `[const §XVII.3]` | `constitution.md:257` — Independence rule | ✅ |
| `[const §XVII.7]` | `constitution.md:265` — Local pre-PR gate | ✅ |
| `[const §XVII.8]` | `constitution.md:270` — /speckit-verify | ✅ |
| `[const §XVIII.2]` | `constitution.md:286` — FIX-Latest post-v1.0 | ✅ |
| `[const §XVIII.7]` | `constitution.md:297` — A-014..A-034 codegen-deferred | ✅ |

Cross-doc cites (`[2a §4.2]`, `[2b §4.3]`, `[2b §4.4]`, `[2b §4.7]`, `[2b §6.4]`, `[2b §6.6]`, `[2c §1.1]`, `[2c §1.2]`, `[2c §1.3]`, `[2c §4.7]`, `[2c §4.7.1]`, `[2c §4.8]`, `[2c §4.9]`, `[2c §6.2]`, `[2c §6.3]`, `[2c §6.7]`, `[2c §7.2]`, `[2c §7.6]`, `[2c §9]`, `[2c §10]`, `[arch §4.2]`, `[arch §5.5]`, `[arch §5.6]`, `[FIX50SP2 §3.3]`, `[FIXT §5]`) inherited verbatim from `spec.md §13` References and the design docs.

All `[const §X.Y]` citations resolve under canonical form. No Constitution Check violations; no Complexity Tracking entries to justify.
