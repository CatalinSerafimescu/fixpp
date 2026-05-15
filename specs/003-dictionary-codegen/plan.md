---
id: 003-dictionary-codegen
title: Implementation Plan — Dictionary codegen (`fixpp-codegen` + per-version typed messages + `dict::reify` bridge)
module: dictionary/
phase: 4
status: drafted
verdict: TBD
spec_kit_step: /plan
gate_a_round: pending (round 1 runs after this plan lands)
gate_b_round: pending
last_updated: 2026-05-15
inherits_design: .specify/2c-codegen.md (v1.3, signed off 2026-05-10)
inherits_spec: specs/003-dictionary-codegen/spec.md (carries /clarify Q&A 2026-05-15 — §"Clarifications" Q1→A, Q2→A, Q3→A + /clarify session Q-subset/Q-golden)
catalogue_rows: D-008 (code-generated constexpr field metadata — four codegen versions), OSS-010 (header-only generated typed messages with constexpr field metadata) — D-010 dropped Gate A r1 (Codex P2-2; not made testable; → spec §10 F6)
blocked_on_replan: yes — Gate A round 1 root causes RC#1 (unshipped+unassigned `version_profile`/`resolve_application_version`/`field_traits` surface → 003-owned, new files/ACs/error-taxonomy) and RC#2 (inherited 2c §4.1.3/§4.7 `decimal_t::from_chars` incoherent with merged 2a/001 — 2c reopen required, not bundle-fixable) + RC#3 (open `dict/`→`wire/` layer-amendment) exceed a convergence text patch. See `## Gate A` below.
plan_decisions: F1 → Candidate A (C++23 host tool reusing 002's Dictionary IR; user sign-off 2026-05-15); R6 → vendor a frozen wire::MessageView<Index> contract stub in this PR (user sign-off 2026-05-15)
---

# Implementation Plan — 003-dictionary-codegen

**Branch:** `003-dictionary-codegen` | **Date:** 2026-05-15 | **Spec:** [`spec.md`](spec.md)
**Input:** Feature specification at `specs/003-dictionary-codegen/spec.md`.

## Summary

Ship the **dictionary codegen feature (D-008)** — the build-time host tool `tools/codegen/fixpp-codegen` and the per-version generated header packs it emits (`Messages.hpp`, `Fields.hpp`, `Validator.hpp`, `Reify.hpp`, `NormativeReferences.md`) for the four codegen-target versions (`fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2`, `fixpp::vt11`), plus the 2c-owned runtime bridge `<fixpp/dict/reify.hpp>` (`dict::reify_as` / `dict::reify` + `owning_message_handle`), the shared runtime-dispatch headers (`_dispatch/reify_dispatch_fixt.hpp` + `reify_dispatch_application.hpp`), the `dict::version_registry` header shape (`[2c §4.9]`), and the `[2c §7.6]` CMake target graph. This is the **second Phase 4 feature of the `dictionary/` module** and consumes the runtime `Dictionary`/`XmlLoader`/`FieldRef`/`ComponentRef`/`GroupRef` surface merged by **002-dictionary-xml-loader** (PR #66, on `main`).

Technical approach is locked by `[2c]` v1.3 and `[arch §4.2]`; the two `/plan`-deferred decisions are resolved here with user sign-off (2026-05-15):

- **F1 — `fixpp-codegen` host-tool language/host → Candidate A (C++23, reuse the 002 `Dictionary` IR).** `tools/codegen/fixpp-codegen` is a C++23 host executable that links the merged `fixpp::dict` runtime, calls `XmlLoader::load(path, mr)` to parse each checked-in `dictionaries/<VER>.xml` into a `Dictionary`, walks the metadata (`FieldRef`/`ComponentRef`/`GroupRef` arrays + `version_profile`), and emits the per-version header packs via a small in-tool C++ string-templating layer. **Rationale:** single toolchain (C++23/Clang/Conan, already mandated by `[const §II.1]`); the XML is interpreted **once**, through the already-fuzzed, already-tested 002 loader (no second QuickFIX-XML parser to keep in sync — research.md D-1); determinism (NFR-003-7 / AC-T1) is inherited from 002's sorted, locale-independent bytewise emission invariant (research.md D-6 / 002 research D-6) for free; **zero new build-time dependencies** (`[const §III.2]` / `[const §V.3]` clean by construction — no new Conan row). Mirrors how 002 itself resolved its own deferred parser decision conservatively. No bootstrap cycle: the tool depends only on the **merged 002 runtime**, never on the headers it generates (research.md D-1).
- **R6 — `wire::MessageView<Index>` build-ordering → vendor a frozen wire contract stub in this PR.** `include/fixpp/wire/` is currently empty (`.gitkeep` only); the 2b wire feature is downstream of `dictionary/` in module order, but generated headers compile against `wire::MessageView<Index>`. This PR ships a minimal, frozen contract header `include/fixpp/wire/message_view_contract.hpp` providing exactly the `[2b §4.3]` / `[2b §4.7]`-locked surface (`get<Tag>()`, `get(uint16_t)`, `group<NoTag,T>()`, `field_view`, `group_view<T>`, the `[2b §6.4]` generation-counter trap). Codegen output compiles and **every AC-G\*/AC-R\*/AC-D\*/AC-C\*/conformance test runs in this PR** (DoD §12 requires it). 2b later replaces the stub with the real implementation against the **same** locked contract; drift is guarded by a `static_assert` + contract test (research.md D-2). Flagged for Gate A.

The codegen pipeline itself is locked by `[arch §4.2]`: `fixpp-codegen` reads `dictionaries/FIXxx.xml`, emits header packs into the **build tree only** (`build/<preset>/_codegen/include/fixpp/...`), and runs at **configure time** via the CMake target `fixpp::dict::generate-vXX`. A dirty checkout never carries stale codegen (AC-T2).

This feature unblocks the typed surface every downstream module (`session/`, `capi/` 2i, `bindings/python` 2m) compiles against, and is a `dictionary/` module-exit prerequisite (`phase-4/dictionary/README.md` surface rows #8/#9).

## Technical Context

**Language/Version:** C++23 (`[const §II.1]`). Free use of concepts, ranges, `std::expected`, `std::pmr`, deducing `this`. No fallback to earlier standards. The `fixpp-codegen` host tool is itself C++23 (F1 Candidate A).

**Primary Dependencies:** GoogleTest 1.17.0, Google Benchmark 1.9.5 (pinned via Conan from Phase 3 CI), **pugixml 1.14** (already a Conan row from 002 — reused transitively by `fixpp::dict`; the codegen tool links `fixpp::dict`, it does **not** add a new XML/templating dependency — F1 Candidate A, research.md D-1). No new Conan row in this PR.

**Storage:** N/A on the runtime hot path. Generated `constexpr` tables are static storage (program lifetime, zero allocation, no `new`/`delete` ever per `[const §VIII.5]`). `owning_<Msg>` storage is caller-`mr`-lifetime (≤ 4 PMR allocations per `reify_as` per `[2c §1.2]` / N-P2-5). Codegen output is written to the **build tree only** (research.md D-3).

**Testing:** GoogleTest + GoogleMock (C++) for AC-G\*, AC-V\*, AC-R\*, AC-D\*, AC-X\*, AC-C\*, AC-T\* per `[const §VII.1]`. No new Python pytest seam (SWIG typed-message bindings are owned by 2m, out of scope per spec §5). No new fuzz harness required: the XmlLoader fuzz harness (`tests/fuzz/fuzz_dict_xml_loader.cpp`) shipped with 002 already covers the XML input that drives codegen (`[2c §9]` seam #8 — XmlLoader-side, already shipped; spec §7). `[const §VII.7]`'s "new parser-touching code without a fuzz harness is a Gate B blocker" is satisfied: `fixpp-codegen` introduces **no new parser** — it consumes the 002 loader's already-fuzzed parse path (F1 Candidate A; research.md D-1 / D-9).

**Target Platform:** Linux primary (Tier 1: Clang 22 Debug + Release + ASan + UBSan + TSan + Coverage; GCC Release sanity). Windows Tier 2 (manual / nightly) per `[const §IX.6]`. No C-ABI surface in this PR (spec §5 — `fixpp_msg_reify` owned by 2i), so no abidiff golden — `[const §IX.5]` ABI check is N/A here (research.md D-17).

**Project Type:** C++23 library, dictionary module per `[arch §4.2]`. Adds one **build-only host executable** (`tools/codegen/fixpp-codegen`, `[const §III.5]` — runs at configure time, never linked into the user-facing library). No SWIG / Python bindings in this PR.

**Performance Goals (Linux/Clang/x86_64, warm cache, release `-O2`):** per `[2c §6.2]`:

- Typed accessor — string/int/char (e.g., `NewOrderSingle::cl_ord_id`): ≤ 20 ns; decimal (`::price`): ≤ 75 ns; `field_value(uint16_t)`: ≤ 25 ns. CI fails on >5% regression vs baseline (`[const §VIII.2]`; NFR-003-1; seam #3).
- Single-version `Messages.hpp`+`Reify.hpp` TU compile: ≤ 3 s (load-bearing). All-versions TU: ≤ 15 s **soft** (configurable `FIXPP_BENCH_ALL_VERSIONS_CEILING`; not a default-supported build) (NFR-003-2; seam #2).
- `dict::reify_as<Msg>`: ≤ 1 µs (20-tag), ≤ 10 µs (200-tag); `dict::reify` (runtime-dispatch): ≤ 1.2 µs (20-tag). ≤ 4 PMR allocations per `reify_as`; no allocation outside `mr` (NFR-003-3; seam #6/#7).
- Zero allocation on the typed-accessor read path (every accessor delegates to `wire::MessageView::get<Tag>()`, allocation-free Index mode) (NFR-003-4; seam #7).

Bench harnesses `bench/codegen/typed_accessor_bench.cpp`, `bench/codegen/compile_time_bench` and `bench/dictionary/reify_bench.cpp` enforce the bars via Google Benchmark per `[const §VIII.1]`; ±5 % regression budget per `[const §VIII.2]`.

**Constraints:**

- Codegen output is `constexpr` static storage — no `new`/`delete` ever (`[const §VIII.5]`); no `thread_local` emitted (`[const §XV]` / `[arch §5.4]`) (NFR-003-5).
- Determinism: byte-identical XML input → byte-identical generated headers across runs **and machines** (NFR-003-7; research.md D-6 — inherited from 002's sorted bytewise-emission invariant). One golden header per codegen version (4 total) anchors the determinism test (spec /clarify Q-golden → A); the goldens are generated codegen output, checked in at `/implement` (not present at Gate A).
- All view-returning accessors carry `[[clang::lifetimebound]]`; all `expected_t<T>`-returning methods carry `[[nodiscard]]` (codegen emits unconditionally) (NFR-003-6; `[arch §5.5]`).
- Layer hygiene: `dictionary → core` (from 002) is clean; the codegen **host tool** `fixpp::dict` link is a host-side build edge (build-only per `[const §III.5]`; not a library layer edge) — clean. **OPEN layer-amendment item (Gate A r1, Codex P1-3 / Opus root cause #3 — NOT green-checkable here):** the `arch §2.4` carve-out covers only **generated** `fixpp::vXX::*` headers; it does **not** cover the **hand-written** `include/fixpp/dict/reify.hpp` `#include <fixpp/wire/message_view_contract.hpp>` (`contracts/reify.hpp:8`) nor this feature *writing a new header into the 2b-owned `include/fixpp/wire/` tree*. Under the unamended `arch §2.3` whitelist (`.specify/architecture.md:113-118`, `dictionary` may include `core` only) `tools/check_layers.py` would **not** be clean for that edge. Resolution requires a `[const §XX]` / `arch §2.3` amendment (or relocating the contract header out of `dict/`) at re-`/plan` — not asserted at Gate A (NFR-003-8 narrowed; spec R6 / research.md D-2 / D-12 revised).
- `dict::reify*` are `noexcept` free function templates; PMR OOM surfaces as `dict_reify_oom` via `[2a §4.2]` `trap_throw` (AC-R7; spec §4.3).

**Scale/Scope:** 1 build-only host tool (`tools/codegen/fixpp-codegen`, ~6–10 C++ source files: XML→IR via 002 loader, the five emitters, the templating helper, the CLI driver) + 2 runtime bridge headers (`reify.hpp`, `version_registry.hpp`) + 1 vendored wire contract stub header + possibly 2 small bridge `.cpp` + the `[2c §7.6]` CMake target graph + ~14 test files + 3 bench harnesses + 4 golden headers (generated codegen output, checked in at `/implement` — not present at Gate A) + the conformance must-include manifest. Generated (build-tree, not counted as source): `{v42,v44,v50sp2,vt11}/{Messages,Fields,Validator,Reify,NormativeReferences}` + 2 `_dispatch/` headers. Roughly ~4500 LOC of hand-written tool + bridge + tests (estimate; the generated output is mechanical and not hand-maintained). Reuses the four `dictionaries/{FIX42,FIX44,FIX50SP2,FIXT11}.xml` checked in by 002 — **no new XML in this PR** (spec §A1).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-evaluated post-Phase 1 design.*

All citations use canonical form `[const §<Roman>.<arabic>]` per `constitution.md:5`. Every cite re-verified against the constitution after Phase 1 (see Citation verification pass).

| Article cited | Topic | How this feature satisfies it |
|---|---|---|
| `[const §II.1]` | C++23, no earlier fallback | Library + the `fixpp-codegen` host tool target C++23 only; uses `std::pmr`, `std::span`, `std::expected` (via `core::expected_t`). |
| `[const §III.2]` | Conan dependency manager, pinned | **No new Conan row.** F1 Candidate A reuses 002's `pugixml/1.14` transitively through `fixpp::dict`; the host tool adds no XML/templating dependency. |
| `[const §III.5]` | `tools/` is build-only; codegen runs at configure | `fixpp-codegen` runs at configure time via `fixpp::dict::generate-vXX`; never linked into the user-facing library; outputs to build tree (AC-T2 / AC-C4; `[arch §4.2]` step 3). |
| `[const §V.1]`, `[const §V.3]` | AGPL-3.0 + commercial dual; no LGPL deps | No new dependency admitted (F1 Candidate A). pugixml (reused from 002) is MIT — already cleared in 002. Generated headers carry `SPDX-License-Identifier: AGPL-3.0-or-later`. |
| `[const §VI.4]`, `[const §VI.5]` | Bidirectional traceability + Normative References | Inherits **D-008, OSS-010** from spec.md front-matter (D-010 dropped Gate A r1 — Codex P2-2 — not made testable here; → spec §10 F6); spec.md §13 lists the exact `[2c §...]`, `[arch §...]`, `[const §...]`, `[FIX...]` references. The generated `NormativeReferences.md` is itself the per-message `[const §VI.5]` mechanism (AC-V5; Appendix B of `[2c]`). |
| `[const §VII.1]`, `[const §VII.3]` | GoogleTest + TDD | `tasks.md` ordered red-green-refactor per seam; every test target is GoogleTest. |
| `[const §VII.7]` | Fuzzing on parser-touching modules | **Satisfied without a new harness.** F1 Candidate A introduces **no new parser**: the codegen tool consumes 002's already-fuzzed `XmlLoader` parse path (`tests/fuzz/fuzz_dict_xml_loader.cpp`, shipped PR #66, `[2c §9]` seam #8). No new parser-touching code lands; the rule's trigger does not fire (research.md D-1 / D-9). |
| `[const §VIII.1]`, `[const §VIII.2]` | Google Benchmark + ±5 % budget | `bench/codegen/typed_accessor_bench.cpp`, `bench/codegen/compile_time_bench`, `bench/dictionary/reify_bench.cpp` run in Tier 1 with `bench/baselines/`. |
| `[const §VIII.5]` | Zero allocation on the hot path | Generated `constexpr` tables = static storage, no `new`/`delete` (NFR-003-5); typed-accessor read path is zero-alloc (NFR-003-4, AC-T3, seam #7); `owning_<Msg>` ≤ 4 PMR allocs, none outside `mr` (AC-R7). |
| `[const §IX.1]` | ≥ 90 % line / ≥ 80 % branch on touched modules | `linux-clang-coverage` measures `tools/codegen/*`, `include/fixpp/dict/reify.hpp`/`version_registry.hpp`, the vendored wire contract header, and bridge `.cpp`; Tier-1 gate. Generated headers' coverage is exercised via the conformance corpus (the emitter is the unit under test, not the mechanical output). |
| `[const §IX.2]` | Tier-1 sanitizers (ASan + UBSan + TSan) | ASan + UBSan: every codegen/reify test. TSan: `reify_cross_strand_test` specifically (AC-R5 / AC-T3 / seam #12). |
| `[const §IX.4]` | Tier-1 static analysis clean | clang-tidy + clang-format + cppcheck + IWYU on the tool, the bridge, and a sample generated header; pre-commit + Tier-1. |
| `[const §IX.5]` | abidiff against last tagged ABI | **N/A this PR** — no C-ABI surface added (`fixpp_msg_reify` owned by 2i, spec §5). Cited to record explicit non-applicability (research.md D-17). |
| `[const §IX.6]` | Two-tier CI | Tier 1: every preset from quickstart §3. Tier 2: Windows manual / nightly. |
| `[const §X.4]` | Bounded `fixpp_error_t` + forwards-compat | New `dict_reify_*` / `dict_no_dictionary_for_application_version` / `dict_unresolved_application_version` / `dict_unknown_appl_ver_id` enum variants appended at unused slots (research.md D-10); C-ABI mapping + audit-trail (`tools/abi_history/error_codes_v1.txt`) deferred to 2i (no C-ABI surface lands here — same time-bounded waiver shape as 002 D-10; auto-expires at the first C-ABI consumer commit). |
| `[const §XIV.2]` | ≤ 5 pure-virtual on pluggable interfaces | N/A — no new pluggable interface; `dict::version_registry` (AC-X*) is a concrete value type, not a virtual interface (research.md D-14). Cited to record non-applicability. |
| `[const §XV]` | Banned patterns — `thread_local` | Codegen never emits `thread_local`; the host tool uses none; the reify bridge uses none. Verified by inspection + the `tools/check_alloc.py`/grep gate (NFR-003-5). |
| `[const §XV.6]` | Runtime-only field validation banned (codegen mandate) | This feature **is** the discharge of `[const §XV.6]` — `constexpr` field metadata + typed accessors generated from the dictionary; misuse fails to compile, not at runtime (AC-G1..G7). |
| `[const §XV.13]` | Hybrid mandate (codegen + runtime XML) | **Narrowed Gate A r1 (Codex P1-2 / Opus Downgrade P1→P2).** 002 shipped **D-007 only** (`XmlLoader` + runtime `Dictionary` — the runtime dictionary path the ban targets); 002 **deferred D-009** (`DialectOverlay`, an additive overlay, to its F2 / a future D-009 feature — `specs/002-dictionary-xml-loader/spec.md:26,100,235-239`). `[const §XV.13]`'s banned pattern (eager codegen with *no runtime dictionary path*) is discharged by **D-008 (this PR) + D-007 (002)**; D-009 is not "the runtime path" and is not required for this check. The hybrid is **not** claimed "complete" — D-009 remains tracked. eager-codegen-with-no-runtime-path is **not** introduced (spec §2). |
| `[const §XVI.3]` | `/clarify` MANDATORY pre-`/plan` (codegen + wire-format trigger) | Ran 2026-05-15: 3 inline `/specify` clarifications (Q1→A, Q2→A, Q3→A) + a `/clarify` session (conformance-subset principle, golden-header count). Recorded in spec.md `Clarifications`. |
| `[const §XVI.4]` | `/analyze` MANDATORY post-`/plan` | Runs after this plan lands, after Gate A converges, before `/tasks`. |
| `[const §XVII.1]` | Codex Gate A before `/tasks` (public C++ API + codegen layout) | `gate_a_required: yes` in spec front-matter. Round 1 runs after this plan; both Codex passes (rescue + `/codex:adversarial-review`) per `feedback_gate_a_codex_dual_pass.md` (auto-memory). The two `/plan` decisions (F1, R6) are explicitly flagged for Gate A review. |
| `[const §XVII.2]` | Gate B before every merge | Standard Gate B precondition. |
| `[const §XVII.3]` | Independence between author and reviewer | Opus author (`/plan`) + Codex reviewer (Gate A) are independent agents per `/gate-a` skill. |
| `[const §XVII.7]` | Local pre-PR build gate | Contributor confirms `local build: green on linux-clang-debug @ <git-sha>` in PR body; agent surfaces `AskUserQuestion` before any local Conan/CMake build. |
| `[const §XVII.8]` | `/speckit-verify` mandatory after `/speckit-implement` | `/speckit-verify 003-dictionary-codegen` produces `.specify/decisions/003-dictionary-codegen-verify.md`; `GREEN` required for `gate-b-done`, `YELLOW`+waivers for `gate-b-waived`. |
| `[const §XVIII.2]` | Roadmap — FIX-Latest post-v1.0 | FIX-Latest A-035..A-065 codegen-filtered with a build warning, not emitted (AC-G9); no `FIXPP_CODEGEN_ENABLE_FIX_LATEST` flag in v1.0 (spec Edge Cases / §5). |
| `[const §XVIII.7]` | Application-message codegen scope for v1.0 | A-014..A-034 not emitted as typed classes in v1.0; reachable only via 002's runtime `view.get(uint16_t)` (AC-G10); the conformance corpus excludes them. |

**Gates — citation form OK; substantive re-`/plan` required (Gate A round 1).** All cited articles resolve under canonical form to actual constitution text (Citation verification pass). However, Gate A round 1 surfaced two false green-checks now corrected above (`[const §XV.13]` D-009 misattribution narrowed; `[const §VI.4]` D-010 dropped) and three root causes (RC#1 unshipped+unassigned upstream surface; RC#2 inherited 2c decimal-decoding defect; RC#3 open `dict/`→`wire/` layer amendment) that exceed a convergence text patch and gate `/tasks` via `blocked_on_replan`. See `## Gate A` → Round 1. `Complexity Tracking` remains empty (no *justified* constitution violation; the open items are blocking-dependency / inherited-design / layer-amendment, resolved at re-`/plan`).

## Project Structure

### Documentation (this feature)

```text
specs/003-dictionary-codegen/
├── plan.md              # this file (/speckit-plan output, round 1 — 2026-05-15)
├── spec.md              # /specify output 2026-05-15; carries /clarify Q&A
├── research.md          # Phase 0 output — design decisions (D-1..D-20)
├── data-model.md        # Phase 1 output — generated-artifact entities, invariants, error mapping, PMR accounting
├── quickstart.md        # Phase 1 output — build / codegen / test / bench / sanitizer / coverage / verify / gate
├── contracts/
│   ├── reify.hpp                       # Phase 1 — dict::reify_as / dict::reify / owning_message_handle from [2c §4.8]
│   ├── version_registry.hpp            # Phase 1 — dict::version_registry shape from [2c §4.9]
│   ├── generated_message.hpp           # Phase 1 — the codegen-emitted typed-message class shape from [2c §4.7]
│   ├── reify_dispatch.hpp              # Phase 1 — the two _dispatch/ switch-header shapes from [2c §4.8]/[2c §6.3]
│   └── wire_message_view_contract.hpp  # Phase 1 — the vendored frozen [2b §4.3]/[2b §4.7] stub surface (R6)
├── checklists/          # /checklist output (already present from prior step)
└── tasks.md             # Phase 2 output (/speckit-tasks, NOT created by /speckit-plan)
```

### Source Code (library submodule root)

```text
tools/
└── codegen/
    └── fixpp-codegen/                    # NEW — C++23 build-only host tool (F1 Candidate A)
        ├── CMakeLists.txt                # NEW — host executable; links fixpp::dict (002 runtime)
        ├── main.cpp                      # NEW — CLI driver: (xml-path, version-tag, out-dir) → header pack
        ├── ir.hpp / ir.cpp               # NEW — XML→IR: calls XmlLoader::load(); walks Dictionary metadata
        ├── emit_messages.cpp             # NEW — emits <vXX>/Messages.hpp (typed flyweights, [2c §4.7])
        ├── emit_fields.cpp               # NEW — emits <vXX>/Fields.hpp (constexpr FieldRef/ComponentRef/GroupRef)
        ├── emit_validator.cpp            # NEW — emits <vXX>/Validator.hpp + Length+Data pair table
        ├── emit_reify.cpp                # NEW — emits <vXX>/Reify.hpp (owning_<Msg> classes, [2c §4.8])
        ├── emit_dispatch.cpp             # NEW — emits _dispatch/{fixt,application}.hpp (shared switches)
        ├── emit_normative_refs.cpp       # NEW — emits <vXX>/NormativeReferences.md ([2c §10] Q7)
        └── template_writer.hpp           # NEW — deterministic string-templating helper (locale-independent)

include/
└── fixpp/
    ├── dict/
    │   ├── reify.hpp                     # NEW — dict::reify_as / dict::reify / owning_message_handle ([2c §4.8])
    │   └── version_registry.hpp          # NEW — dict::version_registry shape only ([2c §4.9]; ownership → F3/2d)
    └── wire/
        └── message_view_contract.hpp     # NEW (vendored, FROZEN) — [2b §4.3]/[2b §4.7] surface stub (R6).
                                          #   2b later replaces with the real impl against this same contract;
                                          #   drift guarded by static_assert + a contract test (research.md D-2).

include/fixpp/core/
└── error.hpp                             # MODIFIED (additive) — new dict_* enum variants appended at unused
                                          #   slots (23..) for the reify/dispatch/registry error taxonomy
                                          #   (research.md D-10). Non-renumbering; forwards-compatible
                                          #   per [const §X.4]. Audit-trail update deferred to 2i (no C-ABI
                                          #   surface here — same time-bounded waiver shape as 002 D-10).

src/
└── dictionary/
    ├── CMakeLists.txt                    # MODIFIED — add reify.cpp / version_registry.cpp if any
                                          #   out-of-line bits are needed (/plan keeps these header-mostly;
                                          #   final split locked at /tasks per research.md D-5).
    ├── reify.cpp                         # NEW (maybe) — out-of-line bits of the reify bridge if needed
    └── version_registry.cpp              # NEW (maybe) — out-of-line bits of version_registry if needed

build/<preset>/_codegen/include/fixpp/    # GENERATED (build tree only — AC-C4 / AC-T2; never source tree)
├── {v42,v44,v50sp2,vt11}/
│   ├── Messages.hpp                      # GENERATED — typed-message flyweights ([2c §4.7])
│   ├── Fields.hpp                        # GENERATED — constexpr FieldRef/ComponentRef/GroupRef arrays
│   ├── Validator.hpp                     # GENERATED — per-message rule tables + Length+Data pair table
│   ├── Reify.hpp                         # GENERATED — owning_<Msg> classes ([2c §4.8])
│   └── NormativeReferences.md            # GENERATED — per-message [FIXxx §X.Y.Z] citations
└── _dispatch/
    ├── reify_dispatch_fixt.hpp           # GENERATED — 7 FIXT admin MsgTypes switch
    └── reify_dispatch_application.hpp    # GENERATED — ~470 (version, MsgType) application switch

tests/
├── codegen/
│   ├── CMakeLists.txt                    # MODIFIED — per-test executables; LABELS codegen
│   ├── conformance/
│   │   ├── must_include_manifest.txt     # NEW — checked-in curated CI subset manifest (AC-G12; Gate-A-reviewed)
│   │   └── conformance_test.cpp          # NEW — seam #1/#15b — parameterised round-trip corpus
│   ├── typed_accessor_test.cpp           # NEW — AC-G1..G8, AC-G11 — typed field access
│   ├── msgtype_boundary_test.cpp         # NEW — AC-G9 (FIX-Latest filtered) / AC-G10 (A-014..A-034 deferred)
│   ├── flyweight_shape_test.cpp          # NEW — seam #18 — AC-G7 sizeof static_assert per message
│   ├── determinism_test.cpp              # NEW — seam #1/#2 — NFR-003-7/AC-T1/AC-T2 vs 4 golden headers
│   ├── length_data_table_test.cpp        # NEW — seam #19 — AC-V4 exhaustive vs source XML
│   └── validator_shape_test.cpp          # NEW — AC-V1..V3, AC-V6 — Validator.hpp / Fields.hpp shape+exhaustiveness
├── dictionary/
│   ├── reify_test.cpp                    # NEW — AC-R1..R3, AC-R6, AC-R8 — reify_as / reify / handle
│   ├── reify_dispatch_test.cpp           # NEW — seam #15a/#15b/#15c — AC-D1..D7 (FIXT + app + unresolved)
│   ├── reify_move_test.cpp               # NEW — seam #14 — AC-R4 lazy-view rebuild + static_asserts
│   ├── reify_cross_strand_test.cpp       # NEW — seam #12 — AC-R5 (TSan target)
│   ├── reify_oom_test.cpp                # NEW — seam #16 — AC-R7 trap_throw → dict_reify_oom
│   └── version_registry_test.cpp         # NEW — AC-X1..X3 — shape-only, in-test hand-built registry
├── integration/
│   ├── multi_session_multi_version.cpp   # NEW — seam #10a — AC-C1..C3 multi-version coexistence
│   └── fixt_cross_vocabulary.cpp         # NEW — seam #10b — AC-D4 worked example
└── (fuzz/: NO new harness — 002's fuzz_dict_xml_loader.cpp covers the XML input; spec §7)

bench/
├── codegen/
│   ├── CMakeLists.txt                    # NEW — wire benches into Tier-1 release preset
│   ├── typed_accessor_bench.cpp          # NEW — NFR-003-1 — cl_ord_id/side/order_qty/price/field_value
│   └── compile_time_bench/               # NEW — NFR-003-2 — single-version ≤3s / all-versions ≤15s soft harness
└── dictionary/
    └── reify_bench.cpp                   # NEW — NFR-003-3 — reify_as 20/200-tag; reify ≤1.2µs

specs/003-dictionary-codegen/contracts/golden/  # GENERATED-then-checked-in AT /implement (does NOT
                                          #   exist in the bundle at Gate A — these are codegen
                                          #   output, not authorable by hand; 4 total, /clarify
                                          #   Q-golden → A; one tasks.md row anchors the determinism test)
├── v42_Messages.golden.hpp               # /implement deliverable (one per version, 4 total)
├── v44_Messages.golden.hpp               # /implement deliverable
├── v50sp2_Messages.golden.hpp            # /implement deliverable
└── vt11_Messages.golden.hpp              # /implement deliverable

CMakeLists.txt / cmake/                    # MODIFIED — the [2c §7.6] target graph:
                                          #   fixpp::dict::{v42,v44,v50sp2,vt11,all_versions,runtime,dispatch}
                                          #   + the configure-time fixpp::dict::generate-vXX custom targets
tools/check_layers.py                      # REVIEWED — codegen-host build edge classification (research.md D-12)
docs/src/dictionary/codegen.md             # NEW — how codegen runs / CMake targets / accessor model / reify
```

**Structure Decision:** single library, no web/mobile/cli split; follows the Phase-3 layout. The one structural addition is the **build-only host tool** under `tools/codegen/fixpp-codegen/` (`[const §III.5]`) — a C++23 executable that links the merged `fixpp::dict` (002) and runs at configure time via `fixpp::dict::generate-vXX`, emitting to the build tree only. One `core/` header is modified additively (`core/error.hpp` gains `dict_reify_*` / registry / dispatch enum variants at unused slots — research.md D-10, same additive discipline as 002 D-3). One header is **vendored and frozen** (`include/fixpp/wire/message_view_contract.hpp`, R6) so codegen output compiles and the full test suite runs in this PR ahead of 2b; 2b later swaps in the real impl against the same locked contract.

### Test seam → file mapping (closes spec.md §9 — every seam bound to a named on-disk file)

Same root-cause class that closed 001/002 Gate A round 1 ("seam→file map partial"). Every test seam in `spec.md §9` is bound to a named on-disk file; cross-cutting per-AC tests get their own files in addition. Seam numbers match `[2c §9]` for traceability; the gaps (#4/#8/#9/#11/#13/#17/#20) are the deliberate DialectOverlay / XmlLoader-fuzz / table_view exclusions documented in spec §9, **not** omissions.

| Seam # | spec.md §9 description | On-disk path | NFR / AC linkage |
|---|---|---|---|
| 1 | Conformance corpus (CI curated subset + nightly exhaustive) | `tests/codegen/conformance/conformance_test.cpp` + `must_include_manifest.txt` | AC-G1..G6, AC-G12, AC-R3 |
| 2 | Compile-time cost regression (single-version ≤3s / all-versions ≤15s soft) | `bench/codegen/compile_time_bench/` | NFR-003-2 |
| 3 | Per-tag accessor latency regression | `bench/codegen/typed_accessor_bench.cpp` | NFR-003-1 |
| 5 | Codegen lookup latency regression (on the codegen-emitted tables) | `bench/dictionary/reify_bench.cpp` (lookup arm) | NFR-003-1 (table side) |
| 6 | Reify latency regression (reify_as 20/200-tag; reify ≤1.2µs; move-across-thread smoke) | `bench/dictionary/reify_bench.cpp` | NFR-003-3 |
| 7 | Allocation guard (read path = 0; reify ≤4 PMR; `mallocnesia`) | `tests/dictionary/reify_oom_test.cpp` + `tools/check_alloc.py` driver | NFR-003-4, AC-R7, AC-T3 |
| 10a | Multi-version coexistence (no namespace bleed) | `tests/integration/multi_session_multi_version.cpp` | AC-C1, AC-C2, AC-C3 |
| 10b | FIXT.1.1 cross-vocabulary worked example | `tests/integration/fixt_cross_vocabulary.cpp` | AC-D4 |
| 10c | Runtime-XML-only versions → `dict_reify_unknown_msg_type` | `tests/dictionary/reify_dispatch_test.cpp` (negative arm) | AC-D5 |
| 12 | `owning_<Msg>` cross-strand handoff (reify A → move → consume B; original traps post-reset) | `tests/dictionary/reify_cross_strand_test.cpp` (TSan) | AC-R5, AC-T3 |
| 14 | `owning_<Msg>` move + lazy view rebuild (+ static_asserts: no ref members, nothrow-move, not `=default`) | `tests/dictionary/reify_move_test.cpp` | AC-R4 |
| 15a | `dict::reify` — 7 FIXT admin MsgTypes | `tests/dictionary/reify_dispatch_test.cpp` (FIXT arm) | AC-D1, AC-D2 |
| 15b | `dict::reify` — application MsgTypes (CI = AC-G12 subset; nightly exhaustive) | `tests/dictionary/reify_dispatch_test.cpp` (app arm) | AC-D1, AC-D3, AC-D7 |
| 15c | `dict_unresolved_application_version` propagation | `tests/dictionary/reify_dispatch_test.cpp` (unresolved arm) | AC-D6 |
| 16 | `trap_throw` PMR OOM injection (reify_as / reify / owning_<Msg>::from_view → `dict_reify_oom`) | `tests/dictionary/reify_oom_test.cpp` | AC-R7 |
| 18 | Static-assert typed-flyweight size per message | `tests/codegen/flyweight_shape_test.cpp` | AC-G7 |
| 19 | Length+Data static-table coverage vs source XML + `[FIX50SP2 §3.3]` | `tests/codegen/length_data_table_test.cpp` | AC-V4 |

**Cross-cutting per-AC tests** (not "seam files" per §9, but binding one AC family to one file):

| File | ACs covered |
|---|---|
| `tests/codegen/typed_accessor_test.cpp` | AC-G1..G8, AC-G11 (typed field access, msg_type_v/version_v, constructor, accessor discipline, lifetime trap) |
| `tests/codegen/msgtype_boundary_test.cpp` | AC-G9 (FIX-Latest filtered + warning), AC-G10 (A-014..A-034 not emitted) |
| `tests/codegen/validator_shape_test.cpp` | AC-V1, AC-V2, AC-V3, AC-V5, AC-V6 (Fields.hpp/Validator.hpp/NormativeReferences.md shape + exhaustiveness) |
| `tests/codegen/determinism_test.cpp` | NFR-003-7, AC-T1, AC-T2 (byte-identical re-emission vs the 4 golden headers; no source-tree write) |
| `tests/dictionary/reify_test.cpp` | AC-R1, AC-R2, AC-R3, AC-R6, AC-R8 (reify_as/reify/handle surface + mismatch errors) |
| `tests/dictionary/version_registry_test.cpp` | AC-X1, AC-X2, AC-X3 (shape only; in-test hand-built registry; no engine wiring) |

**Rule:** no seam maps to "the existing tests collectively". Each seam has at least one dedicated named file. The 6 cross-cutting files supplement the seam files for per-AC verification.

## Complexity Tracking

> No Constitution Check violations. Section intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |

## Gate A

- Round 1 applied 2026-05-15: Codex P1=3 P2=3 P3=1; Opus post-judging P1=4 P2=4 P3=4; rewrite addresses root causes RC#1 (phantom/unassigned upstream `version_profile`/`resolve_application_version`/`field_traits`+`decode_field` surface — 002 deferred/never-shipped; restated as 003-owned blocking surface, re-`/plan`), RC#2 (inherited-from-2c `decimal_t::from_chars` incoherent with merged 2a/001 — documented as un-fixable inherited design-doc defect; 2c §4.1.3/§4.7 reopen required), RC#3 (hand-written `dict/reify.hpp → wire/` include + new `wire/` header write not covered by the arch §2.4 generated-header carve-out — narrowed NFR-003-8, open `arch §2.3`/`[const §XX]` amendment). Reviews: research/reviews/codex_003-dictionary-codegen_gate_a_review.md, research/reviews/opus_003-dictionary-codegen_gate_a_adversarial_review.md.
- Round 2 applied 2026-05-15: Codex P1=1 P2=1 P3=1; Opus post-judging P1=1 P2=1 P3=4; rewrite addresses RC#4 (present-tense overclaim of not-yet-existing bundle artifacts) + Confirmed P3s; RC#1/RC#2/RC#3 remain documented-as-blocking (re-/plan + [const §XX] 2c reopen) — not text-patchable. Reviews: research/reviews/codex_003-dictionary-codegen_gate_a_2_review.md, research/reviews/opus_003-dictionary-codegen_gate_a_2_adversarial_review.md.

### Round 1 — disagreements

No Codex finding was ruled `Disagree` by the authoritative Opus adversarial review. Opus dispositions: Codex P1-1 **Confirm @ P1** (RC#1); Codex P1-2 **Downgrade P1 → P2** (D-009 is an additive overlay, not the `[const §XV.13]` runtime path — the substantive constitutional obligation is met by D-007+D-008; defect is a wording overclaim, applied as a P2 cite-accuracy narrowing, not a P1 structural hole); Codex P1-3 **Confirm @ P1** (RC#3; the arch §2.4 generated-header carve-out narrows blast radius but does not reach the hand-written bridge header — applied); Codex P2-1 **Escalate P2 → P1** (RC#2 — inheritance-integrity break, applied as documented inherited defect); Codex P2-2 / P2-3 **Confirm @ P2** (applied); Codex P3-1 **Confirm @ P3** (applied as an explicit checklist exception line). Plus Opus new findings N-P1-1 (decimal accessor PMR/arena hole — applied as AC-G4a, blocked on RC#2), N-P2-1 (`owning_message_t<>` undefined — applied: alias pinned in `contracts/reify.hpp`), N-P2-2 (`field_traits.hpp` consumed-but-unscoped — folded into RC#1 / spec §8), N-P3-1 (`get<1128>()` contract-test gap — applied below), N-P3-2 (golden anchors only `Messages.hpp` — residual-risk recorded below). Nothing recorded here as a rejected Codex fix because none was rejected.

### Round 1 — design-doc amendment precondition (`[const §XX]`)

RC#2 cannot be closed by any edit to the `specs/003-dictionary-codegen/` bundle: `[2c §4.1.3]`/§4.7 (`.specify/2c-codegen.md:270-271,1040`) bake `fixpp::decimal_t::from_chars(fv->bytes())` into **signed-off `2c-codegen.md` v1.3**, and that symbol does not exist on the merged 001/2a surface (only PMR-mandatory `decimal_traits<T>::from_chars(span,mr)` / `decimal<T>::parse(span,mr)`; 2a's own Gate A removed the no-PMR form — `specs/001-core-decimal/contracts/decimal_traits.hpp:98-100,123-128,162-163`). Per `[const §XX]` the design doc must be amended before AC-G4 / AC-G4a / NFR-003-4 (decimal arm) / the data-model PMR accounting can close. **Action item (out of this convergence pass):** reopen 2c §4.1.3/§4.7 to route decimal through the real PMR-mandatory entry point with an arena threaded into the typed accessor (and reconcile the flyweight `sizeof == one pointer` invariant against needing an `mr` — Opus N-P1-1), then re-run `/plan` for 003. The bundle records this conflict (spec AC-G4, data-model Entity 1, `contracts/generated_message.hpp` `price()` annotation) and does **not** paper it over with a fabricated fix.

### Round 1 — new contract-test / residual-risk items (Opus N-P3-1 / N-P3-2)

- **N-P3-1 (P3):** the R6 drift-guard contract test (folded into `flyweight_shape_test.cpp`) asserts the flyweight member signatures + `sizeof` invariant but does **not** assert `view.template get<1128>()` (the dispatch-path entry `dict::reify` depends on) compiles against the frozen `wire_message_view_contract.hpp` surface. Add a `static_assert`/instantiation in `flyweight_shape_test.cpp` (or `reify_dispatch_test.cpp`) that `view.template get<1128>()` is well-formed against the frozen contract, so a future 2b tag-whitelist constraint fails loud at compile time. Bind at `/tasks` (added to the R6 contract-test task).
- **N-P3-2 (P3, accepted residual risk):** the determinism golden set anchors only `<vXX>_Messages.golden.hpp` (D-16). `Reify.hpp` (custom move bodies, the most correctness-sensitive artifact, R4) and `_dispatch/*.hpp` (~470 cases, R3) have no golden — a template change perturbing a move body or dispatch-case ordering is byte-stable run-to-run (passes determinism) yet invisible to the reviewed-diff mechanism D-16 sells as the R4/R3 mitigation. **Decision:** deliberate scoping gap accepted for v1.0; residual R4/R3 risk explicitly named here (was previously unstated). Revisit (extend the golden set to one `Reify.hpp` + one `_dispatch` golden) if a move-body/dispatch regression escapes review in practice. **This acceptance is plan-local** — it is recorded *here*, not in spec §11; the spec §11 R3/R4 mitigations stand as written and do not (and need not) restate this golden-coverage residual (corrected Gate A round 2, Codex F-3 / Opus Confirm @ P3 — the earlier "Recorded in spec §11 R3/R4" clause was a false cross-reference and is struck). Not a Gate A blocker.

### Round 2 — disagreements

Opus (authoritative adversarial review) **Downgraded Codex round-2 F-2 from P2 to P3** and its fix was **not applied as Codex stated**. Recorded per the independence/disagreement-record discipline:

- **Codex F-2 (P2 → P3, Opus Downgrade).** Codex F-2 objected that `spec.md` §8 "Depends on (in-tree, merged)" folds `core::error`/`expected_t<T>`/`trap_throw_or_throw` into the "002-shipped dictionary surface … all on `main` via PR #66" and demanded the line split into "002-shipped dictionary surface" vs. "**pre-existing** core surface consumed here". **Codex's factual premise is partly false** (independently re-verified against 002's shipped contracts): (1) `core::detail::trap_throw_or_throw` is **net-new in 002 / PR #66** — `specs/002-dictionary-xml-loader/spec.md:190` ("added next to the existing `detail::trap_throw<F>`"), `:201` ("NEW exception-API helper added in this PR"); Codex's claim that it is "not part of the 002-shipped surface" / "pre-existing" is **incorrect**. (2) `core::error`/`expected_t<T>` are 001-origin but were **extended additively by 002** (`002 spec.md:190` — three new `dict_*` variants appended at slots 20–22; `:201` Depends-on lists `core::error` extended in 002), so they genuinely ship/were-re-touched with PR #66. The "all on `main` via PR #66" attribution is therefore **substantively accurate** for every listed symbol. The only true residual is **taxonomic-wording imprecision** (these are *core* surface, not the *dictionary* surface; the `dict_*` variants live in `core::error`, not a `dict::` namespace) with **no downstream effect on any AC, dependency, or `/tasks` input** — hence P3, not P2. **Applied: the minimal taxonomic-wording split only** (spec §8 now separates "002-shipped dictionary surface" from "core surface consumed here", labelling `core::error`/`expected_t` 001-origin-extended-by-002 and `trap_throw_or_throw` net-new-in-002). **NOT applied: Codex's "pre-existing core surface" framing**, because it mis-states `trap_throw_or_throw` (a PR-#66 addition) as pre-existing. Reviews: research/reviews/codex_003-dictionary-codegen_gate_a_2_review.md, research/reviews/opus_003-dictionary-codegen_gate_a_2_adversarial_review.md.

### Round 1 — original pre-review notes (superseded by the applied record above)

Gate A round 1 runs on the Phase-4 bundle (`spec.md` + `plan.md` + `research.md` + `data-model.md` + `contracts/`) with both Codex passes per `feedback_gate_a_codex_dual_pass.md` (auto-memory):
- **Codex rescue agent** (`/codex:rescue`) — full Phase-4 bundle review.
- **`/codex:adversarial-review`** — adversarial pass challenging design choices.

Followed by Opus post-judging (P1/P2/P3 triage and verdict). Reviews land under `research/G19-fix-fpml-iso20022/research/reviews/`. Full /gate-a decision record at `.specify/decisions/003-dictionary-codegen-gatea.md`.

**Explicitly flagged for Gate A review** (the two `/plan`-deferred decisions, user-signed-off 2026-05-15):
- **F1 → Candidate A** (C++23 host tool reusing 002's `Dictionary` IR). Codex reviews the choice against `[2c §9]` seams and `[const §V.3]` (no new dependency admitted; the no-second-parser argument is the load-bearing rationale — research.md D-1).
- **R6 → vendor a frozen wire contract stub.** Codex reviews the stub surface for fidelity to `[2b §4.3]`/`[2b §4.7]` and the 2b-swap-in / drift-guard mechanism (research.md D-2). This is the highest-risk decision in the bundle (it ships a header ahead of its owning feature); the contract-test + `static_assert` drift guard is the mitigation under review.

### Round 2 — pending (if any P1 surfaces in re-review)

If round-1 fixes don't converge, round-2 expands to a full bundle redraft of `plan.md` + `research.md` + `data-model.md` + `contracts/` from a literal re-read of `.specify/2c-codegen.md` v1.3 + `[arch §4.2]`; `spec.md` preserved verbatim (it carries the /clarify Q&A). Hard reset (Round 3) is rare per `[const §XVII.1]`.

## Gate B

### Round 1 — TBD (post-`/implement`, pre-merge)

Gate B procedure: `.specify/codex-review.md` §6 (prompt) / §7 (recording). Independence rule per `[const §XVII.3]` — each round uses a separate Codex session from the implementer. Mandatory before merge per `[const §XVII.2]`. `/speckit-verify` precondition per `[const §XVII.8]` — record at `.specify/decisions/003-dictionary-codegen-verify.md` must be `GREEN` for `gate-b-done`, `YELLOW` with paired waivers for `gate-b-waived`. `/gate-b` applies the label via `gh api` REST with `--repo CatalinSerafimescu/fixpp` (auto-memory `project_gate_label_application`).

## Citation verification pass (round 1)

| Cite | Resolves to | OK |
|---|---|---|
| `[const §II.1]` | `constitution.md:26` — C++23 | ✅ |
| `[const §III.2]` | `constitution.md:41` — Conan | ✅ |
| `[const §III.5]` | `constitution.md:50` — `tools/` build-only | ✅ |
| `[const §V.1]` | `constitution.md:66` — AGPL + commercial dual | ✅ |
| `[const §V.3]` | `constitution.md:68` — No LGPL deps | ✅ |
| `[const §VI.4]` | `constitution.md:79` — Bidirectional traceability | ✅ |
| `[const §VI.5]` | `constitution.md:80` — Normative References | ✅ |
| `[const §VII.1]` | `constitution.md:87` — GoogleTest | ✅ |
| `[const §VII.3]` | `constitution.md:89` — TDD | ✅ |
| `[const §VII.7]` | `constitution.md:93` — Fuzzing (cited to record non-trigger) | ✅ |
| `[const §VIII.1]` | `constitution.md:99` — Google Benchmark | ✅ |
| `[const §VIII.2]` | `constitution.md:100` — ±5 % perf budget | ✅ |
| `[const §VIII.5]` | `constitution.md:106` — Hot-path zero-alloc | ✅ |
| `[const §IX.1]` | `constitution.md:113` — Coverage thresholds | ✅ |
| `[const §IX.2]` | `constitution.md:117` — Tier-1 sanitizers | ✅ |
| `[const §IX.4]` | `constitution.md:119` — Static analysis | ✅ |
| `[const §IX.5]` | `constitution.md:124` — ABI check (N/A here) | ✅ |
| `[const §IX.6]` | `constitution.md:125` — Two-tier CI | ✅ |
| `[const §X.4]` | `constitution.md:136` — Bounded error enum | ✅ |
| `[const §XIV.2]` | `constitution.md:197` — ≤5 pure-virtual (N/A here) | ✅ |
| `[const §XV]` | `constitution.md:203` — Banned patterns (thread_local) | ✅ |
| `[const §XV.6]` | `constitution.md:212` — Runtime-only validation banned | ✅ |
| `[const §XV.13]` | `constitution.md:219` — Hybrid codegen+runtime mandate | ✅ |
| `[const §XVI.3]` | `constitution.md:234` — /clarify mandatory | ✅ |
| `[const §XVI.4]` | `constitution.md:235` — /analyze mandatory | ✅ |
| `[const §XVII.1]` | `constitution.md:245` — Gate A | ✅ |
| `[const §XVII.2]` | `constitution.md:255` — Gate B | ✅ |
| `[const §XVII.3]` | `constitution.md:257` — Independence rule | ✅ |
| `[const §XVII.7]` | `constitution.md:265` — Local pre-PR gate | ✅ |
| `[const §XVII.8]` | `constitution.md:270` — /speckit-verify | ✅ |
| `[const §XVIII.2]` | `constitution.md:286` — FIX-Latest post-v1.0 | ✅ |
| `[const §XVIII.7]` | `constitution.md:297` — A-014..A-034 codegen-deferred | ✅ |

All citations resolve under canonical form. Cross-doc cites (`[2a §4.2]`, `[2b §4.3]`, `[2b §4.4]`, `[2b §4.7]`, `[2b §6.4]`, `[2b §6.6]`, `[2c §X.Y]`, `[arch §4.2]`, `[arch §5.5]`, `[FIX50SP2 §3.3]`, `[FIXT §5]`) inherited verbatim from `spec.md §13` References and the design docs.

## Phase-2 input checklist (for `/tasks`)

When `/speckit-tasks` runs after Gate A converges, it consumes this plan plus `data-model.md` + `research.md` + `contracts/` to produce `tasks.md`. Pre-binding the per-task shape:

- **One task per row of the "Test seam → file mapping" table** (17 seam rows + 6 cross-cutting AC rows = 23 test-target tasks).
- **One task per `fixpp-codegen` source file / emitter** (~10: IR, the five emitters, dispatch emitter, normative-refs emitter, templating helper, CLI driver + its `CMakeLists.txt`).
- **One task per runtime bridge header** (`reify.hpp`, `version_registry.hpp`) + any out-of-line `src/dictionary/*.cpp`.
- **One task** for the vendored frozen `include/fixpp/wire/message_view_contract.hpp` + its drift-guard contract test (R6).
- **One task** for the `include/fixpp/core/error.hpp` additive enum edit (research.md D-10).
- **One task** for the `[2c §7.6]` CMake target graph + the configure-time `fixpp::dict::generate-vXX` custom targets.
- **One task** for the four golden headers (generated by `fixpp-codegen`, then checked in at `/implement` — not present at Gate A) + the conformance must-include manifest (Gate-A-reviewed).
- **One task** per bench harness (`typed_accessor_bench`, `compile_time_bench`, `reify_bench` + their `CMakeLists.txt`).
- **One task** for `docs/src/dictionary/codegen.md` (DoD §12).
- **Polish tasks** (sanitizer presets, coverage threshold, layer-edge lint, bench-baseline seeding, `/speckit-verify`, `/gate-a`, `/gate-b`) — each a `tasks.md` row so `/speckit-verify` has a one-to-one mapping per `[const §XVII.8]`.

Expected total: ~50 tasks. TDD red-green-refactor ordering per `[const §VII.3]` — the vendored wire contract + golden headers + emitter unit tests lead; the emitters follow; conformance/integration/bench/polish close.
