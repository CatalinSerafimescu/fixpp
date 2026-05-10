# Implementation Plan: 001-core-decimal — Decimal type

**Branch**: `001-core-decimal` | **Date**: 2026-05-10 | **Spec**: [`spec.md`](./spec.md)
**Input**: Feature specification from `/specs/001-core-decimal/spec.md` (anchored to `.specify/2a-decimal.md` v0.3, Gate A round-2 converged 2026-05-07)

## Summary

Ship the FIX FLOAT primitive — `fixpp::core::pod_decimal`, `fixpp::core::decimal<T>`, `fixpp::core::decimal_traits<T>`, the `fixpp_decimal_t` C-ABI struct, and the boundary functions (`fixpp_decimal_parse / _format / _compare / _equal / _init`) — as the first feature of the `core/` module. The full *what* is in `spec.md` and the full *how* is locked in `.specify/2a-decimal.md` v0.3; this plan adds only what `/plan` is responsible for: Technical Context, Constitution gate evidence, project layout decision, and Phase 0/Phase 1 artifacts (`research.md`, `data-model.md`, `contracts/`, `quickstart.md`).

The work unblocks **2b** (wire FLOAT-field parser/serializer), **2c** (codegen FLOAT-typed accessors), and **2i** (C-ABI accessor `fixpp_msg_field_decimal`). Per `[const §XVII §1]` and Appendix A, this feature triggers all four mandatory controls: `/clarify` (done), `/analyze` (after `/tasks`), Codex Gate A (on this `/specify` + `/plan`), user `/plan` sign-off.

## Technical Context

**Language/Version**: C++23 (`[const §II §1]` — no fallback; free use of concepts, `std::expected`, `std::pmr`, `noexcept`)
**Primary Dependencies**: GoogleTest 1.17.0 (`[const §VII §1]`), Google Benchmark 1.9.5 (`[const §VIII §1]`); both already pinned in `conanfile.py` per Phase 3 close. No new third-party dependency in this feature.
**Storage**: N/A — pure value primitive.
**Testing**: GoogleTest (C++ unit + property), pytest (Python `Decimal` cross-language oracle, seam #8), libFuzzer (parse fuzz harness, seam #7), `mallocnesia` interceptor (Linux Tier 1 alloc guard, seam #6), `abidiff` (Tier 2 ABI golden, seam #4).
**Target Platform**: Linux x86_64 — Tier 1 every PR; Windows MSVC — Tier 2 manual/nightly (`[const §IX §6]`).
**Project Type**: Library — in-process C++23 (`[const §IV §1]`) plus C-ABI adjacent surface (`[const §IV §2]`).
**Performance Goals** (default traits, x86_64, 5-digit mantissa, warm cache; `spec §6` NFRs):
- `parse` ≤ 50 ns median
- `to_chars` ≤ 30 ns median
- `compare` ≤ 20 ns median
- `±5%` regression bar vs `bench/baselines/` (`[const §VIII §2]`)

**Constraints**:
- Zero allocation between parse and `fromApp` (`[const §VIII §5]` + `[const §XV §1]`); enforced by `mallocnesia` symbol-scoped to `fixpp::*` / `fixpp_*`.
- Zero exceptions on hot path (`noexcept` declarations + ASan/UBSan in Tier 1).
- Coverage ≥ 90 % line / 80 % branch on touched files (`[const §IX §1]`).
- `clang-tidy`, `clang-format`, `cppcheck`, IWYU all clean (`[const §IX §4]`).

**Scale/Scope**: Public surface is 1 PoD struct + 1 traits-driven template + 6 free C functions. Implementation TU ~500 LOC; tests ≥ 1500 LOC across the 10 test seams (`spec §9`). One Spec-Kit feature directory; one library PR.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design (post-`research.md` + `data-model.md` + `contracts/` write).*

### Mandatory triggers (Appendix A)

This feature triggers **all four** mandatory controls (ABI surface change + new error semantics):

| Control | Status | Evidence |
|---|---|---|
| `/clarify` | ✅ done 2026-05-10 | 5 Q&A recorded under `spec.md §Clarifications — Session 2026-05-10` |
| `/analyze` | ⏳ pending | runs after `/tasks` (drift check across constitution↔spec↔plan↔tasks) |
| Codex Gate A | ⏳ pending | runs on combined `/specify` + this `/plan` per `[const §XVII §1]` |
| User `/plan` sign-off | ⏳ pending | after Gate A; recorded in `phases/phase-4.md` Track Log |

### Article-by-article gate

| Article | Verdict | Evidence |
|---|---|---|
| **II — Language / compilers / platforms** | ✅ | C++23 only; Linux/Clang Tier 1, Windows/MSVC Tier 2; no compiler-version pin |
| **III — Build & dependency toolchain** | ✅ | CMake ≥ 3.28 + Ninja + Conan profiles already in tree (Phase 3); no new third-party dep |
| **IV — Distribution model** | ✅ | Adds C-ABI symbols `fixpp_decimal_*` (decimal slice; rest of C-ABI surface owned by **2i**) |
| **V — License** | ✅ | All new code AGPL-3.0; no LGPL dep; no new vendoring |
| **VI — Spec coverage** | ✅ | Inherits W-009 (FLOAT family) per `spec §8` and `2a §11` — **no new catalogue row** (explicit) |
| **VII — Testing** | ✅ | All 10 test seams (`spec §9`) ship in this PR (clarification Q6 resolved 2026-05-10); fuzz harness present (parser-touching → mandatory per §VII §7); pytest oracle Tier 1-promoted |
| **VIII — Performance budgets** | ✅ | Bench harness in this PR (`bench/core/decimal_bench.cpp`); ±5 % regression bar; baseline locked at first `/implement` close |
| **IX — Coverage / sanitizers / static analysis** | ✅ | Tier 1: Linux/Clang Debug+Release, Linux/GCC Release sanity, ASan+UBSan+TSan, coverage, clang-tidy, IWYU, fuzz; Tier 2: abidiff golden |
| **X — ABI policy** | ✅ | This is the feature **establishing** decimal at the C-ABI boundary per §X §3; `extern "C"` only; `static_assert` on layout (`AC-A1..A6`); error-code numeric block per §X §4 (provisional values, dated comment, ratified by **2i**) |
| **XI — Concurrency** | ✅ | N/A — pure-value primitive; `noexcept` everywhere; no awaitable, no mutex |
| **XII — Security** | ✅ | N/A — no TLS / cert / crypto surface |
| **XIII — Observability** | ✅ | N/A — no logger / OTel surface |
| **XIV — Pluggable interfaces** | ✅ | `decimal_traits<T>` is a **compile-time** customization point (no pure-virtual surface; `[const §XIV §2]` ≤ 5 method rule does not apply — no runtime polymorphism per `[const §II §1]` and `2a §2`) |
| **XV — Banned patterns** | ✅ | No heap on hot path (#1); no thread-per-session (#2 N/A); no global lock (#3 N/A); no sync I/O (#4 N/A); no sync logging (#5 N/A); typed accessors via constexpr metadata (#6 — codegen layer 2c); no `std::multimap` (#8); no `std::mutex` in coroutine ctx (#9 N/A); not LGPL (#12); not eager-codegen-only (#13 — runtime path supported via traits) |
| **XVI — Spec Kit workflow** | ✅ | Following `/specify` → `/clarify` → `/plan` → `/tasks` → `/analyze` → `/checklist` → `/taskstoissues` → `/implement` → `/simplify` → CI → Gate B in clean contexts |
| **XVII — Codex review gates** | ✅ | Gate A required (§1 — public C++ API + C-ABI); Gate B required for impl PR (§2); local pre-PR build gate (§7) applies — `local build: green on linux-clang-debug @ <git-sha>` line in PR body |
| **XVIII — Roadmap discipline** | ✅ | v1.0 scope; no early shipping of post-1.0 protocols |
| **XIX — Documentation** | ✅ | Doxygen on the public surface; quickstart shipped here |
| **XX — Amendments** | ✅ | None proposed — feature fits inside the existing constitution unmodified |

### Verdict

**Gates pass.** No constitutional violations to track. The Complexity Tracking section is empty.

## Project Structure

### Documentation (this feature)

```text
specs/001-core-decimal/
├── plan.md              # this file
├── spec.md              # /specify output (already on disk, /clarify-applied)
├── research.md          # Phase 0 — this run
├── data-model.md        # Phase 1 — this run
├── contracts/           # Phase 1 — this run
│   ├── c_api_decimal.h         # C-ABI extract
│   └── decimal_traits.hpp      # C++ traits API extract
├── quickstart.md        # Phase 1 — this run
└── tasks.md             # Phase 2 — produced by /speckit-tasks (NOT this run)
```

### Source code (library repository root)

The Phase 3 module skeleton is already in place. This feature populates the decimal-shaped slice of each existing module dir; no new top-level dirs are created.

```text
include/
├── fixpp/core/
│   ├── decimal.hpp                   # NEW — pod_decimal, decimal<T>, decimal_traits<T>
│   ├── decimal_alias.hpp             # NEW — FIXPP_DECIMAL_T machinery, alias sentinel
│   └── decimal_helpers.hpp           # NEW — detail::trap_throw helper for trait authors
└── fix/
    └── c_api.h                       # MODIFY — add fixpp_decimal_t + 6 fns + error codes (co-owned with 2i)

src/
├── core/
│   └── decimal.cpp                   # NEW — pod_decimal traits impl + alias sentinel def
└── capi/
    ├── decimal.cpp                   # NEW — fixpp_decimal_* boundary fn impls (co-owned with 2i)
    └── decimal_assert.cpp            # NEW — layout static_asserts (sizeof, alignof, offsetof)

tests/
├── core/
│   ├── decimal_parse_test.cpp        # NEW — AC-P1..P10
│   ├── decimal_format_test.cpp       # NEW — AC-S1..S6
│   ├── decimal_compare_test.cpp      # NEW — AC-C1..C6 + Python oracle property test
│   ├── decimal_cross_traits_test.cpp # NEW — AC-X1..X3
│   └── decimal_alias_test.cpp        # NEW — AC-B1..B4 link-time mismatch
├── capi/
│   ├── decimal_layout_test.cpp       # NEW — AC-A1..A6 layout regression
│   └── decimal_reserved_test.cpp     # NEW — AC-A4 _reserved tolerance (seam #10)
├── abi/
│   └── golden/
│       └── fixpp_decimal_t.abidiff   # NEW — Tier 2 ABI golden (seam #4)
├── fuzz/
│   └── fuzz_decimal_parse.cpp        # NEW — libFuzzer harness (seam #7)
└── support/
    └── mock_decimal_traits.hpp       # NEW — parameterizable failing-traits helper (seam #1)

bench/
└── core/
    └── decimal_bench.cpp             # NEW — Google Benchmark for §6 NFRs (seam #5)

bindings/python/tests/
└── test_decimal_oracle.py            # NEW — Python `Decimal` property oracle (seam #8, Tier 1-promoted)
```

**Structure Decision**: **Existing single-library layout** (Phase 3 skeleton). No new option needed. The core / capi / tests / bench module dirs are already carved; this feature populates the decimal-shaped slice. The Python oracle test lives under `bindings/python/tests/` since pytest harness already runs there (Tier 1).

## Complexity Tracking

> **No constitutional violations to justify.** Section intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| _(none)_ | — | — |

## Phase 0 — Outline & Research

→ **Output**: [`research.md`](./research.md)

The research is largely **inherited** from `.specify/2a-decimal.md` v0.3 (Gate A round-2 converged), which carries the converged decisions for: representation choice (PoD `int64 × 10^[-38..0]`), compare algorithm (digit-string, no `__int128`), cross-traits funnel (through `pod_decimal`), error model (4 C++ codes + 3 C-ABI codes), trait amplification mitigation (one alias per build, link-time sentinel), and the 10 test seams. `research.md` is therefore short — it records each decision once, points back at the design-doc section, and records the disposition of the open questions resolved during `/clarify` (Q5, Q6) plus the still-deferred questions (Q1, Q2, Q3, Q4).

## Phase 1 — Design & Contracts

→ **Outputs**: [`data-model.md`](./data-model.md), [`contracts/`](./contracts/), [`quickstart.md`](./quickstart.md)

- **`data-model.md`** — the 5 entities (`pod_decimal`, `decimal<T>`, `decimal_traits<T>`, `fixpp_decimal_t`, `decimal_error` / C-ABI subset of `fixpp_error_t`); validation rules from `spec §4`; relationships; canonical-form invariants.
- **`contracts/`** — two extracts:
  - `c_api_decimal.h` — the C-ABI declarations the feature adds to `include/fix/c_api.h` (struct, init helpers, 6 boundary fns, 3 error-code provisional values).
  - `decimal_traits.hpp` — the C++ traits API and `decimal<T>` member surface as the final reviewer-facing contract (mirrors `2a §4.2 / §4.3`).
- **`quickstart.md`** — getting-started for the feature (build, run unit tests, run the bench, verify the ABI golden, switch the alias for a wider-trait build).

### Agent context update

The library-side `CLAUDE.md` is updated between `<!-- SPECKIT START -->` and `<!-- SPECKIT END -->` to point at this `plan.md`.

## Re-evaluation post-design

After writing `research.md` + `data-model.md` + `contracts/` + `quickstart.md`, the Constitution Check above is re-evaluated for drift. **No drift** — Phase 1 outputs do not introduce any new design choice not already in `2a-decimal.md` v0.3 + `spec.md`; gates remain ✅. Codex Gate A consumes this combined `/specify` + `/plan`.

## References

- Spec: [`spec.md`](./spec.md)
- Design doc (full *how*): `.specify/2a-decimal.md` v0.3
- Architecture: `.specify/architecture.md` §4.1, §4.10, §5.3, §5.5, §6, §10
- Constitution: `.specify/constitution.md` §II §III §IV §V §VI §VII §VIII §IX §X §XV §XVI §XVII §XVIII §XIX §XX + Appendix A
- Catalogue: `spec/feature-catalogue.md` W-009; FLOAT-typed accessors A/M/P/C/R/N families per `2a` Appendix A
- Phase doc: `../../../phases/phase-4.md` (parent repo)
- Codex review procedure: `.specify/codex-review.md`
