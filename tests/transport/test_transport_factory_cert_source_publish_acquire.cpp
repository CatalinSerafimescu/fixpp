// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/transport/test_transport_factory_cert_source_publish_acquire.cpp
// T008 (046-atomic-shared-ptr, NFR-017) — consumer publish/acquire witness for
// asio_tls_transport_factory::cert_source_slot_
// (fixpp::sync::atomic_shared_ptr<fixpp::tls::cert_source>).
//
// Design anchor: .specify/046-atomic-shared-ptr.md (plan row 6-consumers)
// Consumer: asio_tls_transport_factory (include/fixpp/transport/transport_factory.hpp §211)
//
// Pattern: writer thread calls reload_credentials(new_source) (release-store);
// reader thread calls cert_source_snapshot() (acquire-load).
// TSan is the ordering oracle; reader asserts each snapshot is one of the exact
// shared_ptr instances the writer published, discriminating a torn pointer from
// a valid publication.
//
// Scope note: we do NOT call make_asio_tls_transport_factory() here because that
// requires building an SSL_CTX (OpenSSL files). Instead we use the internal
// shared_ctx_tag constructor which accepts a pre-built (null) ssl_ctx and a
// null cert_source. This is sufficient to witness the cert_source_slot_ atomic
// because the slot is an independent member initialized separately from ssl_ctx_.
// The test does NOT exercise handshake correctness — only the publish/acquire
// ordering of the cert_source_slot_ field.

#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <span>
#include <thread>
#include <vector>

#include <fixpp/core/error.hpp>
#include <fixpp/tls/cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>

namespace {

using fixpp::tls::Certificate;
using fixpp::tls::SslCtxConfig;
using fixpp::tls::cert_source;
using fixpp::transport::Transport;
using fixpp::transport::asio_tls_transport_factory;

// ── Minimal no-op cert_source stub ───────────────────────────────────────────
// Does NOT need OpenSSL — just provides the abstract interface so we can
// publish distinct shared_ptr<cert_source> instances for identity comparison.
class stub_cert_source final : public cert_source {
public:
    explicit stub_cert_source(int id) : id_{id} {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::tls::local_credentials>>
    load_credentials() override {
        // Unreachable in this test — stub only used for shared_ptr identity.
        co_return fixpp::core::expected_t<fixpp::tls::local_credentials>{
            std::unexpect, fixpp::core::error::tls_load_cancelled};
    }

    [[nodiscard]] fixpp::core::expected_t<std::span<const Certificate>> load_trust_anchors()
        [[clang::lifetimebound]] override {
        return std::span<const Certificate>{};
    }

    int id() const noexcept { return id_; }

private:
    int id_;
};

constexpr int kRounds = 3000;
constexpr int kReaderCount = 3;

// ── CertSourcePublishAcquire ──────────────────────────────────────────────────

TEST(TransportFactoryCertSourcePublishAcquire, ReaderNeverSeesTornPointer) {
    // Build the factory using the internal shared_ctx_tag constructor with a
    // null ssl_ctx (no OpenSSL needed). The cert_source_slot_ starts as nullptr
    // and is exercised entirely via reload_credentials / cert_source_snapshot.
    Transport::Config transport_cfg{};
    SslCtxConfig ssl_cfg{};
    // ssl_ctx = nullptr is acceptable here because we never call make() —
    // we only exercise the cert_source_slot_ atomic.
    auto factory = std::make_unique<asio_tls_transport_factory>(
        asio_tls_transport_factory::shared_ctx_tag{}, transport_cfg, ssl_cfg,
        /*ctx=*/nullptr);

    // Pre-populate a set of N distinct sources. The reader will verify it reads
    // one of these exact instances (by id), never a partially-written pointer.
    constexpr int kSourceCount = 8;
    std::vector<std::shared_ptr<stub_cert_source>> sources;
    sources.reserve(kSourceCount);
    for (int i = 0; i < kSourceCount; ++i) {
        sources.push_back(std::make_shared<stub_cert_source>(i));
    }

    std::atomic<bool> writer_done{false};
    std::atomic<int> any_reader_started{0};
    std::atomic<int> valid_reads{0};  // reads that returned a known source id

    // Readers: concurrently call cert_source_snapshot() and assert the result is
    // one of the exact instances we published (or nullptr, which is also valid
    // before the first store).
    std::vector<std::thread> readers;
    readers.reserve(kReaderCount);
    for (int t = 0; t < kReaderCount; ++t) {
        readers.emplace_back([&] {
            while (!writer_done.load(std::memory_order_acquire)) {
                auto snap = factory->cert_source_snapshot();
                if (snap != nullptr) {
                    // The pointer must point to one of our exact published instances.
                    // A torn write would yield a pointer to a destroyed object or
                    // garbage — casting it to stub_cert_source* would be UB/crash.
                    // Under ASan: UAF on the destroyed instance.
                    // Under TSan: race on the refcount fields.
                    // The id() call exercises the object through the acquired reference.
                    auto* typed = dynamic_cast<stub_cert_source*>(snap.get());
                    ASSERT_NE(typed, nullptr)
                        << "cert_source_snapshot() returned an unexpected type — "
                           "possible torn acquire";
                    int id = typed->id();
                    ASSERT_GE(id, 0);
                    ASSERT_LT(id, kSourceCount)
                        << "cert_source_snapshot() returned an out-of-range id=" << id
                        << " — possible torn pointer (corrupted object identity)";
                    ++valid_reads;
                }
                any_reader_started.store(1, std::memory_order_release);
            }
        });
    }

    // Wait until at least one reader has started.
    while (any_reader_started.load(std::memory_order_acquire) == 0) { /* spin */ }

    // Writer: cycle through the source instances (release-store each one).
    for (int i = 0; i < kRounds; ++i) {
        auto src = sources[static_cast<std::size_t>(i % kSourceCount)];
        auto r = factory->reload_credentials(src);
        ASSERT_TRUE(r.has_value()) << "reload_credentials() must succeed for non-null source";
    }

    // Issue #283: hold the stimulus until it is WITNESSED. any_reader_started
    // is a START barrier — it proves a reader entered the loop, not that one
    // observed the window. Measured on a starved single core, the kRounds above
    // complete in ~345 us with the readers never rescheduled: 40/40 runs closed
    // the window with zero overlapping reads. Keep publishing until a reader has
    // observed a non-null snapshot while the writer is STILL RUNNING, bounded by
    // a deadline that fails loudly rather than hanging. yield() is required: on
    // a saturated core the writer would otherwise starve the very readers it is
    // waiting for. (A reader killed by its own ASSERT_* returns from the lambda;
    // if every reader dies that way this loop exits on the deadline, not a hang.)
    constexpr auto kWitnessDeadline = std::chrono::seconds{10};
    const auto witness_until = std::chrono::steady_clock::now() + kWitnessDeadline;
    for (int i = kRounds; valid_reads.load(std::memory_order_acquire) == 0 &&
                          std::chrono::steady_clock::now() < witness_until;
         ++i) {
        auto src = sources[static_cast<std::size_t>(i % kSourceCount)];
        auto r = factory->reload_credentials(src);
        ASSERT_TRUE(r.has_value()) << "reload_credentials() must succeed for non-null source";
        std::this_thread::yield();
    }

    // Sample BEFORE closing the window. A read counted after writer_done is set
    // did not overlap the writer at all, so it proves nothing about the
    // publish/acquire edge — asserting on the post-join total is what let the
    // old witness go green on 37 of those same 40 zero-overlap runs.
    const int in_window_reads = valid_reads.load(std::memory_order_acquire);

    writer_done.store(true, std::memory_order_release);
    for (auto& t : readers) {
        t.join();
    }

    EXPECT_GT(in_window_reads, 0)
        << "readers did not observe any non-null snapshots during the write window — "
           "publish/acquire edge not exercised";
}

}  // namespace
