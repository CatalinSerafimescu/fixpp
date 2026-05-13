// src/capi/decimal.cpp
// C-ABI boundary function implementations — US3 T034 + T035.
// Thin thunks over decimal_traits<pod_decimal>; also implements _checked siblings.

#include "fix/c_api/decimal.h"

#include <cstddef>
#include <cstring>
#include <memory_resource>
#include <span>

#include "fixpp/core/decimal.hpp"
#include "fixpp/core/error.hpp"

using fixpp::core::decimal_traits;
using fixpp::core::error;
using fixpp::core::pod_decimal;

namespace {

// Map fixpp::core::error to FIXPP_ERR_* codes.
fixpp_error_t map_error(error e) noexcept {
    switch (e) {
        case error::decimal_invalid_input:
        case error::decimal_overflow:
            return FIXPP_ERR_DECIMAL_INVALID;
        case error::decimal_precision_loss:
            return FIXPP_ERR_DECIMAL_PRECISION_LOSS;
        case error::decimal_buffer_too_small:
            return FIXPP_ERR_BUFFER_TOO_SMALL;
        default:
            return FIXPP_ERR_UNKNOWN;
    }
}

// Lift a fixpp_decimal_t into a pod_decimal through the trait boundary per 2a §5.2.
// Returns an error if the value is out of the canonical domain [-38, 0] per 2a §4.2.
fixpp::core::expected_t<pod_decimal> pod_from_cabi(fixpp_decimal_t d) noexcept {
    return decimal_traits<pod_decimal>::from_pod(
        pod_decimal{.mantissa = d.mantissa, .exponent = d.exponent});
}

bool in_canonical_domain(fixpp_decimal_t d) noexcept {
    return d.exponent >= -38 && d.exponent <= 0;
}

}  // namespace

extern "C" {

// T034: parse — AC-A4: _reserved ignored on read
fixpp_error_t fixpp_decimal_parse(const char* src, size_t src_len, fixpp_decimal_t* out) {
    if (src == nullptr || out == nullptr) {
        return FIXPP_ERR_DECIMAL_INVALID;
    }
    auto r = decimal_traits<pod_decimal>::from_chars(
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(src), src_len},
        std::pmr::null_memory_resource());
    if (!r.has_value()) {
        return map_error(r.error());
    }
    // Route through to_pod to enforce canonical domain per 2a §5.2 / §4.2.
    auto pod = decimal_traits<pod_decimal>::to_pod(*r);
    if (!pod.has_value()) {
        return map_error(pod.error());
    }
    out->mantissa = pod->mantissa;
    out->exponent = pod->exponent;
    std::memset(static_cast<void*>(out->_reserved), 0, sizeof(out->_reserved));  // AC-A5
    return FIXPP_ERR_OK;
}

// T034: format — applies AC-S3 exponent pre-check via from_pod per 2a §5.2;
// _reserved ignored on read (AC-A4)
fixpp_error_t fixpp_decimal_format(fixpp_decimal_t d, char* dst, size_t dst_cap, size_t* written) {
    if (dst == nullptr || written == nullptr) {
        return FIXPP_ERR_DECIMAL_INVALID;
    }
    // Route through from_pod to enforce canonical domain per 2a §5.2 / §4.2.
    auto v = pod_from_cabi(d);
    if (!v.has_value()) {
        return map_error(v.error());
    }
    auto r = decimal_traits<pod_decimal>::to_chars(
        *v, std::span<std::byte>{reinterpret_cast<std::byte*>(dst), dst_cap});
    if (!r.has_value()) {
        return map_error(r.error());
    }
    *written = *r;
    return FIXPP_ERR_OK;
}

// T035: bare compare — assumes canonical domain (D-4 / 2a §5.2).
// Routes through from_pod per 2a §5.2; out-of-domain inputs return 0 (sentinel).
// C-ABI signature frozen at FIXPP_C_ABI_VERSION_MAJOR == 1 [const §X.1]; semantic
// order is intentional — the lint below is not actionable on a frozen surface.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int fixpp_decimal_compare(fixpp_decimal_t a, fixpp_decimal_t b) {
    auto va = pod_from_cabi(a);
    auto vb = pod_from_cabi(b);
    if (!va.has_value() || !vb.has_value()) {
        return 0;  // out-of-domain → sentinel compare result
    }
    auto cmp = decimal_traits<pod_decimal>::compare(*va, *vb);
    if (cmp == std::strong_ordering::less) {
        return -1;
    }
    if (cmp == std::strong_ordering::greater) {
        return 1;
    }
    return 0;
}

// T035: bare equal — assumes canonical domain.
// Routes through from_pod per 2a §5.2; out-of-domain inputs return 0 (not equal).
// C-ABI signature frozen at FIXPP_C_ABI_VERSION_MAJOR == 1 [const §X.1]; equality
// is symmetric by definition — the lint below is not actionable on a frozen surface.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int fixpp_decimal_equal(fixpp_decimal_t a, fixpp_decimal_t b) {
    auto va = pod_from_cabi(a);
    auto vb = pod_from_cabi(b);
    if (!va.has_value() || !vb.has_value()) {
        return 0;  // out-of-domain → not equal
    }
    auto cmp = decimal_traits<pod_decimal>::compare(*va, *vb);
    return cmp == std::strong_ordering::equal ? 1 : 0;
}

// T034: init — zero-fills _reserved for forward-compat
void fixpp_decimal_init(fixpp_decimal_t* out) {
    if (out == nullptr) {
        return;
    }
    out->mantissa = 0;
    out->exponent = 0;
    std::memset(static_cast<void*>(out->_reserved), 0, sizeof(out->_reserved));
}

// T035: _checked compare — AC-C6 defensive validation
fixpp_error_t fixpp_decimal_compare_checked(fixpp_decimal_t a, fixpp_decimal_t b,
                                            int* out_ordering) {
    if (out_ordering == nullptr) {
        return FIXPP_ERR_DECIMAL_INVALID;
    }
    if (!in_canonical_domain(a) || !in_canonical_domain(b)) {
        return FIXPP_ERR_DECIMAL_INVALID;
    }
    *out_ordering = fixpp_decimal_compare(a, b);
    return FIXPP_ERR_OK;
}

// T035: _checked equal — AC-C6 defensive validation
fixpp_error_t fixpp_decimal_equal_checked(fixpp_decimal_t a, fixpp_decimal_t b, int* out_equal) {
    if (out_equal == nullptr) {
        return FIXPP_ERR_DECIMAL_INVALID;
    }
    if (!in_canonical_domain(a) || !in_canonical_domain(b)) {
        return FIXPP_ERR_DECIMAL_INVALID;
    }
    *out_equal = fixpp_decimal_equal(a, b);
    return FIXPP_ERR_OK;
}

}  // extern "C"
