---
id: 001-core-decimal
title: Tasks — Decimal type (`fixpp::core::decimal<T>` + `fixpp_decimal_t` C-ABI)
spec_kit_step: /tasks
last_updated: 2026-05-12
status: drafted (post-/analyze remediation 2026-05-12 — 12 issues addressed; 5 new tasks inserted, 11 rewrites)
---

# Tasks: 001-core-decimal — Decimal type

**Input**: Design documents from `specs/001-core-decimal/`
**Prerequisites**: `plan.md` (required), `spec.md` (required), `research.md`, `data-model.md`, `contracts/c_api_decimal.h`, `contracts/decimal_traits.hpp`, `quickstart.md`

**Tests**: TDD is mandatory per `[const §VII.3]`. Every test file is written before its implementation (red→green). No story is complete until all seams for that story pass Tier 1 CI.

**Organization**: Tasks are grouped by user story per `spec.md §3`. Each story has independent acceptance criteria enabling independent implementation and verification.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Which user story this task belongs to (`[US1]`–`[US5]` maps to `spec.md §3.1`–`§3.5`)
- Exact file paths are included in every implementation task

---

## Phase 1: Setup (Build Infrastructure)

**Purpose**: Wire up CMake targets and directory structure for all new files. No implementation yet.

- [X] T001 Create all new source/header/test directories per `plan.md §Project Structure` (`include/fixpp/core/`, `include/fix/c_api/`, `src/core/`, `src/capi/`, `tests/core/`, `tests/support/`, `tests/abi/golden/`, `tests/alloc_guard/`, `tests/fuzz/`, `tests/oracle/`, `tests/link/`, `bench/core/`, `bench/baselines/`, `bench/results/`, `tools/`)
- [X] T002 [P] Extend or create `src/core/CMakeLists.txt` registering `fixpp_core` library target compiling `src/core/decimal.cpp`; integrate with Phase-3 top-level CMakeLists
- [X] T003 [P] Create `tests/core/CMakeLists.txt` registering two executables: (a) `fixpp_core_tests` with the pure-C++ sources `decimal_parse_test.cpp`, `decimal_format_test.cpp`, `decimal_compare_test.cpp`, `decimal_roundtrip_property_test.cpp`, `decimal_cross_traits_test.cpp`, `decimal_alias_test.cpp` — links against `fixpp_core` + GoogleTest 1.17.0; (b) `fixpp_capi_tests` with the C-ABI-dependent sources `decimal_capi_layout_test.cpp`, `decimal_reserved_tolerance_test.cpp` — links against `fixpp_capi` + GoogleTest 1.17.0; the split prevents US1 / US2 builds from blocking on US3 header creation
- [X] T004 [P] Create `src/capi/CMakeLists.txt` (or extend existing skeleton) compiling `src/capi/decimal.cpp` + `src/capi/decimal_assert.cpp` into `fixpp_capi` target; link against `fixpp_core`
- [X] T005 [P] Create `tests/fuzz/CMakeLists.txt` for `fuzz_decimal_parse` (libFuzzer flags, ASan); `tests/alloc_guard/CMakeLists.txt` for `decimal_alloc_guard_test`; `bench/core/CMakeLists.txt` for `decimal_bench` against Google Benchmark 1.9.5

---

## Phase 2: Foundational (Header Declarations + Test-Support Infrastructure)

**Purpose**: Declare all public C++ API shapes and create seam-#1 test infrastructure. Tests and implementations both depend on these declarations — no user story test can compile until this phase is complete.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete.

- [X] T006 Verify `include/fixpp/core/error.hpp` exists from a Phase 3 skeleton (owned by **2k**); if absent, CREATE it with `namespace fixpp::core { enum class error : int { /* slot 0 reserved for ok */ out_of_memory = 1, /* ... */ }; template<class T> using expected_t = std::expected<T, error>; }` per `[arch §4.1]`. Then add the four decimal variants (`decimal_invalid_input`, `decimal_overflow`, `decimal_precision_loss`, `decimal_buffer_too_small`) per `data-model.md §Entity 5`; mark each variant with a `// owned by 001-core-decimal, contributes to 2k` comment for 2k's eventual takeover. (research.md D-8)
- [X] T007 Declare `pod_decimal` struct, `pod_decimal_invalid` sentinel constant, and `decimal_traits<T>` primary template (undefined body) in `include/fixpp/core/decimal.hpp` per `contracts/decimal_traits.hpp §§4.1–4.2` and `data-model.md §Entity 1–2`; include required member types and static function signatures as commented contract (no implementations yet)
- [X] T008 Declare `decimal<T>` class template (all seven normative members: default ctor, value ctor, `value()`, `parse`, `format`, `from<U>`, `to<U>`; friend `operator==`/`operator<=>`; private `T value_{}`; `using decimal_default = decimal<pod_decimal>`) in `include/fixpp/core/decimal.hpp` per `contracts/decimal_traits.hpp §4.3` and `data-model.md §Entity 3`; member bodies left as stub `{ return {}; }` until Phase 3
- [X] T009 [P] Declare `detail::trap_throw<F>` function template in `include/fixpp/core/decimal_helpers.hpp` per `contracts/decimal_traits.hpp` decimal_helpers section; body left as stub; needed before any throwing-3p trait can compile
- [X] T010 [P] Create `tests/support/mock_decimal_traits.hpp` — seam #1: header-only parameterizable failing-traits helper configurable to fail on `from_chars`, `to_chars`, `from_pod`, `to_pod`, or `compare`; used by `decimal_cross_traits_test.cpp` (seam #3) and future downstream 2b tests per `plan.md §Seam 1`

**Checkpoint**: All new headers compile. `fixpp_core_tests` executable links (even with stub bodies). `mock_decimal_traits.hpp` is usable by later test files.

---

## Phase 3: US1 — Engine Integrator / Default Traits Parse–Format–Compare (Priority: P1) 🎯 MVP

**Goal**: A wire-layer author calls `decimal_traits<pod_decimal>::from_chars(span, mr)` on FLOAT bytes and receives a `pod_decimal` value — no allocation, no exceptions. All of AC-P1..P10, AC-S1..S6, and AC-C1..C5 pass Tier 1 CI. Seams #2, #5, #6, #7 compile and run.

**Independent Test**: `ctest --preset linux-clang-debug -R '^decimal_(parse|format|compare|roundtrip)_test$'`; seams #5 bench, #6 alloc-guard, #7 fuzz compile and produce expected results.

### Tests for User Story 1 (TDD — write first, ensure FAIL before implementation)

- [X] T011 [P] [US1] Write `tests/core/decimal_parse_test.cpp` — GoogleTest cases for AC-P1..P10: empty input → `decimal_invalid_input`; `+0`/`-0` → `{0,0}`; bare `.5`/`5.` → `decimal_invalid_input`; embedded SOH → error; `00005` → `5`; trailing-fractional zero preservation; mantissa overflow → `decimal_overflow`; `exponent < -38` → `decimal_overflow`; sentinel collision → `decimal_overflow`; on-failure `out` unmodified; per `spec.md §4.1`
- [X] T012 [P] [US1] Write `tests/core/decimal_format_test.cpp` — GoogleTest cases for AC-S1..S6: sentinel `mantissa == INT64_MIN` → `decimal_invalid_input`; `mantissa == 0` → `"0"` regardless of exponent; exponent outside `[-38,0]` → `decimal_invalid_input` before formatting; trailing-zero stripping; worst-case 41-byte bound with `pod_decimal` traits; buffer too small → `decimal_buffer_too_small`; per `spec.md §4.2`
- [X] T013 [P] [US1] Write `tests/core/decimal_compare_test.cpp` — GoogleTest cases for AC-C1..C5: `{1,0}`, `{10,-1}`, `{100,-2}` compare equal; `pod_decimal_invalid` orders strictly greater than every finite value; algorithm never uses `__int128`; result is `std::strong_ordering`; `noexcept`; per `spec.md §4.3`
- [X] T014 [P] [US1] Write `tests/core/decimal_roundtrip_property_test.cpp` — seam #2: 10⁴ generated canonical-domain sample round-trips (encode → parse → re-encode → compare equal) plus literal `[FIX50SP2 §3.3]` example table entries; covers AC-P5, AC-S2..S4; per `plan.md §Seam 2`
- [X] T015 [P] [US1] Write `bench/core/decimal_bench.cpp` — seam #5: Google Benchmark `BM_decimal_parse`, `BM_decimal_format`, `BM_decimal_compare` with 5-digit mantissa workload; regression bars 50/30/20 ns median; per `spec.md §6` and `plan.md §Seam 5`; compile-only at this stage (runs in T023 Polish step)
- [X] T016 [P] [US1] Write `tests/fuzz/fuzz_decimal_parse.cpp` — seam #7: libFuzzer entry point (`LLVMFuzzerTestOneInput`) calling `decimal_traits<pod_decimal>::from_chars` on arbitrary bytes; invariant — no crash, no `std::terminate`, result is either valid `pod_decimal` or `fixpp::core::error` code; per `plan.md §Seam 7`
- [X] T017 [P] [US1] Write `tests/alloc_guard/decimal_alloc_guard_test.cpp` — seam #6: parse-format loop calling `decimal<pod_decimal>::parse(...)` then `decimal<pod_decimal>::format(...)`; test body has zero heap calls; designed to be wrapped by `mallocnesia` interceptor via `check_alloc.py`; per `plan.md §Seam 6`
- [X] T018 [P] [US1] Write `tools/check_alloc.py` — seam #6: Python wrapper that LD_PRELOADs `mallocnesia` and runs `decimal_alloc_guard_test` binary; exits nonzero if any `malloc`/`free` intercepted between parse and format calls; per `plan.md §Seam 6` + `research.md D-10`

### Implementation for User Story 1

- [X] T019a [US1] Implement `decimal_traits<pod_decimal>::from_chars` in `src/core/decimal.cpp` — single-pass parser (AC-P1..P10): handle sign, integer digits, optional `.` separator, fractional digits, mantissa overflow check, sentinel-collision check; on-failure leaves `out` unmodified (AC-P10); no allocation, `noexcept`; verify `tests/core/decimal_parse_test.cpp` (T011) goes green before T019b
- [X] T019b [US1] Implement `decimal_traits<pod_decimal>::to_chars` in `src/core/decimal.cpp` — serializer (AC-S1..S6): sentinel check, zero shortcut (AC-S2), two-sided exponent check (AC-S3), trailing-zero strip (AC-S4), worst-case 41-byte bound (AC-S5), buffer-too-small mapping (AC-S6); no allocation, `noexcept`; verify `tests/core/decimal_format_test.cpp` (T012) goes green
- [X] T020 [US1] Implement `decimal_traits<pod_decimal>::compare` using digit-string algorithm of `research.md D-5` / 2a §6.3 in `src/core/decimal.cpp` — steps: sentinel check (AC-C2), sign compare, canonicalize (strip trailing-zero base-10 digits), magnitude-bucket compare (`digit_count + exponent`), lex-compare digit strings on tie; O(≤19 iterations), no `__int128`, `noexcept` (AC-C1..C4)
- [X] T021 [US1] Implement `decimal_traits<pod_decimal>::from_pod`, `to_pod`, `is_finite`, `is_zero`, `is_negative`, `is_lossless_for_fix_float = true`, `max_serialized_bytes = 41` in `src/core/decimal.cpp`; `to_pod` returns `decimal_overflow` for `mantissa == INT64_MIN` sentinel; per `data-model.md §Entity 2`
- [X] T022 [US1] Implement `decimal<T>::parse` and `format` member bodies in `include/fixpp/core/decimal.hpp` — thin shells over `decimal_traits<T>::from_chars` / `to_chars`; leave `from<U>` / `to<U>` member bodies as `static_assert(sizeof(U) == 0, "cross-traits funnel implemented in US4 / T039–T040");` stubs that compile only when never instantiated. US1 itself does not instantiate `from<U>`/`to<U>`, so the stubs are inert here.
- [X] T023 [US1] Build `fixpp_core_tests` (NOT `fixpp_capi_tests` — that target depends on T033) and run `ctest --preset linux-clang-debug -R '^decimal_(parse|format|compare|roundtrip)_test$'` — confirm all AC-P1..P10 + AC-S1..S6 + AC-C1..C5 + seam #2 pass green; confirm `fuzz_decimal_parse` and `decimal_alloc_guard_test` compile; fix any failures before moving to Phase 4

**Checkpoint**: US1 core functional ACs all pass. Wire-layer FLOAT parsing is unblocked. Bench, alloc-guard, fuzz compile and can be run.

---

## Phase 4: US2 — Codegen Integrator / Build-Time Alias (Priority: P2)

**Goal**: Codegen author emits `fixpp::decimal_t` (the engine-wide alias) as FLOAT-accessor return type. Alias defaults to `decimal<pod_decimal>`; consumer override via `FIXPP_DECIMAL_T` + `FIXPP_DECIMAL_USER_HEADER`. Link-time sentinel fires on alias mismatch. All AC-B1..B4 pass.

**Independent Test**: `ctest --preset linux-clang-debug -R '^decimal_alias(_test|_mismatch)$'`; seam #9 wrapper produces expected link failure asserting the `decimal_alias_sentinel` symbol substring.

### Tests for User Story 2 (TDD — write first, ensure FAIL before implementation)

- [X] T024 [P] [US2] Write `tests/core/decimal_alias_test.cpp` — GoogleTest cases AC-B1..B4: default `fixpp::decimal_t == decimal<pod_decimal>` (B1); `FIXPP_DECIMAL_USER_HEADER` include mechanism verified via compile-time check (B2); sentinel symbol links (B3 positive case — builds without unresolved symbol when alias matches); switching alias does not change `fixpp_decimal_t` C-ABI shape (B4); per `spec.md §4.6`
- [X] T025 [P] [US2] Write `tests/link/decimal_alias_mismatch_test.cmake` — seam #9: CMake harness with two `add_executable` targets each defining a conflicting `FIXPP_DECIMAL_T`. Also write `tests/link/check_expected_failure.py` — Python wrapper that runs `cmake -S tests/link -B <tmp> && cmake --build <tmp> --target decimal_alias_mismatch_test 2>&1 | tee build.log`, asserts the build exits nonzero, AND asserts the linker error message contains the substring `decimal_alias_sentinel` (rejects spurious failures from compile errors or missing headers). Register this script as a CTest test in `tests/CMakeLists.txt` so it runs under `ctest --preset linux-clang-debug -R '^decimal_alias_mismatch$'`. Per `plan.md §Seam 9`, `research.md D-7`, and `quickstart.md §2` seam #9 entry ("Suite parser asserts the failure is the expected one")

### Implementation for User Story 2

- [X] T026 [US2] Implement `include/fixpp/core/decimal_alias.hpp` — `FIXPP_DECIMAL_USER_HEADER` conditional include; `FIXPP_DECIMAL_T` default macro → `::fixpp::core::pod_decimal`; `namespace fixpp { using decimal_t = core::decimal<FIXPP_DECIMAL_T>; }`; `decimal_alias_sentinel<T>` template struct with `static char const tag;` declared; `fixpp_decimal_alias_lock` inline pointer in `fixpp::detail` namespace (NOT `fixpp::core` per `data-model.md §Entity 6` note); per `contracts/decimal_traits.hpp §4.4`
- [X] T027 [US2] Add `template<> char const fixpp::detail::decimal_alias_sentinel<FIXPP_DECIMAL_T>::tag = 0;` definition to `src/core/decimal.cpp` — exactly one specialization per translation unit; per `data-model.md §Entity 6`
- [X] T028 [US2] Run `ctest --preset linux-clang-debug -R '^decimal_alias(_test|_mismatch)$'` — AC-B1..B4 pass; seam #9 wrapper (`check_expected_failure.py`) confirms expected link failure with `decimal_alias_sentinel` substring match; fix any alias namespace or sentinel issues

**Checkpoint**: `fixpp::decimal_t` resolves correctly by default and via user override. Link-time sentinel fires on mismatch. Codegen (`2c`) is unblocked.

---

## Phase 5: US3 — C/Python Consumer / C-ABI Boundary Functions (Priority: P3)

**Goal**: C/Python consumer calls `fixpp_decimal_parse`, `_format`, `_compare`, `_equal`, `_init` on the frozen 16-byte `fixpp_decimal_t` struct. `_checked` siblings own AC-C6 defensive validation. Layout `static_assert`s and seam #10 `_reserved` tolerance pass.

**Independent Test**: `ctest --preset linux-clang-debug -R '^decimal_(capi_layout|reserved_tolerance)_test$'`; `src/capi/decimal_assert.cpp` static_asserts compile clean; Python oracle `pytest tests/oracle/decimal_compare_oracle_test.py -q` passes.

### Tests for User Story 3 (TDD — write first, ensure FAIL before implementation)

- [X] T029 [P] [US3] Write `tests/core/decimal_capi_layout_test.cpp` — compile-time `static_assert`s for AC-A1 (`sizeof(fixpp_decimal_t) == 16`), AC-A2 (`alignof == 8`, `offsetof` for `mantissa`, `exponent`, `_reserved`), AC-A3 (`is_standard_layout`); runtime assertions for AC-A4 (`_reserved` ignored on read), AC-A5 (`FIXPP_DECIMAL_INITIALIZER` and `fixpp_decimal_init()` zero `_reserved`), AC-A5b (non-zero `_reserved` is tolerated); per `spec.md §4.5`
- [X] T030 [P] [US3] Write `tests/core/decimal_reserved_tolerance_test.cpp` — seam #10: AC-A4 + AC-A5b regression guard; construct `fixpp_decimal_t` with garbage bytes in `_reserved[7]`, call `fixpp_decimal_parse` and `fixpp_decimal_format`; verify result is identical to zero-`_reserved` case; no UB under ASan; per `plan.md §Seam 10`
- [X] T030b [P] [US3] Create `tests/oracle/conftest.py` — pytest fixture `libfixpp_capi` that resolves `libfixpp_capi.so` via `ctypes.CDLL(os.environ.get("FIXPP_BUILD_DIR", "build/linux-clang-release") + "/lib/libfixpp_capi.so")`; declare `argtypes` and `restype` for `fixpp_decimal_compare(a, b)` and `fixpp_decimal_compare_checked(a, b, c_int*)` using a `ctypes.Structure` mirror of `fixpp_decimal_t`. Also create `tests/oracle/pytest.ini` adding `tests/oracle/` to `testpaths`. Required before T031 oracle test can run.
- [X] T031 [P] [US3] Write `tests/oracle/decimal_compare_oracle_test.py` — seam #8: pytest generating 10⁴ arbitrary canonical-domain `(a, b)` pairs; calls `fixpp_decimal_compare(a, b)` via the ctypes fixture from T030b against the built `libfixpp_capi.so`; compares result against Python `decimal.Decimal` oracle; per `plan.md §Seam 8` (Tier 1 promoted from Tier 2 per 2a §9 seam #8)

### Implementation for User Story 3

- [X] T032 [US3] Create `src/capi/decimal_assert.cpp` — seam #4 *compile-time* anchor (not a runtime test; this is a production source file containing six `static_assert`s for `sizeof(fixpp_decimal_t) == 16`, `alignof(fixpp_decimal_t) == 8`, `offsetof(fixpp_decimal_t, mantissa) == 0`, `offsetof(fixpp_decimal_t, exponent) == 8`, `offsetof(fixpp_decimal_t, _reserved) == 9`, `std::is_standard_layout_v<fixpp_decimal_t>`). The asserts fail to compile until T033 creates `fixpp_decimal_t` with the correct layout; per `data-model.md §Entity 4` + `plan.md §Seam 4`
- [X] T033 [US3] Create `include/fix/c_api/decimal.h` matching `contracts/c_api_decimal.h` byte-for-byte — `fixpp_decimal_t` struct; `FIXPP_DECIMAL_INITIALIZER` / `FIXPP_DECIMAL_INVALID` macros; five 2a §5.2 verbatim bare boundary fn declarations; two `_checked` sibling declarations; provisional `FIXPP_ERR_*` `#define`s with `// allocated 2026-05-12, owned by 2i` comments; self-sufficient `fixpp_error_t` forward typedef; `extern "C"` guards; `#ifndef` include guard; per `research.md D-3` + `D-4` + `D-12`
- [X] T033b [US3] Add `#include "fix/c_api/decimal.h"` to `include/fix/c_api.h` master header at the appropriate location (after existing forward typedefs, before any other carve-out includes); preserve include order coordinated with **2i**; verify Phase 3 compilation does not break (`cmake --build --preset linux-clang-debug --target fixpp_capi`); per `plan.md §Project Structure` ("included by `include/fix/c_api.h`; co-owned with 2i")
- [X] T034 [US3] Implement `fixpp_decimal_parse`, `fixpp_decimal_format`, `fixpp_decimal_init` in `src/capi/decimal.cpp` — thin C-ABI thunks over `decimal_traits<pod_decimal>` members; `fixpp_decimal_parse` maps `fixpp::core::error` variants to `FIXPP_ERR_DECIMAL_INVALID` / `FIXPP_ERR_DECIMAL_PRECISION_LOSS` / `FIXPP_ERR_BUFFER_TOO_SMALL` per `data-model.md §Entity 5`; `fixpp_decimal_format` applies AC-S3 exponent pre-check; `fixpp_decimal_init` zero-fills `_reserved[7]`
- [X] T035 [US3] Implement bare `fixpp_decimal_compare`, `fixpp_decimal_equal` (2a §5.2 verbatim — no validation, canonical-domain precondition, `int`-direct-return) and `_checked` siblings `fixpp_decimal_compare_checked`, `fixpp_decimal_equal_checked` (AC-C6: validate `exponent ∈ [-38,0]` on both inputs; out-of-domain → `FIXPP_ERR_DECIMAL_INVALID`; in-domain → write ordering/equality to out-param, return `FIXPP_ERR_OK`) in `src/capi/decimal.cpp`; per `research.md D-4` + `D-12`
- [X] T036 [US3] Build `fixpp_capi_tests` and run `ctest --preset linux-clang-debug -R '^decimal_(capi_layout|reserved_tolerance)_test$'`; then run `pytest tests/oracle/decimal_compare_oracle_test.py -q`; verify `src/capi/decimal_assert.cpp` static_asserts compile; AC-A1..A6 + seam #8 + seam #10 all pass

**Checkpoint**: C-ABI layer is complete. Python/C consumers can call boundary functions. SWIG bindings (future 2i) can call `_checked` path.

---

## Phase 6: US4 — High-Precision FX/Crypto Consumer / Cross-Traits Conversion (Priority: P4)

**Goal**: A consumer sets `-DFIXPP_DECIMAL_T=my::decimal128` at build time and the engine widens accordingly. `decimal<T>::from<U>()` / `to<U>()` funnel through `pod_decimal` for T≠U with `decimal_precision_loss` on out-of-domain narrowing. T==U path short-circuits without any funnel cost. Seam #3 (AC-X1..X3) passes.

**Independent Test**: `ctest --preset linux-clang-debug -R '^decimal_cross_traits_test$'` — all three AC-X cases pass using an in-test wider-T (`test_decimal_wide` defined inline in the test file).

### Tests for User Story 4 (TDD — write first, ensure FAIL before implementation)

- [X] T037 [P] [US4] Write `tests/core/decimal_cross_traits_test.cpp` — seam #3: AC-X1 (`T≠U` round-trip funnels through `pod_decimal` and restores value for in-domain values); AC-X2 (out-of-domain narrowing returns `decimal_precision_loss`, no silent truncation); AC-X3 (`T==U` `if constexpr` short-circuit returns source unchanged, no funnel, no error, no codegen overhead). Define a minimal wider-T inline in this test file: `struct test_decimal_wide { __int128 mantissa; std::int8_t exponent; };` plus a non-throwing `decimal_traits<test_decimal_wide>` specialization that delegates `from_pod`/`to_pod` to a trivial widen/narrow with `decimal_precision_loss` on narrowing overflow. Also create the companion alias-swap header `tests/core/decimal_cross_traits_test_alias_header.hpp` that forward-declares `test_decimal_wide` and includes its traits specialization (used by T041's alias-swap smoke). This wider-T is the AC-X1/X2 vehicle; `mock_decimal_traits.hpp` (T010) remains the failing-traits helper for negative-path tests. Per `plan.md §Seam 3` and `spec.md §4.4`.

### Implementation for User Story 4

- [X] T038 [US4] Implement `detail::trap_throw<F>` body in `include/fixpp/core/decimal_helpers.hpp` — `try { return fn(); } catch(std::bad_alloc const&) { return std::unexpected{fixpp::core::error::out_of_memory}; } catch(...) { return std::unexpected{fixpp::core::error::decimal_invalid_input}; }` per `contracts/decimal_traits.hpp` decimal_helpers section; required before any throwing-3p trait specialization compiles
- [X] T039 [US4] Implement `decimal<T>::from<U>()` body in `include/fixpp/core/decimal.hpp` (replaces T022's `static_assert` stub) — for T≠U: call `decimal_traits<U>::to_pod(src.value())` then `decimal_traits<T>::from_pod(pod)`; propagate `decimal_precision_loss` on `to_pod` failure (AC-X2); for T==U: `if constexpr (std::is_same_v<T,U>)` returns `expected_t<decimal<U>>{*this}` directly (AC-X3, research.md D-11); uniform `expected_t<decimal<U>>` return type per `contracts/decimal_traits.hpp §4.3`
- [X] T040 [US4] Implement `decimal<T>::to<U>()` body in `include/fixpp/core/decimal.hpp` (replaces T022's `static_assert` stub) — for T≠U: `decimal_traits<T>::to_pod(value_)` then `decimal_traits<U>::from_pod`; for T==U: `if constexpr` short-circuit returns `expected_t<decimal<U>>{decimal<U>{value_}}`; uniform return type `expected_t<decimal<U>>` in both branches per `contracts/decimal_traits.hpp §4.3` (no `conditional_t` per `research.md D-11`)
- [X] T041 [US4] Run `ctest --preset linux-clang-debug -R '^decimal_cross_traits_test$'` — seam #3 AC-X1..X3 pass; smoke-build an alias-swap configuration that re-runs the AC-B1..B4 alias tests with `-DFIXPP_DECIMAL_T=::test_decimal_wide -DFIXPP_DECIMAL_USER_HEADER='"tests/core/decimal_cross_traits_test_alias_header.hpp"'` (the aux header was created as part of T037); exercises AC-B2/B4 end-to-end against a real wider-T (not `pod_decimal`)

**Checkpoint**: Cross-traits funnel is functional. High-precision alias consumers can swap at build time. `decimal_helpers.hpp` is usable by future 3p-trait authors.

---

## Phase 7: US5 — ABI Consumer / Frozen Layout Golden (Priority: P5)

**Goal**: Binding author pins `tests/abi/golden/fixpp_decimal_t.abidiff` in CI and receives a hard-fail on any future `fixpp_decimal_t` shape or boundary-function symbol drift. Tier 2 abidiff gate is operational.

**Independent Test**: `abidiff` command from `quickstart.md §3` produces output identical to the stored golden; seam #4 Tier 2 gate passes.

### Tests for User Story 5 (golden capture first, then lock)

- [X] T042 [P] [US5] Build `linux-clang-release` preset and generate initial `tests/abi/golden/fixpp_decimal_t.abidiff` via the `abidiff` command from `quickstart.md §3`; seam #4 Tier 2 golden initialized; per `plan.md §Seam 4` + `research.md D-3`
- [X] T043 [P] [US5] Write `tests/abi/abidiff.suppr` suppressions file scoped to `fixpp_decimal_t` struct and the seven C-ABI boundary function symbols; prevents unrelated symbol churn from polluting the golden comparison
- [X] T044 [US5] Add Tier 2 abidiff step to CI documentation (or CMakeLists Tier 2 target) referencing `tests/abi/golden/fixpp_decimal_t.abidiff` + `tests/abi/abidiff.suppr`; verify stored golden matches a fresh `linux-clang-release` build; per `plan.md §Seam 4` and `quickstart.md §3`

**Checkpoint**: ABI golden is on-disk and verifiable. Any future `fixpp_decimal_t` layout or symbol change triggers a Tier 2 hard-fail.

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Static analysis, sanitizer runs, coverage validation, ABI symbol audit, and bench baseline capture across all stories.

- [X] T045 [P] Run `clang-tidy --config-file=.clang-tidy` on all new headers (`include/fixpp/core/decimal*.hpp`, `include/fix/c_api/decimal.h`) and sources (`src/core/decimal.cpp`, `src/capi/decimal*.cpp`); fix all violations per `[const §IX.4]`
- [X] T046 [P] Run `cppcheck --enable=all --suppress=missingIncludeSystem` and `iwyu_tool.py -p build/linux-clang-debug` on all new files; fix any extra-include or unused-include violations
- [X] T047 [P] Run `clang-format --dry-run --Werror` on all new headers and sources; apply formatting fixes where needed per `[const §IX.4]`
- [X] T048 [P] Run `ctest --preset linux-clang-coverage -R '^decimal_'`; verify `≥90 % line / ≥80 % branch` per `[const §IX.1]` on all new files; add targeted test cases for any gaps before accepting
- [X] T049 [P] Run `ctest --preset linux-clang-asan -R '^decimal_'` + `--preset linux-clang-ubsan` + `--preset linux-clang-tsan`; fix any sanitizer violation per `[const §IX.2]`
- [X] T049b [P] Run `nm -D --defined-only build/linux-clang-release/lib/libfixpp_capi.so | awk '$2=="T"{print $3}' | grep -v '^fixpp_' | grep -v '^_init$\|^_fini$'` — output MUST be empty per `[const §X.2]` (no C++ symbol leakage through the C ABI; only `fixpp_*` `extern "C"` exports allowed); fix any leaked symbols by adding `static` or moving to an internal translation unit
- [X] T050 [P] Run `fuzz_decimal_parse` for ≥ 10 minutes under libFuzzer + ASan (seam #7 Tier 1 smoke gate per `quickstart.md §8`); fix any crash, `std::terminate`, or sanitizer violation found
- [X] T051 Run `tools/check_alloc.py --target decimal_alloc_guard_test` on the `linux-clang-debug` build (seam #6); fix if any allocation is intercepted between parse and format per `[const §VIII.5]`
- [X] T052 [P] Capture `bench/baselines/decimal_baseline.json` from a `linux-clang-release` run per `quickstart.md §4`; commit as the ±5 % regression baseline per `[const §VIII.2]`
- [X] T053 Run `tools/bench_compare.py --baseline bench/baselines/decimal_baseline.json --current bench/results/decimal.json`; if 50/30/20 ns bars are missed, record in PR description per `spec.md §11 Risk row 1` and `spec.md §12.2` — do NOT block merge, but provide rationale
- [X] T054 Update `spec/feature-catalogue.md` W-009 row status from `planned` to `in-progress` (decimal primitive landed; wire FLOAT-accessor still pending **2b**); add `evidence_pr: <this PR SHA>` annotation per catalogue schema; update `spec/coverage-index.md` if W-009 carries a coverage-index entry for the FLOAT family; verify Tier 1 `catalogue-consistency-check` CI step passes locally before opening the PR per `[const §IX.6]`

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — start immediately
- **Foundational (Phase 2)**: Depends on Phase 1 — BLOCKS all user story test compilation
- **US1 (Phase 3)**: Depends on Phase 2 completion — P1 priority, critical path
- **US2 (Phase 4)**: Depends on Phase 2 headers (T007/T008); can start in parallel with US1 implementation after T008 completes
- **US3 (Phase 5)**: Depends on US1 implementation (T019a–T022) — C-ABI thunks call into `decimal_traits<pod_decimal>`
- **US4 (Phase 6)**: Depends on Phase 2 (seam #1 `mock_decimal_traits.hpp` from T010); `from<U>`/`to<U>` bodies also depend on T021 (`from_pod`/`to_pod`); replaces T022's `static_assert` stubs
- **US5 (Phase 7)**: Depends on US3 completion — needs the built `libfixpp_capi.so` to generate abidiff golden
- **Polish (Phase 8)**: Depends on all user stories complete

### User Story Dependencies

- **US1 (P1)**: No story dependencies — start after Foundational
- **US2 (P2)**: Depends only on Phase 2 headers — independent of US1 implementation; can overlap with US1
- **US3 (P3)**: Depends on US1 traits implementation (T019a–T021)
- **US4 (P4)**: Depends on Phase 2 (`mock_decimal_traits.hpp` T010) + US1 `from_pod`/`to_pod` (T021); T039/T040 replace T022's `from<U>`/`to<U>` `static_assert` stubs
- **US5 (P5)**: Depends on US3 C-ABI built (T033–T035)

### Within Each User Story

- All test-writing tasks (TDD) are written and compile before any implementation task in that story
- Implementation tasks within a story follow: traits members → `decimal<T>` member bodies → integration check
- Story is not complete until its ctest run is green

### Parallel Opportunities

- All Phase 1 tasks T002–T005 can run in parallel (different CMakeLists files)
- T007, T008, T009, T010 in Phase 2 can run in parallel (different header files)
- Test-writing tasks within each story phase (marked `[P]`) can run in parallel
- US1 (Phase 3) and US2 (Phase 4) can overlap after Phase 2 completes
- All Polish tasks marked `[P]` can run in parallel after user stories are complete

---

## Parallel Example: User Story 1

```bash
# Write all eight US1 test/tool files in parallel (T011–T018):
tests/core/decimal_parse_test.cpp                   # T011
tests/core/decimal_format_test.cpp                  # T012
tests/core/decimal_compare_test.cpp                 # T013
tests/core/decimal_roundtrip_property_test.cpp      # T014
bench/core/decimal_bench.cpp                        # T015
tests/fuzz/fuzz_decimal_parse.cpp                   # T016
tests/alloc_guard/decimal_alloc_guard_test.cpp      # T017
tools/check_alloc.py                                # T018

# Then implement sequentially (each builds on the previous):
src/core/decimal.cpp: from_chars                → T019a (verify T011 green)
src/core/decimal.cpp: to_chars                  → T019b (verify T012 green)
src/core/decimal.cpp: compare + predicates      → T020
src/core/decimal.cpp: from_pod + to_pod         → T021
include/fixpp/core/decimal.hpp: parse/format    → T022 (from<U>/to<U> are static_assert stubs)
ctest validation pass                            → T023
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (T001–T005)
2. Complete Phase 2: Foundational (T006–T010) — **CRITICAL**, blocks all compilation
3. Complete Phase 3: US1 (T011–T023; T019a + T019b replace the original T019)
4. **STOP and VALIDATE**: `ctest --preset linux-clang-debug -R '^decimal_(parse|format|compare|roundtrip)_test$'` all green
5. Merge MVP — unblocks wire layer (2b) immediately

### Incremental Delivery

1. Setup + Foundational → compilation baseline
2. US1 (Phase 3) → FLOAT wire-layer parsing unblocked (MVP)
3. US2 (Phase 4) → FLOAT codegen accessors unblocked (2c)
4. US3 (Phase 5) → C/Python consumers unblocked; `_checked` path available
5. US4 (Phase 6) → High-precision alias consumers unblocked; `from<U>`/`to<U>` stubs replaced
6. US5 (Phase 7) → ABI golden locked; CI guards layout
7. Polish (Phase 8) → All 10 seams Tier 1 clean; C-ABI symbol audit passes

---

## Notes

- `[P]` tasks operate on different files with no mutual dependencies — safe to parallelize
- `[Story]` label traces each task to `spec.md §3` user story for Gate B reviewers
- Every test file is written before its implementation (red→green TDD per `[const §VII.3]`)
- Seam numbers match `plan.md §Test seam → file mapping` table (10/10 seams land in this PR per `spec.md §12.3`, Q6 resolved 2026-05-10)
- Bench latency bars (50/30/20 ns) are `spec.md §11 Risk row 1` items — miss by ≤ 2× is a PR note, not a blocker; see 2a §10 Q4
- `decimal_alias_sentinel` lives in `fixpp::detail` (NOT `fixpp::core`) per `data-model.md §Entity 6` note and round-1 finding
- `_checked` siblings (`fixpp_decimal_compare_checked`, `fixpp_decimal_equal_checked`) own AC-C6; bare entry points stay 2a §5.2 verbatim per `research.md D-12`
- `expected_t<T>` is always `std::expected<T, fixpp::core::error>` — never a local `decimal_error` enum (research.md D-8, round-1 rejection)
- T022 leaves `from<U>`/`to<U>` member bodies as `static_assert` stubs so US1 can ship with parse/format only; T039/T040 (US4) replace the stubs with real bodies. This preserves TDD red-green for AC-X1..X3 in T037
- C-ABI symbol-leakage check (`[const §X.2]`) is enforced at Polish via T049b — `nm` audit on `libfixpp_capi.so` rejects any non-`fixpp_*` `T`-class export
- Total task count: **58** (post-/analyze remediation 2026-05-12: T019 → T019a + T019b; new T030b, T033b, T049b, T054)
