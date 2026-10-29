// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/fuzz/fuzz_appl_ver_id.cpp
//
// 097-fix-latest-applverid-10 W-12 (`[const §VII.7]`) — libFuzzer harness for the
// ApplVerID parse, dict::resolve_application_version, which reads a peer-supplied
// DefaultApplVerID(1137) or ApplVerID(1128) value. The input bytes are that value. It is
// resolved under an Unknown-default and a v50sp2-default FIXT profile, and every
// violation of the invariants below is an unconditional __builtin_trap(), so it fires
// under NDEBUG too:
//   - empty input follows the profile default: dict_unresolved_application_version
//     under the Unknown default, the value v50sp2 under the v50sp2 default;
//   - non-empty input gives the same outcome under both profiles (a non-empty value
//     never consults the default);
//   - a refused non-empty value is dict_unknown_appl_ver_id;
//   - an accepted value renders, the rendering equals the input bytes exactly, and it
//     re-parses to the same application_version. Exact byte equality is the strict form:
//     parse must accept only the strings render produces.
// ASan and UBSan cover out-of-bounds reads and UB.

#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <string_view>

namespace {

using fixpp::core::error;
using fixpp::dict::application_version;
using fixpp::dict::render_appl_ver_id;
using fixpp::dict::resolve_application_version;
using fixpp::dict::session_version;
using fixpp::dict::version_profile;

constexpr version_profile kUnknownDefault{.session = session_version::vt11,
                                          .default_appl = application_version::Unknown,
                                          .has_per_message_override = true,
                                          ._reserved = 0};
constexpr version_profile kV50sp2Default{.session = session_version::vt11,
                                         .default_appl = application_version::v50sp2,
                                         .has_per_message_override = true,
                                         ._reserved = 0};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // SAFETY: the fuzzer's byte buffer viewed as characters; it outlives this call.
    const std::string_view value{reinterpret_cast<const char*>(data), size};

    const auto under_unknown = resolve_application_version(kUnknownDefault, value);
    const auto under_v50sp2 = resolve_application_version(kV50sp2Default, value);

    if (value.empty()) {
        if (under_unknown.has_value() ||
            under_unknown.error() != error::dict_unresolved_application_version) {
            __builtin_trap();
        }
        if (!under_v50sp2.has_value() || *under_v50sp2 != application_version::v50sp2) {
            __builtin_trap();
        }
        return 0;
    }

    // A non-empty value never consults the profile default.
    if (under_unknown.has_value() != under_v50sp2.has_value()) {
        __builtin_trap();
    }
    if (under_unknown.has_value() ? *under_unknown != *under_v50sp2
                                  : under_unknown.error() != under_v50sp2.error()) {
        __builtin_trap();
    }

    if (!under_unknown.has_value()) {
        if (under_unknown.error() != error::dict_unknown_appl_ver_id) {
            __builtin_trap();
        }
        return 0;
    }

    const auto rendered = render_appl_ver_id(*under_unknown);
    if (!rendered.has_value() || *rendered != value) {
        __builtin_trap();
    }
    const auto reparsed = resolve_application_version(kUnknownDefault, *rendered);
    if (!reparsed.has_value() || *reparsed != *under_unknown) {
        __builtin_trap();
    }
    return 0;
}
