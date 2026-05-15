#pragma once
// include/fixpp/core/decimal_helpers.hpp
// detail::trap_throw — wraps throwing third-party-library calls at the noexcept boundary.
// Trait authors using libraries that may throw (e.g., boost::multiprecision, mpfr)
// MUST use this helper to translate exceptions into expected_t errors.

#include <expected>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

#include "fixpp/core/error.hpp"

namespace fixpp::core::detail {

template <class F>
constexpr auto trap_throw(F&& fn) noexcept -> fixpp::core::expected_t<std::invoke_result_t<F>> {
    try {
        return std::invoke(std::forward<F>(fn));
    } catch (std::bad_alloc const&) {
        return std::unexpected{fixpp::core::error::out_of_memory};
    } catch (...) {
        return std::unexpected{fixpp::core::error::decimal_invalid_input};
    }
}

// trap_throw_or_throw — exception-API analogue of `trap_throw` for the
// `[arch §5.3]` construction-time carve-out. Used by `XmlLoader::load*` per
// 002-dictionary-xml-loader research.md D-3 / D-5: PMR allocation failure
// (std::bad_alloc) is re-thrown as `E{}` (typically `dict::xml_oom_error`);
// any other exception is rethrown unchanged so the loader's
// `dict::xml_parse_error` / `dict::unknown_version_error` paths propagate.
//
// Semantics:
//   - If `fn()` returns normally → return its result.
//   - If `fn()` throws `std::bad_alloc` (or derived) → throw `E{}`.
//   - If `fn()` throws anything else → rethrow unchanged.
//
// NOT marked `noexcept` (it intentionally throws).
//
// Requires: `E` is default-constructible. The catch site constructs `E{}`,
// so a non-default-constructible exception type fails to compile *here*, not
// at the call site (cleaner diagnostic). The current consumer
// (`dict::xml_oom_error{}`) is no-arg constructible per `[2c §4.5]`.
template <class E, class F>
auto trap_throw_or_throw(F&& fn) -> std::invoke_result_t<F> {
    static_assert(std::is_default_constructible_v<E>,
                  "trap_throw_or_throw<E,F>: E must be default-constructible");
    try {
        return std::invoke(std::forward<F>(fn));
    } catch (std::bad_alloc const&) {
        throw E{};
    }
}

}  // namespace fixpp::core::detail
