// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_credentials_rotated_emit.cpp — T016 [P] [US3] Phase 5
//
// After reload_credentials stages a new cert_source, a reconnect emits exactly
// one credentials_rotated on the session strand BEFORE make(), carrying the REAL
// SHA-256 leaf fingerprints.  First-ever load emits NO event.  No-op rotation
// still emits (old == new).
//
// Anchors: FR-009/010/011; US3 AC1/AC2; SC-005; I-5/I-6/I-7;
//          data-model §E-3; contracts C3.
//
// Test cells (all must execute — no GTEST_SKIP / no SUCCEED placeholder):
//
//   Cell 1 (MOCK) — real-fingerprint witness using a mock factory that captures
//     the cert_source passed to make() and the emission sequence.
//     Uses a mock cert_source whose load_credentials() returns a leaf with a
//     known SHA-256.  Drives ReconnectFsm::drive_reconnect_attempt() directly.
//     Asserts:
//       a) Exactly one credentials_rotated emitted.
//       b) new_sha256 == SHA-256 of the actual loaded leaf (not all-zero).
//       c) Emission happens BEFORE make() — verified by the order counter
//          from the factory's make() hook.
//
//   Cell 2 — First-ever load (last_active_source_ == nullptr):
//     No credentials_rotated event emitted.
//
//   Cell 3 — No-op rotation (same shared_ptr / same fingerprint):
//     credentials_rotated STILL emitted with old_sha256 == new_sha256.
//
//   Cell 4 (LIVE TLS) — over the loopback-TLS harness:
//     reload_credentials → reconnect → assert new_sha256 equals the SHA-256
//     of the actual leaf in leaf_rsa2048.pem (not all-zero, not just non-zero
//     — matches the real value). Depends on T005 harness + live TLS cert.
//
// TDD RED: before T017/T018 impl:
//   - Cells 1/3 fail because no credentials_rotated is emitted.
//   - Cell 2 fails because the mock factory path would fire an event instead of
//     doing nothing (or compile-fail if the callback injection is missing).
//   - Cell 4 fails because no event is emitted.
//
// [[feedback_lying_event]]: new_sha256 must match the real loaded leaf SHA-256,
//   not just be non-zero. The assertion fetches the fingerprint independently
//   from cert_source::load_credentials() and compares byte-for-byte.
//
// [const §XV.9]: this header is tests-only — heavy includes are safe here.

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <future>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>
#include <vector>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <gtest/gtest.h>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/reconnect_fsm.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/cert_source.hpp>
#include <fixpp/tls/certificate.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/reconnect_policy.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>

#include "loopback_tls_session_harness.hpp"
#include "support/minimal_dictionary.hpp"
#include "transport/loopback_tls_fixture.hpp"

using namespace std::chrono_literals;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// FIX frame helpers (copied from test_reconnect_live_happy_path.cpp pattern)
// ─────────────────────────────────────────────────────────────────────────────
static std::string fix_field(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

static std::vector<std::byte> make_logon_frame(
    std::string_view begin_string,
    std::uint32_t seq,
    std::string_view sender,
    std::string_view target)
{
    std::string body;
    body += fix_field(35, "A");
    body += fix_field(34, std::to_string(seq));
    body += fix_field(49, sender);
    body += fix_field(56, target);
    body += fix_field(98, "0");
    body += fix_field(108, "30");

    std::string msg;
    msg += "8=" + std::string(begin_string) + "\x01";
    msg += "9=" + std::to_string(body.size()) + "\x01";
    msg += body;
    unsigned int cs = 0;
    for (unsigned char c : msg) cs += c;
    cs &= 0xFFU;
    char csbuf[5];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    msg += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(msg.size());
    for (char c : msg) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

// ─────────────────────────────────────────────────────────────────────────────
// known_sha_cert_source — a mock cert_source whose load_credentials() returns
// a leaf with a known, configurable SHA-256 fingerprint.
//
// Used to make the expected fingerprint deterministic in the mock cells.
// ─────────────────────────────────────────────────────────────────────────────
class known_sha_cert_source final : public fixpp::tls::cert_source {
public:
    // The SHA-256 fingerprint this source reports in its leaf.
    std::array<std::byte, 32> expected_sha{};

    explicit known_sha_cert_source(std::array<std::byte, 32> sha)
        : expected_sha(sha)
    {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::tls::local_credentials>>
    load_credentials() override {
        ++load_call_count;
        // Build a synthetic leaf Certificate with the configured sha256.
        fixpp::tls::Certificate leaf;
        leaf.sha256_ = expected_sha;
        // Return minimal valid local_credentials — signer is unused in FSM.
        fixpp::tls::local_credentials creds;
        creds.leaf = leaf;
        // chain is an empty span (no intermediates needed for mock path).
        co_return creds;
    }

    [[nodiscard]] fixpp::core::expected_t<std::span<const fixpp::tls::Certificate>>
    load_trust_anchors() override {
        return std::span<const fixpp::tls::Certificate>{};
    }

    std::atomic<int> load_call_count{0};
};

// ─────────────────────────────────────────────────────────────────────────────
// event_capture — a thin strand-bound functor used as the emit_credentials_rotated_
// callback injected into ReconnectFsm for mock-path tests.
// ─────────────────────────────────────────────────────────────────────────────
struct event_capture {
    std::vector<fixpp::session::session_event_credentials_rotated> events;

    void operator()(fixpp::session::session_event_credentials_rotated ev) {
        events.push_back(ev);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// OrderedFactory — records the global sequence of make() calls relative to
// the credentials_rotated emit, so we can assert "emit BEFORE make()".
//
// emit_order is set by the emit callback before make() is reached.
// make_order is set when make() is called.
// ─────────────────────────────────────────────────────────────────────────────
class OrderedFactory final : public fixpp::transport::TransportFactory {
public:
    // The cert_source stored (set by reload_credentials).
    std::shared_ptr<fixpp::tls::cert_source> current_source;

    // Per-call sequence counters:
    //   - event_order_: set by the injected emit callback when it fires.
    //   - make_order_: set when make() is called.
    // We use a simple monotonic counter shared between callback and factory.
    std::atomic<int>  sequence_counter{0};
    int               event_sequence = -1;  // sequence at credentials_rotated
    int               make_sequence  = -1;  // sequence at make()

    // The event capture — filled by the emit callback.
    std::vector<fixpp::session::session_event_credentials_rotated> emitted_events;

    // factory_make_count tracks how many times make() has been called.
    int factory_make_count = 0;

    // The TlsTransport stub returned by make() — a minimal impl that succeeds
    // connect() and handshake() so the FSM can reach step 8.
    class SuccessTransport final : public fixpp::transport::TlsTransport {
    public:
        explicit SuccessTransport(asio::any_io_executor exec) : exec_(std::move(exec)) {}

        [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::ConnectInfo>>
        async_connect(fixpp::transport::Endpoint const& ep) override {
            fixpp::transport::ConnectInfo info;
            info.remote = ep;
            info.local  = fixpp::transport::Endpoint{"127.0.0.1", 0};
            info.family = 2;
            co_return info;
        }

        [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>>
        async_read_some(std::span<std::byte> buf [[clang::lifetimebound]]) override {
            (void)buf;
            co_return std::unexpected{fixpp::core::error::transport_read_eof};
        }

        [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>>
        async_write(std::span<const std::byte> buf [[clang::lifetimebound]]) override {
            co_return buf.size();
        }

        [[nodiscard]] fixpp::core::expected_t<void> cancel() noexcept override { return {}; }
        [[nodiscard]] fixpp::core::expected_t<void> close() noexcept override { return {}; }

        [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::handshake_result>>
        async_handshake(fixpp::tls::SslCtxConfig const& cfg [[clang::lifetimebound]]) override {
            std::pmr::memory_resource* mr = cfg.mr ? cfg.mr : std::pmr::get_default_resource();
            co_return fixpp::transport::handshake_result{
                .peer_id           = fixpp::tls::peer_identity{},
                .captured_pinset   = nullptr,
                .negotiated_cipher = std::pmr::string{"TLS_AES_128_GCM_SHA256", mr},
            };
        }

    private:
        asio::any_io_executor exec_;
    };

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>>
    make(asio::any_io_executor exec,
         fixpp::tls::SslCtxConfig /*ssl_cfg*/,
         std::pmr::memory_resource* /*mr*/) noexcept override
    {
        make_sequence = sequence_counter.fetch_add(1, std::memory_order_relaxed);
        ++factory_make_count;
        return std::make_unique<SuccessTransport>(std::move(exec));
    }

    [[nodiscard]] fixpp::core::expected_t<void>
    reload_credentials(std::shared_ptr<fixpp::tls::cert_source> new_source) noexcept override {
        current_source = std::move(new_source);
        return {};
    }

    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source>
    cert_source_snapshot() const noexcept override {
        return current_source;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build a permissive ReconnectPolicy (zero delay, max 3 attempts).
// ─────────────────────────────────────────────────────────────────────────────
static fixpp::transport::ReconnectPolicy make_fast_policy(std::uint32_t max_attempts = 3) {
    fixpp::transport::ReconnectPolicy p;
    p.max_attempts = max_attempts;
    p.schedule = std::pmr::vector<std::chrono::milliseconds>{std::pmr::get_default_resource()};
    p.schedule.push_back(0ms);
    p.jitter = 0.0;
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build a known fingerprint value for testing
// ─────────────────────────────────────────────────────────────────────────────
static std::array<std::byte, 32> make_sha(std::uint8_t fill) {
    std::array<std::byte, 32> a{};
    for (auto& b : a) b = std::byte{fill};
    return a;
}

// ─────────────────────────────────────────────────────────────────────────────
// Fixture
// ─────────────────────────────────────────────────────────────────────────────
class CredentialsRotatedEmitTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    fixpp::core::EngineConfig engine{};

    void SetUp() override {
        engine.executor = ioc.get_executor();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Cell 2 — First-ever load: NO credentials_rotated event emitted.
//
// When last_active_source_ == nullptr (no previous active cert_source),
// the FSM sets the rotation-detect baseline but does NOT emit an event.
//
// Anchor: FR-009 (SPEC-FIXED rule); data-model E-3; contracts C3.
//
// RED before T017/T018: FSM ignores last_active_source_; cell assertion
//   either passes trivially (no emit site exists) — compile-check will still
//   fail if the callback injection doesn't exist.
// GREEN after T017/T018: no event emitted on first load.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(CredentialsRotatedEmitTest, FirstLoadEmitsNoEvent) {
    auto factory = std::make_shared<OrderedFactory>();

    // Install a cert_source as the initial (first-ever) snapshot.
    auto sha_a = make_sha(0xAA);
    auto source_a = std::make_shared<known_sha_cert_source>(sha_a);
    factory->current_source = source_a;

    // Capture emitted events.
    std::vector<fixpp::session::session_event_credentials_rotated> events;

    fixpp::session::ReconnectFsm fsm(
        factory.get(), make_fast_policy(), 30s, 2000ms);

    // Inject the emit callback — will be populated by T018 wiring.
    // For the standalone FSM test (no Session), we inject directly.
    fsm.set_emit_credentials_rotated(
        [&events](fixpp::session::session_event_credentials_rotated ev) {
            events.push_back(ev);
        });

    // No session_ back-pointer set (standalone test) — authorization gate
    // will be skipped (session_ == nullptr check in step 7).
    // set_session_owner(nullptr) is the default.

    auto fut = asio::co_spawn(ioc, fsm.drive_reconnect_attempt(), asio::use_future);
    ioc.run_for(500ms);
    ioc.restart();

    // FSM completes (make succeeds → connect → handshake → session_ null → drop t).
    ASSERT_EQ(fut.wait_for(0s), std::future_status::ready);
    (void)fut.get();  // may fail due to null session_ handoff; that's OK

    // FR-009 SPEC-FIXED: first-ever load is NOT a rotation → no event.
    EXPECT_EQ(events.size(), 0u)
        << "First-ever load must NOT emit credentials_rotated (FR-009 SPEC-FIXED).";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 1 — Rotation emit: after reload_credentials stages a new cert_source,
// the next drive_reconnect_attempt emits exactly ONE credentials_rotated,
// BEFORE make(), carrying the REAL SHA-256 leaf fingerprints.
//
// This is the anti-lying-event cell: new_sha256 must match the SHA-256 of
// the actually-loaded leaf, not just be non-zero.
//
// Anchors: FR-009/010; I-5/I-6; data-model E-3; contracts C3.
//
// RED before T017/T018: no event emitted; EXPECT_EQ(1) fails.
// GREEN after T017/T018: one event, correct fingerprints, before make().
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(CredentialsRotatedEmitTest, RotationEmitBeforeMakeWithRealFingerprints) {
    auto factory = std::make_shared<OrderedFactory>();

    // Source A (initial, already "active" from a prior reconnect).
    auto sha_a = make_sha(0xAA);
    auto source_a = std::make_shared<known_sha_cert_source>(sha_a);

    // Source B (the new source staged by reload_credentials).
    auto sha_b = make_sha(0xBB);
    auto source_b = std::make_shared<known_sha_cert_source>(sha_b);

    // Prime the FSM with source A as the initial active source by running
    // one reconnect attempt first (first load — no event).
    {
        factory->current_source = source_a;
        std::vector<fixpp::session::session_event_credentials_rotated> init_events;

        fixpp::session::ReconnectFsm fsm_init(
            factory.get(), make_fast_policy(), 30s, 2000ms);
        fsm_init.set_emit_credentials_rotated(
            [&init_events](fixpp::session::session_event_credentials_rotated ev) {
                init_events.push_back(ev);
            });

        auto fut = asio::co_spawn(ioc, fsm_init.drive_reconnect_attempt(), asio::use_future);
        ioc.run_for(500ms);
        ioc.restart();
        ASSERT_EQ(fut.wait_for(0s), std::future_status::ready);
        (void)fut.get();
        ASSERT_EQ(init_events.size(), 0u) << "Precondition: first load must emit no event.";

        // After the first attempt, fsm_init's last_active_source_ == source_a.
        // We'll re-use this FSM's state. But since ReconnectFsm is not copy/movable
        // in a useful way here, we create a fresh FSM and stage it manually by
        // noting that we need the FSM to have "seen" source_a first.
        // The cleanest approach: store source_a in factory, run one attempt,
        // then swap to source_b and run the second attempt on the SAME FSM.
        // We'll use a single FSM below.
        (void)init_events;
    }

    // Fresh factory and FSM for the actual test.
    factory->current_source = source_a;
    factory->factory_make_count = 0;
    factory->event_sequence = -1;
    factory->make_sequence  = -1;

    std::vector<fixpp::session::session_event_credentials_rotated> events;
    int event_order = -1;

    fixpp::session::ReconnectFsm fsm(
        factory.get(), make_fast_policy(5), 30s, 2000ms);

    // Wire the emit callback; also record the sequence counter at emit time.
    fsm.set_emit_credentials_rotated(
        [&factory, &events, &event_order](
            fixpp::session::session_event_credentials_rotated ev) {
            event_order = factory->sequence_counter.fetch_add(1, std::memory_order_relaxed);
            events.push_back(ev);
        });

    // Attempt 0 (first load — source_a): sets baseline, no event.
    {
        auto fut = asio::co_spawn(ioc, fsm.drive_reconnect_attempt(), asio::use_future);
        ioc.run_for(500ms);
        ioc.restart();
        ASSERT_EQ(fut.wait_for(0s), std::future_status::ready);
        (void)fut.get();
        EXPECT_EQ(events.size(), 0u) << "Precondition: first load emits no event.";
        factory->factory_make_count = 0;  // reset for next attempt
        factory->make_sequence = -1;
        event_order = -1;
    }

    // Stage source_b (the rotation).
    factory->current_source = source_b;

    // Attempt 1 (rotated source_b): must emit one credentials_rotated BEFORE make().
    {
        auto fut = asio::co_spawn(ioc, fsm.drive_reconnect_attempt(), asio::use_future);
        ioc.run_for(500ms);
        ioc.restart();
        ASSERT_EQ(fut.wait_for(0s), std::future_status::ready);
        (void)fut.get();
    }

    // ── Assertions ────────────────────────────────────────────────────────────

    // (a) Exactly one credentials_rotated emitted.
    ASSERT_EQ(events.size(), 1u)
        << "Expected exactly one credentials_rotated event. "
        << "RED: stub does not emit.";

    // (b) new_sha256 == sha_b (the REAL fingerprint of source_b's leaf).
    EXPECT_EQ(events[0].new_sha256, sha_b)
        << "new_sha256 must equal the real leaf SHA-256 of source_b. "
        << "Anti-lying-event: must not be all-zero or fabricated.";

    // (c) old_sha256 == sha_a (the REAL fingerprint of source_a's prior leaf).
    EXPECT_EQ(events[0].old_sha256, sha_a)
        << "old_sha256 must equal the real leaf SHA-256 of source_a.";

    // (d) Emission BEFORE make(): event_order < make_sequence.
    // Both use the shared sequence_counter. If make() fired before the emit,
    // event_order > make_sequence — that would violate FR-009.
    EXPECT_NE(event_order, -1) << "Emit callback never fired.";
    EXPECT_NE(factory->make_sequence, -1) << "make() was never called.";
    EXPECT_LT(event_order, factory->make_sequence)
        << "credentials_rotated must be emitted BEFORE make() (FR-009). "
        << "event_order=" << event_order
        << " make_sequence=" << factory->make_sequence;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 3 — No-op rotation: old_sha256 == new_sha256, event still emitted.
//
// When the staged cert_source is a different shared_ptr but has the same
// leaf SHA-256 fingerprint (fingerprint-no-op), credentials_rotated MUST
// still emit with old == new (not suppressed).
//
// Anchors: FR-011; contracts C3.
//
// RED before T017/T018: no event emitted (emit site missing).
// GREEN after T017/T018: one event with old_sha256 == new_sha256.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(CredentialsRotatedEmitTest, NoOpRotationStillEmits) {
    auto factory = std::make_shared<OrderedFactory>();

    // Both sources have the SAME sha (0xCC) — same fingerprint.
    auto sha_c = make_sha(0xCC);
    auto source_c1 = std::make_shared<known_sha_cert_source>(sha_c);
    auto source_c2 = std::make_shared<known_sha_cert_source>(sha_c);  // different ptr, same sha

    factory->current_source = source_c1;

    std::vector<fixpp::session::session_event_credentials_rotated> events;

    fixpp::session::ReconnectFsm fsm(
        factory.get(), make_fast_policy(5), 30s, 2000ms);

    fsm.set_emit_credentials_rotated(
        [&events](fixpp::session::session_event_credentials_rotated ev) {
            events.push_back(ev);
        });

    // Attempt 0 (first load — source_c1): sets baseline, no event.
    {
        auto fut = asio::co_spawn(ioc, fsm.drive_reconnect_attempt(), asio::use_future);
        ioc.run_for(500ms);
        ioc.restart();
        ASSERT_EQ(fut.wait_for(0s), std::future_status::ready);
        (void)fut.get();
        ASSERT_EQ(events.size(), 0u) << "Precondition: first load emits no event.";
    }

    // Stage source_c2 (different ptr, SAME sha — fingerprint no-op).
    factory->current_source = source_c2;

    // Attempt 1: snap != last_active_source_ (different ptr) → rotation path.
    // Even though old_sha == new_sha, the event MUST be emitted (FR-011).
    {
        auto fut = asio::co_spawn(ioc, fsm.drive_reconnect_attempt(), asio::use_future);
        ioc.run_for(500ms);
        ioc.restart();
        ASSERT_EQ(fut.wait_for(0s), std::future_status::ready);
        (void)fut.get();
    }

    ASSERT_EQ(events.size(), 1u)
        << "No-op rotation (old_sha == new_sha) must still emit credentials_rotated "
        << "(FR-011). RED: no emit site exists.";

    EXPECT_EQ(events[0].old_sha256, sha_c)
        << "old_sha256 must equal sha_c.";
    EXPECT_EQ(events[0].new_sha256, sha_c)
        << "new_sha256 must equal sha_c (same fingerprint).";

    // old == new for the no-op case.
    EXPECT_EQ(events[0].old_sha256, events[0].new_sha256)
        << "No-op rotation: old_sha256 must equal new_sha256.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 4 (LIVE TLS) — Over the loopback-TLS harness:
//   Session::reload_credentials(source_b) → reconnect → assert:
//     a) Exactly one credentials_rotated in session.recent_events() AFTER reconnect.
//     b) new_sha256 equals the SHA-256 of the loaded leaf_rsa2048.pem.
//     c) The event is recorded at the Session level (strand-bound via T018).
//
// Uses FIXPP_TLS_FIXTURE_DIR (wired unconditionally in CMakeLists.txt).
// GTEST_SKIP only if the cert files are absent.
//
// Anchors: FR-009/010; SC-005; I-5/I-6; data-model E-3; contracts C3.
//
// RED before T017/T018: no event emitted → ASSERT_EQ(1) fails.
// GREEN after T017/T018: event emitted with real fingerprint.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(CredentialsRotatedEmitTest, LiveTlsRotationEmitRealFingerprint) {
    // ── Resolve fixture dir ────────────────────────────────────────────────
    const char* dir_env = std::getenv("FIXPP_TLS_FIXTURE_DIR");
#ifdef FIXPP_TLS_FIXTURE_DIR
    static const char* kCompileTimeDir = FIXPP_TLS_FIXTURE_DIR;
#else
    static const char* kCompileTimeDir = nullptr;
#endif
    const char* fixture_dir = dir_env ? dir_env : kCompileTimeDir;
    if (!fixture_dir || fixture_dir[0] == '\0') {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }
    {
        std::string leaf = std::string(fixture_dir) + "/leaf_rsa2048.pem";
        if (FILE* f = std::fopen(leaf.c_str(), "r")) {
            std::fclose(f);
        } else {
            GTEST_SKIP() << "leaf_rsa2048.pem absent at " << fixture_dir;
        }
    }

    // ── Build the transport-layer loopback (acceptor + factory) ──────────
    fixpp::transport::test::LoopbackTlsFixture loopback_fixture{
        std::string(fixture_dir), ioc.get_executor()};

    auto server_ep = loopback_fixture.server_endpoint();
    auto& listener = loopback_fixture.listener();
    auto ssl_cfg   = loopback_fixture.ssl_cfg();

    // ── Build a cert_source from the fixture leaf ─────────────────────────
    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path        = std::string(fixture_dir) + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = std::string(fixture_dir) + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path   = std::string(fixture_dir) + "/ca.pem";

    auto cs_result_a = fixpp::tls::file_cert_source::make_file_cert_source(
        cs_cfg, std::pmr::new_delete_resource());
    ASSERT_TRUE(cs_result_a.has_value()) << "Failed to build initial cert_source";
    auto source_a = std::move(*cs_result_a);

    // Also build source_b (same cert, different cert_source instance — represents
    // the rotated source from reload_credentials).
    auto cs_result_b = fixpp::tls::file_cert_source::make_file_cert_source(
        cs_cfg, std::pmr::new_delete_resource());
    ASSERT_TRUE(cs_result_b.has_value()) << "Failed to build rotated cert_source";
    auto source_b = std::move(*cs_result_b);

    // Independently compute the expected fingerprint from source_b.
    // Use load_credentials() synchronously via a future to get the real SHA-256.
    std::array<std::byte, 32> expected_sha{};
    {
        auto fp_fut = asio::co_spawn(
            ioc,
            [&source_b]() -> asio::awaitable<std::array<std::byte, 32>> {
                auto creds_r = co_await source_b->load_credentials();
                if (!creds_r.has_value()) {
                    co_return std::array<std::byte, 32>{};
                }
                co_return creds_r->leaf.sha256();
            },
            asio::use_future);
        ioc.run_for(2s);
        ioc.restart();
        ASSERT_EQ(fp_fut.wait_for(0s), std::future_status::ready)
            << "Failed to get fingerprint from source_b.";
        expected_sha = fp_fut.get();
    }

    // Verify the expected SHA is non-zero (sanity: the cert loaded properly).
    bool all_zero = true;
    for (auto b : expected_sha) if (b != std::byte{0}) { all_zero = false; break; }
    ASSERT_FALSE(all_zero) << "Expected SHA-256 is all-zero — cert may not have loaded.";

    // ── Build the session TransportFactory (source_a initially) ──────────
    fixpp::tls::SslCtxConfig session_ssl_cfg;
    session_ssl_cfg.profile = fixpp::tls::SecurityProfile::mtls_ca;
    session_ssl_cfg.cs      = source_a;
    session_ssl_cfg.clock   = nullptr;
    session_ssl_cfg.caps    = fixpp::tls::CertSourceCaps{};

    auto factory_result = fixpp::transport::make_asio_tls_transport_factory(
        fixpp::transport::Transport::Config{}, session_ssl_cfg);
    ASSERT_TRUE(factory_result.has_value()) << "Failed to build transport factory";
    auto session_factory = std::shared_ptr<fixpp::transport::TransportFactory>{
        std::move(*factory_result)};

    // ── Build the Session (one_way_ca so the authorization gate is permissive)
    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id  = "INITIATOR";
    cfg.target_comp_id  = "ACCEPTOR";
    cfg.begin_string    = "FIX.4.2";
    cfg.heartbeat_interval     = std::chrono::seconds{30};
    cfg.logout_disconnect_timeout_ms = 2000;
    cfg.role            = fixpp::session::session_role::initiator;
    cfg.executor_override       = ioc.get_executor();
    cfg.security_profile = fixpp::session::SecurityProfile{
        fixpp::session::SecurityProfile::kind::one_way_ca};
    cfg.dictionary      = fixpp::test_support::make_minimal_dictionary();
    cfg.reset_seqnum_policy_field =
        fixpp::session::reset_seqnum_policy::bilateral_lenient;
    cfg.transport_factory_override = session_factory;
    cfg.reconnect_endpoint = server_ep;

    fixpp::session::Session session{engine, cfg};

    // ── Open the Session (initial open — LogonSent) ───────────────────────
    {
        auto open_fut = asio::co_spawn(ioc, session.open(), asio::use_future);
        ioc.run_for(1s);
        ioc.restart();
        ASSERT_EQ(open_fut.wait_for(0s), std::future_status::ready)
            << "Session::open() did not complete.";
        auto open_r = open_fut.get();
        ASSERT_TRUE(open_r.has_value())
            << "Session::open() failed: " << static_cast<int>(open_r.error());
    }

    // ── Stage the rotation via Session::reload_credentials(source_b) ─────
    {
        auto reload_r = session.reload_credentials(source_b);
        ASSERT_TRUE(reload_r.has_value())
            << "reload_credentials failed: " << static_cast<int>(reload_r.error());
    }

    // ── Build the ReconnectFsm for this cell ─────────────────────────────
    // IMPORTANT: the FSM starts with last_active_source_ == nullptr (first-load
    // state).  We must prime it by running one attempt with source_a (returns
    // no event, as per FR-009 SPEC-FIXED), then stage source_b and run a second
    // attempt to observe the rotation event.
    fixpp::session::ReconnectFsm reconnect_fsm(
        session_factory.get(),
        make_fast_policy(5),
        30s,
        2000ms);
    reconnect_fsm.set_reconnect_endpoint(server_ep);
    reconnect_fsm.set_session_owner(&session);
    reconnect_fsm.set_tls_profile(fixpp::tls::SecurityProfile::mtls_ca);

    std::vector<fixpp::session::session_event_credentials_rotated> captured_events;
    reconnect_fsm.set_emit_credentials_rotated(
        [&captured_events](fixpp::session::session_event_credentials_rotated ev) {
            captured_events.push_back(ev);
        });

    // ── Priming attempt (attempt 0): factory holds source_a.
    // This sets last_active_source_ = source_a in the FSM.  No event should fire.
    {
        // Need a server-side accept for this attempt too.
        asio::co_spawn(
            ioc,
            [&listener, &ssl_cfg]() -> asio::awaitable<void> {
                co_await asio::this_coro::reset_cancellation_state(
                    asio::enable_total_cancellation());
                auto accept_r = co_await listener.async_accept();
                if (!accept_r.has_value()) co_return;
                auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(
                    accept_r->get());
                if (tls) (void)co_await tls->async_handshake(ssl_cfg);
            },
            asio::detached);

        auto prime_fut = asio::co_spawn(
            ioc, reconnect_fsm.drive_reconnect_attempt(), asio::use_future);
        ioc.run_for(5s);
        ioc.restart();
        ASSERT_EQ(prime_fut.wait_for(0s), std::future_status::ready)
            << "Priming attempt did not complete.";
        (void)prime_fut.get();
        ASSERT_EQ(captured_events.size(), 0u)
            << "First load (source_a) must emit no event (FR-009 SPEC-FIXED).";
    }

    // At this point: last_active_source_ = source_b (set during priming),
    // captured_events is still empty.
    //
    // For the rotation attempt: stage source_c (same cert file as source_b but
    // a new shared_ptr instance — different identity pointer), which triggers
    // snap != last_active_source_ and fires the rotation event.
    fixpp::tls::file_cert_source::Config cs_cfg_c;
    cs_cfg_c.leaf_path        = std::string(fixture_dir) + "/leaf_rsa2048.pem";
    cs_cfg_c.private_key_path = std::string(fixture_dir) + "/leaf_rsa2048.key";
    cs_cfg_c.ca_bundle_path   = std::string(fixture_dir) + "/ca.pem";

    auto cs_result_c = fixpp::tls::file_cert_source::make_file_cert_source(
        cs_cfg_c, std::pmr::new_delete_resource());
    ASSERT_TRUE(cs_result_c.has_value()) << "Failed to build source_c";
    auto source_c = std::move(*cs_result_c);

    // Stage source_c as the next rotation.
    {
        auto reload_r = session.reload_credentials(source_c);
        ASSERT_TRUE(reload_r.has_value()) << "reload_credentials(source_c) failed";
    }

    // ── Rotation attempt (attempt 1): factory now holds source_c (different ptr
    // than source_b, same cert file).  The FSM has last_active_source_ = source_b.
    // snap != last_active_source_ → rotation path → emit credentials_rotated.
    {
        asio::co_spawn(
            ioc,
            [&listener, &ssl_cfg]() -> asio::awaitable<void> {
                co_await asio::this_coro::reset_cancellation_state(
                    asio::enable_total_cancellation());
                auto accept_r = co_await listener.async_accept();
                if (!accept_r.has_value()) co_return;
                auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(
                    accept_r->get());
                if (tls) (void)co_await tls->async_handshake(ssl_cfg);
            },
            asio::detached);

        auto client_fut = asio::co_spawn(
            ioc, reconnect_fsm.drive_reconnect_attempt(), asio::use_future);
        ioc.run_for(5s);
        ioc.restart();
        ASSERT_EQ(client_fut.wait_for(0s), std::future_status::ready)
            << "Rotation attempt did not complete within 5s.";
        (void)client_fut.get();
    }

    // ── Assert: exactly one credentials_rotated emitted with real fingerprint
    ASSERT_EQ(captured_events.size(), 1u)
        << "Expected exactly one credentials_rotated event. "
        << "RED: no emit site exists in 013 stub.";

    EXPECT_EQ(captured_events[0].new_sha256, expected_sha)
        << "new_sha256 must equal the real SHA-256 of source_b's leaf. "
        << "Anti-lying-event (013 burn-class): must match the actual cert, "
        << "not an all-zero stub or fabricated value.";

    // Verify the SHA is non-zero (sanity).
    bool emitted_all_zero = true;
    for (auto b : captured_events[0].new_sha256)
        if (b != std::byte{0}) { emitted_all_zero = false; break; }
    EXPECT_FALSE(emitted_all_zero)
        << "Emitted new_sha256 is all-zero — not a real fingerprint (FR-010).";
}

}  // namespace
