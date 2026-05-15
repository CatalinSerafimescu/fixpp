// SPDX-License-Identifier: AGPL-3.0-or-later
// extract from .specify/2c-codegen.md v1.3 §4.1 (signed off 2026-05-10)
// This file is a /plan Phase 1 contract extract — the canonical declaration
// lives at include/fixpp/dict/field_ref.hpp once 002-dictionary-xml-loader
// reaches /implement. Verbatim from §4.1; no /plan-time shape edits.
//
// Per `[2c §4.1]`: one `FieldRef` per (codegen-version, MsgType, tag) triple in
// `Fields.hpp`. Lifetime: `constexpr` static storage for codegen versions;
// PMR-allocated metadata-handle storage for runtime-XML versions. v0.1 (this
// PR) only materializes the runtime-XML form via `XmlLoader`; the constexpr
// form ships with codegen (D-008, separate feature).

#pragma once

#include <cstdint>
#include <type_traits>

namespace fixpp::dict {

// Field data type per `[FIX50SP2 §3.3]`. Compile-time enumeration; the fully
// expanded type list (PRICE, QTY, AMT, PRICEOFFSET, PERCENTAGE, INT, LENGTH,
// SEQNUM, NUMINGROUP, STRING, MULTIPLEVALUESTRING, MULTIPLECHARVALUE, CHAR,
// CURRENCY, EXCHANGE, COUNTRY, MONTHYEAR, UTCTIMESTAMP, UTCTIMEONLY,
// UTCDATEONLY, LOCALMKTDATE, TZTIMEONLY, TZTIMESTAMP, BOOLEAN, DATA, XMLDATA,
// LANGUAGE) is fixed at the FIX 5.0 SP2 spec level; older versions are
// subsets. Renamed `data_type → field_data_type` for `dict::` namespace
// hygiene per research.md D-14.
enum class field_data_type : std::uint8_t {
    Int, Length, SeqNum, NumInGroup, DayOfMonth,
    Price, Qty, Amt, PriceOffset, Percentage, Float,
    Char, Boolean,
    String, MultiCharValue, MultiStringValue,
    Currency, Exchange, Country, MonthYear,
    UtcTimestamp, UtcTimeOnly, UtcDateOnly, LocalMktDate, TzTimeOnly, TzTimestamp,
    Language,
    Data, XmlData,
    // Sentinel for dialect-introduced types not in the standard set.
    // In this PR (002-dictionary-xml-loader, /clarify Q2 → A) overlays are
    // out of scope, so `DialectExtension` is reserved but never emitted by
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
    std::uint16_t   tag;                  // 0..65535 (matches `[2b §1.2]`'s
                                          // wire range)
    field_data_type type;                 //  1 byte
    field_presence  rule;                 //  1 byte
    std::uint16_t   condition_index;      //  index into per-message
                                          //  conditional-rule table; 0 if
                                          //  rule != Conditional.
                                          //  Indirection chosen over inline
                                          //  closure to keep FieldRef
                                          //  trivially copyable and
                                          //  constexpr-friendly.
    std::uint16_t   group_no_tag;         // 0 if not inside a group; otherwise
                                          // the NoXxx tag of the enclosing
                                          // group.
    std::uint16_t   component_index;      // 0 if not inside a component;
                                          // otherwise an index into the
                                          // per-version ComponentRef table
                                          // (§4.2).
    std::uint16_t   enum_table_index;     // 0 if not enum-constrained;
                                          // otherwise an index into a
                                          // per-version constexpr enum-value
                                          // table.
    std::uint16_t   length_pair_data_tag; // For LENGTH-typed fields paired
                                          // with a DATA field per
                                          // `[FIX50SP2 §3]` Length+Data
                                          // semantics: the tag of the
                                          // following DATA field. 0 if not
                                          // paired. Per `[2c §4.1]` v1.0
                                          // overlays cannot extend this
                                          // table; overlay XML declaring a
                                          // Length+Data pair is rejected
                                          // with
                                          // `dict_overlay_unsupported_length_pair`
                                          // (out of scope for 002-PR).
    std::uint16_t   _reserved;            // padding to 16 bytes for
                                          // cache-line friendliness;
                                          // reserved for future flags under
                                          // FIXPP_DICT_FIELDREF_RESERVED_USED
                                          // (set to zero on emit; ignored on
                                          // read in v1.0; matches
                                          // `[2a §4.2]` discipline — C-P3-1).
};

// AC-F1..AC-F4 per spec.md §4.3 — re-asserted here and again in
// tests/dictionary/ref_shape_test.cpp (seam #4) for ABI-drift detection.
static_assert(sizeof(FieldRef) == 16);
static_assert(alignof(FieldRef) == 2);
static_assert(std::is_standard_layout_v<FieldRef>);
static_assert(std::is_trivially_copyable_v<FieldRef>);

}  // namespace fixpp::dict
