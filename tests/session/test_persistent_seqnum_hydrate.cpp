// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_persistent_seqnum_hydrate.cpp
//
// 029-persistent-seqnum-hydrate unit test suite.
//
// Phase 1 (Setup) — T001: skeleton compiles and registers.
// Real witnesses (T006/T008/T009/T012) are added in later phases.
//
// Fixture shape mirrors test_next_expected_msgseqnum.cpp:
//   struct OutboundCapture — captures outbound frames via transport_send_.
//   class CountingApp029 — Application subclass recording callback invocations.
//   struct Fixture — io_context + SessionConfig + Session under test.
//   make_acceptor / make_initiator — helpers that build Active / LogonSent sessions.
//
// Tests are free TEST(...) macros (not TEST_F) per tasks.md T001 convention.
//
// FaultStore: fault-injecting / callback-observing test MessageStore for W3/W6/W14.
//   - Pre-seeded inbound/outbound counters for next_seqnum(dir, false).
//   - Configurable next_seqnum failure on the Nth call (for read/write-failure injection).
//   - Observable durable counter value at callback time.
//
// Anchors: spec.md FR-001..012, SC-001..006; data-model.md W1..W14;
//          contracts/seqnum-hydrate.md C1..C4.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

using namespace std::chrono_literals;

// mallocnesia weak-symbol hooks — replaced by LD_PRELOAD; no-ops otherwise.
// Must be at file scope for the LD_PRELOAD override to bind.
extern "C" {
// NOLINTNEXTLINE(misc-use-anonymous-namespace) — must be at file scope for LD_PRELOAD override.
__attribute__((weak)) void alloc_guard_start() {}
// NOLINTNEXTLINE(misc-use-anonymous-namespace)
__attribute__((weak)) void alloc_guard_end() {}
// NOLINTNEXTLINE(misc-use-anonymous-namespace)
__attribute__((weak)) long alloc_guard_count() { return 0; }
}

namespace {

// ── Application stub ──────────────────────────────────────────────────────────

// CountingApp029: records callback invocations per type.
// Used to assert delivery (fromApp/fromAdmin) and lifecycle notifications.
class CountingApp029 : public fixpp::session::Application {
public:
    int from_app_count{0};
    int from_admin_count{0};
    int to_admin_count{0};
    int on_logon_count{0};

    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const fixpp::session::SessionId& /*id*/) override {
        ++from_app_count;
        return {};
    }

    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const fixpp::session::SessionId& /*id*/) override {
        ++from_admin_count;
        return {};
    }

    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
                 const fixpp::session::SessionId& /*id*/) override {
        ++to_admin_count;
    }

    void onLogon(const fixpp::session::SessionId& /*id*/) override { ++on_logon_count; }
};

// ── Frame-building helpers (mirror test_next_expected_msgseqnum.cpp) ──────────

static std::string field(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

static std::vector<std::byte> make_fix_frame(std::string_view begin_string,
                                             std::string_view msg_type, std::uint32_t seq,
                                             std::string_view sender, std::string_view target,
                                             std::string_view extra = {}) {
    std::string body;
    body += field(35, msg_type);
    body += field(34, std::to_string(seq));
    body += field(49, sender);
    body += field(52, "20240101-00:00:00.000");
    body += field(56, target);
    if (!extra.empty()) body += std::string(extra);

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

static std::vector<std::byte> make_logon(std::string_view bs, std::uint32_t seq,
                                         std::string_view s, std::string_view t, int hbt = 30) {
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

// ── Outbound capture ──────────────────────────────────────────────────────────

struct OutboundCapture {
    std::vector<std::vector<std::byte>> frames;
    void operator()(std::span<const std::byte> data) {
        frames.emplace_back(data.begin(), data.end());
    }
};

// ── Frame field extractor ─────────────────────────────────────────────────────

static std::string extract_tag(const std::vector<std::byte>& frame, int tag) {
    const auto* data = reinterpret_cast<const char*>(frame.data());
    std::string sv(data, frame.size());
    const std::string needle = std::to_string(tag) + "=";
    auto pos = sv.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    auto end = sv.find('\x01', pos);
    if (end == std::string::npos) return sv.substr(pos);
    return sv.substr(pos, end - pos);
}

// ── FaultStore: fault-injecting / callback-observing test MessageStore ─────────
//
// Supports:
//   (a) Pre-seeded inbound/outbound counters returned by next_seqnum(dir, false).
//   (b) Configurable failure on the Nth next_seqnum call (for W3/W6/W14 injection).
//   (c) Observable durable_inbound: the counter value at the time of the last
//       next_seqnum(inbound, true) call — lets tests verify persist ordering.
//
// IMPORTANT: next_seqnum(dir, true) simulates a persistent-store write:
//   - increments the internal counter and records the new value in durable_inbound.
//   - Returns an error if the call count reaches fail_on_nth_call_.
//
// next_seqnum(dir, false) is the read path used by ensure_hydrated_:
//   - Returns the pre-seeded counter for that direction.
//   - Also subject to fail_on_nth_call_ (for W14 read-failure injection).

using fixpp::session::direction_t;
using fixpp::session::MessageStore;
using fixpp::session::MessageStoreFactory;
using fixpp::session::retrieve_visitor;
using fixpp::session::seqnum_t;
using fixpp::session::visit_result;

class FaultStore final : public MessageStore {
public:
    // seeded_inbound / seeded_outbound: values returned by next_seqnum(dir, false).
    // fail_on_nth_call: if nonzero, the Nth call to next_seqnum fails with
    //   store_io_failure. Calls are counted across both directions and both
    //   increment/read variants.
    explicit FaultStore(seqnum_t seeded_inbound = 1, seqnum_t seeded_outbound = 1,
                        int fail_on_nth_call = 0)
        : MessageStore(flush_thunk_for<FaultStore>()),
          next_inbound_(seeded_inbound),
          next_outbound_(seeded_outbound),
          fail_on_nth_call_(fail_on_nth_call) {}

    // Observable state for witnesses:
    mutable int call_count{0};         // total next_seqnum calls
    seqnum_t durable_inbound{1};       // last persisted inbound counter (increment=true)

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store(
        seqnum_t /*seq*/, std::span<const std::byte> /*frame*/,
        direction_t /*dir*/) noexcept override {
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t /*begin*/, seqnum_t /*end*/, direction_t /*dir*/,
        retrieve_visitor& /*visitor*/) noexcept override {
        co_return std::unexpected(fixpp::core::error::store_seqnum_gap);
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool increment) noexcept override {
        ++call_count;
        if (fail_on_nth_call_ > 0 && call_count >= fail_on_nth_call_) {
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }
        if (increment) {
            if (dir == direction_t::inbound) {
                ++next_inbound_;
                durable_inbound = next_inbound_;
                co_return next_inbound_ - 1U;  // return the pre-increment value
            } else {
                ++next_outbound_;
                co_return next_outbound_ - 1U;
            }
        } else {
            co_return (dir == direction_t::inbound) ? next_inbound_ : next_outbound_;
        }
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        next_inbound_ = 1;
        next_outbound_ = 1;
        durable_inbound = 1;
        co_return fixpp::core::expected_t<void>{};
    }

private:
    seqnum_t next_inbound_;
    seqnum_t next_outbound_;
    int fail_on_nth_call_;  // 0 = never fail
};

// FaultStoreFactory: wraps a FaultStore for use with SessionConfig.
// NOTE: yields_persistent_store() is NOT overridden here (T004 adds it).
// This factory uses the base-class default (which is true in the post-T004 world).
// For Phase 1 we omit the override entirely since MessageStoreFactory::yields_persistent_store()
// does not exist yet — it is added in T004.
// TODO(T004): override yields_persistent_store() once MessageStoreFactory gains the method.
class FaultStoreFactory final : public MessageStoreFactory {
public:
    explicit FaultStoreFactory(seqnum_t seeded_inbound = 1, seqnum_t seeded_outbound = 1,
                               int fail_on_nth_call = 0)
        : seeded_inbound_(seeded_inbound),
          seeded_outbound_(seeded_outbound),
          fail_on_nth_call_(fail_on_nth_call) {}

    // The last store minted by make() — for observable state in witnesses.
    mutable FaultStore* last_store{nullptr};

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/,
        std::pmr::memory_resource* /*mr*/, std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override {
        auto store = std::make_unique<FaultStore>(seeded_inbound_, seeded_outbound_,
                                                  fail_on_nth_call_);
        last_store = store.get();
        return store;
    }

private:
    seqnum_t seeded_inbound_;
    seqnum_t seeded_outbound_;
    int fail_on_nth_call_;
};

// ── Session fixture ───────────────────────────────────────────────────────────

struct Fixture {
    asio::io_context ioc;
    OutboundCapture capture;
    fixpp::core::EngineConfig eng;
    fixpp::session::SessionConfig cfg;
    std::unique_ptr<fixpp::session::Session> session;

    void feed(const std::vector<std::byte>& frame) {
        auto fut = asio::co_spawn(ioc, session->on_inbound_frame(std::span<const std::byte>(frame)),
                                  asio::use_future);
        ioc.run_for(5s);
        ioc.restart();
        (void)fut.get();
    }

    void clear_capture() { capture.frames.clear(); }
};

// make_acceptor: build an acceptor Session through its Logon handshake (Active state).
static std::unique_ptr<Fixture> make_acceptor(
    std::shared_ptr<MessageStoreFactory> store_factory, std::uint32_t peer_logon_seq = 1,
    bool enable_789 = false, bool reset_on_logon = false,
    std::shared_ptr<fixpp::session::Application> app = nullptr) {
    auto fix = std::make_unique<Fixture>();

    fix->cfg.role = fixpp::session::session_role::acceptor;
    fix->cfg.sender_comp_id = "SRV";
    fix->cfg.target_comp_id = "CLI";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{30};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = std::move(store_factory);
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.enable_next_expected_msg_seq_num = enable_789;
    fix->cfg.reset_on_logon = reset_on_logon;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    if (app) {
        fix->eng.application = std::move(app);
    }

    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    // open() — sets NotConnected for acceptor.
    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();

    // Feed peer Logon to reach Active.
    fix->feed(make_logon("FIX.4.4", peer_logon_seq, "CLI", "SRV"));

    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "make_acceptor: session must be Active after Logon";

    return fix;
}

// make_initiator: build an initiator Session, call open() to emit the outbound Logon
// (which transitions the session to LogonSent). The outbound capture holds
// exactly the Logon frame after open().
static std::unique_ptr<Fixture> make_initiator(
    std::shared_ptr<MessageStoreFactory> store_factory, bool enable_789 = false,
    bool reset_on_logon = false,
    std::shared_ptr<fixpp::session::Application> app = nullptr) {
    auto fix = std::make_unique<Fixture>();

    fix->cfg.role = fixpp::session::session_role::initiator;
    fix->cfg.sender_comp_id = "CLI";
    fix->cfg.target_comp_id = "SRV";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{0};  // disable liveness in LogonSent
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = std::move(store_factory);
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.enable_next_expected_msg_seq_num = enable_789;
    fix->cfg.reset_on_logon = reset_on_logon;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    if (app) {
        fix->eng.application = std::move(app);
    }

    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    // open() emits the initiator Logon and transitions to LogonSent.
    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(2s);
    fix->ioc.restart();
    (void)open_fut.get();

    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::LogonSent)
        << "make_initiator: session must be LogonSent after open()";

    return fix;
}

// ── Phase 1 Setup: skeleton build smoke ──────────────────────────────────────
//
// This test exists only to confirm the skeleton compiles, links, and ctest
// discovers the target. All real witnesses (W1–W14) are added in Phases 3–5.

TEST(PersistentSeqnumHydrate, SkeletonBuilds) {
    // Placeholder: confirms the skeleton compiles + links + ctest registers
    // the target. Real assertions are added in later phases (T006/T008/T009/T012).
    SUCCEED();
}

// ── Phase 3 (T006) — RED witnesses: US1 outbound resume ──────────────────────
//
// All four witnesses below are written RED-first (before T007 wires ensure_hydrated_).
// They fail because without ensure_hydrated_(), the seqnum manager starts at 1
// regardless of the store's persisted value.
//
// Anchors: spec.md SC-001, FR-003/004/006; data-model.md W1/W8/W9a/W14;
//          contracts/seqnum-hydrate.md C2.1–C2.6; research.md D-1/D-6/D-9.

// W1 — Initiator_Restart_Resumes_Outbound (SC-001, FR-003/004)
//
// Pre-seed the persistent store to next_outbound=42. Build a fresh initiator
// session over that store and call open(). The emitted Logon MUST carry 34=42
// (not 34=1). Before T007, the seqnum manager ignores the store, so 34=1 and
// this witness FAILs.
TEST(PersistentSeqnumHydrate, Initiator_Restart_Resumes_Outbound) {
    // FaultStoreFactory: persistent (inherits yields_persistent_store()==true),
    // seeded next_outbound=42 (what the store says the next outbound should be).
    auto factory =
        std::make_shared<FaultStoreFactory>(/*seeded_inbound=*/1, /*seeded_outbound=*/42);

    auto fix = make_initiator(factory);

    // Exactly one outbound frame emitted: the Logon.
    ASSERT_EQ(fix->capture.frames.size(), 1u) << "Expected exactly one outbound frame (Logon)";

    const auto& logon_frame = fix->capture.frames[0];
    const std::string seq_str = extract_tag(logon_frame, 34);
    ASSERT_FALSE(seq_str.empty()) << "Logon frame missing tag 34";
    const int seq = std::stoi(seq_str);

    // Post T007: hydrate seeds next_outbound=42 → Logon emits 34=42.
    // Pre T007 (RED): manager starts at 1, Logon emits 34=1 → FAIL.
    EXPECT_EQ(seq, 42) << "Logon must carry 34=42 (resumed from persistent store); got 34=" << seq;
}

// W8 — Hydrate_OneShot_FiresOnce_BothRoles_NotOnReconnect (INV-H3)
//
// Asserts that ensure_hydrated_ fires (reads the store) EXACTLY ONCE per session
// for both initiator and acceptor, and that a second open attempt (simulating a
// reconnect on the same session object) does NOT trigger a re-hydrate.
//
// Observable proxy: the FaultStore's call_count tracks next_seqnum calls.
// ensure_hydrated_ issues exactly 2 reads (inbound=false, outbound=false) per hydrate.
// After the first open, call_count == 2 (initiator) or 2 (acceptor).
// The manager's next_outbound_unsafe() must NOT regress after the initial hydrate:
// if re-hydration occurred, the manager would be reset to the store's (lower) value.
//
// Pre T007 (RED):
//   - Initiator: call_count == 0 (no reads at all) → FAIL (expected 2).
//   - Acceptor: call_count == 0 (no reads at all) → FAIL (expected 2).
TEST(PersistentSeqnumHydrate, Hydrate_OneShot_FiresOnce_BothRoles_NotOnReconnect) {
    // ── Initiator arm ────────────────────────────────────────────────────────
    {
        auto factory = std::make_shared<FaultStoreFactory>(/*seeded_inbound=*/1,
                                                           /*seeded_outbound=*/42);
        auto fix = make_initiator(factory);

        FaultStore* store = factory->last_store;
        ASSERT_NE(store, nullptr) << "FaultStoreFactory must have minted a store";

        // After open() + Logon emission, ensure_hydrated_ must have fired exactly
        // once: 2 reads (next_seqnum(inbound,false) + next_seqnum(outbound,false)).
        // Phase 3 has no persist writes, so ALL call_count increments are hydrate reads.
        EXPECT_EQ(store->call_count, 2)
            << "Initiator: ensure_hydrated_ must read the store exactly twice (in+out);"
            << " call_count=" << store->call_count;

        // The manager MUST reflect the hydrated outbound value.
        const seqnum_t next_out =
            fix->session->seqnum_mgr_test_access().next_outbound_unsafe();
        // After emitting Logon at seqnum 42, next_outbound advances to 43.
        EXPECT_EQ(next_out, 43u)
            << "Initiator: after emitting Logon(34=42), next_outbound must be 43; got "
            << next_out;

        // "Not on reconnect": a second open() on the SAME session object returns
        // already-open (does NOT run ensure_hydrated_ again). The call_count stays 2
        // and next_outbound stays 43 (not regressed to 42).
        // We verify by checking the count doesn't change — no additional ioc.run() needed
        // since open() on an already-open session returns immediately without suspend.
        const int count_before = store->call_count;
        // open() on an already-open session returns session_already_open; we swallow it.
        auto open2_fut =
            asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
        fix->ioc.run_for(1s);
        fix->ioc.restart();
        (void)open2_fut;  // expected to error; we only care about the side effects.

        EXPECT_EQ(store->call_count, count_before)
            << "Initiator: a second open() (reconnect simulation) must NOT re-hydrate;"
            << " call_count changed from " << count_before << " to " << store->call_count;
        EXPECT_EQ(fix->session->seqnum_mgr_test_access().next_outbound_unsafe(), next_out)
            << "Initiator: next_outbound must not regress on a simulated reconnect";
    }

    // ── Acceptor arm ─────────────────────────────────────────────────────────
    {
        auto factory = std::make_shared<FaultStoreFactory>(/*seeded_inbound=*/1,
                                                           /*seeded_outbound=*/37);
        auto fix = make_acceptor(factory, /*peer_logon_seq=*/1);

        FaultStore* store = factory->last_store;
        ASSERT_NE(store, nullptr) << "FaultStoreFactory must have minted a store";

        // After make_acceptor completes (NotConnected → Active via peer Logon),
        // ensure_hydrated_ must have fired exactly once: 2 reads.
        EXPECT_EQ(store->call_count, 2)
            << "Acceptor: ensure_hydrated_ must read the store exactly twice (in+out);"
            << " call_count=" << store->call_count;

        // The acceptor reply Logon samples next_outbound BEFORE advancing.
        // After hydrating to 37 and emitting the reply Logon at seq=37, next_outbound==38.
        const seqnum_t next_out =
            fix->session->seqnum_mgr_test_access().next_outbound_unsafe();
        EXPECT_EQ(next_out, 38u)
            << "Acceptor: after emitting reply Logon(34=37), next_outbound must be 38; got "
            << next_out;
    }
}

// W9a — ResetOnLogon_Wins_Over_OutboundHydrate (INV-H5)
//
// When reset_on_logon=true, the durable reset fires inside emit_initiator_logon_()
// AFTER ensure_hydrated_() has seeded next_outbound=42 → the reset wins, bringing
// next_outbound back to 1. The emitted Logon carries 34=1, NOT 34=42.
// (The outbound hydrate runs BEFORE the reset_on_logon block; the reset overwrites it.)
//
// Pre T007 (RED): ensure_hydrated_ is not called, so next_outbound starts at 1 and
// reset_on_logon resets it to 1 again — the Logon emits 34=1 for the WRONG reason
// (hydrate never ran, not because reset won). The test would accidentally pass if we
// only checked 34=1. We also check that the store was read (call_count==2) to confirm
// hydrate DID run and was overridden. Pre T007, call_count==0 → FAIL.
TEST(PersistentSeqnumHydrate, ResetOnLogon_Wins_Over_OutboundHydrate) {
    // reset_on_logon=true + persisted {in=37, out=42}.
    auto factory =
        std::make_shared<FaultStoreFactory>(/*seeded_inbound=*/37, /*seeded_outbound=*/42);
    auto fix = make_initiator(factory, /*enable_789=*/false, /*reset_on_logon=*/true);

    ASSERT_EQ(fix->capture.frames.size(), 1u) << "Expected exactly one outbound frame (Logon)";
    const auto& logon_frame = fix->capture.frames[0];

    // Post T007: hydrate seeds {37,42}; reset_on_logon fires and brings outbound to 1.
    const std::string seq_str = extract_tag(logon_frame, 34);
    ASSERT_FALSE(seq_str.empty()) << "Logon frame missing tag 34";
    const int seq = std::stoi(seq_str);
    EXPECT_EQ(seq, 1)
        << "reset_on_logon must win over outbound hydrate; Logon must carry 34=1; got 34=" << seq;

    // The hydrate reads MUST have fired (call_count==2) to prove "hydrate ran but reset won",
    // not "hydrate never ran and reset happened to produce the same result".
    // Pre T007 (RED): call_count==0 → FAIL on this check even if 34=1 accidentally matches.
    FaultStore* store = factory->last_store;
    ASSERT_NE(store, nullptr);
    EXPECT_EQ(store->call_count, 2)
        << "ensure_hydrated_ must have run (2 reads) before reset_on_logon fired;"
        << " call_count=" << store->call_count;
}

// W14 — HydrateReadFailure_Fatal_NoPartialSeed (SC-005, FR-006, C2.3, D-9/INV-H6)
//
// inject a next_seqnum(dir,false) READ failure on the hydrate path → session
// transitions to Disconnected (fatal), manager is NOT partially seeded,
// hydrated_ is left false.
//
// Split:
//   (a) First read (inbound,false) fails → no mutation at all.
//   (b) Second read (outbound,false) fails after the first succeeded → still no mutation.
//
// "No partial seed" is witnessed by asserting both manager counters remain at their
// construction defaults (next_inbound==1, next_outbound==1).
//
// Pre T007 (RED): ensure_hydrated_ is not called, so:
//   - The session does NOT go to Disconnected (stays LogonSent) → FAIL.
//   - The call_count is 0 (no reads) → FAIL (though moot since first assertion fails).

// W14(a): first read (inbound) fails → session Disconnected, manager unchanged.
TEST(PersistentSeqnumHydrate, HydrateReadFailure_Fatal_NoPartialSeed_FirstReadFails) {
    // fail_on_nth_call=1: fail on the FIRST next_seqnum call (the inbound read).
    auto factory = std::make_shared<FaultStoreFactory>(/*seeded_inbound=*/10,
                                                       /*seeded_outbound=*/20,
                                                       /*fail_on_nth_call=*/1);
    // make_initiator calls open() which triggers ensure_hydrated_ → first read fails.
    // We cannot use make_initiator (it asserts LogonSent) — build manually.
    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::initiator;
    fix->cfg.sender_comp_id = "CLI";
    fix->cfg.target_comp_id = "SRV";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{0};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = factory;
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(2s);
    fix->ioc.restart();
    (void)open_fut;  // error expected; we care about the side effects.

    // Post T007: hydrate fails on first read → session must be Disconnected.
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
        << "W14(a): first-read failure must transition session to Disconnected";

    // No frame must have been emitted (Logon was never built/sent).
    EXPECT_EQ(fix->capture.frames.size(), 0u)
        << "W14(a): no frame must be emitted when hydrate fails before the Logon";

    // Manager must be completely unmodified — next_inbound==1, next_outbound==1.
    // (C2.3: "no partial seed" — mutate only after BOTH reads succeed.)
    const seqnum_t ni = fix->session->seqnum_mgr_test_access().next_inbound_unsafe();
    const seqnum_t no = fix->session->seqnum_mgr_test_access().next_outbound_unsafe();
    EXPECT_EQ(ni, 1u)
        << "W14(a): next_inbound must remain at construction default 1 after first-read failure;"
        << " got " << ni;
    EXPECT_EQ(no, 1u)
        << "W14(a): next_outbound must remain at construction default 1 after first-read failure;"
        << " got " << no;
}

// W14(b): second read (outbound) fails after the first succeeded → manager still unchanged.
TEST(PersistentSeqnumHydrate, HydrateReadFailure_Fatal_NoPartialSeed_SecondReadFails) {
    // fail_on_nth_call=2: the FIRST call succeeds (inbound read), the SECOND fails
    // (outbound read). This proves "no partial seed" even when inbound was read OK.
    auto factory = std::make_shared<FaultStoreFactory>(/*seeded_inbound=*/10,
                                                       /*seeded_outbound=*/20,
                                                       /*fail_on_nth_call=*/2);
    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::initiator;
    fix->cfg.sender_comp_id = "CLI";
    fix->cfg.target_comp_id = "SRV";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{0};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = factory;
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(2s);
    fix->ioc.restart();
    (void)open_fut;

    // Post T007: hydrate fails on second read → session must be Disconnected.
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
        << "W14(b): second-read failure must transition session to Disconnected";

    // No frame emitted.
    EXPECT_EQ(fix->capture.frames.size(), 0u)
        << "W14(b): no frame must be emitted when hydrate fails before the Logon";

    // Manager must be completely unmodified — C2.3 "no partial seed".
    // Even though the inbound read succeeded, hydrate() is not called until BOTH reads
    // succeed; the manager stays at construction defaults.
    const seqnum_t ni = fix->session->seqnum_mgr_test_access().next_inbound_unsafe();
    const seqnum_t no = fix->session->seqnum_mgr_test_access().next_outbound_unsafe();
    EXPECT_EQ(ni, 1u)
        << "W14(b): next_inbound must remain at construction default 1 after second-read failure;"
        << " got " << ni;
    EXPECT_EQ(no, 1u)
        << "W14(b): next_outbound must remain at construction default 1 after second-read failure;"
        << " got " << no;
}

}  // namespace
