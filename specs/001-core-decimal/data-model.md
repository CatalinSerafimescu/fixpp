# data-model.md — 001-core-decimal

> **Phase 1 output for `/speckit-plan`.** Captures the entities, fields, validation rules, relationships, and invariants for the decimal feature. Concrete C++ / C declarations are extracted to [`contracts/`](./contracts/). Full evidence is in `.specify/2a-decimal.md` v0.3.

## Entity inventory

| # | Entity | Kind | Owner header | Stability |
|---|---|---|---|---|
| 1 | `pod_decimal` | C++ struct (POD) | `include/fixpp/core/decimal.hpp` | Stable v1.0 |
| 2 | `decimal_traits<T>` | C++ template (compile-time customization point) | `include/fixpp/core/decimal.hpp` | Stable v1.0 |
| 3 | `decimal<T>` | C++ template (value wrapper) | `include/fixpp/core/decimal.hpp` | Stable v1.0 |
| 4 | `fixpp_decimal_t` | C struct (PoD, frozen layout) | `include/fix/c_api.h` | **C-ABI v1.0 — frozen** |
| 5 | `fixpp_error_t` (decimal subset) | C enum subset | `include/fix/c_api.h` | **C-ABI v1.0 — provisional in this PR, ratified by 2i** |

## 1. `pod_decimal` — default representation

| Field | Type | Notes |
|---|---|---|
| `mantissa` | `std::int64_t` | Significand. **`INT64_MIN` is reserved as the invalid sentinel.** |
| `exponent` | `std::int8_t` | Power of 10. **Canonical domain: `[-38, 0]`.** Outside → `decimal_invalid_input` at the C-ABI boundary and at trait conversion. |

- Default-constructed `pod_decimal{}` = `(mantissa=0, exponent=0)` — value `0`.
- Sentinel: `inline constexpr pod_decimal pod_decimal_invalid = {INT64_MIN, 0}`.
- **No `operator==`, no `operator<=>`.** Value comparison goes through `decimal<pod_decimal>::compare` (delegates to `decimal_traits<pod_decimal>::compare` per `2a §6.3`). Defaulting equality here would silently field-compare `{1, 0}` and `{10, -1}` as unequal, contradicting the value-equality contract (`AC-C1`).
- C++-level `sizeof` is implementation-defined and **not asserted** — only the C-ABI mirror `fixpp_decimal_t` (entity 4) carries a frozen layout assertion.

### Validation rules

| Rule | Source | Enforcement |
|---|---|---|
| `mantissa != INT64_MIN` for finite values | `2a §4.1` | `from_chars` returns `decimal_overflow` (AC-P8); `to_chars` returns `decimal_invalid_input` (AC-S1) |
| `exponent ∈ [-38, 0]` for canonical values | `2a §6.3` | `from_chars` returns `decimal_overflow` (AC-P7); `to_chars` returns `decimal_invalid_input` (AC-S3) |
| Trailing zeros in fractional part preserved | `2a §6.3` | `5.500` → `{5500, -3}` (AC-P5); canonicalization happens at compare time, not at parse |

### State transitions

N/A — `pod_decimal` is a pure-value type. No lifecycle.

## 2. `decimal_traits<T>` — compile-time customization point

The traits primary template is **undefined**; users specialize it. Required member surface (one specialization required per chosen `T`):

| Required member | Kind | Signature (sketch) | Source |
|---|---|---|---|
| `value_type` | type alias | `using value_type = T;` | `2a §4.2` |
| `is_lossless_for_fix_float` | static constexpr `bool` | `true` if `T → pod_decimal → T` round-trips for every in-domain value | `2a §7.4` |
| `max_serialized_bytes` | static constexpr `std::size_t` | upper bound on `to_chars` output (41 for `pod_decimal`) | `2a §6.5`, `spec §4.2` (AC-S5) |
| `from_chars(span<const byte>, pmr*)` | static `noexcept` | parse FIX FLOAT bytes → `expected<T, decimal_error>` | `2a §6.1` |
| `to_chars(T const&, span<byte>)` | static `noexcept` | serialize → `expected<size_t, decimal_error>` | `2a §6.2` |
| `from_pod(pod_decimal)` | static `noexcept` | canonical-interchange convert → `expected<T, decimal_error>` | `2a §6.4` |
| `to_pod(T const&)` | static `noexcept` | canonical-interchange convert → `expected<pod_decimal, decimal_error>` | `2a §6.4` |
| `compare(T const&, T const&)` | static `noexcept` | `std::strong_ordering` by **value**, not representation | `2a §6.3` (AC-C1) |
| `is_finite / is_zero / is_negative` | static `noexcept` | predicates the wire layer relies on | `2a §4.2` |

Specialization shipped in this feature: `decimal_traits<pod_decimal>` only. Users supply specializations for other `T`.

### Validation rules

- Every specialization MUST be `noexcept` end-to-end on the operations above (`[const §VIII §5]` + `2a §6.5`).
- `is_lossless_for_fix_float` MUST accurately reflect whether `T` round-trips through `pod_decimal` for every value in the canonical domain. Replay sinks at **2e** / **2j** `static_assert(is_lossless_for_fix_float)` against the persistence target.
- Any specialization that wraps a 3rd-party type that *can throw* (e.g., `boost::multiprecision::cpp_dec_float` parse) MUST funnel through `fixpp::core::detail::trap_throw` in `decimal_helpers.hpp` to convert the exception into a `decimal_error` (`2a §7.5`).

## 3. `decimal<T>` — value wrapper

| Member | Kind | Signature (sketch) | Source |
|---|---|---|---|
| ctor | `constexpr explicit` | `constexpr explicit decimal(T) noexcept` | `2a §4.3` |
| `to_pod()` | const member | `noexcept → expected<pod_decimal, decimal_error>` | `2a §4.3` |
| `from_pod(pod_decimal)` | static factory | `noexcept → expected<decimal<T>, decimal_error>` | `2a §4.3` |
| `to<U>()` | const template member | `T == U`: compile-time short-circuit (returns source unchanged); `T ≠ U`: funnel through `pod_decimal`, returns `expected<decimal<U>, decimal_error>` | `2a §6.4`, `spec §4.4` (AC-X1..X3) |
| `value()` | const member | `T const&` accessor for trait authors | `2a §4.3` |

`fixpp::decimal_t` (the engine-wide alias) is `decimal<FIXPP_DECIMAL_T>`. Default `FIXPP_DECIMAL_T = ::fixpp::core::pod_decimal`.

### Cross-traits invariant (AC-X1..X3)

- **AC-X1.** `T ≠ U`: `decimal<T>::to<U>()` always funnels through `pod_decimal`.
- **AC-X2.** Funnel runs and source is outside the PoD `int64 × 10^[-38..0]` domain → `decimal_precision_loss`. No silent truncation.
- **AC-X3.** `T == U`: compile-time short-circuit (`if constexpr (std::is_same_v<T, U>)`) returns the source unchanged. **Round-trip identity holds unconditionally**, including for source values outside the PoD domain (because the funnel doesn't run).

## 4. `fixpp_decimal_t` — C-ABI frozen layout

```c
struct fixpp_decimal_t {
    int64_t mantissa;       // offset 0, size 8
    int8_t  exponent;       // offset 8, size 1
    int8_t  _reserved[7];   // offset 9, size 7  (zero-init recommended; see AC-A4 / AC-A5b)
};
```

| Invariant | Source | Enforcement |
|---|---|---|
| `sizeof(fixpp_decimal_t) == 16` | `[const §X §3]`, `2a §5.1` | `static_assert` in `src/capi/decimal_assert.cpp` (AC-A1) |
| `alignof(fixpp_decimal_t) == 8` | `2a §5.1` | `static_assert` (AC-A1) |
| `offsetof(mantissa) == 0` | `2a §5.1` | `static_assert` (AC-A2) |
| `offsetof(exponent) == 8` | `2a §5.1` | `static_assert` (AC-A2) |
| `offsetof(_reserved) == 9` | `2a §5.1` | `static_assert` (AC-A2) |
| `is_standard_layout_v<fixpp_decimal_t>` | `2a §5.1` | `static_assert` (AC-A3) |
| `_reserved` ignored on read in v1.0 | `spec §4.5` (AC-A4) | regression test `tests/capi/decimal_reserved_test.cpp` (seam #10) |
| `_reserved` zero-init recommended, not required (writers may leave non-zero in v1.0) | `spec §4.5` (AC-A5b), clarification 2026-05-10 | `c_api.h` doc comment + tolerance test |
| Tagged-release ABI golden | `[const §IX §5]`, `2a §9` (seam #4) | `tests/abi/golden/fixpp_decimal_t.abidiff` (Tier 2) |

### Initializer helpers

- `#define FIXPP_DECIMAL_INITIALIZER { 0, 0, {0,0,0,0,0,0,0} }` — C-style aggregate initializer (AC-A5).
- `void fixpp_decimal_init(fixpp_decimal_t* out)` — runtime helper, zero-inits `_reserved` (AC-A5).
- `#define FIXPP_DECIMAL_INVALID { INT64_MIN, 0, {0,0,0,0,0,0,0} }` — sentinel constant.

## 5. `fixpp_error_t` (decimal subset) — C-ABI error codes

| Code | C-ABI numeric value | Source | Owner |
|---|---|---|---|
| `FIXPP_ERR_DECIMAL_INVALID` | provisional, allocated 2026-05-10 (dated comment in `c_api.h`) | `[const §X §4]`, `spec §4.7` | this feature → **2i** ratifies |
| `FIXPP_ERR_DECIMAL_PRECISION_LOSS` | provisional, allocated 2026-05-10 (dated comment in `c_api.h`) | `[const §X §4]`, `spec §4.7` | this feature → **2i** ratifies |
| `FIXPP_ERR_BUFFER_TOO_SMALL` | reused generic (existing) | `2i` (Phase 3 skeleton) | **2i** |

C++ side codes (not at the C-ABI):

| C++ code | Maps to C-ABI |
|---|---|
| `decimal_invalid_input` | `FIXPP_ERR_DECIMAL_INVALID` |
| `decimal_overflow` | `FIXPP_ERR_DECIMAL_INVALID` (mantissa- / exponent-domain breach) |
| `decimal_precision_loss` | `FIXPP_ERR_DECIMAL_PRECISION_LOSS` |
| `decimal_buffer_too_small` | `FIXPP_ERR_BUFFER_TOO_SMALL` |

### Stability

Per `[const §X §4]`: once a numeric value is published in a tagged C-ABI release (`FIXPP_C_ABI_VERSION_MAJOR == 1`), it never changes meaning. The provisional-then-ratify pattern means this PR's numeric values become permanent at **2i**'s ratification PR, audited via `tools/abi_history/error_codes_v1.txt`.

## Relationships

```text
                       ┌────────────────────┐
                       │   pod_decimal      │  ← canonical interchange form (C++)
                       │  (int64, int8)     │
                       └─────────▲──────────┘
                                 │ to_pod / from_pod
                                 │
                  ┌──────────────┴────────────────┐
                  │                               │
        ┌─────────────────┐              ┌────────────────────┐
        │  decimal<T>     │  to<U>():    │  fixpp_decimal_t   │
        │  (engine type,  │  funnel via  │  (C-ABI struct,    │
        │   default T =   │  pod_decimal │   16-byte frozen)  │
        │   pod_decimal)  │              └────────────────────┘
        └─────────────────┘                       ▲
                  ▲                               │ bytewise mirror
                  │                               │
                  │                       boundary fns:
        decimal_traits<T>                 fixpp_decimal_parse / _format /
        (compile-time customization)      _compare / _equal / _init
```

- `decimal<pod_decimal>` ⇄ `pod_decimal` ⇄ `fixpp_decimal_t` is a bytewise mirror plus zero-fill of `_reserved`. The C-ABI struct is the bottom — every traits specialization MUST be able to `to_pod` to it (AC-X1..X2).
- `fixpp::decimal_t` (the engine-wide alias) and `fixpp_decimal_t` are decoupled: switching the C++ alias via `FIXPP_DECIMAL_T` does **not** change the C-ABI shape (AC-B4). The C-ABI form is canonical and frozen.

## Domain invariants (cross-cutting)

1. **Value equality.** Two `pod_decimal` values are equal iff `mantissa_a × 10^exponent_a == mantissa_b × 10^exponent_b` mathematically, regardless of representation. (AC-C1)
2. **Sentinel ordering.** `pod_decimal_invalid` orders strictly greater than every finite value, equal only to itself. (AC-C2)
3. **Round-trip fidelity for in-domain values.** `parse` ∘ `format` is the identity on the canonical domain (modulo trailing-zero canonicalization noted in AC-S4). Tested in seam #2 (10⁴ generated samples + `[FIX50SP2 §3.3]` example table).
4. **No silent precision loss.** Cross-traits `T → U` for `T ≠ U` over an out-of-PoD-domain value MUST surface `decimal_precision_loss` — never silently truncate.
5. **Zero allocation, zero exception** between parse and `fromApp` callback (`[const §VIII §5]`).
