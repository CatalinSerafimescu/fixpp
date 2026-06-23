// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/counterparty_probe.hpp
//
// fixpp::interop — counterparty availability probe (016 T003, FR-023, data-model E6).
//
// Live interop cells require a reference engine (QuickFIX-cpp / QuickFIX-J) that
// the PARENT harness brings up FIRST on a leased port (quickstart §2). A cell run
// in an environment where that counterparty is NOT present MUST *skip with a
// reason*, never silently pass (FR-023). This header is the in-repo half of that
// contract: it probes for the counterparty and, when absent, drives a GoogleTest
// GTEST_SKIP() carrying the reason.
//
// Mechanism (matches the parent-harness contract): the parent exports
//   INTEROP_<TOKEN>_PORT   — the leased TCP port the counterparty listens on
//   INTEROP_<TOKEN>_HOST   — optional; defaults to 127.0.0.1
// where <TOKEN> is the upper-cased counterparty token (e.g. QUICKFIX_CPP).
// The probe (a) requires the port env var to be set, and (b) best-effort verifies
// the port is actually connectable (a non-blocking TCP connect with a short
// deadline). Either check failing ⇒ unavailable + reason.
//
// Header-only + self-contained: depends only on <gtest/gtest.h> + the platform
// sockets API (POSIX sockets / winsock). No fixpp production includes, no asio —
// the probe must be cheap and run before any Engine is constructed.
#pragma once

#include <gtest/gtest.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
// Self-contained linkage: every includer auto-links winsock without each test
// target naming ws2_32 in CMake (matches this header's no-CMake-dependency design).
#pragma comment(lib, "ws2_32.lib")
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <fcntl.h>
#endif

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace fixpp::interop {

struct ProbeResult {
    bool available = false;
    std::string reason;  // populated when !available — the GTEST_SKIP message
};

// Uppercase + map '-' → '_' so "quickfix-cpp" → env token "QUICKFIX_CPP".
inline std::string env_token(std::string_view counterparty) {
    std::string t;
    t.reserve(counterparty.size());
    for (char c : counterparty) {
        if (c == '-') {
            t.push_back('_');
        } else if (c >= 'a' && c <= 'z') {
            t.push_back(static_cast<char>(c - ('a' - 'A')));
        } else {
            t.push_back(c);
        }
    }
    return t;
}

#ifdef _WIN32
// Ensure WSAStartup has run once per process before any winsock call. The probe
// runs in single-threaded test setup; function-local-static init is thread-safe.
inline void ensure_winsock_started() {
    static const bool started = [] {
        WSADATA wsa_data{};
        return ::WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
    }();
    (void)started;
}
#endif

// Best-effort non-blocking TCP connect to host:port with a deadline. Returns true
// iff the connection establishes within the timeout (i.e. something is listening).
inline bool tcp_port_connectable(const std::string& host, std::uint16_t port,
                                 std::chrono::milliseconds timeout) {
#ifdef _WIN32
    ensure_winsock_started();
    SOCKET fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) {
        return false;
    }
    // Put the socket in non-blocking mode so connect() does not hang.
    u_long nonblocking = 1;
    ::ioctlsocket(fd, FIONBIO, &nonblocking);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        ::closesocket(fd);
        return false;
    }

    int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    bool ok = false;
    if (rc == 0) {
        ok = true;  // connected immediately (loopback fast path)
    } else if (::WSAGetLastError() == WSAEWOULDBLOCK) {
        // Wait for writability (connect completion) up to the deadline.
        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(fd, &wset);
        timeval tv{};
        tv.tv_sec = static_cast<long>(timeout.count() / 1000);
        tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
        // On winsock the first nfds argument is ignored.
        if (::select(0, nullptr, &wset, nullptr, &tv) > 0) {
            int so_error = 0;
            int len = sizeof(so_error);
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error),
                             &len) == 0 &&
                so_error == 0) {
                ok = true;
            }
        }
    }
    ::closesocket(fd);
    return ok;
#else
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    // Put the socket in non-blocking mode so connect() does not hang.
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        ::close(fd);
        return false;
    }

    int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    bool ok = false;
    if (rc == 0) {
        ok = true;  // connected immediately (loopback fast path)
    } else if (errno == EINPROGRESS) {
        // Wait for writability (connect completion) up to the deadline.
        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(fd, &wset);
        timeval tv{};
        tv.tv_sec = static_cast<long>(timeout.count() / 1000);
        tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
        if (::select(fd + 1, nullptr, &wset, nullptr, &tv) > 0) {
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) == 0 && so_error == 0) {
                ok = true;
            }
        }
    }
    ::close(fd);
    return ok;
#endif
}

// Probe a counterparty by its token (e.g. "quickfix-cpp"). Reads
// INTEROP_<TOKEN>_PORT / INTEROP_<TOKEN>_HOST and verifies connectability.
inline ProbeResult probe_counterparty(std::string_view counterparty,
                                      std::chrono::milliseconds timeout =
                                          std::chrono::milliseconds{500}) {
    const std::string token = env_token(counterparty);
    const std::string port_var = "INTEROP_" + token + "_PORT";
    // getenv is mt-unsafe only against a concurrent setenv; the probe runs in
    // single-threaded test setup before any Engine/thread exists. NOLINT below.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char* port_env = std::getenv(port_var.c_str());
    if (port_env == nullptr || *port_env == '\0') {
        return {false, std::string{counterparty} + " unavailable: " + port_var +
                           " not set (parent harness did not lease a port)"};
    }
    char* port_end = nullptr;
    long port = std::strtol(port_env, &port_end, 10);
    if (port_end == port_env || *port_end != '\0' || port <= 0 || port > 65535) {
        return {false, std::string{counterparty} + " unavailable: " + port_var +
                           "='" + port_env + "' is not a valid TCP port"};
    }

    const std::string host_var = "INTEROP_" + token + "_HOST";
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char* host_env = std::getenv(host_var.c_str());
    std::string host = (host_env != nullptr && *host_env != '\0') ? host_env : "127.0.0.1";

    if (!tcp_port_connectable(host, static_cast<std::uint16_t>(port), timeout)) {
        return {false, std::string{counterparty} + " unavailable: nothing listening at " +
                           host + ":" + std::to_string(port)};
    }
    return {true, {}};
}

}  // namespace fixpp::interop

// Convenience: skip the current test (with reason) when the counterparty is absent.
// Use at the top of a live-cell test body:
//   INTEROP_REQUIRE_COUNTERPARTY("quickfix-cpp");
#define INTEROP_REQUIRE_COUNTERPARTY(counterparty)                       \
    do {                                                                 \
        ::fixpp::interop::ProbeResult _ip = ::fixpp::interop::probe_counterparty(counterparty); \
        if (!_ip.available) {                                            \
            GTEST_SKIP() << _ip.reason;                                  \
        }                                                                \
    } while (0)
