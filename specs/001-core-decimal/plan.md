---
id: 001-core-decimal
title: Implementation Plan — Decimal type (`fixpp::core::decimal<T>` + `fixpp_decimal_t` C-ABI)
module: core/
phase: 4
status: drafted
verdict: TBD
spec_kit_step: /plan
gate_a_round: 2 (round 1 verdict 2026-05-10: full bundle redraft — see ../../../research/G19-fix-fpml-iso20022/research/reviews/opus_001-core-decimal_gate_a_v1_adversarial_review.md)
last_updated: 2026-05-12
inherits_design: .specify/2a-decimal.md (v0.3, signed off 2026-05-07)
inherits_spec: specs/001-core-decimal/spec.md (preserved from round 1; carries /clarify Q&A 2026-05-10)
---

# Implementation Plan — 001-core-decimal

**Branch:** `001-core-decimal` | **Date:** 2026-05-12 | **Spec:** [`spec.md`](spec.md)
**Input:** Feature specification at `specs/001-core-decimal/spec.md`.

> **Round-2 redraft note.** This plan is the round-2 redraft after Gate A round 1 closed with verdict **"full bundle redraft needed"** (see `Gate A` section at bottom). The /plan toolchain artifacts (`plan.md`, `research.md`, `data-model.md`, `quickstart.md`, `contracts/`) were regenerated from a literal re-read of `.specify/2a-decimal.md` v0.3. `spec.md` (including the 2026-05-10 `/clarify` Q&A) is preserved verbatim from round 1.

## Summary

Ship the FIX FLOAT representation primitive — `fixpp::core::pod_decimal`, `fixpp::core::decimal<T>`, `fixpp::core::decimal_traits<T>`, the `fixpp_decimal_t` C-ABI struct, and the five C-ABI boundary functions — as the first feature of the `core/` module. All contracts are literal extracts from `.specify/2a-decimal.md` v0.3 §4.1–§4.4 and §5.1–§5.2; no shape-altering redesign is introduced at `/plan` time. Unblocks the wire layer (**2b**), codegen (**2c**), and the C-ABI surface (**2i**).

Technical approach is locked by 2a v0.3:
- `pod_decimal = { int64_t mantissa, int8_t exponent ∈ [-38, 0] }`. `INT64_MIN` mantissa is the invalid sentinel.
- `decimal<T>` is a value-typed wrapper over `T`; `decimal_traits<T>` is the compile-time customization point.
- C-ABI `fixpp_decimal_t` is a frozen 16-byte standard-layout struct with `_reserved[7]` at offset 9, **ignored on read** in v1.0.
- Five C-ABI boundary functions, all `noexcept` at the C++ implementation side, with the by-value/`int`-return shapes locked in 2a §5.2.
- Engine-wide alias selection via `FIXPP_DECIMAL_T` macro + `decimal_alias_sentinel<T>::tag` link-time guard (2a §4.4).

## Technical Context

**Language/Version:** C++23 (`[const §II.1]`). Free use of concepts, ranges, `std::expected`, `std::pmr`, deducing `this`. No fallback to earlier standards.

**Primary Dependencies:** GoogleTest 1.17.0, Google Benchmark 1.9.5 (already pinned via Conan from Phase 3 CI), libFuzzer (Clang built-in), `std::pmr` (libc++). No new Conan rows for this feature.

**Storage:** N/A (representation primitive; no persistence).

**Testing:** GoogleTest + GoogleMock (C++); pytest against Python `Decimal` oracle (cross-language seam); libFuzzer for the parse seam; Google Benchmark for the latency seam (`[const §VII.1]`, `[const §VII.2]`, `[const §VII.7]`).

**Target Platform:** Linux primary (Tier 1: Clang 22 Debug + Release + ASan + UBSan + TSan + Coverage; GCC Release sanity). Windows Tier 2 (manual / nightly) per `[const §IX.6]`.

**Project Type:** C++23 library + C-ABI legal-isolation boundary + SWIG Python bindings (`[const §IV.1]`, `[const §IV.2]`, `[const §IV.3]`).

**Performance Goals (Linux/Clang/x86_64, warm cache, 5-digit mantissa workload, default `pod_decimal` traits):**
- `parse`: ≤ 50 ns median.
- `format`: ≤ 30 ns median.
- `compare`: ≤ 20 ns median.

Bench harness `bench/core/decimal_bench.cpp` (seam #5) enforces these as Tier 1 regression bars per `[const §VIII.2]` (±5 % vs `bench/baselines/`).

**Constraints:**
- Zero allocation between parse and `fromApp` callback (`[const §VIII.5]`). Enforced by seam #6 (`mallocnesia` interceptor on Linux).
- `noexcept` on every public function of `decimal<T>` and `decimal_traits<pod_decimal>` (`[arch §5.3]`). Throwing third-party traits wrap with `detail::trap_throw`.
- C-ABI shape frozen for `FIXPP_C_ABI_VERSION_MAJOR == 1` (`[const §X.1]`); abidiff golden under `tests/abi/golden/` gates Tier 2.

**Scale/Scope:** ~9 header files + ~3 source files + 10 named test seams + 1 fuzz harness + 1 bench harness + 1 alloc-guard tool. Roughly ~2500 LOC across implementation and tests; first feature of `core/`.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-evaluated post-Phase 1 design.*

All citations below use canonical form `[const §Roman.arabic]` per `constitution.md:5`. Every cite was re-verified against the constitution after Phase 1 (see `Citation verification pass` at the bottom of this file).

| Article cited | Topic | How this feature satisfies it |
|---|---|---|
| `[const §II.1]` | C++23, no earlier fallback | Plan targets C++23 only; uses `std::expected`, `std::pmr`. |
| `[const §VI.4]`, `[const §VI.5]` | Bidirectional spec traceability + Normative References | Inherits W-009 + FLOAT-typed accessor families from 2a Appendix A (catalogue rows enumerated in `spec.md` front-matter). `spec.md §13` References lists `[FIX50SP2 §3.3]`, `[const §VIII.5]`, `[const §X.1]`, `[const §X.2]`, `[const §X.3]`, `[const §X.4]`, `[arch §4.1]`, `[arch §4.10]`, `[arch §5.3]`, `[arch §5.5]`, `[arch §6]`, `[arch §10]`. |
| `[const §VII.1]`, `[const §VII.2]`, `[const §VII.3]` | GoogleTest + pytest + TDD | `tasks.md` will be ordered red-green-refactor per test seam; pytest seam (#8) for Python `Decimal` oracle. |
| `[const §VII.7]` | Fuzzing on parser-touching modules | `tests/fuzz/fuzz_decimal_parse.cpp` (seam #7) on every Tier 1 PR. |
| `[const §VIII.2]` | ±5 % perf regression budget | Bench harness (seam #5) runs in Tier 1 with `bench/baselines/`. |
| `[const §VIII.5]` | Zero allocation on hot path | AC-P9 + AC-C4 + seam #6 (`tools/check_alloc.py` + `mallocnesia`). |
| `[const §IX.1]` | ≥90 % line / ≥80 % branch on touched modules | `linux-clang-coverage` preset measures the new files; Tier 1 gate. |
| `[const §IX.2]` | Tier 1 sanitizers | ASan + UBSan + TSan presets cover the parse-format loop. |
| `[const §IX.4]` | Tier 1 static analysis clean | clang-tidy + clang-format + cppcheck + IWYU; pre-commit + Tier 1. |
| `[const §IX.5]` | abidiff against last tagged ABI | `tests/abi/golden/fixpp_decimal_t.abidiff` snapshot (seam #4). |
| `[const §X.1]` | C-ABI versioned contract | `FIXPP_C_ABI_VERSION_MAJOR == 1`; struct shape frozen. |
| `[const §X.2]` | No C++ leakage through C-ABI | Only `extern "C"` declarations in `include/fix/c_api/decimal.h`; CI checks via `nm` / `dumpbin`. |
| `[const §X.3]` | Decimal at C-ABI is PoD `(int64 mantissa, int8 exponent)` | Literal layout per 2a §5.1; AC-A1 + AC-A2 + AC-A3 layout asserts. |
| `[const §X.4]` | Bounded `fixpp_error_t` + forwards-compat | Three codes allocated under the 2i numeric range (dated comment in `c_api_decimal.h`); rest deferred to 2i ratification. |
| `[const §X.5]` | Reentrancy contract per C-ABI symbol | Doc-comment on each of the five boundary fns names thread-safe / single-thread / requires-session-lock posture. |
| `[const §X.6]` | ABI-affecting features trigger four controls | This feature is `gate_a_required: yes` in spec front-matter; `/clarify` ran 2026-05-10; `/analyze` runs after Gate A round 2 passes. |
| `[const §XV.1]` | Heap-allocate per message on hot path is banned | Plan honors AC-P9; seam #6 enforces; no `new`/`delete` between parse and format. |
| `[const §XVII.1]` | Codex Gate A before `/tasks` | This plan is the round-2 redraft submitted to Gate A round 2 (Codex review → Opus adversarial). |
| `[const §XVII.3]` | Independence between author and reviewer | Opus author (`/plan`) + Codex reviewer (Gate A) are independent agents per `/gate-a` skill. |

**Gates pass ✅** — all cited articles resolve under canonical form to actual constitution text (verified post-Phase-1 pass below). No violations require justification, so `Complexity Tracking` is empty.

## Project Structure

### Documentation (this feature)

```text
specs/001-core-decimal/
├── plan.md              # this file (/speckit-plan output, round-2 redraft 2026-05-12)
├── spec.md              # preserved verbatim from round 1; carries /clarify Q&A 2026-05-10
├── research.md          # Phase 0 output (round-2 redraft)
├── data-model.md        # Phase 1 output (round-2 redraft)
├── quickstart.md        # Phase 1 output (round-2 redraft)
├── contracts/
│   ├── c_api_decimal.h        # Phase 1 — literal extract from 2a §5.1, §5.2
│   └── decimal_traits.hpp     # Phase 1 — literal extract from 2a §4.1, §4.2, §4.3, §4.4
└── tasks.md             # Phase 2 output (/speckit-tasks, NOT created by /speckit-plan)
```

### Source Code (library submodule root)

```text
include/
├── fixpp/
│   └── core/
│       ├── decimal.hpp                  # NEW — pod_decimal, decimal<T>, decimal_traits<T> declarations
│       ├── decimal_alias.hpp            # NEW — FIXPP_DECIMAL_T plumbing + decimal_alias_sentinel<T>
│       └── decimal_helpers.hpp          # NEW — detail::trap_throw for throwing-3p traits
└── fix/
    └── c_api/
        └── decimal.h                    # NEW — fixpp_decimal_t struct + FIXPP_DECIMAL_INITIALIZER/_INVALID + 5 boundary fns
                                         #       (carved out of include/fix/c_api.h; included by it; co-owned with 2i)

src/
├── core/
│   └── decimal.cpp                      # NEW — pod_decimal traits impl + decimal_alias_sentinel<FIXPP_DECIMAL_T>::tag def
└── capi/
    ├── decimal.cpp                      # NEW — fixpp_decimal_* boundary fn impls (co-owned with 2i)
    └── decimal_assert.cpp               # NEW — sizeof/alignof/offsetof static_asserts (seam #4 anchor)

tests/
├── core/
│   ├── decimal_parse_test.cpp                       # NEW — AC-P1..P10 (parse functional ACs)
│   ├── decimal_format_test.cpp                      # NEW — AC-S1..S6 (serialize functional ACs)
│   ├── decimal_compare_test.cpp                     # NEW — AC-C1..C6 (compare/equal functional ACs)
│   ├── decimal_cross_traits_test.cpp                # NEW — AC-X1..X3 (seam #3)
│   ├── decimal_alias_test.cpp                       # NEW — AC-B1..B4 (build-time alias macro)
│   ├── decimal_roundtrip_property_test.cpp          # NEW — seam #2 (10⁴ generated + [FIX50SP2 §3.3] table)
│   ├── decimal_reserved_tolerance_test.cpp          # NEW — seam #10 (AC-A4 _reserved garbage tolerance)
│   └── decimal_capi_layout_test.cpp                 # NEW — AC-A1..A5b layout assertions (compile + runtime)
├── support/
│   └── mock_decimal_traits.hpp                      # NEW — seam #1 (header-only failing-traits helper)
├── abi/
│   └── golden/
│       └── fixpp_decimal_t.abidiff                  # NEW — seam #4 Tier 2 abidiff golden
├── alloc_guard/
│   └── decimal_alloc_guard_test.cpp                 # NEW — seam #6 runs parse-format loop under mallocnesia
├── fuzz/
│   └── fuzz_decimal_parse.cpp                       # NEW — seam #7 (libFuzzer harness on parse)
├── oracle/
│   └── decimal_compare_oracle_test.py               # NEW — seam #8 (Python Decimal property oracle, Tier 1)
└── link/
    └── decimal_alias_mismatch_test.cmake            # NEW — seam #9 (two-TU build expected to fail link)

bench/
└── core/
    └── decimal_bench.cpp                            # NEW — seam #5 (Google Benchmark parse/format/compare bars)

tools/
└── check_alloc.py                                   # NEW — seam #6 wraps mallocnesia for the alloc-guard test
```

**Structure Decision:** single library, no web/mobile/cli split. Follows the established Phase-3 layout (`include/fixpp/core/`, `src/core/`, `src/capi/`, `tests/`, `bench/`). All new paths above are created by this feature; no existing files modified except `include/fix/c_api.h` (which gains an `#include "fix/c_api/decimal.h"` line — coordinated with 2i).

### Test seam → file mapping (10/10 — closes Root Cause #3)

This sub-section is the answer to Opus root cause #3 ("seam→file map partial") from round 1. Every one of the 10 test seams in `spec.md §9` is bound to a named on-disk file. This table also serves as the `/tasks` input — each row becomes a TDD task.

| Seam # | spec.md §9 description | On-disk path(s) | NFR / AC linkage |
|---|---|---|---|
| 1 | `mock_decimal_traits<T>` header-only failing-traits | `tests/support/mock_decimal_traits.hpp` | Used by seam #3 and downstream **2b** tests. |
| 2 | Round-trip property tests — 10⁴ generated samples + `[FIX50SP2 §3.3]` example table | `tests/core/decimal_roundtrip_property_test.cpp` | AC-P5, AC-S2..S4, §6 property test. |
| 3 | Cross-traits round-trip — value-preserving identity + `decimal_precision_loss` for out-of-domain | `tests/core/decimal_cross_traits_test.cpp` | AC-X1, AC-X2, AC-X3. |
| 4 | C-ABI layout golden (`abidiff` snapshot — Tier 2 hard-fail) | `tests/abi/golden/fixpp_decimal_t.abidiff` + `src/capi/decimal_assert.cpp` | AC-A1..A6, `[const §IX.5]`. |
| 5 | Latency regression — `parse` / `format` / `compare` Google Benchmark bars | `bench/core/decimal_bench.cpp` | NFR rows §6 (50 / 30 / 20 ns), `[const §VIII.2]`. |
| 6 | Allocation guard (Linux) — `mallocnesia` interceptor on parse-format loop | `tools/check_alloc.py` + `tests/alloc_guard/decimal_alloc_guard_test.cpp` | AC-P9, `[const §VIII.5]`, `[const §XV.1]`. |
| 7 | Fuzzer (libFuzzer) — `fuzz_decimal_parse.cpp` against ASan + UBSan invariants | `tests/fuzz/fuzz_decimal_parse.cpp` | `[const §VII.7]`, `[const §IX.4]`. |
| 8 | Property oracle (Python `Decimal`) — gates `compare` | `tests/oracle/decimal_compare_oracle_test.py` | AC-C5, Tier 1 promoted per 2a §9 seam #8. |
| 9 | `FIXPP_DECIMAL_T` link-time mismatch — two-TU build expected to fail link | `tests/link/decimal_alias_mismatch_test.cmake` (CMake harness invoking two `add_executable` calls; CI captures linker error and asserts) | AC-B3, 2a §4.4 sentinel rule. |
| 10 | `_reserved` byte tolerance — C consumer with garbage `_reserved` parses correctly | `tests/core/decimal_reserved_tolerance_test.cpp` | AC-A4, AC-A5b. |

**Rule:** no seam may map to "the existing `decimal_*_test.cpp`s collectively". Each seam has at least one dedicated named file. Cross-cutting ACs (AC-P*, AC-S*, AC-C*, AC-X*, AC-A*, AC-B*) get their own per-section test files (`decimal_parse_test.cpp`, etc.) under `tests/core/` — these are not "seam files" in the §9 sense but cover the per-AC unit checks.

## Complexity Tracking

> No Constitution Check violations. Section intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |

## Gate A

### Round 1 — 2026-05-10 (closed: full bundle redraft)

| Round | Codex P1 | Codex P2 | Codex P3 | Opus post-judging P1 | P2 | P3 | Verdict | Reviews |
|---|---|---|---|---|---|---|---|---|
| 1 | 3 | 3 | 1 | 7 | 6 | 5 | **Full bundle redraft needed** (3 root causes) | [codex_v1](../../../research/G19-fix-fpml-iso20022/research/reviews/codex_001-core-decimal_gate_a_v1_review.md), [opus_v1](../../../research/G19-fix-fpml-iso20022/research/reviews/opus_001-core-decimal_gate_a_v1_adversarial_review.md) |

Root causes per Opus: (1) `contracts/` extract authored from memory of 2a-decimal.md v0.3 (six independent shape divergences across `_format`, `_compare`, `_equal`, sentinel, `expected_t<T>` rebinding, decimal<T> member surface); (2) the broken two-symbol citation form (Roman numeral and arabic numeral split with a literal space-and-second-`§` instead of the canonical dot) was mass-rewritten in the `/plan` toolchain from canonical `[const §X.Y]`; (3) `plan.md §Project Structure` test-seam→file map partial (seams #2, #6 unfilled). Step D incremental rewrite skipped per user decision 2026-05-12; redraft submitted for round 2.

### Round 2 — 2026-05-12 (pending Codex review)

Redraft applied:
- Root cause #1: contracts are **literal extracts** from `.specify/2a-decimal.md` v0.3 §4.1–§4.4 and §5.1–§5.2. Each contract block has a `// extract from .specify/2a-decimal.md v0.3 §X` header so lineage is visible. `expected_t<T>` is `fixpp::core::expected_t<T>` per `[arch §4.1]` (alias for `std::expected<T, fixpp::core::error>`); no local `decimal_error` enum. `decimal<T>` keeps every normative member from 2a §4.3 (default ctor, `parse`, `format`, `from<U>`, `to<U>`, friend `operator==` / `operator<=>`, `decimal_default` alias).
- Root cause #2: all `[const §...]` citations in this round-2 bundle use canonical `[const §Roman.arabic]` form. Verification pass below.
- Root cause #3: test seam → file mapping table above lists all 10 seams against named paths; seam #2 and #6 now have dedicated files.
- AC-X3 carve (round-1 P1): contract preserves uniform `expected_t<decimal<U>>` return for `to<U>()` per 2a §4.3 signature; the `if constexpr (std::is_same_v<T,U>)` short-circuit is an **implementation choice** at `/implement` time (per spec.md `Clarifications` 2026-05-10 line 63), not an API shape change. No 2a v0.4 amendment required.

**One NEEDS CLARIFICATION surfaced and resolved during round 2 (`/clarify` 2026-05-12):** AC-C6 (spec.md `/clarify` 2026-05-10) requires `fixpp_decimal_compare` / `_equal` to return `FIXPP_ERR_DECIMAL_INVALID` on out-of-domain inputs, but 2a §5.2 freezes their signatures as `int`-direct-return (no error channel). Resolved as **option 1** — add `_compare_checked` / `_equal_checked` siblings that own AC-C6's defensive validation; bare entry points stay 2a §5.2 verbatim. See `spec.md Clarifications` Session 2026-05-12 + `research.md` D-12. Per the round-2 redraft brief: "the round-1 failure mode was silent override; the fix is explicit dissent." This was the dissent; resolved.

### Citation verification pass (round 2)

| Cite | Resolves to | OK |
|---|---|---|
| `[const §II.1]` | `constitution.md:26` — "Language standard: C++23." | ✅ |
| `[const §IV.1]` | `constitution.md:56` — "C++ library is the primary public surface." | ✅ |
| `[const §IV.2]` | `constitution.md:57` — "C ABI is the legal isolation boundary." | ✅ |
| `[const §IV.3]` | `constitution.md:58` — "Python bindings ship via SWIG over the C ABI." | ✅ |
| `[const §VI.4]` | `constitution.md:79` — "Bidirectional traceability." | ✅ |
| `[const §VI.5]` | `constitution.md:80` — "Normative References section." | ✅ |
| `[const §VII.1]` | `constitution.md:87` — "GoogleTest + GoogleMock." | ✅ |
| `[const §VII.2]` | `constitution.md:88` — "pytest against the SWIG bindings." | ✅ |
| `[const §VII.3]` | `constitution.md:89` — "TDD is mandatory." | ✅ |
| `[const §VII.7]` | `constitution.md:93` — "Fuzzing — libFuzzer corpus." | ✅ |
| `[const §VIII.2]` | `constitution.md:100` — "Regression budget: ±5 %." | ✅ |
| `[const §VIII.5]` | `constitution.md:106` — "Allocator policy on the hot path: zero new/delete." | ✅ |
| `[const §IX.1]` | `constitution.md:113` — "Coverage thresholds." | ✅ |
| `[const §IX.2]` | `constitution.md:117` — "Sanitizers — Tier 1." | ✅ |
| `[const §IX.4]` | `constitution.md:119` — "Static analysis — Tier 1." | ✅ |
| `[const §IX.5]` | `constitution.md:124` — "ABI check (from the first tagged C ABI release onward)." | ✅ |
| `[const §IX.6]` | `constitution.md:125` — "Two-tier CI." | ✅ |
| `[const §X.1]` | `constitution.md:133` — "C ABI is a versioned contract." | ✅ |
| `[const §X.2]` | `constitution.md:134` — "No C++ symbol leakage through the C ABI." | ✅ |
| `[const §X.3]` | `constitution.md:135` — "Decimal at the C ABI boundary: PoD." | ✅ |
| `[const §X.4]` | `constitution.md:136` — "Error reporting at the C ABI." | ✅ |
| `[const §X.5]` | `constitution.md:137` — "Reentrancy contract." | ✅ |
| `[const §X.6]` | `constitution.md:138` — "ABI-affecting features trigger all four mandatory controls." | ✅ |
| `[const §XV.1]` | `constitution.md:207` — "Heap-allocate per message or per field on the hot path." | ✅ |
| `[const §XVII.1]` | `constitution.md:245` — "Gate A — Design review." | ✅ |
| `[const §XVII.3]` | `constitution.md:257` — "Independence rule." | ✅ |

All 26 citations in this plan resolve under canonical form. Cross-doc cites (`[arch §4.1]`, `[arch §4.10]`, `[arch §5.3]`, `[FIX50SP2 §3.3]`, `[SYN §3.1 Q5]`) are inherited verbatim from `spec.md §13` References and `.specify/2a-decimal.md` Appendix B.
