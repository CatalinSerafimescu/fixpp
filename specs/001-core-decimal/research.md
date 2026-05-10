# research.md — 001-core-decimal

> **Phase 0 output for `/speckit-plan`.** The research record for this feature was largely produced during the design-doc round (`.specify/2a-decimal.md` v0.3, Gate A round-2 converged 2026-05-07). This file records *one decision per topic*, points back at the design-doc section that holds the full evidence, and tracks the disposition of every NEEDS-CLARIFICATION-class question.

**No outstanding NEEDS CLARIFICATION items remain.** All items below are either RESOLVED (with cite) or DEFERRED (with cite + rationale).

---

## R1 — Default representation: `pod_decimal { int64 mantissa, int8 exponent }`

- **Decision.** Default decimal is `(int64_t mantissa, int8_t exponent)`, canonical exponent domain `[-38, 0]`, sentinel `pod_decimal_invalid = {INT64_MIN, 0}`.
- **Rationale.** (i) Covers the FX/equities mainstream price domain (≈ ±9.22 × 10¹⁰ at 8 fractional digits) — sufficient for retail and most institutional flow per `2a §1.1`; (ii) frozen 16-byte `fixpp_decimal_t` is cross-compiler-stable and meets `[const §X §3]` PoD-at-the-boundary; (iii) `int8_t` exponent (vs `uint8_t`) keeps room for both negative-exponent fractions and the canonical-domain sentinel-collision check (AC-P8 / AC-S1).
- **Alternatives considered.**
  - `decimal128` everywhere — rejected as default (too wide, slower compare); kept available as opt-in trait via `FIXPP_DECIMAL_T` (R7).
  - `double` — rejected: not value-lossless against FIX FLOAT bytes (`2a §2`).
  - `string` view of bytes — rejected: defeats numeric `compare`; downstream typed accessors can't reason about value equality.
  - `int128 × scale` — rejected: `__int128` is not standard, breaks Windows MSVC.
- **Source.** `2a §1.1`, `2a §3`, `2a §4.1`, `[const §X §3]`.

## R2 — Compare algorithm: digit-string compare, no `__int128`

- **Decision.** `pod_decimal::compare` uses a digit-string comparison that walks the canonicalized mantissas without ever requiring 128-bit arithmetic. O(digits) ≤ 19 iterations, `noexcept`, zero allocation.
- **Rationale.** Codex Gate A round-1 finding: a naïve `mantissa × 10^exponent` cross-multiply needs `__int128` to avoid overflow when comparing `(INT64_MAX, 0)` against `(small, -38)`. `__int128` is non-standard and absent on MSVC. Digit-string compare side-steps the entire 128-bit dependency while still meeting AC-C5 (Python `Decimal` oracle agrees on every generated pair in the canonical domain).
- **Alternatives considered.**
  - `__int128` cross-multiply — rejected per above.
  - Convert to `double` then compare — rejected: lossy for large mantissas, would fail the property oracle.
  - Promote both to a runtime big-int — rejected: allocates on the hot path, violates `[const §VIII §5]`.
- **Source.** `2a §6.3`, `spec §4.3` (AC-C1..C6), Codex Gate A round-1 review.

## R3 — Cross-traits conversion: funnel through `pod_decimal`

- **Decision.** `decimal<T>::to<U>()` for `T ≠ U` always funnels through `pod_decimal` as canonical interchange form. For `T == U` the path is a compile-time short-circuit via `if constexpr (std::is_same_v<T,U>)` returning the source unchanged (clarification 2026-05-10).
- **Rationale.** (i) Avoids quadratic `T → U` trait-pair overload expansion; (ii) makes the C-ABI shape always reachable from any trait; (iii) the short-circuit honors round-trip identity unconditionally, including for `T` values outside the PoD domain (see clarification Q on AC-X3).
- **Alternatives considered.**
  - Direct `T → U` per-pair overloads — rejected (`2a §10 Q1`, deferred for v1.0; reopen if a real consumer hits it).
  - Always-funnel even for `T == U` — rejected by clarification 2026-05-10: introduces an unnecessary `decimal_precision_loss` for in-domain `T == U` calls, breaks AC-X3's unconditional round-trip identity.
- **Source.** `2a §6.4`, `spec §4.4` (AC-X1..X3), `spec §Clarifications 2026-05-10` Q5.

## R4 — Error model: 4 C++ codes / 3 C-ABI codes

- **Decision.**
  - **C++ side:** `decimal_invalid_input`, `decimal_overflow`, `decimal_precision_loss`, `decimal_buffer_too_small`.
  - **C-ABI side:** `FIXPP_ERR_DECIMAL_INVALID` (data validity), `FIXPP_ERR_DECIMAL_PRECISION_LOSS` (semantic / cross-traits narrowing), `FIXPP_ERR_BUFFER_TOO_SMALL` (reused generic).
  - **Numeric-range allocation:** this PR defines provisional numeric values for `FIXPP_ERR_DECIMAL_INVALID` / `_PRECISION_LOSS` in `include/fix/c_api.h`, marked with a dated `// allocated 2026-05-10, owned by 2i` comment. **2i** ratifies by re-using the same numeric range; any change after ratification is a Tier 2 ABI breakage per `[const §X §4]`.
- **Rationale.** Compact code count (Codex Gate A round 1 reduced from 6 to 4 C++ codes); C-ABI mapping per `[const §X §4]`. Provisional-then-ratify pattern keeps **2i** as the C-ABI owner of record while letting this feature land first without a forward-reference deadlock.
- **Alternatives considered.**
  - 6 C++ codes (one per AC failure shape) — rejected as over-decomposed.
  - Defer C-ABI numeric block until **2i** lands — rejected: leaves this PR's C-ABI symbols incomplete, creates a circular dependency.
- **Source.** `2a §7`, `spec §4.7`, `spec §Clarifications 2026-05-10` Q (C-ABI error codes).

## R5 — `_reserved[7]` byte: zero-init recommended, not required

- **Decision.** Writers (Python / C consumers) are *recommended* to zero-init `_reserved[7]` via `FIXPP_DECIMAL_INITIALIZER` or `fixpp_decimal_init()`, but the library *tolerates* non-zero `_reserved` in v1.0 (AC-A4). Any future v1.x semantic for `_reserved` ships as a new explicit API, never a silent meaning change.
- **Rationale.** Forward-compat without a hard-fail trap for consumers writing the struct without macros. Test seam #10 (`tests/capi/decimal_reserved_test.cpp`) regresses against accidental tightening.
- **Alternatives considered.**
  - Hard-require zero `_reserved` on every read — rejected by clarification 2026-05-10: brittle across Python users that build the struct field-by-field.
  - Reserve `_reserved` for a specific v1.0 meaning — rejected: nothing is needed in v1.0; the bytes exist purely for forward layout stability.
- **Source.** `2a §5.1`, `spec §4.5` (AC-A1..A6, AC-A5b), `spec §Clarifications 2026-05-10` Q (reserved bytes).

## R6 — C-ABI input validation symmetry: `compare` / `equal` validate `exponent ∈ [-38, 0]`

- **Decision.** C-ABI entry points `fixpp_decimal_compare` and `fixpp_decimal_equal` validate `exponent ∈ [-38, 0]` on **every** input (symmetric with AC-S3); out-of-domain inputs return `FIXPP_ERR_DECIMAL_INVALID`. The C++ `compare` member stays `noexcept` and assumes canonical-domain inputs (callers from inside the engine produce canonical values by construction).
- **Rationale.** Defense in depth at the C-ABI boundary: a malicious or buggy C caller could write a negative exponent below `-38` into `int8_t`, and the engine should not assume the contract holds across an FFI boundary. The C++ side's `noexcept` `compare` is internal-only and protected by the parse step's canonicalization.
- **Alternatives considered.**
  - C++ `compare` also validates — rejected: doubles the cost of an in-engine compare for no real-world benefit (caller already canonicalized via parse).
  - C-ABI `compare` UB on bad input — rejected: violates `[const §X §4]` "no undocumented UB at the boundary".
- **Source.** `2a §6.3`, `spec §4.3` (AC-C6), `spec §Clarifications 2026-05-10` Q (C-ABI exponent validation).

## R7 — Build-time alias: `FIXPP_DECIMAL_T` + link-time sentinel

- **Decision.** Engine-wide alias is `fixpp::decimal_t = decimal<FIXPP_DECIMAL_T>`. Default `FIXPP_DECIMAL_T = ::fixpp::core::pod_decimal`. Consumer-supplied `FIXPP_DECIMAL_USER_HEADER` is `#include`d before the alias macro is consulted. A `decimal_alias_sentinel<T>::tag` symbol is defined in `src/core/decimal.cpp` for the chosen `T` only — two TUs built with conflicting `FIXPP_DECIMAL_T` link with one `tag` definition each, which triggers a multiply-defined-symbol link error (or, for header-only mismatches, an unresolved-symbol error).
- **Rationale.** Keeps trait amplification at *one* symbol set per build, satisfying `[const §IV §1]` "primary distribution is in-process C++23 library" and avoiding template bloat. The sentinel-symbol guard catches mismatches at link time, not runtime.
- **Alternatives considered.**
  - Per-trait template instantiation, no link-time guard — rejected: mismatched TUs would compile and link, then ODR-violate at runtime.
  - Runtime check at engine startup — rejected: too late, and not detectable at unit-test time.
- **Source.** `2a §4.4`, `spec §4.6` (AC-B1..B4).

## R8 — Test seams: ship all 10 in this PR

- **Decision.** All 10 test seams from `2a §9` / `spec §9` ship with the implementation in this PR. Specifically: (1) `mock_decimal_traits<T>`, (2) round-trip property tests, (3) cross-traits round-trip, (4) C-ABI layout golden, (5) latency regression bench, (6) allocation guard (Linux), (7) libFuzzer parse harness, (8) Python `Decimal` oracle, (9) `FIXPP_DECIMAL_T` link-time mismatch test, (10) `_reserved` byte tolerance.
- **Rationale.** Clarification 2026-05-10 Q6 resolved this. Splitting seams across feature PRs would let **2b** `/implement` start without the support seams in place — a known TDD smell where the integration site has no fixture.
- **Alternatives considered.** Two-PR split (primitive first, seams in a follow-up before **2b**) — rejected by clarification.
- **Source.** `2a §9`, `spec §9`, `spec §Clarifications 2026-05-10` Q6.

## R9 — Tooling pinned

- **Decision.**
  - GoogleTest 1.17.0 (Conan-pinned), Google Benchmark 1.9.5 (Conan-pinned) — both already in `conanfile.py` per Phase 3 close.
  - libFuzzer ships with Clang 22 (the project toolchain, `[const §XVII §7]`); no external dep.
  - `mallocnesia` interceptor — Linux-only, Tier 1; symbol-scoped to `fixpp::*` / `fixpp_*`.
  - `abidiff` (libabigail) — Tier 2 only; ABI golden lives at `tests/abi/golden/fixpp_decimal_t.abidiff`.
  - Python `Decimal` oracle — CPython 3.10 (matches the `manylinux_2_28_x86_64 cp310` wheel target per `[const §IV §3]`).
- **Rationale.** Reuses Phase 3 toolchain decisions; no new third-party dep introduced for this feature. Constitution Article III §2 (Conan-only) holds.
- **Source.** `[const §II §2]`, `[const §III §2]`, `[const §IV §3]`, `[const §IX §1]`, `[const §XVII §7]`.

## R10 — Open questions disposition

| # | Question | Status | Source |
|---|---|---|---|
| Q1 | Direct `T → U` cross-traits seam (skipping PoD interchange) | DEFERRED to v1.x — not in v1.0 | `2a §10 Q1`, `spec §10` |
| Q2 | Built-in `decimal_traits<__int128>` specialization | DEFERRED to v1.x — not in v1.0 | `2a §10 Q2`, `spec §10` |
| Q3 | Confirm with **2e** that MessageStore records raw FIX frames (replay is decimal-trait-agnostic) | CONFIRM at **2e**; if inverted, reopen 2a §7.1 + spec §5 | `2a §10 Q3`, `spec §10` |
| Q4 | Per-architecture latency ceilings (50 / 30 / 20 ns) — right Tier 1 bars or stricter? | BENCH SPIKE during `/implement`; revise after first wire-layer integration | `2a §10 Q4`, `spec §10` |
| Q5 | C-ABI numeric error-code values | RESOLVED 2026-05-10 — provisional in this PR, ratified by **2i** | `spec §Clarifications 2026-05-10` |
| Q6 | First-feature scope: ship `mock_decimal_traits<T>` + Python oracle in this PR? | RESOLVED 2026-05-10 — same PR | `spec §Clarifications 2026-05-10` |

No NEEDS CLARIFICATION items remain for `/plan`. Q3 / Q4 are flagged for downstream review (at **2e** integration and during `/implement` bench spike respectively); they do not block Gate A or `/tasks`.

## Summary

The decimal feature's research record is the convergence trail of `2a-decimal.md` v0.3 (Codex Gate A round 2 closed 2026-05-07) plus the 5 clarifications captured in `spec.md §Clarifications — Session 2026-05-10`. No new decisions are introduced at `/plan` time. The plan picks Conan-pinned tooling already in tree and routes the feature through the existing Phase 3 module skeleton.
