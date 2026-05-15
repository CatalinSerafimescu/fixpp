---
id: 003-dictionary-codegen
title: Implementation Plan — Dictionary codegen (`fixpp-codegen` + per-version typed messages + `dict::reify` bridge)
module: dictionary/
phase: 4
status: gate-a-converged 2026-05-15 (RC#1/RC#2/RC#3 resolved in-bundle; fresh post-re-`/plan` Gate A converged at replan-loop round 3; /tasks unblocked)
verdict: Gate A converged — replan-loop round 3 (Codex P1=0 P2=0 P3=0; Opus P1=0 P2=0 P3=0; user-signed-off 2026-05-15, commit 3824bb5). /tasks unblocked; Gate B pending.
spec_kit_step: /plan (re-`/plan` 2026-05-15)
gate_a_round: re-`/plan` applied 2026-05-15 — prior rounds 1+2 exhausted → re-plan path; the FRESH post-re-`/plan` round CONVERGED at replan-loop round 3 (Codex/Opus P1=0 P2=0 P3=0, commit 3824bb5) per the exhaustion→re-plan path, `[const §XVII.1]`
gate_b_round: pending
last_updated: 2026-05-15
inherits_design: .specify/2c-codegen.md (v1.4 — v1.3 signed off 2026-05-10; v1.3→v1.4 RC#2 `[const §XX]` amendment 2026-05-15, commit 41dd8c1) + .specify/architecture.md (v0.3 — v0.2 signed off 2026-05-10; v0.2→v0.3 RC#3 `[const §XX]` §2.4 amendment 2026-05-15)
inherits_spec: specs/003-dictionary-codegen/spec.md (carries /clarify Q&A 2026-05-15 — §"Clarifications" Q1→A, Q2→A, Q3→A + /clarify session Q-subset/Q-golden; RC#1/#2/#3 ACs added at re-`/plan`)
catalogue_rows: D-008 (code-generated constexpr field metadata — four codegen versions), OSS-010 (header-only generated typed messages with constexpr field metadata) — D-010 dropped Gate A r1 (Codex P2-2; not made testable; → spec §10 F6)
replan_applied: 2026-05-15 — Gate A round-1 root causes RESOLVED in-bundle (supersedes the prior `blocked_on_replan: yes`). RC#1 `version_profile`/`resolve_application_version`/`field_traits`+`decode_field` → 003-owned (new contracts/ACs/error-taxonomy/enum-map); RC#2 decimal route re-derived from corrected 2c v1.4; RC#3 dict↔wire bridge edge resolved via the arch §2.4 v0.2→v0.3 carve-out amendment. `/tasks` unblocked — the fresh Codex Gate A CONVERGED at replan-loop round 3 (Codex/Opus P1=0 P2=0 P3=0, commit 3824bb5). See `## Re-/plan (RC resolution)` + `## Gate A` → Round 3 (CONVERGED).
plan_decisions: F1 → Candidate A (C++23 host tool reusing 002's Dictionary IR; user sign-off 2026-05-15); R6 → vendor a frozen wire::MessageView<Index> contract stub in this PR (user sign-off 2026-05-15); RC#1 → 003 owns version_profile/resolve_application_version (additive edit to the 002 enums-only file) + field_traits/decode_field (NET-NEW header); RC#2 → consume corrected 2c v1.4 decimal route; RC#3 → arch §2.4 v0.3 bridge-surface carve-out amendment (reviewed/accepted by the fresh Gate A — CONVERGED replan-loop round 3 — + Article XX §2 sign-off, commit 3824bb5)
---

# Implementation Plan — 003-dictionary-codegen

**Branch:** `003-dictionary-codegen` | **Date:** 2026-05-15 (re-`/plan`) | **Spec:** [`spec.md`](spec.md)
**Input:** Feature specification at `specs/003-dictionary-codegen/spec.md`.

## Summary

Ship the **dictionary codegen feature (D-008)** — the build-time host tool `tools/codegen/fixpp-codegen` and the per-version generated header packs it emits (`Messages.hpp`, `Fields.hpp`, `Validator.hpp`, `Reify.hpp`, `NormativeReferences.md`) for the four codegen-target versions (`fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2`, `fixpp::vt11`), plus the 2c-owned runtime bridge `<fixpp/dict/reify.hpp>` (`dict::reify_as` / `dict::reify` + `owning_message_handle`), the shared runtime-dispatch headers (`_dispatch/reify_dispatch_fixt.hpp` + `reify_dispatch_application.hpp`), the `dict::version_registry` header shape (`[2c §4.9]`), and the `[2c §7.6]` CMake target graph. This is the **second Phase 4 feature of the `dictionary/` module** and consumes the runtime `Dictionary`/`XmlLoader`/`FieldRef`/`ComponentRef`/`GroupRef` surface merged by **002-dictionary-xml-loader** (PR #66, on `main`).

> **Re-`/plan` 2026-05-15.** Gate A rounds 1+2 (2026-05-15) exhausted into the re-`/plan` path (`## Gate A`). This re-`/plan` **resolves the three root causes in-bundle** (full record: `## Re-/plan (RC resolution)`): **RC#1** — `version_profile`/`resolved_message_version`/`resolve_application_version` + `field_traits`/`decode_field` are now **003-owned** (002 deferred/never-shipped them); **RC#2** — the decimal route is re-derived from the corrected `2c-codegen.md` **v1.4** (`[const §XX]` amendment, commit 41dd8c1); **RC#3** — the dict↔wire bridge edge is covered by the `arch §2.4` **v0.2→v0.3** dual-compile-bridge carve-out amendment (no module cycle). The bundle is no longer `blocked_on_replan`; the fresh Codex Gate A ran on this re-planned bundle (round counter reset per the exhaustion→re-plan path) and **CONVERGED at replan-loop round 3** (Codex/Opus P1=0 P2=0 P3=0, user-signed-off 2026-05-15, commit 3824bb5); `/tasks` is unblocked (see `## Gate A` → Round 3 (CONVERGED)).

Technical approach is locked by `[2c]` v1.4 and `[arch §4.2]`; the two `/plan`-deferred decisions are resolved with user sign-off (2026-05-15):

- **F1 — `fixpp-codegen` host-tool language/host → Candidate A (C++23, reuse the 002 `Dictionary` IR).** `tools/codegen/fixpp-codegen` is a C++23 host executable that links the merged `fixpp::dict` runtime, calls `XmlLoader::load(path, mr)` to parse each checked-in `dictionaries/<VER>.xml` into a `Dictionary`, walks the metadata (`FieldRef`/`ComponentRef`/`GroupRef` arrays + `Dictionary::which_session_version()`), and emits the per-version header packs via a small in-tool C++ string-templating layer. **Rationale:** single toolchain (C++23/Clang/Conan, already mandated by `[const §II.1]`); the XML is interpreted **once**, through the already-fuzzed, already-tested 002 loader (no second QuickFIX-XML parser to keep in sync — research.md D-1); determinism (NFR-003-7 / AC-T1) is inherited from 002's sorted, locale-independent bytewise emission invariant (research.md D-6 / 002 research D-6) for free; **zero new build-time dependencies** (`[const §III.2]` / `[const §V.3]` clean by construction — no new Conan row). No bootstrap cycle: the tool depends only on the **merged 002 runtime**, never on the headers it generates (research.md D-1).
- **R6 — `wire::MessageView<Index>` build-ordering → vendor a frozen wire contract stub in this PR.** `include/fixpp/wire/` is currently empty (`.gitkeep` only); the 2b wire feature is downstream of `dictionary/` in module order, but generated headers compile against `wire::MessageView<Index>`. This PR ships a minimal, frozen contract header `include/fixpp/wire/message_view_contract.hpp` providing exactly the `[2b §4.3]` / `[2b §4.7]`-locked surface. 2b later replaces the stub's body against the **same** locked contract; drift is guarded by a `static_assert` + contract test (research.md D-2). The bridge surface (this stub + the hand-written `dict/` bridge headers + the generated tree) is now explicitly covered by the `arch §2.4` v0.3 carve-out (RC#3).

The codegen pipeline is locked by `[arch §4.2]`: `fixpp-codegen` reads `dictionaries/FIXxx.xml`, emits header packs into the **build tree only** (`build/<preset>/_codegen/include/fixpp/...`), and runs at **configure time** via `fixpp::dict::generate-vXX`. A dirty checkout never carries stale codegen (AC-T2).

This feature unblocks the typed surface every downstream module (`session/`, `capi/` 2i, `bindings/python` 2m) compiles against, and is a `dictionary/` module-exit prerequisite (`phase-4/dictionary/README.md` surface rows #8/#9).

## Technical Context

**Language/Version:** C++23 (`[const §II.1]`). Free use of concepts, ranges, `std::expected`, `std::pmr`, deducing `this`. No fallback to earlier standards. The `fixpp-codegen` host tool is itself C++23 (F1 Candidate A).

**Primary Dependencies:** GoogleTest 1.17.0, Google Benchmark 1.9.5 (pinned via Conan from Phase 3 CI), **pugixml 1.14** (already a Conan row from 002 — reused transitively by `fixpp::dict`; the codegen tool links `fixpp::dict`, it does **not** add a new XML/templating dependency — F1 Candidate A, research.md D-1). No new Conan row in this PR.

**Storage:** N/A on the runtime hot path. Generated `constexpr` tables are static storage (program lifetime, zero allocation, no `new`/`delete` ever per `[const §VIII.5]`). `owning_<Msg>` storage is caller-`mr`-lifetime (≤ 4 PMR allocations per `reify_as` per `[2c §1.2]` / N-P2-5). Codegen output is written to the **build tree only** (research.md D-3).

**Testing:** GoogleTest + GoogleMock (C++) for AC-G\*, AC-V\*, AC-R\*, AC-D\*, AC-X\*, AC-C\*, AC-T\*, **AC-VP\*, AC-FT\*** per `[const §VII.1]`. No new Python pytest seam (SWIG typed-message bindings owned by 2m, out of scope per spec §5). No new fuzz harness required: 002's `tests/fuzz/fuzz_dict_xml_loader.cpp` already covers the XML input that drives codegen (`[2c §9]` seam #8 — XmlLoader-side, already shipped; spec §7). `[const §VII.7]` is satisfied: F1 Candidate A introduces **no new parser** (research.md D-1 / D-9).

**Target Platform:** Linux primary (Tier 1: Clang 22 Debug + Release + ASan + UBSan + TSan + Coverage; GCC Release sanity). Windows Tier 2 (manual / nightly) per `[const §IX.6]`. No C-ABI surface in this PR (spec §5 — `fixpp_msg_reify` owned by 2i), so no abidiff golden — `[const §IX.5]` ABI check is N/A here (research.md D-17).

**Project Type:** C++23 library, dictionary module per `[arch §4.2]`. Adds one **build-only host executable** (`tools/codegen/fixpp-codegen`, `[const §III.5]` — runs at configure time, never linked into the user-facing library). No SWIG / Python bindings in this PR.

**Performance Goals (Linux/Clang/x86_64, warm cache, release `-O2`):** per `[2c §6.2]`:

- Typed accessor — string/int/char (e.g., `NewOrderSingle::cl_ord_id`): ≤ 20 ns; **decimal (`::price(mr)`, v1.4 PMR-mandatory signature): ≤ 75 ns** — `find` (~15 ns) + `decimal_t::parse(bytes, mr)` (~50 ns per `[2a §6.5]`), allocation-free for the default `pod_decimal` trait; `field_value(uint16_t)`: ≤ 25 ns. CI fails on >5% regression vs baseline (`[const §VIII.2]`; NFR-003-1; seam #3).
- Single-version `Messages.hpp`+`Reify.hpp` TU compile: ≤ 3 s (load-bearing). All-versions TU: ≤ 15 s **soft** (configurable `FIXPP_BENCH_ALL_VERSIONS_CEILING`; not a default-supported build) (NFR-003-2; seam #2).
- `dict::reify_as<Msg>`: ≤ 1 µs (20-tag), ≤ 10 µs (200-tag); `dict::reify` (runtime-dispatch): ≤ 1.2 µs (20-tag). ≤ 4 PMR allocations per `reify_as`; no allocation outside `mr` (NFR-003-3; seam #6/#7).
- Zero allocation on the typed-accessor read path: string/int/char unconditionally; decimal for the default `pod_decimal` trait (NFR-003-4; AC-G4a; seam #7).

Bench harnesses `bench/codegen/typed_accessor_bench.cpp`, `bench/codegen/compile_time_bench` and `bench/dictionary/reify_bench.cpp` enforce the bars via Google Benchmark per `[const §VIII.1]`; ±5 % regression budget per `[const §VIII.2]`.

**Constraints:**

- Codegen output is `constexpr` static storage — no `new`/`delete` ever (`[const §VIII.5]`); no `thread_local` emitted (`[const §XV]` / `[arch §5.4]`) (NFR-003-5).
- Determinism: byte-identical XML input → byte-identical generated headers across runs **and machines** (NFR-003-7; research.md D-6 — inherited from 002's sorted bytewise-emission invariant). One golden header per codegen version (4 total) anchors the determinism test (spec /clarify Q-golden → A); the goldens are generated codegen output, checked in at `/implement` (not present at Gate A).
- All view-returning accessors carry `[[clang::lifetimebound]]`; all `expected_t<T>`-returning methods carry `[[nodiscard]]` (codegen emits unconditionally) (NFR-003-6; `[arch §5.5]`).
- **Owned upstream surface (RC#1, 003-owned — re-`/plan` 2026-05-15):** `version_profile`/`resolved_message_version`/`dict::resolve_application_version` are an **additive edit** to the 002-shipped (enums-only) `include/fixpp/dict/version_profile.hpp`; `dict::field_traits<T>`/`decode_field<T>` is the **NET-NEW** `include/fixpp/dict/field_traits.hpp`. Both are `noexcept`; `field_traits` is allocation-free on the ≤ 20 ns hot path. Six `core::error` slots locked 23–28 (the 2b/wire field-absent error stays 2b-owned — no `dict_field_not_present` 003 slot).
- **Layer hygiene — RESOLVED (RC#3, re-`/plan` 2026-05-15).** `dictionary → core` (from 002) is clean; the codegen **host tool** `fixpp::dict` link is a clean host-side build edge (`[const §III.5]`). The hand-written `include/fixpp/dict/reify.hpp`/`field_traits.hpp` `#include <fixpp/wire/...>` + the vendored R6 stub written into `include/fixpp/wire/` are now covered by the **`arch §2.4` v0.2→v0.3 dual-compile bridge-surface carve-out** (`[const §XX]` amendment): the bridge surface compiles against both modules and is **not** a `dictionary` module link edge — no cycle (a literal `dictionary→wire` whitelist edge was *rejected*; it would create the forbidden `wire↔dictionary` cycle). `arch §2.3` carries a bridge-carve-out note; `tools/check_layers.py` is taught a comment-documented bridge file-list (`BRIDGE_SOURCE_FILES`/`BRIDGE_EXEMPT_INCLUDES`) and runs clean (exit 0). NFR-003-8 updated from "open item" to "resolved via arch §2.4 v0.3".
- `dict::reify*` are `noexcept` free function templates; PMR OOM surfaces as `dict_reify_oom` via `[2a §4.2]` `trap_throw` (AC-R7; spec §4.3).

**Scale/Scope:** 1 build-only host tool (`tools/codegen/fixpp-codegen`, ~6–10 C++ source files) + 2 runtime bridge headers (`reify.hpp`, `version_registry.hpp`) + the RC#1 owned surface (`version_profile.hpp` additive edit + the NET-NEW `field_traits.hpp`) + 1 vendored wire contract stub header + possibly 2 small bridge `.cpp` + the `[2c §7.6]` CMake target graph + ~16 test files + 3 bench harnesses + 4 golden headers (generated codegen output, checked in at `/implement` — not present at Gate A) + the conformance must-include manifest. Generated (build-tree, not counted as source): `{v42,v44,v50sp2,vt11}/{Messages,Fields,Validator,Reify,NormativeReferences}` + 2 `_dispatch/` headers. Roughly ~4800 LOC of hand-written tool + bridge + tests (estimate; generated output is mechanical, not hand-maintained). Reuses the four `dictionaries/{FIX42,FIX44,FIX50SP2,FIXT11}.xml` checked in by 002 — **no new XML in this PR** (spec §A1).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-evaluated post-Phase 1 design.*

All citations use canonical form `[const §<Roman>.<arabic>]` per `constitution.md:5`. Every cite re-verified against the constitution after Phase 1 (see Citation verification pass).

| Article cited | Topic | How this feature satisfies it |
|---|---|---|
| `[const §II.1]` | C++23, no earlier fallback | Library + the `fixpp-codegen` host tool target C++23 only; uses `std::pmr`, `std::span`, `std::expected` (via `core::expected_t`). |
| `[const §III.2]` | Conan dependency manager, pinned | **No new Conan row.** F1 Candidate A reuses 002's `pugixml/1.14` transitively through `fixpp::dict`. |
| `[const §III.5]` | `tools/` is build-only; codegen runs at configure | `fixpp-codegen` runs at configure time via `fixpp::dict::generate-vXX`; never linked into the user-facing library; outputs to build tree (AC-T2 / AC-C4; `[arch §4.2]` step 3). |
| `[const §V.1]`, `[const §V.3]` | AGPL-3.0 + commercial dual; no LGPL deps | No new dependency admitted (F1 Candidate A). pugixml (reused from 002) is MIT — already cleared. Generated headers carry `SPDX-License-Identifier: AGPL-3.0-or-later`. |
| `[const §VI.4]`, `[const §VI.5]` | Bidirectional traceability + Normative References | Inherits **D-008, OSS-010** from spec front-matter (D-010 dropped Gate A r1); spec §13 lists the exact references. Generated `NormativeReferences.md` is the per-message `[const §VI.5]` mechanism (AC-V5). |
| `[const §VII.1]`, `[const §VII.3]` | GoogleTest + TDD | `tasks.md` ordered red-green-refactor per seam; every test target is GoogleTest. |
| `[const §VII.7]` | Fuzzing on parser-touching modules | **Satisfied without a new harness.** F1 Candidate A introduces **no new parser** (research.md D-1 / D-9). |
| `[const §VIII.1]`, `[const §VIII.2]` | Google Benchmark + ±5 % budget | Three bench harnesses run in Tier 1 with `bench/baselines/`. |
| `[const §VIII.5]` | Zero allocation on the hot path | Generated `constexpr` tables = static storage, no `new`/`delete` (NFR-003-5); string/int/char read path zero-alloc; decimal zero-alloc for default `pod_decimal`, allocating-trait heap traffic confined to the caller arena, never raw `new`/`delete` (AC-G4a; NFR-003-4); `owning_<Msg>` ≤ 4 PMR allocs, none outside `mr` (AC-R7). |
| `[const §IX.1]` | ≥ 90 % line / ≥ 80 % branch on touched modules | `linux-clang-coverage` measures `tools/codegen/*`, `include/fixpp/dict/{reify,version_registry,version_profile,field_traits}.hpp`, the vendored wire contract header, and bridge `.cpp`; Tier-1 gate. |
| `[const §IX.2]` | Tier-1 sanitizers (ASan + UBSan + TSan) | ASan + UBSan: every codegen/reify test. TSan: `reify_cross_strand_test` (AC-R5 / AC-T3 / seam #12). |
| `[const §IX.4]` | Tier-1 static analysis clean | clang-tidy + clang-format + cppcheck + IWYU on the tool, the bridge, and a sample generated header; pre-commit + Tier-1. |
| `[const §IX.5]` | abidiff against last tagged ABI | **N/A this PR** — no C-ABI surface added (`fixpp_msg_reify` owned by 2i, spec §5). Cited to record explicit non-applicability (research.md D-17). |
| `[const §IX.6]` | Two-tier CI | Tier 1: every preset from quickstart §3. Tier 2: Windows manual / nightly. |
| `[const §X.4]` | Bounded `fixpp_error_t` + forwards-compat | Six new `dict_*` variants appended at **slots 23–28, LOCKED** (`dict_reify_msg_type_mismatch=23` … `dict_no_dictionary_for_application_version=28`; research.md D-10 / D-21; data-model "Error mapping"). Non-renumbering; existing slots verbatim; on-disk `error.hpp` verified ending at 22. The 2b/wire field-absent error from `get<1128>()` is **not** a 003 slot. C-ABI mapping + `tools/abi_history/error_codes_v1.txt` audit-trail deferred to 2i under the same time-bounded waiver shape as 002 D-10 (no C-ABI surface here; auto-expires at first C-ABI consumer commit). |
| `[const §XIV.2]` | ≤ 5 pure-virtual on pluggable interfaces | N/A — no new pluggable interface; `dict::version_registry` (AC-X*) is a concrete value type (research.md D-14). Cited to record non-applicability. |
| `[const §XV]` | Banned patterns — `thread_local` | Codegen never emits `thread_local`; the host tool/reify bridge use none. Verified by inspection + the `tools/check_alloc.py`/grep gate (NFR-003-5). |
| `[const §XV.6]` | Runtime-only field validation banned (codegen mandate) | This feature **is** the discharge of `[const §XV.6]` — `constexpr` field metadata + typed accessors generated from the dictionary; misuse fails to compile (AC-G1..G7). |
| `[const §XV.13]` | Hybrid mandate (codegen + runtime XML) | **Narrowed Gate A r1 (Codex P1-2 / Opus Downgrade P1→P2).** `[const §XV.13]`'s banned pattern (eager codegen with *no runtime dictionary path*) is discharged by **D-008 (this PR) + D-007 (002)**; D-009 is an additive overlay, not "the runtime path", and is not required for this check (spec §2). |
| `[const §XVI.3]` | `/clarify` MANDATORY pre-`/plan` | Ran 2026-05-15: 3 inline `/specify` clarifications + a `/clarify` session. Recorded in spec `Clarifications`. |
| `[const §XVI.4]` | `/analyze` MANDATORY post-`/plan` | Runs post-re-`/plan`, after the fresh Gate A CONVERGED (replan-loop round 3, commit 3824bb5), before `/tasks` (this `/speckit-analyze` pass). |
| `[const §XVII.1]` | Codex Gate A before `/tasks` (public C++ API + codegen layout) | `gate_a_required: yes`. Rounds 1+2 ran 2026-05-15 (exhausted → re-plan path); the **fresh** round ran on this re-planned bundle and **CONVERGED at replan-loop round 3** (Codex/Opus P1=0 P2=0 P3=0, commit 3824bb5); both Codex passes (rescue + `/codex:adversarial-review`) per `feedback_gate_a_codex_dual_pass.md`. F1, R6, **and the RC#1/#2/#3 resolutions + the two `[const §XX]` amendments** were explicitly flagged for that review and accepted. |
| `[const §XVII.2]` | Gate B before every merge | Standard Gate B precondition. |
| `[const §XVII.3]` | Independence between author and reviewer | Opus author (`/plan`) + Codex reviewer (Gate A) are independent agents per `/gate-a`. |
| `[const §XVII.7]` | Local pre-PR build gate | Contributor confirms `local build: green on linux-clang-debug @ <git-sha>`; agent surfaces `AskUserQuestion` before any local Conan/CMake build. |
| `[const §XVII.8]` | `/speckit-verify` mandatory after `/speckit-implement` | `/speckit-verify 003-dictionary-codegen` produces `.specify/decisions/003-dictionary-codegen-verify.md`; `GREEN` required for `gate-b-done`. |
| `[const §XVIII.2]` | Roadmap — FIX-Latest post-v1.0 | FIX-Latest A-035..A-065 codegen-filtered with a build warning, not emitted (AC-G9). |
| `[const §XVIII.7]` | Application-message codegen scope for v1.0 | A-014..A-034 not emitted as typed classes in v1.0 (AC-G10); conformance corpus excludes them. |
| `[const §XX]` | Amendments (constitution/design-doc/architecture) | **Two `[const §XX]` amendments applied at this re-`/plan` (2026-05-15), each carrying an RC marker, Article XX §2 review = the fresh Codex Gate A (CONVERGED replan-loop round 3, commit 3824bb5), user sign-off, `_log.md` entry:** (1) `2c-codegen.md` v1.3→v1.4 — RC#2 decimal-API coherence (already committed, 41dd8c1). (2) `architecture.md` §2.4 v0.2→v0.3 — RC#3 dictionary↔wire bridge-surface carve-out (no module cycle; the literal `dictionary→wire` edge rejected as cyclic). Recorded in `## Re-/plan (RC resolution)`. |

**Gates — citations OK; RC#1/#2/#3 RESOLVED in-bundle at re-`/plan`.** All cited articles resolve under canonical form (Citation verification pass). Gate A round 1 surfaced two false green-checks (since corrected: `[const §XV.13]` D-009 narrowed; `[const §VI.4]` D-010 dropped) and three root causes; the latter are now **closed in this re-`/plan`** (`## Re-/plan (RC resolution)`): RC#1 (003-owned surface materialised), RC#2 (2c v1.4 consumed), RC#3 (arch §2.4 v0.3 carve-out). `Complexity Tracking` remains empty — no *justified* constitution violation; the two `[const §XX]` amendments are the sanctioned resolution path, not violations. `blocked_on_replan` is cleared (front-matter `replan_applied`); `/tasks` is unblocked — the fresh Gate A CONVERGED at replan-loop round 3 (commit 3824bb5; see `## Gate A` → Round 3 (CONVERGED)).

## Re-/plan (RC resolution)

This section is the headline output of the 2026-05-15 re-`/plan`. Gate A rounds 1+2 (both 2026-05-15) exhausted into the re-`/plan` path with three root causes the prior bundle correctly recorded as **not text-patchable** and **gating `/tasks` via `blocked_on_replan`**. The re-`/plan` closes all three in-bundle.

### RC#1 — `version_profile` + `field_traits` are 003-owned (CLOSED)

- **Was:** the bundle specified `dict::reify` against `version_profile`/`dict::resolve_application_version` and the typed accessors against `dict::field_traits<T>`/`decode_field<T>`, asserting them "002-shipped". 002 ships only the `session_version`/`application_version` enums and **deferred** the `version_profile` struct + `resolve_application_version` free function (`specs/002-dictionary-xml-loader/contracts/version_profile.hpp:59-66`); it ships **no** `field_traits.hpp`. Verified on disk 2026-05-15: `include/fixpp/dict/version_profile.hpp` = two enums only; `include/fixpp/dict/field_traits.hpp` = absent.
- **Now (003-owned):**
  - `include/fixpp/dict/version_profile.hpp` — **MODIFIED (additive edit)**: append the `version_profile` + `resolved_message_version` structs (+ their `static_assert`s) and the `dict::resolve_application_version` free-function declaration **below** the unchanged 002 enums, non-renumbering (same discipline as the `core/error.hpp` additive edit, D-10). Pinned: `contracts/version_profile.hpp`; data-model Entity 10; spec §4.8 ACs **AC-VP1..AC-VP6**; the wire `ApplVerID(1128)`→C++ `application_version` enum-mapping table reproduced verbatim from `2c §4.3:486-501` (AC-VP3 + the AC-VP4 negative test that the C++ index is *not* reused — N2-P3-1).
  - `include/fixpp/dict/field_traits.hpp` — **NEW (003-owned)**: primary `field_traits<T>` + the string_view/char/int/bool/timestamp/MultiChar-MultiString specialisations + `decode_field<T>`; `decimal_t` excluded by design. Pinned: `contracts/field_traits.hpp`; data-model Entity 11; spec §4.8 ACs **AC-FT1..AC-FT3**.
  - Error taxonomy: six `core::error` slots LOCKED 23–28 (AC-VP6; data-model "Error mapping"; research D-10/D-21). The 2b/wire field-absent error from `get<1128>()` is **2b-owned**, not a 003 slot — explicit cross-feature note prevents an RC#1-class phantom-ownership repeat.
- **Effect:** unblocks AC-D3/AC-D4, story 3.3, the `dict::reify` runtime-dispatch half, and the AC-G4 string/int/char typed-decoding layer.

### RC#2 — decimal route re-derived from corrected 2c v1.4 (CLOSED)

- **Was:** an **inherited design-doc defect** in signed-off `2c-codegen.md` v1.3 (`decimal_t::from_chars(fv->bytes())` — a phantom no-`mr` symbol absent on merged 001/2a; 2a's Gate A removed the no-`mr` form). Un-fixable by a bundle edit; required a `[const §XX]` 2c reopen.
- **Now:** `2c-codegen.md` is amended **v1.3→v1.4** (commit 41dd8c1) — the decimal accessor takes an explicit `std::pmr::memory_resource* mr` and calls `decimal_t::parse(fv->bytes(), mr)` (`[2a §4.3]`). AC-G7's `sizeof == one pointer` flyweight invariant is **preserved** (`mr` is a caller-threaded parameter, not a member). Re-derived: `contracts/generated_message.hpp` `price(mr)`, AC-G4, AC-G4a, NFR-003-4 (decimal arm), data-model Entity 1 + PMR accounting. The `[const §XX]` design-doc-amendment precondition (`## Gate A` → Round 1) is now **CLEARED**.

### RC#3 — dict↔wire bridge edge resolved via arch §2.4 v0.3 carve-out (CLOSED)

- **Was:** the `arch §2.4` carve-out covered only *generated* `fixpp::vXX::*` headers; it did not reach the hand-written `include/fixpp/dict/reify.hpp`/`field_traits.hpp` `#include <fixpp/wire/...>` nor the R6 stub written into `include/fixpp/wire/`. An open `[const §XX]` / `arch §2.3` amendment item.
- **Decision (and the alternative explicitly rejected):** amend `arch §2.4` **v0.2→v0.3** (`[const §XX]`) so the carve-out names the full **dictionary↔wire bridge surface** — the generated tree **+** the hand-written `dict/` bridge headers **+** the vendored frozen `wire/message_view_contract.hpp` stub — as a header-only **dual-compile bridge** that is **not** a `dictionary` module link edge and creates **no cycle**. The other floated option — "add a literal `dictionary → wire` edge to the `arch §2.3` whitelist" — is **rejected**: `arch §2.3` already grants `wire → dictionary`, so a `dictionary → wire` edge would form the **forbidden `wire ↔ dictionary` cycle** (`arch §2.2`/`§2.3` ban cycles). Relocating the bridge header out of `dict/` (the third option) is dishonest — the namespace is `fixpp::dict::reify` and the header genuinely *is* dual-compile glue, exactly like the generated tree the carve-out already blesses. The principled fix is to make the carve-out say what it always meant.
- **Now applied:** `arch §2.4` v0.3 amendment + an `arch §2.3` "Bridge-surface carve-out" note + `tools/check_layers.py` taught a comment-documented `BRIDGE_SOURCE_FILES`/`BRIDGE_EXEMPT_INCLUDES` exemption (the `dictionary | core` whitelist is otherwise unchanged). `tools/check_layers.py` runs clean (exit 0, verified). The vendored stub stays FROZEN/R6-tracked; 2b replaces its body at 2b's Gate B (`dictionary` does not become `wire/`'s owner). **Non-blocking follow-up:** the scanner walks `src/`+`bindings/` only — extending it to `include/` so the rule is *enforced* there (not merely *stated*) is tracked (research.md D-23); the load-bearing RC#3 resolution is the `arch §2.4` rule amendment, which the fresh Gate A reviewed and accepted per Article XX §2 (CONVERGED replan-loop round 3, commit 3824bb5).

### Process / status

Both `[const §XX]` amendments carry an RC marker (mirroring the 2c v1.4 amendment style). Article XX §2 closing steps — Codex Gate A review on the amendment + user sign-off + `_log.md` entry — are discharged by the **fresh** Codex Gate A on this re-planned bundle, which **CONVERGED at replan-loop round 3** (commit 3824bb5; the amendments were explicitly flagged for it and accepted), plus the user's `/plan` sign-off. `spec.md` is preserved (it carries the /clarify Q&A + Gate A round records); the RC#1/#2/#3 ACs are *added*, the historical Gate A Q&A entries are *annotated with RESOLVED notes*, not rewritten.

## Project Structure

### Documentation (this feature)

```text
specs/003-dictionary-codegen/
├── plan.md              # this file (/speckit-plan — re-`/plan` 2026-05-15)
├── spec.md              # /specify 2026-05-15; carries /clarify Q&A + Gate A records + RC#1/#2/#3 ACs
├── research.md          # Phase 0 — design decisions D-1..D-23 (D-21/22/23 = RC resolutions)
├── data-model.md        # Phase 1 — 11 entities (Entity 10 version_profile, Entity 11 field_traits), invariants, error mapping, PMR accounting
├── quickstart.md        # Phase 1 — build / codegen / test / bench / sanitizer / coverage / verify / gate
├── contracts/
│   ├── reify.hpp                       # [2c §4.8] — dict::reify_as / dict::reify / owning_message_handle
│   ├── version_profile.hpp             # [2c §4.3] — 003-OWNED (RC#1): version_profile/resolved_message_version/resolve_application_version + ApplVerID wire→C++ map
│   ├── field_traits.hpp                # [2c §4.1.3] — 003-OWNED NEW (RC#1): field_traits<T>/decode_field<T>
│   ├── version_registry.hpp            # [2c §4.9] — dict::version_registry shape only
│   ├── generated_message.hpp           # [2c §4.7] — codegen typed-message class shape (price(mr) v1.4)
│   ├── reify_dispatch.hpp              # [2c §4.8]/[2c §6.3] — the two _dispatch/ switch-header shapes
│   └── wire_message_view_contract.hpp  # vendored frozen [2b §4.3]/[2b §4.7] stub (R6)
├── checklists/          # /checklist output (already present)
└── tasks.md             # Phase 2 (/speckit-tasks, NOT created by /speckit-plan)
```

### Source Code (library submodule root)

```text
tools/
└── codegen/
    └── fixpp-codegen/                    # NEW — C++23 build-only host tool (F1 Candidate A)
        ├── CMakeLists.txt                # NEW — host executable; links fixpp::dict (002 runtime)
        ├── main.cpp                      # NEW — CLI driver
        ├── ir.hpp / ir.cpp               # NEW — XML→IR: XmlLoader::load(); walk Dictionary metadata
        ├── emit_messages.cpp             # NEW — <vXX>/Messages.hpp ([2c §4.7])
        ├── emit_fields.cpp               # NEW — <vXX>/Fields.hpp (constexpr FieldRef/ComponentRef/GroupRef)
        ├── emit_validator.cpp            # NEW — <vXX>/Validator.hpp + Length+Data pair table
        ├── emit_reify.cpp                # NEW — <vXX>/Reify.hpp (owning_<Msg>, [2c §4.8])
        ├── emit_dispatch.cpp             # NEW — _dispatch/{fixt,application}.hpp (shared switches)
        ├── emit_normative_refs.cpp       # NEW — <vXX>/NormativeReferences.md
        └── template_writer.hpp           # NEW — deterministic locale-independent string-templating

include/
└── fixpp/
    ├── dict/
    │   ├── version_profile.hpp           # MODIFIED (additive — RC#1) — 002 ships the two enums;
    │   │                                 #   003 appends version_profile + resolved_message_version
    │   │                                 #   + dict::resolve_application_version + ApplVerID wire→C++
    │   │                                 #   map, BELOW the unchanged enums, non-renumbering.
    │   ├── field_traits.hpp              # NEW (003-owned — RC#1) — field_traits<T> + decode_field<T>
    │   │                                 #   ([2c §4.1.3]); 002 ships none. Bridge header (arch §2.4 v0.3).
    │   ├── reify.hpp                     # NEW — dict::reify_as / dict::reify / owning_message_handle
    │   │                                 #   ([2c §4.8]). Bridge header (arch §2.4 v0.3 carve-out).
    │   └── version_registry.hpp          # NEW — dict::version_registry shape only ([2c §4.9]; → F3/2d)
    └── wire/
        └── message_view_contract.hpp     # NEW (vendored, FROZEN — R6) — [2b §4.3]/[2b §4.7] surface
                                          #   stub; bridge surface (arch §2.4 v0.3). 2b replaces the
                                          #   body against this same contract; drift-guarded.

include/fixpp/core/
└── error.hpp                             # MODIFIED (additive) — six dict_* variants LOCKED at slots
                                          #   23..28 (research.md D-10/D-21; data-model "Error mapping").
                                          #   Non-renumbering; [const §X.4]-forwards-compatible.

src/
└── dictionary/
    ├── CMakeLists.txt                    # MODIFIED — add reify.cpp / version_registry.cpp if any
    │                                     #   out-of-line bits are needed (final split at /tasks, D-5).
    ├── reify.cpp                         # NEW (maybe) — out-of-line reify bridge bits if needed.
    │                                     #   check_layers.py BRIDGE_SOURCE_FILES exemption (arch §2.4 v0.3).
    └── version_registry.cpp              # NEW (maybe) — out-of-line version_registry bits if needed.

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
│   │   ├── must_include_manifest.txt     # NEW — checked-in curated CI subset manifest (AC-G12)
│   │   └── conformance_test.cpp          # NEW — seam #1/#15b — parameterised round-trip corpus
│   ├── typed_accessor_test.cpp           # NEW — AC-G1..G8, AC-G11 — typed field access
│   ├── msgtype_boundary_test.cpp         # NEW — AC-G9 / AC-G10
│   ├── flyweight_shape_test.cpp          # NEW — seam #18 — AC-G7 + AC-G7a (per-message
│   │                                     #   owning_message_traits<Msg> specialisation pin, RC#1) + R6 drift
│   │                                     #   guard + get<1128>/get<35> well-formed (N-P3-1/N-P2-2)
│   ├── determinism_test.cpp              # NEW — seam #1/#2 — NFR-003-7/AC-T1/AC-T2 vs 4 golden headers
│   ├── codegen_build_graph_test.cmake    # NEW (RC#4) — CTest fixture: asserts the configure-time
│   │                                     #   build-tree codegen graph (AC-C4). Registered as the
│   │                                     #   `fixpp::dict::codegen-build-graph-check` CTest target
│   │                                     #   (a `cmake -P` script test, NOT a GoogleTest C++ TU).
│   │                                     #   Asserts: (a) build/<preset>/_codegen/include/fixpp/...
│   │                                     #   exists post-configure; (b) `fixpp::dict::generate-vXX`
│   │                                     #   is a configure-time custom target (not build-time);
│   │                                     #   (c) the per-version INTERFACE targets carry
│   │                                     #   INTERFACE_INCLUDE_DIRECTORIES into the build tree;
│   │                                     #   (d) nothing written under the source tree.
│   ├── length_data_table_test.cpp        # NEW — seam #19 — AC-V4 exhaustive vs source XML
│   └── validator_shape_test.cpp          # NEW — AC-V1..V3, AC-V5, AC-V6
├── dictionary/
│   ├── reify_test.cpp                    # NEW — AC-R1..R3, AC-R6, AC-R8
│   ├── reify_dispatch_test.cpp           # NEW — seam #15a/#15b/#15c, #10c — AC-D1..D7
│   ├── reify_move_test.cpp               # NEW — seam #14 — AC-R4 + static_asserts
│   ├── reify_cross_strand_test.cpp       # NEW — seam #12 — AC-R5 (TSan target)
│   ├── reify_oom_test.cpp                # NEW — seam #7/#16 — AC-R7
│   ├── version_registry_test.cpp         # NEW — AC-X1..X3 — shape-only, in-test hand-built registry
│   ├── version_profile_test.cpp          # NEW (RC#1) — AC-VP1..AC-VP6: structs/static_asserts,
│   │                                     #   resolve_application_version free fn, full ApplVerID
│   │                                     #   wire→C++ map, the AC-VP4 negative (C++ index NOT reused),
│   │                                     #   _reserved discipline, the six error slots
│   └── field_traits_test.cpp             # NEW (RC#1) — AC-FT1..AC-FT3: primary+specialisations+
│                                         #   decode_field; AC-FT2 negative (decimal_t excluded)
├── integration/
│   ├── multi_session_multi_version.cpp   # NEW — seam #10a — AC-C1..C3
│   └── fixt_cross_vocabulary.cpp         # NEW — seam #10b — AC-D4 worked example
└── (fuzz/: NO new harness — 002's fuzz_dict_xml_loader.cpp covers the XML input; spec §7)

bench/
├── codegen/
│   ├── CMakeLists.txt                    # NEW — wire benches into Tier-1 release preset
│   ├── typed_accessor_bench.cpp          # NEW — NFR-003-1 (incl. price(mr) decimal arm, v1.4)
│   └── compile_time_bench/               # NEW — NFR-003-2
└── dictionary/
    └── reify_bench.cpp                   # NEW — NFR-003-3 + codegen-lookup arm (seam #5)

specs/003-dictionary-codegen/contracts/golden/  # GENERATED-then-checked-in AT /implement (NOT
                                          #   present at Gate A; 4 total; /clarify Q-golden → A)
├── v42_Messages.golden.hpp               # /implement deliverable
├── v44_Messages.golden.hpp               # /implement deliverable
├── v50sp2_Messages.golden.hpp            # /implement deliverable
└── vt11_Messages.golden.hpp              # /implement deliverable

.specify/architecture.md                   # MODIFIED (RC#3, [const §XX]) — §2.4 v0.2→v0.3 bridge-
                                          #   surface carve-out + §2.3 carve-out note + status line
.specify/2c-codegen.md                     # already MODIFIED v1.3→v1.4 (RC#2, commit 41dd8c1)
tools/check_layers.py                      # MODIFIED (RC#3) — BRIDGE_SOURCE_FILES/BRIDGE_EXEMPT_INCLUDES
CMakeLists.txt / cmake/                    # MODIFIED — the [2c §7.6] target graph + generate-vXX
docs/src/dictionary/codegen.md             # NEW — how codegen runs / CMake targets / accessor model / reify
```

**Structure Decision:** single library, no web/mobile/cli split; follows the Phase-3 layout. The build-only host tool lives under `tools/codegen/fixpp-codegen/` (`[const §III.5]`). Three `dict/` headers carry RC resolutions: `version_profile.hpp` **MODIFIED additively** (RC#1 — append below the unchanged 002 enums), `field_traits.hpp` **NEW** (RC#1 — 002 ships none), and `reify.hpp` **NEW** (the reify bridge). Per the `arch §2.4` v0.3 amendment (RC#3), `reify.hpp`/`field_traits.hpp` + the vendored `wire/message_view_contract.hpp` are the **dictionary↔wire bridge surface** (header-only dual-compile glue, no module cycle). `core/error.hpp` is modified additively (six slots locked 23–28).

### Test seam → file mapping (closes spec.md §9 — every seam bound to a named on-disk file)

Same root-cause class that closed 001/002 Gate A round 1 ("seam→file map partial"). Every test seam in `spec.md §9` is bound to a named on-disk file; cross-cutting per-AC tests get their own files. Seam numbers match `[2c §9]`; the gaps (#4/#8/#9/#11/#13/#17/#20) are the deliberate DialectOverlay / XmlLoader-fuzz / table_view exclusions documented in spec §9, **not** omissions.

| Seam # | spec.md §9 description | On-disk path | NFR / AC linkage |
|---|---|---|---|
| 1 | Conformance corpus (CI curated subset + nightly exhaustive) | `tests/codegen/conformance/conformance_test.cpp` + `must_include_manifest.txt` | AC-G1..G6, AC-G12, AC-R3 |
| 2 | Compile-time cost regression (single-version ≤3s / all-versions ≤15s soft) | `bench/codegen/compile_time_bench/` | NFR-003-2 |
| 3 | Per-tag accessor latency regression (incl. `price(mr)` decimal arm, v1.4) | `bench/codegen/typed_accessor_bench.cpp` | NFR-003-1 |
| 5 | Codegen lookup latency regression (on the codegen-emitted tables) | `bench/dictionary/reify_bench.cpp` (lookup arm) | NFR-003-1 (table side) |
| 6 | Reify latency regression (reify_as 20/200-tag; reify ≤1.2µs; move-across-thread smoke) | `bench/dictionary/reify_bench.cpp` | NFR-003-3 |
| 7 | Allocation guard (string/int/char read = 0; default-trait decimal = 0; reify ≤4 PMR; `mallocnesia`) | `tests/dictionary/reify_oom_test.cpp` + `tools/check_alloc.py` driver | NFR-003-4, AC-R7, AC-T3, AC-G4a |
| 10a | Multi-version coexistence (no namespace bleed) | `tests/integration/multi_session_multi_version.cpp` | AC-C1, AC-C2, AC-C3 |
| 10b | FIXT.1.1 cross-vocabulary worked example | `tests/integration/fixt_cross_vocabulary.cpp` | AC-D4 |
| 10c | Runtime-XML-only versions → `dict_reify_unknown_msg_type` | `tests/dictionary/reify_dispatch_test.cpp` (negative arm) | AC-D5 |
| 12 | `owning_<Msg>` cross-strand handoff (reify A → move → consume B; original traps post-reset) | `tests/dictionary/reify_cross_strand_test.cpp` (TSan) | AC-R5, AC-T3 |
| 14 | `owning_<Msg>` move + lazy view rebuild (+ static_asserts: no ref members, nothrow-move, not `=default`) | `tests/dictionary/reify_move_test.cpp` | AC-R4 |
| 15a | `dict::reify` — 7 FIXT admin MsgTypes | `tests/dictionary/reify_dispatch_test.cpp` (FIXT arm) | AC-D1, AC-D2 |
| 15b | `dict::reify` — application MsgTypes (CI = AC-G12 subset; nightly exhaustive) | `tests/dictionary/reify_dispatch_test.cpp` (app arm) | AC-D1, AC-D3, AC-D7 |
| 15c | `dict_unresolved_application_version` propagation | `tests/dictionary/reify_dispatch_test.cpp` (unresolved arm) | AC-D6 |
| 16 | `trap_throw` PMR OOM injection (reify_as / reify / owning_<Msg>::from_view → `dict_reify_oom`) | `tests/dictionary/reify_oom_test.cpp` | AC-R7 |
| 18 | Static-assert typed-flyweight size per message + per-message `owning_message_traits<Msg>` specialisation pin (codegen-shape golden, compile-time) | `tests/codegen/flyweight_shape_test.cpp` | AC-G7, AC-G7a |
| 19 | Length+Data static-table coverage vs source XML + `[FIX50SP2 §3.3]` | `tests/codegen/length_data_table_test.cpp` | AC-V4 |
| C4 (CMake-graph) | Configure-time, build-tree-only codegen graph (RC#4 — `generate-vXX` is a configure-time target; outputs under `build/<preset>/_codegen/...`; per-version INTERFACE targets carry `INTERFACE_INCLUDE_DIRECTORIES` into the build tree; nothing in the source tree) | `tests/codegen/codegen_build_graph_test.cmake` → CTest target `fixpp::dict::codegen-build-graph-check` | AC-C4, DoD §12 (build-tree/configure-time clause) |

**Cross-cutting per-AC tests** (not "seam files" per §9, but binding one AC family to one file):

| File | ACs covered |
|---|---|
| `tests/codegen/typed_accessor_test.cpp` | AC-G1..G8, AC-G11 (typed field access incl. the `price(mr)` v1.4 decimal accessor + AC-G4a default/allocating-trait behaviour). **AC-G7a** (the per-message `owning_message_traits<Msg>` specialisation pin — the canonical 2c v1.4 §4.8 form, RC#1) is verified by the codegen-shape golden in `flyweight_shape_test.cpp` / seam #18, not here. |
| `tests/codegen/flyweight_shape_test.cpp` | AC-G7 (sizeof), **AC-G7a** (the per-message `owning_message_traits<Msg>` specialisation pin, compile-time — RC#1), R6 drift guard (`get<1128>()` + `get<35>()` well-formed — N-P3-1/N-P2-2) — seam #18 |
| `tests/codegen/codegen_build_graph_test.cmake` (CTest target `fixpp::dict::codegen-build-graph-check`) | **AC-C4** (RC#4 — configure-time / build-tree-only / `INTERFACE_INCLUDE_DIRECTORIES` / `generate-vXX` is a configure-time target; DoD §12 build-tree clause). CMake-graph script test, not a GoogleTest C++ TU. |
| `tests/codegen/msgtype_boundary_test.cpp` | AC-G9 (FIX-Latest filtered + warning), AC-G10 (A-014..A-034 not emitted) |
| `tests/codegen/validator_shape_test.cpp` | AC-V1, AC-V2, AC-V3, AC-V5, AC-V6 |
| `tests/codegen/determinism_test.cpp` | NFR-003-7, AC-T1, AC-T2 (byte-identical re-emission vs the 4 golden headers; no source-tree write) |
| `tests/dictionary/reify_test.cpp` | AC-R1, AC-R2, AC-R3, AC-R6, AC-R8 |
| `tests/dictionary/version_registry_test.cpp` | AC-X1, AC-X2, AC-X3 (shape only; in-test hand-built registry) |
| `tests/dictionary/version_profile_test.cpp` | **AC-VP1..AC-VP6** (RC#1 — version_profile/resolved_message_version structs+static_asserts, `resolve_application_version` free fn, full ApplVerID wire→C++ map, the AC-VP4 negative, `_reserved` discipline, the six locked error slots) |
| `tests/dictionary/field_traits_test.cpp` | **AC-FT1..AC-FT3** (RC#1 — primary + specialisations + `decode_field`; AC-FT2 negative: `decimal_t` not a `field_traits` specialisation) |

**Rule:** no seam maps to "the existing tests collectively"; **every AC in §4 (including AC-G7a and AC-C4)** is addressable by at least one dedicated named on-disk file or CTest target. The 11 cross-cutting entries (6 prior + the 2 RC#1 files + the explicit `flyweight_shape_test.cpp` AC-G7a row + the RC#4 `codegen_build_graph_test.cmake` CTest target + the determinism row) supplement the seam files for per-AC verification. The AC-C4 CMake-graph properties (configure-time / `INTERFACE_INCLUDE_DIRECTORIES` / `generate-vXX`) are not C++-seam-testable, so they bind to the dedicated `cmake -P` CTest target rather than a GoogleTest TU — the seam→file completeness rule is satisfied without forcing a CMake-graph property into a C++ test that cannot assert it.

## Complexity Tracking

> No Constitution Check violations. The two `[const §XX]` amendments (2c v1.4, arch v0.3) are the sanctioned amendment path, not violations. Section intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |

## Gate A

- Round 1 applied 2026-05-15: Codex P1=3 P2=3 P3=1; Opus post-judging P1=4 P2=4 P3=4; rewrite addresses root causes RC#1 (phantom/unassigned upstream `version_profile`/`resolve_application_version`/`field_traits`+`decode_field` surface — 002 deferred/never-shipped; restated as 003-owned blocking surface, re-`/plan`), RC#2 (inherited-from-2c `decimal_t::from_chars` incoherent with merged 2a/001 — documented as un-fixable inherited design-doc defect; 2c §4.1.3/§4.7 reopen required), RC#3 (hand-written `dict/reify.hpp → wire/` include + new `wire/` header write not covered by the arch §2.4 generated-header carve-out — narrowed NFR-003-8, open `arch §2.3`/`[const §XX]` amendment). Reviews: research/reviews/codex_003-dictionary-codegen_gate_a_review.md, research/reviews/opus_003-dictionary-codegen_gate_a_adversarial_review.md.
- Round 2 applied 2026-05-15: Codex P1=1 P2=1 P3=1; Opus post-judging P1=1 P2=1 P3=4; rewrite addresses RC#4 (present-tense overclaim of not-yet-existing bundle artifacts) + Confirmed P3s; RC#1/RC#2/RC#3 remain documented-as-blocking (re-/plan + [const §XX] 2c reopen) — not text-patchable. Reviews: research/reviews/codex_003-dictionary-codegen_gate_a_2_review.md, research/reviews/opus_003-dictionary-codegen_gate_a_2_adversarial_review.md.

### Re-`/plan` applied 2026-05-15 — RC#1/RC#2/RC#3 RESOLVED in-bundle (exhaustion → re-plan path)

Rounds 1+2 exhausted with RC#1/RC#2/RC#3 documented-as-blocking and `blocked_on_replan` gating `/tasks`. Per `[const §XVII.1]`'s exhaustion→re-plan path, this re-`/plan` closes all three in-bundle (full record: `## Re-/plan (RC resolution)`):

- **RC#1 — CLOSED.** `version_profile`/`resolved_message_version`/`resolve_application_version` (`contracts/version_profile.hpp` — additive edit to the 002 enums-only file) + `field_traits`/`decode_field` (`contracts/field_traits.hpp` — NET-NEW). New ACs AC-VP1..AC-VP6 / AC-FT1..AC-FT3 (spec §4.8); data-model Entities 10–11; error slots LOCKED 23–28; ApplVerID wire→C++ map reproduced (AC-VP3/VP4); seam→file bindings added (`version_profile_test.cpp`, `field_traits_test.cpp`). Files-in-scope reclassified.
- **RC#2 — CLOSED.** `2c-codegen.md` amended v1.3→v1.4 (commit 41dd8c1) — the `[const §XX]` design-doc-amendment precondition (below) is **CLEARED**. Decimal route re-derived (AC-G4/AC-G4a/NFR-003-4/Entity 1/`generated_message.hpp price(mr)`).
- **RC#3 — CLOSED.** `architecture.md` §2.4 amended v0.2→v0.3 (`[const §XX]`) — dictionary↔wire bridge-surface carve-out (no module cycle; the literal `dictionary→wire` whitelist edge rejected as cyclic). `arch §2.3` note + `check_layers.py` exemption applied; lint clean (exit 0). NFR-003-8 / spec R6 / research D-12→D-23 updated.

A **fresh** Codex Gate A ran on this re-planned bundle (round counter reset per the exhaustion→re-plan path) and **CONVERGED at replan-loop round 3** (Codex/Opus P1=0 P2=0 P3=0, user-signed-off 2026-05-15, commit 3824bb5; see `## Gate A` → Round 3 (CONVERGED)). Both Codex passes (rescue + `/codex:adversarial-review`) per `feedback_gate_a_codex_dual_pass.md`. **Explicitly flagged for that review and accepted:** F1, R6, the three RC resolutions, and the two `[const §XX]` amendments (2c v1.4 — already committed; arch v0.3 — applied here). Article XX §2 closing (amendment review + user sign-off + `_log.md`) is discharged by that fresh Gate A + the user's `/plan` sign-off.

### Round 1 — disagreements

No Codex finding was ruled `Disagree` by the authoritative Opus adversarial review. Opus dispositions: Codex P1-1 **Confirm @ P1** (RC#1); Codex P1-2 **Downgrade P1 → P2** (D-009 is an additive overlay, not the `[const §XV.13]` runtime path — the substantive constitutional obligation is met by D-007+D-008; defect is a wording overclaim, applied as a P2 cite-accuracy narrowing, not a P1 structural hole); Codex P1-3 **Confirm @ P1** (RC#3; the arch §2.4 generated-header carve-out narrows blast radius but does not reach the hand-written bridge header — applied); Codex P2-1 **Escalate P2 → P1** (RC#2 — inheritance-integrity break, applied as documented inherited defect); Codex P2-2 / P2-3 **Confirm @ P2** (applied); Codex P3-1 **Confirm @ P3** (applied as an explicit checklist exception line). Plus Opus new findings N-P1-1 (decimal accessor PMR/arena hole — applied as AC-G4a, blocked on RC#2), N-P2-1 (`owning_message_t<>` undefined — applied: alias pinned in `contracts/reify.hpp`), N-P2-2 (`field_traits.hpp` consumed-but-unscoped — folded into RC#1 / spec §8), N-P3-1 (`get<1128>()` contract-test gap — applied below), N-P3-2 (golden anchors only `Messages.hpp` — residual-risk recorded below). Nothing recorded here as a rejected Codex fix because none was rejected.

### Round 1 — design-doc amendment precondition (`[const §XX]`)

RC#2 cannot be closed by any edit to the `specs/003-dictionary-codegen/` bundle: `[2c §4.1.3]`/§4.7 (`.specify/2c-codegen.md:270-271,1040`) bake `fixpp::decimal_t::from_chars(fv->bytes())` into **signed-off `2c-codegen.md` v1.3**, and that symbol does not exist on the merged 001/2a surface (only PMR-mandatory `decimal_traits<T>::from_chars(span,mr)` / `decimal<T>::parse(span,mr)`; 2a's own Gate A removed the no-PMR form — `specs/001-core-decimal/contracts/decimal_traits.hpp:98-100,123-128,162-163`). Per `[const §XX]` the design doc must be amended before AC-G4 / AC-G4a / NFR-003-4 (decimal arm) / the data-model PMR accounting can close. **Action item (out of this convergence pass):** reopen 2c §4.1.3/§4.7 to route decimal through the real PMR-mandatory entry point with an arena threaded into the typed accessor (and reconcile the flyweight `sizeof == one pointer` invariant against needing an `mr` — Opus N-P1-1), then re-run `/plan` for 003. The bundle records this conflict (spec AC-G4, data-model Entity 1, `contracts/generated_message.hpp` `price()` annotation) and does **not** paper it over with a fabricated fix.

> **CLEARED at re-`/plan` 2026-05-15.** `2c-codegen.md` was amended **v1.3→v1.4** (commit 41dd8c1, `[const §XX]`): the decimal route is now PMR-mandatory `decimal_t::parse(fv->bytes(), mr)` with an explicit `std::pmr::memory_resource* mr` threaded into the typed accessor; the `sizeof == one pointer` invariant is preserved (`mr` is a parameter, not a member — Opus N-P1-1 addressed). `/plan` was re-run (this document). AC-G4 / AC-G4a / NFR-003-4 (decimal arm) / data-model Entity 1 + PMR accounting are re-derived from v1.4 and no longer blocked. See `## Re-/plan (RC resolution)` → RC#2; research.md D-22.

### Round 1 — new contract-test / residual-risk items (Opus N-P3-1 / N-P3-2)

- **N-P3-1 (P3) + N-P2-2 (P2, replan loop round 1):** the R6 drift-guard contract test (folded into `flyweight_shape_test.cpp`) asserts the flyweight member signatures + `sizeof` invariant but does **not** assert `view.template get<1128>()` (the `dict::reify` ApplVerID-resolution entry, AC-D3) **nor** `view.template get<35>()` (the MsgType-peek entry that *both* `reify_as`'s msg-type-mismatch arm (AC-R8) and `reify`'s FIXT/application dispatch (AC-D2) depend on) compile against the frozen `wire_message_view_contract.hpp` surface. **Single fix (Opus N-P2-2):** extend the R6 drift-guard in `flyweight_shape_test.cpp` (or `reify_dispatch_test.cpp`) with a `static_assert`/instantiation that **both** `view.template get<1128>()` *and* `view.template get<35>()` are well-formed against the frozen contract, so a future 2b tag-whitelist constraint fails loud at compile time. Referenced from AC-R8 / AC-D2 / AC-D3. Bind at `/tasks` (added to the R6 contract-test task). *(Re-`/plan` + replan loop round 1: bound in the Project-Structure tree at `flyweight_shape_test.cpp` — "+ get<1128>/get<35> well-formed (N-P3-1/N-P2-2)".)*
- **N-P3-2 (P3, accepted residual risk):** the determinism golden set anchors only `<vXX>_Messages.golden.hpp` (D-16). `Reify.hpp` (custom move bodies, the most correctness-sensitive artifact, R4) and `_dispatch/*.hpp` (~470 cases, R3) have no golden — a template change perturbing a move body or dispatch-case ordering is byte-stable run-to-run (passes determinism) yet invisible to the reviewed-diff mechanism D-16 sells as the R4/R3 mitigation. **Decision:** deliberate scoping gap accepted for v1.0; residual R4/R3 risk explicitly named here (was previously unstated). Revisit (extend the golden set to one `Reify.hpp` + one `_dispatch` golden) if a move-body/dispatch regression escapes review in practice. **This acceptance is plan-local** — it is recorded *here*, not in spec §11; the spec §11 R3/R4 mitigations stand as written and do not (and need not) restate this golden-coverage residual (corrected Gate A round 2, Codex F-3 / Opus Confirm @ P3 — the earlier "Recorded in spec §11 R3/R4" clause was a false cross-reference and is struck). Not a Gate A blocker.

### Round 2 — disagreements

Opus (authoritative adversarial review) **Downgraded Codex round-2 F-2 from P2 to P3** and its fix was **not applied as Codex stated**. Recorded per the independence/disagreement-record discipline:

- **Codex F-2 (P2 → P3, Opus Downgrade).** Codex F-2 objected that `spec.md` §8 "Depends on (in-tree, merged)" folds `core::error`/`expected_t<T>`/`trap_throw_or_throw` into the "002-shipped dictionary surface … all on `main` via PR #66" and demanded the line split into "002-shipped dictionary surface" vs. "**pre-existing** core surface consumed here". **Codex's factual premise is partly false** (independently re-verified against 002's shipped contracts): (1) `core::detail::trap_throw_or_throw` is **net-new in 002 / PR #66** — `specs/002-dictionary-xml-loader/spec.md:190` ("added next to the existing `detail::trap_throw<F>`"), `:201` ("NEW exception-API helper added in this PR"); Codex's claim that it is "not part of the 002-shipped surface" / "pre-existing" is **incorrect**. (2) `core::error`/`expected_t<T>` are 001-origin but were **extended additively by 002** (`002 spec.md:190` — three new `dict_*` variants appended at slots 20–22; `:201` Depends-on lists `core::error` extended in 002), so they genuinely ship/were-re-touched with PR #66. The "all on `main` via PR #66" attribution is therefore **substantively accurate** for every listed symbol. The only true residual is **taxonomic-wording imprecision** (these are *core* surface, not the *dictionary* surface; the `dict_*` variants live in `core::error`, not a `dict::` namespace) with **no downstream effect on any AC, dependency, or `/tasks` input** — hence P3, not P2. **Applied: the minimal taxonomic-wording split only** (spec §8 now separates "002-shipped dictionary surface" from "core surface consumed here", labelling `core::error`/`expected_t` 001-origin-extended-by-002 and `trap_throw_or_throw` net-new-in-002). **NOT applied: Codex's "pre-existing core surface" framing**, because it mis-states `trap_throw_or_throw` (a PR-#66 addition) as pre-existing. Reviews: research/reviews/codex_003-dictionary-codegen_gate_a_2_review.md, research/reviews/opus_003-dictionary-codegen_gate_a_2_adversarial_review.md.

### Replan loop (post re-`/plan`) — round-by-round

- Round 1 applied 2026-05-15: Codex P1=0 P2=4 P3=2; Opus post-judging P1=1 P2=4 P3=4; rewrite addresses root causes #1 (owning_message_t concretisation), #2 (arch §2.4 v0.2→v0.3 sweep), #3 (RC-closure body reconciliation), #4 (AC-C4 CMake-graph test binding). Reviews: research/reviews/codex_003-dictionary-codegen_gate_a_replan_review.md, research/reviews/opus_003-dictionary-codegen_gate_a_replan_adversarial_review.md.
- Round 2 applied 2026-05-15: Codex P1=1 P2=2 P3=0; Opus post-judging P1=1 P2=3 P3=2; FINAL rewrite (2/2) addresses Root cause #1 (Option A — adopt 2c v1.4 §4.8 owning_message_traits<Msg> verbatim, replacing the invented Msg::owning_type member alias + correcting the false 2c-attribution bundle-wide), Root cause #3 residual (Entity 5 spelling), Root cause #4 (## 13 → Normative References + [const §VI.2] canonicalisation). RC#2 confirmed closed (no edits). Reviews: research/reviews/codex_003-dictionary-codegen_gate_a_replan_2_review.md, research/reviews/opus_003-dictionary-codegen_gate_a_replan_2_adversarial_review.md.
- **Round 3 (CONVERGED) 2026-05-15:** Codex P1=0 P2=0 P3=0; Opus post-judging (final convergence-deciding pass) P1=0 P2=0 P3=0 — **no Codex finding to escalate, nothing missed rising to P1/P2/P3**. All four replan-loop root causes independently re-derived as genuinely closed: the `owning_message_traits`/`owning_message_t` mechanism is verbatim-faithful to `.specify/2c-codegen.md` v1.4 §4.8 L1456–1464 (with the documented `fixpp::dict` namespace hoist), the invented `Msg::owning_type` alias fully removed (survives only as historical record), the decimal route uniformly PMR-mandatory `decimal_t::parse(span, mr)` with `sizeof == one pointer` preserved, `architecture.md` clean at v0.3, `spec §13` titled "Normative References" with `[FIX42]`/`[FIX44]` canonicalised. No circular include, no unbound AC, no new defect from the bundle-wide sweep. F1/R6 + the RC#1/#2/#3 resolutions + both `[const §XX]` amendments (2c v1.4, arch §2.4 v0.3) reviewed and accepted; Article XX §2 closing discharged (amendment review via this Gate A + user `/plan` sign-off + `_log.md` entry). **User-signed-off 2026-05-15, commit 3824bb5 ("Gate A converged round 3").** `/tasks` unblocked (recorded in `tasks.md` front-matter `gate_a`). Reviews: research/reviews/codex_003-dictionary-codegen_gate_a_replan_3_review.md, research/reviews/opus_003-dictionary-codegen_gate_a_replan_3_adversarial_review.md. Full decision record: `.specify/decisions/003-dictionary-codegen-gatea.md` (local; gitignored per the gate-records layout).

### Replan loop round 2 — disagreements

Codex round-2 P1 secondary prong ('reify.hpp and generated_message.hpp do not pin the alias identically') — Disagree per Opus round-2 review: the two files were internally consistent under the `Msg::owning_type` model; the real defect is the false 2c-attribution + mechanism divergence (Root cause #1), not internal inconsistency. Fixed via Option A (adopt 2c's `owning_message_traits` verbatim), not via re-pinning.

### Replan loop round 1 — disagreements

None. The authoritative Opus adversarial review (`research/reviews/opus_003-dictionary-codegen_gate_a_replan_adversarial_review.md`) ruled **no Codex finding `Disagree`** — it states "No Codex finding is judged Disagree. Codex's RC#1/#2/#3-resolved conclusion is independently confirmed." Every Codex finding was Confirmed or Escalated (Codex P3-1 escalated to P2 and clustered into Root cause #1; Codex P3-2 confirmed at P3 and clustered into Root cause #2). No Codex fix was rejected; nothing to record here.

### Round 1 — original pre-review notes (superseded by the applied record above)

Gate A round 1 runs on the Phase-4 bundle (`spec.md` + `plan.md` + `research.md` + `data-model.md` + `contracts/`) with both Codex passes per `feedback_gate_a_codex_dual_pass.md` (auto-memory):
- **Codex rescue agent** (`/codex:rescue`) — full Phase-4 bundle review.
- **`/codex:adversarial-review`** — adversarial pass challenging design choices.

Followed by Opus post-judging (P1/P2/P3 triage and verdict). Reviews land under `research/G19-fix-fpml-iso20022/research/reviews/`. Full /gate-a decision record at `.specify/decisions/003-dictionary-codegen-gatea.md`.

**Explicitly flagged for Gate A review** (the two `/plan`-deferred decisions, user-signed-off 2026-05-15):
- **F1 → Candidate A** (C++23 host tool reusing 002's `Dictionary` IR). Codex reviews the choice against `[2c §9]` seams and `[const §V.3]` (no new dependency admitted; the no-second-parser argument is the load-bearing rationale — research.md D-1).
- **R6 → vendor a frozen wire contract stub.** Codex reviews the stub surface for fidelity to `[2b §4.3]`/`[2b §4.7]` and the 2b-swap-in / drift-guard mechanism (research.md D-2). This is the highest-risk decision in the bundle; the contract-test + `static_assert` drift guard is the mitigation under review.

### Fresh round — scope & outcome (post re-`/plan`)

The fresh Gate A ran on the full re-planned bundle (`spec.md` + `plan.md` + `research.md` + `data-model.md` + `contracts/` incl. the new `version_profile.hpp`/`field_traits.hpp` extracts) **plus** the two `[const §XX]` amendments (`.specify/2c-codegen.md` v1.4 — committed; `.specify/architecture.md` §2.4 v0.3 + `tools/check_layers.py` — applied here). Both Codex passes. Per `[const §XVII.1]` the round counter reset for the re-planned artifact (the exhaustion→re-plan path is not a "round 3 on the same bundle"); hard reset remains rare. **Outcome: CONVERGED at replan-loop round 3** (Codex/Opus P1=0 P2=0 P3=0, user-signed-off 2026-05-15, commit 3824bb5) — full record at `## Gate A` → Round 3 (CONVERGED).

## Gate B

### Round 1 — TBD (post-`/implement`, pre-merge)

Gate B procedure: `.specify/codex-review.md` §6 (prompt) / §7 (recording). Independence rule per `[const §XVII.3]` — each round uses a separate Codex session from the implementer. Mandatory before merge per `[const §XVII.2]`. `/speckit-verify` precondition per `[const §XVII.8]` — record at `.specify/decisions/003-dictionary-codegen-verify.md` must be `GREEN` for `gate-b-done`, `YELLOW` with paired waivers for `gate-b-waived`. `/gate-b` applies the label via `gh api` REST with `--repo CatalinSerafimescu/fixpp` (auto-memory `project_gate_label_application`).

## Citation verification pass (round 1; extended at re-`/plan`)

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
| `[const §XX]` | `constitution.md:311` — Amendments (2c v1.4 RC#2 + arch v0.3 RC#3) | ✅ |

All citations resolve under canonical form. Cross-doc cites (`[2a §4.2]`, `[2a §4.3]`, `[2a §4.4]`, `[2a §6.5]`, `[2b §4.3]`, `[2b §4.4]`, `[2b §4.7]`, `[2b §6.4]`, `[2b §6.6]`, `[2c §X.Y]` incl. `[2c §4.1.3]`/`[2c §4.3]`, `[arch §2.3]`, `[arch §2.4]`, `[arch §4.2]`, `[arch §5.2]`, `[arch §5.5]`, `[FIX50SP2 §3.3]`, `[FIXT §5]`, `[FIXT §5.1]`, `[FIXT §5.3]`) inherited verbatim from `spec.md §13` References and the design docs.

## Phase-2 input checklist (for `/tasks`)

`/speckit-tasks` ran after the **fresh** Gate A CONVERGED at replan-loop round 3 (commit 3824bb5); it consumed this plan plus `data-model.md` + `research.md` + `contracts/` to produce `tasks.md` (54 tasks). The per-task shape was pre-bound as:

- **One task per row of the "Test seam → file mapping" table** (17 seam rows + the RC#4 "C4 (CMake-graph)" seam row + **11** cross-cutting AC entries = ~29 test-target tasks; the RC#1 files are `version_profile_test.cpp`, `field_traits_test.cpp`; the RC#4 entry is the `codegen_build_graph_test.cmake` → `fixpp::dict::codegen-build-graph-check` CTest target; `flyweight_shape_test.cpp` is now an explicit cross-cutting row for AC-G7/AC-G7a).
- **One task per `fixpp-codegen` source file / emitter** (~10: IR, the five emitters, dispatch emitter, normative-refs emitter, templating helper, CLI driver + its `CMakeLists.txt`).
- **One task per runtime bridge header** (`reify.hpp`, `version_registry.hpp`) + any out-of-line `src/dictionary/*.cpp`.
- **One task** for the **RC#1 `version_profile.hpp` additive edit** (append the structs + `resolve_application_version` + the ApplVerID wire→C++ map below the unchanged 002 enums) — `contracts/version_profile.hpp` is the shape oracle; AC-VP1..AC-VP6.
- **One task** for the **RC#1 NET-NEW `field_traits.hpp`** (`field_traits<T>` + specialisations + `decode_field<T>`, `decimal_t` excluded) — `contracts/field_traits.hpp` is the shape oracle; AC-FT1..AC-FT3.
- **One task** for the vendored frozen `include/fixpp/wire/message_view_contract.hpp` + its drift-guard contract test incl. the N-P3-1/N-P2-2 `get<1128>()` **and** `get<35>()` well-formed assertions (R6).
- **One task** for the RC#4 `tests/codegen/codegen_build_graph_test.cmake` + the `fixpp::dict::codegen-build-graph-check` CTest registration (AC-C4 / DoD §12 build-tree clause).
- **One task** for the `include/fixpp/core/error.hpp` additive enum edit — six slots LOCKED 23–28 (research.md D-10/D-21).
- **One task** for the `[2c §7.6]` CMake target graph + the configure-time `fixpp::dict::generate-vXX` custom targets.
- **One task** for the four golden headers (generated by `fixpp-codegen`, then checked in at `/implement` — not present at Gate A) + the conformance must-include manifest (Gate-A-reviewed).
- **One task** per bench harness (`typed_accessor_bench` incl. the `price(mr)` decimal arm, `compile_time_bench`, `reify_bench` + their `CMakeLists.txt`).
- **One task** for `docs/src/dictionary/codegen.md` (DoD §12).
- **Polish tasks** (sanitizer presets, coverage threshold, layer-edge lint incl. the `check_layers.py` bridge exemption, bench-baseline seeding, `/speckit-verify`, `/gate-a`, `/gate-b`) — each a `tasks.md` row so `/speckit-verify` has a one-to-one mapping per `[const §XVII.8]`.

Expected total: ~54 tasks (≈50 prior + RC#1 `version_profile` edit, `field_traits` header, and their two test files). TDD red-green-refactor ordering per `[const §VII.3]` — the vendored wire contract + `version_profile.hpp`/`field_traits.hpp` + golden headers + emitter unit tests lead; the emitters follow; conformance/integration/bench/polish close.
