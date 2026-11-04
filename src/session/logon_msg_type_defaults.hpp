// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/logon_msg_type_defaults.hpp — internal session header.
//
// 099-fixt-appl-version-validation (data-model E-10, FR-024, FR-030, FR-031): what a Logon's
// NoMsgTypes group says, by one rule, validating or not. `interpret_logon` does not read the group.
//
// The walk uses the transport route's hooks, so a Data field is not read as a group member. The
// count is the value `consume_group` reads, so the scan and the gate read one count.
//
// `out` is called once per instance consumed, in frame order, counted or not, so that S3's block
// can refuse on an uncounted entry and the record site can keep the subset E-10 names. Nothing here
// allocates: the count comes from the peer, and an instance holds views into the frame.
//
// Internal header: NOT under include/fixpp/.
#pragma once

#include <cstddef>
#include <cstdint>
#include <fixpp/wire/dict_hooks.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/tag_scan.hpp>
#include <optional>
#include <span>
#include <string_view>

namespace fixpp::session::detail {

// One instance as read. A member the instance does not carry is empty; a member with an empty value
// holds an empty view.
struct logon_msg_type_instance {
    std::string_view ref_msg_type;                    // 372: every instance starts at one
    std::optional<std::string_view> msg_direction;    // 385
    std::optional<std::string_view> ref_appl_ver_id;  // 1130
    std::optional<std::string_view> default_ver;      // 1410

    // E-10: an entry counts when 385 is S and 1410 is Y.
    [[nodiscard]] bool counted() const noexcept {
        return msg_direction == std::string_view{"S"} && default_ver == std::string_view{"Y"};
    }
};

namespace msg_type_default_detail {

[[nodiscard]] inline std::string_view text(std::span<const std::byte> value) noexcept {
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

// E-10: 384 absent, not all ASCII digits, or 0, is no entries.
[[nodiscard]] inline std::uint32_t declared_count(std::span<const std::byte> value) noexcept {
    if (value.empty()) {
        return 0;
    }
    for (std::byte const b : value) {
        auto const c = static_cast<unsigned char>(b);
        if (c < '0' || c > '9') {
            return 0;
        }
    }
    return fixpp::wire::parse_bounded_u32(value);
}

}  // namespace msg_type_default_detail

template <class Out>
void scan_logon_msg_type_defaults(std::span<const std::byte> frame,
                                  fixpp::wire::dict_hooks const& hooks, Out&& out) noexcept {
    using iter_t = fixpp::wire::MessageView<fixpp::wire::access_mode::Iter>::field_iterator;
    constexpr std::uint16_t kNoMsgTypes = 384;
    constexpr std::uint16_t kRefMsgType = 372;
    constexpr std::uint16_t kMsgDirection = 385;
    constexpr std::uint16_t kRefApplVerId = 1130;
    constexpr std::uint16_t kRefApplExtId = 1406;
    constexpr std::uint16_t kRefCstmApplVerId = 1131;
    constexpr std::uint16_t kDefaultVerIndicator = 1410;

    iter_t it{frame, 0, hooks};
    iter_t const end{frame, frame.size(), hooks};
    while (!(it == end) && (*it).tag != kNoMsgTypes) {
        ++it;
    }
    if (it == end) {
        return;
    }
    std::uint32_t remaining = msg_type_default_detail::declared_count((*it).value);
    if (remaining == 0) {
        return;
    }
    std::optional<logon_msg_type_instance> instance;
    for (++it; !(it == end); ++it) {
        std::uint16_t const tag = (*it).tag;
        std::string_view const value = msg_type_default_detail::text((*it).value);
        if (tag == kRefMsgType) {
            if (instance) {
                out(*instance);
                instance.reset();
            }
            if (remaining == 0) {
                return;
            }
            --remaining;
            instance.emplace(logon_msg_type_instance{.ref_msg_type = value,
                                                     .msg_direction = std::nullopt,
                                                     .ref_appl_ver_id = std::nullopt,
                                                     .default_ver = std::nullopt});
            continue;
        }
        if (!instance) {
            return;  // the first tag past 384 does not start an instance
        }
        // A member repeated within one instance reads first-wins, as MessageView::get does.
        if (tag == kMsgDirection) {
            if (!instance->msg_direction) {
                instance->msg_direction = value;
            }
        } else if (tag == kRefApplVerId) {
            if (!instance->ref_appl_ver_id) {
                instance->ref_appl_ver_id = value;
            }
        } else if (tag == kDefaultVerIndicator) {
            if (!instance->default_ver) {
                instance->default_ver = value;
            }
        } else if (tag != kRefApplExtId && tag != kRefCstmApplVerId) {
            break;  // the first non-member, a second 384 among them
        }
    }
    if (instance) {
        out(*instance);
    }
}

}  // namespace fixpp::session::detail
