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
//
// 092-garbled-frame-reject (specs/092-garbled-frame-reject/tasks.md T067; research.md
// R-7 "Fuzz"; data-model.md E-4): before the frame factory runs, every input is also
// walked whole by MessageView<Index>::field_iterator and built by OffsetTable::build
// under the same hooks: the validation dictionary's, a dictionary declaring a
// dictionary-only Length+Data pair, and dict_hooks::none(). Both directions trap:
// an encoding failure of the build with fault() == none, and a successful build
// with fault() != none. Only a resource-failure build status is skipped, and those
// are counted (fuzz_092_support.hpp).

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/dict_hooks.hpp>
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/tag_scan.hpp>
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
// 092: the dictionary-only pair fixture and the skip counter.
#include "fuzz_092_support.hpp"

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
        case error::wire_invalid_tag_number:        // reason 0
        case error::wire_length_data_mismatch:      // reason 5
            return true;
        default:
            return false;
    }
}

// 092 R-7: one (input, hooks) comparison of the iterator's fault record with
// OffsetTable::build.
void check_iterator_fault_agrees_with_build(std::span<const std::byte> buf,
                                            fixpp::wire::dict_hooks const& hooks) {
    using iter_t = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>::field_iterator;
    auto& counts = fixpp::fuzz092::counter("R-7 field_iterator vs OffsetTable::build");
    ++counts.cases;
    std::pmr::monotonic_buffer_resource arena;  // heap upstream: no artificial out_of_memory
    auto const fv = fixpp::wire::frame_view_slice_access::make(buf.data(), buf.size(), {});
    fixpp::wire::OffsetTable const table(fv, &arena, hooks);
    auto const status = table.build_status();
    if (fixpp::fuzz092::is_resource_failure(status)) {
        ++counts.skipped;
        return;
    }
    iter_t it{buf, 0, hooks};
    iter_t const end{buf, buf.size(), hooks};
    while (!(it == end)) {
        ++it;
    }
    bool const faulted = it.fault() != fixpp::wire::field_fault::none;
    if (!status.has_value() && !faulted) {
        __builtin_trap();  // an encoding failure the iterator did not report
    }
    if (status.has_value() && faulted) {
        __builtin_trap();  // a spurious fault on a frame the build accepts
    }
}

void check_iterator_fault_agrees_with_build(std::span<const std::byte> buf) {
    static fixpp::dict::table_view const validation_tv =
        fixpp::test_support::make_validation_test_dictionary()->as_table_view();
    static fixpp::wire::dict_hooks const validation_hooks =
        fixpp::wire::dict_hooks::for_table_view(validation_tv);
    static fixpp::wire::dict_hooks const pair_hooks =
        fixpp::wire::dict_hooks::for_table_view(fixpp::fuzz092::pair_dict_table_view());
    check_iterator_fault_agrees_with_build(buf, validation_hooks);
    check_iterator_fault_agrees_with_build(buf, pair_hooks);
    check_iterator_fault_agrees_with_build(buf, fixpp::wire::dict_hooks::none());
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

    // 092 R-7 arm, on the raw input: it does not depend on the frame factory.
    check_iterator_fault_agrees_with_build(buf);

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
        // Invariant: every rejection must be a wire_* slot is_valid_wire_error allows.
        // A raw decimal_* slot here means the T009a remap is broken.
        if (!is_valid_wire_error(validate_result.error())) {
            __builtin_trap();
        }
    }

    return 0;
}
