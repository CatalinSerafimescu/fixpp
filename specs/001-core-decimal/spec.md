---
id: 001-core-decimal
title: Decimal type — `fixpp::core::decimal<T>` and `fixpp_decimal_t` C-ABI shape
module: core/
phase: 4
status: drafted
verdict: TBD
spec_kit_step: /specify
last_updated: 2026-05-10
owner: fixpp::core (C++); fixpp::capi (C-ABI shape co-owned)
inherits_design: .specify/2a-decimal.md (v0.3, signed off 2026-05-07)
catalogue_rows: W-009 (FLOAT family); FLOAT-typed accessors of A-001..A-034 / M-001..M-012 / P-001..P-008 / C-001..C-003 / R-001..R-005 / N-001..N-003 (Appendix A of 2a)
gate_a_required: yes (touches public C++ API + C ABI)
---

# 001-core-decimal — Decimal type

> **/specify scope.** This spec captures *what* the decimal primitive does and *why*. The full *how* is locked in design doc [`2a-decimal.md`](../../.specify/2a-decimal.md) v0.3 — public API, behavioural contract, error model, integration with adjacent modules, and test seams already converged through Codex Gate A round 2. This document does not re-derive any of that; it carves a Phase 4 feature scope out of 2a so `/clarify`, `/plan`, `/tasks`, and `/implement` have a single starting artifact.

## 1. Summary

Ship the FIX FLOAT representation primitive — `fixpp::core::pod_decimal`, `fixpp::core::decimal<T>`, `fixpp::core::decimal_traits<T>`, the `fixpp_decimal_t` C-ABI struct, and the boundary functions `fixpp_decimal_parse / _format / _compare / _equal / _init` — as the first feature of the `core/` module. This unblocks the wire layer (**2b**), codegen (**2c**), and the C-ABI surface (**2i**), all of which already cite this surface as inherited.

## 2. Why (user value)

Every FLOAT-typed FIX field — Price, Qty, MDEntryPx, NetMoney, AllocPrice, etc. — needs a representation that:
1. Parses lossy-source ASCII decimal bytes into a value with **no allocation** and **no exceptions** on the hot path (`[const §VIII.5]`, `[arch §5.3]`).
2. Compares by **numeric value**, not by encoded representation, so `1`, `1.0`, `1.00` are equal.
3. Crosses the C-ABI boundary as a frozen-layout PoD (`[const §X.3]`) so Python and C consumers see one stable shape.
4. Is **swappable per build** between the default `pod_decimal` and a wider `T` (`decimal128`, `boost::multiprecision`, custom) without forking the library, so consumers in large-notional venues can opt into a wider mantissa without rebuilding business logic.

Without this primitive, the wire parser has no FLOAT-field accessor type and codegen has no FLOAT accessor return type — every downstream feature in `core/`, `wire/`, `dictionary/`, `capi/`, `bindings/python/` is blocked.

## 3. User stories

### 3.1 Engine integrator (in-tree, default traits)

> *As a wire-layer author (**2b**), I want to call `decimal_traits<pod_decimal>::from_chars(span, mr)` on a FLOAT field's bytes and receive a `pod_decimal` value, so I can store it as the typed accessor without leaking parser concerns into the message frame.*

### 3.2 Codegen integrator (**2c**)

> *As the codegen author, I want to emit `fixpp::decimal_t` (the engine-wide alias) as the return type for every FLOAT, PRICE, QTY, AMT, PRICEOFFSET, PERCENTAGE field on a typed message accessor, so the public surface stays one symbol set per build.*

### 3.3 C / Python consumer

> *As a Python user, I want `msg.field('Price')` to return a numeric value I can compare against `Decimal('1.50')` (not a byte string), so I can write trade-validation logic without re-parsing ASCII at the boundary.*

### 3.4 High-precision FX/crypto consumer

> *As a consumer trading at venues that breach `pod_decimal`'s ±9.22 × 10^10 mainstream ceiling at 8 fractional digits, I want to set `-DFIXPP_DECIMAL_T=my::decimal128 -DFIXPP_DECIMAL_USER_HEADER='"my_decimal.hpp"'` at build time and have the entire engine widen accordingly (with the C-ABI staying PoD), so I don't fork the library.*

### 3.5 ABI consumer (Python / C / cross-language bridge)

> *As a binding author, I want `fixpp_decimal_t` to be a frozen 16-byte struct with documented `_reserved[7]` bytes, so I can pin a layout golden in CI and detect breakage on a future minor library bump.*

## Clarifications

### Session 2026-05-10
- Q: Should `mock_decimal_traits<T>` (seam 1) and the Python `Decimal` property oracle (seam 8) ship in this PR or in a follow-up before 2b `/implement`? → A: Same PR — all 10 seams land alongside the primitive.
- Q: How should this feature handle the C-ABI numeric error codes owned by 2i? → A: Allocate provisional values in this PR (`c_api.h`, dated comment); 2i ratifies by re-using the same numeric range.
- Q: Should `fixpp_decimal_compare` / `_equal` defensively validate `exponent ∈ [-38, 0]` on inputs received via the C-ABI (symmetric with AC-S3)? → A: Yes — validate on every C entry; out-of-domain returns `FIXPP_ERR_DECIMAL_INVALID`. C++ `compare` stays `noexcept` and assumes canonical domain.
- Q: Must C/Python consumers always zero `_reserved[7]` before passing `fixpp_decimal_t` into the library? → A: Recommended, not required. Doc-comment in `c_api.h` strongly recommends zero-init for forward-compat; library tolerates non-zero in v1.0 (AC-A4). Any future v1.x meaning for `_reserved` is opt-in via a new function, not a silent semantic change.
- Q: When `T==U` and `T` is wider than PoD, should `decimal<T>::to<U>()` still funnel through `pod_decimal` (per AC-X1) or short-circuit? → A: Compile-time short-circuit via `if constexpr (std::is_same_v<T,U>)` — return source unchanged, no funnel, no error. AC-X1's funnel applies only to `T≠U`.

## 4. Functional acceptance criteria

Lifted from 2a §6 and §5; one bullet per testable property. Tests cover each.

### 4.1 Parse — `from_chars` / `fixpp_decimal_parse`
- **AC-P1.** Empty input → `decimal_invalid_input`.
- **AC-P2.** `+0`, `-0` → value `0`, canonical `{0, 0}`.
- **AC-P3.** `.5` and `5.` → `decimal_invalid_input` (no integer or no fractional digit).
- **AC-P4.** Any non-digit / non-dot / non-sign byte (incl. embedded SOH `\x01`) → `decimal_invalid_input`.
- **AC-P5.** `00005` → value `5`; trailing zeros in fractional part preserved in `exponent` (`5.500` → `{5500, -3}`).
- **AC-P6.** Mantissa overflow of `int64_t` → `decimal_overflow`.
- **AC-P7.** Required `exponent < -38` → `decimal_overflow`.
- **AC-P8.** Result `mantissa == INT64_MIN` (sentinel collision) → `decimal_overflow`.
- **AC-P9.** Single-pass, no allocation, no exception (asserted by allocation guard in §6).
- **AC-P10.** On failure, `out` is unmodified (caller checks return).

### 4.2 Serialize — `to_chars` / `fixpp_decimal_format`
- **AC-S1.** `mantissa == INT64_MIN` (sentinel) → `decimal_invalid_input`.
- **AC-S2.** `mantissa == 0` → `"0"` (one byte) regardless of exponent.
- **AC-S3.** `exponent ∈ [-38, 0]` precondition; outside → `decimal_invalid_input` *before* formatting (two-sided check; protects against malicious / buggy C caller writing a negative exponent < -38 into `int8_t`).
- **AC-S4.** Trailing-zero stripping in fractional segment (canonical output).
- **AC-S5.** Worst-case `max_serialized_bytes = 41` for `pod_decimal` (`-0.<19 zeros><19 mantissa digits>` form).
- **AC-S6.** `dst` too small → `decimal_buffer_too_small` / `FIXPP_ERR_BUFFER_TOO_SMALL`.

### 4.3 Compare / equal — `compare` / `fixpp_decimal_compare` / `fixpp_decimal_equal`
- **AC-C1.** `{1, 0}`, `{10, -1}`, `{100, -2}` all compare equal (value equality, not field equality).
- **AC-C2.** `pod_decimal_invalid` orders strictly greater than every finite value, equal only to itself.
- **AC-C3.** Algorithm runs without `__int128` and without overflow (digit-string compare per 2a §6.3).
- **AC-C4.** O(digits) ≤ 19 iterations; no allocation; `noexcept`.
- **AC-C5.** Property test: arbitrary-precision oracle (Python `Decimal`) agrees on all generated pairs in canonical domain (Tier 1 gate).
- **AC-C6.** C-ABI entry points `fixpp_decimal_compare` / `fixpp_decimal_equal` validate `exponent ∈ [-38, 0]` on **every** input (symmetric with AC-S3); out-of-domain returns `FIXPP_ERR_DECIMAL_INVALID`. C++ `compare` remains `noexcept` and assumes canonical domain (callers from inside the engine produce canonical values by construction).

### 4.4 Cross-traits conversion — `decimal<T>::to<U>()`
- **AC-X1.** For `T ≠ U`, `decimal<T>` → `decimal<U>` funnels through `pod_decimal` (canonical interchange form).
- **AC-X2.** When the funnel runs (T ≠ U) and the source value is outside the PoD `int64 × 10^[-38..0]` domain → `decimal_precision_loss` (no silent truncation).
- **AC-X3.** For `T == U`, `to<U>()` is a compile-time short-circuit via `if constexpr (std::is_same_v<T,U>)` and returns the source unchanged — no funnel, no error, no codegen overhead. Honors round-trip identity unconditionally, including for source values outside the PoD domain.

### 4.5 C-ABI layout
- **AC-A1.** `sizeof(fixpp_decimal_t) == 16`, `alignof == 8`.
- **AC-A2.** Field offsets: `mantissa` at 0, `exponent` at 8, `_reserved[7]` at 9 (`static_assert`s in `src/capi/decimal_assert.cpp`).
- **AC-A3.** `is_standard_layout_v<fixpp_decimal_t>`.
- **AC-A4.** `_reserved` ignored on read in v1.0; consumer may leave it uninitialized without breaking parsing (regression guard for "ignore on read" rule).
- **AC-A5.** `FIXPP_DECIMAL_INITIALIZER` and `fixpp_decimal_init()` zero-init `_reserved` for forward-compat consumers.
- **AC-A5b.** Writer contract: zero-init of `_reserved` is **recommended, not required**. The `c_api.h` doc-comment strongly recommends consumers use `FIXPP_DECIMAL_INITIALIZER` / `fixpp_decimal_init()`; the library tolerates non-zero `_reserved` in v1.0 (per AC-A4). Any future semantic for `_reserved` ships as a new explicit API, never a silent meaning change for existing consumers.
- **AC-A6.** Tier 2 `abidiff` golden snapshot under `tests/abi/golden/` against tagged ABI release.

### 4.6 Build-time alias — `FIXPP_DECIMAL_T`
- **AC-B1.** Default `FIXPP_DECIMAL_T == ::fixpp::core::pod_decimal`; `fixpp::decimal_t == decimal<pod_decimal>`.
- **AC-B2.** Consumer-supplied `FIXPP_DECIMAL_USER_HEADER` is included before the alias macro is consulted.
- **AC-B3.** Link-time mismatch test: two TUs built with conflicting `FIXPP_DECIMAL_T` → unresolved-symbol link error via `decimal_alias_sentinel<T>::tag`.
- **AC-B4.** Switching the alias does **not** change `fixpp_decimal_t` C-ABI shape.

### 4.7 Error model
- C++ codes: `decimal_invalid_input`, `decimal_overflow`, `decimal_precision_loss`, `decimal_buffer_too_small`.
- C-ABI mapping (`[const §X.4]`): `FIXPP_ERR_DECIMAL_INVALID` (data), `FIXPP_ERR_DECIMAL_PRECISION_LOSS` (semantic), `FIXPP_ERR_BUFFER_TOO_SMALL` (reused generic).
- **Numeric-range allocation:** this feature's PR defines provisional numeric values for `FIXPP_ERR_DECIMAL_INVALID` / `_PRECISION_LOSS` in `include/fix/c_api.h`, marked with a dated `// allocated 2026-05-10, owned by 2i` comment. **2i** ratifies by re-using the same numeric range in its own PR; any change after ratification is a Tier 2 ABI breakage.

## 5. Out of scope

Carried verbatim from 2a §2 — re-stated here so `/clarify` does not reopen.

- **No general-purpose arithmetic** on `decimal<T>` (no `+ - * /`). Trait specializations may layer arithmetic on top; the public surface does not.
- **No locale-aware formatting** (no thousands separators, no exponent notation, no per-locale decimal marks).
- **No silent precision loss** — lossy conversions report `decimal_precision_loss`.
- **No runtime polymorphism** — `decimal_traits<T>` is a compile-time customization point only.
- **No direct `T → U` cross-traits path** in v1.0 (always funnels through `pod_decimal`).
- **No built-in `decimal_traits<__int128>`** in v1.0 (deferred per 2a §10 Q2).
- **No replay-decimal-trait coupling** — replay re-emits raw FIX frames, decimal trait fidelity is irrelevant. (Typed-payload persistence sinks gate on `is_lossless_for_fix_float` per 2a §7.1; that gate lives at **2e** / **2j**, not here.)

## 6. Non-functional requirements

Lifted from 2a §6.5, `[const §VIII.5]`, `[const §IX.4]`, and the project quality gate.

| NFR | Target | Test seam | Tier |
|---|---|---|---|
| Allocation on hot path | Zero between parse and `fromApp` | `tools/check_alloc.py` + `mallocnesia` interceptor (Linux) | 1 (Linux); 2 deferred (Windows) |
| Exceptions on hot path | None between parse and `fromApp` | static (`noexcept` declarations) + ASan/UBSan run | 1 |
| Parse latency (default traits, x86_64, 5-digit mantissa, warm cache) | ≤ 50 ns median | Google Benchmark regression bar | 1 |
| Format latency (same workload) | ≤ 30 ns median | Google Benchmark | 1 |
| Compare latency (same workload) | ≤ 20 ns median | Google Benchmark | 1 |
| Coverage (lines / branches) | ≥ 90 % / ≥ 80 % on touched files | `linux-clang-coverage` preset | 1 |
| Sanitizers | ASan + UBSan + TSan clean | Tier 1 matrix | 1 |
| Fuzz harness | `tests/fuzz/fuzz_decimal_parse.cpp` runs ≥ 10 min in CI, longer nightly | libFuzzer | 1 (smoke); 2 (long) |
| clang-tidy / IWYU | clean | pre-commit + Tier 1 | 1 |
| Property oracle (Python `Decimal`) | Agrees on `compare(a,b)` for all generated pairs | dedicated test | 1 (promoted from 2 per 2a §9 seam 8) |

## 7. Files in scope

| Path | Owner | Notes |
|---|---|---|
| `include/fixpp/core/decimal.hpp` | this feature | `pod_decimal`, `decimal<T>`, `decimal_traits<T>` declarations + sentinel constants |
| `include/fixpp/core/decimal_alias.hpp` | this feature | `FIXPP_DECIMAL_T` macro plumbing, alias sentinel |
| `include/fixpp/core/decimal_helpers.hpp` | this feature | `detail::trap_throw` helper for trait authors wrapping throwing 3p libraries |
| `include/fix/c_api.h` | co-owned with **2i** | `fixpp_decimal_t` struct, `FIXPP_DECIMAL_INITIALIZER`, `FIXPP_DECIMAL_INVALID`, boundary fn declarations |
| `src/core/decimal.cpp` | this feature | `pod_decimal` traits implementation (parse / format / compare / canonicalize), alias sentinel definition |
| `src/capi/decimal.cpp` | co-owned with **2i** | `fixpp_decimal_*` boundary fn implementations |
| `src/capi/decimal_assert.cpp` | this feature | layout `static_assert`s |
| `tests/core/decimal_*.cpp` | this feature | unit tests per AC bullets in §4 |
| `tests/abi/golden/fixpp_decimal_t.abidiff` | this feature | Tier 2 ABI golden |
| `tests/fuzz/fuzz_decimal_parse.cpp` | this feature | libFuzzer harness |
| `tests/support/mock_decimal_traits.hpp` | this feature | parameterizable failing-traits helper for downstream tests |
| `bench/core/decimal_bench.cpp` | this feature | Google Benchmark harness for §6 latency NFRs |

`include/fix/c_api/` and `src/capi/` already exist as skeletons from Phase 3; this feature populates the decimal-shaped slice only. Other C-ABI surface remains owned by **2i**.

## 8. Inheritance / dependencies

- **Inherits from:** `[const §X.1, X.2, X.3, X.4, VIII.5, IX.4, IX.5, XVII.1]`, `[arch §4.1, §4.10, §5.3, §5.5, §6, §10]`, `[SYN §3.1 Q5]`. All cited inline in 2a Appendix B.
- **Blocks:** **2b** (wire FLOAT-field parser/serializer), **2c** (codegen FLOAT-typed accessors), **2i** (C-ABI accessor `fixpp_msg_field_decimal`).
- **Does not block:** session FSM, message store (decimal trait is parser/codegen-internal once landed), TLS, transport.
- **Catalogue impact:** none. 2a explicitly does not add a new row (§11) — W-009 covers the FLOAT family; per-message FLOAT accessors are inherited.

## 9. Test seams (carried from 2a §9)

Implementation MUST expose all 10 seams:

1. `mock_decimal_traits<T>` — header-only failing-traits for downstream tests (`tests/support/`).
2. Round-trip property tests — 10⁴ generated samples + `[FIX50SP2 §3.3]` example table.
3. Cross-traits round-trip — value-preserving identity for in-domain values; `decimal_precision_loss` for out-of-domain.
4. C-ABI layout golden — `abidiff` snapshot (Tier 2 hard-fail).
5. Latency regression — `parse / format / compare` Google Benchmark bars.
6. Allocation guard (Linux) — `mallocnesia` interceptor on the parse-format loop.
7. Fuzzer (libFuzzer) — `fuzz_decimal_parse.cpp` against ASan + UBSan invariants.
8. Property oracle (Python `Decimal`) — Tier 1 promoted; gates `compare`.
9. `FIXPP_DECIMAL_T` link-time mismatch — two-TU build expected to fail link with sentinel-symbol unresolved.
10. `_reserved` byte tolerance — C consumer with garbage `_reserved` parses correctly (regression guard for "ignore on read").

## 10. Open questions (carried from 2a §10 + new for /clarify)

| # | Question | Disposition |
|---|---|---|
| Q1 | Direct `T → U` cross-traits seam (skipping PoD interchange). | DEFERRED per 2a §10 Q1 — not in v1.0. |
| Q2 | Built-in `decimal_traits<__int128>` specialization. | DEFERRED per 2a §10 Q2 — not in v1.0. |
| Q3 | Confirm with **2e** that MessageStore records raw FIX frames (replay is decimal-trait-agnostic) and the typed-payload-persistence `static_assert(is_lossless_for_fix_float)` lives at the persistence-write site. | Confirm at **2e**; if inverted, reopen 2a §7.1 *and* this spec's §5 "out of scope" line on replay. |
| Q4 | Per-architecture latency ceilings (50 / 30 / 20 ns) — right Tier 1 bars or stricter? | Bench spike during `/implement`; revise after first wire-layer integration. |
| Q5 *(new)* | C-ABI numeric error-code values for `FIXPP_ERR_DECIMAL_INVALID` / `_PRECISION_LOSS`. | RESOLVED 2026-05-10 (`/clarify`) — provisional values defined in this PR's `c_api.h` with dated comment; **2i** ratifies by re-using the same numeric range. |
| Q6 *(new)* | First-feature scope: do we ship `mock_decimal_traits<T>` and the Python `Decimal` oracle in this PR, or in a follow-up before **2b** `/implement` lands? | RESOLVED 2026-05-10 (`/clarify`) — **same PR**. Both seams land with the primitive. |

## 11. Risk register

| Risk | Severity | Mitigation |
|---|---|---|
| Latency NFR (50 / 30 / 20 ns) too tight for first implementation pass | M | Land impl + bench together; if NFR misses by ≤ 2×, treat as TODO not blocker (2a §10 Q4 explicitly allows revision). |
| `_reserved` byte tolerance regression (consumer code that breaks on a non-zero `_reserved`) | L | AC-A4 + AC-A5 + dedicated test; documented in `c_api.h` doc comment. |
| ABI golden churn during early development | M | Lock golden once first `/implement` lands; treat post-lock changes as Tier 2 hard-fail per 2a §9 seam 4. |
| Trait-amplification symbol bloat from `decimal<T>` template | L | Mitigated by "one alias per build" (AC-B3 link-time guard); link error caught immediately, not at runtime. |
| Cross-traits `T → U` narrowing surprises a wider-than-PoD-to-wider-than-PoD consumer | L | Documented in §5 + 2a §6.4; deferred per Q1; error code `decimal_precision_loss` is loud, not silent. |

## 12. Definition of done

This feature is `merged` (per phase-4 state legend) when:

1. All ACs in §4 pass under Tier 1 CI (Linux/Clang Debug + Release + ASan + UBSan + TSan + Coverage; Linux/GCC Release sanity).
2. All NFRs in §6 met or formally waived in the PR description with rationale.
3. All 10 test seams in §9 land in this PR (Q6 resolved — see Clarifications 2026-05-10).
4. Codex Gate A on this `/specify` + the resulting `/plan` returns SHIP-AS-IS or SHIP-WITH-FIXES (all P0/P1 resolved).
5. Codex Gate B on the implementation PR returns SHIP-AS-IS or SHIP-WITH-FIXES (all P0/P1 resolved).
6. Opus review pass.
7. User sign-off in `phases/phase-4.md` Track Log.
8. Library submodule bumped in parent repo with the merge commit.

## 13. References

- Design doc: [`.specify/2a-decimal.md`](../../.specify/2a-decimal.md) v0.3 (2026-05-07).
- Constitution: [`.specify/constitution.md`](../../.specify/constitution.md) — §VIII.5, §IX.4, §IX.5, §X.1–X.4, §XVII.1.
- Architecture: [`.specify/architecture.md`](../../.specify/architecture.md) — §4.1, §4.10, §5.3, §5.5, §6, §10.
- Catalogue: [`spec/feature-catalogue.md`](../../spec/feature-catalogue.md) — W-009; A/M/P/C/R/N FLOAT-field accessors per 2a Appendix A.
- Phase doc: [`phases/phase-4.md`](../../../phases/phase-4.md) — pipeline, Track Log, sub-file convention.
- Codex review procedure: [`.specify/codex-review.md`](../../.specify/codex-review.md).
