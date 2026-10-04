// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/plain_engine_rig.hpp — test-only.
//
// 093-inbound-frame-dispositions: one plaintext Engine session driven over loopback
// TCP by a raw peer socket, so a cell reaches the real read pump and the real
// first-frame read (both live in src/session/engine.cpp) with bytes it chose. The
// engine clock is a mock_clock: SendingTime(52) is stamped from it, so the MaxLatency
// guard sees a fresh time, and nothing on it moves unless a cell advances it.
//
// Plaintext (insecure_plain_tcp) needs no TLS fixture directory, so no cell here can
// skip. The precedent for the engine wiring is tests/session/test_session_plaintext_roundtrip.cpp.
//
// Teardown: Rig::stop() releases every Session lease before Engine::stop(), and the
// io_context is the first member, so it outlives the engine and the peer.
#pragma once

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/connect.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <asio/write.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/pump_until_ready.hpp"

namespace fixpp::test_support::plain_rig {

inline std::vector<std::byte> to_bytes(std::string_view s) {
    std::vector<std::byte> out;
    out.reserve(s.size());
    for (char const c : s) out.push_back(static_cast<std::byte>(c));
    return out;
}

// The three-digit CheckSum(10) value of `s` (the sum of its bytes, modulo 256).
inline std::string checksum_of(std::string_view s) {
    unsigned sum = 0;
    for (unsigned char const c : s) sum += c;
    std::array<char, 4> buf{};
    std::snprintf(buf.data(), buf.size(), "%03u", sum % 256U);
    return std::string{buf.data(), 3};
}

// "8=<bs>|9=<len>|<body>10=<NNN>|", with BodyLength and CheckSum computed.
inline std::string frame(std::string_view begin_string, std::string_view body) {
    std::string head = "8=" + std::string{begin_string} + "\x01" +
                       "9=" + std::to_string(body.size()) + "\x01" + std::string{body};
    return head + "10=" + checksum_of(head) + "\x01";
}

// A frame from the peer: 35, 34, 49, 52, 56 in that order, then `extra`.
inline std::string message(std::string_view begin_string, std::string_view msg_type,
                           std::uint32_t seq, std::string_view sender, std::string_view target,
                           std::string_view sending_time, std::string_view extra = {}) {
    std::string body = "35=" + std::string{msg_type} + "\x01" + "34=" + std::to_string(seq) +
                       "\x01" + "49=" + std::string{sender} + "\x01" +
                       "52=" + std::string{sending_time} + "\x01" + "56=" + std::string{target} +
                       "\x01" + std::string{extra};
    return frame(begin_string, body);
}

// The value of the first `<SOH>tag=` field in `wire` at or after `from`, or "".
inline std::string field_value(std::string_view wire, std::string_view tag, std::size_t from = 0) {
    std::string const needle = "\x01" + std::string{tag} + "=";
    auto const at = wire.find(needle, from);
    if (at == std::string_view::npos) return {};
    auto const v = at + needle.size();
    auto const end = wire.find('\x01', v);
    return std::string{wire.substr(v, end == std::string_view::npos ? end : end - v)};
}

// Every complete frame in `wire` whose MsgType(35) is `msg_type`, as text.
inline std::vector<std::string> frames_of_type(std::string_view wire, std::string_view msg_type) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (true) {
        auto const start = wire.find("8=FIX", pos);
        if (start == std::string_view::npos) break;
        auto const cs = wire.find(
            "\x01"
            "10=",
            start);
        if (cs == std::string_view::npos) break;
        auto const end = wire.find('\x01', cs + 4);
        if (end == std::string_view::npos) break;
        std::string_view const f = wire.substr(start, end + 1 - start);
        if (field_value(f, "35") == msg_type) out.emplace_back(f);
        pos = end + 1;
    }
    return out;
}

// A raw TCP peer: writes the bytes a cell chooses, and records every byte the
// session sends until its read ends.
struct Peer {
    explicit Peer(asio::io_context& ioc) : sock{ioc} {}

    asio::ip::tcp::socket sock;
    std::string received;
    bool read_ended = false;
    std::size_t writes_done = 0;
    std::size_t writes_issued = 0;

    void start_reading() {
        asio::co_spawn(
            sock.get_executor(),
            [this]() -> asio::awaitable<void> {
                std::array<char, 4096> buf{};
                for (;;) {
                    std::error_code ec;
                    std::size_t const n = co_await sock.async_read_some(
                        asio::buffer(buf), asio::redirect_error(asio::use_awaitable, ec));
                    if (ec) {
                        read_ended = true;
                        co_return;
                    }
                    received.append(buf.data(), n);
                }
            },
            asio::detached);
    }

    // Queues `bytes`; the buffer lives in the write's own frame.
    void send(std::string bytes) {
        ++writes_issued;
        asio::co_spawn(
            sock.get_executor(),
            [this, b = std::move(bytes)]() -> asio::awaitable<void> {
                std::error_code ec;
                co_await asio::async_write(sock, asio::buffer(b),
                                           asio::redirect_error(asio::use_awaitable, ec));
                ++writes_done;
            },
            asio::detached);
    }

    [[nodiscard]] bool all_written() const noexcept { return writes_done == writes_issued; }

    void close() {
        std::error_code ec;
        sock.close(ec);
    }
};

// One plaintext session on a mock-clock Engine. Configure through cfg(), then start().
class Rig {
public:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    std::shared_ptr<fixpp::session::Application> app;
    std::unique_ptr<fixpp::session::Engine> engine;
    fixpp::session::SessionId id;
    Peer peer{ioc};
    std::unique_ptr<asio::ip::tcp::acceptor> listener;  // initiator role only
    // BeginString(8) of the session cfg() builds and of every frame msg() builds.
    std::string begin_string = "FIX.4.2";

    explicit Rig(std::shared_ptr<fixpp::session::Application> application = nullptr)
        : app{std::move(application)} {
        using namespace std::chrono;
        clock = std::make_shared<fixpp::core::mock_clock>(
            system_clock::time_point{} + seconds{1704067200}, fixpp::core::steady_time_point{},
            ioc.get_executor());
    }

    Rig(Rig const&) = delete;
    Rig& operator=(Rig const&) = delete;

    ~Rig() {
        peer.close();
        if (listener) {
            std::error_code ec;
            listener->close(ec);
        }
    }

    // The engine-side session config: this Rig's peer is "TW", the session "ISLD".
    [[nodiscard]] fixpp::session::SessionConfig cfg(
        fixpp::session::session_role role = fixpp::session::session_role::acceptor) {
        fixpp::session::SessionConfig c;
        c.sender_comp_id = "ISLD";
        c.target_comp_id = "TW";
        c.begin_string = begin_string;
        c.role = role;
        c.executor_override = ioc.get_executor();
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        c.security_profile = fixpp::session::SecurityProfile{
            fixpp::session::SecurityProfile::kind::insecure_plain_tcp};
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        c.dictionary = fixpp::test_support::make_minimal_dictionary();
        c.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        c.heartbeat_interval = std::chrono::seconds{30};
        c.logout_disconnect_timeout_ms = 500;
        c.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 0};
        c.transport_send = [](std::span<const std::byte>) {};
        return c;
    }

    // Registers `c` and starts the engine. An acceptor's listener is bound once this
    // returns true; an initiator connects to this Rig's listener, which the peer
    // accepts.
    [[nodiscard]] bool start(fixpp::session::SessionConfig c) {
        if (c.role == fixpp::session::session_role::initiator) {
            listener = std::make_unique<asio::ip::tcp::acceptor>(
                ioc, asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), 0});
            c.reconnect_endpoint =
                fixpp::transport::Endpoint{"127.0.0.1", listener->local_endpoint().port()};
            listener->async_accept(peer.sock, [this](std::error_code const& ec) {
                if (!ec) peer.start_reading();
            });
        }
        fixpp::core::EngineConfig ec;
        ec.executor = ioc.get_executor();
        ec.clock = clock;
        ec.application = app;
        engine = std::make_unique<fixpp::session::Engine>(ioc.get_executor(), std::move(ec));
        id = fixpp::session::SessionId::from_config(c);
        if (!engine->register_session(std::move(c)).has_value()) return false;
        if (!engine->start().has_value()) return false;
        if (role_is_initiator()) return true;
        return run_until([this] { return engine->acceptor_bound_endpoint(id).port != 0; });
    }

    // Acceptor only: connects the peer to the session's listener.
    [[nodiscard]] bool connect_peer() {
        auto const port = engine->acceptor_bound_endpoint(id).port;
        // Shared, so a handler still queued after a missed wait writes into live state.
        struct Outcome {
            bool done = false;
            std::error_code ec;
        };
        auto const out = std::make_shared<Outcome>();
        peer.sock.async_connect(asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), port},
                                [out](std::error_code const& ec) {
                                    out->ec = ec;
                                    out->done = true;
                                });
        if (!run_until([&] { return out->done; }) || out->ec) return false;
        peer.start_reading();
        return true;
    }

    [[nodiscard]] std::shared_ptr<fixpp::session::Session> session() const {
        return engine ? engine->lookup(id) : nullptr;
    }

    [[nodiscard]] fixpp::session::fsm_state state() const {
        auto const s = session();
        return s ? s->state() : fixpp::session::fsm_state::NotConnected;
    }

    // SendingTime(52) for a frame stamped now on the mock clock.
    [[nodiscard]] std::string sending_time() const {
        std::array<char, 32> buf{};
        auto const r = fixpp::core::utc_time_to_fix_string(
            clock->now(), fixpp::core::fix_time_precision::millis, std::span<char>{buf});
        return r ? std::string{r->data(), r->size()} : std::string{};
    }

    // A frame from the peer ("TW") to the session ("ISLD"), stamped now.
    [[nodiscard]] std::string msg(std::string_view msg_type, std::uint32_t seq,
                                  std::string_view extra = {}) const {
        return message(begin_string, msg_type, seq, "TW", "ISLD", sending_time(), extra);
    }

    [[nodiscard]] std::string logon(std::uint32_t seq = 1) const {
        return msg("A", seq,
                   "98=0\x01"
                   "108=30\x01");
    }
    [[nodiscard]] std::string heartbeat(std::uint32_t seq) const { return msg("0", seq); }

    // msg(msg_type, seq, prefix + filler) of exactly `size` bytes, or "" when no filler
    // reaches it. Dense: the filler is `<tag>=<SOH>` fields, the densest layout for a
    // one-digit tag, plus at most one longer `<tag>=x…<SOH>` for the remainder. Not
    // dense: the filler is one `<tag>=x…<SOH>` field.
    [[nodiscard]] std::string msg_of_size(std::string_view msg_type, std::uint32_t seq,
                                          std::string_view prefix, std::size_t size,
                                          std::string_view tag, bool dense) const {
        std::string const m0 = msg(msg_type, seq, prefix);
        std::size_t const body0 = std::stoul(field_value(m0, "9"));
        std::size_t const head = 3 + begin_string.size();  // "8=<bs><SOH>"
        constexpr std::size_t kTrailer = 7;                // "10=NNN<SOH>"
        std::size_t body = 0;
        for (std::size_t digits = 1; digits <= 7 && body == 0; ++digits) {
            if (size < head + 3 + digits + kTrailer) break;
            std::size_t const b = size - head - (3 + digits) - kTrailer;
            if (std::to_string(b).size() == digits) body = b;
        }
        if (body < body0) return {};
        std::size_t const fill = body - body0;
        std::size_t const unit = tag.size() + 2;  // "<tag>=<SOH>"
        std::string const empty_field = std::string{tag} + "=\x01";
        auto long_field = [&](std::size_t bytes) {
            return std::string{tag} + "=" + std::string(bytes - unit, 'x') + "\x01";
        };
        std::string filler;
        if (fill != 0U && !dense) {
            if (fill < unit) return {};
            filler = long_field(fill);
        } else if (fill != 0U) {
            std::size_t const n = fill / unit;
            std::size_t const rem = fill % unit;
            if (n == 0) return {};
            filler.reserve(fill);
            for (std::size_t i = 0; i + (rem != 0U ? 1U : 0U) < n; ++i) filler += empty_field;
            if (rem != 0U) filler += long_field(unit + rem);
        }
        std::string m = msg(msg_type, seq, std::string{prefix} + filler);
        return m.size() == size ? m : std::string{};
    }

    template <class Pred>
    [[nodiscard]] bool run_until(Pred pred,
                                 std::chrono::steady_clock::duration budget = kPumpBudget) {
        return fixpp::test_support::pump_until(ioc, std::move(pred), budget, kPumpSlice, nullptr);
    }

    // Lets every queued handler run: a bounded real-time window, for a cell whose
    // claim is that something did NOT happen.
    void settle(std::chrono::milliseconds window = std::chrono::milliseconds{50}) {
        ioc.run_for(window);
        ioc.restart();
    }

    [[nodiscard]] bool role_is_initiator() const { return listener != nullptr; }

    // Acceptor: connects, logs on and waits for Active. Initiator: waits for the
    // engine's Logon, answers it and waits for Active.
    [[nodiscard]] bool to_active() {
        if (role_is_initiator()) {
            if (!run_until([this] {
                    return !frames_of_type(peer.received, "A").empty() &&
                           state() == fixpp::session::fsm_state::LogonSent;
                })) {
                return false;
            }
        } else if (!connect_peer()) {
            return false;
        }
        peer.send(logon());
        return run_until([this] { return state() == fixpp::session::fsm_state::Active; });
    }

    // Writes `bytes`, runs until the peer's write completed, then settles for a short
    // window. The window is not a barrier: a cell that needs the bytes processed waits
    // for their effect.
    [[nodiscard]] bool deliver(std::string bytes) {
        peer.send(std::move(bytes));
        if (!run_until([this] { return peer.all_written(); })) return false;
        settle();
        return true;
    }

    void stop() {
        if (!engine) return;
        auto fut = asio::co_spawn(ioc, engine->stop(), asio::use_future);
        if (!fixpp::test_support::pump_until_ready(ioc, fut, kPumpBudget, "plain_rig::Rig::stop")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "plain_rig::Rig::stop");
            ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << "plain_rig::Rig::stop";
            return;
        }
        fut.get();
    }
};

}  // namespace fixpp::test_support::plain_rig
