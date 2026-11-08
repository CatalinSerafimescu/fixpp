// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_101_tag_class.hpp
//
// 101-inbound-reject-fidelity (research D1, FR-002): the SessionRejectReason a validating table
// answers for a tag outside the message, 2 when its dictionary defines the tag and 0 when it does
// not. A cell whose tag is computed at run time reads the class here, from an independent walk of
// the judging dictionary's source file (its field definitions), never through the loader whose
// store the code under test reads: an expectation read from the code under test cannot fail.
//
// The walk: QuickFIX XML writes a field definition as `<field number=…>` (a message's reference
// has no number); Orchestra writes `<fixr:field id=…>` (a reference is `<fixr:fieldRef …>`).
// `relative` is a path under FIXPP_DICT_DATA_DIR, e.g. "FIX44.xml" or
// "orchestra/OrchestraFIXLatest.xml".

#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace fixpp::session::test101 {

inline const std::set<std::uint16_t>& source_field_definitions(const std::string& relative) {
    static std::map<std::string, std::set<std::uint16_t>> cache;
    auto it = cache.find(relative);
    if (it != cache.end()) {
        return it->second;
    }
    std::ifstream in{std::filesystem::path{FIXPP_DICT_DATA_DIR} / relative, std::ios::binary};
    std::ostringstream s;
    s << in.rdbuf();
    const std::string xml = s.str();
    const bool orchestra = xml.find("<fixr:") != std::string::npos;
    const std::regex re = orchestra ? std::regex{R"(<fixr:field\b[^>]*\bid=['"](\d+)['"])"}
                                    : std::regex{R"(<field\s+number=['"](\d+)['"])"};
    std::set<std::uint16_t> out;
    for (std::sregex_iterator m{xml.begin(), xml.end(), re}, end; m != end; ++m) {
        out.insert(static_cast<std::uint16_t>(std::stoul((*m)[1].str())));
    }
    return cache.emplace(relative, std::move(out)).first->second;
}

// "2" when `relative` defines `tag`, "0" when it does not; an empty walk is reported as "?" so a
// missing file cannot read as "undefined".
inline std::string outside_tag_reason(const std::string& relative, std::uint16_t tag) {
    const auto& defs = source_field_definitions(relative);
    if (defs.empty()) {
        return "?";
    }
    return defs.contains(tag) ? "2" : "0";
}

}  // namespace fixpp::session::test101
