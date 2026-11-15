// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_103_support.hpp — test-only.
//
// Feature 103 (resend and Logon-reset fidelity; specs/103-resend-logon-reset-fidelity/,
// tasks T016): the helpers every 103 cell shares. They read the frames a session handed its
// transport callback, held as one std::string per frame, in the order written. Only API that
// exists without 103 is used, so a cell built on these compiles against the base tree.
//
// The fixtures themselves are reused by inclusion, never copied: resend_gap_scene.hpp's
// Scene and mock_reconnect_factory.hpp's MockReconnectFactory come in through this header.
// A target that includes it defines FIXPP_ALLOW_MOCK_TRANSPORT (the mock transport header
// refuses one that does not); add_session_103_bucket does.
#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/io_context.hpp>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "session/mock_reconnect_factory.hpp"
#include "session/resend_gap_scene.hpp"
#include "support/pump_until_ready.hpp"

namespace fixpp::test_support::t103 {

// The value of the first `<SOH>tag=` field of one frame, or nullopt when the frame carries no
// such field. An empty value is returned as an empty string, so "absent" and "present but
// empty" stay apart.
[[nodiscard]] inline std::optional<std::string> field(std::string_view const wire,
                                                      std::string_view const tag) {
    std::string const needle = "\x01" + std::string{tag} + "=";
    auto const at = wire.find(needle);
    if (at == std::string_view::npos) return std::nullopt;
    auto const v = at + needle.size();
    auto const end = wire.find('\x01', v);
    return std::string{wire.substr(v, end == std::string_view::npos ? wire.size() - v : end - v)};
}

// The fields a 103 cell asserts on, read from one frame the session wrote.
struct frame_fields {
    std::optional<std::string> msg_type;       // 35
    std::optional<std::string> seq;            // 34
    std::optional<std::string> begin_seq_no;   // 7
    std::optional<std::string> end_seq_no;     // 16
    std::optional<std::string> new_seq_no;     // 36
    std::optional<std::string> poss_dup;       // 43
    std::optional<std::string> text;           // 58
    std::optional<std::string> gap_fill;       // 123
    std::optional<std::string> reset_seq_num;  // 141
    std::optional<std::string> ref_tag_id;     // 371
    std::optional<std::string> reject_reason;  // 373
};

[[nodiscard]] inline frame_fields decode(std::string_view const wire) {
    return frame_fields{
        .msg_type = field(wire, "35"),
        .seq = field(wire, "34"),
        .begin_seq_no = field(wire, "7"),
        .end_seq_no = field(wire, "16"),
        .new_seq_no = field(wire, "36"),
        .poss_dup = field(wire, "43"),
        .text = field(wire, "58"),
        .gap_fill = field(wire, "123"),
        .reset_seq_num = field(wire, "141"),
        .ref_tag_id = field(wire, "371"),
        .reject_reason = field(wire, "373"),
    };
}

// A reconnect mark: the index into `written` at the moment it is taken. Counts "after the
// reconnect" start here, so frames the old connection wrote never count.
[[nodiscard]] inline std::size_t mark(std::vector<std::string> const& written) {
    return written.size();
}

// The number of frames of MsgType `type` at or after index `from_mark` of `written`.
[[nodiscard]] inline std::size_t count_35(std::vector<std::string> const& written,
                                          std::string_view const type,
                                          std::size_t const from_mark = 0) {
    std::size_t n = 0;
    for (std::size_t i = from_mark; i < written.size(); ++i) {
        if (field(written[i], "35") == type) ++n;
    }
    return n;
}

// SC-003: no frame in `written` carries a MsgSeqNum(34) whose value is zero. A 34 made only
// of '0' digits is zero whatever its width.
[[nodiscard]] inline bool no_frame_carries_34_zero(std::vector<std::string> const& written) {
    return std::ranges::none_of(written, [](std::string const& f) {
        auto const v = field(f, "34");
        return v.has_value() && !v->empty() &&
               std::ranges::all_of(*v, [](char const c) { return c == '0'; });
    });
}

// A bounded pump that fails the cell at its deadline instead of hanging. It runs `ioc` until
// `ready()` holds or `deadline` of real time has passed; on a miss it records a NON-FATAL
// failure naming `site` and returns false, so a cell that started an Engine can still stop
// it before asserting anything fatal. There is no default deadline: each caller derives its
// own from the timeout that competes with the mechanism it observes (tasks.md, "Bounded
// waits and Logout identity").
template <class Ready>
[[nodiscard]] bool pump_or_fail(asio::io_context& ioc, Ready ready,
                                std::chrono::steady_clock::duration const deadline,
                                char const* const site) {
    if (pump_until(ioc, std::move(ready), deadline, kPumpSlice, site)) return true;
    ADD_FAILURE() << kPumpBudgetMiss << site;
    return false;
}

}  // namespace fixpp::test_support::t103
