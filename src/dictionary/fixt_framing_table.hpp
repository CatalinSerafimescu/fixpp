// SPDX-License-Identifier: AGPL-3.0-or-later
// src/dictionary/fixt_framing_table.hpp
//
// 081-strict-validation-residuals Concern A (research.md D-2, data-model.md
// E-1): the baked FIXT.1.1 standard framing `tag -> field_type` table — the
// `<header>` + `<trailer>` field tags declared in `dictionaries/FIXT11.xml`,
// including the flat-recursed nested `<header>` `NoHops` group tags (one
// level of recursion into `<header>`/`<trailer>` groups per data-model.md
// E-1), each reduced to its structural `field_type` via
// `field_type_from_data_type` (field_type.hpp) applied to the tag's real
// datatype in FIXT11.xml's `<fields>` catalog.
//
// SOURCE OF TRUTH: dictionaries/FIXT11.xml `<header>` + `<trailer>`. FIXT.1.1
// is a frozen protocol (does not version), so this table is immutable.
// `tests/dictionary/fixt_header_merge_test.cpp` pins it exact-set-equal to
// FIXT11.xml (tags AND datatypes, both directions) by an INDEPENDENT raw-XML
// walk (pugixml) — drift is a CI-enforced impossibility.
//
// CROSS-TU VISIBILITY (fixpp::dict::detail — mirrors `dict_metadata_handle`
// in include/fixpp/dict/dictionary.hpp): this table is consumed by
// `Dictionary::as_table_view()` (dictionary.cpp) to populate the
// validator-private `table_view::fixt_framing_tags_`/`fixt_framing_types_`
// members (v50/v50sp1/v50sp2 ONLY — research.md D-1/D-2) AND directly by the
// census test above. It is NOT part of the 6-method validator-facing
// contract (table_view.hpp), NOT attached to `message_fields()`/`field_ref()`
// /the shared `valid_`/`types_` stores, and NOT read by the parser.
//
// Kept private to src/dictionary/ (alongside the TU-private `kVersionTable`/
// `kFieldTypeTable` precedent in xml_loader.cpp) rather than a public
// `include/fixpp/dict/` header — this table has no public-API role; only
// dictionary.cpp (production) and the census test (via an explicit
// target_include_directories seam, matching the `ir.hpp`/required_scope_
// census_test.cpp precedent) need it.

#pragma once

#include <cstdint>
#include <fixpp/dict/field_type.hpp>

namespace fixpp::dict::detail {

struct fixt_framing_entry {
    std::uint16_t tag;
    field_type type;
};

// Sorted by tag ascending (not load-bearing — consumers treat this as a set
// — kept sorted for human scannability against FIXT11.xml).
//
//   tag   name                      FIXT11.xml type   -> field_type
inline constexpr fixt_framing_entry kFixtFramingTable[] = {
    {.tag = 8, .type = field_type::String},      // BeginString      STRING
    {.tag = 9, .type = field_type::Length},      // BodyLength       LENGTH
    {.tag = 10, .type = field_type::String},     // CheckSum         STRING   (trailer)
    {.tag = 34, .type = field_type::Int},        // MsgSeqNum        SEQNUM
    {.tag = 35, .type = field_type::String},     // MsgType          STRING
    {.tag = 43, .type = field_type::Boolean},    // PossDupFlag      BOOLEAN
    {.tag = 49, .type = field_type::String},     // SenderCompID     STRING
    {.tag = 50, .type = field_type::String},     // SenderSubID      STRING
    {.tag = 52, .type = field_type::String},     // SendingTime      UTCTIMESTAMP
    {.tag = 56, .type = field_type::String},     // TargetCompID     STRING
    {.tag = 57, .type = field_type::String},     // TargetSubID      STRING
    {.tag = 89, .type = field_type::Data},       // Signature        DATA     (trailer)
    {.tag = 90, .type = field_type::Length},     // SecureDataLen    LENGTH
    {.tag = 91, .type = field_type::Data},       // SecureData       DATA
    {.tag = 93, .type = field_type::Length},     // SignatureLength  LENGTH   (trailer)
    {.tag = 97, .type = field_type::Boolean},    // PossResend       BOOLEAN
    {.tag = 115, .type = field_type::String},    // OnBehalfOfCompID STRING
    {.tag = 116, .type = field_type::String},    // OnBehalfOfSubID  STRING
    {.tag = 122, .type = field_type::String},    // OrigSendingTime  UTCTIMESTAMP
    {.tag = 128, .type = field_type::String},    // DeliverToCompID  STRING
    {.tag = 129, .type = field_type::String},    // DeliverToSubID   STRING
    {.tag = 142, .type = field_type::String},    // SenderLocationID STRING
    {.tag = 143, .type = field_type::String},    // TargetLocationID STRING
    {.tag = 144, .type = field_type::String},    // OnBehalfOfLocationID STRING
    {.tag = 145, .type = field_type::String},    // DeliverToLocationID  STRING
    {.tag = 212, .type = field_type::Length},    // XmlDataLen       LENGTH
    {.tag = 213, .type = field_type::Data},      // XmlData          DATA
    {.tag = 347, .type = field_type::String},    // MessageEncoding  STRING
    {.tag = 369, .type = field_type::Int},       // LastMsgSeqNumProcessed SEQNUM
    // Flat-recursed nested <header> NoHops group (FIXT11.xml:32-35,
    // data-model.md E-1 disposition — accept-only, hop group not
    // structurally validated; SC-003 no-false-reject of routed FIXT traffic).
    {.tag = 627, .type = field_type::Int},       // NoHops           NUMINGROUP
    {.tag = 628, .type = field_type::String},    // HopCompID        STRING
    {.tag = 629, .type = field_type::String},    // HopSendingTime   UTCTIMESTAMP
    {.tag = 630, .type = field_type::Int},       // HopRefID         SEQNUM
    {.tag = 1128, .type = field_type::String},   // ApplVerID        STRING
    {.tag = 1129, .type = field_type::String},   // CstmApplVerID    STRING
    {.tag = 1156, .type = field_type::Int},      // ApplExtID        INT
};

}  // namespace fixpp::dict::detail
