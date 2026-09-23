// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/copy_site_fixtures.hpp
//
// Shared frame builders and comparisons for the copy-site cap cells of fixpp#493
// (`.specify/495-493-486-dict-reify-copy.md` §10, T-2..T-6). One definition for the
// C clone cells and the C++ reify cells, so both sides copy the same shape; list
// the consumers with `grep -rln copy_site_fixtures.hpp tests`.
//
// The frames carry a computed CheckSum (reify_test_frame.hpp's `assemble_frame`), so
// they pass the Framer the reify factory re-frames its copy with.
//
// Consumers must define FIXPP_DICT_DATA_DIR (fix44_dictionary.hpp).
#pragma once

#include <algorithm>
#include <cstddef>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/wire/group_view.hpp>    // wire::group_context
#include <fixpp/wire/offset_table.hpp>  // wire::OffsetTable::Config
#include <fixpp/wire/parser.hpp>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "support/fix44_dictionary.hpp"
#include "support/frame_view_factory.hpp"
#include "support/reify_test_frame.hpp"  // assemble_frame

namespace fixpp::test_support {

// MsgType(35)=D, a marker field SenderCompID(49)=SENDERID, then `n_occurrences`
// repeats of a plain non-group tag (1=x). Once `n_occurrences + 2` exceeds
// offset_table.hpp's `default_max_offset_entries`, a DEFAULT-cap parse of this
// frame fails its OffsetTable build while a raised-cap parse admits it.
[[nodiscard]] inline std::vector<std::byte> make_oversized_frame_for_clone_test(int n_occurrences) {
    std::string body =
        "35=D\x01"
        "49=SENDERID\x01";
    for (int i = 0; i < n_occurrences; ++i) {
        body += "1=x\x01";
    }
    return assemble_frame("8=FIX.4.4\x01", body);
}

// MsgType(35)=D with SenderCompID(49)=SENDERID and one NoPartyIDs(453) instance:
// PartyID(448)=P, then `n_party_id_source` repeats of PartyIDSource(447)=D, then
// PartyRole(452)=1. FIX44 registers PartyIDSource and PartyRole as NoPartyIDs
// members, so under a dict-backed parse the whole run is ONE instance of
// `n_party_id_source + 2` entries — the lever for
// OffsetTable::Config::max_group_entries_per_instance, which is enforced lazily on the group read.
[[nodiscard]] inline std::vector<std::byte> make_long_party_instance_frame(int n_party_id_source) {
    std::string body =
        "35=D\x01"
        "49=SENDERID\x01"
        "453=1\x01"
        "448=P\x01";
    for (int i = 0; i < n_party_id_source; ++i) {
        body += "447=D\x01";
    }
    body += "452=1\x01";
    return assemble_frame("8=FIX.4.4\x01", body);
}

// Member-wise comparisons: the types carry no operator== (note §10
// "Comparisons").
[[nodiscard]] inline bool same_config(fixpp::wire::OffsetTable::Config const& a,
                                      fixpp::wire::OffsetTable::Config const& b) noexcept {
    return a.max_offset_entries == b.max_offset_entries &&
           a.max_group_entries_per_instance == b.max_group_entries_per_instance;
}

// `msg_type` by CONTENT (each side views its own message buffer), then `depth` and
// the `parent_path` prefix `[0, depth)`.
[[nodiscard]] inline bool same_group_context(fixpp::wire::group_context const& a,
                                             fixpp::wire::group_context const& b) noexcept {
    return a.msg_type == b.msg_type && a.depth == b.depth &&
           std::equal(a.parent_path.begin(), a.parent_path.begin() + a.depth,
                      b.parent_path.begin());
}

// A copy-site SOURCE view: `frame` parsed under `cfg`, dict-backed over FIX44
// when `dict_backed`, else through a dict-free `Parser{}`. Every dependency (the
// Dictionary, its table, the frame bytes, the parse arena) is a member, so the
// view lives as long as the fixture. Callers ASSERT ok() before view().
class copy_site_source {
public:
    copy_site_source(std::vector<std::byte> frame, fixpp::wire::OffsetTable::Config cfg,
                     bool dict_backed)
        : frame_(std::move(frame)) {
        if (dict_backed) {
            dict_ = make_fix44_dictionary();
            tv_.emplace(dict_->as_table_view());
        }
        auto fv = fixpp::wire::test::make_frame_view(frame_);
        if (!fv.has_value()) {
            return;
        }
        fv_ = *fv;
        std::optional<fixpp::wire::Parser<fixpp::wire::access_mode::Index>> parser;
        if (tv_) {
            parser.emplace(*tv_);
        } else {
            parser.emplace();
        }
        if (auto parsed = parser->parse(fv_, &arena_, cfg); parsed.has_value()) {
            mv_.emplace(std::move(*parsed));
        }
    }
    copy_site_source(copy_site_source const&) = delete;
    copy_site_source& operator=(copy_site_source const&) = delete;
    copy_site_source(copy_site_source&&) = delete;
    copy_site_source& operator=(copy_site_source&&) = delete;
    ~copy_site_source() = default;

    [[nodiscard]] bool ok() const noexcept { return mv_.has_value(); }
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    [[nodiscard]] fixpp::wire::MessageView<fixpp::wire::access_mode::Index> const& view()
        const noexcept {
        return *mv_;
    }

private:
    std::shared_ptr<const fixpp::dict::Dictionary> dict_;
    std::optional<fixpp::dict::table_view> tv_;
    std::vector<std::byte> frame_;
    std::pmr::monotonic_buffer_resource arena_;
    fixpp::wire::frame_view fv_{};
    std::optional<fixpp::wire::MessageView<fixpp::wire::access_mode::Index>> mv_;
};

// The marker field every copy-site frame carries: SenderCompID(49)=SENDERID.
[[nodiscard]] inline bool reads_sender_id(
    fixpp::wire::MessageView<fixpp::wire::access_mode::Index> const& v) {
    auto f = v.get(49);
    return f.has_value() && f->as_string() == "SENDERID";
}

}  // namespace fixpp::test_support
