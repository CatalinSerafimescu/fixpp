#pragma once
// tests/conformance/mock_transport.hpp
// T020a — MockTransport for test_fixs_rotation.cpp (seam #3).
//
// Pure in-process canned-byte replay harness. No sockets, no executor.
// Constructor takes an ordered sequence of (direction, bytes) pairs;
// runtime asserts each call matches the expected direction + payload and
// returns the recorded peer bytes.
//
// NOTE: the byte streams used by T020 (test_fixs_rotation.cpp) are
// SYNTHESISED (not a real OpenSSL capture). The contract under test is the
// cross-handshake atomicity of Pinset::snapshot(), NOT the TLS wire format.
// This is documented explicitly per the T020a brief (T020a shipping note).
//
// Cap: ≤200 LoC per [[feedback_phase_implementer_sonnet_runaway_scope]].

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace fixpp::tls::test {

enum class direction { send, recv };

struct frame {
    direction             dir;
    std::vector<std::byte> bytes;
};

// MockTransport — sequential replay of recorded send/recv frames.
class MockTransport {
 public:
    explicit MockTransport(std::vector<frame> script) noexcept
        : script_{std::move(script)}, pos_{0} {}

    // Called by the "TLS layer" when it would send bytes to the peer.
    // Asserts direction == send and payload == recorded bytes.
    void send(std::vector<std::byte> const& payload) {
        check_bounds("send");
        auto const& expected = script_[pos_];
        if (expected.dir != direction::send) {
            throw std::logic_error(
                "MockTransport: expected recv at step " + std::to_string(pos_) +
                " but got send");
        }
        if (payload != expected.bytes) {
            throw std::logic_error(
                "MockTransport: send payload mismatch at step " + std::to_string(pos_));
        }
        ++pos_;
    }

    // Called by the "TLS layer" when it would recv bytes from the peer.
    // Asserts direction == recv and returns the recorded bytes.
    std::vector<std::byte> recv() {
        check_bounds("recv");
        auto const& expected = script_[pos_];
        if (expected.dir != direction::recv) {
            throw std::logic_error(
                "MockTransport: expected send at step " + std::to_string(pos_) +
                " but got recv");
        }
        auto result = expected.bytes;
        ++pos_;
        return result;
    }

    // True when all scripted frames have been consumed.
    [[nodiscard]] bool done() const noexcept { return pos_ >= script_.size(); }

    [[nodiscard]] std::size_t step() const noexcept { return pos_; }
    [[nodiscard]] std::size_t total_steps() const noexcept { return script_.size(); }

 private:
    void check_bounds(const char* op) const {
        if (pos_ >= script_.size()) {
            throw std::out_of_range(
                std::string("MockTransport: ") + op +
                " called past end of script (step " + std::to_string(pos_) +
                " of " + std::to_string(script_.size()) + ")");
        }
    }

    std::vector<frame> script_;
    std::size_t        pos_;
};

// Helper: build a synthetic send/recv pair for a "handshake" with a given label.
// The bytes encode the label as ASCII; no real TLS framing.
inline std::vector<frame> make_handshake_script(std::string_view label) {
    auto to_bytes = [](std::string_view s) {
        std::vector<std::byte> v;
        v.reserve(s.size());
        for (char c : s) v.push_back(static_cast<std::byte>(c));
        return v;
    };
    std::string client_hello = "ClientHello:" + std::string(label);
    std::string server_hello = "ServerHello:" + std::string(label);
    return {
        {direction::send, to_bytes(client_hello)},
        {direction::recv, to_bytes(server_hello)},
    };
}

}  // namespace fixpp::tls::test
