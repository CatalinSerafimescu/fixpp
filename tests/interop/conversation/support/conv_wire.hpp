// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/conversation/support/conv_wire.hpp — 089 T052/T053a.
//
// Generic, intent-driven wire helpers for the fixpp side of a conversation
// cell: (a) build a message BODY from a flat list of (path, raw_value) field
// entries read from the per-run intent file (FR-008d (a)), supporting the
// nested-repeating-group path grammar `"453[0].802[0].523"` B-01 requires
// (FR-008b) — via `fixpp::wire::body_builder`, never a hand-rolled byte
// pusher (C-8: `sent.fields` must be derivable from the SAME builder inputs
// this function consumes); (b) walk a received message's BODY fields (C-6's
// canonical header/trailer exclusion) into the same FieldEntry shape for a
// `readback` record. No repeating-group support is needed on the readback
// side for THIS combo's script (none of C1's peer-originated steps declare a
// group — only B-01, fixpp-originated, does).
#pragma once

#include "support/readback_jsonl.hpp"  // FieldEntry, is_canonical_header_or_trailer_tag
#include "support/intent_file.hpp"

#include <fixpp/core/error.hpp>
#include <fixpp/wire/body_builder.hpp>
#include <fixpp/wire/parser.hpp>

#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fixpp::interop::conversation {

// Delimiter (first-field) tag for each group this script uses. body_builder
// requires the AUTHOR to supply the delimiter (no dictionary lookup) — the
// script's own B-01 field order (spec.md § Conversation census, "Field
// content the census presumes") fixes these two: NoPartyIDs(453) ->
// PartyID(448) first; NoPartySubIDs(802) -> PartySubID(523) first.
inline int group_delimiter_tag(int no_tag)
{
    switch (no_tag) {
        case 453: return 448;  // NoPartyIDs -> PartyID
        case 802: return 523;  // NoPartySubIDs -> PartySubID
        default:
            throw std::runtime_error(
                "conv_wire.hpp: no known delimiter tag for group " + std::to_string(no_tag) +
                " -- add it here before using this group in a conversation script");
    }
}

// "453[1].802[0].523" -> group_path={{453,1},{802,0}}, leaf=523.
// "453[0].448"        -> group_path={{453,0}},          leaf=448.
// "11"                -> group_path={},                 leaf=11.
struct ParsedPath {
    std::vector<std::pair<int, int>> group_path;
    int leaf_tag = 0;
};

inline ParsedPath parse_field_path(std::string const& path)
{
    ParsedPath out;
    std::size_t pos = 0;
    while (pos < path.size()) {
        std::size_t dot = path.find('.', pos);
        std::string token = (dot == std::string::npos) ? path.substr(pos) : path.substr(pos, dot - pos);
        bool const is_last = (dot == std::string::npos);
        std::size_t bracket = token.find('[');
        if (bracket == std::string::npos) {
            // Plain numeric token: a leaf ONLY if it is the last token.
            if (!is_last) {
                throw std::runtime_error("conv_wire.hpp: malformed group path segment: " + path);
            }
            out.leaf_tag = std::stoi(token);
        } else {
            int const no_tag = std::stoi(token.substr(0, bracket));
            std::size_t const close = token.find(']', bracket);
            int const inst_idx = std::stoi(token.substr(bracket + 1, close - bracket - 1));
            out.group_path.emplace_back(no_tag, inst_idx);
        }
        pos = is_last ? path.size() : dot + 1;
    }
    return out;
}

// Builds a message BODY (no header/trailer) from an intent-file Message's
// fields, in FILE ORDER, opening/advancing/closing groups as the path
// changes (LIFO, matching body_builder's stack discipline). Author order is
// preserved verbatim within a nesting level, matching C-8/C1's "derived from
// builder inputs" rule.
[[nodiscard]] inline fixpp::core::expected_t<std::span<std::byte>> build_body_from_intent(
    std::span<std::byte> out, std::string_view msg_type,
    std::vector<fixpp::interop::intent::FieldEntry> const& fields)
{
    fixpp::wire::body_builder bb(msg_type);

    struct Level {
        int no_tag = 0;
        fixpp::wire::group_handle handle;
        int cur_instance = -1;
        fixpp::wire::entry_handle entry;
    };
    std::vector<Level> stack;

    for (auto const& f : fields) {
        ParsedPath const parsed = parse_field_path(f.path);

        // Close every open level whose GROUP TAG no longer matches this
        // path (a level count decrease, or the no_tag itself changing) --
        // NOT on an instance-index change: moving to the NEXT instance of
        // an ALREADY-OPEN group (e.g. NoPartySubIDs[0] -> [1] within the
        // SAME NoPartyIDs entry) must keep the group_handle open and just
        // add_entry() again (below), never close+reopen it -- reopening
        // would emit a SECOND, separate NoXxx block instead of growing the
        // first one, breaking the NumInGroup count the peer validates.
        std::size_t common = 0;
        while (common < stack.size() && common < parsed.group_path.size() &&
               stack[common].no_tag == parsed.group_path[common].first) {
            ++common;
        }
        while (stack.size() > common) {
            auto const closed = stack.back();
            stack.pop_back();
            if (auto r = bb.group_end(closed.handle); !r.has_value()) {
                return std::unexpected(r.error());
            }
        }
        // Open (if new) / advance (if the instance index moved) EVERY level
        // the path names -- including levels below `common`, whose handle
        // survived the close above but may still need a new instance.
        for (std::size_t i = 0; i < parsed.group_path.size(); ++i) {
            int const no_tag = parsed.group_path[i].first;
            int const inst_idx = parsed.group_path[i].second;
            if (i == stack.size()) {
                Level lvl;
                lvl.no_tag = no_tag;
                int const delim = group_delimiter_tag(no_tag);
                if (i == 0) {
                    auto r = bb.group_begin(static_cast<std::uint16_t>(no_tag),
                                            static_cast<std::uint16_t>(delim));
                    if (!r.has_value()) return std::unexpected(r.error());
                    lvl.handle = *r;
                } else {
                    auto r = stack[i - 1].entry.group_begin(static_cast<std::uint16_t>(no_tag),
                                                             static_cast<std::uint16_t>(delim));
                    if (!r.has_value()) return std::unexpected(r.error());
                    lvl.handle = *r;
                }
                stack.push_back(lvl);
            }
            if (stack[i].cur_instance != inst_idx) {
                auto r = stack[i].handle.add_entry();
                if (!r.has_value()) return std::unexpected(r.error());
                stack[i].entry = *r;
                stack[i].cur_instance = inst_idx;
            }
        }

        if (parsed.group_path.empty()) {
            if (auto r = bb.field(static_cast<std::uint16_t>(parsed.leaf_tag), f.value);
                !r.has_value()) {
                return std::unexpected(r.error());
            }
        } else {
            if (auto r = stack.back().entry.set_string(static_cast<std::uint16_t>(parsed.leaf_tag),
                                                        f.value);
                !r.has_value()) {
                return std::unexpected(r.error());
            }
        }
    }
    while (!stack.empty()) {
        auto const closed = stack.back();
        stack.pop_back();
        if (auto r = bb.group_end(closed.handle); !r.has_value()) {
            return std::unexpected(r.error());
        }
    }
    return bb.commit(out);
}

// The `NoXxx` COUNT paths a group-bearing intent field list implies, but
// never spells out as its own entry (the script declares MEMBER paths only
// -- e.g. "453[0].448" -- never a literal "453" count). The peer's readback
// walk (data-model.md §2, "every body field the peer parsed") DOES report
// the count field -- it is genuinely on the wire -- so the `sent` record's
// `fields` must carry it too, or FR-006's exact-set comparison sees it as
// `spurious` on every group-bearing step. Returns one FieldEntry per open
// group level actually used, keyed on the SAME path grammar body_builder
// itself emits ("453" for the top-level count, "453[0].802" for a nested
// one) -- e.g. two entries for B-01 (spec.md § Conversation census): {"453",
// "2"} and, one level down, {"453[0].802","2"} + {"453[1].802","1"}.
inline std::vector<fixpp::interop::intent::FieldEntry> derive_group_count_fields(
    std::vector<fixpp::interop::intent::FieldEntry> const& fields)
{
    // key: the path PREFIX identifying the parent context ("" for the root,
    // "453[0]" for inside PartyID instance 0); value: no_tag -> highest
    // instance index seen + 1 (the count).
    std::map<std::string, std::map<int, int>> counts;
    for (auto const& f : fields) {
        ParsedPath const parsed = parse_field_path(f.path);
        std::string prefix;
        for (auto const& [no_tag, inst_idx] : parsed.group_path) {
            auto& count = counts[prefix][no_tag];
            count = std::max(count, inst_idx + 1);
            prefix += (prefix.empty() ? "" : ".") + std::to_string(no_tag) + "[" +
                      std::to_string(inst_idx) + "]";
        }
    }
    std::vector<fixpp::interop::intent::FieldEntry> out;
    for (auto const& [prefix, by_tag] : counts) {
        for (auto const& [no_tag, count] : by_tag) {
            std::string const path = prefix.empty() ? std::to_string(no_tag)
                                                     : prefix + "." + std::to_string(no_tag);
            out.push_back({path, std::to_string(count)});
        }
    }
    return out;
}

// Flat top-level BODY-field walk (data-model.md §2): every field in the
// frame EXCLUDING the canonical header/trailer union (C-6). No group
// expansion — none of this combo's peer-originated steps declare one.
template <fixpp::wire::access_mode Mode>
inline std::vector<fixpp::interop::readback::FieldEntry> collect_body_fields(
    fixpp::wire::MessageView<Mode> const& msg)
{
    std::vector<fixpp::interop::readback::FieldEntry> out;
    for (auto it = msg.begin(); !(it == msg.end()); ++it) {
        auto const& f = *it;
        if (fixpp::interop::readback::is_canonical_header_or_trailer_tag(f.tag)) {
            continue;
        }
        std::string value(reinterpret_cast<char const*>(f.value.data()), f.value.size());
        out.push_back({std::to_string(f.tag), std::move(value)});
    }
    return out;
}

// A small, script-scoped FIX 4.4 datatype map — data-model.md §2's
// `TypedEntry.fix_type` ("evidence of dictionary-backed resolution", NOT
// accessor provenance; the comparator (witness_comparator.hpp ParsedRecord)
// does not read `typed_reads` at all — see file scope note there). Covers
// exactly the tags this script's `typed_reads` declarations name (FIX44.xml
// datatypes, verified against the production dictionary).
inline std::string fix_type_for_tag(int tag)
{
    static const std::map<int, std::string> kTypes = {
        {1, "STRING"},     {6, "PRICE"},      {11, "STRING"},    {14, "QTY"},
        {17, "STRING"},    {37, "STRING"},    {38, "QTY"},       {39, "CHAR"},
        {40, "CHAR"},      {41, "STRING"},    {44, "PRICE"},     {45, "SEQNUM"},
        {54, "CHAR"},      {55, "STRING"},    {60, "UTCTIMESTAMP"},
        {102, "INT"},      {112, "STRING"},   {150, "CHAR"},     {151, "QTY"},
        {371, "INT"},      {372, "STRING"},   {373, "INT"},      {434, "CHAR"},
    };
    auto it = kTypes.find(tag);
    return it == kTypes.end() ? std::string() : it->second;
}

}  // namespace fixpp::interop::conversation
