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
    Int, Length, SeqNum, NumInGroup, DayOfMonth,
    Price, Qty, Amt, PriceOffset, Percentage, Float,
    Char, Boolean,
    String, MultiCharValue, MultiStringValue,
    Currency, Exchange, Country, MonthYear,
    UtcTimestamp, UtcTimeOnly, UtcDateOnly, LocalMktDate, TzTimeOnly, TzTimestamp,
    Language,
    Data, XmlData,
    // Sentinel for dialect-introduced types not in the standard set. In this
    // PR (002-dictionary-xml-loader, /clarify Q2 → A) overlays are out of
    // scope, so `DialectExtension` is reserved but never emitted by
    // `XmlLoader::load*`. Re-enabled when D-009 ships (spec.md §10 F2).
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
    NotDeclared = 0,    // tag is not part of this MsgType's grammar
    Optional    = 1,
    Required    = 2,
    Conditional = 3,    // codegen-version base only in v1.0; consult
                        // condition_index
};

struct FieldRef {
    std::uint16_t   tag;
    field_data_type type;
    field_presence  rule;
    std::uint16_t   condition_index;
    std::uint16_t   group_no_tag;
    std::uint16_t   component_index;
    std::uint16_t   enum_table_index;
    std::uint16_t   length_pair_data_tag;
    std::uint16_t   _reserved;
};

// AC-F1..AC-F4 per spec.md §4.3 — re-asserted here and again in
// tests/dictionary/ref_shape_test.cpp (seam #4) for ABI-drift detection.
static_assert(sizeof(FieldRef) == 16);
static_assert(alignof(FieldRef) == 2);
static_assert(std::is_standard_layout_v<FieldRef>);
static_assert(std::is_trivially_copyable_v<FieldRef>);

}  // namespace fixpp::dict
