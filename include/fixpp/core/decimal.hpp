#pragma once
// include/fixpp/core/decimal.hpp
// pod_decimal, decimal_traits<T>, decimal<T> — literal extract from 2a-decimal.md v0.3
// §4.1, §4.2, §4.3. See contracts/decimal_traits.hpp for the full contract extract.

#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory_resource>
#include <span>
#include <type_traits>
#include <utility>

#include "fixpp/core/error.hpp"

namespace fixpp::core {

// ── §4.1 — Default representation: pod_decimal ──────────────────────────────
// extract from 2a-decimal.md v0.3 §4.1

struct pod_decimal {
    std::int64_t mantissa;  // INT64_MIN is the invalid sentinel
    std::int8_t exponent;   // canonical domain: [-38, 0]

    // No operator==, no operator<=>. Value comparison goes through decimal<pod_decimal>
    // which delegates to the canonicalizing decimal_traits<pod_decimal>::compare.
};

inline constexpr pod_decimal pod_decimal_invalid{.mantissa = INT64_MIN, .exponent = 0};

// ── §4.2 — decimal_traits<T> customization point ────────────────────────────
// Primary template — undefined; users must specialize per T.

template <class T>
struct decimal_traits;

// ── §4.3 — decimal<T> value type ────────────────────────────────────────────
// extract from 2a-decimal.md v0.3 §4.3

template <class T = pod_decimal>
class decimal {
public:
    using traits_type = decimal_traits<T>;
    using value_type = T;

    constexpr decimal() noexcept = default;
    constexpr explicit decimal(T v) noexcept : value_(std::move(v)) {}

    [[nodiscard]] constexpr T const& value() const noexcept { return value_; }

    // Generic bodies: delegate to traits_type. Defining inline in the primary
    // template (rather than via per-T explicit specialization) keeps the
    // alias-swappable API live for any T whose decimal_traits<T> provides
    // from_chars/to_chars — see 2a-decimal.md §4.3-§4.4 (Gate B P1 #2).
    [[nodiscard]] static expected_t<decimal> parse(std::span<const std::byte> src,
                                                   std::pmr::memory_resource* mr) noexcept {
        auto r = traits_type::from_chars(src, mr);
        if (!r) {
            return std::unexpected{r.error()};
        }
        return decimal{*r};
    }
    [[nodiscard]] expected_t<std::size_t> format(std::span<std::byte> dst) const noexcept {
        return traits_type::to_chars(value_, dst);
    }

    // Cross-traits conversion — funnels through pod_decimal for T≠U (T039/T040).
    // T==U path short-circuits via if constexpr — no funnel, no error (AC-X3, D-11).
    // Requires decimal_traits<U> to be a complete specialization at instantiation.
    template <class U>
    static expected_t<decimal> from(decimal<U> const& src) noexcept {
        static_assert(
            requires { decimal_traits<U>::to_pod; },
            "decimal::from<U>() requires decimal_traits<U> to be specialized. "
            "Ensure decimal_traits<U>::from_pod and ::to_pod are defined.");
        if constexpr (std::is_same_v<T, U>) {
            return decimal{src.value()};
        } else {
            auto pod = decimal_traits<U>::to_pod(src.value());
            if (!pod) {
                // Remap decimal_overflow → decimal_precision_loss at the cross-traits
                // boundary per 2a §6.4: "values that don't fit … produce
                // error::decimal_precision_loss". The source type's to_pod returns
                // decimal_overflow when the value exceeds the pod_decimal domain; the
                // wrapper presents this as precision loss to callers.
                return std::unexpected{pod.error() == error::decimal_overflow
                                           ? error::decimal_precision_loss
                                           : pod.error()};
            }
            auto result = decimal_traits<T>::from_pod(*pod);
            if (!result) {
                return std::unexpected{result.error()};
            }
            return decimal{*result};
        }
    }
    template <class U>
    expected_t<decimal<U>> to() const noexcept {
        static_assert(
            requires { decimal_traits<U>::from_pod; },
            "decimal::to<U>() requires decimal_traits<U> to be specialized. "
            "Ensure decimal_traits<U>::from_pod and ::to_pod are defined.");
        if constexpr (std::is_same_v<T, U>) {
            return decimal<U>{value_};
        } else {
            auto pod = decimal_traits<T>::to_pod(value_);
            if (!pod) {
                // Remap decimal_overflow → decimal_precision_loss at the cross-traits
                // boundary per 2a §6.4.
                return std::unexpected{pod.error() == error::decimal_overflow
                                           ? error::decimal_precision_loss
                                           : pod.error()};
            }
            auto result = decimal_traits<U>::from_pod(*pod);
            if (!result) {
                return std::unexpected{result.error()};
            }
            return decimal<U>{*result};
        }
    }

    friend bool operator==(decimal const& a, decimal const& b) noexcept {
        return traits_type::compare(a.value_, b.value_) == 0;
    }
    friend std::strong_ordering operator<=>(decimal const& a, decimal const& b) noexcept {
        return traits_type::compare(a.value_, b.value_);
    }

private:
    T value_{};
};

using decimal_default = decimal<pod_decimal>;

// ── decimal_traits<pod_decimal> specialization declaration ──────────────────
// Full definition in src/core/decimal.cpp.

template <>
struct decimal_traits<pod_decimal> {
    using value_type = pod_decimal;
    static constexpr bool is_lossless_for_fix_float = true;
    static constexpr std::size_t max_serialized_bytes = 41;

    static expected_t<pod_decimal> from_chars(std::span<const std::byte> src,
                                              std::pmr::memory_resource* mr) noexcept;
    static expected_t<std::size_t> to_chars(pod_decimal const& v,
                                            std::span<std::byte> dst) noexcept;
    static expected_t<pod_decimal> from_pod(pod_decimal pd) noexcept;
    static expected_t<pod_decimal> to_pod(pod_decimal const& v) noexcept;
    static std::strong_ordering compare(pod_decimal const& a, pod_decimal const& b) noexcept;
    static bool is_finite(pod_decimal const& v) noexcept;
    // Callers MUST gate validity with is_finite FIRST before trusting is_zero/is_negative
    // on a possibly-invalid decimal. In particular the invalid sentinel (INT64_MIN)
    // reports is_negative()==true, yet compare() orders it strictly greater than every
    // finite value (B-001-3) — a sign check on an unvalidated decimal disagrees with its
    // ordering. (is_zero is mantissa==0 and stays consistent with that ordering.)
    static bool is_zero(pod_decimal const& v) noexcept;
    static bool is_negative(pod_decimal const& v) noexcept;
};

}  // namespace fixpp::core
