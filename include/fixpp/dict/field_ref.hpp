// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/dict/field_ref.hpp
//
// `FieldRef` — per-tag metadata POD. Canonical declaration; mirrors the
// `[2c §4.1]` extract recorded at
// `specs/002-dictionary-xml-loader/contracts/field_ref.hpp`.
//
// Per `[2c §4.1]`: one `FieldRef` per (codegen-version, MsgType, tag) triple
// in `Fields.hpp`. Lifetime: `constexpr` static storage for codegen versions
// (D-008, separate feature); PMR-allocated metadata-handle storage for
// runtime-XML versions emitted by `XmlLoader` per
// `specs/002-dictionary-xml-loader/data-model.md` Entity 1.
//
// 102-dict-width-multichar-type D1 supersedes the 16-bit `component_index`
// and the `_reserved` tail of `[2c §4.1]` and of the 002 extract:
// `component_index` is `std::uint32_t` in place (offset 8), so it holds the
// component's index + 1 for any component below 2^32, and `_reserved` is
// removed. The record stays 16 bytes, now at `alignof == 4`; a generated row
// has one positional initializer per member.

#pragma once

#include <cstdint>
#include <type_traits>

namespace fixpp::dict {

// Field data type per `[FIX50SP2 §3.3]`. The fully expanded list (PRICE, QTY,
// AMT, PRICEOFFSET, PERCENTAGE, INT, LENGTH, SEQNUM, NUMINGROUP, STRING,
// MULTIPLEVALUESTRING, MULTIPLECHARVALUE, CHAR, CURRENCY, EXCHANGE, COUNTRY,
// MONTHYEAR, UTCTIMESTAMP, UTCTIMEONLY, UTCDATEONLY, LOCALMKTDATE,
// TZTIMEONLY, TZTIMESTAMP, BOOLEAN, DATA, XMLDATA, LANGUAGE) is fixed at the
// FIX 5.0 SP2 spec level; older versions are subsets. Renamed
// `data_type → field_data_type` for `dict::` namespace hygiene per
// research.md D-14.
enum class field_data_type : std::uint8_t {
    Int,
    Length,
    SeqNum,
    NumInGroup,
    DayOfMonth,
    Price,
    Qty,
    Amt,
    PriceOffset,
    Percentage,
    Float,
    Char,
    Boolean,
    String,
    MultiCharValue,
    MultiStringValue,
    Currency,
    Exchange,
    Country,
    MonthYear,
    UtcTimestamp,
    UtcTimeOnly,
    UtcDateOnly,
    LocalMktDate,
    TzTimeOnly,
    TzTimestamp,
    Language,
    Data,
    XmlData,
    // Reserved; no loader produces it. A dialect overlay (098-dialect-overlay,
    // FR-012) declares its fields in the base format's type vocabulary, so an
    // overlay field maps to one of the enumerators above, as a base field does.
    // The enumerator keeps its position in this enum. Re-check that nothing
    // produces it: `git grep -n DialectExtension -- src` must print nothing.
    DialectExtension,
};

// Field-presence rule. Encoded explicitly so a single FieldRef carries both
// "is this field declared on this MsgType?" and "if so, is it required?".
// Conditional-required rules per `[FIX50SP2 §3.4]` are not exercised by
// 002-dictionary-xml-loader (the runtime XML loader emits `Optional`,
// `Required`, or `NotDeclared`); `Conditional` reaches the runtime only
// through codegen (D-008) and the v1.0 grammar closure of D-009.
//
// Renamed `presence → field_presence` for `dict::` namespace hygiene per
// research.md D-14.
enum class field_presence : std::uint8_t {
    NotDeclared = 0,  // tag is not part of this MsgType's grammar
    Optional = 1,
    Required = 2,
    Conditional = 3,  // codegen-version base only in v1.0; consult
                      // condition_index
};

struct FieldRef {
    std::uint16_t tag;
    field_data_type type;
    field_presence rule;
    std::uint16_t condition_index;
    std::uint16_t group_no_tag;
    std::uint32_t component_index;  // 0 = not in a component, else the component's index + 1
    std::uint16_t enum_table_index;
    std::uint16_t length_pair_data_tag;
};

// AC-F1..AC-F4 per spec.md §4.3 — re-asserted here and again in
// tests/dictionary/ref_shape_test.cpp (seam #4) for ABI-drift detection.
static_assert(sizeof(FieldRef) == 16);
static_assert(alignof(FieldRef) == 4);
static_assert(std::is_standard_layout_v<FieldRef>);
static_assert(std::is_trivially_copyable_v<FieldRef>);

}  // namespace fixpp::dict
