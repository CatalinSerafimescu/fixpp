// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/codegen/fixpp-codegen/emit_manifest.cpp
//
// 076-fix-latest-typed-codegen — T012 (FR-006, data-model.md Entity 3). The
// per-message CENSUS MANIFEST, sourced from T005's projection lossless
// occurrence list (`MessageIR::occurrences`, ir.hpp — populated ONLY by
// `populate_orchestra_projection`; empty for `<fix>`-schema versions). NOT
// sourced from `MessageIR.fields` (tag-deduped, immediate-parent-only
// `group_no_tag` — lossy) nor the read-side group flyweights
// (version-wide deduped union, `emit_messages.cpp:403-419`) — see the
// `OccurrenceIR` banner in ir.hpp. Emitted for ALL messages in `ir`, ungated
// by the app-subset builder filter (T012 scope note; the builder tier was
// descoped, but the census covers all 181 regardless). `write_file`'s
// empty-string skip (main.cpp:29) means a version with no `occurrences`
// (every legacy `<fix>`-schema tier) gets no `Manifest.txt` at all — by
// design.
//
// LINE GRAMMAR (also documented in the emitted file's own header comment,
// so a census-test author can parse Manifest.txt without reading this
// file):
//   MSG <msg_type>\t<name>\t<occurrence_count>
//   OCC\t<group_path>\t<tag>\t<rule>\t<datatype>
// Fields are tab-separated; lines starting with '#' and blank lines are not
// data. `group_path` is a dot-separated chain of enclosing repeating-group
// NoInGroup tags, outermost-to-innermost ("-" denotes a top-level
// occurrence / empty chain). `rule` is one of {required, optional,
// conditional, not_declared} (`fixpp::dict::field_presence`). `datatype` is
// the lowercase snake_case `fixpp::dict::field_data_type` token. Messages
// are emitted sorted by `msg_type` (bytewise); occurrences within a message
// are sorted by `(group_path, tag)` — both orderings are imposed here for
// determinism (NFR-003-7 / AC-T1), independent of upstream declaration
// order.
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "emit.hpp"
#include "ir.hpp"
#include "template_writer.hpp"

namespace fixpp::codegen {

namespace {

// Fail-closed per FR-010 spirit: an unhandled enumerator is a build-tool
// bug, not a silently-lossy "unknown" token — throw (exceptions are
// enabled for this host tool, [arch §5.3] / CMakeLists.txt).
std::string_view rule_token(fixpp::dict::field_presence rule) {
    switch (rule) {
        case fixpp::dict::field_presence::NotDeclared:
            return "not_declared";
        case fixpp::dict::field_presence::Optional:
            return "optional";
        case fixpp::dict::field_presence::Required:
            return "required";
        case fixpp::dict::field_presence::Conditional:
            return "conditional";
    }
    throw std::logic_error("fixpp-codegen: emit_manifest — unhandled field_presence enumerator");
}

std::string_view datatype_token(fixpp::dict::field_data_type dt) {
    switch (dt) {
        case fixpp::dict::field_data_type::Int:
            return "int";
        case fixpp::dict::field_data_type::Length:
            return "length";
        case fixpp::dict::field_data_type::SeqNum:
            return "seq_num";
        case fixpp::dict::field_data_type::NumInGroup:
            return "num_in_group";
        case fixpp::dict::field_data_type::DayOfMonth:
            return "day_of_month";
        case fixpp::dict::field_data_type::Price:
            return "price";
        case fixpp::dict::field_data_type::Qty:
            return "qty";
        case fixpp::dict::field_data_type::Amt:
            return "amt";
        case fixpp::dict::field_data_type::PriceOffset:
            return "price_offset";
        case fixpp::dict::field_data_type::Percentage:
            return "percentage";
        case fixpp::dict::field_data_type::Float:
            return "float";
        case fixpp::dict::field_data_type::Char:
            return "char";
        case fixpp::dict::field_data_type::Boolean:
            return "boolean";
        case fixpp::dict::field_data_type::String:
            return "string";
        case fixpp::dict::field_data_type::MultiCharValue:
            return "multi_char_value";
        case fixpp::dict::field_data_type::MultiStringValue:
            return "multi_string_value";
        case fixpp::dict::field_data_type::Currency:
            return "currency";
        case fixpp::dict::field_data_type::Exchange:
            return "exchange";
        case fixpp::dict::field_data_type::Country:
            return "country";
        case fixpp::dict::field_data_type::MonthYear:
            return "month_year";
        case fixpp::dict::field_data_type::UtcTimestamp:
            return "utc_timestamp";
        case fixpp::dict::field_data_type::UtcTimeOnly:
            return "utc_time_only";
        case fixpp::dict::field_data_type::UtcDateOnly:
            return "utc_date_only";
        case fixpp::dict::field_data_type::LocalMktDate:
            return "local_mkt_date";
        case fixpp::dict::field_data_type::TzTimeOnly:
            return "tz_time_only";
        case fixpp::dict::field_data_type::TzTimestamp:
            return "tz_timestamp";
        case fixpp::dict::field_data_type::Language:
            return "language";
        case fixpp::dict::field_data_type::Data:
            return "data";
        case fixpp::dict::field_data_type::XmlData:
            return "xml_data";
        case fixpp::dict::field_data_type::DialectExtension:
            return "dialect_extension";
    }
    throw std::logic_error(
        "fixpp-codegen: emit_manifest — unhandled field_data_type enumerator");
}

void write_group_path(TemplateWriter& w, std::vector<std::uint16_t> const& path) {
    if (path.empty()) {
        w.raw("-");
        return;
    }
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (i != 0) {
            w.raw(".");
        }
        w.num(path[i]);
    }
}

bool occurrence_less(OccurrenceIR const& a, OccurrenceIR const& b) {
    if (a.group_path != b.group_path) {
        return a.group_path < b.group_path;  // lexicographic uint16_t vector compare
    }
    return a.tag < b.tag;
}

}  // namespace

std::string emit_manifest(VersionIR const& ir) {
    // Non-Orchestra (`<fix>`-schema) versions carry no occurrences (ir.hpp
    // OccurrenceIR banner) — nothing to census; write_file's empty-skip
    // then writes no Manifest.txt for them (main.cpp:29).
    bool any_occurrences = false;
    for (auto const& m : ir.messages) {
        if (!m.occurrences.empty()) {
            any_occurrences = true;
            break;
        }
    }
    if (!any_occurrences) {
        return {};
    }

    std::vector<MessageIR const*> msgs;
    msgs.reserve(ir.messages.size());
    for (auto const& m : ir.messages) {
        msgs.push_back(&m);
    }
    std::sort(msgs.begin(), msgs.end(), [](MessageIR const* a, MessageIR const* b) {
        return a->msg_type < b->msg_type;
    });

    TemplateWriter w;
    w.raw("# fixpp-codegen census manifest -- fixpp::");
    w.raw(ir.ns);
    w.line(" (GENERATED -- DO NOT EDIT)");
    w.line("#");
    w.line("# 076-fix-latest-typed-codegen T012 (FR-006, data-model.md Entity 3).");
    w.line("# Sourced from the Orchestra IR projection's lossless occurrence list");
    w.line("# (MessageIR::occurrences) -- NOT MessageIR.fields (tag-deduped, lossy)");
    w.line("# NOR the read-side group flyweights. Emitted for ALL messages, ungated");
    w.line("# by the app-subset builder filter.");
    w.line("#");
    w.line("# Grammar (fields are tab-separated; '#'-prefixed/blank lines are not");
    w.line("# data):");
    w.line("#   MSG <msg_type><TAB><name><TAB><occurrence_count>");
    w.line("#   OCC<TAB><group_path><TAB><tag><TAB><rule><TAB><datatype>");
    w.line("# group_path is a dot-separated chain of enclosing group NoInGroup tags,");
    w.line("# outermost-to-innermost (\"-\" = top-level / empty chain). rule is one");
    w.line("# of {required, optional, conditional, not_declared}. datatype is the");
    w.line("# lowercase snake_case field_data_type token.");
    w.line("#");
    w.line("# Messages are sorted by msg_type (bytewise); occurrences within a");
    w.line("# message are sorted by (group_path, tag).");
    w.line("#");
    w.raw("# TOTAL_MESSAGES ");
    w.num(msgs.size());
    w.line();
    w.line();

    for (auto const* m : msgs) {
        std::vector<OccurrenceIR> occs = m->occurrences;
        std::sort(occs.begin(), occs.end(), occurrence_less);

        w.raw("MSG\t");
        w.raw(m->msg_type);
        w.raw("\t");
        w.raw(m->name);
        w.raw("\t");
        w.num(occs.size());
        w.line();
        for (auto const& occ : occs) {
            w.raw("OCC\t");
            write_group_path(w, occ.group_path);
            w.raw("\t");
            w.num(occ.tag);
            w.raw("\t");
            w.raw(rule_token(occ.rule));
            w.raw("\t");
            w.raw(datatype_token(occ.datatype));
            w.line();
        }
        w.line();
    }

    return std::move(w).take();
}

}  // namespace fixpp::codegen
