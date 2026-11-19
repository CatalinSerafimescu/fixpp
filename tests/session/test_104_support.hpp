// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_104_support.hpp — test-only helpers shared by 104-sequence-reset-fidelity's
// own test files (test_104_*.cpp), in the shape of the 101 and 103 feature headers. Each is a
// helper more than one of those files needs; a helper one file needs stays in that file.
#pragma once

#include <algorithm>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <chrono>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_event.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include "session/resend_gap_scene.hpp"
#include "support/session_test_access.hpp"

namespace fixpp::test_support::t104 {

// A pair of next numbers, inbound and outbound: the manager's or a store's.
struct Numbers104 {
    fixpp::session::seqnum_t in = 0;
    fixpp::session::seqnum_t out = 0;
    bool operator==(Numbers104 const&) const = default;
};

// The session's SeqnumManager numbers, read with no lock (the cell's own strand turn).
[[nodiscard]] inline Numbers104 manager_numbers(fixpp::session::Session& sess) {
    auto& m = fixpp::session::session_test_access::seqnum_mgr(sess);
    return {.in = m.next_inbound_unsafe(), .out = m.next_outbound_unsafe()};
}

// A store's next inbound and outbound numbers, read through next_seqnum(dir, false) by a
// coroutine spawned on `ioc`, then `pump_until(pred)` run until both reads return. Empty when
// a read fails or `pump_until` returns false.
template <class PumpUntil>
[[nodiscard]] std::optional<Numbers104> read_store_numbers(fixpp::session::MessageStore* st,
                                                           asio::io_context& ioc,
                                                           PumpUntil&& pump_until) {
    if (st == nullptr) return std::nullopt;
    auto const read = std::make_shared<std::optional<Numbers104>>();
    auto const done = std::make_shared<bool>(false);
    asio::co_spawn(
        ioc,
        [st, read, done]() -> asio::awaitable<void> {
            auto in = co_await st->next_seqnum(fixpp::session::direction_t::inbound, false);
            auto out = co_await st->next_seqnum(fixpp::session::direction_t::outbound, false);
            if (in && out) *read = Numbers104{.in = *in, .out = *out};
            *done = true;
        },
        asio::detached);
    if (!pump_until([done] { return *done; })) return std::nullopt;
    return *read;
}

// The same read on a resend_gap Scene, pumped by its pump_until within `budget`.
[[nodiscard]] inline std::optional<Numbers104> read_store_numbers(
    fixpp::session::MessageStore* st, fixpp::test_support::resend_gap::Scene& s,
    std::chrono::steady_clock::duration budget = fixpp::test_support::kPumpBudget) {
    return read_store_numbers(
        st, s.ioc, [&s, budget](auto pred) { return s.pump_until(std::move(pred), budget); });
}

// True when the session's recent events hold a CompID-authorisation failure.
[[nodiscard]] inline bool compid_authorization_failed(fixpp::session::Session const& sess) {
    return std::ranges::any_of(sess.recent_events(), [](auto const& ev) {
        return std::holds_alternative<fixpp::session::session_event_compid_authorization_failed>(
            ev);
    });
}

// A transport_send sink that throws std::runtime_error(`what`) on a frame for which
// `should_throw` returns true, and otherwise forwards the frame to `sink`.
[[nodiscard]] inline std::function<void(std::span<const std::byte>)> throwing_sink(
    std::function<void(std::span<const std::byte>)> sink,
    std::function<bool(std::span<const std::byte>)> should_throw, std::string what) {
    return [sink = std::move(sink), should_throw = std::move(should_throw),
            what = std::move(what)](std::span<const std::byte> f) {
        if (should_throw(f)) {
            throw std::runtime_error(what);
        }
        sink(f);
    };
}

}  // namespace fixpp::test_support::t104
