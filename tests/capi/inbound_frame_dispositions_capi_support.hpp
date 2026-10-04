// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/inbound_frame_dispositions_capi_support.hpp — 093-inbound-frame-dispositions
// (contract C-7): a C-ABI initiator engine over the bundled FIX 4.4 dictionary, facing a
// raw TCP acceptor peer that writes hand-built frames.
//
// Shared by inbound_frame_dispositions_capi_test.cpp (T090, T091) and
// inbound_frame_dispositions_c7_witness_test.cpp (T102). It names no symbol 093 adds,
// so it and the witness compile against the merge-base tree, where tasks.md T102 runs
// the witness. Keep it that way: a 093 symbol here breaks that run.
//
// The engine side is driven through the public C ABI only: the dictionary comes from
// fixpp_dict_load_from_xml and the endpoint from fixpp_session_config_set_tcp_endpoint.
// session_id_of (capi_loopback_support.hpp) reaches behind the opaque config only to
// key the drain wait that precedes fixpp_session_close.
//
// Every peer operation is bounded. A read that misses its bound is cancelled, not
// closed, so the peer can still write afterwards; its handler retires before the
// locals it references go out of scope.
#pragma once

#include <gtest/gtest.h>

#include <array>
#include <asio/buffer.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/write.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "capi_drain_support.hpp"
#include "capi_loopback_support.hpp"
#include "fix/c_api/dict.h"
#include "fix/c_api/engine.h"
#include "fix/c_api/message.h"
#include "fix/c_api/session.h"
#include "fix/c_api/version.h"
#include "fixpp/core/fix_time.hpp"

namespace fixpp::capi_test::ifd {

inline constexpr char const* kEngineCompId = "INI-93";
inline constexpr char const* kPeerCompId = "ACC-93";

// The bound on every peer step that waits for the engine, except where a cell derives
// its own from a competing timeout.
inline constexpr std::chrono::milliseconds kStepBudget{5000};

// ── Frames ──────────────────────────────────────────────────────────────────

// SendingTime(52) from the real clock: the C-ABI engine runs a real-time clock and
// checks SendingTime against it.
inline std::string utc_now_sending_time() {
    std::array<char, 32> buf{};
    auto r = fixpp::core::utc_time_to_fix_string(std::chrono::system_clock::now(),
                                                 fixpp::core::fix_time_precision::millis,
                                                 std::span<char>{buf});
    if (!r) {
        ADD_FAILURE() << "utc_time_to_fix_string failed";
        return {};
    }
    return std::string{r->data(), r->size()};
}

// `tag=value<SOH>` for each pair, in order.
inline std::string fix_fields(std::initializer_list<std::pair<int, std::string>> fields) {
    std::string out;
    for (auto const& [tag, value] : fields) {
        out += std::to_string(tag) + "=" + value + "\x01";
    }
    return out;
}

// A FIX 4.4 frame around `body`, which is everything between BodyLength(9) and
// CheckSum(10), in the order given.
inline std::string frame44(std::string const& body) {
    std::string full =
        "8=FIX.4.4\x01"
        "9=" +
        std::to_string(body.size()) + "\x01" + body;
    unsigned int cs = 0;
    for (unsigned char c : full) cs += c;
    char csbuf[4];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs & 0xFFU);
    return full + "10=" + csbuf + "\x01";
}

// The standard header fields after MsgType(35), from the peer to the engine.
inline std::string peer_header(std::uint32_t seq) {
    return fix_fields({{34, std::to_string(seq)},
                       {49, kPeerCompId},
                       {52, utc_now_sending_time()},
                       {56, kEngineCompId}});
}

// The peer's Logon reply. The engine's initiator sets ResetSeqNumFlag(141)=Y, so the
// reply carries it too.
inline std::string logon_reply(std::uint32_t heartbeat_s) {
    return frame44("35=A\x01" + peer_header(1) +
                   fix_fields({{98, "0"}, {108, std::to_string(heartbeat_s)}, {141, "Y"}}));
}

// The fields of a NewOrderSingle after the header, carrying ClOrdID(11) = `cl_ord_id`.
inline std::string order_fields(std::string_view cl_ord_id) {
    return fix_fields({{11, std::string{cl_ord_id}},
                       {21, "1"},
                       {55, "X"},
                       {54, "1"},
                       {60, "20240101-00:00:00.000"},
                       {40, "1"}});
}

// A NewOrderSingle from the peer.
inline std::string order(std::uint32_t seq, std::string_view cl_ord_id) {
    return frame44("35=D\x01" + peer_header(seq) + order_fields(cl_ord_id));
}

inline std::string heartbeat(std::uint32_t seq) { return frame44("35=0\x01" + peer_header(seq)); }

inline std::string test_request(std::uint32_t seq, std::string_view id) {
    return frame44("35=1\x01" + peer_header(seq) + fix_fields({{112, std::string{id}}}));
}

// How frame44_of_size fills a frame: `dense`, with fields `pad_tag=<SOH>` (the last
// takes the remainder as value bytes), or `one_field`, with a single `pad_tag` field
// whose value takes the whole remainder.
enum class Pad { dense, one_field };

// A FIX 4.4 frame of exactly `size` bytes: `head` (from MsgType(35) on), then fields
// with tag `pad_tag` to fill the rest, shaped by `pad`. Empty if `size` cannot be met.
inline std::string frame44_of_size(std::string const& head, std::size_t size,
                                   std::string_view pad_tag, Pad pad) {
    std::size_t const fixed = std::string_view{"8=FIX.4.4\x01"}.size() +
                              std::string_view{"10=000\x01"}.size() + 3U;  // "9=" and SOH
    for (std::size_t digits = 1; digits < 8; ++digits) {
        if (size < fixed + digits) return {};
        std::size_t const body_len = size - fixed - digits;
        if (std::to_string(body_len).size() != digits) continue;
        std::size_t const empty_field = pad_tag.size() + 2U;  // "<tag>=<SOH>"
        if (body_len < head.size() + empty_field) return {};
        std::size_t const fill = body_len - head.size();
        std::size_t const n = pad == Pad::dense ? fill / empty_field : 1U;
        std::size_t const rem = fill - n * empty_field;
        std::string body = head;
        body.reserve(body_len);
        for (std::size_t i = 0; i + 1 < n; ++i) {
            body.append(pad_tag);
            body += "=\x01";
        }
        body.append(pad_tag);
        body += "=";
        body.append(rem, 'x');
        body += "\x01";
        std::string f = frame44(body);
        return f.size() == size ? f : std::string{};
    }
    return {};
}

// ── The raw acceptor peer ───────────────────────────────────────────────────

class RawAcceptor {
public:
    RawAcceptor() {
        asio::ip::tcp::endpoint const any_port{asio::ip::make_address("127.0.0.1"), 0};
        std::error_code ec;
        listener_.open(any_port.protocol(), ec);
        if (!ec) listener_.bind(any_port, ec);
        if (!ec) listener_.listen(asio::socket_base::max_listen_connections, ec);
        listening_ = !ec;
        if (ec) ADD_FAILURE() << "peer listen: " << ec.message();
    }

    RawAcceptor(RawAcceptor const&) = delete;
    RawAcceptor& operator=(RawAcceptor const&) = delete;

    ~RawAcceptor() { disconnect(); }

    [[nodiscard]] bool listening() const { return listening_; }

    [[nodiscard]] std::uint16_t port() const {
        std::error_code ec;
        auto const ep = listener_.local_endpoint(ec);
        return ec ? std::uint16_t{0} : ep.port();
    }

    // Accepts the engine's connection. The listener is closed either way: the
    // initiator connects once.
    [[nodiscard]] bool accept(std::chrono::milliseconds budget = kStepBudget) {
        bool done = false;
        std::error_code aec;
        listener_.async_accept(sock_, [&](std::error_code e) {
            done = true;
            aec = e;
        });
        run_until(std::chrono::steady_clock::now() + budget);
        std::error_code ec;
        listener_.close(ec);
        if (!done) {
            ioc_.restart();
            ioc_.run();
            return false;
        }
        return !aec;
    }

    // The next whole frame the engine sent, or nullopt at `until`, EOF or an error.
    [[nodiscard]] std::optional<std::string> read_frame(
        std::chrono::steady_clock::time_point until) {
        for (;;) {
            if (auto f = pop_frame()) return f;
            if (!read_more(until)) return std::nullopt;
        }
    }

    // Reads frames until one satisfies `pred`, which is returned.
    [[nodiscard]] std::optional<std::string> read_until(
        std::function<bool(std::string const&)> const& pred,
        std::chrono::milliseconds budget = kStepBudget) {
        auto const until = std::chrono::steady_clock::now() + budget;
        for (;;) {
            auto f = read_frame(until);
            if (!f) return std::nullopt;
            if (pred(*f)) return f;
        }
    }

    // Reads the engine's Logon.
    [[nodiscard]] bool read_logon(std::chrono::milliseconds budget = kStepBudget) {
        return read_until([](std::string const& f) { return has_field(f, "35=A"); }, budget)
            .has_value();
    }

    // Reads, discarding frames, until the engine closes the connection; true if it did
    // before `until`.
    [[nodiscard]] bool wait_eof(std::chrono::steady_clock::time_point until) {
        for (;;) {
            while (pop_frame()) {
            }
            if (eof_) return true;
            if (!read_more(until)) return eof_;
        }
    }

    // Sends a TestRequest and reads up to the Heartbeat that answers it, so every frame
    // written before it has been processed. False if no answer comes.
    [[nodiscard]] bool fence(std::uint32_t seq, std::string_view id,
                             std::chrono::milliseconds budget = kStepBudget) {
        if (!write(test_request(seq, id))) return false;
        std::string const want = "112=" + std::string{id};
        return read_until(
                   [&](std::string const& f) { return has_field(f, "35=0") && has_field(f, want); },
                   budget)
            .has_value();
    }

    bool write(std::string_view bytes) {
        std::error_code ec;
        asio::write(sock_, asio::buffer(bytes.data(), bytes.size()), ec);
        return !ec;
    }

    void disconnect() {
        std::error_code ec;
        sock_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        sock_.close(ec);
        listener_.close(ec);
    }

    [[nodiscard]] bool eof() const { return eof_; }

    // True if the frame holds the whole field `tag=value` (between SOHs).
    static bool has_field(std::string_view frame, std::string_view tag_eq_value) {
        std::string const needle = "\x01" + std::string{tag_eq_value} + "\x01";
        return frame.find(needle) != std::string_view::npos;
    }

private:
    void run_until(std::chrono::steady_clock::time_point until) {
        ioc_.restart();
        ioc_.run_until(until);
    }

    // One bounded read appended to buf_. False on a miss (the read is cancelled; the
    // socket stays usable), on EOF or on an error.
    bool read_more(std::chrono::steady_clock::time_point until) {
        if (eof_ || failed_) return false;
        std::array<char, 8192> chunk{};
        bool done = false;
        std::error_code rec;
        std::size_t n = 0;
        sock_.async_read_some(asio::buffer(chunk), [&](std::error_code e, std::size_t k) {
            done = true;
            rec = e;
            n = k;
        });
        run_until(until);
        if (!done) {
            std::error_code ec;
            sock_.cancel(ec);
            ioc_.restart();
            ioc_.run();
        }
        buf_.append(chunk.data(), n);
        if (rec == asio::error::eof) {
            eof_ = true;
        } else if (rec && rec != asio::error::operation_aborted) {
            failed_ = true;
        }
        return done && !rec;
    }

    // A whole frame from the front of buf_: up to the SOH after CheckSum(10)'s three
    // digits.
    std::optional<std::string> pop_frame() {
        constexpr std::string_view kTrailer{
            "\x01"
            "10="};
        auto const at = buf_.find(kTrailer);
        if (at == std::string::npos) return std::nullopt;
        std::size_t const end = at + kTrailer.size() + 4U;
        if (buf_.size() < end || buf_[end - 1U] != '\x01') return std::nullopt;
        std::string f = buf_.substr(0, end);
        buf_.erase(0, end);
        return f;
    }

    asio::io_context ioc_;
    asio::ip::tcp::acceptor listener_{ioc_};
    asio::ip::tcp::socket sock_{ioc_};
    std::string buf_;
    bool listening_ = false;
    bool eof_ = false;
    bool failed_ = false;
};

// ── The C-ABI initiator ─────────────────────────────────────────────────────

// What the callbacks saw: the ClOrdID(11) of each message the receive callback was
// invoked for, and the number of toApp invocations. Written on the engine's worker.
struct Recorder {
    std::atomic<int> to_app{0};
    std::mutex mu;
    std::vector<std::string> received;

    std::vector<std::string> received_snapshot() {
        std::scoped_lock const lock{mu};
        return received;
    }

    bool received_id(std::string_view id) {
        std::scoped_lock const lock{mu};
        for (auto const& r : received) {
            if (r == id) return true;
        }
        return false;
    }
};

inline void record_receive(const fixpp_msg_t* inbound, void* ud) {
    auto* rec = static_cast<Recorder*>(ud);
    char const* value = nullptr;
    std::size_t len = 0;
    std::string id;
    if (fixpp_msg_get_string(inbound, 11, &value, &len) == FIXPP_ERR_OK && value != nullptr) {
        id.assign(value, len);
    }
    std::scoped_lock const lock{rec->mu};
    rec->received.push_back(std::move(id));
}

inline fixpp_toapp_verdict record_to_app(const fixpp_msg_t* /*outbound*/, void* ud) {
    static_cast<Recorder*>(ud)->to_app.fetch_add(1);
    return FIXPP_TOAPP_SEND;
}

// A NewOrderSingle payload for fixpp_session_send.
inline std::string order_payload(std::string_view cl_ord_id) {
    return "35=D\x01" + order_fields(cl_ord_id);
}

// One C-ABI initiator engine over dictionaries/FIX44.xml, its session opened and its
// callbacks registered, connecting to 127.0.0.1:`port`. `configure` runs on the
// session config before fixpp_session_open. `recv` replaces the recording receive
// callback; its userdata is `recv_userdata`, or the Recorder when that is null.
struct CInitiator {
    fixpp_dict_t* dict = nullptr;
    fixpp_engine_t* engine = nullptr;
    fixpp_session_t* session = nullptr;
    fixpp::session::SessionId id{};
    Recorder rec;
    bool opened = false;

    CInitiator(std::uint16_t port, std::uint32_t heartbeat_s,
               std::function<void(fixpp_session_config_t*)> const& configure = {},
               fixpp_recv_cb recv = record_receive, void* recv_userdata = nullptr) {
        if (fixpp_dict_load_from_xml(FIXPP_DICT_DIR "/FIX44.xml", &dict) != FIXPP_ERR_OK) {
            ADD_FAILURE() << "fixpp_dict_load_from_xml(FIX44.xml) failed";
            return;
        }
        fixpp_engine_config_t* ec = nullptr;
        if (fixpp_engine_config_create(&ec) != FIXPP_ERR_OK ||
            fixpp_engine_config_set_realtime_clock(ec) != FIXPP_ERR_OK) {
            ADD_FAILURE() << "engine config";
            fixpp_engine_config_destroy(ec);
            return;
        }
        if (fixpp_engine_create(ec, FIXPP_C_ABI_VERSION_MAJOR, FIXPP_C_ABI_VERSION_MINOR,
                                &engine) != FIXPP_ERR_OK) {
            ADD_FAILURE() << "fixpp_engine_create failed";
            fixpp_engine_config_destroy(ec);
            return;
        }
        fixpp_session_config_t* sc = nullptr;
        if (fixpp_session_config_create(&sc) != FIXPP_ERR_OK) {
            ADD_FAILURE() << "fixpp_session_config_create failed";
            return;
        }
        bool ok =
            fixpp_session_config_set_comp_ids(sc, kEngineCompId, kPeerCompId) == FIXPP_ERR_OK &&
            fixpp_session_config_set_begin_string(sc, "FIX.4.4") == FIXPP_ERR_OK &&
            fixpp_session_config_set_role(sc, FIXPP_ROLE_INITIATOR) == FIXPP_ERR_OK &&
            fixpp_session_config_set_heartbeat_seconds(sc, heartbeat_s) == FIXPP_ERR_OK &&
            fixpp_session_config_set_security(sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr,
                                              nullptr) == FIXPP_ERR_OK &&
            fixpp_session_config_set_reset_on_logon(sc, true) == FIXPP_ERR_OK &&
            fixpp_session_config_set_reset_seqnum_policy(
                sc, FIXPP_RESET_SEQNUM_BILATERAL_LENIENT) == FIXPP_ERR_OK &&
            fixpp_session_config_set_dictionary(sc, dict) == FIXPP_ERR_OK &&
            fixpp_session_config_set_tcp_endpoint(sc, "127.0.0.1", port) == FIXPP_ERR_OK;
        if (ok && configure) configure(sc);
        id = session_id_of(sc);
        if (!ok || fixpp_session_open(engine, sc, &session) != FIXPP_ERR_OK) {
            ADD_FAILURE() << "session config or fixpp_session_open failed";
            fixpp_session_config_destroy(sc);
            return;
        }
        void* ud = recv_userdata != nullptr ? recv_userdata : &rec;
        opened = fixpp_session_register_callback(session, recv, ud) == FIXPP_ERR_OK &&
                 fixpp_session_register_send_callback(session, record_to_app, &rec) == FIXPP_ERR_OK;
        if (!opened) ADD_FAILURE() << "callback registration failed";
    }

    CInitiator(CInitiator const&) = delete;
    CInitiator& operator=(CInitiator const&) = delete;

    ~CInitiator() {
        if (engine != nullptr) fixpp_engine_destroy(engine);
        if (dict != nullptr) fixpp_dict_destroy(dict);
    }

    [[nodiscard]] bool start() { return opened && fixpp_engine_start(engine) == FIXPP_ERR_OK; }

    [[nodiscard]] bool established() {
        bool est = false;
        return fixpp_session_is_established(session, &est) == FIXPP_ERR_OK && est;
    }

    // Polls fixpp_session_is_established until true or the budget elapses.
    [[nodiscard]] bool wait_established(std::chrono::milliseconds budget = kStepBudget) {
        auto const until = std::chrono::steady_clock::now() + budget;
        for (;;) {
            if (established()) return true;
            if (std::chrono::steady_clock::now() >= until) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
    }

    fixpp_error_t send_order(std::string_view cl_ord_id) {
        std::string const p = order_payload(cl_ord_id);
        return fixpp_session_send(session, reinterpret_cast<const uint8_t*>(p.data()), p.size());
    }

    // Waits until the session has drained (bounded), then closes it.
    fixpp_error_t drain_then_close() {
        EXPECT_TRUE(wait_for_acceptor_drained(engine, id)) << "the session never drained";
        return fixpp_session_close(session);
    }
};

}  // namespace fixpp::capi_test::ifd
