// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/dict/field_type.hpp
//
// `fixpp::dict::field_type` — the 7-value type-category enum the wire
// validator switches on (`validator.hpp:295`).
//
// Collapses `field_data_type` (29 values, `field_ref.hpp`) to the 7
// structural categories that the Phase-1 `dictionary_driven_validator`
// distinguishes:
//
//   field_data_type (29)        → field_type (7)
//   ─────────────────────────────────────────────
//   Int / SeqNum / NumInGroup   → Int
//   DayOfMonth / Length         → Length   (Length drives paired-Data logic)
//   Price / Qty / Amt /
//   PriceOffset / Percentage /
//   Float                       → Float
//   Char / Boolean              → Char / Boolean  (already 1:1)
//   Data / XmlData              → Data
//   String / MultiCharValue /
//   MultiStringValue / Currency/
//   Exchange / Country /
//   MonthYear / UtcTimestamp /   → String   (gate-A P3-b: UtcTimestamp→String;
//   UtcTimeOnly / UtcDateOnly /   a malformed SendingTime(52) value is
//   LocalMktDate / TzTimeOnly /   therefore undetectable to the Phase-1
//   TzTimestamp / Language /      validator — falls to the 038 semantic path)
//   XmlData (duplicate above)
//   DialectExtension            → String   (safe default)
//
// Promoted from `tests/support/mock_dict_table.hpp` to the production
// `dict/` layer so `validator.hpp` has a complete-type include that does NOT
// depend on the mock being included first (RC-A, 041-validation-gate-wiring).
//
// Minimal include graph — deliberate ([const §XV.9]):
//   Only <cstdint> + <fixpp/dict/field_ref.hpp>. No mutex, no std::string,
//   no asio, no dictionary.hpp (dependency must go dictionary.hpp → this,
//   never the reverse).

#pragma once

#include <cstdint>
#include <fixpp/dict/field_ref.hpp>  // field_data_type

namespace fixpp::dict {

// 7-value structural type category used by `dictionary_driven_validator` and
// by the `table_view` method `field_type_of(tag)`.
//
// Values match the mock's ORIGINAL enum byte-for-byte, so that existing test TUs
// that use the mock continue to compile unchanged once this header is the sole
// definition. T009 then removed that enum: `tests/support/mock_dict_table.hpp`
// is now a compatibility shim that includes THIS header, so there is no second
// definition left to agree with -- the byte-for-byte match is history, not a
// live invariant, and nothing downstream can drift from it.
enum class field_type : std::uint8_t {
    String,   // STRING + all timestamp/date/currency/geographic/dialect types
    Int,      // INT, SEQNUM, NUMINGROUP
    Float,    // PRICE, QTY, AMT, PRICEOFFSET, PERCENTAGE, FLOAT
    Char,     // CHAR (exactly one byte)
    Boolean,  // BOOLEAN
    Data,     // DATA, XMLDATA
    Length,   // LENGTH, DAYOFMONTH
};

// Map a `field_data_type` (29-value) to the 7-value `field_type` category.
// The mapping is stable for the v1.0 validator; consult the comment block above
// for the full collapse table. Called once per tag at `as_table_view()` time
// ([const §XV.1] — config-time, never per-message).
[[nodiscard]] constexpr field_type field_type_from_data_type(field_data_type dt) noexcept {
    switch (dt) {
        case field_data_type::Int:
        case field_data_type::SeqNum:
        case field_data_type::NumInGroup:
            return field_type::Int;

        case field_data_type::Length:
        case field_data_type::DayOfMonth:
            return field_type::Length;

        case field_data_type::Price:
        case field_data_type::Qty:
        case field_data_type::Amt:
        case field_data_type::PriceOffset:
        case field_data_type::Percentage:
        case field_data_type::Float:
            return field_type::Float;

        case field_data_type::Char:
            return field_type::Char;

        case field_data_type::Boolean:
            return field_type::Boolean;

        case field_data_type::Data:
        case field_data_type::XmlData:
            return field_type::Data;

        // String + all temporal/geographic/currency/dialect types → String.
        // Gate A P3-b: UtcTimestamp→String (malformed 52 undetectable to
        // Phase-1 validator; handled by the 038 semantic SendingTime path).
        case field_data_type::String:
        case field_data_type::MultiCharValue:
        case field_data_type::MultiStringValue:
        case field_data_type::Currency:
        case field_data_type::Exchange:
        case field_data_type::Country:
        case field_data_type::MonthYear:
        case field_data_type::UtcTimestamp:
        case field_data_type::UtcTimeOnly:
        case field_data_type::UtcDateOnly:
        case field_data_type::LocalMktDate:
        case field_data_type::TzTimeOnly:
        case field_data_type::TzTimestamp:
        case field_data_type::Language:
        case field_data_type::DialectExtension:
        default:
            return field_type::String;
    }
}

}  // namespace fixpp::dict
