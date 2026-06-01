// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// fixpp::transport::Endpoint — owning-by-value endpoint value type.
// Re-emitted verbatim from [2h §4.3].
//
// Address-family-agnostic; resolved at async_connect time via asio::ip::resolver
// (IPv4 vs IPv6 determined from the resolver's result). Admits IPv6 zone-id
// strings (host%zone for link-local addresses).
//
// v1.0 surface does NOT publish a PMR-aware overload — the host string uses
// the system default allocator. HFT users who want arena-allocated host
// storage layer that on top via a pmr_endpoint type — out of v1.0 scope per
// [2h §1.2] / [2h §10]. The v0.1 inline-comment claim about a "PMR-aware
// overload available" is retired in v0.2 per Codex P3 #7.

#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace fixpp::transport {

struct Endpoint {
    // Hostname OR IP literal OR "host%zone" for IPv6 link-local addresses.
    // IPv6 zone-id full conformance corpus DEFERRED to Phase-4 per [2h §10 Q6].
    std::string host;

    // Initiator: peer port. Acceptor: bind port (0 = OS-picked).
    std::uint16_t port{0};

    // For acceptor-side construction only: listen queue depth. OS may cap
    // silently (Linux: /proc/sys/net/core/somaxconn; Windows: SOMAXCONN).
    // Initiator-side construction ignores this field — distinction is at the
    // type-system level (which method consumes the Endpoint), NOT a runtime
    // predicate. The v0.1 is_initiator_shape() heuristic was dropped per
    // [2h §4.3].
    std::uint32_t backlog{128};

    Endpoint() = default;
    Endpoint(std::string h, std::uint16_t p) : host(std::move(h)), port(p) {}
    Endpoint(std::string h, std::uint16_t p, std::uint32_t b)
        : host(std::move(h)), port(p), backlog(b) {}
};

}  // namespace fixpp::transport
