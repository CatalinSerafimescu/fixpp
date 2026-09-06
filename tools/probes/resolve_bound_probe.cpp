// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tools/probes/resolve_bound_probe.cpp — MANUAL probe, not wired into CI.
//
// Re-derives the #361 numbers: with the bounded resolve in place, does a wedged
// getaddrinfo still hold async_connect, and what does abandoning it leave behind?
//
// WHY THIS IS A PROBE AND NOT A CELL. There is no process-local way to wedge
// glibc's resolver — no environment override for /etc/resolv.conf, no
// per-process nameserver — so an in-tree cell could only fake a never-completing
// op, which would assert a property of asio::steady_timer rather than of the
// resolve path. Wedging it needs a private mount namespace, which a gtest cell
// cannot enter. So the evidence for L-361-2 and for B-361-1's timeout/cancel
// figures is THIS, run by hand, with its control arm.
//
// ⚠️ THE CONTROL ARM IS NOT OPTIONAL. A resolver that answers makes every other
// number here meaningless, and that is the failure mode this probe fails toward.
// Always run the working-resolver arm in the same session.
//
// BUILD (adjust the preset dir; the sanitizer flags must match the library's, or
// the link fails on __asan_* symbols):
//
//   clang++ -DASIO_STANDALONE -DFIXPP_BUILD_OTEL=1 -DFIXPP_LOG_MIN_LEVEL=0 \
//     -I include -I src -isystem <asio>/include -isystem <openssl>/include \
//     -isystem <zlib>/include -std=c++23 -stdlib=libstdc++ -pthread \
//     -fsanitize=address -fno-omit-frame-pointer -g \
//     -o /tmp/resolve_bound_probe tools/probes/resolve_bound_probe.cpp \
//     build/linux-clang-asan/lib/*.a -lssl -lcrypto
//
// RUN — control first, then the wedge in a private mount namespace so the HOST's
// resolver is untouched:
//
//   /tmp/resolve_bound_probe deadline www.google.com 2000
//   /tmp/resolve_bound_probe cancel   www.google.com 2000
//   printf 'nameserver 203.0.113.1\n' > /tmp/rc     # TEST-NET-3, blackholed
//   unshare -Urm --map-root-user sh -c 'mount --bind /tmp/rc /etc/resolv.conf \
//     && /tmp/resolve_bound_probe deadline bh.example.com 2000'
//
// To reproduce the BEFORE row, compile `git show <pre-fix>:src/transport/
// asio_plain_transport.cpp` alongside this file — an explicitly named object
// wins over the archive member, so the library need not be rebuilt.
//
// WHAT EACH COLUMN MEANS. "connect retired" is the property #361 is about: the
// frame retiring is what lets Engine::stop()'s outstanding_counter_ join finish.
// "ioc.run() returned" is the residual L-361-2 records: an abandoned resolve
// still holds asio work, so the drain is NOT bounded by anything fixpp controls.
#include <asio/bind_cancellation_slot.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/strand.hpp>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "transport/asio_plain_transport.hpp"

using namespace fixpp::transport;
using clk = std::chrono::steady_clock;

static long ms(clk::time_point a, clk::time_point b) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
}

int main(int argc, char** argv) {
    const std::string arm = argc > 1 ? argv[1] : "deadline";
    const std::string host = argc > 2 ? argv[2] : "blackhole.example.com";
    const int budget = argc > 3 ? std::atoi(argv[3]) : 2000;

    asio::io_context ioc;
    Transport::Config cfg{};
    cfg.connect_timeout = std::chrono::milliseconds{budget};
    auto strand = asio::make_strand(ioc);
    asio_plain_transport t{asio::any_io_executor{strand}, cfg};

    const auto t0 = clk::now();
    long retired = -1;
    int code = -999;
    asio::cancellation_signal sig;

    asio::co_spawn(
        strand,
        [&]() -> asio::awaitable<void> {
            auto r = co_await t.async_connect(Endpoint{host, static_cast<std::uint16_t>(443)});
            retired = ms(t0, clk::now());
            code = r.has_value() ? 0 : static_cast<int>(r.error());
            co_return;
        },
        asio::bind_cancellation_slot(sig.slot(), asio::detached));

    // JOINED, not detached. A detached emitter captures `sig`, `strand` and `ioc`
    // by reference and outlives them on the CONTROL arm, where ioc.run() returns
    // in tens of ms: it then posts into a destroyed io_context. That survives
    // only by winning a race with process exit, in a file whose own BUILD line
    // prescribes -fsanitize=address. `armed` lets the join finish immediately
    // once the run loop is done rather than waiting out the 300 ms.
    std::atomic<bool> armed{arm == "cancel"};
    std::thread emitter([&] {
        for (int i = 0; i < 300 && armed.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (armed.exchange(false)) {
            asio::post(strand, [&sig] { sig.emit(asio::cancellation_type::total); });
        }
    });

    // No work guard on purpose: run() returns only when nothing is outstanding,
    // which is exactly what an abandoned resolve prevents.
    ioc.run();
    armed.store(false);
    emitter.join();
    std::printf("arm=%-8s connect retired: %6ld ms (err=%d)   ioc.run() returned: %6ld ms\n",
                arm.c_str(), retired, code, ms(t0, clk::now()));
    return 0;
}
