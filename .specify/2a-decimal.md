# Design Doc 2a — Decimal Type & `decimal_traits<T>` Extension Point

> **Status:** Draft v0.3 — Gate A round 2 converged
> **Date:** 2026-05-07
> **Owner:** `fixpp::core` (`include/fixpp/core/decimal.hpp`); C-ABI shape co-owned by `fixpp::capi` (`include/fix/c_api.h`).
> **Inherits:** `[arch §4.1]` (core surface), `[arch §4.10]` (C-ABI shape), `[arch §5.3]` (error model), `[arch §5.5]` (lifetime model), `[arch §6]` (plugin pattern), `[arch §10]` (handoff requirements).
> **Cites:** `[const §VIII.5]` (hot-path discipline), `[const §X.1]` (C-ABI SemVer), `[const §X.2]` (no C++ leakage through C ABI), `[const §X.3]` (decimal at the C ABI boundary), `[const §X.4]` (out-of-range C-ABI code mapping), `[const §VI.4–5]` (Normative References), `[const §XVII.1]` (Codex Gate A before `/tasks`), `[SYN §3.1 Q5]` (decimal type decision).
> **Catalogue rows owned (in part):** **W-009** (field data types — FLOAT family), and the FLOAT-typed accessors of every generated message under `A-001..A-034` / `M-001..M-012` / `P-001..P-008` / `C-001..C-003` / `R-001..R-005` / `N-001..N-003` (Appendix A). Decimal storage representation is owned here; *parse / serialize / validate* behavior is owned by **2b**; *typed-message codegen* binding is owned by **2c**.
> **Convergence log:** see end-of-doc Appendix C — addresses Codex `codex_2a_decimal_review.md` (4 P2), Opus adversarial `opus_2a_decimal_adversarial_review.md` (5 P1, 10 P2, 12 P3), and Codex follow-up `codex_2a_2_decimal_review.md` (1 P1, 4 P2).

---

## 1. Goals

1. Define a single C++ extension point — `fixpp::core::decimal<T>` plus the `decimal_traits<T>` customization point — that lets every consumer pick the underlying numeric representation (PoD mantissa+exp, `decimal128`, `boost::multiprecision`, custom) without forking the library.
2. Lock the **default** representation (`fixpp::core::pod_decimal`) at `(int64_t mantissa, int8_t exponent)` with **canonical exponent domain `[-38, 0]`** — the form proven sufficient for the FX/equities mainstream and adopted verbatim by the C ABI.
3. Lock the **C ABI boundary form** at the same PoD layout `[const §X.3]`, with explicit version macros, layout asserts, an `_reserved[7]` byte field reserved for future use under a feature macro, and a documented forwards-compatibility rule.
4. Specify the operations the wire layer (**2b**) and codegen layer (**2c**) need: lossless parse from FIX FLOAT bytes, **value-lossless** serialize back to FIX FLOAT bytes (round-trip under §6.3 canonicalization, *not* byte-equivalence), equality, ordering, conversion to/from arbitrary user types via traits.
5. Stay zero-allocation on the hot path `[const §VIII.5]` and exception-free between parse and `fromApp` `[arch §5.3]`.

### 1.1 Magnitude domain — what `pod_decimal` can and cannot hold

`pod_decimal` represents `mantissa × 10^exponent` with `mantissa ∈ [INT64_MIN+1, INT64_MAX]` (≈ ±9.22 × 10^18) and `exponent ∈ [-38, 0]`. With **8 fractional digits** (common for FX/crypto), the maximum absolute value is therefore ≈ ±9.22 × 10^10 — sufficient for retail and most institutional flow but *marginal* for accumulators (e.g., `NetMoney(118)`, `CumQty(14)`, `LeavesQty(151)`) on heavily-traded large-notional venues. Consumers who hit this ceiling swap the engine-wide alias to `decimal128` (or other) via `FIXPP_DECIMAL_T` (§4.4); the C-ABI form stays PoD `[const §X.3]` and the binding layer converts.

## 2. Non-goals

- **No general-purpose arithmetic.** This is a *FIX wire* decimal: parse bytes, serialize bytes, compare, convert at the boundary. Add/subtract/multiply/divide are not part of the public surface; users who need arithmetic specialize traits to a type that provides it (e.g., `boost::multiprecision::cpp_dec_float`).
- **No locale-aware formatting.** FIX FLOAT is `[-]?DIGITS(\.DIGITS)?` per `[FIX50SP2 §3.3]`; no thousands separators, no exponent notation, no per-locale decimal marks. Formatting is fixed.
- **No silent precision loss.** Any traits specialization whose conversion from the parsed mantissa+exp pair *cannot* round-trip back to a value-equivalent representation must report `error::decimal_precision_loss` (see §7.4); we do not silently truncate.
- **No runtime polymorphism.** `decimal_traits<T>` is a compile-time customization point; there is no virtual interface and no `std::variant`. The trade is zero indirection on the hot path against templated-symbol amplification — accepted per `[SYN §3.1 Q5]` and constrained to one symbol set per build by §4.4.
- **No direct `T → U` cross-traits path** in v1.0. All cross-traits conversion funnels through `pod_decimal` as canonical interchange (§6.4); for users whose `T` and `U` are both wider than `pod_decimal`, this is a documented narrowing point. Re-evaluated post-v1.0 if a real consumer hits it.

## 3. Inherited surface

From `[arch §4.1]`:

> `fixpp::core::decimal<T>` and `fixpp::core::decimal_traits<T>` — extension point. Default `T = pod_decimal { int64_t mantissa; int8_t exponent; }`; users specialise traits to plug in `decimal128`, `boost::multiprecision`, etc. — owned by **2a**.

From `[arch §4.10]`:

> Decimal at the C boundary: PoD `(int64 mantissa, int8 exponent)` `[const §X.3]`.

From `[arch §10]` row 2a:

> Decimal type — `fixpp::core::decimal<T>`, `decimal_traits<T>`, PoD C-ABI shape — cross-cutting hooks: §4.1 surface; §5.3 error model.

This document refines that surface; it does **not** diverge.

## 4. Public API — C++

### 4.1 Default representation: `pod_decimal`

```cpp
// include/fixpp/core/decimal.hpp
namespace fixpp::core {

struct pod_decimal {
    std::int64_t mantissa;   // significand. Reserved: INT64_MIN is the invalid sentinel.
    std::int8_t  exponent;   // power of 10; value = mantissa * 10^exponent.
                             // Canonical domain: exponent ∈ [-38, 0]. Anything outside
                             // is rejected at the C-ABI boundary and at trait conversion.

    // No operator==, no operator<=>. Value comparison goes through decimal<pod_decimal>
    // (§4.3), which delegates to the canonicalizing traits (§6.3). Defaulting equality
    // here would silently field-compare {1,0} and {10,-1} as unequal, contradicting
    // the value-equality contract — see Appendix C for the original Codex finding.
};

inline constexpr pod_decimal pod_decimal_invalid {INT64_MIN, 0};

}  // namespace fixpp::core
```

`pod_decimal` is intentionally raw: ~9 bytes of payload, no operators, no inline arithmetic. Storage size at the C++ level is implementation-defined and *not* asserted (the C-ABI struct in §5.1 is the one with a frozen layout). The `INT64_MIN` mantissa value is reserved as the invalid/sentinel form (the value `INT64_MIN × 10^0` is unrepresentable in the canonical `[-INT64_MAX, INT64_MAX]` range without a sign bit; reserving it costs nothing).

Default-constructed `pod_decimal{}` has `mantissa=0, exponent=0` — the value `0`. The sentinel is produced only by explicit construction (`pod_decimal_invalid`), by parse failure, or by canonicalization overflow.

### 4.2 The `decimal_traits<T>` customization point

```cpp
namespace fixpp::core {

template <class T>
struct decimal_traits;          // primary template — undefined; user must specialize.

// ── Required member types and constants ──────────────────────────────────────
//   using value_type        = T;                  // representation
//   static constexpr bool   is_lossless_for_fix_float = ...;   // see §7.4 + below
//   static constexpr std::size_t max_serialized_bytes = ...;   // upper bound
//
// ── Required static member functions (all noexcept) ───────────────────────────
//   // Parse FIX FLOAT bytes into T. Hot path; non-allocating traits ignore mr.
//   // mr is never null (the wire layer always passes a valid resource).
//   static expected_t<T>
//   from_chars(std::span<const std::byte> src,
//              std::pmr::memory_resource* mr) noexcept;
//
//   // Serialize T to FIX FLOAT bytes into a caller-provided buffer.
//   // Returns the count of bytes written, or an error.
//   static expected_t<std::size_t>
//   to_chars(T const& v, std::span<std::byte> dst) noexcept;
//
//   // Conversion to/from the canonical PoD form (used at the C ABI boundary,
//   // by typed-message bridges, and by tests). Must canonicalize to the
//   // pod_decimal exponent ∈ [-38, 0] domain. Out-of-domain → decimal_overflow.
//   static expected_t<T>           from_pod(pod_decimal) noexcept;
//   static expected_t<pod_decimal> to_pod  (T const&) noexcept;
//
//   // Total ordering by value (decimal value, not encoded representation):
//   // compare(decimal{1,0}, decimal{10,-1}) == strong_ordering::equal.
//   // equality is derived: equal(a,b) ≡ (compare(a,b) == 0).
//   static std::strong_ordering compare(T const&, T const&) noexcept;
//
//   // Domain checks the wire layer relies on.
//   static bool is_finite (T const&) noexcept;
//   static bool is_zero   (T const&) noexcept;
//   static bool is_negative(T const&) noexcept;
}  // namespace fixpp::core
```

Notes:

- **PMR is required.** `from_chars` must accept a `std::pmr::memory_resource*`. Non-allocating traits (e.g., `pod_decimal`) ignore it. Allocating traits (e.g., `boost::multiprecision::cpp_dec_float`) use it to honour the hot-path discipline `[const §VIII.5]`. The wire layer (§7.1) always passes the per-message arena `[arch §5.2]`. Rationale: the previous design allowed an optional PMR overload, which left a contract gap for allocating traits — see Codex review (Appendix C, P2 #4) and Opus P1 #4.
- **`equal` derived from `compare`.** Earlier drafts declared both as required; carrying two functions for the same predicate invited inconsistent specializations. `decimal<T>::operator==` calls `compare(...) == 0` (§4.3).
- **`is_lossless_for_fix_float` is a *trait author's promise*.** Read it as: "for every input string `s` accepted by `from_chars` (i.e., FIX-valid per §6.1), the resulting `T` round-trips through `to_chars` to a byte sequence whose `from_chars` re-parse compares value-equal under `compare` (§6.3)." This is *value*-lossless, not byte-equivalent (trailing zeros and leading `+` are not preserved). `pod_decimal` declares `true`. Wrappers around `double` declare `false` (some FIX-valid inputs do round-trip exactly under IEEE-754, but not all). The wire layer permits `false` traits on read accessors but **forbids them on the store/replay path** (§7.1).
- **Throwing third-party libraries.** Some arbitrary-precision wrappers (`cpp_dec_float`, `mpfr`) call into code that throws (`std::bad_alloc`, format errors). Required `noexcept` traits MUST wrap such calls in `try`/`catch` and translate to `expected_t` errors; otherwise an escaping exception terminates the program at the `noexcept` boundary. A reference helper `fixpp::core::detail::trap_throw([&]{ ... })` is provided in `<fixpp/core/decimal_helpers.hpp>` for trait authors.
- **Forward-compat.** New required members may only be added in a *minor* library version under a feature-test macro `FIXPP_DECIMAL_TRAITS_FEATURE_<NAME>`; existing specializations stay compiling. Macros are declared in `include/fixpp/core/decimal.hpp`.

### 4.3 The `decimal<T>` value type

```cpp
namespace fixpp::core {

template <class T = pod_decimal>
class decimal {
public:
    using traits_type = decimal_traits<T>;
    using value_type  = T;

    constexpr decimal() noexcept = default;
    constexpr explicit decimal(T v) noexcept : value_(std::move(v)) {}

    constexpr T const& value() const noexcept { return value_; }

    // Round-trip helpers — thin shells over decimal_traits<T>; provided so call
    // sites need not name the traits explicitly. mr defaults to the active arena
    // when the call site is inside a wire callback; otherwise the caller must
    // pass one (or pass std::pmr::get_default_resource() and accept the cost).
    static expected_t<decimal>     parse(std::span<const std::byte> src,
                                         std::pmr::memory_resource* mr) noexcept;
    expected_t<std::size_t>        format(std::span<std::byte> dst) const noexcept;

    // Cross-traits conversion (e.g., decimal<pod_decimal> ↔ decimal<decimal128>).
    // Funnels through pod_decimal — see §6.4 for the documented v1.0 limitation.
    template <class U>
    static expected_t<decimal>     from(decimal<U> const&) noexcept;
    template <class U>
    expected_t<decimal<U>>         to() const noexcept;

    friend bool operator==(decimal const& a, decimal const& b) noexcept
        { return traits_type::compare(a.value_, b.value_) == 0; }
    friend std::strong_ordering operator<=>(decimal const& a, decimal const& b) noexcept
        { return traits_type::compare(a.value_, b.value_); }

private:
    T value_{};
};

using decimal_default = decimal<pod_decimal>;

}  // namespace fixpp::core
```

`decimal<T>` is intentionally tiny: it is a value-typed wrapper, not an arithmetic façade. `decimal<T>::parse` consumes the input span eagerly — the resulting `decimal<T>` does **not** alias the input buffer, so callers may free or reuse the source bytes immediately. (Distinct from `wire::View` flyweights, which *do* alias and require the lifetime contract of `[arch §5.5]`.) Codegen (**2c**) emits `decimal<T>` accessors on typed messages where `T` is the engine-wide alias; one alias per build = one symbol set, avoiding the templated-codegen amplification called out in `[SYN §3.1 Q5]`.

### 4.4 Build-time selection — `FIXPP_DECIMAL_T`

The "one alias per build" rule is exposed as:

```cpp
// include/fixpp/core/decimal_alias.hpp
#ifdef FIXPP_DECIMAL_USER_HEADER
#  include FIXPP_DECIMAL_USER_HEADER     // user-supplied header that declares T
#endif

#ifndef FIXPP_DECIMAL_T
#  define FIXPP_DECIMAL_T ::fixpp::core::pod_decimal
#endif

namespace fixpp { using decimal_t = core::decimal<FIXPP_DECIMAL_T>; }

// Link-time enforcement: every TU that includes this header emits a reference
// to an explicit template specialization whose mangled name encodes T. The
// pre-built library defines the specialization for *its* FIXPP_DECIMAL_T
// exactly once, in src/core/decimal.cpp. A consumer built with a different
// FIXPP_DECIMAL_T references a specialization the library never defined →
// unresolved-symbol link error with a clear message.
//
// (The previous v0.2 design pasted the macro name into an identifier, which
// is a no-op — macros do not expand inside identifiers — so every TU used
// the same symbol regardless of the chosen alias. See Codex follow-up P1
// in Appendix C.)
namespace fixpp::detail {
template <class T> struct decimal_alias_sentinel {
    static char const tag;     // declared here, defined in src/core/decimal.cpp
                               // for the library's chosen FIXPP_DECIMAL_T only.
};
inline char const* const fixpp_decimal_alias_lock =
    &decimal_alias_sentinel<FIXPP_DECIMAL_T>::tag;
}  // namespace fixpp::detail

// src/core/decimal.cpp emits exactly one definition:
//   template<> char const fixpp::detail::decimal_alias_sentinel<FIXPP_DECIMAL_T>::tag = 0;
```

Codegen and typed wire accessors (**2b**, **2c**) refer to `fixpp::decimal_t`, *not* to `decimal<pod_decimal>` directly. Switching the build-time alias requires:

1. A user-supplied header that declares the chosen `T` and any `decimal_traits<T>` specialization (e.g., `my_project/fixpp_decimal.hpp`).
2. `-DFIXPP_DECIMAL_USER_HEADER='"my_project/fixpp_decimal.hpp"'` *and* `-DFIXPP_DECIMAL_T=my::decimal128` on the compile line.
3. The pre-built library being built with the same flags. CMake `find_package(fixpp)` exposes a `FIXPP_DECIMAL_T_REBUILD_NEEDED` boolean if the consumer's flag differs from the library's; the `fixpp::fixpp` target's `INTERFACE_COMPILE_DEFINITIONS` carries the library's chosen alias for verification.

Doing so does **not** change the C-ABI form, which stays PoD `[const §X.3]`.

## 5. Public API — C ABI

### 5.1 Layout

```c
/* include/fix/c_api.h */
#include <stdint.h>
#include <stddef.h>   /* size_t — required by fixpp_decimal_parse / _format below */

typedef struct fixpp_decimal {
    int64_t mantissa;
    int8_t  exponent;
    int8_t  _reserved[7];   /* reserved for future use under FIXPP_C_ABI_DECIMAL_RESERVED_USED.
                               In v1.0 the engine IGNORES these bytes on read. Consumers
                               SHOULD initialize via FIXPP_DECIMAL_INITIALIZER or
                               fixpp_decimal_init() to remain forward-compatible. */
} fixpp_decimal_t;

#define FIXPP_DECIMAL_INITIALIZER { 0, 0, {0,0,0,0,0,0,0} }
#define FIXPP_DECIMAL_INVALID     { INT64_MIN, 0, {0,0,0,0,0,0,0} }
```

This shape is **frozen** for `FIXPP_C_ABI_VERSION_MAJOR == 1` `[const §X.1]`. The `_reserved` field exists for forward compatibility — a future minor version may co-opt one or more bytes for a flag byte / NaN indicator under the `FIXPP_C_ABI_DECIMAL_RESERVED_USED` feature macro. Until then, the engine **does not validate `_reserved`** on read; consumers may leave it uninitialized without breaking parsing. The `FIXPP_DECIMAL_INITIALIZER` and `fixpp_decimal_init()` helpers are provided for consumers who want forward compatibility — when a future minor version *does* use `_reserved`, callers using either initializer keep working.

Decision rationale for ignore-on-read (vs zero-check): the previous draft required `_reserved == 0` on read, which broke any C consumer that wrote `fixpp_decimal_t d; d.mantissa = ...; d.exponent = ...;` and passed it in (garbage in `_reserved`). See Opus P2 #10.

### 5.2 Boundary functions

```c
/* parse FIX FLOAT bytes from src into out; returns FIXPP_ERR_OK on success. */
fixpp_error_t
fixpp_decimal_parse(const char* src, size_t src_len, fixpp_decimal_t* out);

/* serialize d into dst; writes byte count to *written.
   Worst-case bound: 41 bytes (sign + "0." + 38 leading zeros + 19 mantissa digits).
   The 41-byte ceiling holds because exponent is restricted to [-38, 0] at the boundary;
   positive exponents are rejected (FIXPP_ERR_DECIMAL_INVALID).
   Returns FIXPP_ERR_BUFFER_TOO_SMALL if dst_cap < required. */
fixpp_error_t
fixpp_decimal_format(fixpp_decimal_t d, char* dst, size_t dst_cap, size_t* written);

/* lexicographic-after-canonicalization comparison; returns -1 / 0 / +1.
   Algorithm is the digit-string compare of §6.3 — never overflows. */
int fixpp_decimal_compare(fixpp_decimal_t a, fixpp_decimal_t b);

/* Convenience: equality. Equivalent to fixpp_decimal_compare(a,b) == 0. */
int fixpp_decimal_equal  (fixpp_decimal_t a, fixpp_decimal_t b);

/* Zero-init helper for forward compatibility with future _reserved-byte semantics. */
void fixpp_decimal_init  (fixpp_decimal_t* out);
```

The C++ engine constructs and consumes `fixpp_decimal_t` *only* via `decimal_traits<pod_decimal>::to_pod / from_pod`. No other path crosses the ABI for decimals.

### 5.3 Layout assertions

`src/capi/decimal_assert.cpp` carries:

```cpp
static_assert(sizeof(fixpp_decimal_t) == 16);
static_assert(alignof(fixpp_decimal_t) == 8);
static_assert(offsetof(fixpp_decimal_t, mantissa) == 0);
static_assert(offsetof(fixpp_decimal_t, exponent) == 8);
static_assert(offsetof(fixpp_decimal_t, _reserved) == 9);
static_assert(std::is_standard_layout_v<fixpp_decimal_t>);
```

Plus a Tier 2 abidiff golden against the previous tagged ABI release `[const §IX.5]`.

## 6. Behavioral contract

### 6.1 Parse (FIX FLOAT → `pod_decimal`)

Grammar `[FIX50SP2 §3.3]`, tightened:

```
FIXFLOAT := SIGN? DIGITS ( '.' DIGITS )?
SIGN     := '+' | '-'
DIGITS   := [0-9]+              # at least one digit on each side of '.' if '.' present
```

Algorithm (default traits):

1. Reject empty input → `error::decimal_invalid_input`.
2. Strip optional leading sign (`+` or `-`). Both `+0` and `-0` are accepted and produce the value `0` (canonical form `{0, 0}`); negative-zero is not preserved.
3. Reject the bare forms `.5` (no integer digits) and `5.` (no fractional digits) → `error::decimal_invalid_input`.
4. Reject any non-digit, non-dot, non-sign byte mid-field, including `\x01` SOH (the wire layer strips field delimiters before calling `from_chars`; an embedded SOH means a malformed frame and is reported here).
5. Leading zero runs in the integer part (`00005`) are accepted; the value `5` is produced (i.e., zeros are not significant). Trailing zeros in the fractional part (`5.500`) are preserved in `exponent` — `{5500, -3}` — pending serialization-time stripping per §6.2.
6. Assemble `mantissa` as the signed 64-bit integer formed by the digit sequence (integer + fractional concatenated). Reject overflow → `error::decimal_overflow`.
7. Set `exponent = -(fractional-digit-count)`. Always `≤ 0` by construction. Reject `exponent < -38` → `error::decimal_overflow` (cannot represent in the canonical domain).
8. Apply sign to mantissa. Reject `mantissa == INT64_MIN` (sentinel collision) → `error::decimal_overflow`.

Algorithm runs in a single pass, no allocation, no exception. On failure the `out` parameter is left unmodified (caller must check the return).

### 6.2 Serialize (`pod_decimal` → FIX FLOAT)

Pre-condition: `exponent ∈ [-38, 0]` (the canonical domain). If a `pod_decimal` arrives with `exponent` outside this range — either `exponent > 0` (e.g., from a buggy traits specialization) or `exponent < -38` (the C-ABI struct's `int8_t` admits `[-128, 127]`, so a malicious or buggy C caller could supply, say, `-128`) — `to_chars` returns `error::decimal_invalid_input` *before* attempting to format. The check is two-sided. Without it, a value like `{1, -128}` would walk past the 41-byte ceiling in step 3's `dot_pos < 0` branch (writing 128+ leading zeros) and silently overflow caller buffers. This is the rejection point referenced by §5.2.

1. If `mantissa == INT64_MIN` (sentinel) → `error::decimal_invalid_input`.
2. If `mantissa == 0` → write `"0"` (one byte) regardless of exponent (canonical zero).
3. Otherwise let `digits = base-10 representation of |mantissa|` (1..19 chars), `exp = exponent` (in `[-38, 0]`), and `dot_pos = digit_count + exp`:
   - If `dot_pos > 0`: write `[sign?] digits[0..dot_pos] '.' digits[dot_pos..end]`. The fractional segment may be empty when `dot_pos == digit_count`, in which case omit the trailing `.` (canonical: write `"5"`, not `"5."`).
   - If `dot_pos == 0`: write `[sign?] '0.' digits`.
   - If `dot_pos < 0`: write `[sign?] '0.' '0'×(-dot_pos) digits`.
4. Trailing-zero stripping in the fractional segment is performed (canonical output).
5. Worst-case ceiling: `1` (sign) + `2` (`0.`) + `38` (leading zeros for `dot_pos = -38`) + `19` (mantissa digits) − overlap when `dot_pos < 0` ⇒ `1 + 2 + 38 + 19 = 60` minus the overlap structure where the leading `0.` and the leading-zero run are contiguous. Concretely, `{-9223372036854775807, -38}` produces `-0.000000000000000000009223372036854775807` = `1 + 2 + 19 + 19 = 41` bytes.

`max_serialized_bytes = 41` for `pod_decimal`.

### 6.3 Equality and comparison (canonicalized, no overflow)

Two `pod_decimal`s represent the same value if their numeric value matches: `{1, 0}` (= 1) equals `{10, -1}` (= 1.0) equals `{100, -2}` (= 1.00). Comparison is *not* implemented by scaling to a common exponent in a wider integer (the previous `__int128` fallback was unsound — see Codex P2 + Opus P1 #3). Instead:

```
compare(a, b):
    # 0. invalid sentinel sorts strictly greater than every finite value;
    #    equal only to itself. MUST come before sign()/abs(), because
    #    sign(INT64_MIN) is well-defined but |INT64_MIN| overflows int64,
    #    and because canonicalize() below operates on |mantissa|.
    a_inv := (a.mantissa == INT64_MIN); b_inv := (b.mantissa == INT64_MIN)
    if a_inv && b_inv: return strong_ordering::equal
    if a_inv:          return strong_ordering::greater
    if b_inv:          return strong_ordering::less

    # 1. sign
    sign_a := sign(a.mantissa); sign_b := sign(b.mantissa)
    if sign_a != sign_b: return sign_a <=> sign_b
    if sign_a == 0:      return strong_ordering::equal     # both are 0

    # 2. canonicalize: strip trailing-zero base-10 digits from |mantissa|, adjusting exponent
    #    (e.g., {100, -2} → {1, 0}). Stops when |mantissa| % 10 != 0.
    a' := canonicalize(a)
    b' := canonicalize(b)

    # 3. magnitude bucket: position of leading digit relative to decimal point
    #    = digit_count(|mantissa|) + exponent  (a single int subtract; no overflow)
    bucket_a := digit_count(|a'.mantissa|) + a'.exponent
    bucket_b := digit_count(|b'.mantissa|) + b'.exponent
    if bucket_a != bucket_b:
        return (sign_a > 0) ? (bucket_a <=> bucket_b)
                            : (bucket_b <=> bucket_a)   # sign flips order

    # 4. same magnitude bucket → compare digit strings lexicographically,
    #    left-padding the shorter mantissa-digit-string with zeros on the right.
    #    O(digits); each step compares one base-10 digit; max 19 iterations.
    ord := compare_digit_strings(|a'.mantissa|, |b'.mantissa|)
    return (sign_a > 0) ? ord : (ord inverted)
```

`pod_decimal_invalid` orders strictly greater than every finite value (and equal only to itself) — implemented by step 0 above. Convenient for sort stability of mixed valid/invalid arrays during debugging. Total order: `strong_ordering`.

The algorithm runs in O(digit_count) = O(1) since digits ≤ 19. No multiplication, no wide-int dependency, no MSVC-vs-Clang algorithm split.

Tests pin the table from `[FIX50SP2 §3.3]` examples and cross-check against an arbitrary-precision oracle (Python `Decimal`, run in Tier 1 — see §9 seam #7).

### 6.4 Cross-traits conversion (v1.0 limitation)

`decimal<T>::to<U>()` calls `decimal_traits<T>::to_pod` then `decimal_traits<U>::from_pod`. PoD is the canonical interchange form for **all** cross-traits conversions, mirroring the C-ABI boundary rule. Consequence: for two non-default traits where both representations are *wider* than `pod_decimal` (e.g., `decimal128 → cpp_dec_float<50>`), values that don't fit in `int64 × 10^[-38..0]` produce `error::decimal_precision_loss` — there is no direct `T → U` escape hatch in v1.0. This is intentional and tied to `[SYN §3.1 Q5]`: PoD is the lingua franca; a direct `T → U` seam would multiply the number of conversion paths and undermine the "one alias per build" simplicity. Re-evaluated post-v1.0 if a real consumer hits it (tracked in §10 Q1).

The wire layer (**2b**) translates `decimal_precision_loss` into `error::wire_field_value_truncated` for the FIX-level error report.

### 6.5 Allocation, exceptions, threading

- **Allocation.** All public functions on `decimal<T>` and `decimal_traits<pod_decimal>` are `noexcept` and zero-allocation `[const §VIII.5]` in the default-traits case. Allocating-traits specializations honour the per-message arena (§7.1) for any heap traffic.
- **Exceptions.** No `throw` between parse and `fromApp` `[arch §5.3]`. Trait wrappers around throwing third-party libraries MUST trap (§4.2 note).
- **Thread / strand safety.** `pod_decimal` is trivially copyable; safe to pass by value across thread/strand boundaries; no atomic ops needed. Arbitrary `T` is **not** guaranteed trivially copyable — traits authors documenting their `T` is the user's contract.
- **Latency target (default traits, Linux/Clang/x86_64, 5-digit mantissa, warm cache).** Parse: ≤ 50 ns. Format: ≤ 30 ns. Compare: ≤ 20 ns. These are bench-harness regression bars (§9 seam #5); breaches fail Tier 1.

## 7. Integration with adjacent modules

### 7.1 Wire (`[arch §4.3]`, owner **2b**)

The wire parser uses `decimal_traits<FIXPP_DECIMAL_T>::from_chars(span, mr)` for FLOAT-typed fields and provides a `std::span<const std::byte>` view directly into the receive buffer (zero-copy). `mr` is the per-message arena `[arch §5.2]`. On serialize, `to_chars` writes into the message-frame builder's PMR-backed buffer.

**Replay fidelity does *not* gate on the decimal trait.** The store backend (**2e**) records **raw FIX frames** at the wire layer (before parsing); replay re-emits those frames byte-for-byte without going through field-level re-serialization. Decimal trait fidelity is therefore irrelevant to replay — even an `is_lossless_for_fix_float == true` trait would lose trailing zeros and the leading `+` through canonicalization (§6.2 step 4), so any compile-time gate based on `is_lossless_for_fix_float` would be both insufficient (for the trailing-zero/sign case) and unnecessary (for replay, which doesn't re-serialize). This was the v0.2 design error noted as Codex follow-up P2 in Appendix C.

**Lossy traits *are* gated on the typed-payload persistence surface** — distinct from replay. A persistence sink that records *parsed values* rather than raw frames (e.g., SBE/Avro snapshotting, cross-process IPC of decoded fields, audit pipelines that emit normalized records) loses precision irrecoverably if the engine-wide alias is `is_lossless_for_fix_float == false`. That sink (defined at **2e** for MessageStore and at **2j** for control-plane snapshots) carries a `static_assert(decimal_traits<FIXPP_DECIMAL_T>::is_lossless_for_fix_float, "lossy decimal traits must not feed typed-payload persistence")` at the persistence-write call site. Read accessors and ephemeral compute (e.g., risk math) are unaffected. Confirmed at **2e**.

### 7.2 Dictionary / codegen (`[arch §4.2]`, owner **2c**)

Generated typed accessors return `fixpp::decimal_t` for FLOAT-family fields (PRICE, QTY, AMT, PRICEOFFSET, PERCENTAGE — all FLOAT subtypes per `[FIX50SP2 §3.3]`). The QuickFIX dictionary loader has no decimal-specific behavior; the codegen template substitutes `fixpp::decimal_t` wherever the dictionary marks a field as FLOAT or one of its subtypes.

### 7.3 C ABI message representation (`[arch §4.10]`, owner **2i**)

`fixpp_msg_field_decimal(msg, tag, &out)` returns `fixpp_error_t` and writes a `fixpp_decimal_t` into `out`. **2i** owns this accessor's signature and its error-code wiring; **2a** owns only the `fixpp_decimal_t` shape and the parse/format/compare/equal/init helpers above.

### 7.4 Error model (`[arch §5.3]`)

Decimal-layer errors are members of `fixpp::core::error`:

| Code | When raised | Remediation class |
|---|---|---|
| `decimal_invalid_input` | Parse rejected (bad bytes, bare `.5`, empty input, unexpected char, embedded SOH); also `to_chars` on a `pod_decimal` with `exponent > 0` or `mantissa == INT64_MIN`. | Bad data — reject the message. |
| `decimal_overflow` | Mantissa overflows `int64_t`, required `exponent < -38`, or `mantissa == INT64_MIN` from parse. | Bad data — reject the message; log loudly. |
| `decimal_precision_loss` | Lossy conversion (cross-traits) where the source value cannot be represented in the destination. | Caller / dictionary bug at conversion site. |
| `decimal_buffer_too_small` | `to_chars` `dst` too small. | Caller bug — fix allocation. |

C-ABI mapping (`[const §X.4]`): split into three codes to preserve the data-vs-bug distinction (Opus P2 #9):

| C-ABI code | C++ source |
|---|---|
| `FIXPP_ERR_DECIMAL_INVALID` | `decimal_invalid_input`, `decimal_overflow` (data errors) |
| `FIXPP_ERR_DECIMAL_PRECISION_LOSS` | `decimal_precision_loss` (semantic / caller) |
| `FIXPP_ERR_BUFFER_TOO_SMALL` | `decimal_buffer_too_small` (already a generic C-ABI code; reused) |

**2i** confirms the final code numeric ranges.

## 8. PMR — recap

PMR is **required** in the trait `from_chars` signature (§4.2). Non-allocating traits ignore the resource pointer; the wire layer always passes a valid arena. Attempting to wrap an allocating implementation in the previous "optional PMR overload" pattern produced an incoherent contract — see Codex P2 #4 and Opus P1 #4 in Appendix C.

For trait specializations whose `T` represents the entire decimal value in-place (no auxiliary storage, e.g., `pod_decimal`, `decimal128`), `decimal_traits<T>::is_lossless_for_fix_float` is typically `true` and `mr` is unused; for arbitrary-precision wrappers, `mr` is consulted on every parse.

## 9. Test seams

Per `[arch §10]` requirement (4): every design doc ends with the test seams it exposes.

1. **`mock_decimal_traits<T>`** — header-only traits specialization for tests, parameterized to inject failures (truncate-on-parse, fail every Nth call, allocate from a tracked PMR resource) at the seam without touching wire-layer code. Lives in `tests/support/mock_decimal_traits.hpp`.
2. **Round-trip property tests** — for any FIX-valid byte sequence `b`, `parse(b)` and `parse(format(parse(b)))` produce values equal under `compare` (§6.3). Run on a corpus of 10⁴ generated samples plus the `[FIX50SP2 §3.3]` example table. (Wording correction from v0.1 — bytes themselves don't compare under §6.3.)
3. **Cross-traits round-trip** — for any `T`, `U` pair where both declare `is_lossless_for_fix_float`, *and* the source value fits in the PoD interchange domain, `decimal<T> → decimal<U> → decimal<T>` is the identity. For values *outside* the PoD domain, the conversion returns `decimal_precision_loss` (verified separately).
4. **C-ABI layout golden** — Linux / x86_64 abidiff snapshot of `fixpp_decimal_t` (16 bytes, `_reserved[7]` at offset 9) checked in under `tests/abi/golden/`. Drift fails Tier 2.
5. **Latency regression** — Google Benchmark runs `parse`, `format`, and `compare` on the warm-cache 5-digit-mantissa workload; CI fails if median exceeds the §6.5 ceilings.
6. **Allocation guard (Linux).** `tools/check_alloc.py` runs the parse-format loop under the `mallocnesia` interceptor; any allocation between parse and format fails CI `[const §VIII.5]`. **Windows gap:** equivalent `_CrtSetAllocHook` wiring is a Tier 2 nice-to-have for v1.x; for v1.0 we document the gap explicitly per `[const §IX.4]` (Linux/Clang Tier 1 covers the discipline; Windows is allowed to lag).
7. **Fuzzer (libFuzzer)** — `tests/fuzz/fuzz_decimal_parse.cpp` feeds arbitrary bytes to `parse`; targets ASan + UBSan invariants. Required by `[const §IX.4]`.
8. **Property-based comparison oracle (Tier 1).** For any two `pod_decimal`s in the canonical domain, `compare(a, b)` matches the comparison of `mantissa × 10^exponent` computed in arbitrary precision (Python `Decimal` reference). **Promoted from Tier 2 to Tier 1** because the new digit-string compare algorithm (§6.3) is the riskiest piece of the design and we want CI gating it from day one.
9. **`FIXPP_DECIMAL_T` link-time mismatch test** — two TUs build with conflicting `FIXPP_DECIMAL_T` aliases; link is expected to fail with the sentinel-symbol unresolved-reference.
10. **`_reserved` byte tolerance test** — C-ABI consumer passes an `fixpp_decimal_t` with garbage `_reserved`; engine accepts and parses correctly (regression guard for the "ignore on read in v1.0" rule).

## 10. Open questions

| # | Question | Disposition | Owner |
|---|---|---|---|
| 1 | Direct `T → U` cross-traits seam (skipping PoD interchange) — re-evaluate post-v1.0 if a real consumer's wider-than-PoD-to-wider-than-PoD path is bottlenecked by the PoD narrowing. | DEFERRED to first user pull; not in v1.0 (§6.4). | 2a follow-up |
| 2 | Built-in `decimal_traits<__int128>` specialization — ship or leave to consumers? Risk: signals an arithmetic-friendly representation we don't otherwise endorse. | DEFERRED to first user pull; not in v1.0. | 2a follow-up |
| 3 | Confirm with **2e** that (a) MessageStore records **raw FIX frames** for replay (not typed payloads), so replay is decimal-trait-agnostic, and (b) the typed-payload-persistence `static_assert(is_lossless_for_fix_float)` lives at the persistence-write site (§7.1 framing). If (a) is not the chosen MessageStore design, this whole rule needs reopening. | Confirm at 2e; if (a) inverts, reopen §7.1. | 2a + 2e |
| 4 | Per-architecture latency ceilings (§6.5) — are 50 ns parse / 30 ns format / 20 ns compare the right Tier 1 bars, or do we want stricter? | Bench spike during 2b implementation; revise if the wire integration suggests headroom. | 2a + 2b |

(v0.1's Q4 — the `_reserved` byte policy — is now DECIDED in §5.1 and removed from this list per Opus P3 #20. v0.1's Q3 wide-compare benchmark question is OBSOLETED by the algorithm change in §6.3.)

## 11. Hand-off

- After Gate A and user sign-off, **2a** unblocks **2b** (wire parser knows the FLOAT field type) and **2c** (codegen knows what accessor type to emit). It has no further upstream dependencies inside Phase 2.
- **2a** does not add a new catalogue row. The W-009 row is already OFFICIAL; the FLOAT-data-type sub-rows are inherited rather than newly generated.
- `feature-catalogue.md` does **not** need a row added when 2a lands. (Distinct from 2d's `NFR-015` clock row, tracked in `[arch §11]` row 7.)

---

## Appendix A — Catalogue row coverage

`decimal<T>` storage and the C-ABI `fixpp_decimal_t` shape participate in the following OFFICIAL catalogue rows. For each row, this doc owns the *decimal representation*; the row's parse/serialize is owned by **2b**, the typed accessors by **2c**, and the C-ABI accessor signatures by **2i**.

| Row range | Family | What 2a covers |
|---|---|---|
| W-009 | Field data types — INT, FLOAT, … | The FLOAT family's representation type. |
| A-001..A-034 | Order-management messages (per `[arch §4.4]`) | All Price, OrderQty, AvgPx, CumQty, LeavesQty, … FLOAT fields. |
| M-001..M-012 | Market Data + Reference Data | Price, MDEntryPx, MDEntrySize, … |
| P-001..P-008 | Pre-trade (Quote, Indication, …) | BidPx, OfferPx, BidSize, OfferSize, … |
| C-001..C-003 | Cross / list strategy parameters | Price, OrderQty, … |
| R-001..R-005 | Post-trade allocations & confirmations | AllocQty, AllocPrice, NetMoney, … |
| N-001..N-003 | Trade-capture / position reports | LastPx, LastQty, MarkToMarketPnL, … |

(Final per-tag mapping confirmed at **2c** when codegen lands; the list above is upper-bound, not exhaustive on a per-tag basis. Range corrected from v0.1 per Opus P3 #16.)

## Appendix B — Normative References

Per `[const §VI.5]`, every `/specify` artifact lists the exact coverage-index references that inform it. This doc binds to:

| Topic | Source | Where applied |
|---|---|---|
| FIX FLOAT data type grammar | `[FIX50SP2 §3.3] Field data types` (catalogue **W-009**) | §6.1 parse, §6.2 serialize |
| C-ABI decimal shape mandate | `[const §X.3]` | §5 (entire), §10 row 4 |
| Hot-path allocation discipline | `[const §VIII.5]` | §4.2 traits requirements; §6.5 |
| Codex Gate A trigger (ABI-touching) | `[const §X.6]`, `[const §XVII.1]` | this doc requires Gate A before `/tasks` |
| Decimal extension-point decision | `[SYN §3.1 Q5]` | §1 goal 1, §2 non-goal 4, §4.4, §6.4 |
| Architectural inheritance | `[arch §4.1]`, `[arch §4.10]`, `[arch §5.3]`, `[arch §10]` | §3 |
| ABI versioning macros | `[const §X.1]`, `[arch §9.2]` | §5.1 |
| ABI surface diff (abidiff / structural) | `[const §IX.5]` | §5.3, §9 test seam 4 |
| C-ABI out-of-range code mapping | `[const §X.4]` | §7.4 |

Engineering-judgment decisions (max-serialized-bytes ceiling, `_reserved` byte policy, `INT64_MIN` mantissa sentinel, the cross-traits PoD-as-interchange rule, latency ceilings) cite `[SYN §3.1 Q5]` inline at point of use and are intentionally omitted from this appendix.

## Appendix C — Convergence log

### v0.2 → v0.3 (Codex Gate A round 2)

Input: Codex follow-up review `research/reviews/codex_2a_2_decimal_review.md` — 1 P1, 4 P2 findings on the v0.2 draft.

| Finding | Severity | Resolution |
|---|---|---|
| Codex 2nd-pass P1 — `FIXPP_DECIMAL_T` alias sentinel does not actually depend on the macro (macros do not expand inside identifiers, so every TU references the same symbol; the link-time mismatch detection is a no-op) | P1 | **Replaced identifier-pasting with template specialization** in §4.4. `decimal_alias_sentinel<T>::tag` is defined exactly once in `src/core/decimal.cpp` for the library's `FIXPP_DECIMAL_T`; a consumer built with a different alias references a specialization the library never defined → link fails. Inline comment in §4.4 calls out the v0.2 bug for future readers. Test seam #9 still applies. |
| Codex 2nd-pass P2 — §6.2 only rejects `exponent > 0`; `int8_t` admits `[-128, 127]`, so `exponent < -38` walks past the 41-byte ceiling | P2 | **Two-sided rejection in §6.2 pre-condition.** `to_chars` returns `error::decimal_invalid_input` for any exponent outside `[-38, 0]` *before* attempting to format. Without it, `{1, -128}` would emit 128 leading zeros and overflow caller buffers. Explicit rationale added pointing at the int8_t domain. |
| Codex 2nd-pass P2 — §6.3 takes `sign(INT64_MIN)` then `|INT64_MIN|`, which overflows; also contradicts the prose rule that invalid sorts greater than every finite value | P2 | **Added step 0 to the §6.3 algorithm**: invalid-sentinel check runs before `sign()`/canonicalize. `(invalid, invalid) == equal`, `(invalid, finite) == greater`, `(finite, invalid) == less`. Algorithm now matches the prose contract. |
| Codex 2nd-pass P2 — `is_lossless_for_fix_float` is value-lossless, not byte-equivalent; using it as a replay-fidelity gate is unsound (e.g., `+1.2300` re-formats as canonical `1.23`) | P2 | **Reframed §7.1**: replay = **raw frames** at MessageStore (decimal-trait-agnostic); the lossy-traits `static_assert` applies to a separate **typed-payload persistence** surface (SBE/Avro snapshots, control-plane decoded records). Q3 in §10 updated: confirmation now sits with **2e**, not **2b**. |
| Codex 2nd-pass P2 — `<fix/c_api.h>` declares `size_t` in prototypes but only includes `<stdint.h>`; pure-C consumers including the header standalone fail to compile | P2 | **Added `#include <stddef.h>`** to the §5.1 c_api.h block. |

**Net effect:** the v0.2 alias-sentinel safety net is now actually load-bearing; §6.2 and §6.3 close two unsound branches; the replay-fidelity rule is re-grounded on raw-frame storage where it belongs; the C header compiles standalone. v0.3 is the recommended pre-Gate-A-final draft.

### v0.1 → v0.2 (Codex Gate A round 1)

Inputs:
- Codex review: `research/reviews/codex_2a_decimal_review.md` — 4 P2 findings.
- Opus adversarial review: `research/reviews/opus_2a_decimal_adversarial_review.md` — 5 P1, 10 P2, 12 P3 (incorporates and escalates all 4 Codex findings).

| Finding | Severity | Resolution |
|---|---|---|
| Codex P2 #1 / Opus P1 #5 — `pod_decimal::operator==` does field equality, contradicts §6.3 | P1 | **Removed `operator==` and `operator<=>` from `pod_decimal`** (§4.1). Value comparison is via `decimal<>` only. |
| Codex P2 #2 / Opus P1 #1 — Worst-case bound 21 vs 41; 41 wrong if exponent ∈ [-38, +38] | P1 | **Restricted `pod_decimal::exponent ∈ [-38, 0]`** (§4.1). Worst case is now 41 bytes; both `fixpp_decimal_format` doc comment and `max_serialized_bytes` corrected (§5.2, §6.2). |
| Codex P2 #3 / Opus P1 #3 — `__int128` fallback overflows | P1 | **Replaced wide-compare with digit-string compare** (§6.3). O(digits), no overflow. Algorithm spelled out in pseudocode. |
| Codex P2 #4 / Opus P1 #4 — PMR `from_chars` overload incoherent | P1 | **PMR is required** in the trait signature (§4.2). Single-arg form removed. Wire layer always passes the arena (§7.1). |
| Opus P1 #2 — exponent domain not reconciled with parser | P1 | Resolved by P1 #1's domain restriction. Parser §6.1 now produces `exponent ≤ 0` by construction; serializer §6.2 explicitly rejects `exponent > 0` at the boundary. |
| Opus P2 #6 — `FIXPP_DECIMAL_T` ODR / chicken-and-egg | Accepted | Added `FIXPP_DECIMAL_USER_HEADER` injection hook + link-time sentinel symbol (§4.4). New test seam #9 verifies link-time mismatch fails. |
| Opus P2 #7 — no direct `T → U` cross-traits path | Accepted | Documented as v1.0 limitation (§2 non-goal, §6.4); deferred re-evaluation tracked in §10 Q1. |
| Opus P2 #8 — `equal()` redundant with `compare()` | Accepted | Dropped `equal` from required traits (§4.2). `decimal<T>::operator==` calls `compare(...) == 0` (§4.3). |
| Opus P2 #9 — coalesced C-ABI error code | Accepted | Split into three codes (§7.4): `FIXPP_ERR_DECIMAL_INVALID`, `FIXPP_ERR_DECIMAL_PRECISION_LOSS`, `FIXPP_ERR_BUFFER_TOO_SMALL`. |
| Opus P2 #10 — `_reserved` field declaration vs zero-on-read | Accepted | Kept as explicit field; engine **ignores** on read in v1.0; provided `FIXPP_DECIMAL_INITIALIZER` + `fixpp_decimal_init()` (§5.1, §5.2). New test seam #10. |
| Opus P2 #11 — `is_lossless_for_fix_float` overgeneralized | Accepted | Tightened language to "trait author's promise" framed in terms of round-trip under §6.3 (§4.2 note). |
| Opus P2 #12 — Goal 4 says "lossless" but §4.2 admits non-byte-equivalence | Accepted | Goal 4 reworded to "value-lossless" (§1). |
| Opus P2 #13 — int64 mantissa undersized for cumulative-money fields | Accepted | New §1.1 quantifies the magnitude domain (~9.22 × 10^10 with 8 fractional digits) and points to `decimal128` swap. |
| Opus P2 #14 — `noexcept` vs throwing third-party numeric types | Accepted | Explicit guidance + reference helper `fixpp::core::detail::trap_throw(...)` mentioned (§4.2 note). |
| Opus P2 #15 — generation counter / lifetime contract | Accepted | Explicit "parse consumes the span eagerly; result does not alias" sentence in §4.3. |
| Opus P3 #16 — Appendix A range A-001..A-014 should be A-001..A-034 | Accepted | Corrected (Appendix A). |
| Opus P3 #17 — §6.1 grammar edge cases | Accepted | Bare `.5` / `5.` rejected; `+0` / `-0` accepted; leading zeros accepted; SOH rejected (§6.1). |
| Opus P3 #18 — §6.2 positive-exponent serialization branch | Resolved by P1 #2 | No positive exponent at boundary; algorithm simplified. |
| Opus P3 #19 — "trivially copyable" only true for `pod_decimal` | Accepted | Re-qualified in §6.5. |
| Opus P3 #20 — v0.1 Q4 already decided in §5.1 | Accepted | Removed from §10 (now noted as DECIDED below the table). |
| Opus P3 #21 — test seam #2 wording "bytes don't compare under §6.3" | Accepted | Rewording in §9 seam #2. |
| Opus P3 #22 — hot-path enforcement Linux-only | Accepted | Documented Windows gap explicitly in §9 seam #6. |
| Opus P3 #23 — property-based comparison Tier 2 → Tier 1 | Accepted | Promoted (§9 seam #8). |
| Opus P3 #24 — no latency targets named | Accepted | Pinned 50 ns parse / 30 ns format / 20 ns compare on Linux/Clang/x86_64 warm-cache 5-digit-mantissa (§6.5); regression bar in §9 seam #5. Tracked as Q4 in §10 for revision after the wire integration in 2b. |
| Opus P3 #25 — `expected_t` not pre-aliased in §4.2 snippet | Accepted | Comment context added in §4.2 (assumed visible from `[arch §4.1]`); reader-friendly note. |
| Opus P3 #26 — missing C ABI conveniences | Accepted | Added `fixpp_decimal_equal()`, `fixpp_decimal_init()`, `FIXPP_DECIMAL_INITIALIZER`, `FIXPP_DECIMAL_INVALID` (§5.1, §5.2). |
| Opus P3 #27 — Q3 wide-compare framed as "not blocker" | Resolved by P1 #3 | Algorithm replaced; question OBSOLETED, removed from §10. |

**Net effect:** all 5 P1 / 10 P2 / 12 P3 findings resolved in v0.2. (v0.2 was then itself reviewed in Codex round 2, yielding the 5 follow-up findings handled in the v0.2 → v0.3 section above.)
