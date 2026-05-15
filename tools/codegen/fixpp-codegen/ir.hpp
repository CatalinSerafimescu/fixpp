// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/codegen/fixpp-codegen/ir.hpp
//
// 003-dictionary-codegen — T014. XML→IR: F1 Candidate A reuses the merged 002
// XmlLoader/Dictionary as the single XML truth (no second QuickFIX-XML parser
// — research.md D-1). build_ir() calls XmlLoader::load(path, mr) and walks the
// Dictionary metadata into a deterministic, codegen-ready IR. Build-only host
// tool ([const §III.5]).
//
// Foundational-checkpoint scope: the IR captures version + bytewise-sorted
// message list + Length+Data pairs (the determinism-relevant spine). Full
// per-message field/group enumeration is completed with the US1 emitters
// (T023-T027), which extend this IR.
#pragma once
#include <cstdint>
#include <filesystem>
#include <memory_resource>
#include <string>
#include <vector>

#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/version_profile.hpp>

namespace fixpp::codegen {

// One declared field of a message (RC#5 — sourced via the additive
// Dictionary::message_fields / Dictionary::field_name accessors; D-24).
struct FieldIR {
    std::uint16_t                tag{};
    std::string                  name;   // FIX field name, e.g. "ClOrdID"
    fixpp::dict::field_data_type type{};
    fixpp::dict::field_presence  rule{};
    std::uint16_t                group_no_tag{};  // 0 if not a group delimiter
};

struct MessageIR {
    std::string           msg_type;  // FIX MsgType (e.g. "D")
    std::string           name;      // English name (diagnostics / NormativeRefs)
    std::vector<FieldIR>  fields;    // full per-message run (required + optional)
};

struct LengthPairIR {
    std::uint16_t length_tag;
    std::uint16_t data_tag;
};

struct VersionIR {
    fixpp::dict::session_version     session{};
    fixpp::dict::application_version application{};
    std::string                     ns;        // "v42" / "v44" / "v50sp2" / "vt11"
    std::vector<MessageIR>           messages;  // bytewise-sorted (002 D-6)
    std::vector<LengthPairIR>        length_pairs;
};

// Loads `xml_path` via XmlLoader and projects the Dictionary into VersionIR.
// Throws dict::xml_* on malformed input (host tool — exceptions allowed,
// [arch §5.3] / [const §III.5]); the user-facing library hot path is
// unaffected. `mr` backs the loader's PMR allocations.
[[nodiscard]] VersionIR build_ir(std::filesystem::path const& xml_path,
                                 std::pmr::memory_resource*    mr);

}  // namespace fixpp::codegen
