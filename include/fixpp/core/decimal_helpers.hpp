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

}  // namespace fixpp::core::detail
