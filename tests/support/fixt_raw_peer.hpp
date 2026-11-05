#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/support/fixt_raw_peer.hpp
//
// 099-fixt-appl-version-validation, W-12's and W-14's live cells (tasks.md T092, T095): a raw TCP
// peer for an initiator under test. It listens on an OS-assigned loopback port, reads the frames
// the session writes and writes the frames a test spells byte for byte, so that a frame the session
// would refuse to send can still reach it. Blocking calls are bounded: the sockets are non-blocking
// and each read polls until its budget ends, so a test that sees no frame fails rather than hangs.

#include <array>
#include <asio/buffer.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/write.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fixpp/core/fix_time.hpp>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#include "support/extract_tag.hpp"

namespace fixpp::test_support {

// SendingTime(52) from the real clock, for a session on a real-time clock.
inline std::string fix_utc_now() {
    std::array<char, 32> buf{};
    auto r = fixpp::core::utc_time_to_fix_string(std::chrono::system_clock::now(),
                                                 fixpp::core::fix_time_precision::millis,
                                                 std::span<char>{buf});
    return r ? std::string{r->data(), r->size()} : std::string{};
}

// A whole frame with `begin_string` around `body`, which starts at its 35: BodyLength and CheckSum
// computed, so a test can spell whatever else the frame holds.
inline std::string fix_frame(const std::string& begin_string, const std::string& body) {
    std::string s =
        "8=" + begin_string + "\x01" + "9=" + std::to_string(body.size()) + "\x01" + body;
    unsigned sum = 0;
    for (const unsigned char ch : s) {
        sum += ch;
    }
    std::array<char, 16> trailer{};
    std::snprintf(trailer.data(), trailer.size(), "10=%03u\x01", sum % 256U);
    return s + trailer.data();
}

inline std::string fixt_frame(const std::string& body) { return fix_frame("FIXT.1.1", body); }

// The first value of `tag` in a frame, or the empty string. For frames holding no Data field.
inline std::string frame_field(const std::string& frame, int tag) {
    return extract_tag(std::as_bytes(std::span{frame}), static_cast<std::uint32_t>(tag));
}

class FixtRawPeer {
public:
    FixtRawPeer()
        : acceptor_(ioc_, asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), 0}),
          sock_(ioc_) {
        acceptor_.non_blocking(true);
    }

    [[nodiscard]] std::uint16_t port() const { return acceptor_.local_endpoint().port(); }

    // True once the session under test has connected.
    [[nodiscard]] bool accept(std::chrono::milliseconds budget) {
        const auto end = std::chrono::steady_clock::now() + budget;
        while (std::chrono::steady_clock::now() < end) {
            std::error_code ec;
            acceptor_.accept(sock_, ec);
            if (!ec) {
                sock_.non_blocking(true);
                return true;
            }
            if (ec != asio::error::would_block && ec != asio::error::try_again) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        return false;
    }

    // The next frame for which `pred` holds, skipping others; empty when none came in `budget`.
    [[nodiscard]] std::optional<std::string> read_until(
        const std::function<bool(const std::string&)>& pred, std::chrono::milliseconds budget) {
        const auto end = std::chrono::steady_clock::now() + budget;
        while (true) {
            while (auto frame = pop_frame()) {
                if (pred(*frame)) {
                    return frame;
                }
            }
            if (std::chrono::steady_clock::now() >= end) {
                return std::nullopt;
            }
            std::array<char, 8192> chunk{};
            std::error_code ec;
            const std::size_t n = sock_.read_some(asio::buffer(chunk), ec);
            if (!ec) {
                buf_.append(chunk.data(), n);
            } else if (ec == asio::error::would_block || ec == asio::error::try_again) {
                std::this_thread::sleep_for(std::chrono::milliseconds{2});
            } else {
                return std::nullopt;
            }
        }
    }

    // The next frame of MsgType `msg_type`.
    [[nodiscard]] std::optional<std::string> read_msg_type(std::string_view msg_type,
                                                           std::chrono::milliseconds budget) {
        const std::string needle =
            "\x01"
            "35=" +
            std::string{msg_type} + "\x01";
        return read_until(
            [&needle](const std::string& f) { return f.find(needle) != std::string::npos; },
            budget);
    }

    [[nodiscard]] bool write(const std::string& frame) {
        std::error_code ec;
        sock_.non_blocking(false, ec);
        if (ec) {
            return false;
        }
        asio::write(sock_, asio::buffer(frame), ec);
        const bool ok = !ec;
        sock_.non_blocking(true, ec);
        return ok && !ec;
    }

    void close() {
        std::error_code ec;
        sock_.close(ec);
        acceptor_.close(ec);
    }

private:
    std::optional<std::string> pop_frame() {
        const std::size_t at = buf_.find(
            "\x01"
            "10=");
        if (at == std::string::npos || buf_.size() < at + 8 || buf_[at + 7] != '\x01') {
            return std::nullopt;
        }
        std::string frame = buf_.substr(0, at + 8);
        buf_.erase(0, at + 8);
        return frame;
    }

    asio::io_context ioc_;
    asio::ip::tcp::acceptor acceptor_;
    asio::ip::tcp::socket sock_;
    std::string buf_;
};

}  // namespace fixpp::test_support
