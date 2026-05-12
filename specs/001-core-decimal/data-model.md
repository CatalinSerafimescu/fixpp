---
id: 001-core-decimal
title: Data Model — Decimal entities, invariants, error mapping
spec_kit_step: /plan Phase 1
last_updated: 2026-05-12
status: drafted (round-2 redraft)
---

# Data Model — 001-core-decimal

All entities and their invariants are inherited from `.specify/2a-decimal.md` v0.3. This document records each entity's fields, validation rules, and state transitions in the canonical `/plan` Phase 1 format. No new entity is introduced at `/plan` time.

## Entity 1 — `fixpp::core::pod_decimal` (C++ POD payload)

**Header:** `include/fixpp/core/decimal.hpp` (extract source: `.specify/2a-decimal.md` v0.3 §4.1)

**Fields:**

| Field | Type | Domain | Notes |
|---|---|---|---|
| `mantissa` | `std::int64_t` | `[INT64_MIN + 1, INT64_MAX]` for finite values; `INT64_MIN` reserved for invalid sentinel | Significand of `mantissa × 10^exponent`. |
| `exponent` | `std::int8_t` | `[-38, 0]` for finite values (canonical domain) | Power of 10. Negative exponent = fractional digits. `int8_t` admits `[-128, 127]`; values outside `[-38, 0]` are rejected at the C-ABI boundary (AC-S3 via `_format`; AC-C6 via the `_checked` siblings per research.md D-12 resolved 2026-05-12). |

**Invariants:**

- **No `operator==` / `operator<=>` declared.** Field equality (which would say `{1,0} != {10,-1}`) silently contradicts value equality. Comparison goes through `decimal<pod_decimal>` (Entity 3), which delegates to `decimal_traits<pod_decimal>::compare` (canonicalizing per 2a §6.3).
- **`pod_decimal_invalid` constant** = `{INT64_MIN, 0}` is the invalid sentinel. Total ordering by `compare`: `pod_decimal_invalid` is strictly greater than every finite value, equal only to itself (2a §6.3 step 0).
- **Default-constructed `pod_decimal{}`** has `mantissa = 0, exponent = 0` — the value `0` (canonical zero), NOT the invalid sentinel. The sentinel is produced only by explicit construction, parse failure, or canonicalization overflow.

**State transitions:** none — `pod_decimal` is an immutable value type. Transitions happen at the **value** layer (parse/format/compare/canonicalize), not at the struct layer.

## Entity 2 — `fixpp::core::decimal_traits<T>` (compile-time customization point)

**Header:** `include/fixpp/core/decimal.hpp` (extract source: `.specify/2a-decimal.md` v0.3 §4.2)

`decimal_traits<T>` is a primary template with no general definition; users specialize per `T`. The library ships exactly one specialization in v1.0: `decimal_traits<pod_decimal>` (in `src/core/decimal.cpp`).

**Required member types and constants:**

| Member | Type | Notes |
|---|---|---|
| `value_type` | `T` | Representation type. |
| `is_lossless_for_fix_float` | `static constexpr bool` | Trait author's promise: every FIX-valid input round-trips through `to_chars` to a byte sequence whose `from_chars` re-parse compares value-equal under `compare` (2a §6.3). `pod_decimal` declares `true`. |
| `max_serialized_bytes` | `static constexpr std::size_t` | Upper bound on `to_chars` output length. `pod_decimal` declares `41`. |

**Required static member functions (all `noexcept`):**

| Function | Signature | Notes |
|---|---|---|
| `from_chars` | `static expected_t<T> from_chars(std::span<const std::byte> src, std::pmr::memory_resource* mr) noexcept;` | Parse FIX FLOAT bytes. `mr` is a required non-null parameter; non-allocating traits ignore it (D-6). |
| `to_chars` | `static expected_t<std::size_t> to_chars(T const& v, std::span<std::byte> dst) noexcept;` | Serialize. Returns count of bytes written. |
| `from_pod` | `static expected_t<T> from_pod(pod_decimal) noexcept;` | Convert from canonical PoD form. |
| `to_pod` | `static expected_t<pod_decimal> to_pod(T const&) noexcept;` | Convert to canonical PoD form. Out-of-domain → `error::decimal_overflow`. |
| `compare` | `static std::strong_ordering compare(T const&, T const&) noexcept;` | Total ordering by value. |
| `is_finite` | `static bool is_finite(T const&) noexcept;` | Domain predicate. |
| `is_zero` | `static bool is_zero(T const&) noexcept;` | Domain predicate. |
| `is_negative` | `static bool is_negative(T const&) noexcept;` | Domain predicate. |

**Invariants:**

- **PMR required** on `from_chars` (not optional; not overloaded). The wire layer always passes the per-message arena `[arch §5.2]`.
- **No `equal` member** — `decimal<T>::operator==` calls `compare(...) == 0` (avoids two-source-of-truth bug where `equal` and `compare` could be inconsistent).
- **`noexcept` is a contract requirement** — traits wrapping throwing third-party libraries (e.g., `boost::multiprecision`) MUST trap via `fixpp::core::detail::trap_throw(...)` (helper in `include/fixpp/core/decimal_helpers.hpp`).
- **Forward-compat:** new required members may only be added in a minor library version under a feature-test macro `FIXPP_DECIMAL_TRAITS_FEATURE_<NAME>`; existing specializations stay compiling.

## Entity 3 — `fixpp::core::decimal<T>` (value-typed wrapper)

**Header:** `include/fixpp/core/decimal.hpp` (extract source: `.specify/2a-decimal.md` v0.3 §4.3)

**Fields:**

| Field | Type | Visibility | Notes |
|---|---|---|---|
| `value_` | `T` | `private` | The underlying representation (e.g., `pod_decimal`). Default-initialized via `T{}` for the default ctor. |

**Public surface (all 7 normative members + `decimal_default` alias preserved per 2a §4.3 — see research.md D-13):**

| Member | Signature | Notes |
|---|---|---|
| Default ctor | `constexpr decimal() noexcept = default;` | `value_` default-initializes to `T{}` (zero for `pod_decimal`). |
| Value ctor | `constexpr explicit decimal(T v) noexcept;` | Wraps an existing `T`. |
| `value()` | `constexpr T const& value() const noexcept;` | Const-ref access to the wrapped representation. |
| `parse` | `static expected_t<decimal> parse(std::span<const std::byte> src, std::pmr::memory_resource* mr) noexcept;` | Thin shell over `decimal_traits<T>::from_chars`. |
| `format` | `expected_t<std::size_t> format(std::span<std::byte> dst) const noexcept;` | Thin shell over `decimal_traits<T>::to_chars`. |
| `from<U>` | `template<class U> static expected_t<decimal> from(decimal<U> const&) noexcept;` | Cross-traits funnel through PoD. |
| `to<U>` | `template<class U> expected_t<decimal<U>> to() const noexcept;` | Cross-traits funnel through PoD. **For `T == U`, the implementation short-circuits via `if constexpr` (returns the source unchanged, no funnel, no error)** — see research.md D-11. The return-type shape is uniform `expected_t<decimal<U>>` regardless. |
| `operator==` | `friend bool operator==(decimal const& a, decimal const& b) noexcept { return traits_type::compare(a.value_, b.value_) == 0; }` | Value equality via `compare`. |
| `operator<=>` | `friend std::strong_ordering operator<=>(decimal const& a, decimal const& b) noexcept { return traits_type::compare(a.value_, b.value_); }` | Three-way value compare. |

**Namespace-scope alias** (preserved from 2a §4.3 line 169):

```cpp
using decimal_default = decimal<pod_decimal>;
```

**Invariants:**

- **Eager parse:** `decimal<T>::parse` consumes the input span; the resulting `decimal<T>` does **not** alias the input buffer. Callers may free or reuse the source bytes immediately.
- **No arithmetic.** `decimal<T>` has no `+ - * /` operators. Users who need arithmetic specialize traits to a type that provides it (e.g., `boost::multiprecision::cpp_dec_float`).

## Entity 4 — `fixpp_decimal_t` (C-ABI struct)

**Header:** `include/fix/c_api/decimal.h` (extract source: `.specify/2a-decimal.md` v0.3 §5.1)

**Fields:**

| Field | Type | Offset | Notes |
|---|---|---|---|
| `mantissa` | `int64_t` | 0 | Significand. |
| `exponent` | `int8_t` | 8 | Power of 10. |
| `_reserved` | `int8_t[7]` | 9 | Reserved for future use under `FIXPP_C_ABI_DECIMAL_RESERVED_USED` feature macro. **Ignored on read** in v1.0. |

**Layout invariants (verified by `src/capi/decimal_assert.cpp` — seam #4):**

```cpp
static_assert(sizeof(fixpp_decimal_t) == 16);
static_assert(alignof(fixpp_decimal_t) == 8);
static_assert(offsetof(fixpp_decimal_t, mantissa) == 0);
static_assert(offsetof(fixpp_decimal_t, exponent) == 8);
static_assert(offsetof(fixpp_decimal_t, _reserved) == 9);
static_assert(std::is_standard_layout_v<fixpp_decimal_t>);
```

**Macros:**

- `FIXPP_DECIMAL_INITIALIZER` → `{ 0, 0, {0,0,0,0,0,0,0} }` (zero `_reserved` for forward-compat).
- `FIXPP_DECIMAL_INVALID` → `{ INT64_MIN, 0, {0,0,0,0,0,0,0} }` (invalid sentinel).

**Forward-compatibility rule (AC-A4 + AC-A5b):** consumers SHOULD initialize `_reserved` via `FIXPP_DECIMAL_INITIALIZER` or `fixpp_decimal_init()`; the engine tolerates non-zero `_reserved` in v1.0. Any future v1.x semantic for `_reserved` ships as a NEW explicit API, never a silent meaning change for existing consumers (per spec.md `Clarifications` 2026-05-10 line 62).

## Entity 5 — `fixpp::core::error` decimal variants

**Header:** `include/fixpp/core/error.hpp` (owned by **2k**; this feature contributes four named variants per 2a §7.4)

**Variants this feature adds:**

| C++ variant | C-ABI mapping | When raised | Remediation class |
|---|---|---|---|
| `fixpp::core::error::decimal_invalid_input` | `FIXPP_ERR_DECIMAL_INVALID` | Parse rejected (bad bytes, bare `.5`, empty input, unexpected char, embedded SOH); also `to_chars` on a `pod_decimal` with `exponent` outside `[-38, 0]` or `mantissa == INT64_MIN`. | Bad data — reject the message. |
| `fixpp::core::error::decimal_overflow` | `FIXPP_ERR_DECIMAL_INVALID` (data error, same code) | Mantissa overflows `int64_t`, required `exponent < -38`, or `mantissa == INT64_MIN` from parse. | Bad data — reject the message; log loudly. |
| `fixpp::core::error::decimal_precision_loss` | `FIXPP_ERR_DECIMAL_PRECISION_LOSS` | Lossy conversion (cross-traits) where the source value cannot be represented in the destination. | Caller / dictionary bug at conversion site. |
| `fixpp::core::error::decimal_buffer_too_small` | `FIXPP_ERR_BUFFER_TOO_SMALL` (reused generic) | `to_chars` `dst` too small. | Caller bug — fix allocation. |

**Invariants:**

- **`expected_t<T>` is `std::expected<T, fixpp::core::error>`** per `[arch §4.1]`. Not a local alias; not a separate enum.
- **C-ABI numeric values** for `FIXPP_ERR_DECIMAL_INVALID` and `FIXPP_ERR_DECIMAL_PRECISION_LOSS` are **provisional, allocated 2026-05-12 in this PR**, marked with a dated comment in `c_api_decimal.h`. **2i** ratifies by re-using the same numeric range in its own PR; any post-ratification change is a Tier 2 ABI breakage `[const §IX.5]`.

## Entity 6 — `fixpp::detail::decimal_alias_sentinel<T>` (link-time guard)

**Header:** `include/fixpp/core/decimal_alias.hpp` (extract source: `.specify/2a-decimal.md` v0.3 §4.4)

**Fields:**

| Member | Signature | Notes |
|---|---|---|
| `tag` | `template<class T> struct decimal_alias_sentinel { static char const tag; };` | Declared in the header (referenced by every TU). |

**Companion namespace-scope symbol:**

```cpp
namespace fixpp::detail {
inline char const* const fixpp_decimal_alias_lock =
    &decimal_alias_sentinel<FIXPP_DECIMAL_T>::tag;
}
```

`src/core/decimal.cpp` emits **exactly one** specialization:

```cpp
template<> char const fixpp::detail::decimal_alias_sentinel<FIXPP_DECIMAL_T>::tag = 0;
```

**Invariants:**

- A consumer built with a `FIXPP_DECIMAL_T` different from the library's references a specialization the library never defined → unresolved-symbol link error (AC-B3, 2a §4.4).
- `decimal_alias_sentinel` lives in `fixpp::detail` (NOT `fixpp::core`) per the round-1 finding that "sentinel namespace divergence" was a contract issue.

## Cross-entity invariants

- **Single canonical interchange = `pod_decimal`.** All cross-traits conversion (`from<U>` / `to<U>`) funnels through `pod_decimal`. No direct `T → U` path in v1.0 (spec.md §5 out-of-scope).
- **Canonical domain everywhere = `mantissa ∈ [INT64_MIN+1, INT64_MAX]`, `exponent ∈ [-38, 0]`.** Anything outside is `decimal_invalid_input` at parse, `decimal_invalid_input` at format pre-check (AC-S3), `decimal_overflow` at trait conversion.
- **`noexcept` everywhere on the C++ surface** (default-traits case). C-ABI boundary functions are `noexcept`-equivalent (they return `fixpp_error_t` or `int`; never throw).
- **Zero allocation between parse and `fromApp`** `[const §VIII.5]`. PMR resource may be passed but non-allocating traits (`pod_decimal`) ignore it.
