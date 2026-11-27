// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/reserved_ports.hpp — test-only.
//
// reserve_loopback_ports: loopback TCP ports for an engine listener a test registers
// before start().
//
// Why not bind port 0. A port-0 bind draws from the kernel's ephemeral range, and the
// number stays free only while the probe socket is open. Once it closes, nothing holds
// it until the engine binds its listener, so a second reservation can be handed the same
// number, or an outgoing connection (an initiator's source port) can take it first, and
// the session pair registered on that port need not ever connect.
//
// What this does instead:
//   - draws each port at random from BELOW the ephemeral range, which the kernel never
//     hands to an outgoing connection or a port-0 bind. The range's low end is read from
//     /proc/sys/net/ipv4/ip_local_port_range; where that file is absent (Windows, macOS)
//     the low end is taken as kEphemeralFloor, at or below those platforms' defaults;
//   - binds each one WITHOUT reuse_address and keeps every probe open until all n are
//     bound, so a port already bound by anyone, or drawn twice, is refused and redrawn;
//   - closes them all on return. Call it immediately before registering the sessions,
//     and take every port that must coexist from ONE call: two calls release the first
//     call's ports before the second draws.
// It throws std::runtime_error when the range below the ephemeral range is empty, or when
// kMaxAttempts draws do not yield n distinct ports.
#pragma once

#include <algorithm>
#include <asio/io_context.hpp>
#include <asio/ip/address.hpp>
#include <asio/ip/tcp.hpp>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <random>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace fixpp::test_support {

namespace reserved_ports_detail {
// Lowest port drawn: below it sit well-known and commonly configured service ports.
inline constexpr unsigned kLowestPort = 10000;
// Low end of the ephemeral range assumed where the kernel's is not readable.
inline constexpr unsigned kEphemeralFloor = 32768;
inline constexpr std::size_t kMaxAttempts = 1000;

inline unsigned ephemeral_low_end() {
    std::ifstream range{"/proc/sys/net/ipv4/ip_local_port_range"};
    unsigned low = 0;
    unsigned high = 0;
    if (range >> low >> high) return low;
    return kEphemeralFloor;
}
}  // namespace reserved_ports_detail

[[nodiscard]] inline std::vector<std::uint16_t> reserve_loopback_ports(asio::io_context& ioc,
                                                                       std::size_t n) {
    namespace d = reserved_ports_detail;
    const unsigned low_end = d::ephemeral_low_end();
    if (low_end <= d::kLowestPort) {
        throw std::runtime_error{"reserve_loopback_ports: no port range below the ephemeral range"};
    }
    std::random_device seed;
    std::mt19937 rng{seed()};
    std::uniform_int_distribution<unsigned> draw{d::kLowestPort, low_end - 1};

    std::vector<asio::ip::tcp::acceptor> held;
    std::vector<std::uint16_t> ports;
    held.reserve(n);
    ports.reserve(n);
    for (std::size_t attempt = 0; ports.size() < n && attempt < d::kMaxAttempts; ++attempt) {
        const auto port = static_cast<std::uint16_t>(draw(rng));
        if (std::find(ports.begin(), ports.end(), port) != ports.end()) continue;
        asio::ip::tcp::acceptor probe{ioc};
        const asio::ip::tcp::endpoint ep{asio::ip::make_address("127.0.0.1"), port};
        std::error_code ec;
        // No reuse_address: with it, Linux lets two sockets that are not listening bind
        // the same port, and holding the probes would no longer refuse a taken port.
        probe.open(ep.protocol(), ec);
        if (!ec) probe.bind(ep, ec);
        if (ec) continue;
        ports.push_back(port);
        held.push_back(std::move(probe));
    }
    if (ports.size() < n) {
        throw std::runtime_error{"reserve_loopback_ports: could not bind enough distinct ports"};
    }
    return ports;  // `held` closes every probe here, after all n are bound
}

[[nodiscard]] inline std::uint16_t reserve_loopback_port(asio::io_context& ioc) {
    return reserve_loopback_ports(ioc, 1).front();
}

}  // namespace fixpp::test_support
