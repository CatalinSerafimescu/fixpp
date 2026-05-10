// decimal_traits.hpp — Phase 1 contract extract for /speckit-plan
//
// Feature:    001-core-decimal
// Owner:      fixpp::core
// Source:     .specify/2a-decimal.md §4  +  spec.md §4.1..§4.6
// Status:     reviewer-facing contract for Codex Gate A
//
// NOT a header to compile against. The real declarations land in
// include/fixpp/core/decimal.hpp during /implement. Local clang/clang-tidy
// diagnostics on this file are expected:
//   - `std::expected` requires C++23 (the project standard per [const §II §1]);
//     the locally-installed clang in the contracts path may not have it
//     configured.
//   - Snake_case identifier conventions (pod_decimal, decimal_error::*,
//     decimal_alias_sentinel) follow the project naming rules and disagree
//     with clang-tidy's stock readability-identifier-naming config.
// Both are environmental, not real defects. NOLINTBEGIN suppresses the noise.

// NOLINTBEGIN(readability-identifier-naming,modernize-use-nodiscard,no_member_template,no_template,undeclared_var_use)

#pragma once

#include <cstdint>
#include <span>
#include <expected>          // std::expected (C++23)
#include <compare>           // std::strong_ordering
#include <type_traits>       // std::is_same_v, std::conditional_t
#include <memory_resource>   // std::pmr::memory_resource

namespace fixpp::core {

// ─────────────────────────────────────────────────────────────────────────────
// 1. Default representation: pod_decimal
// ─────────────────────────────────────────────────────────────────────────────

struct pod_decimal {
    std::int64_t mantissa;   // Reserved: INT64_MIN is the invalid sentinel.
    std::int8_t  exponent;   // Canonical domain: [-38, 0].

    // No operator==, no operator<=>, no inline arithmetic.
    // Value comparison goes through decimal<pod_decimal>::compare (delegates
    // to decimal_traits<pod_decimal>::compare per §6.3). Defaulting equality
    // here would silently field-compare {1,0} and {10,-1} as unequal.
};

inline constexpr pod_decimal pod_decimal_invalid { /*mantissa=*/INT64_MIN,
                                                   /*exponent=*/0 };

// ─────────────────────────────────────────────────────────────────────────────
// 2. Error type
// ─────────────────────────────────────────────────────────────────────────────

enum class decimal_error : std::uint8_t {
    invalid_input    = 1,   // empty, non-digit bytes, malformed (.5 / 5.), sentinel collision
    overflow         = 2,   // mantissa overflow OR exponent < -38
    precision_loss   = 3,   // cross-traits narrowing: out-of-PoD-domain → narrower T
    buffer_too_small = 4,   // to_chars dst < required bytes
};

template <class T>
using expected_t = std::expected<T, decimal_error>;

// ─────────────────────────────────────────────────────────────────────────────
// 3. The customization point: decimal_traits<T>
// ─────────────────────────────────────────────────────────────────────────────

template <class T>
struct decimal_traits;          // primary template — undefined; user must specialize.

// Required surface of every specialization (member types, constants, statics):
//
//   using value_type = T;
//
//   static constexpr bool        is_lossless_for_fix_float = ...;   // §7.4
//   static constexpr std::size_t max_serialized_bytes      = ...;   // upper bound
//
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
//   // Conversion to/from the canonical PoD interchange form. Out-of-domain
//   // input → decimal_error::overflow.
//   static expected_t<T>           from_pod(pod_decimal) noexcept;
//   static expected_t<pod_decimal> to_pod  (T const&) noexcept;
//
//   // Total ordering by VALUE (decimal value, not encoded representation).
//   // {1,0}, {10,-1}, {100,-2} all compare equal.
//   static std::strong_ordering compare(T const&, T const&) noexcept;
//
//   // Predicates the wire layer relies on.
//   static bool is_finite  (T const&) noexcept;
//   static bool is_zero    (T const&) noexcept;
//   static bool is_negative(T const&) noexcept;

// Specialization shipped in this feature:
template <>
struct decimal_traits<pod_decimal>;       // defined in decimal.hpp / decimal.cpp

// ─────────────────────────────────────────────────────────────────────────────
// 4. The value wrapper: decimal<T>
// ─────────────────────────────────────────────────────────────────────────────

template <class T>
class decimal {
public:
    using value_type  = T;
    using traits_type = decimal_traits<T>;

    constexpr explicit decimal(T value) noexcept : value_(value) {}

    constexpr T const& value() const noexcept { return value_; }

    // Conversion to canonical PoD interchange form.
    constexpr expected_t<pod_decimal> to_pod() const noexcept {
        return traits_type::to_pod(value_);
    }

    // Conversion from canonical PoD interchange form.
    static constexpr expected_t<decimal<T>> from_pod(pod_decimal pod) noexcept {
        return traits_type::from_pod(pod).transform(
            [](T v) { return decimal<T>{ v }; });
    }

    // Cross-traits conversion. AC-X1..X3:
    //   T == U: compile-time short-circuit, returns source unchanged
    //   T != U: funnel through pod_decimal as canonical interchange
    //
    // Return shape differs by branch:
    //   T == U: decimal<U>          (no error possible — round-trip identity)
    //   T != U: expected_t<decimal<U>>
    template <class U>
    constexpr auto to() const noexcept
        -> std::conditional_t<std::is_same_v<T, U>,
                              decimal<U>,
                              expected_t<decimal<U>>>
    {
        if constexpr (std::is_same_v<T, U>) {
            // AC-X3: short-circuit, no funnel. Round-trip identity holds
            // unconditionally — including for source values outside the
            // pod_decimal domain.
            return *this;
        } else {
            // AC-X1: funnel via pod_decimal. AC-X2: out-of-PoD-domain → precision_loss.
            return to_pod().and_then([](pod_decimal pod) {
                return decimal<U>::from_pod(pod);
            });
        }
    }

private:
    T value_;
};

// ─────────────────────────────────────────────────────────────────────────────
// 5. Build-time alias machinery (full detail in decimal_alias.hpp)
// ─────────────────────────────────────────────────────────────────────────────

// Link-time mismatch sentinel — definition lives in src/core/decimal.cpp for
// the chosen FIXPP_DECIMAL_T only. Two TUs with conflicting FIXPP_DECIMAL_T
// link with one tag definition each → multiply-defined-symbol error
// (or unresolved-symbol for header-only mismatches).
template <class T>
struct decimal_alias_sentinel {
    static int const tag;
};

}  // namespace fixpp::core

// FIXPP_DECIMAL_T defaults to ::fixpp::core::pod_decimal unless the user
// overrides it at build time (-DFIXPP_DECIMAL_T=...). Consumer-supplied
// FIXPP_DECIMAL_USER_HEADER (if any) is #include'd before the alias macro
// is consulted. See decimal_alias.hpp in /implement for the full machinery.
#ifndef FIXPP_DECIMAL_T
    #define FIXPP_DECIMAL_T  ::fixpp::core::pod_decimal
#endif

namespace fixpp::core {
    using decimal_t = decimal<FIXPP_DECIMAL_T>;
}

namespace fixpp {
    using decimal_t = ::fixpp::core::decimal_t;     // engine-wide alias
}
