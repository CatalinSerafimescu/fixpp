// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/fuzz/fuzz_wire_validator.cpp
//
// T026 — Seam #11 extension — libFuzzer harness for dictionary_driven_validator.
//
// 041-validation-gate-wiring ends the feature-004 "US4 PAUSED" deferral.
// Feeds arbitrary bytes → frame factory → Parser<Index>::parse → on a
// successfully parsed MessageView, runs dictionary_driven_validator::validate().
//
// Invariants asserted by this harness:
//   - No crash/OOB (ASan catches any out-of-bounds read/write).
//   - No UB (UBSan catches signed overflow, misalignment, null deref, etc.).
//   - No exception escapes the noexcept validate() boundary (libFuzzer would
//     catch any std::terminate() call).
//   - Every rejection returned by validate() is a defined wire_* error slot —
//     never a raw decimal_* error. This directly witnesses the T009a remap:
//     hostile Float-field bytes must emerge as wire_field_value_out_of_range,
//     not decimal_invalid_input or decimal_overflow.
//
// The dictionary_driven_validator is constructed ONCE at static scope from a
// real Dictionary-backed table_view (T008 / Dictionary::as_table_view()), so
// no allocation occurs on the per-input hot path. The per-input parse arena
// and validate scratch MR are stack-allocated (mirror of fuzz_wire_parser).
//
// Campaign note (T026 / 041): A full ≥10-min Tier-1 ASan+UBSan campaign is
// the CI responsibility. The in-PR campaign used -max_total_time=60 under
// -fsanitize=fuzzer,address,undefined; zero crashes/violations found.

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>
#include <memory_resource>
#include <span>

// Production table_view (T009 — complete-type include, not the mock).
// validator.hpp already includes table_view.hpp transitively; listed here for
// clarity.
#include <fixpp/dict/table_view.hpp>

// Test-only frame factory (friend of frame_view).
#include "support/frame_view_factory.hpp"
// Richer FIX 4.2 validation dictionary (Logon/Heartbeat/NewOrderSingle,
// typed fields incl. INT + FLOAT — exercises all check_field_type arms).
#include "support/validation_test_dictionary.hpp"

namespace {

// Build the validator ONCE: construct the dictionary, call as_table_view()
// to materialise the owned tables, then build the validator from the view.
// The dictionary shared_ptr may be released after as_table_view() returns
// because table_view owns its data by value (std::vector / unordered_map).
//
// Lambda-init static: thread-safe under C++11 (§6.7 of the standard); the
// fuzzer runs single-threaded per worker, and the static is read-only after
// the first call.
fixpp::wire::dictionary_driven_validator const& get_validator() {
    static const fixpp::wire::dictionary_driven_validator validator = [] {
        auto dict = fixpp::test_support::make_validation_test_dictionary();
        return fixpp::wire::dictionary_driven_validator{dict->as_table_view()};
    }();
    return validator;
}

// Allowlist of error codes that dictionary_driven_validator::validate() is
// permitted to return. Any other code is a T009a-remap violation: a raw
// decimal_* error escaped the validate() noexcept boundary.
bool is_valid_wire_error(fixpp::core::error e) noexcept {
    using fixpp::core::error;
    switch (e) {
        case error::wire_header_out_of_order:       // reason 14
        case error::wire_unexpected_tag:            // reason 2
        case error::wire_required_field_missing:    // reason 1
        case error::wire_field_value_out_of_range:  // reason 5
        case error::wire_field_value_truncated:     // reason 6
            return true;
        default:
            return false;
    }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    using fixpp::wire::access_mode;
    using fixpp::wire::frame_view;
    using fixpp::wire::Parser;

    // Initialise the static validator on the first call (no allocation on
    // subsequent calls).
    auto const& validator = get_validator();

    // Wrap the fuzzer input as a frame_view using the test factory.
    // If the bytes lack "9=" / "10=" structural markers, the factory returns
    // a wire_* error and we skip both parse and validate (still exercises the
    // factory's error paths).
    auto buf = std::span<const std::byte>{reinterpret_cast<const std::byte*>(data), size};
    auto fv_or_err = fixpp::wire::test::make_frame_view(buf);

    if (!fv_or_err) {
        return 0;
    }

    frame_view const& fv = *fv_or_err;

    // Per-input parse arena (stack-backed; null_memory_resource as upstream
    // so any overflow hard-fails rather than falling back to the heap).
    std::array<std::byte, 256 * 1024> arena_buf{};
    std::pmr::monotonic_buffer_resource parse_arena{arena_buf.data(), arena_buf.size(),
                                                    std::pmr::null_memory_resource()};

    // Parser needs a table_view by const-ref; use an empty one (the real
    // dict feeds the validator, not the parser).
    fixpp::dict::table_view empty_tv{};
    Parser<access_mode::Index> p{empty_tv};

    // parse() is noexcept; any exception → std::terminate → libFuzzer crash report.
    auto parse_result = p.parse(fv, &parse_arena);

    if (!parse_result) {
        // Malformed input: parser returned a wire_* error — correct behaviour.
        return 0;
    }

    auto const& mv = *parse_result;

    // Per-input scratch MR for the validator's Float/decimal parse path.
    // Small stack allocation is sufficient (the decimal parser needs < 512 B).
    std::array<std::byte, 4 * 1024> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};

    // validate() is noexcept. Any exception escape → std::terminate → crash.
    auto validate_result = validator.validate(mv, &scratch_mr, nullptr);

    if (!validate_result) {
        // Invariant: every rejection must be one of the five wire_* slots.
        // A raw decimal_* slot here means the T009a remap is broken.
        if (!is_valid_wire_error(validate_result.error())) {
            __builtin_trap();
        }
    }

    return 0;
}
