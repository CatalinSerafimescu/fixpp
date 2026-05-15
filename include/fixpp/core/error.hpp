#pragma once
// include/fixpp/core/error.hpp
// Engine-wide error enum for fixpp::core. Owned by 2k; this feature (001-core-decimal)
// contributes the four decimal variants per data-model.md Entity 5.
// 002-dictionary-xml-loader contributes the three dict_* variants per its
// research.md D-10. Variant numbering is additive (no renumbering of existing
// slots) per `[const §X.4]` forwards-compat.

#include <cstdint>
#include <expected>

namespace fixpp::core {

enum class error : std::uint8_t {
    // slot 0 reserved for ok (never stored in unexpected)
    out_of_memory = 1,

    // decimal variants — owned by 001-core-decimal, contributes to 2k
    decimal_invalid_input = 10,
    decimal_overflow = 11,
    decimal_precision_loss = 12,
    decimal_buffer_too_small = 13,

    // dict variants — owned by 002-dictionary-xml-loader (research.md D-10);
    // additive at unused slots per `[const §X.4]`. Each exception type in
    // `<fixpp/dict/error.hpp>` carries the matching variant via `code()`.
    dict_xml_parse_failed = 20,
    dict_unknown_version = 21,
    dict_xml_oom = 22,

    // dict codegen / reify variants — owned by 003-dictionary-codegen
    // (research.md D-10/D-21; data-model "Error mapping"; spec AC-VP6).
    // Additive at unused slots 23..28 per `[const §X.4]`; non-renumbering;
    // existing slots above preserved verbatim. The 2b/wire "field absent"
    // error from `MessageView::get<1128>()` is 2b-owned, NOT a slot here
    // (cross-feature note, contracts/version_profile.hpp / spec A6).
    dict_reify_msg_type_mismatch = 23,
    dict_reify_unknown_msg_type = 24,
    dict_reify_oom = 25,
    dict_unresolved_application_version = 26,
    dict_unknown_appl_ver_id = 27,
    dict_no_dictionary_for_application_version = 28,
};

template <class T>
using expected_t = std::expected<T, error>;

}  // namespace fixpp::core
