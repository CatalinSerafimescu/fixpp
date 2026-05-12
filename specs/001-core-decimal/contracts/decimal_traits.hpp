// ============================================================================
// include/fixpp/core/decimal.hpp + decimal_alias.hpp + decimal_helpers.hpp
// Phase 4 / 001-core-decimal — C++ contract extract.
//
// This file is a /plan contract extract, NOT the final installed header. The
// final headers land at the paths named in the section dividers during
// /implement; this single extract is the contract that the implementation must
// match shape-for-shape on the C++ public surface.
//
// EVERY block below is a literal copy-paste from .specify/2a-decimal.md v0.3
// (Gate A round 2 converged, signed off 2026-05-07). Where a block adds
// Phase-4-only commentary, it is clearly marked.
//
// Reviewers verifying inheritance fidelity: diff each block below against the
// cited 2a-decimal.md section. Drift = round-1-style memory-of-2a failure.
//
// Citations use canonical [const §Roman.arabic] form per constitution.md:5.
//
// Constitution refs: [const §II.1] (C++23 only);
//                    [const §VIII.5] (zero-alloc hot path);
//                    [const §X.3] (decimal at C-ABI is PoD);
//                    [const §XV.1] (banned: heap-allocate on hot path).
//
// Architecture refs: [arch §4.1] (core surface + expected_t<T> alias);
//                    [arch §4.10] (C-ABI shape);
//                    [arch §5.3] (error model);
//                    [arch §5.5] (lifetime model).
// ============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <compare>
#include <expected>
#include <memory_resource>
#include <span>
#include <type_traits>
#include <utility>

#include "fixpp/core/error.hpp"   // declares fixpp::core::error + fixpp::core::expected_t<T>
                                  // (owned by 2k; this feature contributes 4 named variants per
                                  //  data-model.md Entity 5 and 2a v0.3 §7.4)

namespace fixpp::core {

// ── expected_t<T> alias ─────────────────────────────────────────────────────
//
// extract from .specify/architecture.md §4.1 (carried into 2a-decimal.md v0.3
// implicitly via every expected_t<T> signature below). Declared here as a
// reminder for the reviewer; the canonical declaration lives in
// fixpp/core/error.hpp owned by 2k.
//
// using expected_t<T> = std::expected<T, fixpp::core::error>;
//
// NOT a local decimal_error enum. NOT a re-bound alias. Per data-model.md
// Entity 5 + research.md D-8.

// ════════════════════════════════════════════════════════════════════════════
// § Target file: include/fixpp/core/decimal.hpp
// ════════════════════════════════════════════════════════════════════════════

// ── §4.1 — Default representation: pod_decimal ──────────────────────────────
// extract from .specify/2a-decimal.md v0.3 §4.1 (lines 53–72).

struct pod_decimal {
    std::int64_t mantissa;   // significand. Reserved: INT64_MIN is the invalid sentinel.
    std::int8_t  exponent;   // power of 10; value = mantissa * 10^exponent.
                             // Canonical domain: exponent ∈ [-38, 0]. Anything outside
                             // is rejected at the C-ABI boundary and at trait conversion.

    // No operator==, no operator<=>. Value comparison goes through decimal<pod_decimal>
    // (§4.3), which delegates to the canonicalizing traits (§6.3). Defaulting equality
    // here would silently field-compare {1,0} and {10,-1} as unequal, contradicting
    // the value-equality contract.
};

inline constexpr pod_decimal pod_decimal_invalid {INT64_MIN, 0};

// ── §4.2 — The decimal_traits<T> customization point ────────────────────────
// extract from .specify/2a-decimal.md v0.3 §4.2 (lines 80–127).
//
// PRIMARY template — undefined; users specialize per T. The library ships
// exactly one specialization in v1.0: decimal_traits<pod_decimal> (in
// src/core/decimal.cpp).

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
//
// Notes (from 2a §4.2):
// - PMR is required (not optional). Non-allocating traits ignore the resource
//   pointer; allocating traits use it. The wire layer always passes the
//   per-message arena [arch §5.2]. (Round-1 had a draft that allowed an
//   optional PMR overload; rejected by Codex P2 #4 + Opus P1 #4 in 2a's own
//   Gate A history.)
// - equal derived from compare; carrying two functions for the same predicate
//   invited inconsistent specializations.
// - is_lossless_for_fix_float is a trait author's promise — "for every input
//   string s accepted by from_chars (i.e., FIX-valid per §6.1), the resulting
//   T round-trips through to_chars to a byte sequence whose from_chars re-
//   parse compares value-equal under compare (§6.3)." Value-lossless, not
//   byte-equivalent.
// - Throwing third-party libraries (e.g., cpp_dec_float, mpfr) MUST wrap calls
//   in try/catch and translate to expected_t errors via
//   fixpp::core::detail::trap_throw(...) (helper below in
//   decimal_helpers.hpp). Otherwise an escaping exception terminates the
//   program at the noexcept boundary.
// - Forward-compat: new required members may only be added in a minor library
//   version under a feature-test macro FIXPP_DECIMAL_TRAITS_FEATURE_<NAME>.

// ── §4.3 — The decimal<T> value type ────────────────────────────────────────
// extract from .specify/2a-decimal.md v0.3 §4.3 (lines 131–171).

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
    //
    // For T == U, the IMPLEMENTATION short-circuits via `if constexpr`
    // (std::is_same_v<T,U>) and returns the source unchanged — no funnel, no
    // error, no codegen overhead (spec.md /clarify 2026-05-10, AC-X3). The
    // RETURN-TYPE SHAPE is uniform expected_t<decimal<U>> regardless — the
    // short-circuit is an implementation choice, not an API shape change.
    // (Research.md D-11; round-1's conditional_t<...> return-type carve was
    // rejected as silent ABI redesign.)
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

// ════════════════════════════════════════════════════════════════════════════
// § Target file: include/fixpp/core/decimal_alias.hpp
// ════════════════════════════════════════════════════════════════════════════

// ── §4.4 — Build-time selection: FIXPP_DECIMAL_T ────────────────────────────
// extract from .specify/2a-decimal.md v0.3 §4.4 (lines 180–214).

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
// (The v0.2 design pasted the macro name into an identifier, which is a no-op
// — macros do not expand inside identifiers — so every TU used the same
// symbol regardless of the chosen alias. The current shape (template
// specialization on T) makes the link guard actually load-bearing.)
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

// ════════════════════════════════════════════════════════════════════════════
// § Target file: include/fixpp/core/decimal_helpers.hpp
// ════════════════════════════════════════════════════════════════════════════

// ── trap_throw helper for throwing-third-party-library traits ──────────────
// extract from .specify/2a-decimal.md v0.3 §4.2 note (lines 124–126).
//
// Trait authors wrapping libraries that throw (e.g., cpp_dec_float, mpfr)
// MUST use this to translate exceptions into expected_t errors at the
// noexcept boundary. An escaping exception across a noexcept function
// terminates the program.

namespace fixpp::core::detail {

template <class F>
constexpr auto trap_throw(F&& fn) noexcept
    -> fixpp::core::expected_t<std::invoke_result_t<F>>;
//   {
//       try { return std::invoke(std::forward<F>(fn)); }
//       catch (std::bad_alloc const&) { return std::unexpected{fixpp::core::error::out_of_memory}; }
//       catch (...)                   { return std::unexpected{fixpp::core::error::decimal_invalid_input}; }
//   }
// (Body is implementation-only; declaration here for the contract reviewer.)

}  // namespace fixpp::core::detail
