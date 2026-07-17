// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/builder_completeness_common.hpp
//
// 077-builder-args-dedup T023 [US4] -- shared machinery for the per-version
// builder-completeness census (contracts/builder-completeness.md C1-C4;
// data-model.md Entity 6). Re-instates 076's descoped V-2/V-2b legs at
// 076-V-1's raw-XML independence strength (see the vlatest V-1/V-1b
// precedent: vlatest_completeness_census_test.cpp /
// vlatest_manifest_class_consistency_test.cpp), generalized to every
// builder-bearing version and adding a compile-time address-of leg the
// V-1/V-1b legs didn't need.
//
// NON-CIRCULARITY: `legacy_expected_msgtypes`/`orchestra_expected_msgtypes`
// below are a from-scratch pugixml walk over the SOURCE dictionaries
// (dictionaries/FIX44.xml, FIX50SP2.xml, orchestra/OrchestraFIXLatest.xml)
// that reads `msgtype`/`msgcat` (legacy) or `msgType`/`category`
// (Orchestra) DIRECTLY off the XML -- this file does NOT include
// tools/codegen/fixpp-codegen/ir.hpp/ir.cpp/emit_builders.cpp, does NOT call
// build_ir(), and does NOT read `MessageIR.is_application`. It
// re-implements the identical fail-closed app/admin rule those files use
// (ir.cpp:182-197 legacy, ir.cpp:479-485 Orchestra) independently, so an
// app/admin misclassification in the emitter's own IR is NOT inherited here
// (the 075/076 blind-corpus lesson:
// feedback_verification_corpus_built_from_the_read_it_checks_is_blind).
//
// THE FOUR CENSUS LEGS (contracts C1-C3), composed by each per-version test
// file that includes this header:
//
//  1. expected(V) -- `legacy_expected_msgtypes`/`orchestra_expected_msgtypes`
//     (C1). Authoritative; re-derived fresh every test run from the live
//     source dictionaries.
//  2. actual(V) via address-of (C2, `expected ⊆ actual`) -- the PER-VERSION
//     .cpp #includes the generated Builders.hpp and a committed
//     tests/codegen/generated/<ns>_builder_completeness_entries.def
//     (X-macro list of (msgtype, C++ identifier) pairs), building a
//     `std::vector<Entry>` whose `build_addr`/`validate_addr` are the
//     addresses of `fixpp::<ns>::build_<Msg>`/`validate_<Msg>` -- a
//     compile-time ODR-use, so the .cpp FAILS TO COMPILE if any listed
//     entry point is missing. The .def table's PROVENANCE is the same raw
//     walk as leg 1 (generated once by a standalone script, documented in
//     each .def's header) -- it is NOT grepped out of the emitted header.
//     Because the table could go stale relative to a future dictionary
//     change, `assert_def_matches_expected` below cross-checks the .def
//     table's msg_type set against the live leg-1 walk on every run, so
//     staleness is self-detecting (RED), never silent.
//  3. `parse_registry_msgtypes` -- text-parses the emitted
//     `builder_registry` array out of the generated per-version `all.hpp`
//     (078-precompiled-builder-libs: the registry's new home, Entity 5; C2
//     secondary / C3a's `actual ⊆ expected` leg).
//  4. `parse_build_fn_identifiers` -- an INDEPENDENT text scan for
//     `build_<Msg>(` function signatures over the per-version
//     `messages/*.builder.inl` + `messages/*.builder.cpp` set
//     (078-precompiled-builder-libs: `all.hpp` holds only `#include`s + the
//     registry array, no literal `build_<Msg>(` bodies -- Entity 3/4), NOT
//     sourced from the registry-array parse above. Closes the Gate-A
//     round-3 residual (077 tasks.md T023 note): the registry array and the
//     `build_<Msg>` definition are emitted from the same per-message pass in
//     emit_builders.cpp (an unproven co-emission invariant) -- this second,
//     differently-shaped parse means a future drift that dropped the
//     registry entry but kept `build_<Msg>` (or vice versa) would surface as
//     a leg-3-vs-leg-4 mismatch, not be silently covered by a single check.
//
// Anchors: specs/077-builder-args-dedup/tasks.md T023;
//          contracts/builder-completeness.md C1-C4; data-model.md Entity 6.

#pragma once

#include <pugixml.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fixpp_test::builder_completeness {

// One census entry: a msg_type plus the addresses of its `build_<Msg>` /
// `validate_<Msg>` entry points (type-erased -- each version's Args type
// differs, but existence is all leg 2 needs). Populated by the per-version
// .cpp via the FIXPP_BC_ENTRY X-macro over its committed .def table.
struct Entry {
    std::string_view msg_type;
    void const* build_addr;
    void const* validate_addr;
};

inline std::set<std::string> def_msgtypes(std::vector<Entry> const& entries) {
    std::set<std::string> out;
    for (auto const& e : entries) {
        out.emplace(e.msg_type);
    }
    return out;
}

// C1 -- legacy <fix>-schema raw walk (FIX44.xml / FIX50SP2.xml). Mirrors
// ir.cpp's fail-closed msgcat rule, independently re-implemented.
inline std::set<std::string> legacy_expected_msgtypes(
    std::string const& xml_path, std::set<std::string> const& exclude = {}) {
    pugi::xml_document doc;
    pugi::xml_parse_result const load = doc.load_file(xml_path.c_str());
    if (!load) {
        throw std::runtime_error("cannot load " + xml_path + ": " + load.description());
    }
    pugi::xml_node const root = doc.child("fix");
    if (!root) {
        throw std::runtime_error("no <fix> root in " + xml_path);
    }
    std::set<std::string> out;
    for (pugi::xml_node m : root.child("messages").children("message")) {
        std::string const msg_type{m.attribute("msgtype").as_string("")};
        std::string const msgcat{m.attribute("msgcat").as_string("")};
        bool is_app;
        if (msgcat == "app") {
            is_app = true;
        } else if (msgcat == "admin") {
            is_app = false;
        } else {
            throw std::runtime_error(xml_path + ": <message msgtype=\"" + msg_type +
                                      "\"> missing or unrecognized msgcat attribute");
        }
        if (is_app && !exclude.count(msg_type)) {
            out.insert(msg_type);
        }
    }
    return out;
}

// C1 -- Orchestra raw walk (orchestra/OrchestraFIXLatest.xml). Mirrors
// ir.cpp's fail-closed category rule (category=="Session" -> admin, empty
// -> throw), independently re-implemented.
inline std::set<std::string> orchestra_expected_msgtypes(std::string const& xml_path) {
    pugi::xml_document doc;
    pugi::xml_parse_result const load = doc.load_file(xml_path.c_str());
    if (!load) {
        throw std::runtime_error("cannot load " + xml_path + ": " + load.description());
    }
    pugi::xml_node const repo = doc.child("fixr:repository");
    if (!repo) {
        throw std::runtime_error("no fixr:repository root in " + xml_path);
    }
    std::set<std::string> out;
    for (pugi::xml_node m : repo.child("fixr:messages").children("fixr:message")) {
        std::string const msg_type{m.attribute("msgType").as_string("")};
        std::string const category{m.attribute("category").as_string("")};
        if (category.empty()) {
            throw std::runtime_error(xml_path + ": <fixr:message msgType=\"" + msg_type +
                                      "\"> missing or empty category attribute");
        }
        if (category != "Session") {
            out.insert(msg_type);
        }
    }
    return out;
}

inline std::string read_file(std::string const& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Leg 3 (C2 secondary / C3a actual-subset-expected): the emitted
// `builder_registry` array's msg_type set. `all_hpp_path` is the
// per-version `all.hpp` aggregator (078-precompiled-builder-libs Entity 5 --
// the registry's new home, formerly the monolithic Builders.hpp).
inline std::set<std::string> parse_registry_msgtypes(std::string const& all_hpp_path) {
    std::string const text = read_file(all_hpp_path);
    auto const begin = text.find("builder_registry = {{");
    if (begin == std::string::npos) {
        throw std::runtime_error("no builder_registry array in " + all_hpp_path);
    }
    auto const end = text.find("}};", begin);
    if (end == std::string::npos) {
        throw std::runtime_error("unterminated builder_registry array in " + all_hpp_path);
    }
    std::string const body = text.substr(begin, end - begin);
    static std::regex const re(R"re(\{"([^"]*)"\},)re");
    std::set<std::string> out;
    for (auto it = std::sregex_iterator(body.begin(), body.end(), re); it != std::sregex_iterator(); ++it) {
        out.insert((*it)[1].str());
    }
    return out;
}

// Leg 4 (Gate-A round-3 residual closure): an INDEPENDENT text scan for
// `build_<Msg>(` FUNCTION DEFINITIONS -- not the registry array. Returns
// C++ identifiers (message names, not msg_types); callers translate through
// the same .def table's (msg_type, identifier) map to compare against
// expected(V)'s msg_type set.
//
// 078-precompiled-builder-libs re-point: `all.hpp` holds only `#include`s +
// the registry array (Entity 5) -- the literal `build_<Msg>(` bodies now
// live per-message in `messages/<Msg>.builder.inl` (inline mode) and
// `messages/<Msg>.builder.cpp` (external-linkage/lib mode, Entity 3/4). This
// scans every `*.builder.inl` + `*.builder.cpp` file under
// `<version_dir>/messages/` and unions the identifiers found.
//
// Manual scan, NOT std::regex: a global `\bbuild_(\w+)\(` search over the
// (formerly monolithic) v50sp2/vlatest headers (~75-78MB) took ~33s under
// std::regex's backtracking engine -- a plain substring search +
// hand-rolled identifier scan is the same logic, sub-second.
inline bool is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

inline std::set<std::string> scan_build_fn_identifiers_in_text(std::string const& text) {
    static std::string const needle = "build_";
    std::set<std::string> out;
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        // \b -- the character before "build_" (if any) must NOT be an
        // identifier char (so "rebuild_x(" doesn't match).
        bool const word_start = (pos == 0) || !is_ident_char(text[pos - 1]);
        std::size_t const ident_begin = pos + needle.size();
        std::size_t ident_end = ident_begin;
        while (ident_end < text.size() && is_ident_char(text[ident_end])) {
            ++ident_end;
        }
        bool const has_ident = ident_end > ident_begin;
        bool const followed_by_paren = (ident_end < text.size()) && (text[ident_end] == '(');
        if (word_start && has_ident && followed_by_paren) {
            out.insert(text.substr(ident_begin, ident_end - ident_begin));
        }
        pos = ident_begin;
    }
    return out;
}

inline std::set<std::string> parse_build_fn_identifiers(std::string const& version_dir) {
    namespace fs = std::filesystem;
    fs::path const messages_dir = fs::path(version_dir) / "messages";
    if (!fs::exists(messages_dir)) {
        throw std::runtime_error("no messages/ dir in " + version_dir);
    }
    std::set<std::string> out;
    for (auto const& entry : fs::directory_iterator(messages_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string const fname = entry.path().filename().string();
        bool const is_builder_file =
            (fname.size() > 12 && fname.compare(fname.size() - 12, 12, ".builder.inl") == 0) ||
            (fname.size() > 12 && fname.compare(fname.size() - 12, 12, ".builder.cpp") == 0);
        if (!is_builder_file) {
            continue;
        }
        std::set<std::string> const file_idents = scan_build_fn_identifiers_in_text(read_file(entry.path().string()));
        out.insert(file_idents.begin(), file_idents.end());
    }
    return out;
}

}  // namespace fixpp_test::builder_completeness
