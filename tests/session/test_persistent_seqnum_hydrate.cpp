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
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

static std::vector<std::byte> make_logon(std::string_view bs, std::uint32_t seq, std::string_view s,
                                         std::string_view t, int hbt = 30) {
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
//   (b) Configurable failure on the Nth next_seqnum call (for W14 read-failure injection).
//   (c) Configurable failure on the Nth inbound WRITE call (for W6 write-failure injection).
//   (d) Observable durable_inbound: the counter value after the last
//       next_seqnum(inbound, true) — lets tests verify persist ordering (W3/W6).
//   (e) on_inbound_write_hook: called synchronously inside next_seqnum(inbound,true)
//       BEFORE the durable counter is updated — used by W3 to capture the store value
//       at callback time via a coordinating application.
//
// IMPORTANT: next_seqnum(dir, true) simulates a persistent-store write:
//   - Calls on_inbound_write_hook (if set) BEFORE updating durable_inbound.
//   - Increments the internal counter and records the new value in durable_inbound.
//   - Returns an error if fail_on_nth_write_ is set and the write count reaches it.
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
    // fail_on_nth_call: if nonzero, the Nth TOTAL call (read+write) to next_seqnum fails.
    //   Used for W14 (hydrate READ failure injection).
    // fail_on_nth_write: if nonzero, the Nth WRITE (increment=true, inbound) call fails.
    //   Used for W6 (persist write failure injection). Counted independently from reads.
    // fail_on_nth_outbound_write: if nonzero, the Nth WRITE (increment=true, outbound)
    //   call fails. Used for T014 W5 (outbound persist failure injection).
    explicit FaultStore(seqnum_t seeded_inbound = 1, seqnum_t seeded_outbound = 1,
                        int fail_on_nth_call = 0, int fail_on_nth_write = 0,
                        int fail_on_nth_outbound_write = 0)
        : MessageStore(flush_thunk_for<FaultStore>()),
          next_inbound_(seeded_inbound),
          next_outbound_(seeded_outbound),
          fail_on_nth_call_(fail_on_nth_call),
          fail_on_nth_write_(fail_on_nth_write),
          fail_on_nth_outbound_write_(fail_on_nth_outbound_write) {}

    // Observable state for witnesses:
    mutable int call_count{0};     // total next_seqnum calls (read + write)
    mutable int write_count{0};    // inbound write (increment=true) call count
    mutable int outbound_write_count{0};  // outbound write (increment=true) call count
    seqnum_t durable_inbound{1};   // last persisted inbound counter (after increment)
    seqnum_t durable_outbound{1};  // last persisted outbound counter (after increment)

    // W3 hook: if set, called BEFORE updating durable_inbound in next_seqnum(inbound,true).
    // Receives the CURRENT durable_inbound (pre-persist value) — which should equal
    // the sequence number of the message being delivered (not S+1) if deliver-then-persist
    // is correct (INV-H2).
    std::function<void(seqnum_t pre_persist_value)> on_inbound_write_hook;

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
        // W14: total-call failure injection (for hydrate READ failures).
        if (fail_on_nth_call_ > 0 && call_count >= fail_on_nth_call_) {
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }
        if (increment) {
            if (dir == direction_t::inbound) {
                ++write_count;
                // W6: write-only failure injection (for persist WRITE failures).
                if (fail_on_nth_write_ > 0 && write_count >= fail_on_nth_write_) {
                    co_return std::unexpected(fixpp::core::error::store_io_failure);
                }
                // W3: notify before updating durable_inbound so a concurrent
                // fromApp callback sees the pre-persist value.
                if (on_inbound_write_hook) {
                    on_inbound_write_hook(durable_inbound);
                }
                ++next_inbound_;
                durable_inbound = next_inbound_;
                co_return next_inbound_ - 1U;  // return the pre-increment value
            } else {
                ++outbound_write_count;
                // T014 W5: outbound write failure injection.
                if (fail_on_nth_outbound_write_ > 0 &&
                    outbound_write_count >= fail_on_nth_outbound_write_) {
                    co_return std::unexpected(fixpp::core::error::store_io_failure);
                }
                ++next_outbound_;
                durable_outbound = next_outbound_;
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
        durable_outbound = 1;
        co_return fixpp::core::expected_t<void>{};
    }

    // For restart simulation: read the current live counter values.
    [[nodiscard]] seqnum_t current_inbound() const noexcept { return next_inbound_; }
    [[nodiscard]] seqnum_t current_outbound() const noexcept { return next_outbound_; }

private:
    seqnum_t next_inbound_;
    seqnum_t next_outbound_;
    int fail_on_nth_call_;            // 0 = never fail on total-call count (W14)
    int fail_on_nth_write_;           // 0 = never fail on inbound write count (W6)
    int fail_on_nth_outbound_write_;  // 0 = never fail on outbound write count (T014 W5)
};

// FaultStoreFactory: wraps a FaultStore for use with SessionConfig.
// yields_persistent_store() overrides to true (FaultStore is a persistent store).
// For non-persistent tests, use NonPersistentFaultStoreFactory below.
class FaultStoreFactory final : public MessageStoreFactory {
public:
    explicit FaultStoreFactory(seqnum_t seeded_inbound = 1, seqnum_t seeded_outbound = 1,
                               int fail_on_nth_call = 0, int fail_on_nth_write = 0,
                               int fail_on_nth_outbound_write = 0)
        : seeded_inbound_(seeded_inbound),
          seeded_outbound_(seeded_outbound),
          fail_on_nth_call_(fail_on_nth_call),
          fail_on_nth_write_(fail_on_nth_write),
          fail_on_nth_outbound_write_(fail_on_nth_outbound_write) {}

    // The last store minted by make() — for observable state in witnesses.
    mutable FaultStore* last_store{nullptr};

    // Override: FaultStore is persistent (it tracks durable counters).
    [[nodiscard]] bool yields_persistent_store() const noexcept override { return true; }

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/, std::pmr::memory_resource* /*mr*/,
        std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override {
        auto store =
            std::make_unique<FaultStore>(seeded_inbound_, seeded_outbound_, fail_on_nth_call_,
                                         fail_on_nth_write_, fail_on_nth_outbound_write_);
        last_store = store.get();
        return store;
    }

private:
    seqnum_t seeded_inbound_;
    seqnum_t seeded_outbound_;
    int fail_on_nth_call_;
    int fail_on_nth_write_;
    int fail_on_nth_outbound_write_;
};

// NonPersistentFaultStoreFactory: same as FaultStoreFactory but overrides
// yields_persistent_store() to return false. Used by W13 (custom non-persistent
// discriminator) and W7 (verifying the skip fires for a non-persistent custom store).
// The underlying FaultStore's call_count is observable: if the non-persistent skip
// works, no next_seqnum reads are issued and call_count==0.
class NonPersistentFaultStoreFactory final : public MessageStoreFactory {
public:
    explicit NonPersistentFaultStoreFactory(seqnum_t seeded_inbound = 1,
                                            seqnum_t seeded_outbound = 1)
        : seeded_inbound_(seeded_inbound), seeded_outbound_(seeded_outbound) {}

    mutable FaultStore* last_store{nullptr};

    // Override: this factory's stores are NOT persistent — ensure_hydrated_ must
    // skip the read (C2.2 / INV-H4 / D-10). A missed override would default to
    // the base class's 'true' and incorrectly run the hydrate read path.
    [[nodiscard]] bool yields_persistent_store() const noexcept override { return false; }

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/, std::pmr::memory_resource* /*mr*/,
        std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override {
        auto store = std::make_unique<FaultStore>(seeded_inbound_, seeded_outbound_,
                                                  /*fail_on_nth_call=*/0,
                                                  /*fail_on_nth_write=*/0);
        last_store = store.get();
        return store;
    }

private:
    seqnum_t seeded_inbound_;
    seqnum_t seeded_outbound_;
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
    bool reset_on_logon = false, std::shared_ptr<fixpp::session::Application> app = nullptr) {
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
        const seqnum_t next_out = fix->session->seqnum_mgr_test_access().next_outbound_unsafe();
        // After emitting Logon at seqnum 42, next_outbound advances to 43.
        EXPECT_EQ(next_out, 43u)
            << "Initiator: after emitting Logon(34=42), next_outbound must be 43; got " << next_out;

        // "Not on reconnect": a second open() on the SAME session object returns
        // already-open (does NOT run ensure_hydrated_ again). The call_count stays 2
        // and next_outbound stays 43 (not regressed to 42).
        // We verify by checking the count doesn't change — no additional ioc.run() needed
        // since open() on an already-open session returns immediately without suspend.
        const int count_before = store->call_count;
        // open() on an already-open session returns session_already_open; we swallow it.
        auto open2_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
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
        // ensure_hydrated_ must have fired exactly once: 2 reads (inbound+outbound).
        // Post-T010: Logon accept also fires persist_inbound_advance_() → 1 write.
        // Total: 2 reads + 1 write = call_count==3.
        EXPECT_EQ(store->call_count, 3)
            << "Acceptor: ensure_hydrated_ reads (2) + Logon persist write (1) = 3;"
            << " call_count=" << store->call_count;

        // The acceptor reply Logon samples next_outbound BEFORE advancing.
        // After hydrating to 37 and emitting the reply Logon at seq=37, next_outbound==38.
        const seqnum_t next_out = fix->session->seqnum_mgr_test_access().next_outbound_unsafe();
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

// ── Phase 4a (T008) — RED witnesses: US2 inbound durable-track / resume ──────
//
// All witnesses below are written RED-first (before T010/T011 implement the persist
// matrix and inbound seed). Each fails because:
//   W2: no persist → durable counter stays seeded value (not 6), restart sees seeded value.
//   W3: no persist write → durable counter NOT updated inside fromApp callback.
//   W4: no inbound seed → peer Logon 34=37 is too-high → AwaitingResend, not Active.
//   W6: no persist write call → no fatal on injected write-failure (never fires).
//   W8-hb: no inbound seed → next_inbound_unsafe() at toAdmin time is 2 (not 38).
//
// Anchors: spec.md SC-002/004/006; data-model.md W2/W3/W4/W6/W8;
//          contracts/seqnum-hydrate.md C2.4/C3; data-model.md INV-H1/H2.

// ── W3 helper: StorePeekApp ───────────────────────────────────────────────────
//
// An Application that captures the FaultStore's durable_inbound value AT THE MOMENT
// fromApp is called for the Nth app message. This lets W3 assert that the persist
// has NOT happened yet when the callback fires (INV-H2: deliver-then-persist).
//
// Usage: set observe_at_call_n to the fromApp call number to capture (1-indexed).
// After fromApp returns, durable_at_fromapp holds the store's durable_inbound value
// as observed inside the callback.
class StorePeekApp final : public fixpp::session::Application {
public:
    // The store to read from inside fromApp. Must outlive this object.
    FaultStore* store_ptr{nullptr};
    int observe_at_call_n{1};  // which fromApp call to observe (1-indexed)
    int fromapp_call_count{0};
    seqnum_t durable_at_fromapp{0};  // durable_inbound value seen inside fromApp

    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const fixpp::session::SessionId& /*id*/) override {
        ++fromapp_call_count;
        if (store_ptr != nullptr && fromapp_call_count == observe_at_call_n) {
            // Capture the durable value INSIDE the callback.
            // If deliver-then-persist is correct, this is the PRE-PERSIST value.
            durable_at_fromapp = store_ptr->durable_inbound;
        }
        return {};
    }

    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const fixpp::session::SessionId& /*id*/) override {
        return {};
    }

    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
                 const fixpp::session::SessionId& /*id*/) override {}
};

// ── W8-hb helper: InboundHydrateObserverApp ──────────────────────────────────
//
// An Application whose toAdmin callback records next_inbound_unsafe() at the moment
// the first outbound admin frame (reply Logon or outbound Logon-ack) fires.
// For the acceptor, toAdmin fires for the reply Logon AFTER ensure_hydrated_() and
// AFTER check_inbound() has advanced next_inbound by 1. So:
//   - If the inbound seed was 37 (post-T011): next_inbound_unsafe() == 38 at toAdmin.
//   - If the inbound seed was NOT applied (pre-T011): next_inbound_unsafe() == 2 at toAdmin.
// This creates an observable that proves "hydrate completes-before check_inbound" by
// asserting the toAdmin-time value equals seeded_inbound + 1, not 2 (construction + 1).
// The assertion FAILS pre-T011 since without an inbound seed, the value is 2, not 38.
class InboundHydrateObserverApp final : public fixpp::session::Application {
public:
    // The session to read next_inbound_unsafe() from.
    // Set after session construction. Must outlive this object.
    fixpp::session::Session* session_ptr{nullptr};
    int to_admin_call_count{0};
    seqnum_t next_inbound_at_first_to_admin{0};  // captured on 1st toAdmin call

    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const fixpp::session::SessionId& /*id*/) override {
        return {};
    }

    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const fixpp::session::SessionId& /*id*/) override {
        return {};
    }

    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
                 const fixpp::session::SessionId& /*id*/) override {
        ++to_admin_call_count;
        if (session_ptr != nullptr && to_admin_call_count == 1) {
            next_inbound_at_first_to_admin =
                session_ptr->seqnum_mgr_test_access().next_inbound_unsafe();
        }
    }
};

// ── Helper: make_logon_reset ─────────────────────────────────────────────────
// Build a Logon with 141=Y (ResetSeqNumFlag).
static std::vector<std::byte> make_logon_reset(std::string_view bs, std::uint32_t seq,
                                               std::string_view s, std::string_view t,
                                               int hbt = 30) {
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    extra += field(141, "Y");
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

// ── Helper: make_heartbeat_frame ─────────────────────────────────────────────
static std::vector<std::byte> make_heartbeat_frame(std::string_view bs, std::uint32_t seq,
                                                   std::string_view s, std::string_view t) {
    return make_fix_frame(bs, "0", seq, s, t);
}

// ── Helper: make_seq_reset_gapfill ──────────────────────────────────────────
// Build a SequenceReset(35=4) GapFill (123=Y) frame.
// seq = MsgSeqNum (the gap-start, in-sequence with what the peer expects)
// new_seqno = NewSeqNo(36): the jump target
static std::vector<std::byte> make_seq_reset_gapfill(std::string_view bs, std::uint32_t seq,
                                                     std::uint32_t new_seqno, std::string_view s,
                                                     std::string_view t) {
    std::string extra;
    extra += field(36, std::to_string(new_seqno));
    extra += field(123, "Y");
    return make_fix_frame(bs, "4", seq, s, t, extra);
}

// ── Helper: make_seq_reset_reset ─────────────────────────────────────────────
// Build a SequenceReset(35=4) Reset-mode (no 123=Y) frame.
static std::vector<std::byte> make_seq_reset_reset(std::string_view bs, std::uint32_t seq,
                                                   std::uint32_t new_seqno, std::string_view s,
                                                   std::string_view t) {
    std::string extra;
    extra += field(36, std::to_string(new_seqno));
    return make_fix_frame(bs, "4", seq, s, t, extra);
}

// ── W2 — Inbound_DurableTrack_AdminInclusive_Resumes6 ─────────────────────────
//
// Drive 5 accepted in-seq inbound messages (3 heartbeats + 2 app messages) through
// an established acceptor backed by a persistent (FaultStore) store. After delivery:
//   - store.durable_inbound must be 6 (Logon seq=1 + 5 accepted messages = next=7,
//     but durable tracks the persisted next value: after accepting seqs 1..6, next=7;
//     but we only send 5 messages at seqs 2..6 (after Logon seq=1), so manager=7).
//
// Wait — let's count precisely:
//   - Acceptor Logon exchange: peer Logon(34=1) → check_inbound(1) → in-seq → manager=2.
//     PERSIST at Logon site (after Active): durable → 2.
//   - Feed msg at seq=2 (HB): check_inbound(2) → manager=3, PERSIST → durable=3.
//   - Feed msg at seq=3 (HB): check_inbound(3) → manager=4, PERSIST → durable=4.
//   - Feed msg at seq=4 (app): check_inbound(4) → manager=5, PERSIST → durable=5.
//   - Feed msg at seq=5 (HB): check_inbound(5) → manager=6, PERSIST → durable=6.
//   - Feed msg at seq=6 (app): check_inbound(6) → manager=7, PERSIST → durable=7.
// So after 5 messages (seqs 2..6): manager.next_inbound=7, store.durable_inbound=7.
//
// The brief says "durable next_inbound==6" meaning we want 5 messages + the Logon = 6
// accepted = next_expected=7. But let's match the brief wording by driving 5 messages
// AFTER the Logon so manager.next_inbound==7 at the END (Logon=seq1 + 5 more = 6 accepted,
// next_expected=7). The brief says "resume 6, not 1" — checking that after restart
// the manager resumes at 7 (next-to-expect after 6 accepted).
//
// Actually the brief says "drive 5 accepted in-seq inbounds INCLUDING admin frames ... →
// durable next_inbound==6; restart → resume 6". So exactly 5 messages including Logon?
// No, Logon is the handshake. I'll drive 5 additional messages (seqs 2..6) so the
// manager ends at next_inbound=7. "Resume 6" means the restart session starts with 7?
// Re-reading: "5 accepted in-seq inbounds → durable next_inbound=6". So 5 messages
// total at seqs 1..5 (including Logon seq=1), manager ends at next_inbound=6. The
// restart session resumes at 6, not 1. This makes more sense.
//
// Interpretation: 5 messages total (Logon seq=1 + 4 more at seqs 2..5):
//   - Logon(1): manager→2, PERSIST durable=2
//   - HB(2): manager→3, PERSIST durable=3
//   - app(3): manager→4, PERSIST durable=4
//   - HB(4): manager→5, PERSIST durable=5
//   - app(5): manager→6, PERSIST durable=6
// → manager.next_inbound=6, store.durable_inbound=6. Restart → resume 6, not 1.
//
// Pre-T010/T011 RED: no persist → store.durable_inbound stays seeded value (1);
// the "restart" session re-seeds from 1 → manager starts at 1, not 6.
// Assertion "next_inbound==6 after restart" FAILS. ✓ RED.
TEST(PersistentSeqnumHydrate, Inbound_DurableTrack_AdminInclusive_Resumes6) {
    // Session 1: acceptor, persistent store seeded at {in=1, out=1}.
    auto factory1 = std::make_shared<FaultStoreFactory>(/*in=*/1, /*out=*/1);
    auto app1 = std::make_shared<CountingApp029>();

    // Build session 1 and reach Active via make_acceptor (Logon seq=1).
    auto fix1 = make_acceptor(factory1, /*peer_logon_seq=*/1, /*enable_789=*/false,
                              /*reset_on_logon=*/false, app1);
    ASSERT_EQ(fix1->session->state(), fixpp::session::fsm_state::Active)
        << "W2 precondition: session 1 must be Active";

    FaultStore* store1 = factory1->last_store;
    ASSERT_NE(store1, nullptr);

    // After Logon (seq=1) is accepted: post-T010, durable_inbound=2 (Logon persisted).
    // Pre-T010: durable_inbound=1 (no persist).

    // Drive 4 more in-seq messages: 2 heartbeats + 2 app messages (seqs 2..5).
    // Including admin frames is critical for RC-2: a heartbeat-only stream must persist.
    fix1->feed(make_heartbeat_frame("FIX.4.4", 2, "CLI", "SRV"));  // HB seq=2
    fix1->feed(make_fix_frame("FIX.4.4", "D", 3, "CLI", "SRV"));   // app seq=3
    fix1->feed(make_heartbeat_frame("FIX.4.4", 4, "CLI", "SRV"));  // HB seq=4
    fix1->feed(make_fix_frame("FIX.4.4", "D", 5, "CLI", "SRV"));   // app seq=5

    // Assert: 5 messages accepted (Logon + 4) → manager.next_inbound==6.
    ASSERT_EQ(fix1->session->seqnum_mgr_test_access().next_inbound_unsafe(),
              fixpp::session::seqnum_t{6})
        << "W2: after 5 accepted messages, manager.next_inbound must be 6";

    // Assert: durable counter tracks the persist (post-T010: durable=6).
    // Pre-T010 (RED): durable_inbound stays 1, assertion fails.
    EXPECT_EQ(store1->durable_inbound, fixpp::session::seqnum_t{6})
        << "W2: store.durable_inbound must equal 6 after 5 accepted in-seq messages "
           "(including admin frames); pre-T010 RED: no persist, stays at seeded value 1";

    // "Restart": simulate by creating a new acceptor session seeded from the
    // store's current durable counter. In a real restart the FileStore would
    // reload these values. Here we seed a new FaultStore with the current durable values.
    const seqnum_t durable_in_after_session1 = store1->durable_inbound;
    const seqnum_t durable_out_after_session1 = store1->current_outbound();

    // Session 2: fresh acceptor over the "reloaded" store. Resumption is asserted
    // END-TO-END (hydrate is lazy / first-counter-touch, D-1/D-6): the acceptor does
    // NOT mutate the manager at open() — it hydrates in the Logon handler. So we drive
    // the peer's continuation Logon at the resumed seq (6) and assert it is accepted
    // in-sequence (reaches Active, no too-high fatal), which proves next_inbound was
    // hydrated to 6 at the gate. Knob OFF so a too-high Logon would be fatal (pre-T011 RED).
    auto factory2 =
        std::make_shared<FaultStoreFactory>(durable_in_after_session1, durable_out_after_session1);
    auto fix2 = std::make_unique<Fixture>();
    fix2->cfg.role = fixpp::session::session_role::acceptor;
    fix2->cfg.sender_comp_id = "SRV";
    fix2->cfg.target_comp_id = "CLI";
    fix2->cfg.begin_string = "FIX.4.4";
    fix2->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix2->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix2->cfg.heartbeat_interval = std::chrono::seconds{30};
    fix2->cfg.executor_override = fix2->ioc.get_executor();
    fix2->cfg.store_factory = factory2;
    fix2->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix2->cfg.enable_next_expected_msg_seq_num = false;  // knob OFF: too-high is fatal
    fix2->cfg.transport_send = [&fix2 = *fix2](std::span<const std::byte> data) {
        fix2.capture(data);
    };
    fix2->session = std::make_unique<fixpp::session::Session>(fix2->eng, fix2->cfg);

    auto open_fut = asio::co_spawn(fix2->ioc, fix2->session->open(), asio::use_future);
    fix2->ioc.run_for(1s);
    fix2->ioc.restart();
    (void)open_fut.get();

    ASSERT_EQ(fix2->session->state(), fixpp::session::fsm_state::NotConnected)
        << "W2: acceptor must be NotConnected after open() (lazy hydrate)";

    // Feed the peer continuation Logon at the resumed seq (6).
    fix2->feed(make_logon("FIX.4.4", 6, "CLI", "SRV"));

    // Post-T011: inbound seed=6 applied at the Logon gate → check_inbound(6) in-seq → Active.
    // Pre-T011 (RED): no inbound seed → next_inbound=1 → check_inbound(6) too-high (knob OFF)
    //   → fatal, not Active. Assertion FAILS. ✓ RED.
    EXPECT_EQ(fix2->session->state(), fixpp::session::fsm_state::Active)
        << "W2 (SC-002): after restart, the acceptor resumes next_inbound=6 from the store, "
           "so peer Logon(34=6) is in-sequence and reaches Active; "
           "pre-T011 RED: no inbound seed, check_inbound(6) too-high → not Active";

    // The resumed inbound advanced past 6 (Logon seq=6 consumed): durable tracks it.
    EXPECT_EQ(fix2->session->seqnum_mgr_test_access().next_inbound_unsafe(),
              fixpp::session::seqnum_t{7})
        << "W2: after the resumed Logon(6) is accepted in-seq, next_inbound advances to 7";
}

// ── W3 — DeliverThenPersist_Ordering ─────────────────────────────────────────
//
// The durable inbound counter for message S must be updated AFTER the fromApp callback
// for S returns (INV-H2: deliver-then-persist). If the persist happened before delivery,
// the store's durable_inbound would already be S+1 when fromApp fires.
//
// Witness: a StorePeekApp observes store.durable_inbound INSIDE fromApp for message S.
// It must equal the PRE-PERSIST value (the seeded value before S's persist), NOT S+1.
//
// Setup: acceptor, persistent store seeded {in=1, out=1}. Reach Active (Logon seq=1).
// Then deliver app message at seq=2. Inside fromApp(seq=2):
//   - If persist has NOT happened yet (correct deliver-then-persist):
//     durable_inbound == 2 (the Logon persist set it to 2, message seq=2 is being
//     processed but its persist hasn't fired yet).
//   - If persist happened BEFORE delivery (incorrect store-before-deliver):
//     durable_inbound == 3 (already persisted to 3 when fromApp fires).
//
// The assertion: durable_at_fromapp == 2 (NOT 3) proves deliver-then-persist.
//
// Pre-T010 RED: no persist at all → durable_inbound stays at seeded value (1) throughout.
// At fromApp time for seq=2, durable_inbound==1 (never updated). Assertion "==2" FAILS. ✓ RED.
TEST(PersistentSeqnumHydrate, DeliverThenPersist_Ordering) {
    auto app = std::make_shared<StorePeekApp>();
    app->observe_at_call_n = 1;  // observe on the first fromApp call (app msg seq=2)

    auto factory = std::make_shared<FaultStoreFactory>(/*in=*/1, /*out=*/1);
    auto fix = make_acceptor(factory, /*peer_logon_seq=*/1, /*enable_789=*/false,
                             /*reset_on_logon=*/false, app);
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "W3 precondition: session must be Active";

    FaultStore* store = factory->last_store;
    ASSERT_NE(store, nullptr);

    // Wire the app to peek the store.
    app->store_ptr = store;

    // Deliver an app message at seq=2 (in-sequence after Logon at seq=1).
    fix->feed(make_fix_frame("FIX.4.4", "D", 2, "CLI", "SRV"));

    // Verify fromApp was called exactly once.
    ASSERT_EQ(app->fromapp_call_count, 1)
        << "W3: fromApp must be called exactly once for the app message";

    // Post-T010: Logon(seq=1) was persisted → durable_inbound=2.
    //            When fromApp fires for seq=2, the persist for seq=2 hasn't run yet
    //            → durable_at_fromapp must equal 2 (=pre-seq2-persist value).
    // Pre-T010 (RED): no persist → durable_inbound stays 1; assertion "==2" FAILS. ✓ RED.
    EXPECT_EQ(app->durable_at_fromapp, fixpp::session::seqnum_t{2})
        << "W3 (INV-H2/FR-002): inside fromApp for msg S, store.durable_inbound must equal S "
           "(pre-persist value, not S+1). post-T010: Logon persisted durable=2; seq=2 persist "
           "fires AFTER fromApp returns → durable_at_fromapp==2. "
           "pre-T010 RED: no persist, durable_inbound stays 1, got "
        << app->durable_at_fromapp;
}

// ── W4 — Acceptor_ColdResume_BothDirections ───────────────────────────────────
//
// Persistent store {in=37, out=42}. Acceptor cold open. Peer sends Logon(34=37).
// Post-T011: inbound seed applied → next_inbound=37 → check_inbound(37) in-seq →
//   reaches Active. Reply Logon samples next_outbound=42. No fatal.
// Post-T010: inbound persist fires → durable_inbound=38 after Logon accepted.
//
// Pre-T011 (RED): no inbound seed → next_inbound=1 → check_inbound(37) too-high →
//   AwaitingResend (not Active). Assertion "state==Active" FAILS. ✓ RED.
TEST(PersistentSeqnumHydrate, Acceptor_ColdResume_BothDirections) {
    auto factory = std::make_shared<FaultStoreFactory>(/*in=*/37, /*out=*/42);

    // Build session manually (make_acceptor uses peer_logon_seq=1 by default;
    // we need to send Logon at seq=37 AFTER open but BEFORE make_acceptor's Logon step).
    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::acceptor;
    fix->cfg.sender_comp_id = "SRV";
    fix->cfg.target_comp_id = "CLI";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{30};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = factory;
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.enable_next_expected_msg_seq_num = false;  // knob OFF: too-high is fatal
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();

    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::NotConnected)
        << "W4 precondition: acceptor must be in NotConnected after open()";

    // Feed peer Logon at seq=37 (the resumed inbound value).
    fix->feed(make_logon("FIX.4.4", 37, "CLI", "SRV"));

    // Post-T011: inbound seed=37 applied → check_inbound(37) in-seq → Active.
    // Pre-T011 (RED): no inbound seed → next_inbound=1 → check_inbound(37) too-high →
    //   not Active (stays NotConnected/AwaitingResend). FAILS. ✓ RED.
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "W4 (FR-009): acceptor cold-resume with {in=37,out=42} must reach Active "
           "when peer Logon(34=37) is in-sequence (inbound seed applied); "
           "pre-T011 RED: no inbound seed, check_inbound(37) too-high → not Active";

    // If Active, verify the reply Logon sampled the hydrated outbound (34=42).
    if (fix->session->state() == fixpp::session::fsm_state::Active) {
        ASSERT_GE(fix->capture.frames.size(), 1u) << "W4: at least one outbound frame expected";
        const std::string out_seq = extract_tag(fix->capture.frames[0], 34);
        EXPECT_EQ(out_seq, "42")
            << "W4: reply Logon must carry 34=42 (hydrated outbound from store)";
    }
}

// ── W6 — InboundPersistFailure_Fatal_LowerBound ──────────────────────────────
//
// Inject a persist WRITE failure (next_seqnum(inbound, true)) on an in-seq message.
// The session must transition to Disconnected (fatal per D-3/SC-006).
// The durable counter must stay at the LAST-PERSISTED lower bound (INV-H1/H2):
//   NOT "manager unchanged" — check_inbound advanced pre-delivery, so the in-flight
//   message may replay at-least-once; the durable counter is the safe restart point.
//
// Split (a): failure on the first inbound write after Logon (seq=2 message write fails).
// Split (b): failure on a LATER write (after 2 successful persists, 3rd write fails).
//
// Pre-T010 RED: no persist write ever fires → injected failure never triggers →
//   session stays Active (not Disconnected). Assertion "state==Disconnected" FAILS. ✓ RED.

// W6(a): first write after Logon acceptance fails → fatal.
// fail_on_nth_write=1: the FIRST inbound write (for Logon seq=1 acceptance) fails.
TEST(PersistentSeqnumHydrate, InboundPersistFailure_Fatal_LowerBound_FirstWrite) {
    // fail_on_nth_write=1: fail on the 1st inbound persist write (Logon at seq=1).
    auto factory = std::make_shared<FaultStoreFactory>(/*in=*/1, /*out=*/1,
                                                       /*fail_on_nth_call=*/0,
                                                       /*fail_on_nth_write=*/1);
    // Build acceptor manually (make_acceptor asserts Active; we expect Disconnected).
    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::acceptor;
    fix->cfg.sender_comp_id = "SRV";
    fix->cfg.target_comp_id = "CLI";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{30};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = factory;
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();

    // Feed peer Logon at seq=1. The accept path runs ensure_hydrated_ (2 reads) then
    // check_inbound(1) then the persist write (1st write). With fail_on_nth_write=1:
    // the persist write fires → fails → session must go Disconnected.
    fix->feed(make_logon("FIX.4.4", 1, "CLI", "SRV"));

    // Post-T010: persist fires for Logon → 1st write fails → Disconnected.
    // Pre-T010 (RED): no persist write fires → session reaches Active, not Disconnected. FAILS. ✓
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
        << "W6(a): first inbound persist write failure must disconnect the session (D-3/SC-006); "
           "pre-T010 RED: no persist, session reaches Active instead";

    FaultStore* store = factory->last_store;
    ASSERT_NE(store, nullptr);

    // Durable counter: after the failed write, durable_inbound stays at the
    // last-persisted lower bound (seeded=1, first write failed → still 1).
    // INV-H1: store.durable_inbound (1) <= manager.next_inbound (2 after check_inbound).
    EXPECT_EQ(store->durable_inbound, fixpp::session::seqnum_t{1})
        << "W6(a): after first-write failure, durable_inbound must stay at lower bound (1); "
           "check_inbound advanced in-memory but durable stays at last successful persist";
}

// W6(b): failure on a LATER write (after 2 successful persists).
// fail_on_nth_write=3: fail on the 3rd inbound write (2 succeed first).
// After 2 successful persists (Logon seq=1 + msg seq=2), durable=3.
// Then seq=3 arrives, check_inbound(3) in-seq, persist fires (3rd write) → fails → Disconnected.
// Durable stays at 3 (the 2nd successful persist value, NOT 4).
TEST(PersistentSeqnumHydrate, InboundPersistFailure_Fatal_LowerBound_LaterWrite) {
    auto factory = std::make_shared<FaultStoreFactory>(/*in=*/1, /*out=*/1,
                                                       /*fail_on_nth_call=*/0,
                                                       /*fail_on_nth_write=*/3);
    auto app = std::make_shared<CountingApp029>();

    // Use make_acceptor for the initial setup (reaches Active with 2 persists:
    // Logon-accept persist + ... wait: make_acceptor asserts Active which requires
    // the Logon to succeed. If persist-for-Logon is write #1 (succeeds since fail_on_3),
    // then make_acceptor would succeed. Let's verify the counting:
    //   ensure_hydrated_: 2 reads (call_count 1,2) — these are read calls, NOT writes
    //   Logon accept persist: write #1 (write_count=1, <3 → succeeds) → Active. ✓
    auto fix = make_acceptor(factory, /*peer_logon_seq=*/1, /*enable_789=*/false,
                             /*reset_on_logon=*/false, app);
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "W6(b) precondition: session must reach Active (first 2 writes succeed)";

    FaultStore* store = factory->last_store;
    ASSERT_NE(store, nullptr);

    // Feed msg at seq=2: check_inbound(2), persist write #2 (succeeds).
    fix->feed(make_fix_frame("FIX.4.4", "D", 2, "CLI", "SRV"));
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "W6(b): after seq=2 with 2nd write (succeeds), session must stay Active";
    EXPECT_EQ(store->durable_inbound, fixpp::session::seqnum_t{3})
        << "W6(b): after seq=2 persist (write#2), durable_inbound must be 3";

    // Feed msg at seq=3: check_inbound(3), persist write #3 (FAILS).
    fix->feed(make_fix_frame("FIX.4.4", "D", 3, "CLI", "SRV"));

    // Post-T010: 3rd write fails → Disconnected.
    // Pre-T010 (RED): no writes fire, session stays Active. FAILS. ✓
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
        << "W6(b): third inbound persist write failure must disconnect the session (D-3/SC-006); "
           "pre-T010 RED: no persist, session stays Active";

    // Durable stays at 3 (last successful persist), NOT 4.
    // INV-H1: durable (3) <= manager (4 after check_inbound(3) advanced).
    EXPECT_EQ(store->durable_inbound, fixpp::session::seqnum_t{3})
        << "W6(b): after 3rd-write failure, durable_inbound must remain at last lower bound (3); "
           "it must NOT have advanced to 4 (the failed write must not update durable)";
}

// ── W8-hb — Hydrate_HappensBefore_FirstCheckInbound_BothRoles ─────────────────
//
// Asserts that ensure_hydrated_() COMPLETES-BEFORE the first check_inbound on both
// roles. The assertion is on OBSERVABLE STATE, not call order, so a coincidental
// single-threaded ordering cannot pass it.
//
// Observable: the InboundHydrateObserverApp records next_inbound_unsafe() at the
// moment the FIRST toAdmin fires. For the acceptor, toAdmin fires for the REPLY Logon,
// which is built AFTER check_inbound() has advanced next_inbound by 1 (from the hydrated
// value to hydrated+1). So:
//   - If inbound seed was 37 (post-T011): next_inbound_unsafe() at toAdmin == 38.
//   - If inbound seed was NOT applied (pre-T011): starts at 1, check_inbound(37) too-high
//     → knob-on behind-side tolerance → next_inbound stays 37 (behind-side does NOT advance).
//     Actually pre-T011, peer Logon 34=37 is too-high; with knob ON, behind-side tolerates
//     but does NOT advance next_inbound. Reply Logon is built → toAdmin fires →
//     next_inbound_unsafe() == 1 (not 38, not 2).
//
// Wait — need to reconsider. For W8-hb the brief says "both roles". Let me design
// the acceptor arm: store {in=37, out=42}, acceptor, knob ON, peer Logon(34=37).
// Post-T011: ensure_hydrated_ applies in=37 → next_inbound=37 → check_inbound(37) in-seq
//   → next_inbound=38. Reply Logon built → toAdmin fires → observable=38.
// Pre-T011: ensure_hydrated_ does NOT apply inbound → next_inbound=1 →
//   check_inbound(37) too-high → behind-side tolerance → next_inbound stays 1.
//   Reply Logon built → toAdmin fires → observable=1.
// Assertion: observable == seeded_inbound + 1 (==38). Pre-T011: observable=1 ≠ 38. FAILS. ✓ RED.
//
// For the initiator arm: seeded {in=37, out=42}. Initiator sends Logon(34=42).
// Then we feed the peer Logon-ack(34=1) to the initiator. Post-T011 (before check_inbound):
//   ensure_hydrated_ applies in=37 → next_inbound=37. check_inbound_on_peer_logon(1)...
//   Actually on the LogonSent path, the initiator receives the peer's Logon-ack at seq=1.
//   check_inbound(1) is too-low if next_inbound is 37 → fatal.
//
// Hmm, the initiator arm is more complex. The W8-hb for initiator: seeded {in=1, out=42},
// initiator sends Logon(34=42). Peer Logon-ack arrives at seq=1. check_inbound(1) in-seq →
// next_inbound=2. toAdmin fires for the peer's logon-ack's... wait, toAdmin fires only for
// OUTBOUND frames. The initiator's toAdmin fires for the OUTBOUND Logon (before check_inbound
// on the ack). So:
// Post-T011: ensure_hydrated_ runs for initiator BEFORE outbound Logon → next_inbound=1 (seeded).
//   toAdmin fires for outbound Logon → next_inbound_unsafe()==1.
// The assertion "next_inbound_unsafe() == seeded_inbound" at toAdmin time for the initiator
// outbound Logon would be trivially true for any seeded_inbound value.
//
// For clean W8-hb for INITIATOR arm: use seeded_inbound=37. At toAdmin time for the outbound
// Logon (which fires BEFORE check_inbound on the peer ack), next_inbound_unsafe() should equal
// 37 (hydrated pre-T011 passes 1, post-T011 passes 37). Pre-T011: observable=1 ≠ 37. FAILS. ✓
//
// Anchors: New-4, [[feedback_single_threaded_harness_masks_strand_races]].
TEST(PersistentSeqnumHydrate, Hydrate_HappensBefore_FirstCheckInbound_Acceptor) {
    // Acceptor arm: seeded {in=37, out=42}, knob ON (behind-side tolerance for pre-T011).
    // Post-T011: peer Logon(34=37) in-seq → Active; toAdmin at 38.
    // Pre-T011: peer Logon(34=37) too-high → behind-side tolerance; toAdmin at 1.
    auto observer_app = std::make_shared<InboundHydrateObserverApp>();
    auto factory = std::make_shared<FaultStoreFactory>(/*in=*/37, /*out=*/42);

    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::acceptor;
    fix->cfg.sender_comp_id = "SRV";
    fix->cfg.target_comp_id = "CLI";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{30};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = factory;
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.enable_next_expected_msg_seq_num = true;  // behind-side tolerance
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->eng.application = observer_app;
    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
    observer_app->session_ptr = fix->session.get();

    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();

    // Feed peer Logon at seq=37.
    fix->feed(make_logon("FIX.4.4", 37, "CLI", "SRV"));

    // toAdmin must have fired for the reply Logon.
    ASSERT_GE(observer_app->to_admin_call_count, 1)
        << "W8-hb (acceptor): toAdmin must have fired for the reply Logon";

    // Post-T011: inbound seed=37 → check_inbound(37) in-seq → next_inbound=38 at toAdmin.
    // Pre-T011 (RED): no inbound seed → next_inbound=1 at toAdmin. Observable=1 ≠ 38. FAILS. ✓
    const seqnum_t seeded_inbound = 37;
    EXPECT_EQ(observer_app->next_inbound_at_first_to_admin, seeded_inbound + 1)
        << "W8-hb (acceptor, New-4): at toAdmin time for reply Logon, next_inbound_unsafe() "
           "must equal seeded_inbound+1="
        << (seeded_inbound + 1)
        << " (hydrate ran with in=37, check_inbound advanced to 38). "
           "pre-T011 RED: no inbound seed, observable="
        << observer_app->next_inbound_at_first_to_admin;
}

TEST(PersistentSeqnumHydrate, Hydrate_HappensBefore_FirstCheckInbound_Initiator) {
    // Initiator arm: seeded {in=37, out=42}.
    // ensure_hydrated_ runs inside emit_initiator_logon_() BEFORE the outbound Logon is sent.
    // toAdmin fires for the OUTBOUND Logon. At that point, next_inbound_unsafe() should
    // already equal the hydrated value (37, post-T011) not the construction value (1, pre-T011).
    auto observer_app = std::make_shared<InboundHydrateObserverApp>();
    auto factory = std::make_shared<FaultStoreFactory>(/*in=*/37, /*out=*/42);

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
    fix->eng.application = observer_app;
    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
    observer_app->session_ptr = fix->session.get();

    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(2s);
    fix->ioc.restart();
    (void)open_fut.get();

    // toAdmin must have fired for the outbound Logon.
    ASSERT_GE(observer_app->to_admin_call_count, 1)
        << "W8-hb (initiator): toAdmin must have fired for the outbound Logon";

    // Post-T011: hydrate ran with in=37 → next_inbound=37 at toAdmin (before any check_inbound).
    // Pre-T011 (RED): hydrate passes 1 for inbound → next_inbound=1 at toAdmin. 1 ≠ 37. FAILS. ✓
    const seqnum_t seeded_inbound = 37;
    EXPECT_EQ(observer_app->next_inbound_at_first_to_admin, seeded_inbound)
        << "W8-hb (initiator, New-4): at toAdmin time for outbound Logon, next_inbound_unsafe() "
           "must equal seeded_inbound="
        << seeded_inbound
        << " (hydrate completed-before the Logon was sent, so inbound already seeded). "
           "pre-T011 RED: hydrate passes 1 for inbound, observable=1 ≠ 37; got "
        << observer_app->next_inbound_at_first_to_admin;
}

// ── Phase 4a (T009) — RED witnesses: US2 recovery / precedence / 789 / 35=4 ──
//
// Anchors: spec.md SC-004, L-029-1; data-model.md W5/W9/W11/W12;
//          contracts/seqnum-hydrate.md C2.6/C2.7/C3.4; research.md RC-1/New-1/New-3/RC-B.

// ── W5 — PostGapFill_LowerBound_RecoveryPrecondition ─────────────────────────
//
// Drive a GapFill jump N→M (SequenceReset GapFill, validate_on) → assert INV-H1:
//   store.durable_inbound <= manager.next_inbound (jump NOT persisted).
// The GapFill jump sets manager to M; the durable counter stays at N (or lower).
//
// Then restart:
//   (i)  knob ON + peer Logon at seq=M: admitted via behind-side tolerance (knob on),
//        session recovers; no fatal. [SC-004, 789 on]
//   (ii) knob OFF + peer Logon at seq=M: too-high at Logon gate (:1615) → fatal →
//        Disconnected. [L-029-1 documented behavior]
//
// Pre-T010 RED for (i): no persist → durable=1 always; after GapFill jump to M, manager=M.
// INV-H1 assertion trivially holds. The restart with knob ON: seeded at 1 → peer Logon M
// is too-high (enable_789 on) → behind-side tolerance → reaches Active.
// This PASSES trivially pre-T010. So W5 needs a different observable.
//
// Strategy for RED: after GapFill, assert the durable counter stayed at a value LESS than M.
// Then seed restart session from durable (=1 pre-T010). Peer Logon at M with knob OFF
// (arm ii): pre-T010 durable=1, new session next_inbound=1, peer M is too-high → Disconnected.
// But with knob ON (arm i): next_inbound=1, peer M is too-high → behind-side → Active.
// So arm (i) with knob ON PASSES pre-T010 (accidentally green via different code path).
//
// The cleanest RED for W5: arm (ii) with knob OFF.
// After GapFill, restart with knob OFF + peer Logon at M:
//   Pre-T010: durable=1, new session→next_inbound=1, peer M too-high → Disconnected (too-high
//   fatal). Post-T010+T011: durable=N (last persisted before GapFill), new session→next_inbound=N,
//     peer M is too-high (M > N) → Disconnected (too-high fatal at Logon gate).
// Both fail the same way! So arm (ii) is also accidentally "same" pre and post T010.
//
// The INV-H1 assertion itself: "store.durable_inbound <= manager.next_inbound after GapFill."
// Pre-T010: after GapFill, manager=M, durable=1. 1 <= M: TRUE. INV-H1 holds trivially.
// Post-T010: after GapFill, manager=M (set_next_inbound, no persist), durable=N (pre-GapFill).
// N <= M (same durable holds — jump didn't persist).
// The INV-H1 assertion PASSES both ways.
//
// For a genuine RED pre-T010/T011, I need: "durable_inbound AFTER GapFill must equal the
// value at which we STOPPED persisting BEFORE the jump (i.e., the last-persisted value, N)."
// If persist ran before the GapFill: durable_inbound=N (the count at which check_inbound ran
// up to seq N, then GapFill set manager to M without persisting M).
// Pre-T010: durable_inbound=1 (never advanced). Assertion "durable==N" FAILs if N>1.
//
// Setup: reach Active (Logon seq=1 → manager=2, durable=2 post-T010).
// Feed normal messages at seq=2..N-1 to advance durable to N. Then feed GapFill N→M.
// Assert: durable==N (NOT M, INV-H1).
// Pre-T010 RED: durable still 1 (no persist) ≠ N. FAILS. ✓ RED.
TEST(PersistentSeqnumHydrate, PostGapFill_LowerBound_RecoveryPrecondition) {
    // Session 1: acceptor, {in=1, out=1}, validate_sequence_numbers=TRUE (default).
    auto factory1 = std::make_shared<FaultStoreFactory>(/*in=*/1, /*out=*/1);
    auto app1 = std::make_shared<CountingApp029>();
    auto fix1 = make_acceptor(factory1, /*peer_logon_seq=*/1, /*enable_789=*/false,
                              /*reset_on_logon=*/false, app1);
    ASSERT_EQ(fix1->session->state(), fixpp::session::fsm_state::Active)
        << "W5 precondition: session must be Active";

    FaultStore* store1 = factory1->last_store;
    ASSERT_NE(store1, nullptr);

    // Drive seq=2 (app message): manager→3, durable→3 post-T010.
    fix1->feed(make_fix_frame("FIX.4.4", "D", 2, "CLI", "SRV"));
    // Now manager.next_inbound=3 (post-T010: durable=3 too).

    // Feed GapFill seq=3 → NewSeqNo=10 (jump 3→10): manager set to 10 via set_next_inbound.
    // NOTE: GapFill is an AwaitingResend recovery tool; to use it in Active state without
    // entering AwaitingResend, we send it with GapFillFlag=Y at the expected seq=3.
    // check_inbound(3) advances manager from 3 to 4 (the +1 for the GapFill frame itself).
    // Then apply_inbound_sequence_reset(10) sets manager to 10 (absolute jump).
    // So final manager.next_inbound=10.
    fix1->feed(make_seq_reset_gapfill("FIX.4.4", /*seq=*/3, /*new_seqno=*/10, "CLI", "SRV"));

    const seqnum_t manager_after_gapfill =
        fix1->session->seqnum_mgr_test_access().next_inbound_unsafe();
    // Post-T010: manager jumped to 10 (via apply_inbound_sequence_reset).
    // The GapFill frame itself at seq=3 advanced check_inbound (3→4), then jump to 10.

    // INV-H1: store.durable_inbound <= manager.next_inbound.
    EXPECT_LE(store1->durable_inbound, manager_after_gapfill)
        << "W5 (INV-H1): store.durable_inbound must be <= manager.next_inbound after GapFill";

    // The critical assertion: durable stayed at N=3 (the last persisted before GapFill),
    // NOT jumped to M=10.
    // Pre-T010 (RED): durable_inbound stays 1 (never persisted), not 3. FAILS. ✓ RED.
    // Post-T010: Logon persist=2, seq=2 persist=3 → durable=3. GapFill frame seq=3
    //   check_inbound(3) advances → persist fires → durable=4 (the GapFill frame IS persisted
    //   since it advanced via check_inbound). Then jump to 10 not persisted. So durable=4.
    // Let me recalculate: seq=3 GapFill is processed via check_inbound(3) → in-seq → advances.
    // Then apply_inbound_sequence_reset(10). The GapFill FRAME itself persists (RC-B C3.4
    // case 1). The absolute jump does NOT persist (INV-H1). So durable after GapFill = 4.
    // manager = 10. INV-H1: 4 <= 10. ✓
    // Assertion: durable < manager (jump not persisted):
    const seqnum_t durable_after_gapfill = store1->durable_inbound;
    EXPECT_LT(durable_after_gapfill, manager_after_gapfill)
        << "W5 (INV-H1): after GapFill jump, durable_inbound (" << durable_after_gapfill
        << ") must be LESS than manager (" << manager_after_gapfill
        << ") — the jump was NOT persisted. "
           "pre-T010 RED: durable stays 1 (no persist), assertion durable<manager "
           "may trivially hold (1<10), but durable=1 is wrong value anyway";

    // The PRIMARY RED assertion: durable must have tracked ACTUAL persists (=4 post-T010),
    // not stayed at the seeded value (=1 pre-T010).
    // After: Logon(1)→durable=2, seq=2→durable=3, GapFill-seq=3-frame→durable=4 (C3.4 case 1).
    EXPECT_EQ(durable_after_gapfill, fixpp::session::seqnum_t{4})
        << "W5: after GapFill (seq=3 frame advanced, seq=3→10 jump not persisted), "
           "durable_inbound must be 4 (Logon:2, msg-seq2:3, gapfill-seq3-advance:4). "
           "pre-T010 RED: no persist, durable stays at 1, not 4";

    // ── Restart arm (ii): knob OFF + peer Logon at seq=10 → Disconnected (L-029-1) ──
    // After GapFill, store durable=4 (post-T010). A restarted session seeded from durable
    // has next_inbound=4. Peer Logon at seq=10 is too-high → fatal (knob OFF).
    // Pre-T011: seeded from durable=1 → next_inbound=1 → peer 10 still too-high → fatal.
    // Both arms Disconnect on too-high-without-knob; the distinction is the seeded value.
    // We assert Disconnected as the L-029-1 documented behavior.
    const seqnum_t durable_val = store1->durable_inbound;
    auto factory2 = std::make_shared<FaultStoreFactory>(durable_val, store1->current_outbound());
    auto fix2 = std::make_unique<Fixture>();
    fix2->cfg.role = fixpp::session::session_role::acceptor;
    fix2->cfg.sender_comp_id = "SRV";
    fix2->cfg.target_comp_id = "CLI";
    fix2->cfg.begin_string = "FIX.4.4";
    fix2->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix2->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix2->cfg.heartbeat_interval = std::chrono::seconds{30};
    fix2->cfg.executor_override = fix2->ioc.get_executor();
    fix2->cfg.store_factory = factory2;
    fix2->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix2->cfg.enable_next_expected_msg_seq_num = false;  // knob OFF: too-high at Logon gate
    fix2->cfg.transport_send = [&fix2 = *fix2](std::span<const std::byte> data) {
        fix2.capture(data);
    };
    fix2->session = std::make_unique<fixpp::session::Session>(fix2->eng, fix2->cfg);

    auto open2_fut = asio::co_spawn(fix2->ioc, fix2->session->open(), asio::use_future);
    fix2->ioc.run_for(1s);
    fix2->ioc.restart();
    (void)open2_fut.get();

    // Feed peer Logon at seq=10 (post-GapFill manager value):
    fix2->feed(make_logon("FIX.4.4", 10, "CLI", "SRV"));

    // L-029-1: knob-OFF + too-high peer Logon → Disconnected (fatal at Logon gate).
    EXPECT_EQ(fix2->session->state(), fixpp::session::fsm_state::Disconnected)
        << "W5 (L-029-1): knob-OFF + too-high peer Logon after GapFill restart must "
           "Disconnect (fatal at Logon gate, :1615). Do NOT call this 'recovers via "
           "ResendRequest' — the Logon gate has no ResendRequest arm.";
}

// ── W9b — Acceptor_ResetLogon_InboundSeedWithheld_NoTooLowFatal ──────────────
//
// Acceptor cold-hydrated {in=37, out=42}. Peer Logon(34=1, 141=Y) (reset-Logon).
// reset_on_logon=false.
//
// Post-T011: the inbound seed is WITHHELD when 141=Y detected → next_inbound stays 1
//   → check_inbound(1) in-seq → Active. The received-141 reset wins. No too-low fatal.
// Pre-T011: no inbound seed at all → next_inbound=1 → check_inbound(1) in-seq → Active.
//   No fatal (passes trivially for wrong reason).
//
// The test is GREEN both pre-T011 and post-T011 (the too-low fatal only manifests when T011
// introduces the inbound seed WITHOUT the withhold guard). This is a regression witness:
// if T011 is implemented WITHOUT the withhold, this test catches the too-low fatal.
//
// To make this witness observable as RED pre-T011, we also assert:
//   - The outbound was hydrated (reply Logon carries 34=42, T007 already done → PASSES).
//   - store.call_count == 2 (both counters read during hydrate, T007 → PASSES).
//   - The session reaches Active AND outbound 34=1 after reset (reset wins over hydrate).
//
// So W9b is a correctness/regression test, not a natural RED pre-T011 test.
// The critical failure it detects: T011 without withhold → too-low fatal → Disconnected.
// We assert Active as the positive case; a buggy T011 would assert Disconnected.
//
// Note: make_acceptor already sends peer Logon(34=1) without 141=Y. We need to send
// 141=Y, so we build manually.
TEST(PersistentSeqnumHydrate, Acceptor_ResetLogon_InboundSeedWithheld_NoTooLowFatal) {
    auto factory = std::make_shared<FaultStoreFactory>(/*in=*/37, /*out=*/42);

    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::acceptor;
    fix->cfg.sender_comp_id = "SRV";
    fix->cfg.target_comp_id = "CLI";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{30};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = factory;
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.reset_on_logon =
        false;  // RC-1: the withhold is triggered by 141=Y, not reset_on_logon
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();

    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::NotConnected)
        << "W9b precondition: acceptor must be in NotConnected after open()";

    // Feed peer Logon(34=1, 141=Y): reset-Logon, inbound seed must be withheld.
    fix->feed(make_logon_reset("FIX.4.4", 1, "CLI", "SRV"));

    // Post-T011 (with withhold): inbound seed withheld → check_inbound(1) in-seq → Active.
    // T011 WITHOUT withhold: inbound=37 applied → check_inbound(1) too-low → Disconnected.
    // Pre-T011 (no inbound seed): next_inbound=1 → check_inbound(1) in-seq → Active (trivial).
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "W9b (RC-1): acceptor with reset-Logon(141=Y) + store{in=37} must NOT fatal as "
           "too-low at :1615. Inbound seed MUST be withheld so check_inbound(1) is in-seq. "
           "If T011 applies inbound seed WITHOUT withheld guard → too-low fatal → Disconnected.";

    // The outbound must have been hydrated (34=42 in reply Logon), then RESET to 1 by 141=Y.
    // After the 141=Y reset, outbound is reset to 1, so the reply Logon carries 34=1
    // (the reset_seqnums_to_one_durable path runs before the reply Logon for knob-driven reset).
    // Actually: bilateral_lenient mirrors 141=Y → reply Logon also has 141=Y and 34=1.
    // Check that session reached Active (the main assertion) and verify the reset ran.
    if (fix->session->state() == fixpp::session::fsm_state::Active) {
        // After the reset-Logon and reply, manager.next_inbound is 2: the 141=Y received
        // reset runs AFTER check_inbound, then 030 restores the consumed seq-1 reset Logon's
        // advance — the advance SURVIVES the reset (QuickFIX reset-then-increment parity).
        const seqnum_t ni = fix->session->seqnum_mgr_test_access().next_inbound_unsafe();
        EXPECT_EQ(ni, fixpp::session::seqnum_t{2})
            << "W9b: after 141=Y reset-Logon, next_inbound must be 2 (030 restores the "
               "consumed seq-1 reset Logon's advance; it survives the reset over hydrate)";
    }

    // gate-b/r1 extension: assert INV-H1 (store.durable_inbound <= manager.next_inbound)
    // UNCONDITIONALLY — the proxy gap that let the 029 over-persist slip.
    // 029 over-persist (the bug this witness was born to catch): reset set store=1, then an
    //   UNCONDITIONAL persist pushed store→2 while manager stayed at 1 — store(2) > manager(1),
    //   a +1 with NO surviving in-memory advance. INV-H1 violated.
    // 030: the consumed seq-1 reset Logon IS a surviving net-advance, so the dedicated 030
    //   arm-local persist takes BOTH manager and store to 2 → store(2) == manager(2). This is
    //   equality, NOT over-persist (there is a surviving advance backing it). INV-H1 holds.
    // [[feedback_witness_asserts_named_postcondition_not_proxy]]
    // [[feedback_unconditional_persist_at_multiexit_gate_breaks_lowerbound]]
    // [029 INV-H1; triage root-cause #1; data-model §Persist matrix; 030 FR-005]
    {
        FaultStore* store = factory->last_store;
        ASSERT_NE(store, nullptr) << "W9b-ext: store must have been minted";
        const seqnum_t manager_ni = fix->session->seqnum_mgr_test_access().next_inbound_unsafe();
        EXPECT_LE(store->durable_inbound, manager_ni)
            << "W9b-ext (INV-H1): store.durable_inbound must be <= manager.next_inbound "
               "after received-141=Y reset. 029 over-persist bug: store=2 > manager=1. "
               "030: store=2 == manager=2 (equality, surviving advance). "
               "store.durable_inbound="
            << store->durable_inbound << " manager.next_inbound=" << manager_ni;
        // Stronger: both must equal 2 after the 030 restore (not just LE, but exact equality
        // since the consumed seq-1 reset Logon's advance survives and is persisted to 2).
        EXPECT_EQ(store->durable_inbound, fixpp::session::seqnum_t{2})
            << "W9b-ext (INV-H1 / 030 FR-005): store.durable_inbound must be exactly 2 after "
               "141=Y reset + 030 restore-to-2. 029 over-persist RED: durable_inbound=2 with "
               "manager=1 (no surviving advance). 030 GREEN: durable_inbound=2 == manager=2 "
               "(consumed seq-1 reset Logon survives, persisted to 2). [030 FR-005]";
    }
}

// ── W11 — Hydrated_Initiator_Advertises789 ────────────────────────────────────
//
// Persisted {in=37, out=42} + enable_next_expected_msg_seq_num=true:
//   The initiator Logon must carry:
//     (a) 789=37 (hydrated next_inbound, NOT 1 — the unhydrated default).
//     (b) NO 141=Y (seqnums_at_one=false on a resumed session — no reset).
//
// Pre-T011 (RED): inbound seed NOT applied → next_inbound=1 → 789=1 (not 37).
//   Assertion "789==37" FAILS. ✓ RED.
//
// Anchor: New-3, C2.7, contracts/seqnum-hydrate.md C2.7.
TEST(PersistentSeqnumHydrate, Hydrated_Initiator_Advertises789) {
    auto factory = std::make_shared<FaultStoreFactory>(/*in=*/37, /*out=*/42);
    // enable_789=true so the initiator Logon includes tag 789.
    auto fix = make_initiator(factory, /*enable_789=*/true, /*reset_on_logon=*/false);

    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::LogonSent)
        << "W11 precondition: initiator must be in LogonSent after open()";
    ASSERT_EQ(fix->capture.frames.size(), 1u) << "W11: exactly one outbound frame (Logon)";

    const auto& logon_frame = fix->capture.frames[0];

    // (a) 789 must equal the hydrated next_inbound (37, not 1).
    // Pre-T011 (RED): next_inbound=1 → 789=1 ≠ 37. FAILS. ✓ RED.
    const std::string tag789 = extract_tag(logon_frame, 789);
    EXPECT_EQ(tag789, "37")
        << "W11 (New-3/C2.7): initiator Logon must carry 789=37 (hydrated next_inbound); "
           "pre-T011 RED: no inbound seed, 789=1 (construction value), got 789="
        << tag789;

    // (b) 141=Y must NOT be present (no reset on a resumed session, seqnums_at_one=false).
    // A resumed session does NOT emit 141=Y — the reset-Logon path is only triggered by
    // reset_on_logon or reset_seqnum_policy (both false/bilateral_lenient here).
    const std::string tag141 = extract_tag(logon_frame, 141);
    EXPECT_TRUE(tag141.empty() || tag141 != "Y")
        << "W11 (C2.7): resumed initiator Logon must NOT carry 141=Y (no spurious reset); "
           "got 141="
        << tag141;
}

// ── W12 — ValidateOff_35eq4_PersistSplit ────────────────────────────────────
//
// Tests the 35=4 three-way persist split (RC-B/C3.4) with validate_sequence_numbers=false:
//
// Case 1 (PERSIST): exact-match GapFill (123=Y, seq=expected, validate_off).
//   Path: check_inbound(seq) passes → +1 advance → S7 arm dispatches fromAdmin,
//   early co_returns WITHOUT apply_inbound_sequence_reset → persist fires (RC-B C3.4 case 1).
//   Assert: store.durable_inbound == pre-gapfill-durable + 1.
//
// Case 2 (NO-PERSIST): Reset-mode 35=4 (no 123=Y, validate_off).
//   Path: runs BEFORE check_inbound (S6 arm), deliver-without-advance →
//   NO persist (no advance). Assert: store.durable_inbound == case2_pre_value (unchanged).
//
// Pre-T010 RED: no persist → durable_inbound stays 1 throughout both cases.
//   Case 1 assertion "durable == pre_durable + 1" FAILs (1 ≠ 2 after Logon persist).
//   Wait — pre-T010: even the Logon persist doesn't fire, so durable=1.
//   After Logon + case1 GapFill: durable=1. Assertion "durable == some expected value > 1" FAILS.
//
// Setup: acceptor, validate_sequence_numbers=false (028 knob).
//   Logon seq=1 → accepted.
//   Case 1: GapFill(seq=2, 123=Y, NewSeqNo=5) — exact-match (seq=2 is expected).
//     Pre-T010: durable stays 1.
//     Post-T010: Logon persist→durable=2, GapFill-seq2 exact-match advance+persist→durable=3.
//   Case 2: Reset-mode 35=4 (no 123=Y, seq=any) — comes BEFORE check_inbound.
//     Pre-T010: durable stays 1.
//     Post-T010: durable stays 3 (no advance → no persist).
//
// Note: with validate_sequence_numbers=false, reset-mode 35=4 bypasses apply_inbound_sequence_reset
// (S6 path at line 2137) AND the GapFill exact-match bypasses apply_inbound_sequence_reset too
// (S7 path at line 2461). The exact-match GapFill DOES advance via check_inbound first.
TEST(PersistentSeqnumHydrate, ValidateOff_35eq4_PersistSplit) {
    // Build acceptor with validate_sequence_numbers=false.
    auto factory = std::make_shared<FaultStoreFactory>(/*in=*/1, /*out=*/1);
    auto app = std::make_shared<CountingApp029>();

    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::acceptor;
    fix->cfg.sender_comp_id = "SRV";
    fix->cfg.target_comp_id = "CLI";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{30};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = factory;
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.validate_sequence_numbers = false;  // 028 knob
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->eng.application = app;
    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();

    // Feed peer Logon at seq=1. With validate_off, Logon is accepted.
    fix->feed(make_logon("FIX.4.4", 1, "CLI", "SRV"));
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "W12 precondition: session must reach Active";

    FaultStore* store = factory->last_store;
    ASSERT_NE(store, nullptr);

    // Post-T010: Logon accepted → persist fires → durable=2.
    // Pre-T010: durable stays 1.
    const seqnum_t durable_after_logon = store->durable_inbound;

    // ── Case 1: exact-match GapFill (123=Y, seq=2, expected=2 after Logon).
    // With validate_off, the GapFill at seq=2 (exact match) goes through:
    //   check_inbound(2) → in-seq → +1 advance → S7 exact-match GapFill path →
    //   dispatches fromAdmin → early co_return (no apply_inbound_sequence_reset) →
    //   PERSIST fires (C3.4 case 1 / RC-B).
    const seqnum_t durable_before_gapfill = durable_after_logon;
    fix->feed(make_seq_reset_gapfill("FIX.4.4", /*seq=*/2, /*new_seqno=*/10, "CLI", "SRV"));

    // Post-T010: GapFill advance (seq=2→3) persisted → durable = durable_before_gapfill + 1.
    // Pre-T010 (RED): durable stays at seeded value (1). durable_before_gapfill=1.
    //   Assertion "durable == 1+1 = 2" FAILs because durable remains 1. ✓ RED.
    EXPECT_EQ(store->durable_inbound, durable_before_gapfill + 1u)
        << "W12 case1 (RC-B/C3.4): validate-off exact-match GapFill(35=4,123=Y) MUST persist "
           "the +1 advance (the GapFill frame went through check_inbound). "
           "Expected durable="
        << (durable_before_gapfill + 1u) << " but got " << store->durable_inbound
        << ". pre-T010 RED: no persist, durable stays at seeded value.";

    // ── Case 2: Reset-mode 35=4 (no 123=Y), validate_off.
    // S6 path: runs BEFORE check_inbound → deliver-without-advance → NO-PERSIST.
    // The reset-mode 35=4 with validate_off bypasses apply_inbound_sequence_reset at line 2137.
    // It is delivered to fromAdmin but the counter is NOT advanced → no persist.
    const seqnum_t durable_before_reset = store->durable_inbound;
    // Manager is currently at 3 (after Logon=1→2, GapFill-seq2→3).
    // Send a reset-mode 35=4 at seq=99 (out-of-order, doesn't matter for reset mode).
    fix->feed(make_seq_reset_reset("FIX.4.4", /*seq=*/99, /*new_seqno=*/20, "CLI", "SRV"));

    // Post-T010: reset-mode 35=4 NO advance → NO persist → durable unchanged.
    // Pre-T010: durable stays 1 anyway (no persist).
    // The assertion holds in both cases — but combined with case1, the full test RED pre-T010. ✓
    EXPECT_EQ(store->durable_inbound, durable_before_reset)
        << "W12 case2 (RC-B/C3.4): validate-off Reset-mode 35=4 must NOT persist "
           "(no check_inbound advance). durable must stay at "
        << durable_before_reset << " but got " << store->durable_inbound;

    // Combined RED summary:
    //   pre-T010: case1 FAILS (durable 1 ≠ 2), case2 passes trivially.
    //   post-T010: both cases correct.
}

// ── Phase 5 (T012) — US3: non-persistent byte-identity + custom discriminator + no-heap ──
//
// Anchors: spec.md SC-003, FR-005; data-model.md W7/W13/NoHeap/INV-H4;
//          contracts/seqnum-hydrate.md C2.2/C3.5; research.md D-10;
//          [[feedback_tracking_pmr_resource_false_pass]].

// ── W7 — NonPersistent_NoOp_MemoryAndNull ─────────────────────────────────────
//
// With a MEMORY store (via NonPersistentFaultStoreFactory with yields_persistent_store()==false),
// assert:
//   (a) counters start at 1 (store is NOT read — hydrate skipped).
//   (b) no read issued: call_count == 0 (ensure_hydrated_ skipped the FaultStore's reads).
//   (c) the initiator Logon carries 34=1 (byte-identical to the pre-feature baseline).
//
// With a NULL store (no store_factory, store_is_persistent_ stays false):
//   (a) counters start at 1.
//   (b) no crash / no attempted store read.
//   (c) the initiator Logon carries 34=1.
//
// The custom NonPersistentFaultStoreFactory seeds {in=99, out=42}. If a read were
// issued (incorrectly), the initiator Logon would carry 34=42, not 34=1. The call_count
// being 0 provides a second independent proof that no read fired.
//
// Pre-T004/T005 RED:
//   - If the discriminator is not wired: yields_persistent_store() defaults to true (base
//     class) and the FaultStore IS read → call_count==2, Logon carries 34=42, fails "==0"/"==1".
//   Since T004/T005 are already implemented (Phases 1-2), this test should be GREEN.
//   It is a regression witness: a mistaken revert of the non-persistent skip would make it RED.
//
// Anchor: SC-003, INV-H4, D-10.
TEST(PersistentSeqnumHydrate, NonPersistent_NoOp_MemoryAndNull) {
    // ── Arm 1: custom non-persistent factory (NonPersistentFaultStoreFactory) ─
    // Seeded {in=99, out=42} — if the read fires, Logon would carry 34=42, not 34=1.
    // The call_count gives a second observable: 0 means the read was correctly skipped.
    {
        auto factory = std::make_shared<NonPersistentFaultStoreFactory>(
            /*seeded_inbound=*/99, /*seeded_outbound=*/42);

        // Build initiator. The initiator Logon is emitted at open().
        auto fix = make_initiator(factory);
        ASSERT_EQ(fix->capture.frames.size(), 1u)
            << "W7 (arm1): expected exactly one outbound frame (Logon)";

        const auto& logon_frame = fix->capture.frames[0];

        // (a) Logon carries 34=1 (counters at construction default — no read, no hydrate).
        const std::string seq_str = extract_tag(logon_frame, 34);
        ASSERT_FALSE(seq_str.empty()) << "W7 (arm1): Logon frame missing tag 34";
        EXPECT_EQ(std::stoi(seq_str), 1)
            << "W7 (SC-003/INV-H4): non-persistent store must NOT hydrate; "
               "counters must start at 1. Logon must carry 34=1 (not 34=42 from seeded store)";

        FaultStore* store = factory->last_store;
        ASSERT_NE(store, nullptr);

        // (b) No read issued — call_count must be 0 (ensure_hydrated_ skipped entirely).
        // If the non-persistent skip is missing, the store IS read (2 reads → call_count==2).
        EXPECT_EQ(store->call_count, 0)
            << "W7 (SC-003/INV-H4/D-10): non-persistent custom factory must NOT trigger "
               "any store read (ensure_hydrated_ skip); call_count="
            << store->call_count << " (expected 0). A missed override causes call_count==2.";

        // (c) Manager counters: next_inbound==1 (not seeded 99), next_outbound==2 (after Logon).
        const seqnum_t ni = fix->session->seqnum_mgr_test_access().next_inbound_unsafe();
        EXPECT_EQ(ni, seqnum_t{1})
            << "W7 (INV-H4): next_inbound must start at 1 (not seeded 99, no hydrate)";
        // next_outbound is 2 after emitting Logon at seq=1 (normal advance).
        const seqnum_t no = fix->session->seqnum_mgr_test_access().next_outbound_unsafe();
        EXPECT_EQ(no, seqnum_t{2})
            << "W7 (INV-H4): next_outbound must be 2 after emitting Logon(34=1)";
    }

    // ── Arm 2: null store (no store_factory at all) ──────────────────────────
    // store_is_persistent_ stays false (default). No store calls are possible.
    // We verify: counters==1, Logon carries 34=1, session reaches LogonSent.
    {
        // make_initiator with nullptr store_factory (no store).
        auto fix = make_initiator(/*store_factory=*/nullptr);
        ASSERT_EQ(fix->capture.frames.size(), 1u)
            << "W7 (arm2 null): expected exactly one outbound frame (Logon)";

        const auto& logon_frame = fix->capture.frames[0];
        const std::string seq_str = extract_tag(logon_frame, 34);
        ASSERT_FALSE(seq_str.empty()) << "W7 (arm2 null): Logon frame missing tag 34";
        EXPECT_EQ(std::stoi(seq_str), 1)
            << "W7 (SC-003): null store (no store_factory) must produce Logon(34=1) — "
               "store_is_persistent_==false, no read, no hydrate";

        // Manager counters.
        const seqnum_t ni = fix->session->seqnum_mgr_test_access().next_inbound_unsafe();
        EXPECT_EQ(ni, seqnum_t{1}) << "W7 (arm2 null): next_inbound must be 1 (no store)";
        const seqnum_t no = fix->session->seqnum_mgr_test_access().next_outbound_unsafe();
        EXPECT_EQ(no, seqnum_t{2}) << "W7 (arm2 null): next_outbound must be 2 after Logon";
    }
}

// ── W13 — CustomStore_Discriminator ──────────────────────────────────────────
//
// Tests the discriminator for CUSTOM stores beyond the two built-ins:
//
//   (a) A CUSTOM PERSISTENT factory (FaultStoreFactory, yields_persistent_store()==true):
//       → discriminated persistent → ensure_hydrated_ reads the store (call_count==2).
//       → counters resume from the seeded values (next_outbound==43 after Logon at 42).
//
//   (b) A CUSTOM NON-PERSISTENT factory (NonPersistentFaultStoreFactory,
//       yields_persistent_store()==false):
//       → discriminated non-persistent → ensure_hydrated_ skips → no read (call_count==0).
//       → counters start at 1 (seeded values ignored, Logon carries 34=1 not 34=42).
//
// A missed override in a custom factory would default to 'true' (the base class default),
// causing arm (b) to incorrectly hydrate from the store. This test catches that.
//
// Anchor: RC-A, New-B, data-model.md W13, contracts C2.2, research D-10.
TEST(PersistentSeqnumHydrate, CustomStore_Discriminator) {
    // ── Arm (a): custom persistent factory — hydrate runs ────────────────────
    // FaultStoreFactory overrides yields_persistent_store() → true.
    // Seeded {in=1, out=42}: the initiator Logon must carry 34=42 (hydrated).
    {
        auto factory = std::make_shared<FaultStoreFactory>(/*in=*/1, /*out=*/42);
        auto fix = make_initiator(factory);

        ASSERT_EQ(fix->capture.frames.size(), 1u)
            << "W13 (arm-a): expected one outbound frame (Logon)";
        const auto& logon_frame = fix->capture.frames[0];

        // Verify hydrate ran: Logon carries 34=42 (seeded outbound).
        const std::string seq_str = extract_tag(logon_frame, 34);
        EXPECT_EQ(std::stoi(seq_str), 42)
            << "W13 (RC-A): custom persistent factory must cause hydrate to run; "
               "Logon must carry 34=42 (seeded outbound); got 34="
            << seq_str;

        FaultStore* store = factory->last_store;
        ASSERT_NE(store, nullptr);

        // Hydrate issues 2 reads (inbound + outbound).
        EXPECT_EQ(store->call_count, 2)
            << "W13 (RC-A): custom persistent factory → 2 reads from ensure_hydrated_; "
               "call_count="
            << store->call_count;
    }

    // ── Arm (b): custom non-persistent factory — hydrate skipped ─────────────
    // NonPersistentFaultStoreFactory overrides yields_persistent_store() → false.
    // Seeded {in=99, out=42}: if the read fires, Logon carries 34=42; correct is 34=1.
    {
        auto factory = std::make_shared<NonPersistentFaultStoreFactory>(
            /*seeded_inbound=*/99, /*seeded_outbound=*/42);
        auto fix = make_initiator(factory);

        ASSERT_EQ(fix->capture.frames.size(), 1u)
            << "W13 (arm-b): expected one outbound frame (Logon)";
        const auto& logon_frame = fix->capture.frames[0];

        // Verify hydrate was SKIPPED: Logon carries 34=1 (not seeded 42).
        const std::string seq_str = extract_tag(logon_frame, 34);
        EXPECT_EQ(std::stoi(seq_str), 1)
            << "W13 (New-B): custom non-persistent factory must skip hydrate; "
               "Logon must carry 34=1 (construction default, not seeded 42); "
               "got 34="
            << seq_str;

        FaultStore* store = factory->last_store;
        ASSERT_NE(store, nullptr);

        // No reads issued — call_count must be 0.
        EXPECT_EQ(store->call_count, 0)
            << "W13 (New-B): custom non-persistent factory → 0 reads (ensure_hydrated_ skip); "
               "call_count="
            << store->call_count
            << ". A missed override (defaulting to true) causes call_count==2.";
    }
}

// ── NoHeap — NoHeap_HydrateAndPersistPaths ───────────────────────────────────
//
// Witnesses that the cold-open hydrate path (ensure_hydrated_) and the in-seq
// persist path (persist_inbound_advance_) add ZERO global-heap allocation.
//
// BINDING gate: the mallocnesia LD_PRELOAD interceptor
//   tools/mallocnesia/libmallocnesia.so, wired in CMakeLists.txt as the
//   session_persistent_seqnum_hydrate_mallocnesia ctest companion.
//   [[feedback_tracking_pmr_resource_false_pass]]: a PMR counting_resource
//   alone is a false-pass (non-PMR std::vector/global-new escapes it);
//   the LD_PRELOAD is the binding proof.
//
// Strategy: DELTA measurement — measure open + one inbound deliver+persist
// AFTER a warm-up that primes all per-thread caches (asio recycler,
// async_mutex slot pool). The FaultStore's next_seqnum returns a ready-value
// (no internal container, no suspension allocation) so the store read+write
// path itself is zero-alloc.
//
// Under mallocnesia: alloc_guard_count() returns the intercepted malloc count.
// Without LD_PRELOAD: alloc_guard_count() is a no-op returning 0 (the test
// still asserts the functional post-conditions; the no-heap claim is proven
// only by the _mallocnesia companion ctest).
//
// Anchors: [const §VIII.5], data-model.md NoHeap/INV-H4, tasks.md T012.
TEST(PersistentSeqnumHydrate, NoHeap_HydrateAndPersistPaths) {
    // ── Setup OUTSIDE the guarded window ─────────────────────────────────────
    // Build a persistent-store acceptor session and reach Active. All one-time
    // allocations (Session ctor, coroutine frames, per-thread recycler init)
    // happen during setup, outside the alloc guard.
    //
    // Warm-up: prime the asio per-thread recycler by running the SAME path
    // (open + inbound deliver + persist) several times before the guard window.
    // The first iteration touches per-thread lazy-init paths (cancellation_slot's
    // thread_info_base, promise frame recycling); subsequent iterations are
    // steady-state zero-alloc (mirrors validation_compat_toggles NoHeap_RelaxedDeliverPath).

    // Session setup: persistent FaultStore seeded {in=1, out=1}.
    // We use an acceptor so both the hydrate path (in the Logon handler) and
    // the persist path (persist_inbound_advance_ for each accepted message) are exercised.
    auto app = std::make_shared<CountingApp029>();

    // Strategy: (1) Build the active session (includes open + hydrate + Logon accept persist).
    //           (2) Warm-up: deliver kWarmup heartbeats (triggers persist_inbound_advance_).
    //           (3) Open the guard window.
    //           (4) Deliver ONE more heartbeat inside the window.
    //           (5) Close the window + assert heap_allocs==0.
    //
    // The hydrate path (ensure_hydrated_) runs ONCE at session open (step 1). To witness
    // it is zero-alloc in the guarded window, we measure the WARM persist path (step 4)
    // which traverses the same async_mutex + co_await surface as the hydrate path
    // (both go through SeqnumManager methods under async_mutex). The hydrate path itself
    // cannot be placed in the guard window without building a second fresh session inside
    // the guard (which would itself allocate). The persist path is the correct proxy for
    // the "allocated by our feature" claim — it is the only ADDED hot-path code on the
    // inbound receive path.
    auto factory = std::make_shared<FaultStoreFactory>(/*in=*/1, /*out=*/1);
    auto fix = make_acceptor(factory, /*peer_logon_seq=*/1, /*enable_789=*/false,
                             /*reset_on_logon=*/false, app);
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "NoHeap precondition: session must be Active";

    FaultStore* store = factory->last_store;
    ASSERT_NE(store, nullptr);

    // Build the warm-up heartbeat frame OUTSIDE the guard window.
    // We drive seqs 2, 3, ..., 2+kWarmup-1 during warm-up.
    constexpr int kWarmup = 8;
    for (int i = 0; i < kWarmup; ++i) {
        const seqnum_t seq = fix->session->seqnum_mgr_test_access().next_inbound_unsafe();
        fix->feed(make_heartbeat_frame("FIX.4.4", static_cast<std::uint32_t>(seq), "CLI", "SRV"));
        ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
            << "NoHeap warm-up: session must stay Active at i=" << i;
    }

    // Snapshot the from_admin count to verify the guarded deliver fires.
    const int from_admin_before = app->from_admin_count;
    const int write_count_before = store->write_count;

    // Build the measured heartbeat frame OUTSIDE the guard window.
    const seqnum_t measured_seq = fix->session->seqnum_mgr_test_access().next_inbound_unsafe();
    const auto measured_frame =
        make_heartbeat_frame("FIX.4.4", static_cast<std::uint32_t>(measured_seq), "CLI", "SRV");

    // ── Guarded window: one persist_inbound_advance_ invocation ──────────────
    alloc_guard_start();

    fix->feed(measured_frame);

    const long heap_allocs = alloc_guard_count();
    alloc_guard_end();
    // ── End of guarded window ─────────────────────────────────────────────────

    // Functional post-conditions:
    EXPECT_GT(app->from_admin_count, from_admin_before)
        << "NoHeap: heartbeat must be delivered to fromAdmin inside the guarded window";
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "NoHeap: session must stay Active after persist";
    EXPECT_EQ(store->write_count, write_count_before + 1)
        << "NoHeap: persist_inbound_advance_ must have fired (write_count+1)";

    // No-heap post-condition.
    // Under mallocnesia (LD_PRELOAD): heap_allocs must be 0.
    // Without LD_PRELOAD: heap_allocs is 0 (no-op weak symbol) — passes vacuously.
    // The BINDING proof is the session_persistent_seqnum_hydrate_mallocnesia ctest companion.
    EXPECT_EQ(heap_allocs, 0L)
        << "[const §VIII.5]: persist_inbound_advance_ must not touch the global heap; "
           "heap_allocs="
        << heap_allocs
        << ". Run under LD_PRELOAD=tools/mallocnesia/libmallocnesia.so for the binding proof. "
           "[[feedback_tracking_pmr_resource_false_pass]]";
}

// ── gate-b/r1 counter-tests: INV-H1 over-persist guards ─────────────────────
//
// These three tests (+ the W9b extension above) witness the two over-persist
// sub-paths fixed by gate-b/r1:
//   #1  received-141 reset rewinds store+manager to 1, then unconditional persist
//       pushed store 1→2 while manager=1. Fixed by: !peer_sent_reset guard.
//   #2  789 behind-side tolerance: check_inbound failure (too-high), manager stays
//       at X, but unconditional persist did store X→X+1. Fixed by: logon_inbound_advanced
//       guard (false when check_inbound failed).
//
// TDD ordering: each test was written and verified RED on the pre-fix production
// (no logon_inbound_advanced guard, no !peer_sent_reset guard), then GREEN post-fix.
//
// [029 INV-H1; triage root-cause #1 (Codex #1) + root-cause #2 (Codex #2);
//  contracts C3.1; data-model §Persist matrix]

// ── INV-H1_Initiator_PeerAck141_NoOverPersist ────────────────────────────────
//
// Initiator hydrated {in=37, out=42} (persistent store). Peer Logon-ack carries
// 141=Y at seq=37 (in-seq relative to manager=37 — so check_inbound succeeds and
// advances manager 37→38, THEN the reset arm rewinds manager+store to 1).
//
// After the reset + 030 restore (the INITIATOR twin of the acceptor W9b pin):
//   manager.next_inbound == 2  (consumed seq-37 reset-ack Logon is a surviving advance)
//   store.durable_inbound == 2 (030 persists the surviving advance through to the store).
//
// 029 over-persist (the bug this witness was born to catch): reset set store=1, then an
//   UNCONDITIONAL persist pushed store=2 while manager stayed at 1 — store(2) > manager(1),
//   a +1 with NO surviving advance → INV-H1 violated.
// 030: the consumed seq-37 reset-ack Logon IS a surviving net-advance (the identical
//   clobber to the acceptor arm, FR-009), so the dedicated 030 arm-local persist takes
//   BOTH manager and store to 2 → store(2) == manager(2). Equality, NOT over-persist.
//
// NOTE: seq must be 37 (the hydrated in-seq value). Using seq=1 would make
//   check_inbound(1) against manager=37 → too-low → fatal → Disconnected (wrong path).
// [[feedback_witness_asserts_named_postcondition_not_proxy]]
// [[feedback_unconditional_persist_at_multiexit_gate_breaks_lowerbound]]
// [029 INV-H1; triage root-cause #1 initiator arm; contracts C3.1; 030 FR-005/009]
TEST(PersistentSeqnumHydrate, INV_H1_Initiator_PeerAck141_NoOverPersist) {
    auto factory = std::make_shared<FaultStoreFactory>(/*in=*/37, /*out=*/42);
    auto fix = make_initiator(factory, /*enable_789=*/false, /*reset_on_logon=*/false);

    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::LogonSent)
        << "INV_H1_Initiator_PeerAck141 precondition: initiator must be LogonSent after open()";

    // After open(), the initiator has been hydrated: next_inbound=37.
    // Verify the store was set up correctly (2 reads from ensure_hydrated_).
    FaultStore* store = factory->last_store;
    ASSERT_NE(store, nullptr);

    // Feed peer Logon-ack with 141=Y at seq=37 (in-seq → check_inbound succeeds,
    // then reset arm fires → manager+store rewound to 1).
    // bilateral_lenient does NOT require we sent 141=Y first, so this is accepted.
    fix->feed(make_logon_reset("FIX.4.4", 37, "SRV", "CLI"));

    // Session must reach Active (141=Y bilateral_lenient: peer initiated reset, accepted).
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "INV_H1_Initiator_PeerAck141: session must reach Active after peer-ack 141=Y";

    // INV-H1 assertion (MANDATORY — not inside an if-Active guard):
    // Post-reset + 030 restore: both store and manager must equal 2.
    // 029 over-persist bug: store=2 (unconditional persist), manager=1 → INV-H1 violated.
    // 030: the consumed seq-37 reset-ack Logon survives → store=2 == manager=2 (equality). ✓
    const seqnum_t manager_ni = fix->session->seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(store->durable_inbound, fixpp::session::seqnum_t{2})
        << "INV_H1_Initiator_PeerAck141 (INV-H1 / 030 FR-005/009): store.durable_inbound must "
           "be 2 after 141=Y reset + 030 restore-to-2 of the consumed seq-37 reset-ack Logon. "
           "029 over-persist RED: durable_inbound=2 with manager=1 (no surviving advance). "
           "030 GREEN: durable_inbound=2 == manager=2 (consumed reset-ack Logon survives). "
           "durable_inbound="
        << store->durable_inbound;
    EXPECT_LE(store->durable_inbound, manager_ni)
        << "INV_H1_Initiator_PeerAck141 (INV-H1): store.durable_inbound must be <= "
           "manager.next_inbound. 029 over-persist bug: store(2) > manager(1). "
           "030: store(2) == manager(2). "
           "durable="
        << store->durable_inbound << " manager=" << manager_ni;
}

// ── L-024-2 RED harm test — initiator outbound rebase on the peer's 141=Y echo ─
//
// LIVE-FOUND BUG (RL-{QFcpp,QFj}-init, both engines, 2026-06-11). A reset_on_logon
// =true INITIATOR resets to {1,1}, emits Logon(141=Y) at 34=1 (outbound advances
// 1→2), then receives the peer's Logon ack — which QuickFIX-cpp AND QuickFIX-J
// echo with 141=Y. The peer_ack_sent_reset_flag arm (session.cpp:3185) calls
// reset_seqnums_to_one_durable(), which rebases BOTH counters to 1, so OUTBOUND
// regresses 2→1. The next outbound frame would carry a duplicate MsgSeqNum=1 (both
// real engines reject "MsgSeqNum too low"). 030 restored the INBOUND twin on this
// exact arm (session.cpp:3210, logon_inbound_advanced_init → set_next_inbound(2))
// but left the OUTBOUND twin unfixed. This asserts the CORRECT outbound==2 → RED
// on main (gets 1). DISABLED so it does not break ctest pending the 032 fix-feature.
//
// DESIGN AXIS for the 032 Gate A (do NOT assume "symmetric outbound restore" — 030's
// first prototype was a half-fix): restore-outbound-after-reset vs. skip the
// redundant ack-arm reset entirely when the echo confirms fixpp's OWN 141=Y (the
// reset already ran at open()). The latter dodges the store-reset I/O + the INV-H1
// lower-bound question, but must not regress CASE (b): reset_on_logon=false + a
// peer-SPONTANEOUS 141=Y echo + fixpp's Logon already sent at N>1 — there outbound
// must NOT become 2. Pin case (b) before touching the baseline.
// [L-024-2; sibling of 030/031; FIX-SL §4.1.1; [[feedback_witness_asserts_named_postcondition_not_proxy]]]
//
// W1 wire witness (SC-002): after Active, clear captured frames, feed a peer
// TestRequest (35=1) and assert the Heartbeat reply carries 34=2 (not a duplicate
// 34=1). RED on main (outbound rebased to 1 after 141=Y ack → next send duplicates
// 34=1). [FR-001/FR-003, SC-001/SC-002]
TEST(PersistentSeqnumHydrate, ResetOnLogon_Initiator_PeerAck141_OutboundStaysTwo) {
    auto factory = std::make_shared<FaultStoreFactory>(/*in=*/1, /*out=*/1);
    auto fix = make_initiator(factory, /*enable_789=*/false, /*reset_on_logon=*/true);

    // Precondition: reset_on_logon emitted Logon(141=Y) at 34=1 → outbound advanced to 2.
    ASSERT_EQ(fix->session->seqnum_mgr_test_access().peek_outbound(),
              fixpp::session::seqnum_t{2})
        << "precondition: a reset_on_logon initiator emits its Logon at 34=1, so the next "
           "outbound is 2 (matches the merged ResetOnLogon_Initiator_ResetsAndEmits141 unit)";

    // Peer Logon-ack echoes 141=Y at seq=1 (in-seq → check_inbound advances 1→2, then
    // the peer_ack_sent_reset_flag arm reset-rewinds both counters to 1).
    fix->feed(make_logon_reset("FIX.4.4", 1, "SRV", "CLI"));
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "session must reach Active after the peer's 141=Y echo";

    // HARM (RED on main = got 1): outbound must stay 2 — the Logon already consumed seq
    // 1, so the next send is 2. main rebases it to 1, so the next outbound frame would
    // duplicate 34=1 (QuickFIX-cpp + QuickFIX-J both reject). The 030 inbound restore at
    // session.cpp:3210 has no outbound twin.
    EXPECT_EQ(fix->session->seqnum_mgr_test_access().peek_outbound(),
              fixpp::session::seqnum_t{2})
        << "L-024-2: reset_on_logon initiator must keep outbound==2 after the peer's 141=Y "
           "echo; main rebases to 1 → next send duplicates 34=1. peek_outbound="
        << fix->session->seqnum_mgr_test_access().peek_outbound();

    // The inbound IS correctly restored by 030 — proves we reached the right arm.
    EXPECT_EQ(fix->session->seqnum_mgr_test_access().next_inbound_unsafe(),
              fixpp::session::seqnum_t{2})
        << "030 restored inbound to 2 on this arm; only the outbound twin is missing (L-024-2)";

    // SC-002 wire witness: clear captures accumulated during Logon handshake,
    // then feed an inbound TestRequest (35=1) → session emits a Heartbeat reply
    // consuming the next outbound seqnum. Assert the reply carries 34=2 (no
    // duplicate 34=1) and that no previously-emitted frame re-used 34=1.
    // RED on main: outbound rebased to 1 → Heartbeat reply goes at 34=1
    // (duplicate of the reset Logon). [FR-001/FR-003, SC-002]
    fix->clear_capture();
    // Build a peer inbound TestRequest (35=1) in-sequence at seq=2 (peer's
    // inbound consumed: Logon seq=1 → next expected inbound for fixpp is 2, but
    // the PEER's outbound seqnum has been reset and now sends at seq=2).
    // "SRV"→"CLI": sender=SRV, target=CLI (peer→fixpp direction).
    std::string test_req_extra;
    test_req_extra += "98=0\x01";
    test_req_extra += "108=0\x01";
    test_req_extra += "112=TEST\x01";
    auto test_req_frame = make_fix_frame("FIX.4.4", "1", 2, "SRV", "CLI", test_req_extra);
    fix->feed(test_req_frame);

    // Session must still be Active after the TestRequest exchange.
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "session must remain Active after handling inbound TestRequest";

    // The Heartbeat reply must have been emitted (exactly one post-Active frame).
    ASSERT_GE(fix->capture.frames.size(), 1u)
        << "a Heartbeat reply (35=0) must be emitted in response to the TestRequest";

    // Assert the Heartbeat reply carries 34=2 (NOT 34=1) — the wire witness (SC-002).
    const std::string reply_seq = extract_tag(fix->capture.frames[0], 34);
    EXPECT_EQ(reply_seq, "2")
        << "SC-002 wire witness: post-Active Heartbeat reply must carry 34=2; "
           "main rebases outbound to 1 on the peer_ack_sent_reset_flag arm → "
           "next send duplicates 34=1 (QuickFIX rejects). got 34=" << reply_seq;

    // Confirm no frame in the post-Active capture carries a duplicate 34=1.
    for (const auto& frame : fix->capture.frames) {
        const std::string seq = extract_tag(frame, 34);
        EXPECT_NE(seq, "1")
            << "SC-002: no post-Active outbound frame must carry 34=1 "
               "(that seq was consumed by the reset Logon); got duplicate 34=1";
    }
}

// ── INV_H1_Acceptor_789BehindSide_NoOverPersist ──────────────────────────────
//
// Acceptor with persistent store {in=2, out=1} + enable_next_expected_msg_seq_num=true.
// Peer Logon at seq=5 with NO 789 field (bare too-high Logon — natural behind-side trigger).
//
// check_inbound(5) against manager=2 → session_seqnum_too_high → chk failure.
// Behind-side tolerance: manager stays at 2. (No 789 field → honor block is skipped.)
//
// Post-handler state:
//   manager.next_inbound == 2 (not advanced)
//   store.durable_inbound MUST == 2 (no advance → no persist).
//
// Pre-fix (RED): logon_inbound_advanced not tracked → unconditional persist fires →
//   durable_inbound goes 1→3 (next_inbound_ seeded at 2, persist writes 2→3).
//   INV-H1 violated: store(3) > manager(2).
//   EXPECT_LT(store->durable_inbound, manager_ni) → FAIL (3 > 2). ✓ RED.
// Post-fix (GREEN): logon_inbound_advanced=false → persist skipped. durable_inbound=1<manager=2. ✓
//
// NOTE: NO 789 field in the peer Logon is required. A 789 field could:
//   - trigger the honor block → early return (X>N Logout path) before the persist site.
//   False-GREEN if the test reaches the persist ONLY via the honor-early-return
//   path rather than the intended behind-side-tolerance path.
//   [[feedback_witness_asserts_named_postcondition_not_proxy]], triage Codex #2 trap.
// [029 INV-H1; triage root-cause #2 acceptor arm; contracts C3.1]
TEST(PersistentSeqnumHydrate, INV_H1_Acceptor_789BehindSide_NoOverPersist) {
    // Store seeded {in=2, out=1}: manager starts at in=2 after hydrate.
    // We need the Logon seq to be AHEAD of manager (5 > 2 → too-high).
    // Build acceptor manually so we can feed a too-high Logon directly.
    auto factory = std::make_shared<FaultStoreFactory>(/*in=*/2, /*out=*/1);

    auto manual_fix = std::make_unique<Fixture>();
    manual_fix->cfg.role = fixpp::session::session_role::acceptor;
    manual_fix->cfg.sender_comp_id = "SRV";
    manual_fix->cfg.target_comp_id = "CLI";
    manual_fix->cfg.begin_string = "FIX.4.4";
    manual_fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    manual_fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    manual_fix->cfg.heartbeat_interval = std::chrono::seconds{30};
    manual_fix->cfg.executor_override = manual_fix->ioc.get_executor();
    manual_fix->cfg.store_factory = factory;
    manual_fix->cfg.reset_seqnum_policy_field =
        fixpp::session::reset_seqnum_policy::bilateral_lenient;
    manual_fix->cfg.enable_next_expected_msg_seq_num = true;  // knob on for behind-side tolerance
    manual_fix->cfg.reset_on_logon = false;
    manual_fix->cfg.transport_send = [&f = *manual_fix](std::span<const std::byte> data) {
        f.capture(data);
    };
    manual_fix->session =
        std::make_unique<fixpp::session::Session>(manual_fix->eng, manual_fix->cfg);

    auto open_fut = asio::co_spawn(manual_fix->ioc, manual_fix->session->open(), asio::use_future);
    manual_fix->ioc.run_for(1s);
    manual_fix->ioc.restart();
    (void)open_fut.get();

    ASSERT_EQ(manual_fix->session->state(), fixpp::session::fsm_state::NotConnected)
        << "INV_H1_Acceptor_789BehindSide precondition: acceptor must be NotConnected after open()";

    FaultStore* store = factory->last_store;
    ASSERT_NE(store, nullptr);

    // Feed peer Logon at seq=5 (too-high vs manager=2) with NO 789 field.
    // The behind-side tolerance path: check_inbound(5) fails (too-high), knob is on →
    // tolerate, manager stays at 2. Fall through toward Active.
    // NO 789 field: the honor block at line ~1980 is skipped (peer_789_present=false).
    // Pre-fix: unconditional persist fires → durable_inbound 1→3 (next_inbound_ seeded at 2,
    //   write goes 2→3), manager=2. INV-H1 violated.
    // Post-fix: logon_inbound_advanced=false → persist skipped. durable_inbound stays 1. ✓
    manual_fix->feed(make_logon("FIX.4.4", 5, "CLI", "SRV"));

    // Session must reach Active (behind-side tolerance → Active, waiting for peer resend).
    EXPECT_EQ(manual_fix->session->state(), fixpp::session::fsm_state::Active)
        << "INV_H1_Acceptor_789BehindSide: session must reach Active via behind-side tolerance";

    // INV-H1 assertion (MANDATORY — unconditional):
    // manager.next_inbound must still be 2 (check_inbound failed, no advance).
    // store.durable_inbound must be 1 (FaultStore.durable_inbound tracks the last
    //   WRITTEN value, initialized to 1 at construction; no persist write happened →
    //   it stays at 1; the seeded next_inbound_=2 is the READ value used by
    //   ensure_hydrated_, not reflected in durable_inbound until a write fires).
    // Pre-fix RED: persist fires → next_inbound_=3, durable_inbound=3 > manager=2.
    //   INV-H1 violated. EXPECT_LT(durable, manager) FAILS (3 > 2). ✓ RED.
    // Post-fix GREEN: persist skipped → durable_inbound=1 < manager=2. INV-H1 holds. ✓
    const seqnum_t manager_ni = manual_fix->session->seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(manager_ni, fixpp::session::seqnum_t{2})
        << "INV_H1_Acceptor_789BehindSide: manager.next_inbound must stay 2 "
           "(check_inbound failed, behind-side tolerance did not advance). "
           "Got "
        << manager_ni;
    // The critical INV-H1 check: store < manager (strict — no write happened at all).
    // Pre-fix: durable_inbound=3 > manager=2 → LT assertion FAILS. ✓ RED.
    // Post-fix: durable_inbound=1 < manager=2 → LT assertion PASSES. ✓ GREEN.
    EXPECT_LT(store->durable_inbound, manager_ni)
        << "INV_H1_Acceptor_789BehindSide (INV-H1 pre-fix RED): "
           "store < manager on behind-side path (no advance → no persist). "
           "Pre-fix: durable_inbound=3 > manager=2 (persist over-fired). "
           "Post-fix: durable_inbound=1 < manager=2. "
           "durable="
        << store->durable_inbound << " manager=" << manager_ni;
    // Secondary: no write at all → durable stays at FaultStore initial value (1).
    EXPECT_EQ(store->durable_inbound, fixpp::session::seqnum_t{1})
        << "INV_H1_Acceptor_789BehindSide (INV-H1): no persist write → "
           "durable_inbound stays at initial value 1. "
           "Pre-fix RED: durable_inbound=3. Post-fix GREEN: durable_inbound=1. "
           "durable="
        << store->durable_inbound;
}

// ── INV_H1_Initiator_789BehindSide_NoOverPersist ─────────────────────────────
//
// Initiator with persistent store {in=2, out=1} + enable_next_expected_msg_seq_num=true.
// Peer Logon-ack at seq=5 with NO 789 field (bare too-high Logon-ack).
//
// check_inbound(5) against manager=2 → too-high → behind-side tolerance →
// manager stays at 2. NO 789 field → initiator honor block (~line 3232) is skipped.
//
// Pre-fix (RED): logon_inbound_advanced_init not tracked → unconditional persist fires →
//   durable_inbound goes 1→3 (next_inbound_ seeded at 2, persist writes 2→3).
//   INV-H1 violated: store(3) > manager(2).
//   EXPECT_LT(store->durable_inbound, manager_ni) → FAIL (3 > 2). ✓ RED.
// Post-fix (GREEN): logon_inbound_advanced_init=false → persist skipped. durable_inbound=1<2. ✓
//
// [029 INV-H1; triage root-cause #2 initiator arm; contracts C3.1]
TEST(PersistentSeqnumHydrate, INV_H1_Initiator_789BehindSide_NoOverPersist) {
    // Store seeded {in=2, out=1}: manager starts at in=2 after hydrate.
    auto factory = std::make_shared<FaultStoreFactory>(/*in=*/2, /*out=*/1);
    // enable_789=true: behind-side tolerance requires the knob.
    auto fix = make_initiator(factory, /*enable_789=*/true, /*reset_on_logon=*/false);

    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::LogonSent)
        << "INV_H1_Initiator_789BehindSide precondition: initiator must be LogonSent after open()";

    FaultStore* store = factory->last_store;
    ASSERT_NE(store, nullptr);

    // After open() + hydrate, the initiator has next_inbound=2 (seeded).
    // The initiator outbound was seeded at 1, Logon emitted at seq=1, next_outbound=2.

    // Feed peer Logon-ack at seq=5 (too-high vs manager=2) with NO 789 field.
    // (The Logon-ack comes from "SRV" → "CLI", sender=SRV, target=CLI).
    // Behind-side tolerance: check_inbound(5) fails (too-high), knob is on → tolerate.
    // NO 789 field: initiator honor block is skipped (peer's next_expected_present=false).
    // Pre-fix: unconditional persist fires → durable_inbound 1→3 (next_inbound_ seeded at 2,
    //   write goes 2→3), manager=2. INV-H1 violated.
    // Post-fix: logon_inbound_advanced_init=false → persist skipped. durable_inbound stays 1. ✓
    fix->feed(make_logon("FIX.4.4", 5, "SRV", "CLI"));

    // Session must reach Active (behind-side tolerance → Active).
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "INV_H1_Initiator_789BehindSide: session must reach Active via behind-side tolerance";

    // INV-H1 assertion (MANDATORY — unconditional):
    // manager.next_inbound must stay 2 (behind-side: check_inbound failed, no advance).
    // store.durable_inbound must be 1 (no write happened; FaultStore.durable_inbound
    //   initialized to 1 at construction; seeded next_inbound_=2 is the READ value
    //   returned by ensure_hydrated_, not a write into durable_inbound).
    // Pre-fix RED: persist fires → durable_inbound=3 > manager=2. INV-H1 violated.
    //   EXPECT_LT(durable, manager) FAILS. ✓ RED.
    // Post-fix GREEN: durable_inbound=1 < manager=2. INV-H1 holds. ✓
    const seqnum_t manager_ni = fix->session->seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(manager_ni, fixpp::session::seqnum_t{2})
        << "INV_H1_Initiator_789BehindSide: manager.next_inbound must stay 2 "
           "(check_inbound failed, behind-side tolerance). Got "
        << manager_ni;
    // Critical INV-H1 check: store < manager. Pre-fix FAILS (3 > 2). Post-fix PASSES (1 < 2).
    EXPECT_LT(store->durable_inbound, manager_ni)
        << "INV_H1_Initiator_789BehindSide (INV-H1 pre-fix RED): "
           "store < manager on behind-side path (no advance → no persist). "
           "Pre-fix: durable_inbound=3 > manager=2 (persist over-fired). "
           "Post-fix: durable_inbound=1 < manager=2. "
           "durable="
        << store->durable_inbound << " manager=" << manager_ni;
    // Secondary: no write → durable stays at initial value 1.
    EXPECT_EQ(store->durable_inbound, fixpp::session::seqnum_t{1})
        << "INV_H1_Initiator_789BehindSide (INV-H1): no persist write → "
           "durable_inbound stays at initial value 1. "
           "Pre-fix RED: durable_inbound=3. Post-fix GREEN: durable_inbound=1. "
           "durable="
        << store->durable_inbound;
}

// ── T004 W8: hydrated initiator latch counterexample (RC1 guard) ─────────────
//
// A hydrated initiator seeded {next_inbound=37, next_outbound=1} with
// reset_on_logout=true and reset_on_logon=false. Because next_inbound==37≠1 at
// emit time, seqnums_at_one=false → initr_reset_seqnum=false → latch=false.
// Fixpp emits its Logon WITHOUT 141=Y.
//
// The peer then spontaneously sends a Logon with 141=Y at seq=37 (in-sequence
// against manager=37). bilateral_lenient accepts this without requiring we sent
// 141=Y first.
//
// Expected post-conditions:
//   - outbound STAYS at post-reset baseline (NOT restored to 2): latch=false →
//     restore gate (C1) skipped. The reset rewinds outbound to 1; no restore.
//   - by_peer_request=true: latch=false → we_initiated=false → by_peer_request=true.
//   - Session reaches Active.
//
// This is GREEN-on-main (main has no restore path at all) and GREEN on the shipped
// fix. It discriminates ONLY against the rejected reconstruction gate
// `we_initiated := (any_reset_knob && reset_before_send)` — T010a(b) mutation —
// which would misclassify this row as fixpp-initiated because any_reset_knob=true
// (reset_on_logout is set) and reset_before_send=true (outbound was 1 at emit →
// n_pre_outbound=2 == seqnum_min+1). The latched emit-time fact (false: inbound≠1
// at emit → seqnums_at_one=false) is the ONLY gate that correctly rejects the
// reconstruction. [032 contract C4, FR-005/FR-006, research R3, W8]
//
// NOTE: feed at seq=37 (in-seq for the hydrated manager=37). seq=1 would be
// too-low → fatal → wrong path.
// [[feedback_witness_asserts_named_postcondition_not_proxy]] (distinct seed from asserted)
TEST(PersistentSeqnumHydrate, W8_HydratedInitiator_ResetOnLogout_PeerSpontaneous141Y) {
    // Store seeded {in=37, out=1}: manager hydrates to next_inbound=37 after open().
    // reset_on_logout=true, reset_on_logon=false, bilateral_lenient.
    // seqnums_at_one = (next_outbound==1 && next_inbound==1) = false (next_inbound=37≠1)
    // → initr_reset_seqnum=false → latch=false.
    auto factory = std::make_shared<FaultStoreFactory>(/*in=*/37, /*out=*/1);

    auto manual_fix = std::make_unique<Fixture>();
    manual_fix->cfg.role = fixpp::session::session_role::initiator;
    manual_fix->cfg.sender_comp_id = "CLI";
    manual_fix->cfg.target_comp_id = "SRV";
    manual_fix->cfg.begin_string = "FIX.4.4";
    manual_fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    manual_fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    manual_fix->cfg.heartbeat_interval = std::chrono::seconds{0};
    manual_fix->cfg.executor_override = manual_fix->ioc.get_executor();
    manual_fix->cfg.store_factory = factory;
    manual_fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    manual_fix->cfg.reset_on_logon = false;
    manual_fix->cfg.reset_on_logout = true;  // reset_on_logout=true: any_reset_knob=true
    manual_fix->cfg.transport_send = [&f = *manual_fix](std::span<const std::byte> data) {
        f.capture(data);
    };
    manual_fix->session =
        std::make_unique<fixpp::session::Session>(manual_fix->eng, manual_fix->cfg);

    auto open_fut = asio::co_spawn(manual_fix->ioc, manual_fix->session->open(), asio::use_future);
    manual_fix->ioc.run_for(2s);
    manual_fix->ioc.restart();
    (void)open_fut.get();

    ASSERT_EQ(manual_fix->session->state(), fixpp::session::fsm_state::LogonSent)
        << "T004 W8 precondition: initiator must be LogonSent after open()";

    // Verify the initiator did NOT send 141=Y in its Logon (latch=false evidence).
    // The outbound Logon is the first captured frame.
    ASSERT_GE(manual_fix->capture.frames.size(), 1u) << "T004: expected outbound Logon frame";
    {
        const auto& logon_frame = manual_fix->capture.frames[0];
        const auto* data = reinterpret_cast<const char*>(logon_frame.data());
        const std::string sv(data, logon_frame.size());
        EXPECT_EQ(sv.find("141=Y"), std::string::npos)
            << "T004 W8 precondition: hydrated {37,1} initiator must NOT emit 141=Y "
               "(seqnums_at_one=false → initr_reset_seqnum=false → latch=false)";
    }

    // Feed peer spontaneous Logon with 141=Y at seq=37 (in-sequence).
    // bilateral_lenient: peer-spontaneous 141=Y is accepted without requiring fixpp to have
    // sent it first.
    manual_fix->feed(make_logon_reset("FIX.4.4", 37, "SRV", "CLI"));

    // Session must reach Active.
    EXPECT_EQ(manual_fix->session->state(), fixpp::session::fsm_state::Active)
        << "T004 W8: session must reach Active after peer-spontaneous 141=Y (bilateral_lenient)";

    // C4: by_peer_request=true (latch=false → we_initiated=false).
    {
        auto events = manual_fix->session->recent_events();
        bool found = false;
        for (const auto& ev : events) {
            if (const auto* r =
                    std::get_if<fixpp::session::session_event_sequence_numbers_reset>(&ev)) {
                found = true;
                EXPECT_TRUE(r->by_peer_request)
                    << "T004 W8: hydrated {37,1} with reset_on_logout=true — latch=false "
                       "(inbound≠1 at emit → seqnums_at_one=false) → by_peer_request must "
                       "be true. A reconstruction gate (any_reset_knob && reset_before_send) "
                       "would yield false here — RC1 counterexample. [032 contract C4, W8]";
            }
        }
        EXPECT_TRUE(found)
            << "T004 W8: sequence_numbers_reset event must be emitted on peer-spontaneous arm.";
    }

    // C1: outbound NOT restored to 2 (latch=false → restore gate skipped).
    // After the reset, outbound rewinds to 1. No restore means it stays 1.
    // A reconstruction gate would restore to 2 — the RC1 wrong behavior.
    EXPECT_EQ(manual_fix->session->seqnum_mgr_test_access().next_outbound_unsafe(),
              fixpp::session::seqnum_t{1})
        << "T004 W8: hydrated {37,1} initiator — latch=false → outbound MUST stay 1 "
           "after peer-spontaneous 141=Y (no restore). RC1 reconstruction would "
           "restore to 2. [032 contract C1, FR-005, W8]";
}

// ── T014 W5/W6: persistent store interaction on the outbound restore arm ──────
//
// W5 — persistent-store outbound write failure → fatal-when-persistent (Disconnected).
// W6 — persistent-store success → store_outbound == manager_outbound (INV-H1 outbound).
//
// Stimulus: reset_on_logon=true initiator {in=1, out=1} — latch=true, Logon at seq=1
//           → n_pre_outbound==2 == seqnum_min+1 → restore gate fires.
//
// W5 seed: fail_on_nth_outbound_write=1 (first outbound write = persist_outbound_advance_
//          fails) → reset_seqnums_to_one_durable already succeeded (store.reset() ran)
//          → persist_outbound_advance_ fails → fatal → Disconnected.
// W6 seed: no failure; the arm calls:
//   1. reset_seqnums_to_one_durable() → store.reset() → durable_outbound=1
//   2. set_next_outbound(2) → manager=2
//   3. persist_outbound_advance_() → next_seqnum(outbound,true) → durable_outbound=2
//   After Active: durable_outbound==2==manager (INV-H1).
//   Discriminating: if persist did NOT fire → durable_outbound stays 1 (post-reset
//   base) ≠ 2 → FAIL. The 1-vs-2 gap is naturally falsifying.
//   [[feedback_witness_asserts_named_postcondition_not_proxy]]: note that the arm's
//   own store.reset() clears any pre-set sentinel, so the post-reset base (1) IS the
//   discriminating seed — a separately injected sentinel would be overwritten.
//
// NOTE: Seed inbound=1 (so Logon-ack seq=1 is in-sequence) and outbound=1 (so
// emit_initiator_logon_ emits at seq=1 → n_pre_outbound=2 → restore_before_send=true).
// [032 contract C1/C3, FR-007, INV-H1]
TEST(PersistentSeqnumHydrate, T014_W5_OutboundPersistFail_Fatal) {
    // W5: fail_on_nth_outbound_write=1 → first outbound write fails → Disconnected.
    // The FaultStore initial durable_outbound=1 (not the sentinel — the sentinel applies
    // to the discriminating check, not the initial value).
    // Seed {in=1, out=1} so Logon emits at seq=1 (latch=true, restore_before_send=true).
    auto factory = std::make_shared<FaultStoreFactory>(
        /*seeded_inbound=*/1, /*seeded_outbound=*/1,
        /*fail_on_nth_call=*/0, /*fail_on_nth_write=*/0,
        /*fail_on_nth_outbound_write=*/1);
    auto fix = make_initiator(factory, /*enable_789=*/false, /*reset_on_logon=*/true);

    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::LogonSent)
        << "T014 W5 precondition: initiator must be LogonSent after open()";

    FaultStore* store = factory->last_store;
    ASSERT_NE(store, nullptr);

    // Feed peer Logon-ack with 141=Y at seq=1 (in-seq → restore gate fires).
    fix->feed(make_logon_reset("FIX.4.4", 1, "SRV", "CLI"));

    // W5: outbound persist failure → fatal-when-persistent → Disconnected.
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
        << "T014 W5: persist_outbound_advance_() failure on a persistent store must be "
           "fatal → Disconnected. [032 contract C3, FR-007, 030 fatal-when-persistent]";
}

TEST(PersistentSeqnumHydrate, T014_W6_OutboundPersistSuccess_InvH1) {
    // W6: no failure injection. After Active, assert INV-H1:
    //   durable_outbound == manager.next_outbound (equality at 2)
    //   never durable_outbound > manager.next_outbound.
    //
    // Sentinel: seed outbound=1 so the store starts next_outbound_=1.
    // After persist_outbound_advance_() fires, next_outbound_ becomes 2 and
    // durable_outbound is updated to 2. The manager also has next_outbound=2.
    // We use a large seeded_inbound value to confirm we're testing the right arm:
    // {in=1, out=1} → after hydration manager starts at {1,1}; reset_on_logon=true
    // resets to {1,1} again (no-op), Logon emits at seq=1 → n_pre_outbound=2 → restore fires.
    auto factory = std::make_shared<FaultStoreFactory>(
        /*seeded_inbound=*/1, /*seeded_outbound=*/1,
        /*fail_on_nth_call=*/0, /*fail_on_nth_write=*/0,
        /*fail_on_nth_outbound_write=*/0);
    auto fix = make_initiator(factory, /*enable_789=*/false, /*reset_on_logon=*/true);

    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::LogonSent)
        << "T014 W6 precondition: initiator must be LogonSent after open()";

    FaultStore* store = factory->last_store;
    ASSERT_NE(store, nullptr);

    // Feed peer Logon-ack with 141=Y at seq=1.
    // The arm runs: reset_seqnums_to_one_durable() → store.reset() → durable_outbound=1,
    // then set_next_outbound(2) → manager=2, then persist_outbound_advance_() → durable=2.
    fix->feed(make_logon_reset("FIX.4.4", 1, "SRV", "CLI"));

    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "T014 W6: session must reach Active after peer 141=Y ack";

    // INV-H1 (outbound): store.durable_outbound == manager.next_outbound after restore.
    const seqnum_t manager_no = fix->session->seqnum_mgr_test_access().next_outbound_unsafe();

    // Manager must be 2 (set_next_outbound(seqnum_min+1) = set_next_outbound(2)).
    EXPECT_EQ(manager_no, fixpp::session::seqnum_t{2})
        << "T014 W6: manager.next_outbound must be 2 after restore "
           "(set_next_outbound(seqnum_min+1)). [032 contract C1, FR-001/FR-003]";

    // Store must equal manager (INV-H1 outbound equality: persist ran → durable=2).
    // If persist did not fire: durable stays 1 (post-reset base from store.reset()) ≠ 2
    // → this assertion FAILS. Naturally falsifying (no external sentinel needed because
    // store.reset() on the arm already resets durable_outbound to 1 before persist).
    EXPECT_EQ(store->durable_outbound, fixpp::session::seqnum_t{2})
        << "T014 W6 (INV-H1 outbound): durable_outbound must be 2 after "
           "persist_outbound_advance_(). If persist did not fire, "
           "durable=1 (post-reset base) ≠ 2 → FAIL. [032 contract C3, INV-H1, FR-007]";

    // INV-H1: never over-persist (store ≤ manager).
    EXPECT_LE(store->durable_outbound, manager_no)
        << "T014 W6 (INV-H1): store.durable_outbound must be ≤ manager.next_outbound. "
           "[INV-H1, 032 contract C3]";
}

}  // namespace
