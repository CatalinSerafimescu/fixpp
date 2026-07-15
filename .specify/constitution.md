<!--
Sync Impact Report — v0.6 → v0.7 (2026-07-14) — RATIFIED
  Bump: MINOR (narrows a post-1.0 carve-out by recording an in-scope v1.0 delivery; no banned-pattern addition, no perf-budget tightening → not v-major per Article XX §4).
  Modified principles:
    - Article I §1 (FIX Latest read/dictionary-tier bullet, added by v0.6) — narrows the deferred-to-post-1.0 list from "typed codegen, live wire validation, ApplExtID(1156)=303 differentiation, and session negotiation" to "typed codegen, ApplExtID(1156)=303 differentiation, and session negotiation" — dictionary-driven wire validation (required/type/enum-domain/group-structure checking via `wire::dictionary_driven_validator` + `table_view::enum_valid()`) is explicitly called out as NOT in the carve-out: feature 075 delivers it generically for all ten supported dictionaries (the nine QuickFIX-XML versions plus FIX Latest), with no FIX-Latest-specific code.
    - Article I §1 (post-1.0 milestone line) — "FIX Latest (typed-codegen / wire / session tiers)" → "FIX Latest (typed-codegen / session-negotiation tiers)"; the parenthetical is updated to record that dictionary-driven wire validation is delivered in v1.0 by 075.
  Added sections: none. Removed sections: none.
  §XVIII.5 disposition: NO residual conflict once §1 is narrowed. §5 bars early-shipping deferred post-1.0 **protocols**; Article XVIII §2's roadmap enumerates protocols (SOFH, SBE, FIXP, FAST, JSON, GPB, MMT). Dictionary-driven wire validation is not a protocol, so §5 is not engaged on its own terms — the amendment is what makes the scope cease to be deferred, not a waiver against §5.
  Rationale: feature 075 (live-wire-enum-validation) makes `table_view::enum_valid()` real, discharging the long-standing Phase-1 stub (L-041-1) for the nine legacy dictionaries — squarely in-scope v1.0 work, independent of FIX Latest. But the check is dictionary-generic: a FIX Latest dictionary loaded into a validating session gets enum-domain checking as an automatic consequence of the same store-driven projection (FR-002/FR-003), with no FIX-Latest-specific code. That touches the letter of the pre-amendment Article I §1 carve-out (which listed "live wire validation" as post-1.0) and Article XVIII §5. Narrowing the carve-out resolves the conflict by construction rather than shipping a silent violation. User-dispositioned at `/clarify` (spec.md Clarifications, "Which dictionaries get enum-domain validation" — all ten).
  Templates / dependents reviewed: plan-template.md / spec-template.md / tasks-template.md — no change. Affected catalogue rows: `spec/coverage-index.md` :581/:189/:68/:704, `spec/feature-catalogue.md` D-011 (:130) / W-014 (:111) — landed alongside this amendment (075 T035/T036).
  Process: Appendix-A mandatory-trigger feature (wire-format/parser: validator changes) → Codex Gate A required (converged round 5, user-signed-off 2026-07-14, submodule `8c8ec699`). Rides feature 075's branch per the established Gate-A-fold deviation from Article XX §2's standalone-PR letter (precedents: 035, 043, 068, 069). Ratified: 2026-07-14 pending user sign-off at Gate A / `/plan`.

  Prior: Sync Impact Report — v0.5 → v0.6 (2026-07-13) — RATIFIED
  Bump: MINOR (additive version-set widening at the read/dictionary tier; no banned-pattern/perf/config change → not v-major per Article XX §4). This is the FIRST amendment to add a NEW FIX version to the supported set (distinct from the 035/043/068/069 within-scope reclassifications, none of which widened the FIX version set).
  Modified principles:
    - Article I §1 — after the runtime-XML-scope bullet, adds a "FIX Latest (read/dictionary tier only)" bullet: `dict::OrchestraLoader` natively ingests the official FIX Orchestra machine-readable standard (`OrchestraFIXLatest.xml`, EP303) into a runtime `Dictionary` under `session_version::vlatest`. Explicitly scoped to the dictionary/runtime-read tier — it does NOT extend the "v1.0 ships 100% of the official spec" session+application obligation to FIX Latest (typed codegen, live wire validation, ApplExtID(1156)=303 differentiation, and session negotiation remain post-1.0).
    - Article I §1 (post-1.0 milestone line) — annotates "FIX Latest" to record that its read/dictionary tier is delivered in v1.0 by feature 074; typed/wire tiers remain post-1.0.
  Added sections: none. Removed sections: none.
  Rationale: REMAINING-WORK.md §A row 4b promoted FIX Latest (read/dictionary tier) to v1.0-gating (user, 2026-07-13). `session_version::vlatest` widens the supported version set, so Article XX requires amending the constitution rather than silently violating Article I §1. Scoped to the read/dictionary tier only so it does not trigger Article I §1's "100% of the official spec" (session+application) obligation for FIX Latest.
  Templates / dependents reviewed: plan/spec/tasks templates — no change. Affected catalogue rows: feature 074 close-out rows (feature-catalogue.md) + coverage-index.md D-011 promotion + A-035..A-065 linkage (feature-074 task T027).
  Process: Appendix-A mandatory-trigger feature (multi-version dictionary coexistence + version identity) → Codex Gate A required (satisfied on the 074 branch, converged round 2 `c2611b2f`). Rides feature 074's branch per the established Gate-A-fold deviation from Article XX §2's standalone-PR letter (precedents: 035/043/068/069). Ratified: 2026-07-13 pending user sign-off at Gate A / `/plan`.

  Prior: Sync Impact Report — v0.4 → v0.5 (2026-07-11) — RATIFIED
  Bump: MINOR (roadmap reclassification; additive — reclassifies v44 msgcat='app' typed codegen from §XVIII.7 v1.x-deferred to v1.0-delivered-by-069; no banned-pattern/perf/config change → not v-major per Article XX §4).
  Modified principles:
    - Article XVIII §7 (Application-message codegen scope for v1.0) — the FIX44-present v44 msgcat='app' subset of A-014..A-034 (BusinessMessageReject A-014, DontKnowTrade A-015, the List family A-019, the Quote/RFQ family A-021, the SecurityList family A-025 incl. SecurityType `v`/`w`, the Network-status family N-001, plus A-016/017/020/022/026 and the C-001/002, R-, P- families) is reclassified as DELIVERED by feature 069 under fixpp::v44 — **delivery is set-based on the full 83-message msgcat='app' scope (the illustrative family list is not exhaustive; every msgcat='app' MsgType except {BE,BF} is delivered)**; the mixed rows A-022/A-026/C-002 deliver only their FIX44 MsgTypes (AW / z,AA / AL,AM,AN,AO,AP). The FIX50-only rows A-018/023/027/028/029/030/031/032/033 + C-003 (whole rows absent from FIX44) and the mixed-row siblings BO (A-022) / BR (A-026) / BL (C-002) carry no FIX44 message and stay deferred to fixpp::v50sp2/all-version widening (NOT delivered by 069). XMLnonFIX (A-034, 35=n) stays deferred — it is msgcat='admin', outside the application-writer emitter, runtime-XML only. A-024 stays dropped-as-duplicate ([SYN §4.4]). fixpp::v42 / fixpp::v50sp2 app-message widening remains v1.x-deferred (069 is v44-only). Absorbs the pending D7 §XVIII.7 staleness (which listed the C/R families inconsistently).
  Added sections: none. Removed sections: none.
  §XVIII.5 disposition: NO amendment required — reclassifying this scope as delivered-now removes it from the "deferred post-1.0 scope being early-shipped" category, so §5's no-early-ship bar has no residual conflict once §7 is rewritten.
  Rationale: 067 shipped the v1.0 OFFICIAL v44 set; nothing coherent remains to widen without landing the §7-deferred app families, and the v1.0 tag is not yet cut. User elected proceed-now (2026-07-11).
  Templates / dependents reviewed: plan/spec/tasks templates — no change. Affected catalogue rows: coverage-index.md write-column flips for A-014/015/019/025 + A-016/017/020/022/026 + C-001/002 + R-001..005 + P-004..008 (v44-present MsgTypes only — the mixed rows A-022/A-026/C-002 flip only AW / z,AA / AL,AM,AN,AO,AP; the FIX50-only rows A-018/023/027/028/029/030/031/032/033 + C-003 and siblings BO/BR/BL are NOT flipped, no v44 row exists to flip); feature-catalogue.md 069 close-out rows.
  Process: Appendix-A codegen-trigger feature → Codex Gate A required (satisfied on the 069 branch). Rides feature 069's branch rather than a standalone `Constitution: amend §XVIII.7 — …` PR (Article XX §2 PR-title form) — a deviation from the letter of §2, recorded here (precedent: 035/043 Gate-A-folded amendments). Ratified: 2026-07-11 pending user sign-off at Gate A.

  Prior: Sync Impact Report — v0.3 → v0.4 (2026-07-11) — RATIFIED
  Bump: MINOR (adds a testing-authoring convention sub-clause; purely additive — no existing config breaks, no banned-pattern addition, no perf-budget tightening → not v-major per Article XX §4).
  Modified principles:
    - Article VII (Testing Requirements) — added §8: new isolation-safe test `.cpp` files default to an existing whole-binary grouped executable per module (`add_executable` + `add_test(NAME <bucket>)` + `LABELS`), not one executable per `.cpp`; `gtest_discover_tests` prohibited for these buckets (regresses serial ctest ~5–6×, measured in feature 068). Tests are selected by `ctest -L <label>`, never `-R <exe-name>`. Isolation-sensitive tests (global alloc/OOM injection, TSan-heterogeneous env, own `main()`/concurrency, exact-set completeness gates) remain standalone.
  Added sections: Article VII §8. Removed sections: none.
  Rationale: feature 068 (test-binary-grouping) cut a 66.9 GB one-exe-per-.cpp test matrix via whole-binary grouping; this codifies the pattern as the durable authoring convention so future tests don't regress the disk/ctest-launch win.
  Templates / dependents reviewed:
    - plan-template.md / spec-template.md / tasks-template.md — no change needed (generic stock templates, no CMake/ctest content to align).
    - specs/068-test-binary-grouping/quickstart.md — aligned in the same change (superseded `gtest_discover_tests` example → whole-binary `add_test`; `-R` → `-L` selection guidance).
  Process: additive documentation/convention amendment, not an Appendix A mandatory-trigger category (no ABI/threading/error-semantics/wire-format/codegen/session-FSM/security surface) — no Codex Gate A required on the amendment itself (068 itself is a gate-a-waived candidate, test-infra only; see `.specify/decisions/068-test-binary-grouping-gatea.md`). Rides feature 068's own branch rather than a standalone `Constitution: amend §VII.8 — …` PR (Article XX §2's PR-title form); this is a deviation from the letter of §2, recorded here rather than silently taken. **Ratified: 2026-07-11 (explicit user request, feature 068)** — the amendment was directly requested by the user as part of scoping feature 068's durable authoring convention, which stands in for the formal Gate-A-reviewed sign-off §2 otherwise prescribes (waived per the same gate-a-waived rationale as the feature itself: additive, outside every Appendix A trigger category). No fabricated reviewer signature is recorded; the sole basis for ratification is the user request itself.

  Prior: Sync Impact Report — v0.2 → v0.3 (2026-06-17): Article XII §5 — reopened closed SecurityProfile set + added `insecure_plain_tcp` non-TLS profile (Gate A folded into feature 043). User-signed-off 2026-06-17.
  Prior: Sync Impact Report — v0.1 → v0.2 (2026-06-13): Article XV §1 + XI §6 — FileStore §XV.4-offload bounded-frame exemption (Gate A folded into feature 035). User-signed-off 2026-06-13.
-->
# fixpp Constitution

> **Status:** user-ratified v0.7 (2026-07-14) — amends Article I §1 to narrow the FIX Latest post-1.0 carve-out (feature 074, v0.6) to *typed codegen + ApplExtID(1156)=303 differentiation + session negotiation*: dictionary-driven wire validation (required/type/enum-domain/group-structure checking) now ships generically for all ten supported dictionaries in v1.0 via feature 075; Gate A folded into feature 075, converged round 5, user-signed-off 2026-07-14. Prior: v0.6 (2026-07-13) — amends Article I §1 to add FIX Latest at the read/dictionary tier via `dict::OrchestraLoader` / `session_version::vlatest` (feature 074) — the first version-set widening; scoped to the dictionary/runtime-read tier only (typed/wire/session tiers stay post-1.0); Gate A folded into feature 074, user-signed-off 2026-07-13. Prior: v0.5 (2026-07-11) — amends Article XVIII §7 (reclassifies the FIX44 `msgcat='app'` typed-codegen subset of A-014..A-034 from v1.x-deferred to v1.0-delivered-by-069; full 83-message set-based delivery, FIX50-only rows + XMLnonFIX stay deferred; Gate A folded into feature 069, user-signed-off 2026-07-11). Prior: v0.4 (2026-07-11) — adds Article VII §8 (test-authoring convention: whole-binary grouped executables, `gtest_discover_tests` prohibited for buckets, `ctest -L` selection; feature 068). Ratified by explicit user request 2026-07-11 (not a standalone Article XX §2 PR — rode feature 068's branch; Codex Gate A waived, additive/no-trigger-category, same rationale as the feature's own `gate-a-waived` disposition). Prior: v0.3 (2026-06-17) — amends Article XII §5 (reopens the closed `SecurityProfile` set + adds `insecure_plain_tcp` non-TLS profile; opt-in-only, loud `[[deprecated]]`-class friction; Gate A folded into feature 043). v0.2 (2026-06-13) — amends Article XV §1 + XI §6 (FileStore §XV.4-offload bounded-frame exemption; Gate A folded into feature 035). Base v0.1 (2026-05-10) — Phase 2 Gate A converged (Codex review + Claude Sonnet review + Codex adversarial pass, all 18 issues resolved); see `decisions/constitution.md`.
> **Authority:** This document is project-wide non-negotiables. Every `/specify`, `/plan`, ADR, and PR must satisfy it. Conflicts are resolved by amending the constitution first (Article XX) — never by silently violating an article.
> **Citation form:** other documents cite articles as `[const §Roman.arabic]` (e.g., `[const §VIII.3]`).

---

## Article I — Identity & Mission

1. **`fixpp` is a modern C++23 implementation of the FIX protocol.** Session layer + application layer for FIX 4.0 through 5.0SP2 + FIXT.1.1. v1.0 ships 100% of the official spec for those versions, with the following codegen-vs-runtime split:
   - **Codegen scope (per `[2c §1.3]`):** FIX 4.2, FIX 4.4, FIX 5.0 SP2, FIXT.1.1. Typed-message classes, `constexpr` field metadata, per-message validators, `dict::reify` runtime-dispatch all generated under per-version namespaces (`fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2`, `fixpp::vt11`).
   - **Runtime-XML scope:** FIX 4.0, FIX 4.1, FIX 4.2, FIX 4.3, FIX 4.4, FIX 5.0, FIX 5.0 SP1, FIX 5.0 SP2, FIXT.1.1. `dict::XmlLoader` accepts QuickFIX-XML for any of these; runtime `Dictionary` works for field/required/group/length-pair lookups; users access fields through the runtime tag-keyed accessor.

   The runtime-XML-only versions (4.0 / 4.1 / 4.3 / 5.0 / 5.0 SP1) ship without a typed-message namespace in v1.0. Per-version codegen for those versions is deferred to post-v1.0 best-effort per Article XVIII §6.

   - **FIX Latest (read/dictionary tier only):** `dict::OrchestraLoader` natively ingests the official FIX Orchestra machine-readable standard (`OrchestraFIXLatest.xml`, EP303) into a runtime `Dictionary` under `session_version::vlatest` (wire application version `v50sp2` via `session_to_application`; no distinct ApplVerID). This is a **dictionary/runtime-read tier** capability delivered by feature 074 — it does NOT extend the "v1.0 ships 100% of the official spec" session+application obligation to FIX Latest (typed codegen, ApplExtID(1156)=303 differentiation, and session negotiation are post-1.0). **Dictionary-driven wire validation is NOT in this carve-out** — feature 075 delivers `enum_valid()`/type/required/group-structure checking generically for all ten supported dictionaries (the nine QuickFIX-XML versions plus FIX Latest via `session_version::vlatest`), with no FIX-Latest-specific code; this is a v1.0-scoped capability, not a post-1.0 deferral.

   FIX Latest (typed-codegen / session-negotiation tiers), FIXP, SBE, FAST, SOFH, JSON, GPB, and FIX MMT are post-1.0 milestones (Article XVIII). *(FIX Latest's read/dictionary tier is delivered in v1.0 per feature 074; dictionary-driven wire validation is delivered in v1.0 per feature 075 (generic across all ten dictionaries); only its typed-codegen, ApplExtID(1156)=303 differentiation, and session-negotiation tiers remain post-1.0.)*
2. **Primary distribution: in-process C++23 library** (static `.a`/`.lib` or shared `.so`/`.dll`). The C ABI is *adjacent* — it exists for non-C++ consumers, language bindings, and the out-of-process service mode, not as the primary surface.
3. **The Master Feature Catalogue** (`spec/feature-catalogue.md` in this repo) is the single coverage tracker. v1.0 cannot ship until every `OFFICIAL` row is `done` or explicitly `dropped` with user-signed rationale.
4. **No silent omissions.** Every normative section of every supported FIX spec must produce at least one catalogue row, traceable through `spec/coverage-index.md`.

---

## Article II — Language, Compilers, Platforms

1. **Language standard: C++23.** No fallback to earlier standards. Modules are not required (toolchain support is uneven); free use of concepts, coroutines, ranges, `std::expected`, `std::flat_map`, `std::pmr`, deducing `this`.
2. **Compiler matrix:**
   - **Clang** — primary development compiler, Linux. Sanitizers + fuzzing run here.
   - **GCC** — Linux CI sanity build only.
   - **MSVC** — Windows; `clang-cl` is **prohibited**.
3. **Platforms:**
   - **Linux is the primary development environment.** Day-to-day work, sanitizers, fuzzing, perf profiling, and e-book authoring all run on Linux.
   - **Windows** builds run **manual / on-demand / nightly via Tier 2 CI** (Article IX), not on every PR.
4. **No compiler-version pinning** for HALO (Heap Allocation eLision Optimization — the compiler eliding a coroutine's heap frame when its lifetime is bounded by the caller) and other optimizations (SYNTHESIS §3.2 Q6). The codebase tolerates HALO not firing on a given compiler revision; PMR fallback paths handle the gap.

---

## Article III — Build & Dependency Toolchain

1. **Build system: CMake ≥ 3.28 + Ninja.** No alternative generators in CI.
2. **Dependency manager: Conan.** Vcpkg is **prohibited**. Every external dep is declared in `conanfile.py` with a pinned version; transitive overrides go in profile files, not in code.
3. **Conan profiles** (under `conan/profiles/`):
   - `linux-clang-debug`, `linux-clang-release`
   - `linux-clang-asan`, `linux-clang-ubsan`, `linux-clang-tsan`
   - `linux-clang-coverage`
   - `linux-gcc-release`
   - `windows-msvc-debug`, `windows-msvc-release`
   - `windows-msvc-asan`
4. **CMake presets reference Conan profiles.** `tool_requires` pins `cmake` and `ninja` versions so the build is reproducible without relying on the host toolchain.
5. **`tools/` is build-only.** Code generators run during configure; no runtime dependency on tooling at user-link time.

---

## Article IV — Distribution Model

1. The C++ library is the primary public surface. It is consumed in-process by C++23 code via Conan.
2. The **C ABI** (`include/fix/c_api.h`) is the legal isolation boundary for AGPL/commercial dual licensing **and** the foundation for language bindings and the service wrapper. It is not the primary API.
3. **Python bindings** ship via SWIG over the C ABI, packaged as a CPython wheel. Linux x86_64 wheel is mandatory for v1.0; Windows wheel is best-effort via Tier 2.
4. **Service wrapper** (`service/`) is opt-in. gRPC is the control-plane transport; iceoryx2 SHM is the optional data-plane (Article XIV §3).
5. **v1.0 release artifacts are built but not published.** Conan packages and Python wheels are attached to GitHub releases; no upload to Conan Center or PyPI in v1. Publishing is gated on production-readiness and the README disclaimer being removed.

---

## Article V — License

1. **`fixpp` library:** AGPL-3.0 + commercial dual. The C ABI is the linkage isolation boundary for commercial users.
2. **E-book** (`book/` in the parent repo): CC-BY-SA 4.0.
3. **No LGPL dependencies.** Viral linkage is incompatible with the dual-license model.
4. **Vendored algorithm code** (e.g., `fixpp::sync::async_mutex` lifted from avast/asio-mutex BSL-1.0) carries upstream attribution at the file level; license compatibility is verified at vendoring time.
5. **Public repo from day one.** The README contains the disclaimer **"Work in progress — sandbox project — NOT for production use."** at the top of the file. The disclaimer must remain visible until publishing is unblocked (Article IV §5).

---

## Article VI — Spec Coverage Discipline (the 100% FIX Rule)

1. Every normative FIX spec section produces **at least one** `OFFICIAL` row in the catalogue (`feature-catalogue.md`).
2. Every OFFICIAL row's `Spec ref` uses canonical format `[DocAbbrev §X.Y.Z] Section title`. Vague refs (`§4`, "FIX spec") are a CI-linting failure.
3. Rows backed by design decisions instead of spec sections carry `[impl] description` or `[constitution] description` and are explicitly noted in `coverage-index.md` as design choices, not spec gaps.
4. **Bidirectional traceability:** `spec/coverage-index.md` maps every spec section → catalogue rows. Every new OFFICIAL row must have a coverage-index entry **before** it lands.
5. Every `/specify` artifact must include a **Normative References** section listing the exact `[DocAbbrev §X.Y.Z] Title` entries from the coverage index that inform the spec.
6. **No PR may close a `done` row without:** (a) a matching `/specify` artifact, (b) verifying tests, (c) Codex Gate B pass, (d) Gate A pass for non-trivial designs.

---

## Article VII — Testing Requirements

1. **Test framework: GoogleTest + GoogleMock** for C++ tests.
2. **Python tests: pytest** against the SWIG bindings.
3. **TDD is mandatory.** Every feature lands as red-green-refactor: failing test first, then implementation. Implementation without a preceding failing test is a Gate B blocker.
4. **No code without a test.** Untested code on `main` is a constitution violation; Codex Gate B prompts for it explicitly.
5. **Conformance corpus:** `tests/conformance/` holds the official FIX session-layer test cases (TC-001..TC-017, sourced from the **FIX Session Layer Test Cases** specification — `FIX-TC` in the coverage index) as executable scenarios. Every PR must pass them in CI.
6. **Interop:** v1.0 includes at least one interop test against an independent FIX implementation (QuickFIX) covering Logon → NewOrderSingle → ExecutionReport → Logout.
7. **Fuzzing (parser-touching modules):** libFuzzer corpus run ≥10 minutes on every PR; longer overnight runs on `main`. New parser-touching code without a fuzz harness is a Gate B blocker.
8. **Author isolation-safe tests grouped; select by ctest label, not executable name (added v0.4).** New isolation-safe test `.cpp` files (pure/stateless — read/parse/compute, single-threaded, no global allocation/OOM/singleton state) default to an existing whole-binary grouped executable in their module — `add_executable` + `add_test(NAME <bucket> COMMAND <bucket>)` + `set_tests_properties(... PROPERTIES LABELS "<label>")` — not one executable per `.cpp`. `gtest_discover_tests` is prohibited for these buckets (per-case discovery regresses serial ctest ~5–6×, measured in feature 068). Isolation-sensitive tests (global alloc/OOM injection, TSan-heterogeneous env, own `main()`/`abort()`/`_exit()`, genuine concurrency, per-`-D` variants, exact-set completeness gates, live `-R <target>` selection) stay standalone. Tests are selected by `ctest -L <label>`, never `-R <exe-name>`. Canonical pattern: `specs/068-test-binary-grouping/IMPLEMENTATION-PROCEDURE.md`, `tests/dictionary/CMakeLists.txt`.

---

## Article VIII — Performance Budgets & Benchmarks

1. **Bench framework: Google Benchmark.** Every perf-sensitive module has a benchmark in `bench/`.
2. **Regression budget: ±5%** vs `bench/baselines/` per profile. Intentional perf changes update the baseline **in the same PR** with rationale in the PR body.
3. **No perf change merged without a benchmark in the same PR.**
4. **v1.0 perf targets:**
   - Parser: parity-or-better with `hffix` on identical hardware (parse/sec).
   - Session throughput: parity-or-better with QuickFIX on identical hardware (messages/sec, end-to-end).
   - Latency: end-to-end session round-trip p50 and p99 measured and reported in `bench/REPORT.md`; no specific number is constitutional, but regressions vs the v1.0 baseline are blockers.
5. **Allocator policy on the hot path:** zero `new`/`delete` between parse and `fromApp` callback. Arena/PMR is the default; deviations require justification in the relevant `/plan`.
6. **Codex adversarial perf review** (v1.0 release-candidate gate) hunts for benchmark hacks, compiler optimization that elides work, and unrealistic data shapes. Findings are blockers.

---

## Article IX — Coverage, Sanitizers, Static Analysis

1. **Coverage thresholds (user-raised 2026-05-17, supersedes the prior 90/80):**
   - **Per-PR:** **≥95% line, ≥85% branch** on **touched modules** (`include/fixpp/<mod>/*`+`src/<mod>/*`, test files excluded), measured with fresh per-binary profraw (never reuse a prior/aborted build's profraw — a mismatched profraw makes `llvm-cov` silently zero a function; see the verify-procedure note).
   - **Binding rule — no silent uncovered error/edge path.** A PR may land below raw 95/85 **only if every uncovered line/branch carries a recorded Opus risk assessment** in `.specify/decisions/<feature>-verify.md`: *genuine error/edge path* → **must be tested** (a `catch`, a `wire_*`/error return, a DoS-cap, an overflow/truncation guard is genuine by default); *defensive / unreachable / trivial-accessor / dead-under-a-cap / dead-under-the-default-trait* → **waived with a one-line rationale**. Raw ≥95/≥85 with no uncovered error path also satisfies the gate. Either way: **no uncovered error/edge path without an explicit assessment** — that is the enforced gate; the percentage is the target.
   - **Global** (once `wire/` and `session/` modules have shipped): ≥95% line.
   - Coverage is measured on Linux/Clang only (`llvm-cov` + `llvm-profdata`). Windows/MSVC builds (Tier 2) do not run a coverage step — coverage thresholds are platform-independent, and the only viable Windows tool (`OpenCppCoverage`, last release 2019) cannot reliably measure modern MSVC output.
   - **Retroactive remediation backlog (not an instant violation, not blocking):** features merged before this raise — **001-core-decimal, 002-dictionary-xml-loader, 003-dictionary-codegen** — are tracked debt. Each gets a sequenced per-feature `/simplify`→coverage pass (own branch + `/speckit-verify` + review) **after 004-wire-codec closes**; until then they remain `done` and are not re-opened as violations. New features apply 95/85 from now.
2. **Sanitizers — Tier 1 (every PR, Linux/Clang):** ASan, UBSan, TSan must all run and pass.
3. **Sanitizers — Tier 2 (Windows/MSVC, manual/nightly):** ASan only. UBSan is not available under MSVC; equivalent UB coverage is provided by Linux/Clang Tier 1 (Article IX §2), since UBSan findings are language-level and platform-independent.
4. **Static analysis — Tier 1:**
   - `clang-tidy` clean against the project ruleset.
   - `clang-format` check.
   - `cppcheck` clean.
   - `include-what-you-use` clean.
5. **ABI check (from the first tagged C ABI release onward):** C ABI surface is dumped (`abidiff` Linux; structural diff Windows in CI) against the previous tagged ABI. Breaking changes are explicit `MAJOR` bumps; silent breaks are a release-blocker bug.
6. **Two-tier CI** (per `opus_plan.md` Quality Gate):
   - **Tier 1 — every PR (required to merge):** Linux/Clang Debug+Release, Linux/GCC Release sanity, sanitizers, coverage, perf, static analysis, fuzz (parser-touching modules), Python pytest, catalogue consistency check.
   - **Tier 2 — Windows + ABI:** manual / nightly / on-demand. Triggered by the `windows` PR label or nightly schedule.

---

## Article X — ABI Policy

1. **The C ABI in `include/fix/c_api.h` is a versioned contract.** Every change to it is reviewed against the contract; Codex Gate A is mandatory.
2. **No C++ symbol leakage** through the C ABI. CI verifies via `nm` (Linux) and `dumpbin` (Windows): the public C ABI surface contains only `extern "C"` symbols.
3. **Decimal at the C ABI boundary:** PoD `(int64 mantissa, int8 exponent)`. C++ users get full template flexibility via `decimal_traits<T>` (per SYNTHESIS §3.1 Q5); the C ABI picks one shape and freezes it.
4. **Error reporting at the C ABI:** `fixpp_error_t` is a bounded enum with reserved range and explicit forwards-compatibility rules (per SYNTHESIS §3.5 Q19). Out-of-range values are mapped to a documented "unknown error" code on read; unknown values from old consumers are tolerated by the engine. **Operational detail (per `[2i §4.4]` / `[2i §4.5]`):** the engine's translation layer downgrades to `FIXPP_ERR_UNKNOWN` (numeric 2) on the return path based on the consumer's published ABI minor version recorded at `fixpp_engine_create` time per `[2i §4.5]` version-binding protocol; a code introduced after the consumer's minor version is mapped to `FIXPP_ERR_UNKNOWN` before return. The FROM-consumer direction stays opaque pass-through — the engine does not actively reject unknown FROM-consumer values; `FIXPP_ERR_VERSION_MISMATCH` (numeric 5) is reserved for the explicit major-version-mismatch case at engine construction (per `[2i §4.5]`), not for unknown-value-tolerance. **Stability rule:** once a numeric value is published in a tagged C ABI release (`FIXPP_C_ABI_VERSION_MAJOR == 1`), it never changes meaning; new variants append at unused numeric slots within their domain block. Audit trail via `tools/abi_history/error_codes_v1.txt` (checked-in append-only file); CI verifies no re-definitions per the abidiff check `[const §IX.5]` and the occupancy gate `tools/check_capi_occupancy.sh`. Numeric-block layout per `[2i §4.3]`.
5. **Reentrancy contract** is documented per C ABI symbol (thread-safe / single-thread / requires-session-lock). No undocumented reentrancy.
6. **ABI-affecting features trigger all four mandatory controls (Appendix A):** `/clarify`, `/analyze`, Codex Gate A, user `/plan` sign-off.

---

## Article XI — Concurrency & Coroutines

1. **C++20/23 coroutines (`asio::awaitable<T>`) are the session/transport composition primitive.** Logon flow, recv, gap-fill, TLS handshake — all coroutines.
2. **Cancellation: ASIO native cancellation slots end-to-end.** No parallel `stop_token` abstraction. `fixpp_session_close()` from the C ABI signals the cancellation slot.
3. **Awaitable mutex required in coroutine context.** `fixpp::sync::async_mutex` (own implementation, BSL-1.0 algorithm attribution to avast/asio-mutex) is the only allowed mutex shape for coroutines. **Plain `std::mutex` is banned in any header that includes `asio::awaitable<...>`.** Enforced by clang-tidy custom check or grep gate.
4. **Application threading default: per-session strand.** Users who say nothing get callbacks serialised per session, never on the I/O thread. Custom executors are opt-in (per SYNTHESIS §3.2 Q6c).
5. **Hot-path lock policy: per-session policy with hard-coded callsite caps.** Default = mutex. Spin opt-in via session config. Store-write path always uses mutex regardless of policy (SYNTHESIS §3.2 Q8).
6. **Coroutine frame allocation: HALO-first.** PMR fallback per-awaiter where HALO doesn't fire. No global compiler-version pin (Article II §4). *(Limit — see §XV.1 scope: a cross-executor offload completion frame falls back to neither HALO nor PMR; §XV.1 permits a single bounded O(1) such frame per durable-I/O op on the §XV.4 FileStore offload path.)*
7. **Threading/concurrency-affecting features trigger all four mandatory controls (Appendix A):** `/clarify`, `/analyze`, Codex Gate A, user `/plan` sign-off.

---

## Article XII — Security & TLS

1. **TLS implementation: OpenSSL on both Linux and Windows.** Schannel is **dropped** (locked decision 2026-05-06).
2. **Allowed TLS versions: 1.2 and 1.3 only.** TLS 1.0, TLS 1.1, all SSL versions are **prohibited** at compile time.
3. **Allowed cipher suites are an explicit compile-time allow-list. The engine refuses to load anything outside it (FIXS RC1 alignment).**
   - **TLS 1.3:** `TLS_AES_128_GCM_SHA256`, `TLS_AES_256_GCM_SHA384`, `TLS_CHACHA20_POLY1305_SHA256` (RFC 8446 §9.1 mandatory + recommended set).
   - **TLS 1.2:** ECDHE-(RSA\|ECDSA) with AES-128-GCM, AES-256-GCM, or ChaCha20-Poly1305; SHA-256 or SHA-384 PRF only.
   - **Key exchange groups:** X25519, secp256r1, secp384r1.
   - **Signature algorithms:** ECDSA (P-256, P-384), RSA-PSS (key size ≥ 2048 bits).

   Anything not on these four lists — including TLS 1.3 0-RTT data, static RSA key exchange, CBC-mode suites, SHA-1 signatures, and 1024-bit RSA — is rejected at compile time.
4. **Banned cryptography:** RC4, DES, 3DES, MD5, DH_anon, NULL ciphers, anonymous key exchange, export-grade ciphers. Enforced at compile time.
5. **`Session` construction requires an explicit `SecurityProfile` choice — there is no implicit default.** The profile selects the trust mode:
   - `mtls_ca` — mutual TLS with CA-chain trust on the peer cert. The recommended starting profile for v1.0 deployments.
   - `mtls_pinned` — mutual TLS with leaf-cert pinning (FIXS RC1 strict profile). Required for FIXS-conformant deployments.
   - `one_way_ca` — server-cert TLS only, CA trust; permitted for legacy interop where the counterparty does not present a client cert. Construction emits a compile-time `[[deprecated]]` diagnostic.
   - `insecure_plain_tcp` — **NO TLS** (amended v0.3, 2026-06-17). A plain-TCP byte stream with **no transport encryption, no peer authentication, and no integrity protection**. Permitted for (a) plaintext FIX over a transport secured beneath the application — colocation cross-connect, VPN/IPsec tunnel — which is a common production deployment, and (b) engine-only benchmark fairness (the `TLS off` workload rows in `benchmark-plan.md`). It is **opt-in only and MUST NOT become an implicit default**: the `unset` sentinel is still rejected at `Session::open()` (the no-implicit-default rule in this clause + N-P2-3 is unchanged), and selecting `insecure_plain_tcp` MUST surface the **compile-time `[[deprecated]]` construction-site diagnostic this section prescribes for `one_way_ca`** — announcing that transport security is OFF (applied at the session-layer `SecurityProfile::kind` enumerator the operator selects). On this profile **no TLS context is constructed**, so the TLS-mechanism rules §1–§4 (OpenSSL impl, allowed versions, cipher allow-list, banned cryptography) are inapplicable and vacuously satisfied; **§7 still applies in full** — `insecure_plain_tcp` removes *transport* encryption only and never permits the banned application-layer `EncryptMethod(98)` encryption.

   Pinset rotation (multiple valid peer certs per counterparty, FIXS §5) is supported under both `mtls_pinned` and `mtls_ca`. The TLS-mechanism rules §1–§4 are conditioned on a TLS profile being selected (`mtls_ca` / `mtls_pinned` / `one_way_ca`); they constrain TLS *when present* and do not assert that every session uses TLS — `insecure_plain_tcp` is the explicit, friction-gated non-TLS escape.
6. **Certificate pinset rotation API** is a v1.0 feature (multiple valid peer certs per counterparty, FIXS §5).
7. **`EncryptMethod(98)` ≠ 0 is rejected.** Application-layer encryption is deprecated since FIX 4.3; encryption lives at TLS only.
8. **Pluggable `cert_source` interface** with one default impl (file-based PEM/DER) in v1.0; HSM/TPM/cloud-KMS impls are user-side or future bundles (Article XIV).
9. **Security-affecting features trigger all four mandatory controls (Appendix A):** `/clarify`, `/analyze`, Codex Gate A, user `/plan` sign-off.

---

## Article XIII — Observability & Logging

1. **OpenTelemetry instrumentation from v1.0.** Traces, metrics, logs all OTLP-exportable. Prometheus + OTLP dual export is the v1.0 minimum.
2. **Async logging is mandatory.** Synchronous logging on the hot path is a banned pattern (Article XV). The in-process logger is zero-alloc producer, bounded MPSC queue, dedicated drain thread, deferred formatting. Telemetry and log queues are permitted to use `drop-oldest` under bounded-queue overflow; this exception is scoped strictly to non-business signals (logs, metrics, traces) and never applies to FIX application or session messages (Article XV §15).
3. **OTel `trace_id` / `span_id` in every log record.** Each `Session` carries a `trace_context` field; logging on the session strand reads it directly. Code paths outside session scope (e.g., listener accept, control-plane handlers) use the `co_await fixpp::current_trace_context` awaitable backed by a strand-stored context (see `architecture.md`). `thread_local` propagation of trace context is **prohibited** — coroutines may resume on a different thread than they suspended on, and a `thread_local` write made before suspension is not guaranteed visible after resume. Correlation must work at the observability backend without manual stitching.
4. **Same sink interface backs OTel log export and file/stderr sinks.** No double-write paths.
5. **Bench spike mandatory** for the in-house logger vs `quill` before locking the implementation choice.

---

## Article XIV — Pluggable Interfaces

1. **The following are pluggable, each with one default impl in v1.0:**
   - **Transport** (default: ASIO TCP/TLS over OpenSSL).
   - **Control plane** (default: gRPC over Unix socket / named pipe).
   - **Cert source** (default: file-based PEM/DER).
   - **Logger sinks** (default: in-process async logger + OTLP exporter).
   - **MessageStore** (default: in-memory; file-based impl also v1.0).
2. **Interface surfaces are small.** Each pluggable interface defines **≤5 pure-virtual methods**. Bigger surfaces are permitted only with an explicit design-doc justification (one paragraph naming the necessary methods and why each is irreducible). The justification is reviewed at Gate A.
3. **Data-plane SHM via iceoryx2** is opt-in for sidecar mode. The control plane (gRPC) works without it.
4. **Plugin discovery is compile-time only in v1.0.** Dynamic plugin loading (`dlopen`) is post-1.0.

---

## Article XV — Banned Patterns

The following patterns are **prohibited** in `fixpp` source code. Each is rooted in a real failure mode observed in surveyed implementations.

1. **Heap-allocate per message or per field on the hot path.** Use zero-copy views; arena/PMR for the rare materialise cases.
   **Scope & §XV.4 exemption (amended v0.2, 2026-06-13).** The "hot path" of this ban is the latency-critical **in-memory** path — parse → validate → dispatch and `MemoryStore`, which MUST stay zero-allocation per message. The **durable-store async-journal offload mandated by §XV.4** (FileStore offloading `pwrite`/`fdatasync`/`rename` to a `file_io_executor`) is exempt to **a single bounded O(1) coroutine frame per offloaded I/O op**: a genuine cross-executor offload must `co_await` its completion inside an `asio::awaitable`, and that completion frame is routable to **neither HALO** (cannot fire across executors) **nor a PMR arena** (the Asio awaitable frame is opaque to the bound allocator) — so it is unavoidably one global-heap frame per op. The exemption is **strictly scoped**: O(1) frames/op only (the compliant fix is a *reduction* — 1 frame/op vs the prior inert offload's 4); it does **not** permit per-field, unbounded/growing, or in-memory-path allocation, nor any allocation on `MemoryStore::store`. This harmonises §XV.1 with §XV.4 and §XI.6 (whose "PMR fallback per-awaiter" presumes a fallback that does not exist for this cross-executor frame). Empirical basis: Gate-A probes `research/G19-fix-fpml-iso20022/research/probes/cospawn_probe*.cpp` (feature 035).
2. **Thread-per-session blocking I/O.** Use ASIO async I/O; multiplex N sessions onto M executor threads.
3. **Coarse global session lock.** Per-session state; lock-free queue between I/O and app thread.
4. **Synchronous disk I/O on every send** (e.g., QuickFIX `FileStore` flush per write). Async journal with background flush; sync-on-failover is opt-in.
5. **Synchronous logging on the hot path.** Async logger only (Article XIII §2).
6. **Runtime-only field validation.** Constexpr field metadata + typed accessors generated from the dictionary; misuse fails to compile, not at runtime.
7. **Forward-only field iteration with linear find** as the only access mode. Offset table is mandatory for typed/random-access path.
8. **`std::multimap` (or any other cache-hostile map) for field storage.** Vector + offset table; SBO for the common case.
9. **`std::mutex` in coroutine context.** Use `fixpp::sync::async_mutex` (Article XI §3).
10. **Application-layer encryption** (`EncryptMethod(98)` ≠ 0). TLS only (Article XII §7).
11. **TLS 1.0 / 1.1 / SSL / RC4 / DES / MD5 / DH_anon / anonymous KX / NULL ciphers / export-grade ciphers.** Compile-time allow-list refusal (Article XII §4).
12. **LGPL dependencies.** Viral linkage is incompatible with dual licensing.
13. **Eager codegen with no runtime dictionary path.** Hybrid mandated: codegen for standard fields (D-008), runtime XML loader for custom (D-007 + D-009).
14. **FAST / SBE / FIXP / SOFH shoehorned into v1.0.** Roadmap-locked to post-1.0 (Article XVIII).
15. **Application-layer message drops on slow consumer.** Backpressure-aware dispatch with two configurable modes: `block` (push back to the producer) or `disconnect-and-recover` (terminate the session and rely on FIX `ResendRequest` semantics on reconnect). `drop-oldest` is **never** permitted on the application or session message path — silent loss desynchronises the sequence-number contract. Telemetry and log queues may use `drop-oldest` under the rules in Article XIII §2.
16. **Custom XML config format** incompatible with the QuickFIX `[DEFAULT]` / `[SESSION]` CFG format. We accept QuickFIX CFG verbatim; TOML is also accepted; new formats require justification.
17. **Vendored OSS based on `master`/`main` when a release tag is older.** Read from the last release tag unless `master` represents a justified upstream improvement (SYNTHESIS §2.1).
18. **Research / decision content** (`research/`, `decisions/`, `book/`) committed into the `fixpp` repo. The `.github/workflows/no-research.yml` guard rejects it.

Each entry is a CI-enforced rule wherever feasible (Article IX §4 covers static analysis; Article XV.18 has its own guard workflow).

---

## Article XVI — Spec Kit Workflow Rules

1. **Full Spec Kit command set, each invoked in a clean context:** `/constitution`, `/specify`, `/clarify`, `/plan`, `/tasks`, `/analyze`, `/checklist`, `/taskstoissues`, `/implement`, `/simplify`.
2. **Clean-context rule:** every Spec Kit command runs as a fresh subagent invocation that loads only the artifacts it needs. No cross-phase context bleeding.
3. **`/clarify` is MANDATORY before `/plan` for any feature that touches:** ABI, threading, error semantics, wire format, codegen, session FSM, or security. (Same trigger set as Codex Gate A — Article XVII.)
4. **`/analyze` is MANDATORY** for the same trigger set as `/clarify`. Drift between constitution ↔ spec ↔ plan ↔ tasks is caught here, before `/implement`.
5. **`/checklist` output is part of CI evidence.** Checklists tied to NFRs and acceptance criteria become the e-book's "how to verify" appendix.
6. **`/implement` is one task at a time, TDD red-green-refactor.** Sonnet executes; Opus reviews increments.
7. **`/simplify` runs on the implementation diff before `/speckit-verify`** (pipeline step 9.5, before step 10 — see `.specify/pipeline.md`). Code-reuse, quality, efficiency findings reviewed by 3 specialized Opus agents, then Opus-triaged: genuine in-scope simplifications + any real Gate-B-relevant defect fixed; behavioral/perf redesigns + ambiguous items deferred as tracked follow-ups in the verify decision doc. **Rationale:** a post-`/simplify` source change invalidates every preset build dir, forcing the full 6-preset `/speckit-verify` matrix to re-run — so `/simplify` must precede verify, not merely precede PR open.
8. **Stuck loop:** three failed `/implement` invocations on the same red test (each invocation is a fresh-context attempt at one TDD cycle, per §1 and §6) → escalate to Codex as fallback implementer; if still stuck, `AskUserQuestion`. Codex's PR review for that task must come from a **fresh** Codex session, not the one that wrote the code (independence between author and reviewer is non-negotiable).

---

## Article XVII — Codex Review Gates

1. **Gate A — Design review (pre-implementation, non-trivial designs).** Triggers (any one):
   - Touches the public C++ API or C ABI.
   - Touches concurrency / threading / cancellation / executor model.
   - Touches the wire format, parser, or codegen layout.
   - Touches the session FSM, recovery, or message store contract.
   - Touches the security surface (TLS, cert handling, PSK).
   - Any new design document under `.specify/` (`architecture.md` and sibling design docs) — qualifies by default.

   Trivial features (rename a private helper, add a P2 boilerplate row over an existing module) skip Gate A. **When in doubt, run it.** Blockers from Gate A must be resolved or explicitly waived with rationale before `/tasks` runs.

2. **Gate B — PR review (post-implementation, every PR before merge).** Mandatory regardless of feature size. High-severity findings resolved or waived with rationale in the PR description before merge.

3. **Independence rule.** Codex's review is independent of the implementer. When Codex implements (escalation), Codex's PR review for that PR comes from a separate Codex session.

4. **User invokes Codex.** Neither Sonnet nor Opus auto-invokes Codex; the gates are user-driven (`codex:codex-rescue` agent or local Codex CLI). The PR description links to the Gate A outcome and the Gate B outcome.

5. **Findings triage:** Opus triages; Sonnet fixes accepted items; user signs off feature completion at `/specify` boundaries and at module close.

6. **CI enforcement.** The `.github/workflows/gate-a.yml` workflow inspects every PR's changed-file set against the Appendix A trigger paths (path globs are owned by the workflow itself, not the constitution). If any trigger path is touched, the workflow blocks merge unless the PR carries either a `gate-a-done` label (Codex Gate A passed) or a `gate-a-waived` label with mandatory rationale in the PR body. Trivial diffs auto-waive: comment-only edits, doc fixes, single-line whitespace, dependency-pin bumps without code changes.

7. **Local pre-PR build gate (mandatory, all PRs).** Before opening any PR, the contributor MUST run a local Conan install + CMake configure + build + ctest cycle on at least the `linux-clang-debug` preset, and `pytest bindings/python/tests/` if the change touches `bindings/python/`. The PR description must include a one-line confirmation (`local build: green on linux-clang-debug @ <git-sha>`). PRs without that line, or with a known-red local build, are rejected at review.
   - **Resource gate:** local builds are resource-heavy (Conan fetches + full compile + sanitizer rebuilds). When an AI agent needs to run the local build, it MUST surface an `AskUserQuestion` first; the user approves the build before it runs. The agent never auto-runs `conan install` / `cmake --build` without explicit approval.
   - **All dev work happens locally.** Contributors do not push speculative commits to remote branches "to see what CI says" as a substitute for local testing. CI is verification of green local work, not a remote test runner.
   - **Local toolchain target: Clang 22** (matches the user's local install and the Conan profile pin per Article II §2 / Article III §3). CI provisions Clang 22 via `apt.llvm.org` so local==CI.

8. **Verification gate (`/speckit-verify`) — required after every `/speckit-implement`.** `/speckit-implement` marks `tasks.md` rows `[X]` on agent confidence, not on evidence. "Run X command and verify Y threshold" tasks (sanitizer presets, coverage gate, static analysis, ABI hygiene, allocation discipline, fuzz smoke, bench regression, abidiff golden) routinely get marked complete without ever firing; the first real run then happens in CI on the open PR, which fails late and costs a Gate B round. The `/speckit-verify` command (in `.claude/commands/speckit-verify.md`) is the local Tier-1 mirror that actually executes each polish task serially against artifacts and writes a decision record at `.specify/decisions/<feature>-verify.md`.
   - **Mandatory after `/speckit-implement`.** A `/speckit-implement` run is not considered complete until `/speckit-verify <feature>` has produced a decision record. The record's verdict is `GREEN` (all PASS or SKIPPED-with-reason), `YELLOW` (every FAIL paired with a `--waive=<task-id>:<rationale>` rationale), or `RED` (at least one unwaived FAIL).
   - **Label evidence rule.** The CI-enforced `gate-a-done` / `gate-a-waived` labels (§6) and the analogous `gate-b-done` / `gate-b-waived` labels (consumed by the planned `gate-b.yml` workflow) may only be applied with paired evidence:
     - `gate-{a,b}-done` requires `/speckit-verify` `GREEN` **and** a corresponding Codex convergence record (`.specify/decisions/<feature>-gate{a,b}.md`).
     - `gate-{a,b}-waived` requires `/speckit-verify` `YELLOW` (or `GREEN` with explicit Codex-side waivers) **and** waiver rationales recorded both in the verify record and the PR body.
     - Applying gate labels by hand without these records is a constitutional violation — the labels are evidence claims, not status decorations.
   - **`/gate-b` precondition.** `/gate-b` pre-flight reads `.specify/decisions/<feature>-verify.md` and refuses to start the Codex review loop if absent or `RED`. `YELLOW` is accepted but carries waiver context forward into the Codex brief. This makes the verification gate enforceable without an `extensions.yml` hook: the only way to apply gate labels is through `/gate-b`, and `/gate-b` cannot run without verify evidence.
   - **Serial preset matrix.** `/speckit-verify` builds the Tier-1 preset matrix one configuration at a time — never in parallel — to keep failures isolable and avoid resource contention with the rest of the contributor's machine.

---

## Article XVIII — Roadmap Discipline

1. **v1.0 scope is locked:** FIX 4.0–5.0SP2 + FIXT.1.1 session + application layers, FIXS over TLS, the C ABI, Python bindings, the gRPC service wrapper, iceoryx2 SHM data plane (opt-in).
2. **Post-1.0 roadmap (locked):**
   - **v1.1 — SOFH** (Simple Open Framing Header).
   - **v1.2 — FIX Latest application messages** (new MsgTypes A-035..A-065 + EP-level field additions to existing messages, per coverage-index Post-1.0 Gap Registry).
   - **v1.3 — SBE** (Simple Binary Encoding).
   - **v1.4 — FIXP** (FIX Performance Session Layer).
   - **v1.5 — FAST** (FIX Adapted for STreaming).
   - **v1.6 — JSON** (FIX JSON encoding).
   - **v1.7 — GPB** (Google Protocol Buffers FIX encoding).
   - **v1.8 — FIX MMT** (Market Model Typology).
3. **Permanently dropped:** FIXML (XML representation; superseded), FIXatdl (UI/display spec, not a wire protocol).
4. **Roadmap changes are constitution amendments.** Re-ordering, additions, removals all require Article XX.
5. **No early shipping** of post-1.0 protocols into v1.0 to "get them done." Each shipping target is its own Spec Kit cycle, gated by the same Tier 1 quality bar.
6. **Post-v1.0 codegen for runtime-XML-only versions.** FIX 4.0, FIX 4.1, FIX 4.3, FIX 5.0, and FIX 5.0 SP1 ship in v1.0 with runtime-XML support only (no per-version codegen namespace). Per-version codegen for these versions is post-v1.0 best-effort, prioritised at the discretion of the maintainer team based on observed downstream demand. The recommended priority order is: FIX 4.3 first (most-used legacy version in the post-v1 backlog), 5.0 SP1 second, 5.0 third, 4.0 / 4.1 last (vanishingly few production deployments). Each version's codegen is its own minor-version Spec Kit cycle, gated by the same Tier 1 quality bar.
7. **Application-message codegen scope for v1.0.** v1.0's typed-message scope under `fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2` is A-001..A-013 plus the M-/P-/C-/R-/N- families per the catalogue. **Under `fixpp::v44`, the full `msgcat='app'` set (83 in-scope messages = 85 app minus the N-002/N-003 pair BE/BF) is DELIVERED by feature 069** — the previously-deferred FIX44 application rows A-014, A-015, A-016/017/020, A-019, A-021 (`AH`/`AI`/`AJ`), A-025 (`v`/`w`/`x`/`y`), the FIX44 members of A-022 (`AW`) and A-026 (`z`, `AA`), the Collateral (C-001), Position (C-002: `AL`/`AM`/`AN`/`AO`/`AP`), Registration/indication (R-), Network-status (N-001: `BC`/`BD`), and post-trade (P-) families now carry typed `build_/validate_/Args` builders. **The delivered set is the full `msgcat='app'` scope (83) — the family enumeration here is illustrative, not the operative bound.** **The FIX50-only rows whose only MsgTypes are absent from FIX44 — A-018 (BN), A-023 (BZ/CA), A-027 (BI/BJ/BS), A-028 (BT/BU/BV), A-029 (BK/BP), A-030 (BQ), A-031 (BM), A-032 (CB), A-033 (CC/CD/CE), C-003 (CQ) — plus the mixed-row FIX50-only siblings BO (A-022), BR (A-026), BL (C-002) are NOT delivered by 069**; they carry no FIX44 message and remain deferred to future `fixpp::v50sp2`/all-version widening. **XMLnonFIX (A-034, 35=n) is NOT delivered** — it is `msgcat='admin'`, outside the application-writer emitter; runtime-XML access via `view.get(uint16_t tag)` remains its only path. A-024 stays dropped as a duplicate per `[SYN §4.4]`. The N-002/N-003 session-FSM pair (BE UserRequest / BF UserResponse) remains deferred to the separate v1.0-tagging gate. **`fixpp::v42` and `fixpp::v50sp2` application-message widening (A-014..A-033) remains v1.x-deferred** (069 is v44-only). Runtime-XML access to any not-yet-typed application message via `view.get(uint16_t tag)` continues to ship across all 9 supported FIX versions.

---

## Article XIX — Documentation

1. **Library docs:** Doxygen → mdBook bridge under `docs/`. Hand-written getting-started, session cookbook, custom dictionary how-to, C ABI usage, Python tutorial, perf tuning, troubleshooting.
2. **E-book:** mdBook + mdbook-pdf under `book/` in the parent repo. CC-BY-SA 4.0 (Article V §2).
3. **Every code example in either artifact is a runnable file in `examples/` exercised by CI.** Stale examples are a documentation bug.
4. **Cross-link to source** at pinned tags, not at `main`, so references survive history rewrites.
5. **Pages tied to public API surfaces** must be regenerated when the surface changes. Doxygen output drift is a Gate B finding.

---

## Article XX — Amendments

1. **The constitution is amendable but not silently violatable.** Any conflict between this document and a feature spec must be resolved by amending the article first (with rationale committed in the same PR), then proceeding.
2. **Amendment process:**
   - Open a PR titled `Constitution: amend §<article>.<number> — <summary>`.
   - PR description states the change, the rationale, and which catalogue rows / specs are affected.
   - Codex Gate A review on every amendment.
   - User signs off.
3. **`_log.md` records every locked decision** with date and source (which article amendment, which `/specify`, which architecture revision).
4. **Backwards-incompatible amendments** (banned-pattern additions, perf-budget tightening) require a v-major bump and an entry in `CHANGELOG.md`.
5. **Cross-cutting hand-off rules** (e.g., "Sonnet asks before every `git push`", "sudo confirmation required") are amendments to this constitution; they live here, not in ad-hoc memory or `CLAUDE.md` carve-outs. *No such rules defined as of v0.1; future rules of this shape are added below as numbered sub-clauses (5.a, 5.b, …) under this article via the standard amendment process (§2).*

---

## Appendix A — Mandatory triggers reference

**This appendix is the canonical mandatory-trigger reference.** Article-level trigger clauses must match it; conflicts are resolved in favour of this table (Article XX). The following features trigger **all four** mandatory controls: `/clarify`, `/analyze`, Codex Gate A, user `/plan` sign-off.

| Trigger | Examples |
|---|---|
| ABI surface change | new C ABI symbol, signature change, error-code addition |
| Threading / concurrency | new awaitable, new strand discipline, lock policy change |
| Error semantics | new `fixpp_error_t` value, exception-vs-`expected` change |
| Wire format / parser | offset-table semantics, framing rules, validator changes |
| Codegen layout | dictionary loader, multi-version coexistence |
| Session FSM | state additions, recovery semantics, gap-fill rules |
| Security | TLS config, cipher allow-list, cert-source plug-in |

Trivial features (P2 boilerplate over an existing P0 module, doc-only) skip all four. **When in doubt, run them.**

---

## Appendix B — Cross-references

- **`opus_plan.md`** (parent repo) — phase plan, owns the Codex gate workflow descriptions, owns the Quality Gate Tier 1/Tier 2 split.
- **`SYNTHESIS.md`** (parent repo) — Phase 1.5 output; the decisions encoded here are sourced from §1, §2, §3, §5.
- **`spec/coverage-index.md`** (this repo) — bidirectional spec ↔ catalogue traceability index. Article VI §4 binds `/specify` to it.
- **`spec/feature-catalogue.md`** (this repo) — the 100% FIX tracker. Article VI §1 binds it to the spec.
- **`architecture.md`** (this directory, drafted next) — module layering, public namespaces, design patterns. Implements the rules; this constitution sets them.
